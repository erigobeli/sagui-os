/* Ponteiros para função: tabelas de operações e funções que recebem funções. */
#include "sagui.h"

static int mais(int a, int b) { return a + b; }
static int menos(int a, int b) { return a - b; }
static int vezes(int a, int b) { return a * b; }

typedef struct { char simbolo; int (*faz)(int, int); } operacao_t;

static const operacao_t tabela[] = { { '+', mais }, { '-', menos }, { '*', vezes } };

static void para_cada(int *v, int n, void (*f)(int *)) { for (int i = 0; i < n; i++) f(&v[i]); }
static void dobra(int *x) { *x *= 2; }
static void nega(int *x) { *x = -*x; }

static int conta_se(const int *v, int n, bool (*teste)(int))
{
    int c = 0;
    for (int i = 0; i < n; i++) if (teste(v[i])) c++;
    return c;
}
static bool positivo(int x) { return x > 0; }
static bool par(int x) { return x % 2 == 0; }

static int (*escolhe(char c))(int, int)
{
    for (unsigned i = 0; i < sizeof tabela / sizeof tabela[0]; i++)
        if (tabela[i].simbolo == c) return tabela[i].faz;
    return NULL;
}

int principal(int argc, char **argv)
{
    int v[5] = { 1, -2, 3, -4, 5 };
    int (*f)(int, int) = mais;
    (void)argc; (void)argv;
    imprimir("%d %d\n", f(2, 3), (*f)(4, 5));
    f = &vezes;
    imprimir("%d\n", f(6, 7));
    for (unsigned i = 0; i < 3; i++) imprimir("8 %c 2 = %d\n", tabela[i].simbolo, tabela[i].faz(8, 2));
    para_cada(v, 5, dobra);
    para_cada(v, 5, nega);
    for (int i = 0; i < 5; i++) imprimir("%d ", v[i]);
    imprimir("\n%d %d\n", conta_se(v, 5, positivo), conta_se(v, 5, par));
    imprimir("%d %d\n", escolhe('-')(10, 4), escolhe('?') == NULL);
    void (*passos[2])(int *) = { dobra, nega };
    int x = 21;
    for (int i = 0; i < 2; i++) passos[i](&x);
    imprimir("%d %d\n", x, f == vezes);
    return 0;
}
