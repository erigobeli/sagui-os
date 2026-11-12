/* teste_brotli.c - descomprime arquivo .br no Linux e grava a saída (comparar com python brotli). */
#include <stdio.h>
#include <stdlib.h>
#include <sagui.h>
uint8_t *brotli_descomprimir(const uint8_t *in, unsigned n, unsigned *saida_n, unsigned max);
int main(int c, char **v)
{
    FILE *f = fopen(v[1], "rb"); fseek(f, 0, 2); long n = ftell(f); rewind(f);
    uint8_t *d = malloc(n); fread(d, 1, n, f); fclose(f);
    unsigned sn = 0;
    uint8_t *s = brotli_descomprimir(d, (unsigned)n, &sn, 64u << 20);
    extern int brotli_linha; if (!s) { printf("FALHOU linha %d\n", brotli_linha); return 1; }
    f = fopen(v[2], "wb"); fwrite(s, 1, sn, f); fclose(f);
    printf("ok %u\n", sn);
    return 0;
}
