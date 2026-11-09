/* Roda o JavaScript de uma página no Linux (32 bits), como o Cipó faz, e
 * grava o HTML resultante. Recursos vêm do curl.
 * Uso: tj pagina.html URL saida.html */
#include "sagui.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint8_t *curl(const char *u, unsigned *n, int *codigo)
{
    char cmd[8192];
    snprintf(cmd, sizeof cmd, "curl -sL --compressed -m 20 -w '\\n%%{http_code}' -A 'Mozilla/5.0 (compatible; SaguiOS)' '%s'", u);
    FILE *f = popen(cmd, "r");
    if (!f) return NULL;
    size_t cap = 1 << 16, k = 0; uint8_t *d = malloc(cap);
    size_t r;
    while ((r = fread(d + k, 1, cap - k, f)) > 0) { k += r; if (k == cap) d = realloc(d, cap *= 2); }
    pclose(f);
    size_t e = k; while (e && d[e - 1] != '\n') e--;
    if (codigo) *codigo = atoi((char *)d + e);
    k = e ? e - 1 : 0;
    d = realloc(d, k + 1); d[k] = 0; *n = (unsigned)k;
    fprintf(stderr, "[baixar] %s -> %u bytes\n", u, *n);
    return d;
}
static uint8_t *baixar(const char *u, unsigned *n) { int c; uint8_t *d = curl(u, n, &c); if (c >= 400) { free(d); return NULL; } return d; }
static void titulo(const char *t) { fprintf(stderr, "[titulo] %s\n", t); }
static void alerta(const char *t) { fprintf(stderr, "[alert] %s\n", t); }
static uint8_t *resposta(const char *u, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax)
{
    cab[0] = 0; snprintf(final, fmax, "%s", u); (void)cmax;
    return curl(u, n, codigo);
}
/* iframes no host: não pinta; QALVO=n diz qual elemento um clique no quadro acerta */
static int q_pintar(int tela, const char *html, int n, const char *url, int l, int a) { (void)html; fprintf(stderr, "[quadro] tela %d %dx%d %s (%d bytes)\n", tela, l, a, url, n); return 1; }
static int q_alvo(int tela, int x, int y) { (void)tela; (void)x; (void)y; return getenv("QALVO") ? atoi(getenv("QALVO")) : -1; }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *h = malloc(n + 1); fread(h, 1, n, f); h[n] = 0; fclose(f);
    static const js_hooks_t g = { baixar, titulo, alerta, resposta, NULL, NULL, NULL, q_pintar, q_alvo };
    int sn = 0;
    unsigned t0 = tempo_ms();
    char *s = js_pagina(h, (int)n, argv[2], &g, &sn);
    fprintf(stderr, "[tempo] %u ms\n", tempo_ms() - t0);
    /* timers usam o relógio de verdade: tiques com pausa (TIQUES=n, padrão 3) */
    int nt = getenv("TIQUES") ? atoi(getenv("TIQUES")) : 3;
    if (getenv("QCLIQUE")) js_clicar_quadro(atoi(getenv("QCLIQUE")), 1, 1);
    for (int i = 0; i < nt && js_vivo(); i++) { struct timespec ts = { 0, 100000000 }; nanosleep(&ts, NULL); int k; char *t = js_tique(100, &k); if (t) { fprintf(stderr, "[tique %d] html novo %d bytes\n", i, k); free(s); s = t; sn = k; } }
    FILE *o = fopen(argv[3], "wb");
    if (s) fwrite(s, 1, sn, o); else fwrite(h, 1, n, o);
    fclose(o);
    return 0;
}
