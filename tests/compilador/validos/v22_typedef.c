/* typedef: apelidos para tipos, inclusive ponteiros para função e vetores. */
#include "sagui.h"

typedef unsigned char byte;
typedef byte bloco_t[8];
typedef int (*conta_t)(int, int);
typedef struct par { int a, b; } par_t, *par_ptr;
typedef const char *texto_t;

static int soma(int a, int b) { return a + b; }
static int produto(int a, int b) { return a * b; }

static int aplica(conta_t f, par_ptr p) { return f(p->a, p->b); }

static unsigned soma_bloco(bloco_t b)
{
    unsigned s = 0;
    for (int i = 0; i < 8; i++) s += b[i];
    return s;
}

int principal(int argc, char **argv)
{
    bloco_t b = { 1, 2, 3, 4, 250, 250, 250, 250 };
    par_t p = { 6, 7 };
    conta_t f = soma;
    texto_t t = "apelido";
    typedef int inteiro;
    inteiro n = 5;
    (void)argc; (void)argv;
    imprimir("%u %d\n", soma_bloco(b), (int)sizeof(bloco_t));
    imprimir("%d %d\n", aplica(f, &p), aplica(produto, &p));
    imprimir("%s %d\n", t, n);
    size_t tam = sizeof(par_t);
    uint16_t curto = 65535;
    int32_t longo = -1;
    imprimir("%u %u %d\n", (unsigned)tam, curto, longo);
    return 0;
}
