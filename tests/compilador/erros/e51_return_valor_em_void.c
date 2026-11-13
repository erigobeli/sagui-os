#include "sagui.h"

static void avisa(int n)
{
    imprimir("%d\n", n);
    return n;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    avisa(1);
    return 0;
}
