/* bool: qualquer valor diferente de zero vira 1. */
#include "sagui.h"

static bool par(int n) { return n % 2 == 0; }
static bool entre(int v, int a, int b) { return v >= a && v <= b; }

int principal(int argc, char **argv)
{
    bool a = true, b = false, c = 42, d = -7;
    int *p = &argc;
    bool tem = p;
    (void)argv;
    imprimir("%d %d %d %d %d\n", a, b, c, d, tem);
    imprimir("%d %d %d\n", par(10), par(7), entre(5, 1, 10));
    imprimir("%d %d %d %d\n", a && b, a || b, !a, !b);
    bool liga = false;
    for (int i = 0; i < 5; i++) { liga = !liga; imprimir("%s ", liga ? "sim" : "nao"); }
    imprimir("\n");
    _Bool x = 256;
    imprimir("%d %d\n", x, (int)sizeof(bool));
    return c ? 0 : 1;
}
