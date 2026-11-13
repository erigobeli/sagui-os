#include "sagui.h"

#define MAIOR(a, b) ((a) > (b) ? (a) : (b))

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    imprimir("%d\n", MAIOR(3));
    return 0;
}
