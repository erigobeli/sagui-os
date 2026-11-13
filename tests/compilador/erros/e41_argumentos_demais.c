#include "sagui.h"

static int soma(int a, int b) { return a + b; }

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    imprimir("%d\n", soma(1, 2, 3));
    return 0;
}
