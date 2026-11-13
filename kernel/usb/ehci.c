/* ehci.c - driver do controlador USB 2.0 (EHCI) (H29). Ver D025.
 *
 * O EHCI lê da memória duas listas: a "periódica" (não usada aqui) e a
 * "assíncrona", um anel de cabeças de fila (QH), uma por endpoint, cada
 * uma apontando para uma lista de descritores de transferência (qTD). Os
 * formatos de QH, qTD e dos registradores são os da especificação EHCI.
 *
 * Desenho deste driver, pensado para ser simples e previsível:
 *   - uma transferência por vez, síncrona: monta os qTDs, pendura uma QH
 *     no anel, espera o controlador terminar, tira a QH;
 *   - sem interrupções do controlador: a espera olha o bit "ativo" dos qTDs;
 *   - o tempo é medido pelo contador de microquadros do próprio controlador
 *     (FRINDEX, 8 por milissegundo), que anda mesmo com as interrupções da
 *     CPU desligadas;
 *   - os dados passam por um buffer de 64 KB em memória física contígua do
 *     kernel (o controlador precisa de endereços físicos; no SaguiOS o
 *     kernel enxerga a memória física 1:1, D008).
 * Antes de tudo, o sistema pede o controlador à BIOS (que o usava para o
 * pendrive de boot), pelo procedimento "BIOS/OS handoff" da especificação. */
#include "usb.h"
#include "console.h"
#include "cpu.h"
#include "memoria.h"
#include "paginacao.h"
#include "portas.h"
#include "texto.h"

/* Registradores de operação (deslocamento / 4) */
#define USBCMD     0
#define USBSTS     1
#define USBINTR    2
#define FRINDEX    3
#define CTRLDSSEG  4
#define PERIODIC   5
#define ASYNCLIST  6
#define CONFIGFLAG 16
#define PORTSC     17

#define CMD_RS     (1u << 0)
#define CMD_RESET  (1u << 1)
#define CMD_ASE    (1u << 5)
#define CMD_IAAD   (1u << 6)
#define STS_IAA    (1u << 5)
#define STS_HALTED (1u << 12)
#define STS_ASS    (1u << 15)

#define P_CCS  (1u << 0)
#define P_CSC  (1u << 1)
#define P_PE   (1u << 2)
#define P_PEC  (1u << 3)
#define P_OCC  (1u << 5)
#define P_PR   (1u << 8)
#define P_LS   (3u << 10)
#define P_PP   (1u << 12)
#define P_PO   (1u << 13)
#define P_MUDANCAS (P_CSC | P_PEC | P_OCC)   /* bits "escreva 1 para limpar" */

/* Formatos de 64 bits (apêndice B da especificação EHCI). Um controlador
 * que se declara capaz de endereços de 64 bits (HCCPARAMS bit 0, caso dos
 * EHCI da Intel do G470) lê SEMPRE a versão longa das estruturas: depois
 * dos 5 ponteiros de página vêm mais 5 palavras com a parte alta de cada
 * um. A primeira versão deste driver usava o formato curto, colado: no
 * G470 o controlador lia como "parte alta" os bytes da estrutura seguinte e
 * mandava os dados para endereços absurdos (erro 17). O QEMU se declara só
 * de 32 bits e não mostrava o problema. Agora as estruturas têm sempre o
 * tamanho longo, com a parte alta zerada; um controlador de 32 bits
 * simplesmente não lê o final. */

/* qTD: 13 palavras (52 bytes), ocupando 64, alinhado em 32 */
typedef struct {
    volatile uint32_t proximo, alternativo, ficha;
    volatile uint32_t buffer[5];
    volatile uint32_t buffer_alto[5];
    uint32_t preenchimento[3];
} __attribute__((aligned(32))) qtd_t;

/* QH: 17 palavras (68 bytes), ocupando 96, alinhado em 32 */
typedef struct {
    volatile uint32_t ligacao, caracteristicas, capacidades, atual;
    volatile uint32_t prox_qtd, alt_qtd, ficha;
    volatile uint32_t buffer[5];
    volatile uint32_t buffer_alto[5];
    uint32_t preenchimento[7];
} __attribute__((aligned(32))) qh_t;

_Static_assert(sizeof(qtd_t) == 64, "qTD no formato de 64 bits ocupa 64 bytes");
_Static_assert(sizeof(qh_t) == 96, "QH no formato de 64 bits ocupa 96 bytes");

#define T_TERMINA    1u
#define T_QH         (1u << 1)
#define F_ATIVO      (1u << 7)
#define F_PAROU      (1u << 6)
#define F_ERROS      0x3Cu        /* buffer, babble, transação, microquadro perdido */
#define PID_SAIDA    0u
#define PID_ENTRADA  1u
#define PID_SETUP    2u

#define MAX_QTD      8
#define QTD_MAX_BYTES 16384
#define BUFFER_BYTES  65536

static ehci_t ctls[USB_MAX_CONTROLADORES];
static int qtd_ctls;

/* Memória de trabalho. Cada controlador tem a SUA página de estruturas
 * (cabeça do anel, fila de trabalho, setup e qTDs): se dois controladores
 * apontassem para o mesmo anel, os dois executariam a mesma transferência,
 * cada um no seu barramento, e uma estragaria a outra (erro 19, G470).
 * O buffer de dados é um só: só o controlador da transferência em curso o
 * toca, e as transferências são uma por vez (interrupções desligadas).
 * Os ponteiros abaixo apontam para a página do controlador em uso. */
static qh_t *cabeca, *fila;
static qtd_t *qtds;
static uint8_t *setup_pacote;
static uint8_t *buffer;

static void usar(ehci_t *c)
{
    uint32_t p = c->estruturas;
    cabeca = (qh_t *)p;
    fila = (qh_t *)(p + 128);
    setup_pacote = (uint8_t *)(p + 256);
    qtds = (qtd_t *)(p + 512);
}

int ehci_quantidade(void) { return qtd_ctls; }
ehci_t *ehci_controlador(int i) { return i < qtd_ctls ? &ctls[i] : NULL; }

/* ------------------------------------------------------------ tempo */

static void esperar_bruto_us(uint32_t us)
{
    for (uint32_t i = 0; i < us; i++) porta_pausa();   /* ~1 µs cada, sem depender do controlador */
}

void ehci_esperar_ms(ehci_t *c, uint32_t ms)
{
    if (!c || (c->op[USBSTS] & STS_HALTED)) { esperar_bruto_us(ms * 1000); return; }
    /* FRINDEX conta microquadros (125 µs) em 14 bits. */
    uint32_t alvo = ms * 8, passou = 0, antes = c->op[FRINDEX] & 0x3FFF;
    for (uint32_t guarda = 0; passou < alvo && guarda < ms * 20000 + 1000; guarda++) {
        uint32_t agora = c->op[FRINDEX] & 0x3FFF;
        passou += (agora - antes) & 0x3FFF;
        antes = agora;
    }
}

/* ------------------------------------------------------------ inicialização */

/* Pede o controlador à BIOS: liga o bit "o sistema quer" na capacidade
 * USBLEGSUP e espera a BIOS soltar; depois desliga as interrupções SMI. */
static void tomar_da_bios(ehci_t *c)
{
    uint32_t hcc = *(volatile uint32_t *)(c->cap + 8);
    uint8_t eecp = (uint8_t)((hcc >> 8) & 0xFF);
    for (int volta = 0; eecp >= 0x40 && volta < 8; volta++) {
        uint32_t v = pci_le32(&c->pci, eecp);
        if ((v & 0xFF) == 1) {   /* USBLEGSUP */
            if (v & (1u << 16)) {
                pci_escreve32(&c->pci, eecp, v | (1u << 24));
                for (int i = 0; i < 1000 && (pci_le32(&c->pci, eecp) & (1u << 16)); i++)
                    esperar_bruto_us(1000);
                c->bios = (pci_le32(&c->pci, eecp) & (1u << 16)) ? -1 : 1;
                klog("[EHCI] BIOS %s o controlador\n", c->bios < 0 ? "NAO soltou" : "soltou");
            }
            pci_escreve32(&c->pci, (uint8_t)(eecp + 4), 0);   /* USBLEGCTLSTS: sem SMI */
            return;
        }
        eecp = (uint8_t)((v >> 8) & 0xFF);
    }
}

static bool alocar_buffer(void)
{
    if (buffer) return true;
    uint32_t b = memfisica_alocar_seguidas(BUFFER_BYTES / PAGINA);
    if (!b) return false;
    buffer = (uint8_t *)b;
    return true;
}

/* Uma página por controlador: cabeça (96), fila de trabalho (96), pacote de
 * setup e 8 qTDs de 64 bytes, sem nenhuma estrutura encostada em outra. */
static bool alocar_estruturas(ehci_t *c)
{
    uint32_t p = memfisica_alocar();
    if (!p) return false;
    memset((void *)p, 0, PAGINA);
    c->estruturas = p;
    usar(c);
    /* A cabeça do anel assíncrono: aponta para si mesma, marcada como "H". */
    cabeca->ligacao = (uint32_t)cabeca | T_QH;
    cabeca->caracteristicas = (1u << 15) | (2u << 12);
    cabeca->capacidades = 1u << 30;
    cabeca->prox_qtd = T_TERMINA;
    cabeca->alt_qtd = T_TERMINA;
    cabeca->ficha = F_PAROU;   /* nunca executa nada */
    return true;
}

static bool iniciar(ehci_t *c)
{
    uint32_t bar = pci_le32(&c->pci, 0x10) & ~0xFu;
    if (!bar) return false;
    /* Comando PCI: espaço de memória + mestre do barramento (DMA). */
    pci_escreve32(&c->pci, 0x04, pci_le32(&c->pci, 0x04) | 0x6);
    c->cap = (volatile uint8_t *)janela_mapear(bar, 4096);
    if (!c->cap) return false;
    c->op = (volatile uint32_t *)(c->cap + c->cap[0]);
    uint32_t hcs = *(volatile uint32_t *)(c->cap + 4);
    c->portas = (int)(hcs & 0xF);
    c->companheiros = (int)((hcs >> 12) & 0xF);
    for (int p = 0; p < USB_MAX_PORTAS; p++) c->resultado[p] = -1;   /* ainda não tentado */
    c->enderecos64 = *(volatile uint32_t *)(c->cap + 8) & 1;
    if (c->portas > USB_MAX_PORTAS) c->portas = USB_MAX_PORTAS;
    if (!alocar_estruturas(c)) return false;
    tomar_da_bios(c);

    /* Para e reinicia o controlador. */
    c->op[USBCMD] &= ~CMD_RS;
    for (int i = 0; i < 100 && !(c->op[USBSTS] & STS_HALTED); i++) esperar_bruto_us(1000);
    c->op[USBCMD] |= CMD_RESET;
    for (int i = 0; i < 250 && (c->op[USBCMD] & CMD_RESET); i++) esperar_bruto_us(1000);
    if (c->op[USBCMD] & CMD_RESET) { klog("[EHCI] reset nao terminou\n"); return false; }

    c->op[USBINTR] = 0;                       /* sem interrupções: o driver consulta */
    if (*(volatile uint32_t *)(c->cap + 8) & 1) c->op[CTRLDSSEG] = 0;   /* 64 bits: parte alta = 0 */
    c->op[PERIODIC] = 0;
    c->op[ASYNCLIST] = (uint32_t)cabeca;
    c->op[USBCMD] = (8u << 16) | CMD_RS | CMD_ASE;   /* 8 microquadros por interrupção (não usada), rodando, lista assíncrona */
    for (int i = 0; i < 100 && (c->op[USBSTS] & STS_HALTED); i++) esperar_bruto_us(1000);
    c->op[CONFIGFLAG] = 1;                     /* todas as portas para este controlador */
    esperar_bruto_us(5000);
    if (hcs & (1u << 4))                       /* controle de energia por porta: liga todas */
        for (int p = 0; p < c->portas; p++)
            c->op[PORTSC + p] = (c->op[PORTSC + p] & ~P_MUDANCAS) | P_PP;
    ehci_esperar_ms(c, 30);
    c->proximo_endereco = 1;
    c->ok = !(c->op[USBSTS] & STS_HALTED);
    klog("[EHCI] controlador %02x:%02x.%x (%04x:%04x) em %x: %d portas, %s\n",
         c->pci.barramento, c->pci.dispositivo, c->pci.funcao, c->pci.fabricante, c->pci.modelo,
         bar, c->portas, c->ok ? "rodando" : "PARADO");
    return c->ok;
}

int ehci_iniciar_todos(void)
{
    pci_disp_t achados[USB_MAX_CONTROLADORES];
    int n = pci_procurar(0x0C, 0x03, 0x20, achados, USB_MAX_CONTROLADORES);   /* serial bus, USB, EHCI */
    if (n == 0 || !alocar_buffer()) return 0;
    bool i0 = int_salva_desliga();
    for (int i = 0; i < n; i++) {
        ehci_t *c = &ctls[qtd_ctls];
        memset(c, 0, sizeof *c);
        c->pci = achados[i];
        if (iniciar(c)) qtd_ctls++;
    }
    int_restaura(i0);
    return qtd_ctls;
}

/* ------------------------------------------------------------ portas */

uint32_t ehci_porta_estado(ehci_t *c, int p) { return c->op[PORTSC + p]; }

bool ehci_porta_mudou(ehci_t *c, int p)
{
    uint32_t v = c->op[PORTSC + p];
    if (!(v & P_CSC)) return false;
    c->op[PORTSC + p] = (v & ~P_MUDANCAS) | P_CSC;   /* escreve 1 para limpar */
    return true;
}

int ehci_porta_resetar(ehci_t *c, int p)
{
    uint32_t v = c->op[PORTSC + p];
    if (!(v & P_CCS)) return USB_ERRO;
    if ((v & P_LS) == (1u << 10)) {                /* estado K: baixa velocidade -> companheiro */
        if (c->companheiros) c->op[PORTSC + p] = (v & ~P_MUDANCAS) | P_PO;
        return USB_ERRO;
    }
    c->op[PORTSC + p] = ((v & ~P_MUDANCAS) & ~P_PE) | P_PR;
    ehci_esperar_ms(c, 50);                        /* a especificação USB pede 50 ms de reset na raiz */
    c->op[PORTSC + p] = (c->op[PORTSC + p] & ~P_MUDANCAS) & ~P_PR;
    for (int i = 0; i < 20 && (c->op[PORTSC + p] & P_PR); i++) ehci_esperar_ms(c, 1);
    ehci_esperar_ms(c, 10);
    v = c->op[PORTSC + p];
    if (!(v & P_PE)) {                             /* não habilitou: velocidade plena -> companheiro */
        /* Sem companheiros (G470: os chipsets série 6 só têm o hub interno),
         * entregar a porta a ninguém a deixaria inútil: só desiste. */
        if (c->companheiros) c->op[PORTSC + p] = (v & ~P_MUDANCAS) | P_PO;
        return USB_ERRO;
    }
    return 0;
}

/* ------------------------------------------------------------ transferências */

static void preparar_qtd(qtd_t *t, uint32_t pid, uint32_t alterna, uint32_t endereco, uint32_t tam, bool ultimo)
{
    t->proximo = T_TERMINA;
    t->alternativo = T_TERMINA;
    t->ficha = (alterna << 31) | (tam << 16) | (ultimo ? (1u << 15) : 0) | (3u << 10) | (pid << 8) | F_ATIVO;
    t->buffer[0] = endereco;
    uint32_t pagina = endereco & ~0xFFFu;
    for (int i = 1; i < 5; i++) t->buffer[i] = pagina + (uint32_t)i * PAGINA;
    for (int i = 0; i < 5; i++) t->buffer_alto[i] = 0;   /* tudo abaixo de 4 GB */
}

/* Pendura a QH no anel, espera o último qTD e tira a QH. Devolve 0 ou erro. */
/* vazio_uf: se > 0, desiste depois de tantos microquadros (125 µs) sem
 * nenhum byte chegar (para a rede: "não tem pacote agora"). Se algum byte já
 * chegou, espera o resto até tempo_ms, para não cortar um pacote no meio. */
static int executar(ehci_t *c, uint8_t endereco, uint8_t ep, uint16_t mps, int n_qtd, uint32_t tempo_ms, uint32_t vazio_uf)
{
    uint32_t tam_inicial = (qtds[0].ficha >> 16) & 0x7FFF;
    for (int i = 0; i < n_qtd - 1; i++) qtds[i].proximo = (uint32_t)&qtds[i + 1];
    memset((void *)fila, 0, sizeof *fila);
    fila->caracteristicas = (uint32_t)endereco | ((uint32_t)ep << 8) | (2u << 12) /* alta velocidade */ |
                            (1u << 14) /* alternância vem do qTD */ | ((uint32_t)mps << 16) | (4u << 28);
    fila->capacidades = 1u << 30;
    fila->prox_qtd = (uint32_t)&qtds[0];
    fila->alt_qtd = T_TERMINA;
    fila->ligacao = (uint32_t)cabeca | T_QH;
    cabeca->ligacao = (uint32_t)fila | T_QH;

    int r = 0;
    uint32_t passou = 0, antes = c->op[FRINDEX] & 0x3FFF;
    for (;;) {
        uint32_t f = qtds[n_qtd - 1].ficha;
        bool parou = false;
        for (int i = 0; i < n_qtd; i++)
            if (qtds[i].ficha & F_PAROU) { parou = true; f = qtds[i].ficha; break; }
        if (parou) { r = (f & F_ERROS) ? USB_ERRO : USB_ERRO_PAROU; break; }
        if (!(f & F_ATIVO)) break;
        uint32_t agora = c->op[FRINDEX] & 0x3FFF;
        passou += (agora - antes) & 0x3FFF;
        antes = agora;
        if (passou > tempo_ms * 8) { r = USB_ERRO_TEMPO; break; }
        if (vazio_uf && passou > vazio_uf && ((qtds[0].ficha >> 16) & 0x7FFF) == tam_inicial) { r = USB_ERRO_TEMPO; break; }
        if (c->op[USBSTS] & STS_HALTED) { r = USB_ERRO; break; }
    }

    /* Tira a QH do anel e espera o controlador confirmar ("doorbell"). */
    cabeca->ligacao = (uint32_t)cabeca | T_QH;
    c->op[USBSTS] = STS_IAA;
    c->op[USBCMD] |= CMD_IAAD;
    for (int i = 0; i < 20000 && !(c->op[USBSTS] & STS_IAA); i++) porta_pausa();
    c->op[USBSTS] = STS_IAA;
    return r;
}

int ehci_controle(ehci_t *c, uint8_t endereco, uint8_t mps0, const void *setup, void *dados, uint16_t tam, bool entrada)
{
    if (tam > QTD_MAX_BYTES) return USB_ERRO;
    bool i0 = int_salva_desliga();
    usar(c);
    memcpy(setup_pacote, setup, 8);
    if (!entrada && tam) memcpy(buffer, dados, tam);
    int n = 0;
    preparar_qtd(&qtds[n++], PID_SETUP, 0, (uint32_t)setup_pacote, 8, false);
    if (tam) preparar_qtd(&qtds[n++], entrada ? PID_ENTRADA : PID_SAIDA, 1, (uint32_t)buffer, tam, false);
    /* Estado: sempre alternância 1, na direção contrária aos dados (ou IN sem dados). */
    preparar_qtd(&qtds[n++], (tam && entrada) ? PID_SAIDA : PID_ENTRADA, 1, (uint32_t)buffer, 0, true);
    int r = executar(c, endereco, 0, mps0 ? mps0 : 64, n, 1000, 0);
    int feito = 0;
    if (r == 0 && tam) {
        feito = (int)tam - (int)((qtds[1].ficha >> 16) & 0x7FFF);
        if (entrada) memcpy(dados, buffer, (uint32_t)feito);
    }
    int_restaura(i0);
    return r < 0 ? r : feito;
}

static int bulk(ehci_t *c, uint8_t endereco, uint8_t ep, uint16_t mps, bool entrada, uint8_t *alterna, void *dados, uint32_t tam,
                uint32_t vazio_uf)
{
    if (tam > BUFFER_BYTES || tam > (uint32_t)MAX_QTD * QTD_MAX_BYTES) return USB_ERRO;
    bool i0 = int_salva_desliga();
    usar(c);
    if (!entrada) memcpy(buffer, dados, tam);
    /* Cada qTD (menos o último) leva um múltiplo do tamanho do pacote, então
     * a alternância de cada um é a inicial trocada uma vez por pacote. */
    int n = 0;
    uint32_t feito_antes = 0, t = *alterna;
    do {
        uint32_t parte = tam - feito_antes > QTD_MAX_BYTES ? QTD_MAX_BYTES : tam - feito_antes;
        preparar_qtd(&qtds[n], entrada ? PID_ENTRADA : PID_SAIDA, t, (uint32_t)buffer + feito_antes, parte, false);
        t ^= (parte / mps) & 1;
        feito_antes += parte;
        n++;
    } while (feito_antes < tam);
    qtds[n - 1].ficha |= 1u << 15;
    int r = executar(c, endereco, ep, mps, n, 5000, vazio_uf);
    uint32_t feito = 0;
    for (int i = 0; i < n; i++) {
        uint32_t pedido = i < n - 1 ? QTD_MAX_BYTES : tam - (uint32_t)(n - 1) * QTD_MAX_BYTES;
        uint32_t sobrou = (qtds[i].ficha >> 16) & 0x7FFF;
        if (qtds[i].ficha & F_ATIVO) break;
        feito += pedido - sobrou;
        if (sobrou) break;   /* pacote curto: acabou aqui */
    }
    /* Alternância para a próxima transferência: troca a cada pacote enviado. */
    uint32_t pacotes = (feito + mps - 1) / mps;
    if (feito == 0 && r == 0) pacotes = 1;   /* pacote de tamanho zero */
    if (r == 0 || r == USB_ERRO_PAROU) *alterna ^= (uint8_t)(pacotes & 1);
    if (entrada && feito) memcpy(dados, buffer, feito);
    int_restaura(i0);
    return r < 0 ? r : (int)feito;
}

int ehci_bulk(ehci_t *c, uint8_t endereco, uint8_t ep, uint16_t mps, bool entrada, uint8_t *alterna, void *dados, uint32_t tam)
{
    return bulk(c, endereco, ep, mps, entrada, alterna, dados, tam, 0);
}

/* Entrada sem esperar (rede USB, H46): se em ~1 ms nada começou a chegar,
 * devolve 0 (o dispositivo respondeu NAK: não tem pacote). Sem pacote, a
 * alternância não muda. */
int ehci_bulk_tentar(ehci_t *c, uint8_t endereco, uint8_t ep, uint16_t mps, uint8_t *alterna, void *dados, uint32_t tam)
{
    int r = bulk(c, endereco, ep, mps, true, alterna, dados, tam, 8);
    return r == USB_ERRO_TEMPO ? 0 : r;
}
