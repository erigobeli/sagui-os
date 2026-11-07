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
void     motor_sem_esconder(motor_t *m, bool sim);   /* ignora display:none do autor */   /* quantos links a página tem */
#endif
