/* aleatorio - teste do gerador (H50): mostra os primeiros bytes e a contagem
 * de cada valor em 64 KB (num gerador bom, cada valor de 0 a 255 aparece perto
 * de 256 vezes). */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    static unsigned char b[65536];
    for (unsigned k = 0; k < sizeof b; k += 4096) bytes_aleatorios(b + k, 4096);
    unsigned cont[256];
    memset(cont, 0, sizeof cont);
    for (unsigned i = 0; i < sizeof b; i++) cont[b[i]]++;
    unsigned menor = 99999, maior = 0;
    for (int i = 0; i < 256; i++) { if (cont[i] < menor) menor = cont[i]; if (cont[i] > maior) maior = cont[i]; }
    imprimir("inicio ");
    for (int i = 0; i < 16; i++) imprimir("%02x", b[i]);
    imprimir("\nmenor %u maior %u\n", menor, maior);
    return 0;
}
