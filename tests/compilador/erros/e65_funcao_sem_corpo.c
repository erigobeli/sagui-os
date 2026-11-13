#include "sagui.h"

int calcula(int a, int b);

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    imprimir("%d\n", calcula(1, 2));
    return 0;
}
