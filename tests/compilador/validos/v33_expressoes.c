/* Expressões compostas: vírgula, conversões no meio das contas, precedência. */
#include "sagui.h"

static int g;
static int efeito(int v) { g = g * 10 + v; return v; }

int principal(int argc, char **argv)
{
    int a, b, c;
    (void)argc; (void)argv;
    a = (b = 3, c = b * 2, b + c);
    imprimir("%d %d %d\n", a, b, c);
    for (a = 0, b = 10; a < b; a++, b--) ;
    imprimir("%d %d\n", a, b);
    imprimir("%d\n", 1 + 2 * 3 - 4 / 2 % 3 << 1 & 0xFF | 1 ^ 2);
    imprimir("%d %d\n", 10 - 4 - 3, 100 / 10 / 5);
    imprimir("%d %d\n", -2 * -3, - -5);
    imprimir("%d\n", (a = 4) * a);
    a = efeito(1) + efeito(2) * efeito(3);
    imprimir("%d %d\n", a, g);
    unsigned char uc = 200;
    int soma = uc + uc;                 /* a conta é feita em int: não estoura */
    unsigned char corta = (unsigned char)(uc + uc);
    imprimir("%d %d\n", soma, corta);
    int grande = 70000;
    short pequeno = (short)grande;
    imprimir("%d %d\n", pequeno, (char)grande);
    unsigned u = 3;
    imprimir("%d %d\n", (int)(u - 5) / 2, (u - 5) / 2 > 100);
    int x = 7;
    x = x++ * 0 + x;                    /* evite isto em programas de verdade */
    imprimir("%d\n", x > 0);
    imprimir("%d\n", ((1 ? 2 : 3) ? 4 : 5) + (0 ? 1 : 0 ? 2 : 3));
    char *p = "abc";
    imprimir("%c %c %d\n", *p + 1, *(p + 1), p[2] - p[0]);
    imprimir("%d\n", !a + ~b * -c);
    return 0;
}
