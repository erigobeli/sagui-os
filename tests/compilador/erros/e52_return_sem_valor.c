#include "sagui.h"

static int dobro(int n)
{
    if (n < 0) return;
    return n * 2;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    imprimir("%d\n", dobro(1));
    return 0;
}
