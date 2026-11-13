#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    int v[3] = { 1, 2, 3 };
    const char *i = "1";
    imprimir("%d\n", v[i]);
    return 0;
}
