/* ------------------------------------------------------------------ CABAC */

typedef struct {
    bits_t *b;
    uint32_t faixa, desl;
    uint8_t st[460];        /* estado*2 + MPS */
} cabac_t;

static void cabac_iniciar(h264_t *h, cabac_t *k, bits_t *b)
{
    k->b = b;
    const int8_t (*tab)[2] = h->tipo == 2 ? cabac_init_I : h->cabac_init == 0 ? cabac_init_PB0 : h->cabac_init == 1 ? cabac_init_PB1 : cabac_init_PB2;
    int qp = clip(h->qp, 0, 51);
    for (int i = 0; i < 460; i++) {
        int pre = clip(((tab[i][0] * qp) >> 4) + tab[i][1], 1, 126);
        k->st[i] = (uint8_t)(pre <= 63 ? (63 - pre) << 1 : ((pre - 64) << 1) | 1);
    }
    k->faixa = 510;
    k->desl = ler_bits(b, 9);
}

static inline int cab(cabac_t *k, int ctx)
{
    uint8_t s = k->st[ctx];
    int estado = s >> 1, mps = s & 1;
    uint32_t lps = cabac_lps[estado][(k->faixa >> 6) & 3];
    int bin;
    k->faixa -= lps;
    if (k->desl >= k->faixa) {
        bin = !mps;
        k->desl -= k->faixa;
        k->faixa = lps;
        if (!estado) mps = !mps;
        estado = cabac_trans_lps[estado];
    } else {
        bin = mps;
        if (estado < 62) estado++;
    }
    k->st[ctx] = (uint8_t)(estado << 1 | mps);
    while (k->faixa < 256) { k->faixa <<= 1; k->desl = (k->desl << 1) | (uint32_t)ler_bit(k->b); }
    return bin;
}

static inline int cab_bypass(cabac_t *k)
{
    k->desl = (k->desl << 1) | (uint32_t)ler_bit(k->b);
    if (k->desl >= k->faixa) { k->desl -= k->faixa; return 1; }
    return 0;
}

static inline int cab_fim(cabac_t *k)
{
    k->faixa -= 2;
    if (k->desl >= k->faixa) return 1;
    while (k->faixa < 256) { k->faixa <<= 1; k->desl = (k->desl << 1) | (uint32_t)ler_bit(k->b); }
    return 0;
}

/* tipo do MB intra (I: base 3; sufixo em P: 17, em B: 32) -> 0 I_NxN, 1..24 I_16x16, 25 I_PCM */
static int cab_intra(cabac_t *k, int base, bool fatia_i, ctx_t *c)
{
    if (fatia_i) {
        int inc = (c->a && !c->a->i4x4) + (c->b && !c->b->i4x4);
        if (!cab(k, base + inc)) return 0;
        base += 2;
    } else if (!cab(k, base)) return 0;
    if (cab_fim(k)) return 25;
    int t = 1;
    t += 12 * cab(k, base + 1);
    if (cab(k, base + 2)) t += 4 + 4 * cab(k, base + 2 + fatia_i);
    t += 2 * cab(k, base + 3 + fatia_i);
    t += cab(k, base + 3 + 2 * fatia_i);
    return t;
}

static int cab_mvd(cabac_t *k, int base, int soma)
{
    int inc = soma < 3 ? 0 : soma > 32 ? 2 : 1;
    if (!cab(k, base + inc)) return 0;
    int v = 1, ctx = base + 3;
    while (v < 9 && cab(k, ctx)) { if (v < 4) ctx++; v++; }
    if (v >= 9) {
        int e = 3;
        while (cab_bypass(k)) { v += 1 << e; e++; if (e > 24) break; }
        while (e--) v += cab_bypass(k) << e;
    }
    return cab_bypass(k) ? -v : v;
}

static const uint8_t cbf_off[5] = { 0, 4, 8, 12, 16 };
static const uint8_t sig_off[5] = { 0, 15, 29, 44, 47 };
static const uint8_t lev_off[5] = { 0, 10, 20, 30, 39 };

/* Bloco residual; coef na ordem de varredura. Devolve quantos não nulos, ou 0 se coded_block_flag = 0. */
static int cab_residuo(cabac_t *k, int cat, int inc_cbf, int *coef, int max)
{
    if (!cab(k, 85 + cbf_off[cat] + inc_cbf)) return 0;
    int sig[16], ns = 0, i;
    for (i = 0; i < max - 1; i++) {
        int ci = cat == 3 ? (i < 2 ? i : 2) : i;
        if (cab(k, 105 + sig_off[cat] + ci)) {
            sig[ns++] = i;
            if (cab(k, 166 + sig_off[cat] + ci)) break;
        }
    }
    if (i == max - 1) sig[ns++] = max - 1;
    int gt1 = 0, eq1 = 0;
    for (int j = ns - 1; j >= 0; j--) {
        int ctx = 227 + lev_off[cat] + (gt1 ? 0 : (1 + eq1 < 4 ? 1 + eq1 : 4));
        int v;
        if (!cab(k, ctx)) { v = 1; eq1++; }
        else {
            int c2 = 227 + lev_off[cat] + 5 + (gt1 < 4 - (cat == 3) ? gt1 : 4 - (cat == 3));
            v = 2;
            while (v < 15 && cab(k, c2)) v++;
            if (v >= 15) {
                int e = 0;
                while (cab_bypass(k)) { e++; if (e > 20) break; }
                int x = 1;
                while (e--) x = (x << 1) | cab_bypass(k);
                v = 14 + x;
            }
            gt1++;
        }
        coef[sig[j]] = cab_bypass(k) ? -v : v;
    }
    return ns;
}

/* coded_block_flag dos vizinhos */
static int cbf_luma(ctx_t *c, int bx, int by, bool intra)
{
    mbinfo_t *m = c->m;
    if (bx < 0) { m = c->a; bx += 4; }
    else if (by < 0) { m = c->b; by += 4; }
    if (!m) return intra;
    if (m->intra == 2) return 1;
    return m->nz[by * 4 + bx] != 0;
}
static int cbf_croma_ac(ctx_t *c, int comp, int bx, int by, bool intra)
{
    mbinfo_t *m = c->m;
    if (bx < 0) { m = c->a; bx += 2; }
    else if (by < 0) { m = c->b; by += 2; }
    if (!m) return intra;
    if (m->intra == 2) return 1;
    if ((m->cbp >> 4) != 2) return 0;
    return m->nz[16 + comp * 4 + by * 2 + bx] != 0;
}
static int cbf_dc(mbinfo_t *n, int bit, bool intra, bool luma)
{
    if (!n) return intra;
    if (n->intra == 2) return 1;
    if (luma && !n->i16) return 0;
    if (!luma && !(n->cbp & 0x30)) return 0;
    return (n->dc >> bit) & 1;
}

static const uint8_t b_parte[23][2] = {   /* pred por partição: 1 L0, 2 L1, 3 Bi */
    {0,0},{1,0},{2,0},{3,0},{1,1},{1,1},{2,2},{2,2},{1,2},{1,2},{2,1},{2,1},{1,3},{1,3},{2,3},{2,3},{3,1},{3,1},{3,2},{3,2},{3,3},{3,3},{0,0} };
static const uint8_t b_sub[13][2] = {      /* pred, forma (0 8x8, 1 8x4, 2 4x8, 3 4x4) */
    {0,0},{1,0},{2,0},{3,0},{1,1},{1,2},{2,1},{2,2},{3,1},{3,2},{1,3},{2,3},{3,3} };

static int cab_ref(cabac_t *k, mvc_t *kk, int bx, int by)
{
    int ra = kk->ref[by + 1][bx], rb = kk->ref[by][bx + 1];
    int inc = (ra > 0 && !kk->dir[by + 1][bx]) + 2 * (rb > 0 && !kk->dir[by][bx + 1]);
    int r = 0;
    while (cab(k, 54 + inc)) { r++; inc = (inc >> 2) + 4; if (r > 31) break; }
    return r;
}

/* lê mvd de uma partição e grava mv + |mvd| no cache */
static void cab_mv(cabac_t *k, mvc_t *kk, int l, int bx, int by, int w, int hh, int forma, int ref)
{
    (void)l;
    int px, py;
    prever_mv(kk, bx, by, w, ref, forma, &px, &py);
    int sx = kk->mvd[by + 1][bx][0] + kk->mvd[by][bx + 1][0];
    int sy = kk->mvd[by + 1][bx][1] + kk->mvd[by][bx + 1][1];
    int dx = cab_mvd(k, 40, sx), dy = cab_mvd(k, 47, sy);
    mvc_escrever(kk, bx, by, w, hh, ref, px + dx, py + dy);
    int ax = absi(dx), ay = absi(dy);
    for (int y = by; y < by + hh; y++)
        for (int x = bx; x < bx + w; x++) { kk->mvd[y + 1][x + 1][0] = (uint8_t)(ax > 255 ? 255 : ax); kk->mvd[y + 1][x + 1][1] = (uint8_t)(ay > 255 ? 255 : ay); }
}

static int qpd_ant;   /* o MB anterior teve mb_qp_delta != 0 */

static int decodificar_mb_cabac(h264_t *h, cabac_t *k, ctx_t *c, bits_t *b)
{
    mbinfo_t *m = c->m;
    iniciar_mb(h, m);
    mvc_t kk[2];
    int fatia = h->tipo;   /* 0 P, 1 B, 2 I */
    bool skip = false;
    if (fatia != 2) {
        int base = fatia == 0 ? 11 : 24;
        int inc = (c->a && !c->a->skip) + (c->b && !c->b->skip);
        skip = cab(k, base + inc);
    }
    int tipo = 0;
    bool intra = false;
    if (!skip) {
        if (fatia == 2) { tipo = cab_intra(k, 3, true, c); intra = true; }
        else if (fatia == 0) {
            if (!cab(k, 14)) {
                if (!cab(k, 15)) tipo = 3 * cab(k, 16);
                else tipo = 2 - cab(k, 17);
            } else { tipo = cab_intra(k, 17, false, c); intra = true; }
        } else {
            int inc = (c->a && !(c->a->skip || c->a->direto)) + (c->b && !(c->b->skip || c->b->direto));
            if (!cab(k, 27 + inc)) tipo = 0;
            else if (!cab(k, 27 + 3)) tipo = 1 + cab(k, 27 + 5);
            else {
                int bits = cab(k, 27 + 4) << 3;
                bits |= cab(k, 27 + 5) << 2;
                bits |= cab(k, 27 + 5) << 1;
                bits |= cab(k, 27 + 5);
                if (bits < 8) tipo = bits + 3;
                else if (bits == 13) { tipo = cab_intra(k, 32, false, c); intra = true; }
                else if (bits == 14) tipo = 11;
                else if (bits == 15) tipo = 22;
                else { bits = (bits << 1) | cab(k, 27 + 5); tipo = bits - 4; }
            }
        }
    }
    int cbp = 0, modo16 = -1, modo_croma = 0;
    if (intra && tipo == 25) {
        /* I_PCM: os bytes começam alinhados depois do decodificador aritmético */
        pcm(h, c, m, b);
        cabac_iniciar(h, k, b);
        qpd_ant = 0;
        return 0;
    }
    if (intra) {
        m->intra = 1;
        if (tipo == 0) {
            m->i4x4 = 1;
            for (int i = 0; i < 16; i++) {
                int bx = blk_x[i], by = blk_y[i];
                int ma, mb;
                bool oka, okb;
                if (bx > 0) { ma = m->modo4[by * 4 + bx - 1]; oka = true; }
                else if (c->a) { ma = c->a->i4x4 ? c->a->modo4[by * 4 + 3] : 2; oka = true; }
                else { ma = 2; oka = false; }
                if (by > 0) { mb = m->modo4[(by - 1) * 4 + bx]; okb = true; }
                else if (c->b) { mb = c->b->i4x4 ? c->b->modo4[12 + bx] : 2; okb = true; }
                else { mb = 2; okb = false; }
                int pred = (oka && okb) ? (ma < mb ? ma : mb) : 2;
                int modo;
                if (cab(k, 68)) modo = pred;
                else { int r = cab(k, 69); r |= cab(k, 69) << 1; r |= cab(k, 69) << 2; modo = r < pred ? r : r + 1; }
                m->modo4[by * 4 + bx] = (int8_t)modo;
            }
        } else {
            m->i16 = 1;
            modo16 = (tipo - 1) % 4;
            cbp = (((tipo - 1) / 4) % 3) << 4;
            if (tipo >= 13) cbp |= 15;
        }
        {
            int inc = (c->a && c->a->intra == 1 && c->a->modo_croma) + (c->b && c->b->intra == 1 && c->b->modo_croma);
            if (!cab(k, 64 + inc)) modo_croma = 0;
            else if (!cab(k, 67)) modo_croma = 1;
            else if (!cab(k, 67)) modo_croma = 2;
            else modo_croma = 3;
        }
        sem_movimento(m);
    } else {
        mvc_preencher(c, &kk[0], 0);
        mvc_preencher(c, &kk[1], 1);
        if (fatia == 0) for (int y = 1; y < 5; y++) for (int x = 1; x < 5; x++) kk[1].ref[y][x] = REF_INTRA;
        if (skip) {
            m->skip = 1;
            if (fatia == 0) { int mx, my; mv_skip_p(&kk[0], &mx, &my); mvc_escrever(&kk[0], 0, 0, 4, 4, 0, mx, my); }
            else { m->direto = 1; prever_direto(h, c, kk, 0xFFFF); for (int y = 1; y < 5; y++) for (int x = 1; x < 5; x++) kk[0].dir[y][x] = 1; }
        } else if (fatia == 0) {
            int nref = h->num_ref[0];
            if (tipo < 3) {
                int np = tipo == 0 ? 1 : 2, ri[2] = { 0, 0 };
                int pb[2][4] = { { 0, 0, 4, 4 }, { 0, 0, 0, 0 } };
                for (int p = 0; p < np; p++) {
                    int bx, by, w, hh;
                    if (tipo == 0) { bx = 0; by = 0; w = 4; hh = 4; }
                    else if (tipo == 1) { bx = 0; by = p * 2; w = 4; hh = 2; }
                    else { bx = p * 2; by = 0; w = 2; hh = 4; }
                    pb[p][0] = bx; pb[p][1] = by; pb[p][2] = w; pb[p][3] = hh;
                    if (nref > 1) ri[p] = cab_ref(k, &kk[0], bx, by);
                    mvc_escrever(&kk[0], bx, by, w, hh, ri[p], 0, 0);
                }
                for (int p = 0; p < np; p++) {
                    int forma = tipo == 0 ? 0 : tipo == 1 ? 1 + p : 3 + p;
                    /* o vetor do vizinho dentro do MB ainda não existe: limpa a ref antes de prever */
                    mvc_escrever(&kk[0], pb[p][0], pb[p][1], pb[p][2], pb[p][3], REF_NADA, 0, 0);
                    cab_mv(k, &kk[0], 0, pb[p][0], pb[p][1], pb[p][2], pb[p][3], forma, ri[p]);
                }
            } else {
                int sub[4], ri[4] = { 0, 0, 0, 0 };
                for (int i = 0; i < 4; i++) {
                    if (cab(k, 21)) sub[i] = 0;
                    else if (!cab(k, 22)) sub[i] = 1;
                    else sub[i] = cab(k, 23) ? 2 : 3;
                }
                for (int i = 0; i < 4; i++) {
                    if (nref > 1 && tipo != 4) ri[i] = cab_ref(k, &kk[0], (i & 1) * 2, (i >> 1) * 2);
                    mvc_escrever(&kk[0], (i & 1) * 2, (i >> 1) * 2, 2, 2, ri[i], 0, 0);
                }
                for (int i = 0; i < 4; i++) mvc_escrever(&kk[0], (i & 1) * 2, (i >> 1) * 2, 2, 2, REF_NADA, 0, 0);
                for (int i = 0; i < 4; i++) {
                    int bx0 = (i & 1) * 2, by0 = (i >> 1) * 2;
                    int n = sub[i] == 0 ? 1 : sub[i] == 3 ? 4 : 2;
                    for (int s = 0; s < n; s++) {
                        int bx = bx0, by = by0, w = 2, hh = 2;
                        if (sub[i] == 1) { hh = 1; by += s; }
                        else if (sub[i] == 2) { w = 1; bx += s; }
                        else if (sub[i] == 3) { w = 1; hh = 1; bx += s & 1; by += s >> 1; }
                        cab_mv(k, &kk[0], 0, bx, by, w, hh, 0, ri[i]);
                    }
                }
            }
        } else {
            /* fatia B */
            if (tipo == 0) {
                m->direto = 1;
                prever_direto(h, c, kk, 0xFFFF);
                for (int y = 1; y < 5; y++) for (int x = 1; x < 5; x++) kk[0].dir[y][x] = 1;
            } else if (tipo < 22) {
                int np = tipo <= 3 ? 1 : 2, ri[2][2] = { { -1, -1 }, { -1, -1 } };
                int pb[2][4];
                for (int p = 0; p < np; p++) {
                    if (tipo <= 3) { pb[p][0] = 0; pb[p][1] = 0; pb[p][2] = 4; pb[p][3] = 4; }
                    else if (tipo % 2 == 0) { pb[p][0] = 0; pb[p][1] = p * 2; pb[p][2] = 4; pb[p][3] = 2; }
                    else { pb[p][0] = p * 2; pb[p][1] = 0; pb[p][2] = 2; pb[p][3] = 4; }
                }
                for (int l = 0; l < 2; l++)
                    for (int p = 0; p < np; p++) {
                        int pred = b_parte[tipo][p];
                        if (!(pred & (1 << l))) { mvc_escrever(&kk[l], pb[p][0], pb[p][1], pb[p][2], pb[p][3], REF_INTRA, 0, 0); continue; }
                        ri[l][p] = h->num_ref[l] > 1 ? cab_ref(k, &kk[l], pb[p][0], pb[p][1]) : 0;
                        mvc_escrever(&kk[l], pb[p][0], pb[p][1], pb[p][2], pb[p][3], ri[l][p], 0, 0);
                    }
                for (int l = 0; l < 2; l++)
                    for (int p = 0; p < np; p++) if (ri[l][p] >= 0) mvc_escrever(&kk[l], pb[p][0], pb[p][1], pb[p][2], pb[p][3], REF_NADA, 0, 0);
                for (int l = 0; l < 2; l++)
                    for (int p = 0; p < np; p++) {
                        if (ri[l][p] < 0) continue;
                        int forma = tipo <= 3 ? 0 : tipo % 2 == 0 ? 1 + p : 3 + p;
                        cab_mv(k, &kk[l], l, pb[p][0], pb[p][1], pb[p][2], pb[p][3], forma, ri[l][p]);
                    }
            } else {
                int sub[4], ri[2][4];
                for (int i = 0; i < 4; i++) {
                    int t;
                    if (!cab(k, 36)) t = 0;
                    else if (!cab(k, 37)) t = 1 + cab(k, 39);
                    else {
                        t = 3;
                        if (cab(k, 38)) {
                            if (cab(k, 39)) { t = 11 + cab(k, 39); goto achou; }
                            t += 4;
                        }
                        t += 2 * cab(k, 39);
                        t += cab(k, 39);
                    }
                achou:
                    sub[i] = t;
                }
                /* diretos primeiro (usam só os vizinhos de fora, como no 16x16) */
                unsigned dirs = 0;
                for (int i = 0; i < 4; i++) if (!sub[i]) dirs |= 0x33u << ((i & 1) * 2 + (i >> 1) * 8);
                mvc_t salvo[2]; salvo[0] = kk[0]; salvo[1] = kk[1];
                if (dirs) {
                    prever_direto(h, c, kk, dirs);
                    for (int b2 = 0; b2 < 16; b2++) if (dirs & (1u << b2)) kk[0].dir[(b2 >> 2) + 1][(b2 & 3) + 1] = 1;
                }
                (void)salvo;
                for (int l = 0; l < 2; l++)
                    for (int i = 0; i < 4; i++) {
                        ri[l][i] = -1;
                        if (!sub[i]) continue;
                        int bx = (i & 1) * 2, by = (i >> 1) * 2;
                        if (!(b_sub[sub[i]][0] & (1 << l))) { mvc_escrever(&kk[l], bx, by, 2, 2, REF_INTRA, 0, 0); continue; }
                        ri[l][i] = h->num_ref[l] > 1 ? cab_ref(k, &kk[l], bx, by) : 0;
                        mvc_escrever(&kk[l], bx, by, 2, 2, ri[l][i], 0, 0);
                    }
                for (int l = 0; l < 2; l++)
                    for (int i = 0; i < 4; i++) if (ri[l][i] >= 0) mvc_escrever(&kk[l], (i & 1) * 2, (i >> 1) * 2, 2, 2, REF_NADA, 0, 0);
                for (int l = 0; l < 2; l++)
                    for (int i = 0; i < 4; i++) {
                        if (ri[l][i] < 0) continue;
                        int bx0 = (i & 1) * 2, by0 = (i >> 1) * 2, f = b_sub[sub[i]][1];
                        int n = f == 0 ? 1 : f == 3 ? 4 : 2;
                        for (int s = 0; s < n; s++) {
                            int bx = bx0, by = by0, w = 2, hh = 2;
                            if (f == 1) { hh = 1; by += s; }
                            else if (f == 2) { w = 1; bx += s; }
                            else if (f == 3) { w = 1; hh = 1; bx += s & 1; by += s >> 1; }
                            cab_mv(k, &kk[l], l, bx, by, w, hh, 0, ri[l][i]);
                        }
                    }
            }
        }
        guardar_movimento(h, m, kk);
        reconstruir_inter(h, c, kk);
    }
    if (skip) {
        m->qp = (int8_t)h->qp;
        m->qpc = (int8_t)qp_croma[clip(h->qp + h->croma_qp_off, 0, 51)];
        qpd_ant = 0;
        return 0;
    }
    /* coded_block_pattern */
    if (modo16 < 0) {
        int ca = c->a ? (c->a->intra == 2 ? 0x2F : c->a->cbp) : 0x0F, cb = c->b ? (c->b->intra == 2 ? 0x2F : c->b->cbp) : 0x0F;
        int l = 0;
        l |= cab(k, 73 + !(ca & 2) + 2 * !(cb & 4));
        l |= cab(k, 73 + !(l & 1) + 2 * !(cb & 8)) << 1;
        l |= cab(k, 73 + !(ca & 8) + 2 * !(l & 1)) << 2;
        l |= cab(k, 73 + !(l & 4) + 2 * !(l & 2)) << 3;
        int cca = c->a ? (c->a->intra == 2 ? 2 : (c->a->cbp >> 4) & 3) : 0, ccb = c->b ? (c->b->intra == 2 ? 2 : (c->b->cbp >> 4) & 3) : 0;
        int cr = 0;
        if (cab(k, 77 + (cca > 0) + 2 * (ccb > 0))) cr = 1 + cab(k, 77 + 4 + (cca == 2) + 2 * (ccb == 2));
        cbp = l | cr << 4;
    }
    m->cbp = (int8_t)cbp; m->modo_croma = (int8_t)modo_croma;
    if (cbp || modo16 >= 0) {
        int v = 0;
        if (cab(k, 60 + qpd_ant)) {
            v = 1;
            int ctx = 62;
            while (cab(k, ctx)) { ctx = 63; v++; if (v > 104) break; }
        }
        int d = (v & 1) ? (v + 1) >> 1 : -(v >> 1);
        h->qp = ((h->qp + d) % 52 + 52) % 52;
        qpd_ant = d != 0;
        m->qpd = (int8_t)(d != 0);
    } else qpd_ant = 0;
    m->qp = (int8_t)h->qp;
    m->qpc = (int8_t)qp_croma[clip(h->qp + h->croma_qp_off, 0, 51)];
    coef_t kf;
    memset(&kf, 0, sizeof kf);
    int t[16];
    if (modo16 >= 0) {
        memset(t, 0, sizeof t);
        int inc = cbf_dc(c->a, 0, true, true) + 2 * cbf_dc(c->b, 0, true, true);
        if (cab_residuo(k, 0, inc, t, 16)) m->dc |= 1;
        for (int i = 0; i < 16; i++) kf.dc[i] = t[i];
        if (cbp & 15)
            for (int i = 0; i < 16; i++) {
                int bx = blk_x[i], by = blk_y[i];
                memset(t, 0, sizeof t);
                int inc2 = cbf_luma(c, bx - 1, by, true) + 2 * cbf_luma(c, bx, by - 1, true);
                m->nz[by * 4 + bx] = (uint8_t)cab_residuo(k, 1, inc2, t, 15);
                for (int j = 0; j < 15; j++) kf.lum[by * 4 + bx][zigzag[j + 1]] = t[j];
            }
    } else {
        for (int i = 0; i < 16; i++) {
            if (!(cbp & (1 << (i >> 2)))) continue;
            int bx = blk_x[i], by = blk_y[i];
            memset(t, 0, sizeof t);
            int inc = cbf_luma(c, bx - 1, by, intra) + 2 * cbf_luma(c, bx, by - 1, intra);
            m->nz[by * 4 + bx] = (uint8_t)cab_residuo(k, 2, inc, t, 16);
            for (int j = 0; j < 16; j++) kf.lum[by * 4 + bx][zigzag[j]] = t[j];
        }
    }
    if (cbp & 0x30) {
        for (int comp = 0; comp < 2; comp++) {
            memset(t, 0, sizeof t);
            int inc = cbf_dc(c->a, 1 + comp, intra, false) + 2 * cbf_dc(c->b, 1 + comp, intra, false);
            if (cab_residuo(k, 3, inc, t, 4)) m->dc |= (uint8_t)(2 << comp);
            for (int i = 0; i < 4; i++) kf.cdc[comp][i] = t[i];
        }
        if (cbp & 0x20)
            for (int comp = 0; comp < 2; comp++)
                for (int i = 0; i < 4; i++) {
                    int bx = i & 1, by = i >> 1;
                    memset(t, 0, sizeof t);
                    int inc = cbf_croma_ac(c, comp, bx - 1, by, intra) + 2 * cbf_croma_ac(c, comp, bx, by - 1, intra);
                    m->nz[16 + comp * 4 + i] = (uint8_t)cab_residuo(k, 4, inc, t, 15);
                    for (int j = 0; j < 15; j++) kf.cro[comp][i][zigzag[j + 1]] = t[j];
                }
    }
    aplicar_residuo(h, c, m, intra, modo16, modo_croma, cbp, &kf);
    return 0;
}
