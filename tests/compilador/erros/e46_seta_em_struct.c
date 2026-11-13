#include "sagui.h"

typedef struct { int x, y; } ponto_t;

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    ponto_t p = { 1, 2 };
    imprimir("%d\n", p->x);
    return 0;
}
