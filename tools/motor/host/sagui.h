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
static inline size_t tam_texto(const char *s) { return strlen(s); }
static inline int compara_texto(const char *a, const char *b) { return strcmp(a, b); }
static inline char *copia_texto(char *d, const char *s, size_t m) { if (!m) return d; strncpy(d, s, m - 1); d[m - 1] = 0; return d; }
static inline char *junta_texto(char *d, const char *s, size_t m) { size_t k = strlen(d); if (k + 1 < m) copia_texto(d + k, s, m - k); return d; }
static inline char *acha_char(const char *s, int c) { return strchr(s, c); }
static inline bool comeca_com(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
#define formatar_texto snprintf
static inline void registrar(const char *f, ...) { va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); }
static inline unsigned tempo_ms(void) { return (unsigned)(clock() / (CLOCKS_PER_SEC / 1000)); }
typedef struct fonte fonte_t;
fonte_t *fonte_abrir(const uint8_t *d, unsigned n);
void     fonte_fechar(fonte_t *f);
int      fonte_avanco(fonte_t *f, unsigned cp, int px);
bool     fonte_tem(fonte_t *f, unsigned cp);
int      fonte_ascendente(fonte_t *f, int px);
int      fonte_descendente(fonte_t *f, int px);
const uint8_t *fonte_glifo(fonte_t *f, unsigned cp, int px, int *l, int *a, int *ox, int *oy);
bool imagem_decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a);
/* Texto interno do Cipó é Latin-1. Caractere acima de U+00FF vira uma
 * "sequência larga": 0x8F (C1, nunca usado em texto) + 3 bytes 10xxxxxx
 * com 18 bits do código (até U+3FFFF: cobre BMP e os emojis). */
#define LARGA_MARCA 0x8F
static inline int larga_por(char *d, uint32_t cp)
{
    if (cp > 0x3FFFF) { d[0] = '?'; return 1; }
    d[0] = (char)LARGA_MARCA; d[1] = (char)(0x80 | ((cp >> 12) & 63)); d[2] = (char)(0x80 | ((cp >> 6) & 63)); d[3] = (char)(0x80 | (cp & 63));
    return 4;
}
static inline bool eh_larga(const char *p, int resto)
{
    return resto >= 4 && (uint8_t)p[0] == LARGA_MARCA && ((uint8_t)p[1] & 0xC0) == 0x80 && ((uint8_t)p[2] & 0xC0) == 0x80 && ((uint8_t)p[3] & 0xC0) == 0x80;
}
static inline uint32_t larga_ler(const char *p) { return ((uint32_t)(p[1] & 63) << 12) | ((uint32_t)(p[2] & 63) << 6) | (uint32_t)(p[3] & 63); }
