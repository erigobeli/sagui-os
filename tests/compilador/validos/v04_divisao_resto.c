/* Divisão e resto: com sinal arredonda para o zero; sem sinal é outra conta. */
#include "sagui.h"

static int mdc(int a, int b) { while (b) { int r = a % b; a = b; b = r; } return a; }

int principal(int argc, char **argv)
{
    int v[] = { 7, -7, 100, -100, 1, 0, 2147483647 };
    int d[] = { 2, -2, 3, 7, -1 };
    (void)argc; (void)argv;
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 5; j++) imprimir("%d/%d=%d r%d  ", v[i], d[j], v[i] / d[j], v[i] % d[j]);
        imprimir("\n");
    }
    unsigned u = 0xF0000000u;
    imprimir("%u %u %d\n", u / 16, u % 7, (int)u / 16);
    imprimir("mdc(1071, 462) = %d\n", mdc(1071, 462));
    int n = 1234567;
    imprimir("digitos ao contrario:");
    while (n) { imprimir(" %d", n % 10); n /= 10; }
    imprimir("\n");
    return 0;
}
