/* vp8.c - WebP com perdas: o quadro-chave VP8 (RFC 6386) dentro do RIFF/WEBP.
 *
 * CÓDIGO DE TERCEIROS (D041, D043). O decodificador e as tabelas
 * (vp8_tabelas.h) seguem o libwebp (Copyright (c) 2010 Google Inc.; licença
 * BSD de 3 cláusulas e concessão de patentes: LICENSE e PATENTS nesta pasta).
 * Foram reproduzidos de memória pela IA que ajudou Robson de Lima Barbosa no
 * pull request #6 do SaguiOS; por isso são tratados como derivados do libwebp.
 * Veja ORIGEM.md para o que mudou.
 *
 * Sem o filtro de suavização dos blocos: a predição não depende dele, e a foto
 * sai certa, só um pouco mais quadriculada em compressão forte. A saída é
 * 0xAARRGGBB, opaca. */
#include "sagui.h"
#include "vp8.h"

#define MAX_PIXELS (4096u * 1024u)
static uint8_t satura(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

#include "vp8_tabelas.h"

typedef struct { const uint8_t *p, *fim; uint32_t val, faixa; int cont; } bool_t;

static void bool_iniciar(bool_t *b, const uint8_t *p, const uint8_t *fim)
{
    b->p = p; b->fim = fim; b->faixa = 255; b->cont = 0;
    b->val = (uint32_t)(b->p < fim ? *b->p++ : 0) << 8;
    b->val |= b->p < fim ? *b->p++ : 0;
}

static int bbit(bool_t *b, int prob)
{
    uint32_t div = 1 + (((b->faixa - 1) * (uint32_t)prob) >> 8), grande = div << 8;
    int r;
    if (b->val >= grande) { r = 1; b->faixa -= div; b->val -= grande; }
    else { r = 0; b->faixa = div; }
    while (b->faixa < 128) {
        b->val <<= 1; b->faixa <<= 1;
        if (++b->cont == 8) { b->cont = 0; if (b->p < b->fim) b->val |= *b->p++; }
    }
    return r;
}

static int blit(bool_t *b, int n) { int v = 0; while (n--) v = v << 1 | bbit(b, 128); return v; }
static int bsin(bool_t *b, int n) { int v = blit(b, n); return bbit(b, 128) ? -v : v; }

enum { B_DC, B_TM, B_VE, B_HE, B_RD, B_VR, B_LD, B_VL, B_HD, B_HU };
static const int8_t arvore4[18] = { -B_DC, 1, -B_TM, 2, -B_VE, 3, 4, 6, -B_HE, 5, -B_RD, -B_VR, -B_LD, 7, -B_VL, 8, -B_HD, -B_HU };
static const uint8_t faixas[17] = { 0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 0 };
static const uint8_t zz4[16] = { 0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15 };

static int valor_grande(bool_t *b, const uint8_t *p)
{
    static const uint8_t c3[] = { 173, 148, 140, 0 }, c4[] = { 176, 155, 140, 135, 0 }, c5[] = { 180, 157, 141, 134, 130, 0 },
                         c6[] = { 254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0 };
    static const uint8_t *const cats[4] = { c3, c4, c5, c6 };
    if (!bbit(b, p[3])) return !bbit(b, p[4]) ? 2 : 3 + bbit(b, p[5]);
    if (!bbit(b, p[6])) {
        if (!bbit(b, p[7])) return 5 + bbit(b, 159);
        int v = 7 + 2 * bbit(b, 165);
        return v + bbit(b, 145);
    }
    int b1 = bbit(b, p[8]), b0 = bbit(b, p[9 + b1]), cat = 2 * b1 + b0, v = 0;
    for (const uint8_t *t = cats[cat]; *t; t++) v += v + bbit(b, *t);
    return v + 3 + (8 << cat);
}

/* prob[faixa][contexto][11]. Devolve a posição depois do último coeficiente. */
static int coeficientes(bool_t *b, const uint8_t (*prob)[3][11], int ctx, const int *dq, int n, int16_t *o)
{
    const uint8_t *p = prob[faixas[n]][ctx];
    for (; n < 16; n++) {
        if (!bbit(b, p[0])) return n;
        while (!bbit(b, p[1])) {
            p = prob[faixas[++n]][0];
            if (n == 16) return 16;
        }
        const uint8_t (*seg)[11] = prob[faixas[n + 1]];
        int v;
        if (!bbit(b, p[2])) { v = 1; p = seg[1]; }
        else { v = valor_grande(b, p); p = seg[2]; }
        o[zz4[n]] = (int16_t)((bbit(b, 128) ? -v : v) * dq[n > 0]);
    }
    return 16;
}

static void wht(const int16_t *e, int16_t *o)
{
    int t[16];
    for (int i = 0; i < 4; i++) {
        int a0 = e[i] + e[12 + i], a1 = e[4 + i] + e[8 + i], a2 = e[4 + i] - e[8 + i], a3 = e[i] - e[12 + i];
        t[i] = a0 + a1; t[8 + i] = a0 - a1; t[4 + i] = a3 + a2; t[12 + i] = a3 - a2;
    }
    for (int i = 0; i < 4; i++) {
        int dc = t[i * 4] + 3, a0 = dc + t[3 + i * 4], a1 = t[1 + i * 4] + t[2 + i * 4], a2 = t[1 + i * 4] - t[2 + i * 4], a3 = dc - t[3 + i * 4];
        o[(i * 4) * 16] = (int16_t)((a0 + a1) >> 3);
        o[(i * 4 + 1) * 16] = (int16_t)((a3 + a2) >> 3);
        o[(i * 4 + 2) * 16] = (int16_t)((a0 - a1) >> 3);
        o[(i * 4 + 3) * 16] = (int16_t)((a3 - a2) >> 3);
    }
}

#define M1(a) ((((a) * 20091) >> 16) + (a))
#define M2(a) (((a) * 35468) >> 16)
static void idct4(const int16_t *e, uint8_t *d, int passo)
{
    int t[16];
    for (int i = 0; i < 4; i++) {
        int a = e[i] + e[8 + i], b = e[i] - e[8 + i], c = M2(e[4 + i]) - M1(e[12 + i]), f = M1(e[4 + i]) + M2(e[12 + i]);
        t[i * 4] = a + f; t[i * 4 + 1] = b + c; t[i * 4 + 2] = b - c; t[i * 4 + 3] = a - f;
    }
    for (int i = 0; i < 4; i++, d += passo) {
        int dc = t[i] + 4, a = dc + t[8 + i], b = dc - t[8 + i], c = M2(t[4 + i]) - M1(t[12 + i]), f = M1(t[4 + i]) + M2(t[12 + i]);
        d[0] = satura(d[0] + ((a + f) >> 3));
        d[1] = satura(d[1] + ((b + c) >> 3));
        d[2] = satura(d[2] + ((b - c) >> 3));
        d[3] = satura(d[3] + ((a - f) >> 3));
    }
}

/* Pixel vizinho, com as bordas que o VP8 define: 127 em cima, 129 à esquerda. */
static inline int viz(const uint8_t *pl, int passo, int x, int y) { return y < 0 ? 127 : x < 0 ? 129 : pl[y * passo + x]; }

/* Predição de um bloco inteiro de t x t (16 no brilho, 8 na cor). */
static void prever_bloco(uint8_t *pl, int passo, int x0, int y0, int t, int modo)
{
    int tl = viz(pl, passo, x0 - 1, y0 - 1);
    if (modo == B_DC) {
        int soma = 0, sh = t == 16 ? 3 : 2, dc;
        if (y0) for (int i = 0; i < t; i++) soma += pl[(y0 - 1) * passo + x0 + i];
        if (x0) for (int i = 0; i < t; i++) soma += pl[(y0 + i) * passo + x0 - 1];
        if (x0 && y0) dc = (soma + t) >> (sh + 2);
        else if (x0 || y0) dc = (soma + t / 2) >> (sh + 1);
        else dc = 128;
        for (int y = 0; y < t; y++) memset(pl + (y0 + y) * passo + x0, dc, (size_t)t);
        return;
    }
    for (int y = 0; y < t; y++) {
        int e = viz(pl, passo, x0 - 1, y0 + y);
        for (int x = 0; x < t; x++) {
            int c = viz(pl, passo, x0 + x, y0 - 1);
            pl[(y0 + y) * passo + x0 + x] = modo == B_VE ? (uint8_t)c : modo == B_HE ? (uint8_t)e : satura(e + c - tl);
        }
    }
}

#define A3(a, b, c) (uint8_t)(((a) + 2 * (b) + (c) + 2) >> 2)
#define A2(a, b) (uint8_t)(((a) + (b) + 1) >> 1)
#define D(x, y) o[(y) * passo + (x)]
static void prever4(uint8_t *pl, int passo, int x0, int y0, int modo, const uint8_t *dir)
{
    int T[8], X = viz(pl, passo, x0 - 1, y0 - 1), I = viz(pl, passo, x0 - 1, y0), J = viz(pl, passo, x0 - 1, y0 + 1),
        K = viz(pl, passo, x0 - 1, y0 + 2), L = viz(pl, passo, x0 - 1, y0 + 3);
    for (int i = 0; i < 4; i++) { T[i] = viz(pl, passo, x0 + i, y0 - 1); T[4 + i] = dir[i]; }
    int A = T[0], B = T[1], C = T[2], E0 = T[3], E = T[4], F = T[5], G = T[6], H = T[7];
    uint8_t *o = pl + y0 * passo + x0;
    switch (modo) {
    case B_DC: { int v = (A + B + C + E0 + I + J + K + L + 4) >> 3; for (int y = 0; y < 4; y++) memset(o + y * passo, v, 4); break; }
    case B_TM: { int e[4] = { I, J, K, L }; for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) D(x, y) = satura(e[y] + T[x] - X); break; }
    case B_VE: { uint8_t v[4] = { A3(X, A, B), A3(A, B, C), A3(B, C, E0), A3(C, E0, E) }; for (int y = 0; y < 4; y++) memcpy(o + y * passo, v, 4); break; }
    case B_HE: memset(o, A3(X, I, J), 4); memset(o + passo, A3(I, J, K), 4); memset(o + 2 * passo, A3(J, K, L), 4); memset(o + 3 * passo, A3(K, L, L), 4); break;
    case B_RD:
        D(0, 3) = A3(J, K, L); D(1, 3) = D(0, 2) = A3(I, J, K); D(2, 3) = D(1, 2) = D(0, 1) = A3(X, I, J);
        D(3, 3) = D(2, 2) = D(1, 1) = D(0, 0) = A3(A, X, I); D(3, 2) = D(2, 1) = D(1, 0) = A3(B, A, X);
        D(3, 1) = D(2, 0) = A3(C, B, A); D(3, 0) = A3(E0, C, B); break;
    case B_LD:
        D(0, 0) = A3(A, B, C); D(1, 0) = D(0, 1) = A3(B, C, E0); D(2, 0) = D(1, 1) = D(0, 2) = A3(C, E0, E);
        D(3, 0) = D(2, 1) = D(1, 2) = D(0, 3) = A3(E0, E, F); D(3, 1) = D(2, 2) = D(1, 3) = A3(E, F, G);
        D(3, 2) = D(2, 3) = A3(F, G, H); D(3, 3) = A3(G, H, H); break;
    case B_VR:
        D(0, 0) = D(1, 2) = A2(X, A); D(1, 0) = D(2, 2) = A2(A, B); D(2, 0) = D(3, 2) = A2(B, C); D(3, 0) = A2(C, E0);
        D(0, 3) = A3(K, J, I); D(0, 2) = A3(J, I, X); D(0, 1) = D(1, 3) = A3(I, X, A); D(1, 1) = D(2, 3) = A3(X, A, B);
        D(2, 1) = D(3, 3) = A3(A, B, C); D(3, 1) = A3(B, C, E0); break;
    case B_VL:
        D(0, 0) = A2(A, B); D(1, 0) = D(0, 2) = A2(B, C); D(2, 0) = D(1, 2) = A2(C, E0); D(3, 0) = D(2, 2) = A2(E0, E);
        D(0, 1) = A3(A, B, C); D(1, 1) = D(0, 3) = A3(B, C, E0); D(2, 1) = D(1, 3) = A3(C, E0, E); D(3, 1) = D(2, 3) = A3(E0, E, F);
        D(3, 2) = A3(E, F, G); D(3, 3) = A3(F, G, H); break;
    case B_HU:
        D(0, 0) = A2(I, J); D(2, 0) = D(0, 1) = A2(J, K); D(2, 1) = D(0, 2) = A2(K, L); D(1, 0) = A3(I, J, K);
        D(3, 0) = D(1, 1) = A3(J, K, L); D(3, 1) = D(1, 2) = A3(K, L, L);
        D(3, 2) = D(2, 2) = D(0, 3) = D(1, 3) = D(2, 3) = D(3, 3) = (uint8_t)L; break;
    default:   /* B_HD */
        D(0, 0) = D(2, 1) = A2(I, X); D(0, 1) = D(2, 2) = A2(J, I); D(0, 2) = D(2, 3) = A2(K, J); D(0, 3) = A2(L, K);
        D(3, 0) = A3(A, B, C); D(2, 0) = A3(X, A, B); D(1, 0) = D(3, 1) = A3(I, X, A); D(1, 1) = D(3, 2) = A3(J, I, X);
        D(1, 2) = D(3, 3) = A3(K, J, I); D(1, 3) = A3(L, K, J); break;
    }
}

typedef struct { int y1[2], y2[2], uv[2]; } quant_t;
static int limita(int v, int m) { return v < 0 ? 0 : v > m ? m : v; }

static bool vp8(const uint8_t *d, unsigned n, uint32_t **saida, int *pl, int *pa)
{
    static uint8_t prob[4][8][3][11];
    if (n < 10) return false;
    uint32_t bits = d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16;
    unsigned parte0 = bits >> 5;
    if ((bits & 1) || d[3] != 0x9D || d[4] != 1 || d[5] != 0x2A) return false;
    int l = (d[6] | d[7] << 8) & 0x3FFF, a = (d[8] | d[9] << 8) & 0x3FFF;
    if (!l || !a || (unsigned)l * (unsigned)a > MAX_PIXELS || 10 + parte0 > n) return false;
    const uint8_t *fim = d + n, *q = d + 10;
    bool_t b;
    bool_iniciar(&b, q, q + parte0);
    bbit(&b, 128); bbit(&b, 128);                     /* espaço de cor e corte */
    int usa_seg = bbit(&b, 128), mapa = 0, absoluto = 0, seg_q[4] = { 0, 0, 0, 0 }, seg_p[3] = { 255, 255, 255 };
    if (usa_seg) {
        mapa = bbit(&b, 128);
        if (bbit(&b, 128)) {
            absoluto = bbit(&b, 128);
            for (int i = 0; i < 4; i++) seg_q[i] = bbit(&b, 128) ? bsin(&b, 7) : 0;
            for (int i = 0; i < 4; i++) if (bbit(&b, 128)) bsin(&b, 6);
        }
        if (mapa) for (int i = 0; i < 3; i++) seg_p[i] = bbit(&b, 128) ? blit(&b, 8) : 255;
    }
    bbit(&b, 128); blit(&b, 6); blit(&b, 3);          /* filtro: não usado */
    if (bbit(&b, 128) && bbit(&b, 128))
        for (int i = 0; i < 8; i++) if (bbit(&b, 128)) bsin(&b, 6);
    int npart = 1 << blit(&b, 2);
    const uint8_t *tam = q + parte0, *dados = tam + 3 * (npart - 1);
    if (dados > fim) return false;
    bool_t *parts = aloca(sizeof(bool_t) * (size_t)npart);
    if (!parts) return false;
    for (int i = 0; i < npart; i++) {
        unsigned t = i < npart - 1 ? tam[3 * i] | (unsigned)tam[3 * i + 1] << 8 | (unsigned)tam[3 * i + 2] << 16 : (unsigned)(fim - dados);
        if (t > (unsigned)(fim - dados)) t = (unsigned)(fim - dados);
        bool_iniciar(&parts[i], dados, dados + t);
        dados += t;
    }
    int bq = blit(&b, 7), dy1 = bbit(&b, 128) ? bsin(&b, 4) : 0, dy2d = bbit(&b, 128) ? bsin(&b, 4) : 0, dy2a = bbit(&b, 128) ? bsin(&b, 4) : 0,
        duvd = bbit(&b, 128) ? bsin(&b, 4) : 0, duva = bbit(&b, 128) ? bsin(&b, 4) : 0;
    quant_t qs[4];
    for (int i = 0; i < 4; i++) {
        int v = usa_seg ? (absoluto ? seg_q[i] : bq + seg_q[i]) : bq;
        if (!usa_seg && i) { qs[i] = qs[0]; continue; }
        qs[i].y1[0] = vp8_dc[limita(v + dy1, 127)]; qs[i].y1[1] = vp8_ac[limita(v, 127)];
        qs[i].y2[0] = vp8_dc[limita(v + dy2d, 127)] * 2;
        qs[i].y2[1] = (vp8_ac[limita(v + dy2a, 127)] * 101581) >> 16;
        if (qs[i].y2[1] < 8) qs[i].y2[1] = 8;
        qs[i].uv[0] = vp8_dc[limita(v + duvd, 117)]; qs[i].uv[1] = vp8_ac[limita(v + duva, 127)];
    }
    bbit(&b, 128);                                     /* guardar probabilidades */
    uint8_t *pp = &prob[0][0][0][0];
    for (int i = 0; i < 1056; i++) pp[i] = bbit(&b, vp8_coef_atual[i]) ? (uint8_t)blit(&b, 8) : vp8_coef[i];
    int usa_pulo = bbit(&b, 128), p_pulo = usa_pulo ? blit(&b, 8) : 0;

    int mw = (l + 15) / 16, mh = (a + 15) / 16, py = mw * 16, pc = mw * 8;
    uint8_t *Y = aloca((size_t)py * (size_t)(mh * 16)), *U = aloca((size_t)pc * (size_t)(mh * 8)), *V = aloca((size_t)pc * (size_t)(mh * 8));
    uint8_t *topo_m = aloca((size_t)mw * 4), *topo_nz = aloca((size_t)mw * 9);
    uint32_t *px = aloca((size_t)l * (size_t)a * 4);
    bool ok = false;
    if (!Y || !U || !V || !topo_m || !topo_nz || !px) goto fim;
    memset(topo_m, B_DC, (size_t)mw * 4);
    memset(topo_nz, 0, (size_t)mw * 9);
    for (int my = 0; my < mh; my++) {
        uint8_t esq_m[4] = { B_DC, B_DC, B_DC, B_DC }, esq_nz[9] = { 0 };
        bool_t *t = &parts[my & (npart - 1)];
        for (int mx = 0; mx < mw; mx++) {
            int seg = 0, modo16 = 0, modo_uv, modos[16];
            uint8_t *tm = topo_m + mx * 4, *tn = topo_nz + mx * 9;
            if (mapa) seg = bbit(&b, seg_p[0]) ? 2 + bbit(&b, seg_p[2]) : bbit(&b, seg_p[1]);
            int pulo = usa_pulo ? bbit(&b, p_pulo) : 0;
            bool i4 = !bbit(&b, 145);
            if (!i4) {
                modo16 = bbit(&b, 156) ? (bbit(&b, 128) ? B_TM : B_HE) : (bbit(&b, 163) ? B_VE : B_DC);
                memset(tm, modo16, 4); memset(esq_m, modo16, 4);
            } else {
                for (int y = 0; y < 4; y++) {
                    int e = esq_m[y];
                    for (int x = 0; x < 4; x++) {
                        const uint8_t *pr = vp8_modos + (tm[x] * 10 + e) * 9;
                        int m = arvore4[bbit(&b, pr[0])];
                        while (m > 0) m = arvore4[2 * m + bbit(&b, pr[m])];
                        e = -m;
                        tm[x] = (uint8_t)e;
                        modos[y * 4 + x] = e;
                    }
                    esq_m[y] = (uint8_t)e;
                }
            }
            modo_uv = !bbit(&b, 142) ? B_DC : !bbit(&b, 114) ? B_VE : bbit(&b, 183) ? B_TM : B_HE;

            static int16_t co[25 * 16];
            int nz[24];
            memset(co, 0, sizeof co);
            memset(nz, 0, sizeof nz);
            const quant_t *qq = &qs[seg];
            if (!pulo) {
                int primeiro = 0, tipo = 3;
                if (!i4) {
                    int16_t dc[16];
                    memset(dc, 0, sizeof dc);
                    int r = coeficientes(t, (const void *)prob[1], tn[8] + esq_nz[8], qq->y2, 0, dc);
                    tn[8] = esq_nz[8] = r > 0;
                    wht(dc, co);
                    primeiro = 1; tipo = 0;
                }
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++) {
                        int r = coeficientes(t, (const void *)prob[tipo], tn[x] + esq_nz[y], qq->y1, primeiro, co + (y * 4 + x) * 16);
                        tn[x] = esq_nz[y] = r > primeiro;
                    }
                for (int c = 0; c < 2; c++)
                    for (int y = 0; y < 2; y++)
                        for (int x = 0; x < 2; x++) {
                            int r = coeficientes(t, (const void *)prob[2], tn[4 + c * 2 + x] + esq_nz[4 + c * 2 + y], qq->uv, 0, co + (16 + c * 4 + y * 2 + x) * 16);
                            tn[4 + c * 2 + x] = esq_nz[4 + c * 2 + y] = r > 0;
                        }
            } else {
                memset(tn, 0, 8); memset(esq_nz, 0, 8);
                if (!i4) tn[8] = esq_nz[8] = 0;
            }
            int x0 = mx * 16, y0 = my * 16;
            if (!i4) {
                prever_bloco(Y, py, x0, y0, 16, modo16);
                for (int i = 0; i < 16; i++) idct4(co + i * 16, Y + (y0 + i / 4 * 4) * py + x0 + i % 4 * 4, py);
            } else {
                uint8_t dir[4];
                if (!my) memset(dir, 127, 4);
                else if (mx < mw - 1) memcpy(dir, Y + (y0 - 1) * py + x0 + 16, 4);
                else memset(dir, Y[(y0 - 1) * py + x0 + 15], 4);
                for (int i = 0; i < 16; i++) {
                    int bx = x0 + i % 4 * 4, by = y0 + i / 4 * 4;
                    uint8_t v[4];
                    const uint8_t *e = dir;
                    if (i % 4 < 3) {
                        for (int k = 0; k < 4; k++) v[k] = (uint8_t)viz(Y, py, bx + 4 + k, by - 1);
                        e = v;
                    }
                    prever4(Y, py, bx, by, modos[i], e);
                    idct4(co + i * 16, Y + by * py + bx, py);
                }
            }
            prever_bloco(U, pc, mx * 8, my * 8, 8, modo_uv);
            prever_bloco(V, pc, mx * 8, my * 8, 8, modo_uv);
            for (int i = 0; i < 4; i++) {
                int ox = mx * 8 + i % 2 * 4, oy = my * 8 + i / 2 * 4;
                idct4(co + (16 + i) * 16, U + oy * pc + ox, pc);
                idct4(co + (20 + i) * 16, V + oy * pc + ox, pc);
            }
        }
    }
    for (int y = 0; y < a; y++)
        for (int x = 0; x < l; x++) {
            int c = 298 * (Y[y * py + x] - 16), u = U[y / 2 * pc + x / 2] - 128, v = V[y / 2 * pc + x / 2] - 128;
            px[y * l + x] = 0xFF000000u | (uint32_t)satura((c + 409 * v + 128) >> 8) << 16 |
                            (uint32_t)satura((c - 100 * u - 208 * v + 128) >> 8) << 8 |
                            satura((c + 516 * u + 128) >> 8);
        }
    *saida = px; *pl = l; *pa = a;
    ok = true;
fim:
    if (!ok) libera(px);
    libera(Y); libera(U); libera(V); libera(topo_m); libera(topo_nz); libera(parts);
    return ok;
}

bool webp_ler(const uint8_t *d, unsigned n, uint32_t **saida, int *pl, int *pa)
{
    for (unsigned k = 12; k + 8 <= n;) {
        unsigned t = d[k + 4] | (unsigned)d[k + 5] << 8 | (unsigned)d[k + 6] << 16 | (unsigned)d[k + 7] << 24;
        if (!memcmp(d + k, "VP8 ", 4)) return vp8(d + k + 8, t < n - k - 8 ? t : n - k - 8, saida, pl, pa);
        if (t > n) break;
        k += 8 + t + (t & 1);
    }
    return false;
}
