/* Deslocamentos: para a direita, o sinal acompanha em int e não em unsigned. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int a = -1024, n = 3;
    unsigned u = 0x80000000u;
    (void)argc; (void)argv;
    imprimir("%d %d %d\n", 1 << 10, a >> 2, a >> n);
    imprimir("%u %u\n", u >> 4, u >> n);
    imprimir("%x %x\n", 0xABCD << 8, 0xABCD >> 4);
    imprimir("%d\n", (a << 2) >> 4);
    for (int i = 0; i < 32; i += 5) imprimir("%x ", 1u << i);
    imprimir("\n");
    unsigned x = 0x12345678;
    unsigned rodado = (x << 8) | (x >> 24);
    imprimir("%x\n", rodado);
    int bits = 0;
    for (unsigned y = 0xF0F0F0F1u; y; y >>= 1) bits += y & 1;
    imprimir("bits ligados: %d\n", bits);
    char c = -128;
    imprimir("%d %d\n", c >> 1, (unsigned char)c >> 1);
    return 0;
}
