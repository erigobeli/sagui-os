/* Inteiros sem sinal: dão a volta em 2^32 e comparam de outro jeito. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    unsigned a = 4000000000u, b = 500000000u;
    unsigned zero = 0;
    int negativo = -1;
    (void)argc; (void)argv;
    imprimir("%u %u %u\n", a + b, a - b, b - a);
    imprimir("%u %u\n", zero - 1, (unsigned)negativo);
    imprimir("%u %u\n", a / 3, a % 1000);
    imprimir("%d %d\n", a > b, negativo > 0);
    imprimir("%d\n", (unsigned)negativo > a);       /* -1 vira 4294967295 */
    imprimir("%d\n", negativo < (int)b);
    imprimir("%x %x\n", a, 0xFFFFFFFFu);
    unsigned f = 1;
    for (unsigned i = 1; i <= 12; i++) f *= i;
    imprimir("12! = %u\n", f);
    unsigned h = 2166136261u;
    const char *t = "sagui";
    for (int i = 0; t[i]; i++) h = (h ^ (unsigned char)t[i]) * 16777619u;
    imprimir("hash = %u\n", h);
    return 0;
}
