#include "sagui.h"

typedef struct { int x, y; } ponto_t;

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    ponto_t a = { 1, 2 }, b = { 3, 4 };
    if (a == b) imprimir("iguais\n");
    return 0;
}
