/* Cabeçalho do teste v37: tipos e funções divididos com o .c. */
#ifndef V37_CABECALHO_H
#define V37_CABECALHO_H
#include "sagui.h"

#define MAX_ITENS 4

typedef struct {
    const char *nome;
    int preco;      /* em centavos */
} item_t;

int total(const item_t *itens, int n);
static inline int reais(int centavos) { return centavos / 100; }

#endif
