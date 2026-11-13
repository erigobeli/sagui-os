#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    int x = argc;
    if (x > 1) imprimir("muitos\n"); imprimir("!\n");
    else imprimir("um\n");
    return 0;
}
