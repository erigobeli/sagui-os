/* sagui.h de mentira para compilar o motor no Linux (tools/motor). */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#define aloca malloc
#define libera free
#define realoca realloc
static inline size_t tam_texto(const char *s) { return strlen(s); }
static inline int compara_texto(const char *a, const char *b) { return strcmp(a, b); }
static inline int compara_texto_n(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
static inline char *copia_texto(char *d, const char *s, size_t m) { if (!m) return d; strncpy(d, s, m - 1); d[m - 1] = 0; return d; }
static inline char *junta_texto(char *d, const char *s, size_t m) { size_t k = strlen(d); if (k + 1 < m) copia_texto(d + k, s, m - k); return d; }
static inline char *acha_char(const char *s, int c) { return strchr(s, c); }
static inline bool comeca_com(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
#define formatar_texto snprintf
static inline int texto_para_int(const char *s) { return atoi(s); }
static inline void registrar(const char *f, ...) { va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); }
static inline unsigned tempo_ms(void) { return (unsigned)(clock() / (CLOCKS_PER_SEC / 1000)); }
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
bool imagem_decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a);
/* UTF-8: lê um caractere (byte inválido vale por si: cp = byte) e escreve um. */
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
void quebra_linha(const uint32_t *cp, int n, uint8_t *r);
