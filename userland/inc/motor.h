/* motor.h - motor de páginas do Cipó: HTML em árvore, cascata CSS, caixas
 * (bloco, inline, float, flex, grid, tabela, posicionadas) e lista de pintura. */
#ifndef SAGUI_MOTOR_H
#define SAGUI_MOTOR_H

typedef struct motor motor_t;

typedef struct {
    /* Baixa um recurso (CSS ou imagem); devolve bloco de aloca ou NULL. */
    uint8_t *(*baixar)(const char *url, unsigned *n);
    /* Decodifica uma imagem já baixada (PNG/JPEG). */
    bool (*imagem)(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a);
    /* Pixels atuais de um <canvas> (data-sagui-canvas); NULL se não houver. */
    const uint32_t *(*tela)(int id, int *l, int *a);
} motor_ganchos_t;

/* Tipos de alvo de clique. */
#define MA_LINK  1     /* href */
#define MA_CAMPO 2     /* <input> de texto: nome do campo, ação do formulário */
#define MA_BOTAO 3     /* botão de envio */

typedef struct {
    int tipo;
    const char *href;      /* link: endereço já resolvido; campo/botão: ação do formulário */
    const char *nome;      /* campo: atributo name */
    const char *fixos;     /* campos escondidos do formulário, "a=1&b=2" */
    const char *metodo;
    void *no;              /* campo/botão: o elemento (para foco e digitação) */
    int js;                /* clique tratado pelo JavaScript (data-sagui-js), ou -1 */
    bool envia;            /* botão que envia o formulário (submit, image, <button> sem type) */
    int campo;             /* data-sagui-campo (campos e botões depois do JavaScript), ou -1 */
} motor_alvo_t;

motor_t *motor_novo(const motor_ganchos_t *g);
void     motor_fim(motor_t *m);
/* Lê o HTML (Latin-1) e as folhas de estilo; não faz layout ainda. */
void     motor_ler(motor_t *m, const char *html, int n, const char *url);
/* Baixa as imagens que faltam; devolve quantas chegaram (chamar de novo até 0). */
int      motor_imagens(motor_t *m, int max);
/* Faz o layout para a largura dada; devolve a altura da página. */
int      motor_layout(motor_t *m, int largura, int altura_tela);
/* Pinta a faixa [y, y + a) da página em px (larg x a, passo = larg). */
void     motor_pintar(motor_t *m, uint32_t *px, int larg, int passo, int y, int a);
/* Alvo no ponto (x, y) da página, ou NULL. */
const motor_alvo_t *motor_alvo(motor_t *m, int x, int y);
const char *motor_titulo(motor_t *m);
uint32_t motor_fundo(motor_t *m);
/* Estatísticas para o log: nós, caixas, comandos de pintura. */
void     motor_numeros(motor_t *m, int *nos, int *cmds, int *imgs);
int      motor_links(motor_t *m);
/* Campo de texto com foco: recebe a digitação dentro da própria caixa. */
void     motor_foco(motor_t *m, const motor_alvo_t *a);   /* NULL tira o foco */
bool     motor_tem_foco(motor_t *m);
const char *motor_foco_valor(motor_t *m);                  /* texto do campo (UTF-8) */
void     motor_foco_definir(motor_t *m, const char *v);    /* troca o texto; refazer o layout depois */
int      motor_foco_campo(motor_t *m);                     /* data-sagui-campo do campo, ou -1 */
bool     motor_focar_campo(motor_t *m, int campo);         /* depois de reler a página */
/* Envio do formulário de quem (campo ou botão; NULL = campo com foco) como
 * endereço GET: acao?nome=valor&... com todos os campos que têm nome. */
bool     motor_envio(motor_t *m, const motor_alvo_t *quem, char *url, int max);
bool     motor_envio_campo(motor_t *m, int campo, char *url, int max);   /* quem = o elemento data-sagui-campo */
/* Caixas dos elementos marcados com data-sagui-n (16 ints por elemento) e estilo computado. */
int      motor_caixas(motor_t *m, int *d, int max);
bool     motor_estilo_de(motor_t *m, int k, const char *prop, char *o, int max);
fonte_t *motor_fonte_nomeada(motor_t *m, const char *familia, int peso, int italico, int largura);
void     motor_fontes_limpar(void);   /* nova página: esquece as fontes da web baixadas */
void     motor_sem_esconder(motor_t *m, bool sim);   /* ignora display:none do autor */   /* quantos links a página tem */
#endif
