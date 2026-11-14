/* hda.c - driver de som Intel High Definition Audio (H40).
 *
 * O "HD Audio" tem duas partes:
 *   - o controlador, no chipset (PCI classe 04, subclasse 03): copia as
 *     amostras da memória para o codec por DMA e leva comandos até ele;
 *   - o codec, um chip à parte (de outro fabricante, em geral), que converte
 *     números em som e tem as saídas: alto-falante, fone, linha.
 *
 * Comandos ao codec ("verbos") vão por duas filas na memória, como manda a
 * especificação: o CORB (fila de saída, um verbo de 32 bits por posição) e o
 * RIRB (fila de respostas, 64 bits por posição). Escrevemos o verbo, avançamos
 * o ponteiro de escrita do CORB e esperamos o RIRB andar.
 *
 * O codec é um grafo de "widgets": conversores digital-analógico (DAC),
 * misturadores, seletores e "pinos" (as tomadas). Cada widget diz de quem ele
 * pode receber som (lista de conexões). Para tocar, o driver procura, a partir
 * de cada pino de saída que existe de verdade no aparelho (configuração
 * padrão do pino diz "alto-falante", "fone" ou "linha" e que está ligado),
 * um caminho até um DAC; escolhe a conexão certa nos seletores, tira o mudo e
 * põe o volume no máximo nos amplificadores do caminho, liga a saída do pino
 * e, se o pino tiver, o amplificador externo (EAPD, comum em notebooks).
 *
 * A reprodução usa uma "descrição de fluxo" de saída do controlador: um anel
 * de buffers (lista BDL) que o DMA percorre sem parar. O kernel enche o anel
 * um pouco à frente da posição de leitura do DMA a cada tique do relógio
 * (mixer.c), e o codec recebe o mesmo número de fluxo e o mesmo formato:
 * 48 kHz, 16 bits, estéreo. Nada de interrupções: o tique de 10 ms basta. */
#include "hda.h"
#include "pci.h"
#include "console.h"
#include "cpu.h"
#include "memoria.h"
#include "paginacao.h"
#include "relogio.h"
#include "texto.h"
#include "portas.h"

/* Registradores do controlador (deslocamentos no BAR0). */
#define GCAP      0x00
#define GCTL      0x08
#define STATESTS  0x0E
#define INTCTL    0x20
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP    0x48
#define CORBRP    0x4A
#define CORBCTL   0x4C
#define CORBSIZE  0x4E
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP    0x58
#define RINTCNT   0x5A
#define RIRBCTL   0x5C
#define RIRBSTS   0x5D
#define RIRBSIZE  0x5E
/* Descritor de fluxo (a partir de 0x80 + 0x20 * n). */
#define SD_CTL    0x00
#define SD_STS    0x03
#define SD_LPIB   0x04
#define SD_CBL    0x08
#define SD_LVI    0x0C
#define SD_FMT    0x12
#define SD_BDPL   0x18
#define SD_BDPU   0x1C

/* Verbos (comandos ao codec). */
#define V_PARAMETRO      0xF00
#define V_CONEXAO_LISTA  0xF02
#define V_CONEXAO_SEL    0x701
#define V_ENERGIA        0x705
#define V_FLUXO          0x706
#define V_PINO           0x707
#define V_EAPD           0x70C
#define V_CONFIG_PADRAO  0xF1C
#define V4_FORMATO       0x2
#define V4_AMPLIFICADOR  0x3
/* Parâmetros. */
#define P_FABRICANTE     0x00
#define P_SUBORDINADOS   0x04
#define P_TIPO_GRUPO     0x05
#define P_WIDGET         0x09
#define P_PINO           0x0C
#define P_CONEXOES       0x0E
#define P_AMP_SAIDA      0x12

#define FORMATO_48K_16_ESTEREO 0x0011
#define FLUXO_NUMERO 1

static volatile uint8_t *regs;
static uint32_t *corb;
static volatile uint32_t *rirb;   /* pares (resposta, extra) */
static int corb_n, rirb_n;
static uint16_t rirb_lido;
static int codec = -1;
static bool pronto;
static uint32_t sd;               /* deslocamento do descritor de fluxo de saída */
static uint8_t *anel;             /* buffers do DMA */
static uint32_t *bdl;
static char descricao[96];

static inline uint32_t r32(uint32_t o) { return *(volatile uint32_t *)(regs + o); }
static inline uint16_t r16(uint32_t o) { return *(volatile uint16_t *)(regs + o); }
static inline uint8_t  r8(uint32_t o)  { return *(volatile uint8_t *)(regs + o); }
static inline void w32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(regs + o) = v; }
static inline void w16(uint32_t o, uint16_t v) { *(volatile uint16_t *)(regs + o) = v; }
static inline void w8(uint32_t o, uint8_t v)   { *(volatile uint8_t *)(regs + o) = v; }

static void esperar_us(uint32_t us)
{
    for (uint32_t i = 0; i < us; i++) porta_pausa();
}

/* Espera um bit ficar no valor pedido (ou desiste depois de ~ms). */
static bool esperar_bit16(uint32_t o, uint16_t mascara, bool ligado, uint32_t ms)
{
    for (uint32_t i = 0; i < ms * 10; i++) {
        if (((r16(o) & mascara) != 0) == ligado) return true;
        esperar_us(100);
    }
    return false;
}

static bool esperar_bit8(uint32_t o, uint8_t mascara, bool ligado, uint32_t ms)
{
    for (uint32_t i = 0; i < ms * 10; i++) {
        if (((r8(o) & mascara) != 0) == ligado) return true;
        esperar_us(100);
    }
    return false;
}

/* ------------------------------------------------------------ verbos */

/* Manda um verbo e devolve a resposta (0xFFFFFFFF se o codec não respondeu). */
static uint32_t verbo(uint32_t comando)
{
    uint16_t wp = (uint16_t)((r16(CORBWP) & 0xFF) + 1) % (uint16_t)corb_n;
    corb[wp] = comando;
    w16(CORBWP, wp);
    for (int i = 0; i < 2000; i++) {
        uint16_t chegou = r16(RIRBWP) & 0xFF;
        if (chegou != rirb_lido) {
            rirb_lido = (uint16_t)((rirb_lido + 1) % rirb_n);
            uint32_t resposta = rirb[rirb_lido * 2];
            w8(RIRBSTS, 0x05);   /* limpa os avisos de resposta (alguns controladores só seguem depois disso) */
            return resposta;
        }
        esperar_us(10);
    }
    klog("[HDA] sem resposta ao verbo %x (CORBWP %x CORBRP %x RIRBWP %x)\n", comando, r16(CORBWP), r16(CORBRP), r16(RIRBWP));
    return 0xFFFFFFFFu;
}

static uint32_t comando(int nid, uint32_t v12, uint32_t carga8)
{
    return verbo(((uint32_t)codec << 28) | ((uint32_t)nid << 20) | (v12 << 8) | (carga8 & 0xFF));
}

static uint32_t comando16(int nid, uint32_t v4, uint32_t carga16)
{
    return verbo(((uint32_t)codec << 28) | ((uint32_t)nid << 20) | (v4 << 16) | (carga16 & 0xFFFF));
}

static uint32_t parametro(int nid, uint32_t p) { return comando(nid, V_PARAMETRO, p); }

/* ------------------------------------------------------------ o grafo do codec */

#define MAX_NOS 96
static struct {
    uint32_t caps;        /* capacidades do widget */
    uint8_t  conexoes[16];
    int      qtd_conexoes;
    uint32_t config;      /* configuração padrão (pinos) */
    bool     usado;
} nos[MAX_NOS];
static int no_ini, no_fim;   /* faixa de nós do grupo de áudio */
static int grupo;            /* o grupo de áudio (seus parâmetros valem para os widgets sem os próprios) */

static int tipo_no(int n) { return (int)((nos[n].caps >> 20) & 0xF); }
enum { W_SAIDA = 0, W_ENTRADA = 1, W_MISTURADOR = 2, W_SELETOR = 3, W_PINO = 4 };

static void ler_conexoes(int n)
{
    nos[n].qtd_conexoes = 0;
    if (!(nos[n].caps & (1u << 8))) return;
    uint32_t info = parametro(n, P_CONEXOES);
    int total = (int)(info & 0x7F);
    bool longa = info & 0x80;
    for (int i = 0; i < total && nos[n].qtd_conexoes < 16; i += longa ? 2 : 4) {
        uint32_t r = comando(n, V_CONEXAO_LISTA, (uint32_t)i);
        for (int k = 0; k < (longa ? 2 : 4) && i + k < total && nos[n].qtd_conexoes < 16; k++) {
            uint32_t e = longa ? (r >> (16 * k)) & 0xFFFF : (r >> (8 * k)) & 0xFF;
            nos[n].conexoes[nos[n].qtd_conexoes++] = (uint8_t)(e & (longa ? 0x7FFF : 0x7F));
        }
    }
}

/* Procura um DAC a partir do nó n (busca em profundidade, até 6 passos).
 * caminho[] recebe os nós, escolha[] o índice da conexão usada em cada um. */
static int caminho[8], escolha[8], tam_caminho;

static bool buscar_dac(int n, int profundidade)
{
    if (n < no_ini || n >= no_fim || profundidade > 6) return false;
    caminho[profundidade] = n;
    if (tipo_no(n) == W_SAIDA) { tam_caminho = profundidade + 1; return true; }
    for (int i = 0; i < nos[n].qtd_conexoes; i++) {
        escolha[profundidade] = i;
        int prox = nos[n].conexoes[i];
        if (prox >= no_ini && prox < no_fim && (tipo_no(prox) == W_SAIDA || tipo_no(prox) == W_MISTURADOR ||
                                                tipo_no(prox) == W_SELETOR) && buscar_dac(prox, profundidade + 1))
            return true;
    }
    return false;
}

/* Volume máximo, sem mudo, no amplificador de saída (e no de entrada da conexão usada). */
static void abrir_amplificadores(int n, int indice_entrada)
{
    uint32_t caps = nos[n].caps;
    if (caps & (1u << 2)) {                                  /* tem amplificador de saída */
        uint32_t amp = (caps & (1u << 3)) ? parametro(n, P_AMP_SAIDA) : parametro(grupo, P_AMP_SAIDA);
        uint32_t ganho = amp & 0x7F;                          /* o passo de 0 dB: alto sem distorcer */
        comando16(n, V4_AMPLIFICADOR, (1u << 15) | (1u << 13) | (1u << 12) | (ganho & 0x7F));
    }
    if ((caps & (1u << 1)) && indice_entrada >= 0)           /* amplificador de entrada */
        comando16(n, V4_AMPLIFICADOR, (1u << 14) | (1u << 13) | (1u << 12) | ((uint32_t)indice_entrada << 8) | 0x7F);
}

static int configurar_codec(void)
{
    uint32_t sub = parametro(0, P_SUBORDINADOS);
    int ini = (int)((sub >> 16) & 0xFF), qtd = (int)(sub & 0xFF);
    int afg = -1;
    for (int g = ini; g < ini + qtd; g++)
        if ((parametro(g, P_TIPO_GRUPO) & 0xFF) == 1) { afg = g; break; }
    if (afg < 0) { klog("[HDA] codec %d sem grupo de audio\n", codec); return 0; }
    grupo = afg;
    comando(afg, V_ENERGIA, 0);                              /* D0: ligado */
    sub = parametro(afg, P_SUBORDINADOS);
    no_ini = (int)((sub >> 16) & 0xFF);
    no_fim = no_ini + (int)(sub & 0xFF);
    if (no_fim > MAX_NOS) no_fim = MAX_NOS;
    for (int n = no_ini; n < no_fim; n++) {
        nos[n].caps = parametro(n, P_WIDGET);
        nos[n].usado = false;
        ler_conexoes(n);
        if (tipo_no(n) == W_PINO) nos[n].config = comando(n, V_CONFIG_PADRAO, 0);
        if (nos[n].caps & (1u << 10)) comando(n, V_ENERGIA, 0);
    }
    /* Pinos de saída de verdade: linha (0), alto-falante (1) e fone (2), com
     * algo ligado (conectividade diferente de "nada", 01). */
    int saidas = 0, dac_usado = -1;
    static const char *const nomes[3] = { "linha", "alto-falante", "fone" };
    char lista[64] = "";
    for (int n = no_ini; n < no_fim; n++) {
        if (tipo_no(n) != W_PINO) continue;
        uint32_t cfg = nos[n].config;
        uint32_t aparelho = (cfg >> 20) & 0xF, ligado = (cfg >> 30) & 0x3;
        uint32_t pcaps = parametro(n, P_PINO);
        /* Diagnóstico (G470): todo pino vai para o diário, com a configuração
         * que o fabricante gravou e o que ele sabe fazer. */
        klog("[HDA] pino %d: config %x (aparelho %d, ligacao %d), caps %x, %d conexoes\n", n, cfg, aparelho, ligado, pcaps,
             nos[n].qtd_conexoes);
        if (aparelho > 2 || ligado == 1) continue;
        if (!(pcaps & (1u << 4))) continue;                   /* não sabe ser saída */
        if (!buscar_dac(n, 0)) continue;
        for (int k = 0; k < tam_caminho; k++) {
            int no = caminho[k];
            if (k < tam_caminho - 1 && nos[no].qtd_conexoes > 1 && tipo_no(no) != W_MISTURADOR)
                comando(no, V_CONEXAO_SEL, (uint32_t)escolha[k]);
            abrir_amplificadores(no, (k < tam_caminho - 1 && tipo_no(no) == W_MISTURADOR) ? escolha[k] : -1);
            nos[no].usado = true;
        }
        int dac = caminho[tam_caminho - 1];
        char rota[48] = "";
        for (int k = 0; k < tam_caminho; k++) {
            size_t l = txt_tamanho(rota);
            snformatar(rota + l, sizeof rota - l, "%s%d", k ? ">" : "", caminho[k]);
        }
        klog("[HDA] saida %s no pino %d: caminho %s%s\n", nomes[aparelho], n, rota, (pcaps & (1u << 16)) ? ", com EAPD" : "");
        comando16(dac, V4_FORMATO, FORMATO_48K_16_ESTEREO);
        comando(dac, V_FLUXO, (FLUXO_NUMERO << 4) | 0);
        comando(n, V_PINO, 0x40 | (aparelho == 2 ? 0x80 : 0));   /* saída ligada (+ fone) */
        if (pcaps & (1u << 16)) comando(n, V_EAPD, 0x02);         /* amplificador externo */
        if (dac_usado < 0) dac_usado = dac;
        saidas++;
        size_t l = txt_tamanho(lista);
        snformatar(lista + l, sizeof lista - l, "%s%s", l ? ", " : "", nomes[aparelho]);
    }
    uint32_t fab = parametro(0, P_FABRICANTE);
    snformatar(descricao, sizeof descricao, "codec %04x:%04x, saidas: %s", fab >> 16, fab & 0xFFFF, saidas ? lista : "nenhuma");
    klog("[HDA] codec %d (%04x:%04x): %d saida(s) (%s), DAC %d\n", codec, fab >> 16, fab & 0xFFFF, saidas, lista, dac_usado);
    return saidas;
}

/* ------------------------------------------------------------ controlador */

static bool reiniciar_controlador(void)
{
    w32(GCTL, r32(GCTL) & ~1u);                               /* entra em reset */
    for (int i = 0; i < 1000 && (r32(GCTL) & 1); i++) esperar_us(100);
    esperar_us(200);
    w32(GCTL, r32(GCTL) | 1u);                                /* sai do reset */
    for (int i = 0; i < 1000 && !(r32(GCTL) & 1); i++) esperar_us(100);
    if (!(r32(GCTL) & 1)) return false;
    esperar_us(2000);                                         /* os codecs se apresentam (mín. 521 µs) */
    return true;
}

/* Os bits 4 a 7 do registrador de tamanho dizem quais tamanhos a fila aceita
 * (já deslocados para 0 a 3 aqui): 0x4 = 256 posições, 0x2 = 16, 0x1 = 2. */
static int tamanho_fila(uint8_t capacidade, uint8_t *codigo)
{
    if (capacidade & 0x4) { *codigo = 2; return 256; }
    if (capacidade & 0x2) { *codigo = 1; return 16; }
    *codigo = 0;
    return 2;
}

static bool preparar_filas(void)
{
    uint32_t pagina = memfisica_alocar();
    if (!pagina) return false;
    memset((void *)pagina, 0, PAGINA);
    corb = (uint32_t *)pagina;                                /* 1 KB */
    rirb = (volatile uint32_t *)(pagina + 2048);              /* 2 KB */
    /* CORB */
    w8(CORBCTL, 0);
    esperar_bit8(CORBCTL, 0x02, false, 10);
    uint8_t cod;
    corb_n = tamanho_fila(r8(CORBSIZE) >> 4, &cod);
    w8(CORBSIZE, (uint8_t)((r8(CORBSIZE) & 0xFC) | cod));
    w32(CORBLBASE, (uint32_t)corb);
    w32(CORBUBASE, 0);
    w16(CORBRP, 0x8000);                                      /* zera o ponteiro de leitura */
    esperar_bit16(CORBRP, 0x8000, true, 5);
    w16(CORBRP, 0);
    esperar_bit16(CORBRP, 0x8000, false, 5);
    w16(CORBWP, 0);
    w8(CORBCTL, 0x02);                                        /* liga o DMA do CORB */
    /* RIRB */
    w8(RIRBCTL, 0);
    esperar_bit8(RIRBCTL, 0x02, false, 10);
    rirb_n = tamanho_fila(r8(RIRBSIZE) >> 4, &cod);
    w8(RIRBSIZE, (uint8_t)((r8(RIRBSIZE) & 0xFC) | cod));
    w32(RIRBLBASE, (uint32_t)rirb);
    w32(RIRBUBASE, 0);
    w16(RIRBWP, 0x8000);                                      /* zera o ponteiro de escrita */
    w16(RINTCNT, 1);
    rirb_lido = 0;
    /* Liga o DMA do RIRB e o aviso de resposta (bit 0). As interrupções
     * globais do controlador ficam desligadas (INTCTL = 0), então o aviso
     * nunca chega à CPU: só marca o registrador de estado, que limpamos a
     * cada resposta. Sem o aviso ligado, há controladores (o do QEMU, por
     * exemplo) que param de ler a fila depois de cada resposta. */
    w8(RIRBCTL, 0x03);
    esperar_us(1000);
    return true;
}

static void programar_fluxo(void);

static bool preparar_fluxo(void)
{
    uint32_t paginas = HDA_ANEL / PAGINA;
    uint32_t b = memfisica_alocar_seguidas(paginas);
    uint32_t l = memfisica_alocar();
    if (!b || !l) return false;
    anel = (uint8_t *)b;
    bdl = (uint32_t *)l;
    memset(anel, 0, HDA_ANEL);
    memset(bdl, 0, PAGINA);
    for (uint32_t i = 0; i < HDA_PEDACOS; i++) {              /* lista de buffers: endereço, tamanho, aviso */
        bdl[i * 4 + 0] = b + i * (HDA_ANEL / HDA_PEDACOS);
        bdl[i * 4 + 1] = 0;
        bdl[i * 4 + 2] = HDA_ANEL / HDA_PEDACOS;
        bdl[i * 4 + 3] = 0;
    }
    uint32_t iss = (r16(GCAP) >> 8) & 0xF;
    sd = 0x80 + 0x20 * iss;                                   /* primeiro fluxo de saída */
    programar_fluxo();
    return true;
}

/* Reset do descritor de fluxo e partida (especificação HD Audio, 3.3.35 e
 * 4.5.3): parar, SRST em 1 e esperar ler 1, SRST em 0 e esperar ler 0;
 * depois tamanho, último índice, formato, lista de buffers, número e RUN. */
static void programar_fluxo(void)
{
    w8(sd + SD_CTL, 0);
    esperar_us(100);
    w8(sd + SD_CTL, 1);                                       /* reset do fluxo */
    esperar_bit8(sd + SD_CTL, 1, true, 10);
    w8(sd + SD_CTL, 0);
    esperar_bit8(sd + SD_CTL, 1, false, 10);
    w32(sd + SD_CBL, HDA_ANEL);
    w16(sd + SD_LVI, HDA_PEDACOS - 1);
    w16(sd + SD_FMT, FORMATO_48K_16_ESTEREO);
    w32(sd + SD_BDPL, (uint32_t)bdl);
    w32(sd + SD_BDPU, 0);
    w8(sd + SD_CTL + 2, (uint8_t)(FLUXO_NUMERO << 4));        /* número do fluxo */
    w8(sd + SD_STS, 0x1C);                                    /* limpa avisos */
    w8(sd + SD_CTL, 0x02);                                    /* RUN */
}

/* O misturador viu a posição parada: tenta partir o fluxo de novo, uma vez. */
void hda_reiniciar_fluxo(void)
{
    if (!pronto) return;
    bool i0 = int_salva_desliga();
    programar_fluxo();
    int_restaura(i0);
    klog("[HDA] fluxo reprogramado: CTL %x STS %x\n", r32(sd + SD_CTL) & 0xFFFFFF, r8(sd + SD_STS));
}

bool hda_iniciar(void)
{
    pci_disp_t d;
    if (pci_procurar(0x04, 0x03, 0x00, &d, 1) < 1) return false;
    uint32_t bar = pci_le32(&d, 0x10) & ~0xFu;
    if (!bar) return false;
    pci_escreve32(&d, 0x04, pci_le32(&d, 0x04) | 0x6);       /* memória + mestre (DMA) */
    if (d.fabricante == 0x8086) {
        /* Controladores Intel: TCSEL (PCI 0x44, bits 0-2) em TC0, como o
         * datasheet do chipset recomenda para o áudio (G470: série 6). */
        uint32_t v = pci_le32(&d, 0x44);
        pci_escreve32(&d, 0x44, v & ~7u);
        klog("[HDA] TCSEL %x -> %x\n", v & 7, 0);
    }
    regs = (volatile uint8_t *)janela_mapear(bar, 0x4000);
    if (!regs) return false;
    bool i0 = int_salva_desliga();
    bool ok = reiniciar_controlador();
    if (ok) {
        w32(INTCTL, 0);                                        /* sem interrupções: o tique cuida */
        uint16_t presentes = r16(STATESTS);
        ok = preparar_filas();
        klog("[HDA] GCAP %x, codecs presentes %x, CORB %d RIRB %d, CORBCTL %x RIRBCTL %x\n", r16(GCAP), presentes,
             corb_n, rirb_n, r8(CORBCTL), r8(RIRBCTL));
        for (int c = 0; ok && c < 15 && codec < 0; c++)
            if (presentes & (1u << c)) {
                codec = c;
                if (configurar_codec() == 0) codec = -1;
            }
        ok = ok && codec >= 0 && preparar_fluxo();
    }
    int_restaura(i0);
    pronto = ok;
    klog("[HDA] controlador %04x:%04x em %x: %s\n", d.fabricante, d.modelo, bar, ok ? "tocando (48 kHz, 16 bits, estereo)" : "sem saida de som");
    return ok;
}

bool hda_pronto(void) { return pronto; }
const char *hda_descricao(void) { return pronto ? descricao : "sem placa de som"; }
uint8_t *hda_anel(void) { return anel; }
uint32_t hda_posicao(void) { return pronto ? r32(sd + SD_LPIB) % HDA_ANEL : 0; }

/* Diagnóstico (G470): o fluxo está andando? Chamado pelo misturador alguns
 * segundos depois da partida; o resultado vai para o diário (registro). */
void hda_diagnostico(int fase)
{
    static uint32_t p1;
    if (!pronto) return;
    if (fase == 0) { p1 = r32(sd + SD_LPIB); return; }      /* primeira leitura; a segunda vem meio segundo depois */
    uint32_t p2 = r32(sd + SD_LPIB);
    klog("[HDA] fluxo: CTL %x STS %x, posicao %u -> %u em 0,5 s (%s), GCTL %x\n", r32(sd + SD_CTL) & 0xFFFFFF, r8(sd + SD_STS),
         p1, p2, p1 != p2 ? "andando" : "PARADO", r32(GCTL));
    for (int n = no_ini; n < no_fim; n++)
        if (nos[n].usado || tipo_no(n) == W_PINO) {
            uint32_t ctl = tipo_no(n) == W_PINO ? comando(n, 0xF07, 0) : 0;
            uint32_t eapd = tipo_no(n) == W_PINO ? comando(n, 0xF0C, 0) : 0;
            uint32_t amp = comando16(n, 0xB, (1u << 15) | (1u << 13));   /* ganho de saída, esquerda */
            uint32_t sentido = tipo_no(n) == W_PINO ? comando(n, 0xF09, 0) : 0;   /* algo plugado? */
            klog("[HDA] no %d tipo %d%s: pinctl %x eapd %x amp %x presenca %x energia %x\n", n, tipo_no(n),
                 nos[n].usado ? " (usado)" : "", ctl, eapd, amp, sentido, comando(n, 0xF05, 0));
        }
}
