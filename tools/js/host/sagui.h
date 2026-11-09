/* sagui.h de mentira para compilar o motor JS no Linux (tools/js): o do motor
 * de layout mais o que o JavaScript usa da libsagui. */
#ifndef SAGUI_HOST_JS
#define SAGUI_HOST_JS
#include "../../motor/host/sagui.h"
#include <stdbool.h>
#define _dados_ini __data_start
#define _dados_fim _end
typedef struct { unsigned short ano; unsigned char mes, dia, hora, minuto, segundo, dia_semana; } data_t;
static inline int ler_data(data_t *d) { time_t t = time(0); struct tm *m = gmtime(&t); d->ano = (unsigned short)(m->tm_year + 1900); d->mes = (unsigned char)(m->tm_mon + 1); d->dia = (unsigned char)m->tm_mday; d->hora = (unsigned char)m->tm_hour; d->minuto = (unsigned char)m->tm_min; d->segundo = (unsigned char)m->tm_sec; d->dia_semana = (unsigned char)m->tm_wday; return 0; }
void http_preparar(const char *metodo, const char *cabecalhos, const void *corpo, int n);
void cookie_guardar(const char *url, const char *set_cookie, bool de_js);
int  cookie_texto(const char *url, char *o, int max, bool de_js);
typedef struct {
    uint8_t *(*baixar)(const char *url, unsigned *n);
    void (*titulo)(const char *t);
    void (*alerta)(const char *t);
    uint8_t *(*resposta)(const char *url, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax);
    int (*layout)(const char *html, int n, int *caixas, int qtd);
    bool (*estilo)(int k, const char *prop, char *o, int max);
    struct fonte *(*fonte)(const char *familia, int peso, int italico, int largura);
    int (*quadro_pintar)(int tela, const char *html, int n, const char *url, int l, int a);
    int (*quadro_alvo)(int tela, int x, int y);
} js_hooks_t;
void js_clicar_quadro(int tela, int x, int y);
char *js_pagina(const char *html, int n, const char *url, const js_hooks_t *h, int *saida_n);
const uint32_t *tela2d_pixels(int id, int *l, int *a);
uint32_t *tela2d_px_escrita(int id, int *l, int *a);
unsigned tela2d_versao(void);
void tela2d_liberar_todas(void);
char *js_clicar(int id, int *saida_n);
int   js_clique_cancelado(void);
void js_fim(void);
char *js_tique(int ms, int *saida_n);
int   js_vivo(void);
int   js_animando(void);
int   js_ativo(void);
#endif
