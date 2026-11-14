/* misturador.c - quem enche o anel da placa de som (H40, H41, H42).
 *
 * Cada programa que toca som abre uma "fonte": um anel próprio de quadros
 * (16 bits, estéreo, 48 kHz) que ele vai enchendo pela chamada SIS_SOM_ENVIAR.
 * Além delas há uma fonte de notas, que toca as listas de notas do bip (B3)
 * e os sons do sistema como ondas na placa de som, quando ela existe.
 *
 * A cada tique do relógio (10 ms), o misturador vê onde o DMA está lendo e
 * escreve no anel da placa o pedaço que falta para ficar ~60 ms à frente:
 * soma as fontes quadro a quadro, aplica o volume e corta o que passar de
 * 16 bits (em vez de deixar dar a volta, que faz um estalo). Como nada
 * espera por nada, nenhum programa trava enquanto o som toca. */
#include "hda.h"
#include "relogio.h"
#include "abi.h"
#include "console.h"
#include "cpu.h"
#include "memoria.h"
#include "processo.h"
#include "sistema.h"
#include "texto.h"
#include "configuracoes.h"

#define MAX_FONTES   8
#define FONTE_QUADROS 16384            /* ~340 ms por programa */
#define FRENTE       (HDA_TAXA / 1000 * 60 * HDA_QUADRO)   /* 60 ms à frente do DMA */

typedef struct {
    bool     usada;
    int      pid;
    int16_t *quadros;                   /* pares esquerdo, direito */
    uint32_t ini, qtd;
} fonte_t;

static fonte_t fontes[MAX_FONTES];
static uint32_t escrito;               /* próximo byte do anel da placa a escrever */
static bool ligado;
static int volume = 80;                /* 0 a 100 */
static uint32_t geracao_vista = 0xFFFFFFFFu;

/* Fonte de notas: frequência, quadros que faltam, fase da onda. */
#define MAX_NOTAS 64
static nota_t notas[MAX_NOTAS];
static int notas_ini, notas_qtd;
static uint32_t nota_freq, nota_resta, nota_fase, nota_total;

void misturador_iniciar(void)
{
    ligado = hda_pronto();
    if (!ligado) return;
    escrito = (hda_posicao() + 1920) % HDA_ANEL & ~3u;
}

static void ler_configuracoes(void)
{
    if (config_geracao() == geracao_vista) return;
    geracao_vista = config_geracao();
    const char *v = config_obter("som.volume");
    if (v) {
        int n = 0;
        for (const char *p = v; *p >= '0' && *p <= '9'; p++) n = n * 10 + (*p - '0');
        volume = n > 100 ? 100 : n;
    }
}

static void proxima_nota(void)
{
    if (notas_qtd == 0) { nota_resta = 0; nota_freq = 0; return; }
    nota_t n = notas[notas_ini];
    notas_ini = (notas_ini + 1) % MAX_NOTAS;
    notas_qtd--;
    nota_freq = n.freq;
    nota_total = nota_resta = (uint32_t)n.ms * (HDA_TAXA / 1000);
    nota_fase = 0;
}

bool misturador_notas(const void *lista, int n, bool enfileirar)
{
    if (!ligado) return false;
    bool i0 = int_salva_desliga();
    if (!enfileirar) { notas_ini = 0; notas_qtd = 0; nota_resta = 0; }
    const nota_t *ns = lista;
    for (int i = 0; i < n && notas_qtd < MAX_NOTAS; i++) {
        notas[(notas_ini + notas_qtd) % MAX_NOTAS] = ns[i];
        notas_qtd++;
    }
    if (nota_resta == 0) proxima_nota();
    int_restaura(i0);
    return true;
}

/* Uma amostra da fonte de notas: onda quadrada suave (as bordas sobem em
 * rampa) com um "envelope" que entra e sai em 5 ms, para não estalar. */
static int32_t amostra_nota(void)
{
    if (nota_resta == 0) { proxima_nota(); if (nota_resta == 0) return 0; }
    int32_t v = 0;
    if (nota_freq >= 20) {
        uint32_t periodo = HDA_TAXA / nota_freq;
        if (periodo < 2) periodo = 2;
        uint32_t p = nota_fase % periodo, meio = periodo / 2;
        uint32_t rampa = periodo / 8 + 1;
        int32_t alto = 5000;
        if (p < rampa) v = -alto + (int32_t)(2 * alto * (int32_t)p / (int32_t)rampa);
        else if (p < meio) v = alto;
        else if (p < meio + rampa) v = alto - (int32_t)(2 * alto * (int32_t)(p - meio) / (int32_t)rampa);
        else v = -alto;
        uint32_t ja = nota_total - nota_resta, borda = HDA_TAXA / 200;
        if (ja < borda) v = v * (int32_t)ja / (int32_t)borda;
        if (nota_resta < borda) v = v * (int32_t)nota_resta / (int32_t)borda;
    }
    nota_fase++;
    nota_resta--;
    return v;
}

static bool processo_vivo(int pid)
{
    processo_t *p = processo_por_pid(pid);
    return p && p->estado != P_LIVRE && p->estado != P_ZUMBI;
}

void misturador_tique(void)
{
    if (!ligado) return;
    static uint32_t tiques;
    ++tiques;                                                     /* uma vez, para o diário (G470) */
    if (tiques == 3 * TIQUES_POR_SEGUNDO) hda_diagnostico(0);
    if (tiques == 3 * TIQUES_POR_SEGUNDO + TIQUES_POR_SEGUNDO / 2) hda_diagnostico(1);
    ler_configuracoes();
    uint32_t pos = hda_posicao() & ~3u;
    /* Um programa mandando som e a posição da placa sem andar por 2 s: o
     * fluxo está parado (aconteceu no G470). Anota uma vez, com o estado. */
    static uint32_t pos_antes, parado;
    static bool avisou;
    bool tem_som = false;
    for (int f = 0; f < MAX_FONTES; f++) if (fontes[f].usada && fontes[f].qtd) tem_som = true;
    parado = pos == pos_antes && tem_som ? parado + 1 : 0;
    pos_antes = pos;
    if (parado == 2 * TIQUES_POR_SEGUNDO && !avisou) {
        avisou = true;
        klog("[HDA] a posicao do fluxo nao anda (%u) com som na fila: reprogramando o fluxo\n", pos);
        hda_diagnostico(0);
        hda_diagnostico(1);
        hda_reiniciar_fluxo();
        escrito = 0;
    }
    if (avisou && tem_som && parado == 0) {   /* com som na fila, parado 0 = a posição mudou */
        static bool andou;
        if (!andou) { andou = true; klog("[HDA] o fluxo voltou a andar (posicao %u)\n", pos); }
    }
    uint32_t frente = (escrito - pos + HDA_ANEL) % HDA_ANEL;
    if (frente > HDA_ANEL / 2) {          /* o DMA passou de nós (pausa longa): recomeça logo à frente */
        escrito = (pos + 1920) % HDA_ANEL;
        frente = 1920;
    }
    if (frente >= FRENTE) return;
    uint32_t faltam = (FRENTE - frente) / HDA_QUADRO;
    for (int f = 0; f < MAX_FONTES; f++)
        if (fontes[f].usada && !processo_vivo(fontes[f].pid)) fontes[f].usada = false;   /* o dono saiu */
    int16_t *anel = (int16_t *)hda_anel();
    for (uint32_t q = 0; q < faltam; q++) {
        int32_t esq = 0, dir = 0;
        int32_t n = amostra_nota();
        esq += n; dir += n;
        for (int f = 0; f < MAX_FONTES; f++) {
            fonte_t *fo = &fontes[f];
            if (!fo->usada || fo->qtd == 0) continue;
            esq += fo->quadros[fo->ini * 2];
            dir += fo->quadros[fo->ini * 2 + 1];
            fo->ini = (fo->ini + 1) % FONTE_QUADROS;
            fo->qtd--;
        }
        esq = esq * volume / 100;
        dir = dir * volume / 100;
        if (esq > 32767) esq = 32767;
        if (esq < -32768) esq = -32768;
        if (dir > 32767) dir = 32767;
        if (dir < -32768) dir = -32768;
        uint32_t i = escrito / 2;
        anel[i] = (int16_t)esq;
        anel[i + 1] = (int16_t)dir;
        escrito = (escrito + HDA_QUADRO) % HDA_ANEL;
    }
}

/* ------------------------------------------------------------ sons do sistema */

void misturador_som_sistema(int qual)
{
    const char *s = config_obter("som.sistema");
    if (!ligado || (s && txt_compara(s, "nao") == 0)) return;
    static const nota_t inicio[] = { { 523, 90 }, { 659, 90 }, { 784, 90 }, { 1047, 220 } };
    static const nota_t aviso[] = { { 880, 60 }, { 1175, 90 } };
    static const nota_t pendrive[] = { { 659, 70 }, { 988, 110 } };
    if (qual == SOM_INICIO) misturador_notas(inicio, 4, false);
    else if (qual == SOM_AVISO) misturador_notas(aviso, 2, false);
    else misturador_notas(pendrive, 2, false);
    klog("[SOM] som do sistema %d\n", qual);
}

/* ------------------------------------------------------------ chamadas */

/* SIS_SOM_ABRIR () -> número da fonte, ou ERRO_NAO_EXISTE sem placa de som. */
int32_t sis_som_abrir(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    if (!ligado) return ERRO_NAO_EXISTE;
    for (int f = 0; f < MAX_FONTES; f++) {
        fonte_t *fo = &fontes[f];
        if (fo->usada) continue;
        if (!fo->quadros) fo->quadros = kmalloc(FONTE_QUADROS * HDA_QUADRO);   /* fica para a próxima fonte */
        if (!fo->quadros) return ERRO_SEM_MEMORIA;
        bool i0 = int_salva_desliga();
        fo->ini = fo->qtd = 0;
        fo->pid = atual->pid;
        fo->usada = true;
        int_restaura(i0);
        klog("[SOM] fonte %d aberta por %d\n", f, atual->pid);
        return f;
    }
    return ERRO_LIMITE;
}

/* SIS_SOM_ENVIAR (fonte, &quadros, bytes) -> bytes aceitos (pode ser menos:
 * o programa manda o resto depois). Quadros de 16 bits estéreo a 48 kHz. */
int32_t sis_som_enviar(uint32_t f, uint32_t ptr, uint32_t bytes, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (f >= MAX_FONTES || !fontes[f].usada || fontes[f].pid != atual->pid) return ERRO_DESCRITOR;
    if (!usuario_ok((const void *)ptr, bytes, false)) return ERRO_ENDERECO;
    fonte_t *fo = &fontes[f];
    bool i0 = int_salva_desliga();
    uint32_t cabem = FONTE_QUADROS - fo->qtd, n = bytes / HDA_QUADRO;
    if (n > cabem) n = cabem;
    const int16_t *src = (const int16_t *)ptr;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = (fo->ini + fo->qtd) % FONTE_QUADROS;
        fo->quadros[i * 2] = src[k * 2];
        fo->quadros[i * 2 + 1] = src[k * 2 + 1];
        fo->qtd++;
    }
    int_restaura(i0);
    return (int32_t)(n * HDA_QUADRO);
}

int32_t sis_som_fechar(uint32_t f, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (f >= MAX_FONTES || !fontes[f].usada || fontes[f].pid != atual->pid) return ERRO_DESCRITOR;
    fontes[f].usada = false;
    return 0;
}

/* SIS_SOM_VOLUME (novo ou -1) -> volume atual (0 a 100), ou ERRO_NAO_EXISTE.
 * Mudar o volume grava a configuração "som.volume". */
int32_t sis_som_volume(uint32_t novo, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (!ligado) return ERRO_NAO_EXISTE;
    if ((int32_t)novo >= 0) {
        if (novo > 100) novo = 100;
        char t[8];
        snformatar(t, sizeof t, "%u", novo);
        config_definir("som.volume", t);
        ler_configuracoes();
    }
    return volume;
}
