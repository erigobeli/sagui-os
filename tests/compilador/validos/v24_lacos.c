/* for, while, do-while, break e continue. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int i, n;
    (void)argc; (void)argv;
    for (i = 0; i < 5; i++) imprimir("%d ", i);
    imprimir("\n");
    i = 10;
    while (i > 0) { imprimir("%d ", i); i -= 3; }
    imprimir("\n");
    i = 100;
    do { imprimir("%d ", i); i++; } while (i < 100);        /* roda ao menos uma vez */
    imprimir("\n");
    for (i = 0; ; i++) {
        if (i % 2) continue;
        if (i > 8) break;
        imprimir("%d ", i);
    }
    imprimir("\n");
    n = 0;
    for (int a = 1; a <= 9; a++)
        for (int b = a; b <= 9; b++) {
            if (a * b > 20) break;
            n++;
        }
    imprimir("%d\n", n);
    for (int a = 0, b = 10; a < b; a += 2, b--) imprimir("(%d,%d)", a, b);
    imprimir("\n");
    n = 27;
    int passos = 0;
    while (n != 1) { n = n % 2 ? 3 * n + 1 : n / 2; passos++; }
    imprimir("collatz(27): %d passos\n", passos);
    for (i = 3; i--; ) imprimir("%d", i);
    imprimir("\n");
    i = 0;
    for (;;) { if (++i == 1000) break; }
    imprimir("%d\n", i);
    int linhas = 4;
    for (int l = 1; l <= linhas; l++) {
        for (int e = 0; e < linhas - l; e++) imprimir(" ");
        for (int c = 0; c < 2 * l - 1; c++) imprimir("*");
        imprimir("\n");
    }
    return 0;
}
