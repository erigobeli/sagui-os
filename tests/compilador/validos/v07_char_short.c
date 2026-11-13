/* Tipos de 8 e 16 bits: o que não cabe é cortado; o sinal é estendido. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    char c = 200;                 /* não cabe: vira -56 */
    unsigned char uc = 200;
    signed char sc = -5;
    short s = 40000;              /* não cabe: vira -25536 */
    unsigned short us = 40000;
    (void)argc; (void)argv;
    imprimir("%d %d %d %d %d\n", c, uc, sc, s, us);
    uc += 100; c += 100; us += 30000; s -= 30000;
    imprimir("%d %d %d %d\n", uc, c, us, s);
    uint8_t conta = 250;
    for (int i = 0; i < 10; i++) conta++;
    imprimir("%d\n", conta);
    int16_t v[4] = { -1, 32767, -32768, 1234 };
    int soma = 0;
    for (int i = 0; i < 4; i++) soma += v[i];
    imprimir("%d\n", soma);
    char letra = 'a';
    letra = letra - 'a' + 'A';
    imprimir("%c %d %c\n", letra, letra, letra + 1);
    unsigned char bytes[4] = { 0x12, 0x34, 0xAB, 0xFF };
    unsigned junto = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((unsigned)bytes[3] << 24);
    imprimir("%x\n", junto);
    imprimir("%d %d\n", (char)0x1FF, (short)0x12345);
    imprimir("%d %d %d\n", (int)sizeof(char), (int)sizeof(short), (int)sizeof(int));
    return 0;
}
