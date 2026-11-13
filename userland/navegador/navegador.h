/* navegador.h - o Sagui Navegador (fase 6 da versão 2.0).
 *
 * Arquivos:
 *   descompactar.c  DEFLATE, zlib, gzip
 *   png.c, jpeg.c   imagens
 *   html.c          leitura do HTML em uma árvore de elementos
 *   pagina.c        layout: a árvore vira caixas de texto e imagens na tela
 *   navegador.c     janela, barra de endereço, histórico, favoritos */
#ifndef SAGUI_NAVEGADOR_H
#define SAGUI_NAVEGADOR_H
#include "sagui.h"
#include "gui.h"
#include "http.h"

typedef struct {
    uint32_t  larg, alt;
    uint32_t *px;                 /* 0xAARRGGBB */
} imagem_t;

int inflar(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);
int inflar_zlib(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);
int inflar_gzip(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);
imagem_t *png_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro);
imagem_t *jpeg_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro);
imagem_t *imagem_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro);   /* escolhe pelo começo do arquivo */

/* ---- HTML (html.c) ---- */
enum {
    TAG_OUTRA, TAG_HTML, TAG_HEAD, TAG_BODY, TAG_TITLE, TAG_META, TAG_LINK, TAG_STYLE, TAG_SCRIPT, TAG_NOSCRIPT,
    TAG_BASE, TAG_P, TAG_DIV, TAG_SPAN, TAG_A, TAG_B, TAG_STRONG, TAG_I, TAG_EM, TAG_U, TAG_BR, TAG_HR, TAG_IMG,
    TAG_H1, TAG_H2, TAG_H3, TAG_H4, TAG_H5, TAG_H6, TAG_UL, TAG_OL, TAG_LI, TAG_DL, TAG_DT, TAG_DD,
    TAG_BLOCKQUOTE, TAG_PRE, TAG_CODE, TAG_TABLE, TAG_CAPTION, TAG_THEAD, TAG_TBODY, TAG_TFOOT, TAG_TR, TAG_TD,
    TAG_TH, TAG_FORM, TAG_INPUT, TAG_BUTTON, TAG_SELECT, TAG_OPTION, TAG_TEXTAREA, TAG_LABEL, TAG_NAV,
    TAG_HEADER, TAG_FOOTER, TAG_SECTION, TAG_ARTICLE, TAG_ASIDE, TAG_MAIN, TAG_FIGURE, TAG_FIGCAPTION, TAG_SUP,
    TAG_SUB, TAG_SMALL, TAG_CENTER, TAG_FONT, TAG_TT, TAG_KBD, TAG_SAMP, TAG_CITE, TAG_ABBR, TAG_IFRAME, TAG_SVG,
    TAG_MATH, TAG_TEMPLATE, TAG_WBR, TAG_AREA, TAG_COL, TAG_EMBED, TAG_SOURCE, TAG_PARAM, TAG_TRACK, TAG_DETAILS,
    TAG_SUMMARY, TAG_ADDRESS, TAG_VIDEO, TAG_AUDIO, TAG_CANVAS, TAG_OBJECT, TAG_S, TAG_STRIKE, TAG_DEL, TAG_INS,
    TAG_MARK, TAG_Q, TAG_VAR, TAG_BIG, TAG_COLGROUP, TAG_FIELDSET, TAG_LEGEND, TAG_PICTURE, TAG_TIME,
    TAG_QTD
};
#define NO_ELEMENTO 1
#define NO_TEXTO    2

typedef struct no {
    uint8_t     tipo, tag;
    uint32_t    tam;              /* texto: quantos bytes */
    char       *texto;            /* texto (Latin-1), ou o nome de uma tag desconhecida */
    char       *atributos;        /* "nome\0valor\0...\0" */
    struct no  *pai, *filho, *ultimo, *prox;
} no_t;

typedef struct {
    no_t  *raiz;
    char   titulo[128];
    char   base[256];             /* <base href> */
    bool   utf8, charset_definido;
    int    qtd_nos;
    void  *arena;
} documento_t;

/* charset: 1 UTF-8, 0 Latin-1, -1 não sabe (procura o <meta>). */
documento_t *html_ler(const char *d, uint32_t n, int charset);
documento_t *html_texto_puro(const char *d, uint32_t n, int charset);
void         html_liberar(documento_t *doc);
const char  *html_atributo(const no_t *x, const char *nome);
const char  *html_nome_tag(int tag);
void        *arena_aloca(documento_t *doc, uint32_t n);

/* ---- página: layout e desenho (pagina.c) ---- */
#define PECA_TEXTO   1
#define PECA_IMAGEM  2
#define PECA_LINHA   3
#define PECA_FUNDO   4
#define PECA_BORDA   5
#define PECA_CAMPO   6
#define PECA_BOTAO   7
/* marcas */
#define PECA_NEGRITO    1
#define PECA_SUBLINHADO 2
#define PECA_DOBRO      4
#define PECA_RISCADO    8
#define PECA_FIXA       16     /* já tem posição (marcador de lista) */

#define MARGEM_PAGINA 12
#define MAX_LINKS     4000
#define MAX_IMAGENS   300
#define MAX_CAMPOS    32
#define MAX_FORMS     16
#define MAX_ANCORAS   2000

typedef struct {
    uint8_t     tipo, marcas;
    int16_t     link, img, campo;
    int         x, y, l, a;
    uint32_t    cor, fundo;
    const char *texto;
    int         n;
} peca_t;

typedef struct {
    const no_t *no;
    int16_t     form;
    bool        botao;
    char        valor[128];
    int         peca;
} campo_t;

typedef struct {
    const no_t *no;
    const char *src;
    imagem_t   *img;
    int         estado;           /* 0 a buscar, 1 pronta, 2 falhou */
} img_pagina_t;

typedef struct {
    documento_t  *doc;
    peca_t       *pecas;
    int           qtd, cap;
    int           largura, altura;
    uint32_t      fundo;
    const char   *links[MAX_LINKS];
    int           nlinks;
    img_pagina_t  imgs[MAX_IMAGENS];
    int           nimgs;
    campo_t       campos[MAX_CAMPOS];
    int           ncampos;
    const no_t   *forms[MAX_FORMS];
    int           nforms;
    struct { const char *nome; int y; } ancoras[MAX_ANCORAS];
    int           nancoras;
    struct {
        uint8_t tag; const char *classe, *id, *decl; int ndecl;
    }             regras[400];
    int           nregras;
} pagina_t;

pagina_t *pagina_criar(documento_t *doc);
void      pagina_layout(pagina_t *p, int largura);
void      pagina_desenhar(pagina_t *p, jan_t *j, int x, int y, int l, int a, int rolagem, int campo_foco);
int       pagina_peca_em(pagina_t *p, int x, int y);
int       pagina_ancora(pagina_t *p, const char *nome);
void      pagina_liberar(pagina_t *p);

/* ---- endereços (enderecos.c) ---- */
#define MAX_URL_NAV 1024
bool endereco_tem_esquema(const char *u);
void endereco_resolver(const char *base, const char *rel, char *out, int max);
void endereco_codificar(const char *s, char *out, int max);     /* para formulários: espaço vira + */
void endereco_para_rede(const char *u, char *out, int max);     /* acentos e espaços viram %XX; tira o #fragmento */
#endif
