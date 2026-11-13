/* Conversões entre tipos: as que o C faz sozinho e as pedidas com (tipo). */
#include "sagui.h"

static unsigned char corta(int v) { return (unsigned char)v; }
static int estende(signed char c) { return c; }
static short meio(unsigned v) { return (short)(v >> 8); }

int principal(int argc, char **argv)
{
    int i = -1;
    unsigned u = i;
    char c = (char)300;
    unsigned char uc = (unsigned char)-1;
    short s = (short)0xFFFF8001;
    long long ll = i;
    unsigned long long ull = u;
    (void)argc; (void)argv;
    imprimir("%u %d %d %d\n", u, c, uc, s);
    imprimir("%d %d\n", (int)(ll >> 32), (int)(ull >> 32));
    imprimir("%d %d %d\n", corta(511), estende((signed char)200), meio(0x00ABCDEF));
    imprimir("%d %d\n", (int)(unsigned char)-56, (int)(signed char)200);
    imprimir("%d %d\n", (unsigned short)-1 + 1, (short)40000 + 1);
    int grande = 0x12345678;
    imprimir("%x %x %x\n", (unsigned char)grande, (unsigned short)grande, (unsigned)(char)grande);
    unsigned endereco = (unsigned)&grande;
    int *volta = (int *)endereco;
    imprimir("%x %d\n", *volta, volta == &grande);
    char letra = 'A';
    int codigo = letra;
    letra = (char)(codigo + 25);
    imprimir("%d %c\n", codigo, letra);
    bool b = (bool)0x100;
    imprimir("%d %d\n", b, (char)0x100);
    long long mistura = 3000000000u + 3000000000u;      /* a soma é feita em 32 bits e dá a volta */
    long long certa = 3000000000LL + 3000000000u;
    imprimir("%d %d\n", (int)(mistura >> 32), (int)(certa >> 32));
    int negativo = -10;
    imprimir("%d %d\n", negativo / 3u > 1000, negativo / 3);
    unsigned char a = 200, d = 100;
    imprimir("%d %d\n", a + d, (unsigned char)(a + d));
    void *p = &letra;
    imprimir("%c\n", *(char *)p);
    return 0;
}
