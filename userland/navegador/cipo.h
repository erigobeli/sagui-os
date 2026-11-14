/* cipo.h - o que as partes do Cipó (o navegador do SaguiOS) dividem entre si.
 *
 * O motor de páginas, as fontes, o JavaScript e o Canvas vieram do pull request
 * #6 (Robson de Lima Barbosa), onde moravam na libsagui. Na 2.1 eles são parte
 * do aplicativo (userland/navegador) e não da libsagui, que é o núcleo ligado
 * em todos os programas (D041, D043). Este cabeçalho junta as declarações que
 * no #6 estavam no sagui.h. Só precisa dos tipos básicos do sagui.h (ou do
 * sagui.h simulado dos testes no computador).
 *
 *   motor/     HTML em árvore, cascata CSS, layout e pintura (motor.h)
 *   motor/quebra.c   quebra de linha do Unicode (UAX #14)
 *   fontes/    TrueType, CFF, WOFF/WOFF2 (com Brotli, RFC 7932)
 *   js/        motor de JavaScript e DOM
 *   graficos/  Canvas 2D, SVG, WebGL e GLSL em software
 *   rede.c     pedidos da página (cookies, fetch/XHR) sobre o HTTP da 2.0 */
#ifndef CIPO_H
#define CIPO_H

/* ---- UTF-8: lê um caractere (byte inválido vale por si: cp = byte) e escreve um ---- */
static inline int utf8_ler(const char *p, int resto, uint32_t *cp)
{
    uint8_t c = (uint8_t)p[0];
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!n || n > resto) { *cp = c; return 1; }
    uint32_t v = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (int k = 1; k < n; k++) { if (((uint8_t)p[k] & 0xC0) != 0x80) { *cp = c; return 1; } v = v << 6 | ((uint8_t)p[k] & 0x3Fu); }
    *cp = v;
    return n;
}
static inline int utf8_por(char *d, uint32_t cp)
{
    if (cp < 0x80) { d[0] = (char)cp; return 1; }
    if (cp < 0x800) { d[0] = (char)(0xC0 | cp >> 6); d[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) { d[0] = (char)(0xE0 | cp >> 12); d[1] = (char)(0x80 | ((cp >> 6) & 63)); d[2] = (char)(0x80 | (cp & 63)); return 3; }
    d[0] = (char)(0xF0 | cp >> 18); d[1] = (char)(0x80 | ((cp >> 12) & 63)); d[2] = (char)(0x80 | ((cp >> 6) & 63)); d[3] = (char)(0x80 | (cp & 63)); return 4;
}

/* ---- quebra de linha (UAX #14): r[i] antes do caractere i: 0 proibido, 1 permitido, 2 obrigatório ---- */
void quebra_linha(const uint32_t *cp, int n, uint8_t *r);

/* ---- fontes (TrueType, CFF, WOFF, WOFF2): avanço em 1/64 px; glifo em cobertura 0..255 ---- */
typedef struct fonte fonte_t;
fonte_t *fonte_abrir(const uint8_t *d, unsigned n);
void     fonte_fechar(fonte_t *f);
int      fonte_avanco(fonte_t *f, unsigned cp, int px);
bool     fonte_tem(fonte_t *f, unsigned cp);
int      fonte_ascendente(fonte_t *f, int px);
int      fonte_descendente(fonte_t *f, int px);
int      fonte_altura_normal(fonte_t *f, int px);
const uint8_t *fonte_glifo(fonte_t *f, unsigned cp, int px, int *l, int *a, int *ox, int *oy);
/* Fonte variável: instância com os eixos dados (tag OpenType, valor x 100); a base continua dona das tabelas. */
fonte_t *fonte_instancia(fonte_t *b, const uint32_t *tags, const int *valores100, int n);
bool     fonte_variavel(fonte_t *f);
fonte_t *fonte_obliqua(fonte_t *b);   /* itálico sintético */
/* Fontes do sistema (DejaVu em /fontes): g 0 sans-serif, 1 serif, 2 monospace. Uma vez por processo. */
fonte_t *fonte_sistema(int g, int negrito, int italico);
/* Brotli (RFC 7932), para as fontes WOFF2. */
uint8_t *brotli_descomprimir(const uint8_t *in, unsigned n, unsigned *saida_n, unsigned max);

/* ---- imagens: PNG, JPEG (também progressivo), GIF e WebP para pixels 0xAARRGGBB ---- */
bool imagem_decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a);

/* ---- rede da página (rede.c) ---- */
/* Cookies (RFC 6265): pote do processo. Set-Cookie e document.cookie gravam; pedidos levam. */
void cookie_guardar(const char *url, const char *set_cookie, bool de_js);
int  cookie_texto(const char *url, char *o, int max, bool de_js);
/* Método, cabeçalhos e corpo do próximo pedido (fetch/XHR com POST); NULL volta ao GET. */
void http_preparar(const char *metodo, const char *cabecalhos, const void *corpo, int n);

/* ---- JavaScript (js/js.c) ---- */
typedef struct {
    uint8_t *(*baixar)(const char *url, unsigned *n);      /* devolve bloco de aloca */
    void (*titulo)(const char *t);                         /* document.title = */
    void (*alerta)(const char *t);
    /* resposta completa (fetch): corpo de qualquer código, código, cabeçalhos e URL final */
    uint8_t *(*resposta)(const char *url, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax);
    /* Layout síncrono (o script pediu uma medida): diagrama o HTML e preenche
     * 16 ints por elemento data-sagui-n (motor_caixas). 0 = não deu. */
    int (*layout)(const char *html, int n, int *caixas, int qtd);
    /* getComputedStyle do elemento k do último layout. */
    bool (*estilo)(int k, const char *prop, char *o, int max);
    /* Fonte de uma família do @font-face da página (canvas, document.fonts); NULL se não há. */
    struct fonte *(*fonte)(const char *familia, int peso, int italico, int largura);
    /* iframes: diagrama o HTML do quadro e pinta na tela tela2d (l x a); e acha o
     * elemento clicado nele (data-sagui-js do alvo, ou -1) */
    int (*quadro_pintar)(int tela, const char *html, int n, const char *url, int l, int a);
    int (*quadro_alvo)(int tela, int x, int y);
} js_hooks_t;
/* Roda os scripts da página e devolve o HTML do DOM resultante (NULL: nada mudou). */
char *js_pagina(const char *html, int n, const char *url, const js_hooks_t *h, int *saida_n);
void  js_clicar_quadro(int tela, int x, int y);   /* clique em (x, y) dentro do iframe pintado na tela */
char *js_clicar(int id, int *saida_n);            /* HTML novo depois do clique, ou NULL */
int   js_clique_cancelado(void);                  /* o último clique teve preventDefault */
void  js_fim(void);
char *js_tique(int ms, int *saida_n);   /* timers depois do load; HTML novo se o DOM mudou */
int   js_vivo(void);                    /* ainda há timers na página */
int   js_animando(void);                /* requestAnimationFrame pendente: ritmo de quadro */
int   js_ativo(void);                   /* a página tem JavaScript rodando */
/* Janela do navegador: tamanho da área da página e rolagem (innerWidth, scrollY...). */
void  js_janela(int l, int a, int rx, int ry);
/* A página rolou ou mudou de tamanho: eventos scroll/resize e observadores; HTML novo se mudou. */
char *js_rolar(int rx, int ry, int *saida_n);
char *js_redimensionar(int l, int a, int *saida_n);
int   js_rolagem_pedida(int *x, int *y);                    /* scrollTo do script: 1 = há pedido */
int   js_navegacao(char *url, int max, int *substituir);   /* location.href= / assign / replace / reload */
/* Campo n (data-sagui-campo) recebeu valor e a tecla (13 Enter, 8 apagar, ou o caractere):
 * roda keydown/input/keyup; HTML novo se os scripts mudaram o DOM. */
char *js_campo(int n, const char *valor, int tecla, int *saida_n);
/* Envio do formulário do campo n: evento submit; 1 = o script cancelou (preventDefault). */
int   js_enviar(int n, char **html, int *saida_n);
const char *js_video(void);   /* URL que o JS deu a um <video>, ou NULL */

/* ---- Canvas 2D (graficos/canvas2d.c): telas com pixels ARGB sem pré-multiplicar ---- */
const uint32_t *tela2d_pixels(int id, int *l, int *a);
uint32_t *tela2d_px_escrita(int id, int *l, int *a);   /* para pintar nela (iframes) */
unsigned tela2d_versao(void);                          /* muda a cada desenho */
void     tela2d_liberar_todas(void);
#endif
