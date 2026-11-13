/* Operadores de bits e máscaras. */
#include "sagui.h"

#define LER    1
#define GRAVAR 2
#define RODAR  4

static void mostra(unsigned m)
{
    imprimir("%c%c%c", m & LER ? 'l' : '-', m & GRAVAR ? 'g' : '-', m & RODAR ? 'r' : '-');
}

int principal(int argc, char **argv)
{
    unsigned a = 0xFF00FF00u, b = 0x0FF00FF0u;
    (void)argc; (void)argv;
    imprimir("%x %x %x %x\n", a & b, a | b, a ^ b, ~a);
    unsigned m = 0;
    m |= LER; mostra(m); imprimir(" ");
    m |= RODAR; mostra(m); imprimir(" ");
    m &= ~LER; mostra(m); imprimir(" ");
    m ^= GRAVAR | RODAR; mostra(m); imprimir("\n");
    int x = 0x5A;
    imprimir("%d %d %d\n", x & 0xF, x | 0x100, x ^ x);
    imprimir("%d %d\n", ~0, ~x);
    unsigned cor = (200u << 16) | (100u << 8) | 50u;
    imprimir("r=%u g=%u b=%u\n", (cor >> 16) & 255, (cor >> 8) & 255, cor & 255);
    imprimir("%d %d\n", (6 & 3) == 2, 6 & (3 == 2));
    return 0;
}
