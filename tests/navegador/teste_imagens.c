/* teste_imagens.c - roda os decodificadores de PNG e JPEG do navegador
 * (userland/navegador) no computador de desenvolvimento: lê a imagem, decodifica
 * e grava os pixels crus (largura, altura e 0xAARRGGBB em little-endian) para o
 * teste em Python comparar com a referência. */
#include "../../userland/navegador/navegador.h"

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "uso: teste_imagens entrada saida\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    char erro[128] = "";
    imagem_t *img = imagem_ler(d, (uint32_t)n, erro, sizeof erro);
    if (!img) { printf("ERRO: %s\n", erro); return 1; }
    FILE *s = fopen(argv[2], "wb");
    fwrite(&img->larg, 4, 1, s);
    fwrite(&img->alt, 4, 1, s);
    fwrite(img->px, 4, (size_t)img->larg * img->alt, s);
    fclose(s);
    printf("ok %ux%u\n", img->larg, img->alt);
    return 0;
}
