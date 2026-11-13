/* Ponteiros: endereço, valor apontado, ponteiro para ponteiro, NULL. */
#include "sagui.h"

static void troca(int *a, int *b) { int t = *a; *a = *b; *b = t; }
static void zera(int **pp) { **pp = 0; *pp = NULL; }
static int tamanho(const char *s) { const char *p = s; while (*p) p++; return (int)(p - s); }

int principal(int argc, char **argv)
{
    int x = 10, y = 20;
    int *p = &x, **pp = &p;
    (void)argc; (void)argv;
    *p += 5;
    troca(&x, &y);
    imprimir("%d %d %d\n", x, y, **pp);
    p = &y;
    imprimir("%d\n", **pp);
    zera(pp);
    imprimir("%d %d\n", y, p == NULL);
    char texto[] = "macaco";
    char *c = texto;
    while (*c) { if (*c == 'a') *c = 'A'; c++; }
    imprimir("%s %d\n", texto, tamanho(texto));
    void *v = texto;
    char *volta = v;
    imprimir("%c\n", volta[1]);
    int n = 0x11223344;
    unsigned char *b = (unsigned char *)&n;
    imprimir("%x %x %x %x\n", b[0], b[1], b[2], b[3]);     /* o byte de baixo vem primeiro */
    const char *nomes[] = { "um", "dois", "tres", NULL };
    for (const char **q = nomes; *q; q++) imprimir("%s ", *q);
    imprimir("\n");
    if (!p) imprimir("p esta vazio\n");
    return 0;
}
