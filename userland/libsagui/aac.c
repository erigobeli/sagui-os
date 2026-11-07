/* aac.c - decodificador AAC-LC (o áudio mp4a.40.2 do YouTube e dos MP4).
 *
 * Cobre: SCE/CPE/LFE, janelas longas e curtas (com grupos), seções,
 * fatores de escala, pulsos, TNS, M/S, intensidade, PNS, janelas seno e KBD
 * e a IMDCT por FFT. Fica de fora: SBR/PS (HE-AAC), CCE e predição (Main/LTP).
 *
 * Usa ponto flutuante (precisa das flags de FPU no Makefile). Compila também
 * no Linux (H264_HOST) para comparar com o ffmpeg. */
#ifdef H264_HOST
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define aloca malloc
#define libera free
static void *aloca_zerado(size_t n) { return calloc(1, n); }
#else
#include "sagui.h"
#endif
#include "aac.h"
#include "aac_huff.h"

/* --------------------------------------------------------- matemática */

#define PI 3.14159265358979323846

static double m_sqrt(double x) { return __builtin_sqrt(x); }

static double m_sin(double x)
{
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    if (x > PI / 2) x = PI - x;
    else if (x < -PI / 2) x = -PI - x;
    double x2 = x * x, t = x, s = x;
    for (int i = 1; i < 12; i++) { t *= -x2 / ((2 * i) * (2 * i + 1)); s += t; }
    return s;
}
static double m_cos(double x) { return m_sin(x + PI / 2); }

static double m_cbrt(double x)
{
    if (x <= 0) return 0;
    double y = x > 1 ? x / 3 : 1;
    for (int i = 0; i < 60; i++) {
        double n = (2 * y + x / (y * y)) / 3;
        if (n == y) break;
        y = n;
    }
    return y;
}

/* 2^(q/4) para q inteiro */
static double m_pot2q(int q)
{
    static const double frac[4] = { 1.0, 1.18920711500272106672, 1.41421356237309504880, 1.68179283050742908606 };
    int e = q >> 2;   /* chão */
    double v = frac[q & 3];
    while (e > 0) { v *= 2; e--; }
    while (e < 0) { v *= 0.5; e++; }
    return v;
}

static double bessel_i0(double x)
{
    double s = 1, t = 1;
    for (int k = 1; k < 60; k++) {
        t *= (x / (2 * k)) * (x / (2 * k));
        s += t;
        if (t < s * 1e-17) break;
    }
    return s;
}

/* ------------------------------------------------------------ tabelas */

static const int taxas[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
static const uint8_t tns_max_longa[13] = { 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39, 39 };
static const uint8_t tns_max_curta[13] = { 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static const uint16_t swb_longa_48[50] = { 0,4,8,12,16,20,24,28,32,36,40,48,56,64,72,80,88,96,108,120,132,144,160,176,196,216,240,264,292,320,352,384,416,448,480,512,544,576,608,640,672,704,736,768,800,832,864,896,928,1024 };
static const uint16_t swb_longa_32[52] = { 0,4,8,12,16,20,24,28,32,36,40,48,56,64,72,80,88,96,108,120,132,144,160,176,196,216,240,264,292,320,352,384,416,448,480,512,544,576,608,640,672,704,736,768,800,832,864,896,928,960,992,1024 };
static const uint16_t swb_curta_48[15] = { 0,4,8,12,16,20,28,36,44,56,68,80,96,112,128 };
static const uint16_t swb_longa_24[48] = { 0,4,8,12,16,20,24,28,32,36,40,44,52,60,68,76,84,92,100,108,116,124,136,148,160,172,188,204,220,240,260,284,308,336,364,396,432,468,508,552,600,652,704,768,832,896,960,1024 };
static const uint16_t swb_curta_24[16] = { 0,4,8,12,16,20,24,28,36,44,52,64,76,92,108,128 };
static const uint16_t swb_longa_16[44] = { 0,8,16,24,32,40,48,56,64,72,80,88,100,112,124,136,148,160,172,184,196,212,228,244,260,280,300,320,344,368,396,424,456,492,532,572,616,664,716,772,832,896,960,1024 };
static const uint16_t swb_curta_16[16] = { 0,4,8,12,16,20,24,28,32,40,48,60,72,88,108,128 };

/* Huffman: tabela direta de 10 bits; códigos maiores vão para busca linear. */
#define HB 10
typedef struct {
    uint32_t dir[1 << HB];          /* (tam << 16) | índice; 0 = código longo */
    const uint32_t *cod;
    int qtd;
} huff_t;
static huff_t hf[12];
static const uint32_t *const livros[12] = { aac_cb0, aac_cb1, aac_cb2, aac_cb3, aac_cb4, aac_cb5, aac_cb6, aac_cb7, aac_cb8, aac_cb9, aac_cb10, aac_cb11 };
static const int livros_qtd[12] = { 121, 81, 81, 81, 81, 81, 81, 64, 64, 169, 169, 289 };
static const int8_t livro_dim[12] = { 0, 4, 4, 4, 4, 2, 2, 2, 2, 2, 2, 2 };
static const int8_t livro_sinal[12] = { 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0 };
static const int8_t livro_mod[12] = { 0, 3, 3, 3, 3, 9, 9, 8, 8, 13, 13, 17 };

static float pot43[8192];
static float jan_seno_l[1024], jan_seno_c[128], jan_kbd_l[1024], jan_kbd_c[128];
static bool tabelas_ok;

/* IMDCT por FFT */
typedef struct { float re, im; } cpx;
typedef struct {
    int n;                    /* tamanho da janela (2048 ou 256) */
    float *tcos, *tsin;
    cpx *w;                   /* raízes da FFT de n/4 pontos */
    uint16_t *rev;
} imdct_t;
static imdct_t md_l, md_c;

static bool imdct_iniciar(imdct_t *m, int n, double escala)
{
    int n4 = n / 4, bits = 0;
    while ((1 << bits) < n4) bits++;
    m->n = n;
    m->tcos = aloca(sizeof(float) * n4);
    m->tsin = aloca(sizeof(float) * n4);
    m->w = aloca(sizeof(cpx) * n4);
    m->rev = aloca(sizeof(uint16_t) * n4);
    if (!m->tcos || !m->tsin || !m->w || !m->rev) return false;
    double s = m_sqrt(escala);
    for (int i = 0; i < n4; i++) {
        double a = 2 * PI * (i + 0.125) / n;
        m->tcos[i] = (float)(-m_cos(a) * s);
        m->tsin[i] = (float)(-m_sin(a) * s);
        m->w[i].re = (float)m_cos(-2 * PI * i / n4);
        m->w[i].im = (float)m_sin(2 * PI * i / n4);
        int r = 0;
        for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        m->rev[i] = (uint16_t)r;
    }
    return true;
}

static void fft(const imdct_t *m, cpx *z)
{
    int n = m->n / 4;
    for (int i = 0; i < n; i++) {
        int r = m->rev[i];
        if (r > i) { cpx t = z[i]; z[i] = z[r]; z[r] = t; }
    }
    for (int tam = 2; tam <= n; tam <<= 1) {
        int meio = tam / 2, passo = n / tam;
        for (int i = 0; i < n; i += tam)
            for (int j = 0; j < meio; j++) {
                cpx w = m->w[j * passo], *a = &z[i + j], *b = &z[i + j + meio];
                float tr = b->re * w.re - b->im * w.im, ti = b->re * w.im + b->im * w.re;
                b->re = a->re - tr; b->im = a->im - ti;
                a->re += tr; a->im += ti;
            }
    }
}

/* Saída: n amostras (janela inteira) a partir de n/2 coeficientes. */
static void imdct(const imdct_t *m, const float *in, float *out)
{
    int n = m->n, n2 = n / 2, n4 = n / 4, n8 = n / 8;
    cpx z[512];
    for (int k = 0; k < n4; k++) {
        float a = -in[n2 - 1 - 2 * k], b = -in[2 * k];
        z[k].re = a * m->tcos[k] - b * m->tsin[k];
        z[k].im = a * m->tsin[k] + b * m->tcos[k];
    }
    fft(m, z);
    for (int k = 0; k < n8; k++) {
        int i0 = n8 - k - 1, i1 = n8 + k;
        float r0 = z[i0].im * m->tsin[i0] - z[i0].re * m->tcos[i0];
        float im0 = z[i0].im * m->tcos[i0] + z[i0].re * m->tsin[i0];
        float r1 = z[i1].im * m->tsin[i1] - z[i1].re * m->tcos[i1];
        float im1 = z[i1].im * m->tcos[i1] + z[i1].re * m->tsin[i1];
        z[i0].re = r0; z[i0].im = im1;
        z[i1].re = r1; z[i1].im = im0;
    }
    float *meio = out + n4;
    for (int k = 0; k < n4; k++) { meio[2 * k] = z[k].re; meio[2 * k + 1] = z[k].im; }
    for (int k = 0; k < n4; k++) {
        out[k] = -out[n2 - k - 1];
        out[n - k - 1] = out[n2 + k];
    }
}

static void kbd(float *w, int n, double alfa)
{
    /* n = metade da janela */
    double soma = 0, *k = aloca(sizeof(double) * (n + 1));
    for (int i = 0; i <= n; i++) {
        double t = (double)(i - n / 2.0) / (n / 2.0);
        k[i] = bessel_i0(PI * alfa * m_sqrt(1.0 - t * t));
        soma += k[i];
    }
    double acc = 0;
    for (int i = 0; i < n; i++) { acc += k[i]; w[i] = (float)m_sqrt(acc / soma); }
    libera(k);
}

static bool tabelas_iniciar(void)
{
    if (tabelas_ok) return true;
    for (int c = 0; c < 12; c++) {
        huff_t *h = &hf[c];
        h->cod = livros[c];
        h->qtd = livros_qtd[c];
        for (int i = 0; i < h->qtd; i++) {
            int l = (int)(h->cod[i] >> 24);
            uint32_t cw = h->cod[i] & 0xFFFFFF;
            if (l > HB) continue;
            uint32_t base = cw << (HB - l), n = 1u << (HB - l);
            for (uint32_t k = 0; k < n; k++) h->dir[base + k] = ((uint32_t)l << 16) | (uint32_t)i;
        }
    }
    for (int i = 0; i < 8192; i++) pot43[i] = (float)(i * m_cbrt(i));
    for (int i = 0; i < 1024; i++) jan_seno_l[i] = (float)m_sin(PI / 2048 * (i + 0.5));
    for (int i = 0; i < 128; i++) jan_seno_c[i] = (float)m_sin(PI / 256 * (i + 0.5));
    kbd(jan_kbd_l, 1024, 4);
    kbd(jan_kbd_c, 128, 6);
    if (!imdct_iniciar(&md_l, 2048, 1.0 / 1024) || !imdct_iniciar(&md_c, 256, 1.0 / 128)) return false;
    tabelas_ok = true;
    return true;
}

/* -------------------------------------------------------------- bits */

typedef struct {
    const uint8_t *d;
    uint32_t pos, fim;
} bits_t;

static inline uint32_t espiar(const bits_t *b, int n)
{
    uint32_t i = b->pos >> 3, s = b->pos & 7;
    uint32_t v = ((uint32_t)b->d[i] << 24) | ((uint32_t)b->d[i + 1] << 16) | ((uint32_t)b->d[i + 2] << 8) | b->d[i + 3];
    if (s) v = (v << s) | (b->d[i + 4] >> (8 - s));
    return v >> (32 - n);
}
static inline uint32_t ler(bits_t *b, int n)
{
    if (!n) return 0;
    uint32_t v = espiar(b, n);
    b->pos += n;
    return v;
}

static int huff(bits_t *b, int c)
{
    const huff_t *h = &hf[c];
    uint32_t e = h->dir[espiar(b, HB)];
    if (e) { b->pos += e >> 16; return (int)(e & 0xFFFF); }
    uint32_t v = espiar(b, 24);
    for (int i = 0; i < h->qtd; i++) {
        int l = (int)(h->cod[i] >> 24);
        if (l > HB && (v >> (24 - l)) == (h->cod[i] & 0xFFFFFF)) { b->pos += l; return i; }
    }
    b->pos = b->fim + 1;
    return 0;
}

/* ------------------------------------------------------------- estado */

#define LONGA_SO     0
#define LONGA_INICIO 1
#define CURTAS       2
#define LONGA_FIM    3

typedef struct {
    int seq, forma, max_sfb, n_jan, n_grupos;
    int grupo_tam[8];
    int predicao;
} ics_info_t;

typedef struct {
    ics_info_t info;
    int global;
    uint8_t cb[8][64];
    int sf[8][64];
    int q[1024];
    float spec[1024];
    /* TNS */
    int tns_presente;
    int tns_n[8], tns_res[8];
    int tns_tam[8][4], tns_ordem[8][4], tns_dir[8][4];
    float tns_coef[8][4][20];
} canal_t;

struct aac {
    int idx_taxa, taxa, canais_cfg;
    const uint16_t *swb_l, *swb_c;
    int nswb_l, nswb_c;
    canal_t ch[2];
    float sobra[2][1024];
    int forma_ant[2];
    uint32_t semente;
    uint8_t buf[8192 + 16];
};

/* ------------------------------------------------------------ parsing */

static int ler_ics_info(aac_t *a, bits_t *b, ics_info_t *i)
{
    ler(b, 1);
    i->seq = (int)ler(b, 2);
    i->forma = (int)ler(b, 1);
    if (i->seq == CURTAS) {
        i->max_sfb = (int)ler(b, 4);
        int g = (int)ler(b, 7);
        i->n_jan = 8;
        i->n_grupos = 1;
        i->grupo_tam[0] = 1;
        for (int k = 6; k >= 0; k--) {
            if (g & (1 << k)) i->grupo_tam[i->n_grupos - 1]++;
            else i->grupo_tam[i->n_grupos++] = 1;
        }
        if (i->max_sfb > a->nswb_c) return -1;
    } else {
        i->max_sfb = (int)ler(b, 6);
        i->n_jan = 1;
        i->n_grupos = 1;
        i->grupo_tam[0] = 1;
        if (ler(b, 1)) return -1;    /* predição: não é LC */
        if (i->max_sfb > a->nswb_l) return -1;
    }
    return 0;
}

static int ler_ics(aac_t *a, bits_t *b, canal_t *c, bool janela_comum)
{
    ics_info_t *in = &c->info;
    c->global = (int)ler(b, 8);
    if (!janela_comum && ler_ics_info(a, b, in) < 0) return -1;
    bool curta = in->seq == CURTAS;
    const uint16_t *swb = curta ? a->swb_c : a->swb_l;
    /* seções */
    int bits_len = curta ? 3 : 5, esc = (1 << bits_len) - 1;
    for (int g = 0; g < in->n_grupos; g++) {
        int k = 0;
        while (k < in->max_sfb) {
            int cb = (int)ler(b, 4), len = 0, inc;
            if (cb == 12) return -1;
            do { inc = (int)ler(b, bits_len); len += inc; } while (inc == esc && b->pos < b->fim);
            if (k + len > in->max_sfb) return -1;
            for (int i = 0; i < len; i++) c->cb[g][k++] = (uint8_t)cb;
        }
        for (; k < 64; k++) c->cb[g][k] = 0;
    }
    /* fatores de escala */
    int sf = c->global, is = 0, nrg = c->global - 90;
    bool primeiro_ruido = true;
    for (int g = 0; g < in->n_grupos; g++)
        for (int k = 0; k < in->max_sfb; k++) {
            int cb = c->cb[g][k];
            if (cb == 0) c->sf[g][k] = 0;
            else if (cb == 14 || cb == 15) { is += huff(b, 0) - 60; c->sf[g][k] = is; }
            else if (cb == 13) {
                if (primeiro_ruido) { nrg += (int)ler(b, 9) - 256; primeiro_ruido = false; }
                else nrg += huff(b, 0) - 60;
                c->sf[g][k] = nrg;
            } else {
                sf += huff(b, 0) - 60;
                if (sf < 0 || sf > 255) return -1;
                c->sf[g][k] = sf;
            }
        }
    /* pulsos */
    int n_pulsos = 0, pulso_ini = 0, pulso_off[4], pulso_amp[4];
    if (ler(b, 1)) {
        if (curta) return -1;
        n_pulsos = (int)ler(b, 2) + 1;
        pulso_ini = (int)ler(b, 6);
        for (int i = 0; i < n_pulsos; i++) { pulso_off[i] = (int)ler(b, 5); pulso_amp[i] = (int)ler(b, 4); }
    }
    /* TNS */
    c->tns_presente = (int)ler(b, 1);
    if (c->tns_presente) {
        for (int w = 0; w < in->n_jan; w++) {
            c->tns_n[w] = (int)ler(b, curta ? 1 : 2);
            if (!c->tns_n[w]) continue;
            int res = (int)ler(b, 1);
            c->tns_res[w] = res;
            for (int f = 0; f < c->tns_n[w]; f++) {
                c->tns_tam[w][f] = (int)ler(b, curta ? 4 : 6);
                int ordem = (int)ler(b, curta ? 3 : 5);
                if (ordem > (curta ? 7 : 12)) return -1;
                c->tns_ordem[w][f] = ordem;
                if (!ordem) continue;
                c->tns_dir[w][f] = (int)ler(b, 1);
                int comp = (int)ler(b, 1), nb = res + 3 - comp;
                double iq = ((1 << (res + 2)) - 0.5) / (PI / 2), iqm = ((1 << (res + 2)) + 0.5) / (PI / 2);
                float tmp[20];
                for (int i = 0; i < ordem; i++) {
                    int v = (int)ler(b, nb);
                    if (v & (1 << (nb - 1))) v -= 1 << nb;
                    tmp[i] = (float)m_sin(v / (v >= 0 ? iq : iqm));
                }
                /* parcor -> LPC */
                float lpc[20], bb[20];
                lpc[0] = 1;
                for (int m = 1; m <= ordem; m++) {
                    for (int i = 0; i < m; i++) bb[i] = lpc[i];
                    for (int i = 1; i < m; i++) lpc[i] = bb[i] + tmp[m - 1] * bb[m - i];
                    lpc[m] = tmp[m - 1];
                }
                for (int i = 0; i <= ordem; i++) c->tns_coef[w][f][i] = lpc[i];
            }
        }
    }
    if (ler(b, 1)) return -1;    /* controle de ganho: não é LC */
    /* espectro */
    memset(c->q, 0, sizeof c->q);
    int jan = 0;
    for (int g = 0; g < in->n_grupos; g++) {
        for (int k = 0; k < in->max_sfb; k++) {
            int cb = c->cb[g][k];
            int ini = swb[k], fim = swb[k + 1];
            for (int w = 0; w < in->grupo_tam[g]; w++) {
                int *q = c->q + (jan + w) * 128;
                if (cb == 0 || cb >= 13) continue;
                int dim = livro_dim[cb], mod = livro_mod[cb];
                for (int p = ini; p < fim; p += dim) {
                    int idx = huff(b, cb), v[4];
                    if (dim == 4) {
                        v[0] = idx / 27; v[1] = (idx / 9) % 3; v[2] = (idx / 3) % 3; v[3] = idx % 3;
                    } else { v[0] = idx / mod; v[1] = idx % mod; }
                    if (livro_sinal[cb]) {
                        int off = mod / 2;
                        for (int i = 0; i < dim; i++) v[i] -= off;
                    } else {
                        for (int i = 0; i < dim; i++) if (v[i] && ler(b, 1)) v[i] = -v[i];
                        if (cb == 11)
                            for (int i = 0; i < 2; i++) {
                                if (v[i] != 16 && v[i] != -16) continue;
                                int n = 0;
                                while (ler(b, 1)) { n++; if (n > 8) return -1; }
                                int m = (1 << (n + 4)) + (int)ler(b, n + 4);
                                v[i] = v[i] < 0 ? -m : m;
                            }
                    }
                    for (int i = 0; i < dim; i++) q[p + i] = v[i];
                }
            }
        }
        jan += in->grupo_tam[g];
    }
    if (n_pulsos) {
        int k = a->swb_l[pulso_ini < a->nswb_l ? pulso_ini : a->nswb_l];
        for (int i = 0; i < n_pulsos; i++) {
            k += pulso_off[i];
            if (k >= 1024) return -1;
            c->q[k] += c->q[k] > 0 ? pulso_amp[i] : -pulso_amp[i];
        }
    }
    return b->pos <= b->fim ? 0 : -1;
}

/* ----------------------------------------------- reconstrução espectral */

static uint32_t aleatorio(aac_t *a) { a->semente = a->semente * 1664525u + 1013904223u; return a->semente; }

static void desquantizar(aac_t *a, canal_t *c)
{
    ics_info_t *in = &c->info;
    const uint16_t *swb = in->seq == CURTAS ? a->swb_c : a->swb_l;
    memset(c->spec, 0, sizeof c->spec);
    int jan = 0;
    for (int g = 0; g < in->n_grupos; g++) {
        for (int k = 0; k < in->max_sfb; k++) {
            int cb = c->cb[g][k], ini = swb[k], fim = swb[k + 1];
            for (int w = 0; w < in->grupo_tam[g]; w++) {
                float *s = c->spec + (jan + w) * 128;
                int *q = c->q + (jan + w) * 128;
                if (cb == 13) {
                    double e = 0;
                    for (int i = ini; i < fim; i++) { s[i] = (float)(int32_t)aleatorio(a); e += (double)s[i] * s[i]; }
                    double esc = m_pot2q(c->sf[g][k]) / m_sqrt(e > 0 ? e : 1);
                    for (int i = ini; i < fim; i++) s[i] = (float)(s[i] * esc);
                } else if (cb && cb < 13) {
                    float esc = (float)m_pot2q(c->sf[g][k] - 100);
                    for (int i = ini; i < fim; i++) {
                        int v = q[i], m = v < 0 ? -v : v;
                        float x = m < 8192 ? pot43[m] : (float)(m * m_cbrt(m));
                        s[i] = (v < 0 ? -x : x) * esc;
                    }
                }
            }
        }
        jan += in->grupo_tam[g];
    }
}

static void tns(aac_t *a, canal_t *c)
{
    if (!c->tns_presente) return;
    ics_info_t *in = &c->info;
    bool curta = in->seq == CURTAS;
    const uint16_t *swb = curta ? a->swb_c : a->swb_l;
    int nswb = curta ? a->nswb_c : a->nswb_l;
    int maxb = curta ? tns_max_curta[a->idx_taxa] : tns_max_longa[a->idx_taxa];
    if (maxb > in->max_sfb) maxb = in->max_sfb;
    for (int w = 0; w < in->n_jan; w++) {
        float *s = c->spec + w * 128;
        int topo = nswb;
        for (int f = 0; f < c->tns_n[w]; f++) {
            int fundo = topo - c->tns_tam[w][f];
            if (fundo < 0) fundo = 0;
            int ordem = c->tns_ordem[w][f];
            int ini = swb[fundo < maxb ? fundo : maxb], fim = swb[topo < maxb ? topo : maxb];
            topo = fundo;
            int tam = fim - ini;
            if (!ordem || tam <= 0) continue;
            const float *lpc = c->tns_coef[w][f];
            int inc = 1, pos = ini;
            if (c->tns_dir[w][f]) { inc = -1; pos = fim - 1; }
            /* filtro só-polos, na ordem do espectro */
            float hist[20] = { 0 };
            for (int m = 0; m < tam; m++, pos += inc) {
                float y = s[pos];
                for (int i = 1; i <= ordem; i++) y -= lpc[i] * hist[i - 1];
                for (int i = ordem - 1; i > 0; i--) hist[i] = hist[i - 1];
                hist[0] = y;
                s[pos] = y;
            }
        }
    }
}

static void estereo(aac_t *a, int ms_presente, uint8_t ms[8][64])
{
    canal_t *l = &a->ch[0], *r = &a->ch[1];
    ics_info_t *in = &l->info;
    const uint16_t *swb = in->seq == CURTAS ? a->swb_c : a->swb_l;
    int jan = 0;
    for (int g = 0; g < in->n_grupos; g++) {
        for (int k = 0; k < in->max_sfb; k++) {
            int ini = swb[k], fim = swb[k + 1];
            bool usa_ms = ms_presente == 2 || (ms_presente == 1 && ms[g][k]);
            int cbr = r->cb[g][k];
            for (int w = 0; w < in->grupo_tam[g]; w++) {
                float *sl = l->spec + (jan + w) * 128, *sr = r->spec + (jan + w) * 128;
                if (usa_ms && l->cb[g][k] < 13 && cbr < 13)
                    for (int i = ini; i < fim; i++) { float m = sl[i], s = sr[i]; sl[i] = m + s; sr[i] = m - s; }
                if (cbr == 14 || cbr == 15) {
                    float esc = (float)m_pot2q(-r->sf[g][k]);
                    if (cbr == 14) esc = -esc;
                    if (usa_ms) esc = -esc;
                    for (int i = ini; i < fim; i++) sr[i] = sl[i] * esc;
                }
            }
        }
        jan += in->grupo_tam[g];
    }
}

static void banco_filtros(aac_t *a, int ch, float *saida)
{
    canal_t *c = &a->ch[ch];
    ics_info_t *in = &c->info;
    float buf[2048], t[2048];
    const float *ant_l = a->forma_ant[ch] ? jan_kbd_l : jan_seno_l;
    const float *ant_c = a->forma_ant[ch] ? jan_kbd_c : jan_seno_c;
    const float *cur_l = in->forma ? jan_kbd_l : jan_seno_l;
    const float *cur_c = in->forma ? jan_kbd_c : jan_seno_c;
    if (in->seq == CURTAS) {
        memset(buf, 0, sizeof buf);
        for (int w = 0; w < 8; w++) {
            imdct(&md_c, c->spec + w * 128, t);
            const float *sobe = w ? cur_c : ant_c;
            float *d = buf + 448 + w * 128;
            for (int i = 0; i < 128; i++) {
                d[i] += t[i] * sobe[i];
                d[128 + i] += t[128 + i] * cur_c[127 - i];
            }
        }
    } else {
        imdct(&md_l, c->spec, buf);
        if (in->seq == LONGA_FIM) {
            for (int i = 0; i < 448; i++) buf[i] = 0;
            for (int i = 0; i < 128; i++) buf[448 + i] *= ant_c[i];
        } else
            for (int i = 0; i < 1024; i++) buf[i] *= ant_l[i];
        if (in->seq == LONGA_INICIO) {
            for (int i = 0; i < 128; i++) buf[1472 + i] *= cur_c[127 - i];
            for (int i = 1600; i < 2048; i++) buf[i] = 0;
        } else
            for (int i = 0; i < 1024; i++) buf[1024 + i] *= cur_l[1023 - i];
    }
    for (int i = 0; i < 1024; i++) {
        saida[i] = buf[i] + a->sobra[ch][i];
        a->sobra[ch][i] = buf[1024 + i];
    }
    a->forma_ant[ch] = in->forma;
}

/* --------------------------------------------------------------- API */

aac_t *aac_novo(const uint8_t *asc, int n)
{
    if (n < 2 || !tabelas_iniciar()) return NULL;
    int tipo = asc[0] >> 3;
    int idx = ((asc[0] & 7) << 1) | (asc[1] >> 7);
    int canais = (asc[1] >> 3) & 15;
    if (tipo != 2 || idx > 12 || canais < 1 || canais > 2) return NULL;
    aac_t *a = aloca_zerado(sizeof(aac_t));
    if (!a) return NULL;
    a->idx_taxa = idx;
    a->taxa = taxas[idx];
    a->canais_cfg = canais;
    a->semente = 0x1F2E3D4C;
    if (idx <= 4) { a->swb_l = swb_longa_48; a->nswb_l = 49; a->swb_c = swb_curta_48; a->nswb_c = 14; }
    else if (idx == 5) { a->swb_l = swb_longa_32; a->nswb_l = 51; a->swb_c = swb_curta_48; a->nswb_c = 14; }
    else if (idx <= 7) { a->swb_l = swb_longa_24; a->nswb_l = 47; a->swb_c = swb_curta_24; a->nswb_c = 15; }
    else { a->swb_l = swb_longa_16; a->nswb_l = 43; a->swb_c = swb_curta_16; a->nswb_c = 15; }
    return a;
}

void aac_fim(aac_t *a) { if (a) libera(a); }
int aac_taxa(aac_t *a) { return a->taxa; }

static inline int16_t pcm(float v)
{
    int x = (int)(v + (v >= 0 ? 0.5f : -0.5f));
    return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
}

int aac_quadro(aac_t *a, const uint8_t *d, int n, int16_t *saida)
{
    if (n <= 0 || n > 8192) return AAC_ERRO;
    memcpy(a->buf, d, n);
    memset(a->buf + n, 0, 16);
    bits_t b = { a->buf, 0, (uint32_t)n * 8 };
    float pcm_l[1024], pcm_r[1024];
    int canais = 0;
    for (;;) {
        if (b.pos + 3 > b.fim) break;
        int id = (int)ler(&b, 3);
        if (id == 7) break;
        if (id == 0 || id == 3) {           /* SCE, LFE */
            ler(&b, 4);
            if (canais >= 2) return AAC_ERRO;
            canal_t *c = &a->ch[canais];
            if (ler_ics(a, &b, c, false) < 0) return AAC_ERRO;
            desquantizar(a, c);
            tns(a, c);
            banco_filtros(a, canais, canais ? pcm_r : pcm_l);
            canais++;
        } else if (id == 1) {               /* CPE */
            ler(&b, 4);
            if (canais) return AAC_ERRO;
            int comum = (int)ler(&b, 1), ms_p = 0;
            static uint8_t ms[8][64];
            if (comum) {
                if (ler_ics_info(a, &b, &a->ch[0].info) < 0) return AAC_ERRO;
                a->ch[1].info = a->ch[0].info;
                ms_p = (int)ler(&b, 2);
                if (ms_p == 1)
                    for (int g = 0; g < a->ch[0].info.n_grupos; g++)
                        for (int k = 0; k < a->ch[0].info.max_sfb; k++) ms[g][k] = (uint8_t)ler(&b, 1);
                if (ms_p == 3) return AAC_ERRO;
            }
            for (int i = 0; i < 2; i++)
                if (ler_ics(a, &b, &a->ch[i], comum) < 0) return AAC_ERRO;
            desquantizar(a, &a->ch[0]);
            desquantizar(a, &a->ch[1]);
            if (comum) estereo(a, ms_p, ms);
            tns(a, &a->ch[0]);
            tns(a, &a->ch[1]);
            banco_filtros(a, 0, pcm_l);
            banco_filtros(a, 1, pcm_r);
            canais = 2;
        } else if (id == 4) {               /* DSE */
            ler(&b, 4);
            int alinha = (int)ler(&b, 1), cnt = (int)ler(&b, 8);
            if (cnt == 255) cnt += (int)ler(&b, 8);
            if (alinha) b.pos = (b.pos + 7) & ~7u;
            b.pos += cnt * 8;
        } else if (id == 6) {               /* FIL */
            int cnt = (int)ler(&b, 4);
            if (cnt == 15) cnt += (int)ler(&b, 8) - 1;
            b.pos += cnt * 8;
        } else return AAC_SEM_SUPORTE;      /* CCE, PCE */
        if (b.pos > b.fim) return AAC_ERRO;
    }
    if (!canais) return 0;
    for (int i = 0; i < 1024; i++) {
        saida[2 * i] = pcm(pcm_l[i]);
        saida[2 * i + 1] = pcm(canais == 2 ? pcm_r[i] : pcm_l[i]);
    }
    return 1024;
}
