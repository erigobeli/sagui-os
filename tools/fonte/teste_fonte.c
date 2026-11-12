/* teste_fonte.c - desenha uma frase com fonte TTF/WOFF/WOFF2 num PGM: tf fonte px "texto" saida.pgm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sagui.h>
int main(int c, char **v)
{
    FILE *f = fopen(v[1], "rb"); fseek(f, 0, 2); long n = ftell(f); rewind(f);
    uint8_t *d = malloc(n); fread(d, 1, n, f); fclose(f);
    fonte_t *fo = fonte_abrir(d, (unsigned)n);
    if (!fo) { printf("nao abriu\n"); return 1; }
    int px = atoi(v[2]); const char *t = v[3];
    int W = 1200, H = px * 2, base = fonte_ascendente(fo, px) + 2;
    uint8_t *img = calloc(W, H);
    int x64 = 2 * 64; long tinta = 0;
    for (const char *p = t; *p; p++) {
        int l, a, ox, oy;
        const uint8_t *g = fonte_glifo(fo, (uint8_t)*p, px, &l, &a, &ox, &oy);
        int x = x64 / 64;
        if (g) for (int yy = 0; yy < a; yy++) for (int xx = 0; xx < l; xx++) {
            int X = x + ox + xx, Y = base + oy + yy;
            if (X >= 0 && X < W && Y >= 0 && Y < H) { int s = img[Y * W + X] + g[yy * l + xx]; img[Y * W + X] = s > 255 ? 255 : s; }
        }
        x64 += fonte_avanco(fo, (uint8_t)*p, px);
    }
    for (int i = 0; i < W * H; i++) tinta += img[i];
    f = fopen(v[4], "wb"); fprintf(f, "P5 %d %d 255\n", W, H); fwrite(img, 1, W * H, f); fclose(f);
    printf("ok largura %d tinta %ld\n", x64 / 64, tinta);
    fonte_fechar(fo);
    return 0;
}
