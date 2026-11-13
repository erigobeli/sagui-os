#include "sagui.h"

#define PARTES 0

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    imprimir("%d\n", 100 / PARTES);
    return 0;
}
