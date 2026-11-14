/* acumulador.c - rasterizador de contornos por área acumulada.
 *
 * CÓDIGO DE TERCEIROS (D041, D043). O algoritmo e a forma destas funções vêm
 * do font-rs, de Raph Levien (Copyright 2015 Google Inc.), licenciado sob a
 * Apache License 2.0 (LICENSE nesta pasta). Não é cópia literal: foi
 * reproduzido de memória, em C, pela IA que ajudou Robson de Lima Barbosa no
 * pull request #6 do SaguiOS, e por isso é tratado como derivado do font-rs.
 * Veja ORIGEM.md para o que mudou.
 *
 * A ideia: cada segmento do contorno soma, em cada pixel por onde passa, a
 * parte da área (com sinal, pelo sentido do segmento) que fica à direita dele
 * naquela linha; depois, uma soma corrida por linha dá a cobertura exata de
 * cada pixel (0 = fora, 1 = dentro), que vira o antialias. */
#include "acumulador.h"

static inline float fpiso(float v) { int i = (int)v; return (float)(v < (float)i ? i - 1 : i); }
static inline float fteto(float v) { int i = (int)v; return (float)(v > (float)i ? i + 1 : i); }
static inline float fabs_f(float v) { return v < 0 ? -v : v; }
static double c_floor(double x) { int i = (int)x; return (double)(x < i ? i - 1 : i); }
static double c_ceil(double x) { int i = (int)x; return (double)(x > i ? i + 1 : i); }
static double c_min(double a, double b) { return a < b ? a : b; }
static double c_max(double a, double b) { return a > b ? a : b; }

/* Fontes (fontes/fonte.c): acumulador de w x h floats; segmento em pixels. */
void fontrs_linha(float *acc, int w, int h, float x0, float y0, float x1, float y1)
{
    if (fabs_f(y0 - y1) < 1e-6f) return;
    float dir = 1.0f;
    if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1.0f; }
    float dxdy = (x1 - x0) / (y1 - y0);
    float x = x0;
    int ya = y0 < 0 ? 0 : (int)y0;
    if (y0 < 0) x -= y0 * dxdy;
    int yb = (int)fteto(y1); if (yb > h) yb = h;
    for (int y = ya; y < yb; y++) {
        float *l = acc + y * w;
        float lo = (float)y > y0 ? (float)y : y0, hi = (float)(y + 1) < y1 ? (float)(y + 1) : y1;
        float dy = hi - lo;
        float xn = x + dxdy * dy, d = dy * dir;
        float xa = x < xn ? x : xn, xb = x < xn ? xn : x;
        if (xa < 0) xa = 0;
        if (xb < 0) xb = 0;
        float xaf = fpiso(xa); int xai = (int)xaf;
        float xbc = fteto(xb); int xbi = (int)xbc;
        if (xai < 0 || xbi >= w) { x = xn; continue; }
        if (xbi <= xai + 1) {
            float xm = 0.5f * (x + xn) - xaf;
            l[xai] += d - d * xm;
            l[xai + 1] += d * xm;
        } else {
            float s = 1.0f / (xb - xa);
            float x0f = xa - xaf;
            float a0 = 0.5f * s * (1.0f - x0f) * (1.0f - x0f);
            float x1f = xb - xbc + 1.0f;
            float am = 0.5f * s * x1f * x1f;
            l[xai] += d * a0;
            if (xbi == xai + 2) l[xai + 1] += d * (1.0f - a0 - am);
            else {
                float a1 = s * (1.5f - x0f);
                l[xai + 1] += d * (a1 - a0);
                for (int xi = xai + 2; xi < xbi - 1; xi++) l[xi] += d * s;
                float a2 = a1 + (float)(xbi - xai - 3) * s;
                l[xbi - 1] += d * (1.0f - a2 - am);
            }
            l[xbi] += d * am;
        }
        x = xn;
    }
}

/* Canvas e SVG (graficos/canvas2d.c): linha com "passo" floats; o que passa
 * à esquerda da caixa conta inteiro na coluna 0, o que passa à direita some. */
void fontrs_linha_recortada(float *acc, int passo, int l, int a, double x0, double y0, double x1, double y1)
{
    if (y0 == y1) return;
    double dir = 1;
    if (y0 > y1) { double t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
    if (y1 <= 0 || y0 >= a) return;
    double dxdy = (x1 - x0) / (y1 - y0);
    double x = x0;
    if (y0 < 0) { x -= y0 * dxdy; y0 = 0; }
    if (y1 > a) y1 = a;
    for (int y = (int)y0; y < a && y < y1; y++) {
        float *lin = acc + y * passo;
        double dy = c_min(y + 1, y1) - c_max(y, y0);
        double xn = x + dxdy * dy, d = dy * dir;
        double a0x = x < xn ? x : xn, a1x = x < xn ? xn : x;
        /* fora da largura: à esquerda conta na coluna 0, à direita não aparece */
        if (a1x <= 0) { lin[0] += (float)d; x = xn; continue; }
        if (a0x >= l) { x = xn; continue; }
        if (a0x < 0) {   /* parte à esquerda: vertical na borda, o resto como está */
            double f = -a0x / (a1x - a0x);
            lin[0] += (float)(d * f);
            d -= d * f; a0x = 0;
        }
        if (a1x > l) a1x = l;
        double fl = c_floor(a0x); int x0i = (int)fl;
        double ce = c_ceil(a1x); int x1i = (int)ce;
        if (x1i <= x0i + 1) {
            double xm = 0.5 * (a0x + a1x) - fl;
            lin[x0i] += (float)(d - d * xm);
            lin[x0i + 1] += (float)(d * xm);
        } else {
            double s = 1 / (a1x - a0x), x0f = a0x - fl, a0 = 0.5 * s * (1 - x0f) * (1 - x0f);
            double x1f = a1x - ce + 1, am = 0.5 * s * x1f * x1f;
            lin[x0i] += (float)(d * a0);
            if (x1i == x0i + 2) lin[x0i + 1] += (float)(d * (1 - a0 - am));
            else {
                double a1 = s * (1.5 - x0f);
                lin[x0i + 1] += (float)(d * (a1 - a0));
                for (int xi = x0i + 2; xi < x1i - 1; xi++) lin[xi] += (float)(d * s);
                double a2 = a1 + (x1i - x0i - 3) * s;
                lin[x1i - 1] += (float)(d * (1 - a2 - am));
            }
            lin[x1i] += (float)(d * am);
        }
        x = xn;
    }
}

/* Soma corrida de cada linha: cobertura 0..255 (regra "não zero", com o
 * valor absoluto e o teto em 1, como no font-rs). */
void fontrs_acumular(const float *acc, int w, int h, unsigned char *cob)
{
    for (int y = 0; y < h; y++) {
        float s = 0;
        for (int x = 0; x < w; x++) {
            s += acc[y * w + x];
            float v = fabs_f(s);
            if (v > 1) v = 1;
            cob[y * w + x] = (unsigned char)(v * 255.0f + 0.5f);
        }
    }
}

/* O mesmo para o Canvas, com a regra "par ou ímpar" (fill-rule evenodd),
 * acrescentada no #6: a cobertura dobra a cada 2 voltas. */
void fontrs_acumular_regra(const float *acc, int passo, int l, int a, int parimpar, unsigned char *m)
{
    for (int y = 0; y < a; y++) {
        float s = 0;
        const float *lin = acc + y * passo;
        for (int x = 0; x < l; x++) {
            s += lin[x];
            double c = s < 0 ? -s : s;
            if (parimpar) { c -= 2 * c_floor(c / 2); if (c > 1) c = 2 - c; }
            else if (c > 1) c = 1;
            m[y * l + x] = (unsigned char)(c * 255 + 0.5);
        }
    }
}
