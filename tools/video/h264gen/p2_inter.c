/* --------------------------------------------- predição de vetor de movimento */

/* Cache 6x5 por lista: linha 0 = acima, coluna 0 = esquerda, 5 = direita. */
typedef struct {
    int8_t  ref[5][6];
    int16_t mv[5][6][2];
    uint8_t mvd[5][6][2];
    uint8_t dir[5][6];
} mvc_t;

static void mvc_preencher(ctx_t *c, mvc_t *k, int l)
{
    for (int r = 0; r < 5; r++)
        for (int q = 0; q < 6; q++) {
            k->ref[r][q] = REF_NADA; k->mv[r][q][0] = k->mv[r][q][1] = 0;
            k->mvd[r][q][0] = k->mvd[r][q][1] = 0; k->dir[r][q] = 0;
        }
    mbinfo_t *ns[4] = { c->a, c->b, c->c, c->d };
    for (int n = 0; n < 4; n++) {
        mbinfo_t *m = ns[n];
        if (!m) continue;
        int qtd = n == 0 || n == 1 ? 4 : 1;
        for (int i = 0; i < qtd; i++) {
            int by, bx, r, q;
            if (n == 0) { by = i; bx = 3; r = i + 1; q = 0; }
            else if (n == 1) { by = 3; bx = i; r = 0; q = i + 1; }
            else if (n == 2) { by = 3; bx = 0; r = 0; q = 5; }
            else { by = 3; bx = 3; r = 0; q = 0; }
            if (m->intra) { k->ref[r][q] = REF_INTRA; continue; }
            int b = by * 4 + bx;
            k->ref[r][q] = m->refidx[l][(by >> 1) * 2 + (bx >> 1)];
            if (k->ref[r][q] < 0) k->ref[r][q] = REF_INTRA;
            k->mv[r][q][0] = m->mv[l][b][0];
            k->mv[r][q][1] = m->mv[l][b][1];
            k->mvd[r][q][0] = m->mvd[l][b][0];
            k->mvd[r][q][1] = m->mvd[l][b][1];
            k->dir[r][q] = m->dir[b];
        }
    }
}

static void mvc_escrever(mvc_t *k, int bx, int by, int w, int hh, int ref, int mx, int my)
{
    for (int y = by; y < by + hh; y++)
        for (int x = bx; x < bx + w; x++) {
            k->ref[y + 1][x + 1] = (int8_t)ref;
            k->mv[y + 1][x + 1][0] = (int16_t)mx;
            k->mv[y + 1][x + 1][1] = (int16_t)my;
        }
}

/* forma: 0 normal, 1 16x8 de cima, 2 16x8 de baixo, 3 8x16 esquerda, 4 8x16 direita */
static void prever_mv(mvc_t *k, int bx, int by, int w, int ref, int forma, int *px, int *py)
{
    int ra = k->ref[by + 1][bx], rb = k->ref[by][bx + 1], rc = k->ref[by][bx + w + 1];
    const int16_t *ma = k->mv[by + 1][bx], *mb = k->mv[by][bx + 1], *mc = k->mv[by][bx + w + 1];
    bool c_ok = rc != REF_NADA;
    if (bx + w >= 4 && by > 0) c_ok = false;
    if (!c_ok) { rc = k->ref[by][bx]; mc = k->mv[by][bx]; }
    if (forma == 1 && rb == ref) { *px = mb[0]; *py = mb[1]; return; }
    if (forma == 2 && ra == ref) { *px = ma[0]; *py = ma[1]; return; }
    if (forma == 3 && ra == ref) { *px = ma[0]; *py = ma[1]; return; }
    if (forma == 4 && rc == ref) { *px = mc[0]; *py = mc[1]; return; }
    if (rb == REF_NADA && rc == REF_NADA && ra != REF_NADA) { *px = ma[0]; *py = ma[1]; return; }
    int iguais = (ra == ref) + (rb == ref) + (rc == ref);
    if (iguais == 1) {
        const int16_t *m = ra == ref ? ma : rb == ref ? mb : mc;
        *px = m[0]; *py = m[1];
        return;
    }
    *px = mediana(ma[0], mb[0], mc[0]);
    *py = mediana(ma[1], mb[1], mc[1]);
}

/* P_Skip */
static void mv_skip_p(mvc_t *k, int *mx, int *my)
{
    int ra = k->ref[1][0], rb = k->ref[0][1];
    *mx = *my = 0;
    if (ra == REF_NADA || rb == REF_NADA || (ra == 0 && !k->mv[1][0][0] && !k->mv[1][0][1]) ||
        (rb == 0 && !k->mv[0][1][0] && !k->mv[0][1][1])) return;
    prever_mv(k, 0, 0, 4, 0, 0, mx, my);
}

/* Predição direta dos blocos 4x4 marcados em "quais" (bit por bloco raster).
 * Escreve ref e mv nos dois caches. */
static void prever_direto(h264_t *h, ctx_t *c, mvc_t *k, unsigned quais)
{
    quadro_t *col = h->lista[1][0];
    mbinfo_t *mc = col && col->mb ? &col->mb[c->idx] : NULL;
    if (h->direto_espacial) {
        int ref[2], mv[2][2];
        for (int l = 0; l < 2; l++) {
            int ra = k[l].ref[1][0], rb = k[l].ref[0][1], rc = k[l].ref[0][5];
            if (rc == REF_NADA) rc = k[l].ref[0][0];
            int r = -1;
            int cand[3] = { ra, rb, rc };
            for (int i = 0; i < 3; i++) if (cand[i] >= 0 && (r < 0 || cand[i] < r)) r = cand[i];
            ref[l] = r;
        }
        bool zero = ref[0] < 0 && ref[1] < 0;
        if (zero) ref[0] = ref[1] = 0;
        for (int l = 0; l < 2; l++) {
            mv[l][0] = mv[l][1] = 0;
            if (!zero && ref[l] >= 0) prever_mv(&k[l], 0, 0, 4, ref[l], 0, &mv[l][0], &mv[l][1]);
        }
        for (int b = 0; b < 16; b++) {
            if (!(quais & (1u << b))) continue;
            int bx = b & 3, by = b >> 2;
            int cx = bx, cy = by;
            if (h->direto_8x8) { cx = (bx >> 1) * 3; cy = (by >> 1) * 3; }
            bool col_zero = false;
            if (mc && !mc->intra && !zero) {
                int b8 = (cy >> 1) * 2 + (cx >> 1), lc = mc->refidx[0][b8] >= 0 ? 0 : 1;
                int rcol = mc->refidx[lc][b8];
                const int16_t *mvc = mc->mv[lc][cy * 4 + cx];
                col_zero = rcol == 0 && mvc[0] >= -1 && mvc[0] <= 1 && mvc[1] >= -1 && mvc[1] <= 1;
            }
            for (int l = 0; l < 2; l++) {
                int mx = mv[l][0], my = mv[l][1];
                if (ref[l] < 0) { mvc_escrever(&k[l], bx, by, 1, 1, REF_INTRA, 0, 0); continue; }
                if (zero || (ref[l] == 0 && col_zero)) mx = my = 0;
                mvc_escrever(&k[l], bx, by, 1, 1, ref[l], mx, my);
            }
        }
        return;
    }
    /* temporal */
    for (int b = 0; b < 16; b++) {
        if (!(quais & (1u << b))) continue;
        int bx = b & 3, by = b >> 2, cx = bx, cy = by;
        if (h->direto_8x8) { cx = (bx >> 1) * 3; cy = (by >> 1) * 3; }
        int r0 = 0, mvc0 = 0, mvc1 = 0;
        if (mc && !mc->intra) {
            int b8 = (cy >> 1) * 2 + (cx >> 1), lc = mc->refidx[0][b8] >= 0 ? 0 : 1;
            int id = mc->ref[lc][b8];
            mvc0 = mc->mv[lc][cy * 4 + cx][0]; mvc1 = mc->mv[lc][cy * 4 + cx][1];
            for (int i = 0; i < h->num_ref[0]; i++) if (h->lista[0][i] && (h->lista[0][i]->id & 127) == id) { r0 = i; break; }
        }
        quadro_t *q0 = h->lista[0][r0];
        int tb = clip(h->poc - (q0 ? q0->poc : 0), -128, 127), td = clip((col ? col->poc : 0) - (q0 ? q0->poc : 0), -128, 127);
        int m0x = mvc0, m0y = mvc1;
        if (td) {
            int tx = (16384 + absi(td / 2)) / td;
            int dsf = clip((tb * tx + 32) >> 6, -1024, 1023);
            m0x = (dsf * mvc0 + 128) >> 8; m0y = (dsf * mvc1 + 128) >> 8;
        }
        mvc_escrever(&k[0], bx, by, 1, 1, r0, m0x, m0y);
        mvc_escrever(&k[1], bx, by, 1, 1, 0, m0x - mvc0, m0y - mvc1);
    }
}

/* ------------------------------------------------ compensação com pesos */

static void pesos_implicitos(h264_t *h, int r0, int r1, int *w0, int *w1)
{
    quadro_t *a = h->lista[0][r0], *b = h->lista[1][r1];
    *w0 = *w1 = 32;
    if (!a || !b) return;
    int tb = clip(h->poc - a->poc, -128, 127), td = clip(b->poc - a->poc, -128, 127);
    if (!td) return;
    int tx = (16384 + absi(td / 2)) / td;
    int dsf = clip((tb * tx + 32) >> 6, -1024, 1023);
    if ((dsf >> 2) < -64 || (dsf >> 2) > 128) return;
    *w0 = 64 - (dsf >> 2); *w1 = dsf >> 2;
}

/* Bloco w x hh em (x, y) do quadro atual: componente 0 luma, 1 Cb, 2 Cr. */
static void combinar(h264_t *h, uint8_t *d, int passo, const uint8_t *p0, const uint8_t *p1, int w, int hh,
                     int comp, int r0, int r1)
{
    bool bi = p0 && p1;
    int modo = 0;   /* 0 média/cópia, 1 explícito, 2 implícito */
    if (h->tipo == 0 && h->ponderada) modo = 1;
    else if (h->tipo == 1 && h->bipred == 1) modo = 1;
    else if (h->tipo == 1 && h->bipred == 2 && bi) modo = 2;
    if (modo == 1 && !(bi ? (h->tem_peso[0][r0] || h->tem_peso[1][r1]) : (p0 ? h->tem_peso[0][r0] : h->tem_peso[1][r1]))) modo = 0;
    if (!modo) {
        for (int j = 0; j < hh; j++)
            for (int i = 0; i < w; i++)
                d[j * passo + i] = bi ? (uint8_t)((p0[j * 16 + i] + p1[j * 16 + i] + 1) >> 1) : (p0 ? p0 : p1)[j * 16 + i];
        return;
    }
    int lw = modo == 2 ? 5 : h->log2_peso[comp ? 1 : 0];
    int w0, w1, o0 = 0, o1 = 0;
    if (modo == 2) pesos_implicitos(h, r0, r1, &w0, &w1);
    else {
        int def = 1 << lw;
        w0 = h->tem_peso[0][r0] ? h->peso[0][r0][comp].w : def; o0 = h->tem_peso[0][r0] ? h->peso[0][r0][comp].o : 0;
        w1 = h->tem_peso[1][r1] ? h->peso[1][r1][comp].w : def; o1 = h->tem_peso[1][r1] ? h->peso[1][r1][comp].o : 0;
    }
    for (int j = 0; j < hh; j++)
        for (int i = 0; i < w; i++) {
            int v;
            if (bi) v = ((p0[j * 16 + i] * w0 + p1[j * 16 + i] * w1 + (1 << lw)) >> (lw + 1)) + ((o0 + o1 + 1) >> 1);
            else {
                int p = p0 ? p0[j * 16 + i] : p1[j * 16 + i], ww = p0 ? w0 : w1, oo = p0 ? o0 : o1;
                v = lw >= 1 ? ((p * ww + (1 << (lw - 1))) >> lw) + oo : p * ww + oo;
            }
            d[j * passo + i] = clip1(v);
        }
}

/* Faz a predição inter do MB inteiro a partir dos caches (refs e mvs das duas listas). */
static void reconstruir_inter(h264_t *h, ctx_t *c, mvc_t *k)
{
    quadro_t *q = h->atual;
    int x0 = c->mbx * 16, y0 = c->mby * 16;
    uint8_t t[2][16 * 16], tc[2][2][8 * 16];
    for (int b8 = 0; b8 < 4; b8++) {
        int bx = (b8 & 1) * 2, by = (b8 >> 1) * 2;
        bool iguais = true;
        for (int l = 0; l < 2; l++)
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++)
                    if (k[l].ref[by + 1 + j][bx + 1 + i] != k[l].ref[by + 1][bx + 1] ||
                        k[l].mv[by + 1 + j][bx + 1 + i][0] != k[l].mv[by + 1][bx + 1][0] ||
                        k[l].mv[by + 1 + j][bx + 1 + i][1] != k[l].mv[by + 1][bx + 1][1]) iguais = false;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++) {
                if (iguais && (i || j)) continue;
                int xx = bx + i, yy = by + j, tam = iguais ? 8 : 4;
                int lx = x0 + xx * 4, ly = y0 + yy * 4, cx = lx / 2, cy = ly / 2, ct = tam / 2;
                int rr[2] = { -1, -1 };
                for (int l = 0; l < 2; l++) {
                    int ri = k[l].ref[yy + 1][xx + 1];
                    if (ri < 0 || ri > MAX_REF) continue;
                    quadro_t *ref = h->lista[l][ri];
                    if (!ref) ref = h->lista[l][0] ? h->lista[l][0] : q;
                    rr[l] = ri;
                    int mx = k[l].mv[yy + 1][xx + 1][0], my = k[l].mv[yy + 1][xx + 1][1];
                    mc_luma(h, ref, t[l], 16, lx, ly, tam, tam, mx, my);
                    mc_croma(h, ref->u, tc[l][0], 16, cx, cy, ct, ct, mx, my);
                    mc_croma(h, ref->v, tc[l][1], 16, cx, cy, ct, ct, mx, my);
                }
                if (rr[0] < 0 && rr[1] < 0) continue;
                combinar(h, q->y + ly * h->passo + lx, h->passo, rr[0] >= 0 ? t[0] : NULL, rr[1] >= 0 ? t[1] : NULL, tam, tam, 0, rr[0] < 0 ? 0 : rr[0], rr[1] < 0 ? 0 : rr[1]);
                for (int cp = 0; cp < 2; cp++)
                    combinar(h, (cp ? q->v : q->u) + cy * h->passo_c + cx, h->passo_c, rr[0] >= 0 ? tc[0][cp] : NULL, rr[1] >= 0 ? tc[1][cp] : NULL,
                             ct, ct, 1 + cp, rr[0] < 0 ? 0 : rr[0], rr[1] < 0 ? 0 : rr[1]);
            }
    }
}

/* Copia refs e mvs dos caches para o MB (para vizinhos, desbloqueio e quadros futuros). */
static void guardar_movimento(h264_t *h, mbinfo_t *m, mvc_t *k)
{
    for (int l = 0; l < 2; l++) {
        for (int i = 0; i < 4; i++) {
            int r = k[l].ref[(i >> 1) * 2 + 1][(i & 1) * 2 + 1];
            if (r < 0) r = -1;
            m->refidx[l][i] = (int8_t)r;
            quadro_t *rq = (r >= 0 && r <= MAX_REF) ? h->lista[l][r] : NULL;
            m->ref[l][i] = (int8_t)(rq ? rq->id & 127 : -1);
        }
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                m->mv[l][y * 4 + x][0] = k[l].ref[y + 1][x + 1] >= 0 ? k[l].mv[y + 1][x + 1][0] : 0;
                m->mv[l][y * 4 + x][1] = k[l].ref[y + 1][x + 1] >= 0 ? k[l].mv[y + 1][x + 1][1] : 0;
                m->mvd[l][y * 4 + x][0] = k[l].mvd[y + 1][x + 1][0];
                m->mvd[l][y * 4 + x][1] = k[l].mvd[y + 1][x + 1][1];
            }
    }
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) m->dir[y * 4 + x] = k[0].dir[y + 1][x + 1];
}
