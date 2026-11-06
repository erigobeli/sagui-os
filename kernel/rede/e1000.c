/* e1000.c - driver da placa Intel PRO/1000 (82540EM e parentes). Ver D032.
 *
 * É a placa que o QEMU emula por padrão e a do VirtualBox ("Intel PRO/1000
 * MT Desktop"). A placa conversa com a memória por dois anéis de
 * descritores de 16 bytes, um de recepção e um de envio:
 *   - recepção: cada descritor aponta para um buffer de 2 KB; a placa grava o
 *     quadro, liga o bit DD ("feito") e avança RDH. O driver consome, desliga
 *     o DD e devolve o descritor escrevendo o índice em RDT;
 *   - envio: o driver preenche o descritor seguinte com o endereço e o
 *     tamanho do quadro e avança TDT; a placa manda e liga o DD.
 * Sem interrupções da placa: a tarefa da rede consulta o anel a cada tique.
 * Registradores e formatos: manual do desenvolvedor dos controladores
 * Intel 8254x. O kernel enxerga a memória física 1:1 (D008), então o
 * endereço dos buffers é o próprio ponteiro. */
#include "rede.h"
#include "pci.h"
#include "console.h"
#include "memoria.h"
#include "paginacao.h"
#include "portas.h"
#include "texto.h"

#define CTRL    0x0000
#define STATUS  0x0008
#define EERD    0x0014
#define ICR     0x00C0
#define IMC     0x00D8
#define RCTL    0x0100
#define TCTL    0x0400
#define TIPG    0x0410
#define RDBAL   0x2800
#define RDBAH   0x2804
#define RDLEN   0x2808
#define RDH     0x2810
#define RDT     0x2818
#define TDBAL   0x3800
#define TDBAH   0x3804
#define TDLEN   0x3808
#define TDH     0x3810
#define TDT     0x3818
#define MTA     0x5200
#define RAL0    0x5400
#define RAH0    0x5404

#define CTRL_ASDE  (1u << 5)
#define CTRL_SLU   (1u << 6)
#define CTRL_RST   (1u << 26)
#define RCTL_EN    (1u << 1)
#define RCTL_BAM   (1u << 15)    /* aceita broadcast (ARP, DHCP) */
#define RCTL_SECRC (1u << 26)    /* a placa tira o CRC */
#define TCTL_EN    (1u << 1)
#define TCTL_PSP   (1u << 3)     /* completa quadros curtos até 60 bytes */
#define CMD_EOP    0x01
#define CMD_IFCS   0x02
#define CMD_RS     0x08
#define DD         0x01
#define EOP        0x02

#define N_RX   64                 /* RDLEN precisa ser múltiplo de 128 bytes */
#define N_TX   16
#define BUF    2048

static volatile uint8_t *regs;
static uint8_t *rx, *tx;          /* anéis de descritores */
static uint8_t *rx_buf, *tx_buf;
static uint32_t rx_prox, tx_prox;

static inline uint32_t le(uint32_t r) { return *(volatile uint32_t *)(regs + r); }
static inline void escreve(uint32_t r, uint32_t v) { *(volatile uint32_t *)(regs + r) = v; }

static bool enviar(const void *quadro, uint32_t n)
{
    if (n > ETH_MAX) return false;
    uint8_t *d = tx + tx_prox * 16;
    if (!(((volatile uint8_t *)d)[12] & DD)) return false;   /* a placa ainda não mandou este */
    memcpy(tx_buf + tx_prox * BUF, quadro, n);
    *(uint32_t *)(d + 0) = (uint32_t)(tx_buf + tx_prox * BUF);
    *(uint32_t *)(d + 4) = 0;
    *(uint16_t *)(d + 8) = (uint16_t)n;
    d[10] = 0;
    d[11] = CMD_EOP | CMD_IFCS | CMD_RS;
    ((volatile uint8_t *)d)[12] = 0;
    tx_prox = (tx_prox + 1) % N_TX;
    __asm__ volatile("" ::: "memory");   /* descritor completo antes de avisar a placa */
    escreve(TDT, tx_prox);
    return true;
}

static int receber(void *destino)
{
    volatile uint8_t *d = rx + rx_prox * 16;
    uint8_t st = d[12];
    if (!(st & DD)) return 0;
    int n = *(volatile uint16_t *)(d + 8);
    /* Quadro com erro ou maior que um buffer (não deveria vir: LPE desligado). */
    if (d[13] || !(st & EOP) || n > ETH_MAX) n = -1;
    else memcpy(destino, rx_buf + rx_prox * BUF, (uint32_t)n);
    d[12] = 0;
    escreve(RDT, rx_prox);   /* devolve o descritor à placa */
    rx_prox = (rx_prox + 1) % N_RX;
    return n;
}

static bool cabo(void) { return le(STATUS) & 2; }

static uint16_t eeprom(uint8_t endereco)
{
    escreve(EERD, ((uint32_t)endereco << 8) | 1);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = le(EERD);
        if (v & (1u << 4)) return (uint16_t)(v >> 16);
    }
    return 0;
}

/* Modelos com o mesmo conjunto de registradores do 82540EM. O 82574L
 * (e1000e do QEMU) e as placas PCIe mais novas são diferentes. */
static bool modelo_conhecido(uint16_t m)
{
    return m == 0x100E || m == 0x100F || m == 0x1004 || m == 0x1015 || m == 0x1019 || m == 0x101E;
}

bool e1000_procurar(placa_t *p)
{
    pci_disp_t achados[4];
    int n = pci_procurar(0x02, 0x00, 0x00, achados, 4);   /* rede, Ethernet */
    pci_disp_t *d = NULL;
    for (int i = 0; i < n && !d; i++)
        if (achados[i].fabricante == 0x8086 && modelo_conhecido(achados[i].modelo)) d = &achados[i];
    if (!d) return false;

    uint32_t bar = pci_le32(d, 0x10);
    if (bar & 1) return false;                              /* precisa ser memória, não porta */
    pci_escreve32(d, 0x04, pci_le32(d, 0x04) | 0x6);        /* memória + mestre do barramento (DMA) */
    regs = (volatile uint8_t *)janela_mapear(bar & ~0xFu, 128 * 1024);
    uint32_t desc = memfisica_alocar();
    uint32_t brx = memfisica_alocar_seguidas(N_RX * BUF / PAGINA);
    uint32_t btx = memfisica_alocar_seguidas(N_TX * BUF / PAGINA);
    if (!regs || !desc || !brx || !btx) { klog("[E1000] sem memoria\n"); return false; }
    rx = (uint8_t *)desc;
    tx = rx + N_RX * 16;
    rx_buf = (uint8_t *)brx;
    tx_buf = (uint8_t *)btx;
    memset(rx, 0, PAGINA);

    escreve(IMC, 0xFFFFFFFFu);
    escreve(CTRL, le(CTRL) | CTRL_RST);
    for (int i = 0; i < 10000 && (le(CTRL) & CTRL_RST); i++) porta_pausa();
    escreve(IMC, 0xFFFFFFFFu);                               /* o reset religa o que estava desligado */
    (void)le(ICR);
    /* Liga o link e desliga os resets do PHY e o "sinal invertido". */
    escreve(CTRL, (le(CTRL) | CTRL_SLU | CTRL_ASDE) & ~((1u << 3) | (1u << 7) | (1u << 31)));

    /* O MAC vem da EEPROM; a placa costuma copiá-lo sozinha para RAL/RAH. */
    uint32_t ral = le(RAL0), rah = le(RAH0);
    if (!(rah & (1u << 31))) {
        uint16_t w0 = eeprom(0), w1 = eeprom(1), w2 = eeprom(2);
        ral = w0 | (uint32_t)w1 << 16;
        rah = w2 | (1u << 31);
        escreve(RAL0, ral);
        escreve(RAH0, rah);
    }
    for (int i = 0; i < 4; i++) p->mac[i] = (uint8_t)(ral >> (8 * i));
    p->mac[4] = (uint8_t)rah;
    p->mac[5] = (uint8_t)(rah >> 8);
    for (int i = 0; i < 128; i++) escreve(MTA + 4 * i, 0);

    for (int i = 0; i < N_RX; i++) *(uint32_t *)(rx + i * 16) = (uint32_t)(rx_buf + i * BUF);
    escreve(RDBAL, (uint32_t)rx);
    escreve(RDBAH, 0);
    escreve(RDLEN, N_RX * 16);
    escreve(RDH, 0);
    escreve(RDT, N_RX - 1);
    escreve(RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);          /* buffers de 2048 bytes (BSIZE = 0) */

    for (int i = 0; i < N_TX; i++) tx[i * 16 + 12] = DD;     /* todos livres */
    escreve(TDBAL, (uint32_t)tx);
    escreve(TDBAH, 0);
    escreve(TDLEN, N_TX * 16);
    escreve(TDH, 0);
    escreve(TDT, 0);
    escreve(TCTL, TCTL_EN | TCTL_PSP | (0x0Fu << 4) | (0x40u << 12));   /* repetições e distância de colisão */
    escreve(TIPG, 0x0060200A);                               /* intervalo entre quadros recomendado (cobre) */
    rx_prox = tx_prox = 0;

    p->nome = "Intel PRO/1000 (e1000)";
    p->enviar = enviar;
    p->receber = receber;
    p->cabo = cabo;
    klog("[E1000] %02x:%02x.%x (%04x:%04x) em %x, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
         d->barramento, d->dispositivo, d->funcao, d->fabricante, d->modelo, bar & ~0xFu,
         p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5]);
    return true;
}
