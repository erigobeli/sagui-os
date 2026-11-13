/* sagui.h simulado: deixa compilar partes do navegador fora do SaguiOS, para
 * os testes no computador de desenvolvimento. Só os nomes usados lá. */
#ifndef SAGUI_H
#define SAGUI_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
static inline void *aloca(size_t n) { return malloc(n ? n : 1); }
static inline void *aloca_zerado(size_t n) { return calloc(1, n ? n : 1); }
static inline void *realoca(void *p, size_t n) { return realloc(p, n ? n : 1); }
static inline void libera(void *p) { free(p); }
static inline size_t tam_texto(const char *s) { return strlen(s); }
static inline int compara_texto(const char *a, const char *b) { return strcmp(a, b); }
static inline int compara_texto_n(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
static inline char *copia_texto(char *d, const char *s, size_t max) { if (max) { strncpy(d, s, max - 1); d[max - 1] = 0; } return d; }
static inline char *junta_texto(char *d, const char *s, size_t max) { size_t k = strlen(d); return copia_texto(d + k, s, max > k ? max - k : 0), d; }
static inline char *acha_char(const char *s, int c) { return strchr(s, c); }
static inline char *acha_ultimo_char(const char *s, int c) { return strrchr(s, c); }
static inline bool comeca_com(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static inline int texto_para_int(const char *s) { return atoi(s); }
static inline bool eh_espaco(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
#define formatar_texto snprintf
/* Nomes que existem no sagui.h de verdade (para pegar conflitos aqui também). */
int montar(const char *nome);
void reiniciar(void);
void desligar(void);
int abrir(const char *caminho, int modo);
int ler(int fd, void *buf, unsigned n);
int fechar(int fd);
int listar(const char *caminho, int indice, void *e);
static inline void registrar(const char *f, ...) { (void)f; }
#endif
