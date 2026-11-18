/* teste_zip.c - roda o leitor de ZIP do JClic (userland/jogos/jclic/zip.c)
 * no computador, com verificação de memória, sobre um arquivo dado.
 *
 * Uso: teste_zip arquivo.zip
 * Abre o arquivo, extrai todas as entradas e imprime "abriu N" e
 * "extraiu K" (ou "recusou"). Qualquer leitura fora do arquivo faz o
 * AddressSanitizer parar o programa com erro. */
#include "zip.h"

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* Um buffer do tamanho exato: um byte a mais lido já é erro de memória. */
    uint8_t *d = malloc(n ? (size_t)n : 1);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    zip_t z;
    if (zip_abrir(&z, d, (uint32_t)n) != 0) { printf("recusou\n"); free(d); return 0; }
    int k = 0;
    for (int i = 0; i < z.qtd; i++) {
        uint32_t t;
        uint8_t *s = zip_extrair(&z, i, &t);
        if (s) { k++; libera(s); }
    }
    printf("abriu %d\nextraiu %d\n", z.qtd, k);
    zip_fechar(&z);
    free(d);
    return 0;
}
