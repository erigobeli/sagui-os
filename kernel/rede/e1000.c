/* e1000.c - placa de rede Intel PRO/1000 (família 8254x) (H43).
 *
 * É a placa que o QEMU e o VirtualBox simulam, e o manual dela é público. A
 * placa conversa com o sistema por dois anéis de "descritores" na memória:
 *   - recepção: cada descritor aponta para um buffer de 2 KB; a placa enche o
 *     buffer com um quadro que chegou e marca "pronto" (DD) no descritor;
 *   - envio: cada descritor aponta para um quadro a mandar; a placa manda e
 *     marca "feito".
 * Em cada anel há uma "cabeça" (até onde a placa já foi) e uma "cauda" (até
 * onde o sistema já preparou). O driver não usa interrupções: a tarefa da
 * rede pergunta a cada tique se chegou alguma coisa. */
#include "rede.h"
#include "pci.h"
#include "console.h"
#include "cpu.h"
#include "memoria.h"
#include "paginacao.h"
#include "texto.h"
#include "portas.h"

#define CTRL   0x0000
#define STATUS 0x0008
#define EERD   0x0014
#define ICR    0x00C0
#define IMC    0x00D8
#define RCTL   0x0100
#define TCTL   0x0400
#define TIPG   0x0410
#define RDBAL  0x2800
#define RDBAH  0x2804
#define RDLEN  0x2808
#define RDH    0x2810
#define RDT    0x2818
#define TDBAL  0x3800
#define TDBAH  0x3804
#define TDLEN  0x3808
#define TDH    0x3810
#define TDT    0x3818
#define MTA    0x5200
#define RAL0   0x5400
#define RAH0   0x5404

#define QTD_RX 32
#define QTD_TX 16
#define BUF    2048

typedef struct {
    uint64_t endereco;
    uint16_t tamanho, soma;
    uint8_t  estado, erros;
    uint16_t especial;
} __attribute__((packed)) desc_rx_t;

typedef struct {
    uint64_t endereco;
    uint16_t tamanho;
    uint8_t  cso, comando, estado, css;
    uint16_t especial;
} __attribute__((packed)) desc_tx_t;

static volatile uint8_t *regs;
static desc_rx_t *rx;
static desc_tx_t *tx;
static uint8_t *buf_rx, *buf_tx;
static uint32_t rx_prox, tx_prox;
static placa_t placa;

static inline uint32_t r32(uint32_t o) { return *(volatile uint32_t *)(regs + o); }
static inline void w32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(regs + o) = v; }

static bool enviar(placa_t *p, const void *quadro, uint32_t tam)
{
    (void)p;
    if (tam > BUF) return false;
    desc_tx_t *d = &tx[tx_prox];
    if (!(d->estado & 1) && d->comando) return false;   /* anel cheio: a placa ainda não mandou este */
    memcpy(buf_tx + tx_prox * BUF, quadro, tam);
    if (tam < 60) { memset(buf_tx + tx_prox * BUF + tam, 0, 60 - tam); tam = 60; }   /* tamanho mínimo */
    d->endereco = (uint32_t)(buf_tx + tx_prox * BUF);
    d->tamanho = (uint16_t)tam;
    d->estado = 0;
    d->comando = 0x01 | 0x02 | 0x08;                     /* fim do quadro, soma Ethernet, avisar */
    tx_prox = (tx_prox + 1) % QTD_TX;
    w32(TDT, tx_prox);
    return true;
}

static int receber(placa_t *p, void *quadro, uint32_t max)
{
    (void)p;
    desc_rx_t *d = &rx[rx_prox];
    if (!(d->estado & 1)) return 0;                       /* nada pronto */
    int n = 0;
    if ((d->estado & 2) && !d->erros) {                   /* quadro inteiro e sem erro */
        n = d->tamanho > max ? (int)max : d->tamanho;
        memcpy(quadro, buf_rx + rx_prox * BUF, (uint32_t)n);
    }
    d->estado = 0;
    w32(RDT, rx_prox);                                    /* devolve o descritor à placa */
    rx_prox = (rx_prox + 1) % QTD_RX;
    return n ? n : -1;                                    /* -1: chegou algo, mas inválido */
}

static bool cabo(placa_t *p) { (void)p; return r32(STATUS) & 2; }

/* Lê uma palavra da EEPROM (onde fica o endereço físico da placa). */
static uint16_t eeprom(uint8_t endereco)
{
    w32(EERD, 1u | ((uint32_t)endereco << 8));
    for (int i = 0; i < 100000; i++) {
        uint32_t v = r32(EERD);
        if (v & (1u << 4)) return (uint16_t)(v >> 16);
    }
    return 0;
}

placa_t *e1000_iniciar(void)
{
    static const uint16_t modelos[] = { 0x100E, 0x100F, 0x1004, 0x100C, 0x1015, 0x1019, 0x101E, 0x1026, 0x1076, 0x107C };
    pci_disp_t todos[PCI_MAX];
    int n = pci_listar(todos, PCI_MAX);
    pci_disp_t *d = NULL;
    for (int i = 0; i < n && !d; i++)
        if (todos[i].fabricante == 0x8086 && todos[i].classe == 0x02)
            for (unsigned k = 0; k < sizeof modelos / sizeof modelos[0]; k++)
                if (todos[i].modelo == modelos[k]) d = &todos[i];
    if (!d) return NULL;
    uint32_t bar = pci_le32(d, 0x10) & ~0xFu;
    pci_escreve32(d, 0x04, pci_le32(d, 0x04) | 0x6);     /* memória + mestre (DMA) */
    regs = (volatile uint8_t *)janela_mapear(bar, 0x20000);
    if (!regs) return NULL;

    w32(IMC, 0xFFFFFFFFu);                                /* sem interrupções */
    w32(CTRL, r32(CTRL) | (1u << 26));                    /* reset */
    for (volatile int i = 0; i < 100000; i++) ;
    for (int i = 0; i < 1000 && (r32(CTRL) & (1u << 26)); i++) for (volatile int k = 0; k < 1000; k++) ;
    w32(IMC, 0xFFFFFFFFu);
    r32(ICR);
    w32(CTRL, (r32(CTRL) | (1u << 6)) & ~(1u << 3));      /* SLU: liga o enlace; tira o reset do PHY */

    /* Endereço físico: o que a placa já tem no filtro 0 ou, se não, a EEPROM. */
    uint32_t ral = r32(RAL0), rah = r32(RAH0);
    if (rah & (1u << 31)) {
        for (int i = 0; i < 4; i++) placa.mac[i] = (uint8_t)(ral >> (8 * i));
        placa.mac[4] = (uint8_t)rah; placa.mac[5] = (uint8_t)(rah >> 8);
    } else {
        for (int i = 0; i < 3; i++) {
            uint16_t w = eeprom((uint8_t)i);
            placa.mac[2 * i] = (uint8_t)w; placa.mac[2 * i + 1] = (uint8_t)(w >> 8);
        }
        w32(RAL0, (uint32_t)placa.mac[0] | ((uint32_t)placa.mac[1] << 8) | ((uint32_t)placa.mac[2] << 16) | ((uint32_t)placa.mac[3] << 24));
        w32(RAH0, (uint32_t)placa.mac[4] | ((uint32_t)placa.mac[5] << 8) | (1u << 31));
    }
    for (int i = 0; i < 128; i++) w32(MTA + i * 4, 0);    /* sem grupos (multicast) */

    /* Anéis e buffers: memória física contígua, abaixo de 4 GB. */
    uint32_t pag = memfisica_alocar();
    buf_rx = (uint8_t *)memfisica_alocar_seguidas(QTD_RX * BUF / PAGINA);
    buf_tx = (uint8_t *)memfisica_alocar_seguidas(QTD_TX * BUF / PAGINA);
    if (!pag || !buf_rx || !buf_tx) return NULL;
    memset((void *)pag, 0, PAGINA);
    rx = (desc_rx_t *)pag;                                /* 32 x 16 = 512 bytes */
    tx = (desc_tx_t *)(pag + 1024);                       /* 16 x 16 = 256 bytes */
    for (int i = 0; i < QTD_RX; i++) rx[i].endereco = (uint32_t)(buf_rx + i * BUF);
    for (int i = 0; i < QTD_TX; i++) tx[i].estado = 1;    /* "feito": livre para usar */
    w32(RDBAL, (uint32_t)rx); w32(RDBAH, 0);
    w32(RDLEN, QTD_RX * sizeof(desc_rx_t));
    w32(RDH, 0); w32(RDT, QTD_RX - 1);
    w32(RCTL, (1u << 1) | (1u << 15) | (1u << 26));       /* ligar, aceitar difusão, tirar a soma Ethernet */
    w32(TDBAL, (uint32_t)tx); w32(TDBAH, 0);
    w32(TDLEN, QTD_TX * sizeof(desc_tx_t));
    w32(TDH, 0); w32(TDT, 0);
    w32(TCTL, (1u << 1) | (1u << 3) | (0x10u << 4) | (0x40u << 12));   /* ligar, completar quadros curtos */
    w32(TIPG, 10 | (8u << 10) | (6u << 20));
    rx_prox = tx_prox = 0;

    snformatar(placa.nome, sizeof placa.nome, "Intel PRO/1000 (%04x)", d->modelo);
    placa.enviar = enviar;
    placa.receber = receber;
    placa.cabo = cabo;
    klog("[REDE] %s: endereco fisico %02x:%02x:%02x:%02x:%02x:%02x, cabo %s\n", placa.nome,
         placa.mac[0], placa.mac[1], placa.mac[2], placa.mac[3], placa.mac[4], placa.mac[5], cabo(&placa) ? "ligado" : "desligado");
    return &placa;
}
