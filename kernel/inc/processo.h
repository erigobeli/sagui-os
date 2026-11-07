/* processo.h - processos e escalonador. */
#ifndef SAGUI_PROCESSO_H
#define SAGUI_PROCESSO_H
#include "tipos.h"
#include "cpu.h"
#include "paginacao.h"
#include "caminho.h"

#define MAX_PROCESSOS   64
#define MAX_ARQUIVOS    16
#define PILHA_KERNEL    8192     /* 2 páginas por processo */
#define QUANTUM_TIQUES  2        /* 20 ms de CPU antes de passar a vez */

typedef enum { P_LIVRE, P_PRONTO, P_EXECUTANDO, P_BLOQUEADO, P_ZUMBI } estado_t;

struct arquivo;

typedef struct processo {
    int         pid;
    int         pai;
    estado_t    estado;
    char        nome[32];
    bool        usuario;          /* programa no anel 3 (ou tarefa do kernel) */
    espaco_t    espaco;
    uint32_t    pilha_kernel;     /* base (física = virtual) */
    quadro_t   *quadro;           /* onde os registradores ficaram salvos */
    const void *espera;           /* "canal" em que está bloqueado */
    uint32_t    acordar_em;       /* para dormir: tique em que acorda (0 = não) */
    int         codigo_saida;
    char        motivo[64];       /* por que terminou, se foi encerrado pelo kernel */
    struct arquivo *arquivos[MAX_ARQUIVOS];
    char        pasta[CAMINHO_MAX];
    uint32_t    dados_inicio, dados_fim;   /* área que cresce com aumentar_memoria */
    uint32_t    tiques_cpu;
    uint8_t     fpu[512] __attribute__((aligned(16)));   /* área do fxsave */
} processo_t;

extern processo_t *atual;

void        processos_iniciar(void);
processo_t *processo_novo(const char *nome, int pai);
processo_t *tarefa_kernel(const char *nome, void (*funcao)(void *), void *arg);
processo_t *processo_por_pid(int pid);
processo_t *processo_indice(int i);

quadro_t *escalonador_tique(quadro_t *q);     /* chamado pelo relógio */
quadro_t *escalonar(quadro_t *q);             /* passa a vez agora */
void      bloquear(const void *canal);
void      ceder(void);   /* tarefa do kernel: passa a vez agora (ex.: depois de bloquear) */
void      acordar(const void *canal);
void      processo_encerrar(processo_t *p, int codigo, const char *motivo);
void      processo_liberar(processo_t *p);
const char *nome_estado(estado_t e);
void      processo_preparar_usuario(processo_t *p, uint32_t entrada, uint32_t pilha);
int       processo_executar(const char *caminho, int argc, char *const argv[], processo_t *pai);

#define PILHA_USUARIO_TOPO  USUARIO_FIM
#define PILHA_USUARIO_TAM   (64u * 1024u)
#define DADOS_LIMITE        (USUARIO_FIM - PILHA_USUARIO_TAM - 4096u)

#endif
