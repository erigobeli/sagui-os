/* gui.h - kit gráfico da libsagui (H22).
 *
 * Tudo aqui roda no próprio programa, no anel 3: o kernel só entrega a área
 * de pixels da janela e os eventos. Cada componente é uma estrutura com a
 * posição e o estado, uma função que desenha e uma que trata um evento. */
#ifndef SAGUI_GUI_H
#define SAGUI_GUI_H
#include "sagui.h"

/* Cores do tema. */
#define COR_FUNDO      0xF4F4F0
#define COR_TEXTO      0x1E2328
#define COR_TEXTO_FRACO 0x6A737C
#define COR_BORDA      0x8A939C
#define COR_BOTAO      0xE2E6EA
#define COR_BOTAO_APERTADO 0xC4CBD2
#define COR_DESTAQUE   0x0B7A3E   /* verde do SaguiOS */
#define COR_SELECAO    0x2A6FD8
#define COR_BRANCO     0xFFFFFF

typedef struct {
    int       id;
    uint32_t *px;
    int       larg, alt;
} jan_t;

/* ---- janela e desenho ---- */
jan_t *jan_abrir(const char *titulo, int largura, int altura);
void   jan_fechar(jan_t *j);
void   jan_mostrar(jan_t *j);                                   /* a janela toda */
/* H78: a janela pode mudar de tamanho (arrastando a borda, ou maximizando).
 * Depois, cada EV_REDIMENSIONAR traz o tamanho novo em e.x e e.y: o programa
 * atualiza j->larg e j->alt (jan_novo_tamanho) e redesenha tudo. */
bool   jan_redimensionavel(jan_t *j, int larg_min, int alt_min);
void   jan_novo_tamanho(jan_t *j, const evento_t *e);
void   jan_mostrar_area(jan_t *j, int x, int y, int l, int a);
void   jan_menu(jan_t *j, int x, int y, const char *itens);       /* menu próprio; escolha em EV_MENU (H81) */
void   g_retangulo(jan_t *j, int x, int y, int l, int a, uint32_t cor);
void   g_moldura(jan_t *j, int x, int y, int l, int a, uint32_t cor);
void   g_ret_redondo(jan_t *j, int x, int y, int l, int a, int raio, uint32_t topo, uint32_t base);   /* borda suavizada */
void   g_contorno_redondo(jan_t *j, int x, int y, int l, int a, int raio, uint32_t cor);
void   g_linha(jan_t *j, int x0, int y0, int x1, int y1, uint32_t cor);
void   g_texto(jan_t *j, int x, int y, const char *t, uint32_t cor);
void   g_texto_n(jan_t *j, int x, int y, const char *t, int n, uint32_t cor);
void   g_texto_forte(jan_t *j, int x, int y, const char *t, uint32_t cor);            /* negrito */
void   g_texto_n_forte(jan_t *j, int x, int y, const char *t, int n, uint32_t cor);
void   g_desenho(jan_t *j, int x, int y, const char *const *linhas, int l, int a,
                 uint32_t (*paleta)(char), int escala);   /* paleta devolve 0xFF000000 = transparente */
void   g_imagem(jan_t *j, int x, int y, const uint32_t *px, int l, int a);   /* ARGB com alfa (H92) */
void   g_relevo(jan_t *j, int x, int y, int l, int a, bool afundado);
int    g_largura_texto(const char *t);
#define LETRA_L 8

/* Fonte suave (tools/mkfonte_suave.py): cada pixel da letra tem um tom de
 * 0 (vazio) a 15 (cheio), já calculado no build; desenhar é só misturar a
 * cor nesse tom, e só nas bordas da letra (o miolo é cor cheia). As faixas
 * sem desenho na fonte suave (controle e 0x80-0x9F, as molduras e blocos do
 * terminal) continuam com a fonte bitmap. */
extern const unsigned char fonte_suave[256][64], fonte_suave_negrito[256][64];
static inline bool letra_suave(unsigned c) { return c >= 0x20 && c != 0x7F && (c < 0x80 || c >= 0xA0); }
static inline int tom_letra(const unsigned char *g, int col, int lin)
{
    unsigned char b = g[lin * 4 + (col >> 1)];
    return col & 1 ? b & 15 : b >> 4;
}
/* fundo levado t/15 na direção da cor, sem divisão (t * 17 / 256 ~ t / 15). */
static inline uint32_t no_tom(uint32_t fundo, uint32_t cor, int t)
{
    int a = t * 17 + (t >> 1);
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int f = (int)((fundo >> s) & 255), c = (int)((cor >> s) & 255);
        r |= (uint32_t)(f + (((c - f) * a + 128) >> 8)) << s;
    }
    return r;
}

#define LETRA_A 16

/* ---- rótulo ---- */
void rotulo(jan_t *j, int x, int y, const char *texto, uint32_t cor);

/* ---- botão ---- */
typedef struct {
    int x, y, l, a;
    const char *texto;
    bool apertado;
} botao_t;
void botao_desenhar(jan_t *j, botao_t *b);
bool botao_tratar(jan_t *j, botao_t *b, const evento_t *e);   /* true = foi clicado */

/* ---- caixa de texto de uma linha ---- */
typedef struct {
    int  x, y, l;
    char texto[128];
    int  cursor;
    bool foco;
} caixa_t;
void caixa_desenhar(jan_t *j, caixa_t *c);
bool caixa_tratar(jan_t *j, caixa_t *c, const evento_t *e);   /* true = Enter */
void caixa_definir(caixa_t *c, const char *texto);

/* ---- área de texto com várias linhas e rolagem ---- */
typedef struct {
    int   x, y, l, a;
    char *buf;            /* texto (Latin-1), terminado em zero */
    int   tam, cap;
    int   cursor;         /* posição no buffer */
    int   topo;           /* primeira linha visível */
    bool  foco;
    bool  modificado;
    bool  somente_leitura;
    int   sel;            /* seleção: a outra ponta + 1 (0 = nada selecionado); vai do sel-1 ao cursor */
    bool  arrastando;     /* botão apertado dentro do texto: mover o mouse estende a seleção */
} area_t;
bool area_iniciar(area_t *a, int x, int y, int l, int al);
void area_definir(area_t *a, const char *texto, int n);
void area_desenhar(jan_t *j, area_t *a);
bool area_tratar(jan_t *j, area_t *a, const evento_t *e);    /* true = mudou o texto */
int  area_linhas_visiveis(area_t *a);

/* ---- lista com seleção ---- */
#define LISTA_NADA       0
#define LISTA_SELECIONOU 1
#define LISTA_ATIVOU     2   /* duplo clique ou Enter */
typedef struct {
    int    x, y, l, a;
    char (*itens)[64];
    int    qtd;
    int    sel;
    int    topo;
    /* Desenha um ícone pequeno (16x16) antes do texto do item, se não for NULL. */
    void (*icone)(jan_t *j, int x, int y, int item);
    /* D058: seleção de vários itens. Com marc != NULL (um byte por item), o
     * clique marca um, Ctrl+clique alterna, Shift+clique marca a faixa,
     * Ctrl+A marca todos, e arrastar no vazio desenha o laço translúcido. */
    uint8_t *marc;
    bool laco;
    int  laco_x0, laco_y0, laco_x1, laco_y1;
    int  laco_linha;                    /* +1: linha onde o laço começou (soltar sem arrastar a seleciona) */
    /* D058: o nome editado na própria linha (lista_editar). */
    bool editando;
    int  ed_item, ed_cur, ed_marca;     /* ed_marca: início da parte marcada (até o cursor) */
    char ed_texto[64];
} lista_t;
void lista_desenhar(jan_t *j, lista_t *l);
int  lista_tratar(jan_t *j, lista_t *l, const evento_t *e);
/* Começa a editar o nome do item i na linha dele, com os primeiros "marcar"
 * caracteres marcados (o nome sem a extensão, por exemplo): digitar troca a
 * parte marcada. Enter termina (LISTA_EDITOU, o texto em l->ed_texto), Esc
 * desiste (LISTA_DESISTIU); clicar fora também termina. */
void lista_editar(jan_t *j, lista_t *l, int i, int marcar);
bool lista_marcado(const lista_t *l, int i);
int  lista_qtd_marcados(const lista_t *l);
#define LISTA_EDITOU   3
#define LISTA_DESISTIU 4
#define LISTA_DEVAGAR  5   /* clique simples no item que já era o único marcado (renomear devagar) */
#define LISTA_ALTURA_ITEM 20

/* ---- diálogos (janelas próprias, esperam a resposta) ---- */
bool confirmar(const char *titulo, const char *pergunta);      /* "Tem certeza?" Sim/Não */
void mensagem(const char *titulo, const char *texto);           /* OK */
bool pedir_texto(const char *titulo, const char *pergunta, char *resposta, int max);

#endif
