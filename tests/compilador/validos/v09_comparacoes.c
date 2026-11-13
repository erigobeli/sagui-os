/* Comparações com e sem sinal, e os operadores == != < <= > >=. */
#include "sagui.h"

static const char *ordem(int a, int b) { return a < b ? "menor" : a > b ? "maior" : "igual"; }

int principal(int argc, char **argv)
{
    int a = -5, b = 3;
    unsigned ua = 5, ub = 4000000000u;
    char c = -1;
    unsigned char uc = 255;
    (void)argc; (void)argv;
    imprimir("%d %d %d %d %d %d\n", a < b, a <= b, a > b, a >= b, a == b, a != b);
    imprimir("%d %d %d\n", ua < ub, ub > 3000000000u, (int)ub < 0);
    imprimir("%d %d\n", c == uc, c < uc);
    imprimir("%s %s %s\n", ordem(1, 2), ordem(2, 1), ordem(7, 7));
    imprimir("%d\n", a < b == 1);
    imprimir("%d %d\n", -1 < 1u, -1 < 1);       /* com unsigned no meio, -1 é enorme */
    int maior = a;
    if (b > maior) maior = b;
    imprimir("maior: %d\n", maior);
    short s = -1;
    unsigned short us = 65535;
    imprimir("%d %d\n", s == us, s < us);
    return 0;
}
