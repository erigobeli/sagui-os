/* Contas com ponteiros: andam no tamanho do tipo apontado. */
#include "sagui.h"

typedef struct { int a; char b; } par_t;

int principal(int argc, char **argv)
{
    int v[6] = { 10, 20, 30, 40, 50, 60 };
    int *p = v, *fim = v + 6;
    (void)argc; (void)argv;
    imprimir("%d %d %d\n", *(p + 2), p[3], *(v + 5));
    p += 2;
    imprimir("%d %d %d\n", *p, (int)(p - v), (int)(fim - p));
    p++;
    imprimir("%d %d\n", *p, p[-1]);
    int soma = 0;
    for (int *q = v; q < fim; q++) soma += *q;
    imprimir("%d\n", soma);
    short s[4] = { 1, 2, 3, 4 };
    short *ps = s + 3;
    imprimir("%d %d\n", *ps, (int)((char *)ps - (char *)s));
    par_t pares[3] = { { 1, 'a' }, { 2, 'b' }, { 3, 'c' } };
    par_t *pp = pares;
    pp += 2;
    imprimir("%d %c %d\n", pp->a, pp->b, (int)(pp - pares));
    pp--;
    imprimir("%d %c\n", (*pp).a, (pp - 1)->b);
    char *t = "abcdef";
    imprimir("%s %c %c\n", t + 3, *(t + 1), 2[t]);
    imprimir("%d\n", &v[4] > &v[1]);
    imprimir("%d\n", *(fim - 1) - *v);
    return 0;
}
