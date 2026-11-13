/* flutuante N - teste do ponto flutuante nos programas (H80).
 *
 * Soma 1/k^2 (que tende a pi^2/6) em double e uma série em float, cedendo a
 * vez ao outro processo a cada passo (dormir), e confere no meio do caminho
 * que os registradores não foram estragados por outro programa: o resultado
 * tem de ser o mesmo de uma conta feita de uma vez só, sem trocas. */
#include "sagui.h"

static double serie(int n, double fator)
{
    double s = 0;
    for (int k = 1; k <= n; k++) s += fator / ((double)k * (double)k);
    return s;
}

int principal(int argc, char **argv)
{
    int n = argc > 1 ? texto_para_int(argv[1]) : 1;
    double fator = 1.0 + n * 0.5;
    double esperado = serie(2000, fator);
    double s = 0;
    float f = 0.0f;
    volatile double guardado = 0;
    for (int k = 1; k <= 2000; k++) {
        s += fator / ((double)k * (double)k);
        f += 1.0f / (float)k;
        if (k % 100 == 0) { guardado = s; dormir(2); if (guardado != s) { imprimir("flutuante %d: ESTRAGADO\n", n); return 1; } }
    }
    if (s != esperado) {
        union { double d; unsigned u[2]; } a = { s }, b = { esperado };
        imprimir("flutuante %d: ERRO %x%08x x %x%08x\n", n, a.u[1], a.u[0], b.u[1], b.u[0]);
        return 1;
    }
    /* Mostra com 6 casas, sem printf de double: parte inteira e fração. */
    long inteiro = (long)s;
    long frac = (long)((s - (double)inteiro) * 1000000.0 + 0.5);
    long fi = (long)f, ff = (long)((f - (float)fi) * 1000.0f + 0.5f);
    imprimir("flutuante %d: ok %ld.%06ld (float %ld.%03ld)\n", n, inteiro, frac, fi, ff);
    return 0;
}
