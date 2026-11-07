/* teste_aac.c - decodifica o áudio de um MP4 com o aac.c e grava s16le estéreo. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../userland/libsagui/aac.h"
#include "../../userland/libsagui/mp4.h"

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n + 16);
    if (fread(d, 1, n, f) != (size_t)n) return 1;
    fclose(f);
    mp4_t m;
    if (mp4_ler(d, n, &m)) return 1;
    aac_t *a = aac_novo(m.asc, m.asc_n);
    if (!a) { fprintf(stderr, "asc recusado\n"); return 1; }
    FILE *o = fopen(argv[2], "wb");
    int16_t pcm[2048];
    int erros = 0;
    clock_t t0 = clock();
    for (int i = 0; i < m.n_audio; i++) {
        int r = aac_quadro(a, d + m.audio[i].pos, m.audio[i].tam, pcm);
        if (r < 0) { if (erros++ < 5) fprintf(stderr, "quadro %d: erro %d\n", i, r); continue; }
        fwrite(pcm, 4, r, o);
    }
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    fprintf(stderr, "%d quadros, %d erros, %.2f s (%.0fx tempo real a %d Hz)\n", m.n_audio, erros, s,
            m.n_audio * 1024.0 / aac_taxa(a) / s, aac_taxa(a));
    fclose(o);
    return 0;
}
