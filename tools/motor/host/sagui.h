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
