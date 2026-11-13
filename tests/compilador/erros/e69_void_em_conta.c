#include "sagui.h"

static void avisa(void) { imprimir("oi\n"); }

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    int r = avisa() + 1;
    (void)r;
    return 0;
}
