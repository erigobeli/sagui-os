/* ac97.c - saída de som PCM pela placa AC'97 (Intel 82801AA e compatíveis).
 *
 * O programa manda amostras de 16 bits em estéreo. Elas vão para 32 buffers
 * fixos, que a placa lê por DMA seguindo a lista de descritores. Não há
 * interrupção: quem escreve olha qual buffer a placa está tocando e usa os
 * livres; se não houver, a chamada devolve 0 e o programa tenta depois. */
#include "ac97.h"
#include "pci.h"
#include "portas.h"
#include "console.h"
#include "memoria.h"
#include "sistema.h"
#include "texto.h"

#define NAM_RESET   0x00
#define NAM_MESTRE  0x02
#define NAM_PCM     0x18
#define NAM_EXT     0x2A
#define NAM_TAXA    0x2C
#define PO_BDL      0x10
#define PO_CIV      0x14
#define PO_LVI      0x15
#define PO_SR       0x16
#define PO_CR       0x1B
#define GLOB_CNT    0x2C
#define QTD         32
#define TAM         8192    /* bytes por buffer */
#define PAGINA_B    4096

typedef struct { uint32_t end; uint16_t amostras; uint16_t opcoes; } __attribute__((packed)) bdl_t;

static uint16_t nam, nabm;
static bdl_t *lista;
static uint8_t *dados;
static int proximo;      /* próximo buffer a preencher */
static bool tocando;

bool ac97_iniciar(void)
{
    pci_disp_t v[2];
    if (pci_procurar(0x04, 0x01, 0x00, v, 2) < 1) return false;
    nam = (uint16_t)(pci_le32(&v[0], 0x10) & ~1u);
    nabm = (uint16_t)(pci_le32(&v[0], 0x14) & ~1u);
    pci_escreve32(&v[0], 0x04, pci_le32(&v[0], 0x04) | 0x5);   /* E/S + DMA */
    uint32_t paginas = (QTD * TAM) / PAGINA_B + 1;
    uint32_t mem = memfisica_alocar_seguidas(paginas);
    if (!mem) return false;
    lista = (bdl_t *)mem;
    dados = (uint8_t *)mem + PAGINA_B;
    porta_escreve32((uint16_t)(nabm + GLOB_CNT), 2);        /* tira do reset frio */
    for (volatile int i = 0; i < 2000000; i++) {}
    porta_escreve16((uint16_t)(nam + NAM_RESET), 1);
    porta_escreve16((uint16_t)(nam + NAM_MESTRE), 0);
    porta_escreve16((uint16_t)(nam + NAM_PCM), 0x0808);
    porta_escreve16((uint16_t)(nam + NAM_EXT), 1);          /* taxa variável */
    porta_escreve8((uint16_t)(nabm + PO_CR), 2);           /* zera o canal */
    while (porta_le8((uint16_t)(nabm + PO_CR)) & 2) {}
    for (int i = 0; i < QTD; i++) {
        lista[i].end = (uint32_t)(dados + i * TAM);
        lista[i].amostras = TAM / 2;
        lista[i].opcoes = 0;
    }
    porta_escreve32((uint16_t)(nabm + PO_BDL), (uint32_t)lista);
    kprintf("ac97: placa em %x/%x\n", nam, nabm);
    return true;
}

static void parar(void)
{
    porta_escreve8((uint16_t)(nabm + PO_CR), 0);
    porta_escreve8((uint16_t)(nabm + PO_CR), 2);
    while (porta_le8((uint16_t)(nabm + PO_CR)) & 2) {}
    porta_escreve32((uint16_t)(nabm + PO_BDL), (uint32_t)lista);
    proximo = 0;
    tocando = false;
}

/* SIS_PCM (amostras, bytes, taxa): aceita até um buffer; devolve bytes aceitos.
 * Com bytes 0, para de tocar. Sem placa, devolve -1. */
int32_t sis_pcm(uint32_t p, uint32_t n, uint32_t taxa, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (!nam) return -1;
    if (!n) { if (tocando) parar(); return 0; }
    if (!usuario_ok((const void *)p, n, false)) return -1;
    if (n > TAM) n = TAM;
    n &= ~3u;
    if (!tocando) {
        porta_escreve16((uint16_t)(nam + NAM_TAXA), (uint16_t)taxa);
    } else {
        int civ = porta_le8((uint16_t)(nabm + PO_CIV)) & 31;
        if (((proximo + 1) & 31) == civ) return 0;   /* fila cheia */
    }
    memcpy(dados + proximo * TAM, (const void *)p, n);
    lista[proximo].amostras = (uint16_t)(n / 2);
    porta_escreve8((uint16_t)(nabm + PO_LVI), (uint8_t)proximo);
    proximo = (proximo + 1) & 31;
    if (!tocando) { porta_escreve8((uint16_t)(nabm + PO_CR), 1); tocando = true; }
    else if (porta_le16((uint16_t)(nabm + PO_SR)) & 1) {   /* parou por falta de dados */
        porta_escreve16((uint16_t)(nabm + PO_SR), 0x1C);
        porta_escreve8((uint16_t)(nabm + PO_CR), 1);
    }
    return (int32_t)n;
}
