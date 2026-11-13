/* goto: sair de laços aninhados e juntar a limpeza num lugar só. */
#include "sagui.h"

static int procura(int m[3][3], int alvo, int *l, int *c)
{
    for (*l = 0; *l < 3; (*l)++)
        for (*c = 0; *c < 3; (*c)++)
            if (m[*l][*c] == alvo) goto achou;
    return 0;
achou:
    return 1;
}

static int processa(int n)
{
    int *a = NULL, *b = NULL, r = -1;
    a = aloca(16);
    if (!a) goto fim;
    if (n < 0) goto fim;
    b = aloca(16);
    if (!b) goto fim;
    a[0] = n; b[0] = n * 2;
    r = a[0] + b[0];
fim:
    if (b) libera(b);
    if (a) libera(a);
    return r;
}

int principal(int argc, char **argv)
{
    int m[3][3] = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } }, l, c, i = 0;
    (void)argc; (void)argv;
    if (procura(m, 6, &l, &c)) imprimir("6 em %d,%d\n", l, c);
    imprimir("%d\n", procura(m, 60, &l, &c));
    imprimir("%d %d\n", processa(5), processa(-1));
de_novo:
    imprimir("%d ", i);
    if (++i < 4) goto de_novo;
    imprimir("\n");
    goto pula;
    imprimir("isto nao aparece\n");
pula:
    imprimir("fim\n");
    return 0;
}
