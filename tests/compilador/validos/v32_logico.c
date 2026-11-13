/* && e || param no meio; ?: escolhe um dos dois lados. */
#include "sagui.h"

static int chamadas;
static int marca(int v) { chamadas++; return v; }

static int absoluto(int x) { return x < 0 ? -x : x; }
static int limita(int v, int min, int max) { return v < min ? min : v > max ? max : v; }

int principal(int argc, char **argv)
{
    int *nulo = NULL;
    int x = 5;
    (void)argc; (void)argv;
    imprimir("%d %d %d %d\n", 1 && 1, 1 && 0, 0 || 0, 0 || 3);
    chamadas = 0;
    if (marca(0) && marca(1)) imprimir("nao\n");
    imprimir("%d\n", chamadas);
    chamadas = 0;
    if (marca(1) || marca(1)) imprimir("sim\n");
    imprimir("%d\n", chamadas);
    if (nulo && *nulo == 3) imprimir("nao\n");          /* o segundo teste nem roda */
    if (!nulo || *nulo == 3) imprimir("vazio\n");
    imprimir("%d %d %d\n", absoluto(-9), limita(50, 0, 10), limita(-50, 0, 10));
    imprimir("%s\n", x > 3 ? x > 4 ? "maior que 4" : "igual a 4" : "pequeno");
    int r = x > 0 && x < 10 && x != 7;
    imprimir("%d %d %d\n", r, !r, !!x);
    chamadas = 0;
    int v = marca(0) ? marca(10) : marca(20);
    imprimir("%d %d\n", v, chamadas);
    int a = 0, b = 0;
    *(x > 3 ? &a : &b) = 9;
    imprimir("%d %d\n", a, b);
    imprimir("%d\n", (x == 5) + (x != 5) * 10 + (x >= 5 && x <= 5) * 100);
    int n = 0;
    while (n < 10 && (n * n < 50 || n == 0)) n++;
    imprimir("%d\n", n);
    return 0;
}
