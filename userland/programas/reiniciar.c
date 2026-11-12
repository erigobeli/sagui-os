/* reiniciar.c - reinicia o computador */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    escrever(1, "Reiniciando o SaguiOS...\n", 25);
    dormir(100);
    reiniciar();
    return 0;
}

