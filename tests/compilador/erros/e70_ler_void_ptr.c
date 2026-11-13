#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    int x = 7;
    void *p = &x;
    imprimir("%d\n", *p);
    return 0;
}
