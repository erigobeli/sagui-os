/* tom.c - toca uma onda quadrada de 1 kHz por 2 s na placa de som. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    static short b[2048 * 2];
    for (int i = 0; i < 2048; i++) { short v = (i / 24) & 1 ? 8000 : -8000; b[i * 2] = b[i * 2 + 1] = v; }
    int feito = 0, total = 48000 * 4 * 2;
    while (feito < total) {
        int r = tocar_pcm(b, sizeof b, 48000);
        if (r < 0) { registrar("tom: sem placa\n"); return 1; }
        if (!r) dormir(10); else feito += r;
    }
    dormir(2000);
    tocar_pcm(b, 0, 0);
    registrar("tom: fim %d\n", feito);
    return 0;
}
