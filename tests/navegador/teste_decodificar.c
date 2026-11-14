/* teste_decodificar.c - roda a porta única das imagens do Cipó
 * (userland/navegador/imagens.c: PNG, JPEG, GIF e WebP) no computador e grava
 * os pixels crus (largura, altura e 0xAARRGGBB) para o teste comparar com o
 * Pillow. Uso: teste_decodificar entrada saida */
#include "../../userland/navegador/navegador.h"

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    uint32_t *px;
    int l, a;
    bool ok = imagem_decodificar(d, (unsigned)n, &px, &l, &a);
    free(d);
    if (!ok) { printf("ERRO\n"); return 1; }
    FILE *s = fopen(argv[2], "wb");
    fwrite(&l, 4, 1, s);
    fwrite(&a, 4, 1, s);
    fwrite(px, 4, (size_t)l * (size_t)a, s);
    fclose(s);
    free(px);
    printf("ok %dx%d\n", l, a);
    return 0;
}
