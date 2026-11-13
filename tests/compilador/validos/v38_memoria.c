/* aloca, aloca_zerado, realoca e libera. */
#include "sagui.h"

typedef struct { int *itens; int qtd, cap; } vetor_t;

static void junta(vetor_t *v, int x)
{
    if (v->qtd == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 4;
        v->itens = realoca(v->itens, (size_t)v->cap * sizeof(int));
    }
    v->itens[v->qtd++] = x;
}

static char *duplica(const char *s)
{
    size_t n = tam_texto(s) + 1;
    char *d = aloca(n);
    memcpy(d, s, n);
    return d;
}

int principal(int argc, char **argv)
{
    vetor_t v = { NULL, 0, 0 };
    long soma = 0;
    (void)argc; (void)argv;
    for (int i = 0; i < 1000; i++) junta(&v, i * 3);
    for (int i = 0; i < v.qtd; i++) soma += v.itens[i];
    imprimir("%d %d %d %d\n", v.qtd, v.cap >= 1000, v.itens[999], (int)soma);
    libera(v.itens);
    unsigned char *z = aloca_zerado(256);
    int sujos = 0;
    for (int i = 0; i < 256; i++) if (z[i]) sujos++;
    imprimir("%d\n", sujos);
    libera(z);
    char *nomes[5];
    const char *base[5] = { "sagui", "mico-leao", "bugio", "muriqui", "macaco-prego" };
    for (int i = 0; i < 5; i++) nomes[i] = duplica(base[i]);
    nomes[2][0] = 'B';
    for (int i = 4; i >= 0; i--) { imprimir("%s ", nomes[i]); libera(nomes[i]); }
    imprimir("\n");
    int **tabela = aloca(4 * sizeof(int *));
    for (int i = 0; i < 4; i++) {
        tabela[i] = aloca(4 * sizeof(int));
        for (int j = 0; j < 4; j++) tabela[i][j] = i * j;
    }
    imprimir("%d %d\n", tabela[3][3], tabela[2][1]);
    for (int i = 0; i < 4; i++) libera(tabela[i]);
    libera(tabela);
    void *grande = aloca(300000);
    memset(grande, 0x5A, 300000);
    imprimir("%d\n", ((unsigned char *)grande)[299999]);
    libera(grande);
    return 0;
}
