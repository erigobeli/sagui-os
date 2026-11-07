/* teste_h264.c - roda o decodificador do SaguiOS no Linux e grava YUV cru
 * para comparar com o ffmpeg (tools/video/comparar.py). */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../userland/inc/h264.h"
#include "../../userland/inc/mp4.h"

static void gravar(FILE *o, h264_imagem_t *im)
{
    for (int y = 0; y < im->alt; y++) fwrite(im->y + y * im->passo_y, 1, im->larg, o);
    for (int y = 0; y < im->alt / 2; y++) fwrite(im->u + y * im->passo_c, 1, im->larg / 2, o);
    for (int y = 0; y < im->alt / 2; y++) fwrite(im->v + y * im->passo_c, 1, im->larg / 2, o);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: teste_h264 entrada.mp4 saida.yuv\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n + 16);
    fread(d, 1, n, f);
    fclose(f);
    mp4_t m;
    int r = mp4_ler(d, n, &m);
    if (r) { fprintf(stderr, "mp4_ler %d\n", r); return 1; }
    fprintf(stderr, "video %d amostras %dx%d escala %u; audio %d amostras %d Hz %d canais asc %u\n",
            m.n_video, m.larg, m.alt, m.escala_video, m.n_audio, m.taxa, m.canais, m.asc_n);
    extern int h264_dbg_mb(int);
    if (getenv("MB")) h264_dbg_mb(atoi(getenv("MB")));
    h264_t *h = h264_novo();
    int tl;
    r = h264_config(h, m.avcc, m.avcc_n, &tl);
    if (r) { fprintf(stderr, "config %d\n", r); return 1; }
    FILE *o = fopen(argv[2], "wb");
    clock_t t0 = clock();
    for (int i = 0; i < m.n_video; i++) {
        r = h264_amostra(h, d + m.video[i].pos, m.video[i].tam, tl);
        if (r) fprintf(stderr, "quadro %d: erro %d\n", i, r);
        h264_imagem_t im;
        while (h264_imagem(h, &im)) gravar(o, &im);
    }
    h264_esvaziar(h);
    h264_imagem_t im;
    while (h264_imagem(h, &im)) gravar(o, &im);
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    fprintf(stderr, "%d quadros em %.2f s (%.1f qps)\n", m.n_video, s, m.n_video / s);
    fclose(o);
    return 0;
}
