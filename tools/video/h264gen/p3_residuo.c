/* --------------------------------------------------- resíduo e reconstrução */

static inline uint8_t *py_(h264_t *h, quadro_t *q, int mbx, int mby) { return q->y + mby * 16 * h->passo + mbx * 16; }

typedef struct {
    int lum[16][16];        /* por bloco raster, coeficientes na posição da matriz */
    int dc[16];             /* DC da luma (16x16), na ordem de varredura */
    int cdc[2][4];
    int cro[2][4][16];
} coef_t;

/* Predição intra + desquantização + IDCT. O MB inter já tem a predição no quadro. */
static void aplicar_residuo(h264_t *h, ctx_t *c, mbinfo_t *m, bool intra, int modo16, int modo_croma, int cbp, coef_t *k)
{
    quadro_t *q = h->atual;
    int passo = h->passo, pc = h->passo_c;
    int qp = h->qp, q6 = qp / 6, r6 = qp % 6;
    int qpc = m->qpc;
    int (*lum)[16] = k->lum;
    if (modo16 >= 0) {
        int cm[16], tmp[16];
        for (int i = 0; i < 16; i++) cm[zigzag[i]] = k->dc[i];
        for (int i = 0; i < 4; i++) {
            int *r = &cm[i * 4];
            int a = r[0] + r[1], bq = r[0] - r[1], cc = r[2] + r[3], dd = r[2] - r[3];
            tmp[i * 4 + 0] = a + cc; tmp[i * 4 + 1] = a - cc; tmp[i * 4 + 2] = bq - dd; tmp[i * 4 + 3] = bq + dd;
        }
        for (int i = 0; i < 4; i++) {
            int a = tmp[i] + tmp[4 + i], bq = tmp[i] - tmp[4 + i], cc = tmp[8 + i] + tmp[12 + i], dd = tmp[8 + i] - tmp[12 + i];
            int f[4] = { a + cc, a - cc, bq - dd, bq + dd };
            for (int kk = 0; kk < 4; kk++) {
                int v = f[kk], ls = 16 * dq_v[r6][0];
                v = qp >= 36 ? (v * ls) << (q6 - 6) : (v * ls + (1 << (5 - q6))) >> (6 - q6);
                lum[kk * 4 + i][0] = v;
            }
        }
    }
    if (cbp & 0x30) {
        int c6 = qpc / 6, cr6 = qpc % 6;
        for (int comp = 0; comp < 2; comp++) {
            int a = k->cdc[comp][0], bq = k->cdc[comp][1], cc = k->cdc[comp][2], dd = k->cdc[comp][3];
            int f[4] = { a + bq + cc + dd, a - bq + cc - dd, a + bq - cc - dd, a - bq - cc + dd };
            for (int i = 0; i < 4; i++)
                k->cro[comp][i][0] = (((f[i] * 16 * dq_v[cr6][0]) << c6) >> 5);
        }
        for (int comp = 0; comp < 2; comp++)
            for (int i = 0; i < 4; i++)
                for (int j = 1; j < 16; j++) {
                    if (!k->cro[comp][i][j]) continue;
                    int cl = ((j & 3) & 1) == 0 && ((j >> 2) & 1) == 0 ? 0 : (((j & 3) & 1) && ((j >> 2) & 1)) ? 1 : 2;
                    k->cro[comp][i][j] = ((k->cro[comp][i][j] * dq_v[cr6][cl]) << c6);
                }
    }
    for (int blk = 0; blk < 16; blk++)
        for (int j = modo16 >= 0 ? 1 : 0; j < 16 && m->nz[blk]; j++) {
            if (!lum[blk][j]) continue;
            int cl = ((j & 1) == 0 && ((j >> 2) & 1) == 0) ? 0 : ((j & 1) && ((j >> 2) & 1)) ? 1 : 2;
            lum[blk][j] = ((lum[blk][j] * dq_v[r6][cl]) << q6);
        }
    uint8_t *Y = py_(h, q, c->mbx, c->mby);
    if (intra && modo16 < 0) {
        for (int i = 0; i < 16; i++) {
            int bx = blk_x[i], by = blk_y[i];
            int t[8], l[4], lt;
            bool tt, tl;
            bordas_luma(h, c, bx, by, t, l, &lt, &tt, &tl);
            uint8_t *d = Y + by * 4 * passo + bx * 4;
            pred4x4(d, passo, m->modo4[by * 4 + bx], t, l, lt, tt, tl);
            if (m->nz[by * 4 + bx]) idct4_somar(d, passo, lum[by * 4 + bx]);
        }
    } else {
        if (intra) {
            uint8_t t[16], l[16];
            int lt = 128;
            bool ta = c->b != NULL, la = c->a != NULL;
            for (int i = 0; i < 16; i++) {
                t[i] = ta ? Y[-passo + i] : 128;
                l[i] = la ? Y[i * passo - 1] : 128;
            }
            if (c->d) lt = Y[-passo - 1];
            if (modo16 == 0) for (int j = 0; j < 16; j++) memcpy(Y + j * passo, t, 16);
            else if (modo16 == 1) for (int j = 0; j < 16; j++) memset(Y + j * passo, l[j], 16);
            else if (modo16 == 2) {
                int s = 0, v;
                for (int i = 0; i < 16; i++) s += (ta ? t[i] : 0) + (la ? l[i] : 0);
                v = ta && la ? (s + 16) >> 5 : (ta || la) ? (s + 8) >> 4 : 128;
                for (int j = 0; j < 16; j++) memset(Y + j * passo, v, 16);
            } else pred_plano(Y, passo, 16, t, l, lt);
        }
        for (int i = 0; i < 16; i++) {
            int bx = i & 3, by = i >> 2;
            bool tem = modo16 >= 0 ? (m->nz[i] || lum[i][0]) : m->nz[i] != 0;
            if (tem) idct4_somar(Y + by * 4 * passo + bx * 4, passo, lum[i]);
        }
    }
    for (int comp = 0; comp < 2; comp++) {
        uint8_t *C = (comp ? q->v : q->u) + c->mby * 8 * pc + c->mbx * 8;
        if (intra) {
            uint8_t t[8], l[8];
            int lt = 128;
            bool ta = c->b != NULL, la = c->a != NULL;
            for (int i = 0; i < 8; i++) { t[i] = ta ? C[-pc + i] : 128; l[i] = la ? C[i * pc - 1] : 128; }
            if (c->d) lt = C[-pc - 1];
            if (modo_croma == 0) {
                for (int bl = 0; bl < 4; bl++) {
                    int xo = (bl & 1) * 4, yo = (bl >> 1) * 4, st = 0, sl = 0, v;
                    for (int i = 0; i < 4; i++) { st += t[xo + i]; sl += l[yo + i]; }
                    if ((xo == 0 && yo == 0) || (xo && yo)) v = ta && la ? (st + sl + 4) >> 3 : la ? (sl + 2) >> 2 : ta ? (st + 2) >> 2 : 128;
                    else if (xo) v = ta ? (st + 2) >> 2 : la ? (sl + 2) >> 2 : 128;
                    else v = la ? (sl + 2) >> 2 : ta ? (st + 2) >> 2 : 128;
                    for (int j = 0; j < 4; j++) memset(C + (yo + j) * pc + xo, v, 4);
                }
            } else if (modo_croma == 1) for (int j = 0; j < 8; j++) memset(C + j * pc, l[j], 8);
            else if (modo_croma == 2) for (int j = 0; j < 8; j++) memcpy(C + j * pc, t, 8);
            else pred_plano(C, pc, 8, t, l, lt);
        }
        if (cbp & 0x30)
            for (int i = 0; i < 4; i++)
                idct4_somar(C + (i >> 1) * 4 * pc + (i & 1) * 4, pc, k->cro[comp][i]);
    }
}

static void iniciar_mb(h264_t *h, mbinfo_t *m)
{
    memset(m->nz, 0, sizeof m->nz);
    m->fatia = h->fatia_atual;
    m->intra = m->i4x4 = m->i16 = m->skip = m->direto = 0;
    m->qpd = 0; m->cbp = 0; m->modo_croma = 0; m->dc = 0;
    for (int i = 0; i < 16; i++) { m->modo4[i] = 2; m->dir[i] = 0; }
    memset(m->mvd, 0, sizeof m->mvd);
}

static void sem_movimento(mbinfo_t *m)
{
    for (int l = 0; l < 2; l++) for (int i = 0; i < 4; i++) { m->refidx[l][i] = -1; m->ref[l][i] = -1; }
    memset(m->mv, 0, sizeof m->mv);
}

static void pcm(h264_t *h, ctx_t *c, mbinfo_t *m, bits_t *b)
{
    quadro_t *q = h->atual;
    int passo = h->passo, pc = h->passo_c;
    b->pos = (b->pos + 7) & ~7u;
    uint8_t *y = py_(h, q, c->mbx, c->mby);
    for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) y[j * passo + i] = (uint8_t)ler_bits(b, 8);
    for (int comp = 0; comp < 2; comp++) {
        uint8_t *d = (comp ? q->v : q->u) + c->mby * 8 * pc + c->mbx * 8;
        for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) d[j * pc + i] = (uint8_t)ler_bits(b, 8);
    }
    m->intra = 2;
    memset(m->nz, 16, sizeof m->nz);
    m->qp = 0; m->qpc = (int8_t)qp_croma[clip(h->croma_qp_off, 0, 51)];
    m->cbp = 0x2F; m->dc = 7;
    sem_movimento(m);
}

/* --------------------------------------------------- MB por CAVLC (I e P) */

static int getenv_dbg_mb = -1;
static int decodificar_mb(h264_t *h, bits_t *b, ctx_t *c, int tipo_mb, bool skip)
{
    mbinfo_t *m = c->m;
    iniciar_mb(h, m);
    mvc_t k[2];
    bool inter = !skip && h->tipo == 0 && tipo_mb < 5;
    if (h->tipo == 0 && !skip && tipo_mb >= 5) tipo_mb -= 5;
    bool intra = !skip && !inter;
    int cbp = 0, modo16 = -1, modo_croma = 0;
    if (intra && tipo_mb == 25) { pcm(h, c, m, b); return 0; }
    if (intra) {
        m->intra = 1;
        if (tipo_mb == 0) {
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
                if (ler_bit(b)) modo = pred;
                else { int r = (int)ler_bits(b, 3); modo = r < pred ? r : r + 1; }
                m->modo4[by * 4 + bx] = (int8_t)modo;
            }
        } else {
            m->i16 = 1;
            modo16 = (tipo_mb - 1) % 4;
            cbp = (((tipo_mb - 1) / 4) % 3) << 4;
            if (tipo_mb >= 13) cbp |= 15;
        }
        modo_croma = (int)ue(b);
        sem_movimento(m);
    } else {
        mvc_preencher(c, &k[0], 0);
        mvc_preencher(c, &k[1], 1);
        for (int y = 1; y < 5; y++) for (int x = 1; x < 5; x++) k[1].ref[y][x] = REF_INTRA;
        int nref = h->num_ref[0];
        if (skip) {
            int mx, my;
            m->skip = 1;
            mv_skip_p(&k[0], &mx, &my);
            mvc_escrever(&k[0], 0, 0, 4, 4, 0, mx, my);
        } else if (tipo_mb < 3) {
            int np = tipo_mb == 0 ? 1 : 2;
            int ri[2] = { 0, 0 };
            for (int p = 0; p < np; p++)
                if (nref > 1) ri[p] = nref == 2 ? !ler_bit(b) : (int)ue(b);
            for (int p = 0; p < np; p++) {
                int bx, by, w, hh, forma;
                if (tipo_mb == 0) { bx = 0; by = 0; w = 4; hh = 4; forma = 0; }
                else if (tipo_mb == 1) { bx = 0; by = p * 2; w = 4; hh = 2; forma = 1 + p; }
                else { bx = p * 2; by = 0; w = 2; hh = 4; forma = 3 + p; }
                int mx, my;
                prever_mv(&k[0], bx, by, w, ri[p], forma, &mx, &my);
                mx += se(b); my += se(b);
                mvc_escrever(&k[0], bx, by, w, hh, ri[p], mx, my);
            }
        } else {
            int sub[4], ri[4] = { 0, 0, 0, 0 };
            for (int i = 0; i < 4; i++) sub[i] = (int)ue(b) & 3;
            if (nref > 1 && tipo_mb != 4)
                for (int i = 0; i < 4; i++) ri[i] = nref == 2 ? !ler_bit(b) : (int)ue(b);
            for (int i = 0; i < 4; i++) {
                int bx0 = (i & 1) * 2, by0 = (i >> 1) * 2;
                int n = sub[i] == 0 ? 1 : sub[i] == 3 ? 4 : 2;
                for (int s = 0; s < n; s++) {
                    int bx = bx0, by = by0, w = 2, hh = 2;
                    if (sub[i] == 1) { hh = 1; by += s; }
                    else if (sub[i] == 2) { w = 1; bx += s; }
                    else if (sub[i] == 3) { w = 1; hh = 1; bx += s & 1; by += s >> 1; }
                    int mx, my;
                    prever_mv(&k[0], bx, by, w, ri[i], 0, &mx, &my);
                    mx += se(b); my += se(b);
                    mvc_escrever(&k[0], bx, by, w, hh, ri[i], mx, my);
                }
            }
        }
        guardar_movimento(h, m, k);
        reconstruir_inter(h, c, k);
    }
    if (!skip && modo16 < 0) {
        uint32_t cod = ue(b);
        if (cod > 47) return -1;
        cbp = intra ? cbp_intra[cod] : cbp_inter[cod];
    }
    if (cbp || modo16 >= 0) {
        int d = se(b);
        if (d < -26 || d > 25) return -1;
        h->qp = (h->qp + d + 52) % 52;
    }
    m->qp = (int8_t)h->qp;
    m->qpc = (int8_t)qp_croma[clip(h->qp + h->croma_qp_off, 0, 51)];
    m->cbp = (int8_t)cbp; m->modo_croma = (int8_t)modo_croma;
    if (skip) return 0;
    coef_t kf;
    memset(&kf, 0, sizeof kf);
    if (modo16 >= 0) {
        int16_t dc[16];
        memset(dc, 0, sizeof dc);
        if (residuo(b, nc_luma(c, 0, 0), dc, 0, 16) < 0) return -1;
        for (int i = 0; i < 16; i++) kf.dc[i] = dc[i];
        if (cbp & 15)
            for (int i = 0; i < 16; i++) {
                int bx = blk_x[i], by = blk_y[i];
                int16_t t[16];
                memset(t, 0, sizeof t);
                int n = residuo(b, nc_luma(c, bx, by), t, 0, 15);
                if (n < 0) return -1;
                m->nz[by * 4 + bx] = (uint8_t)n;
                for (int j = 0; j < 15; j++) kf.lum[by * 4 + bx][zigzag[j + 1]] = t[j];
            }
    } else {
        for (int i = 0; i < 16; i++) {
            if (!(cbp & (1 << (i >> 2)))) continue;
            int bx = blk_x[i], by = blk_y[i];
            int16_t t[16];
            memset(t, 0, sizeof t);
            int n = residuo(b, nc_luma(c, bx, by), t, 0, 16);
            if (n < 0) return -1;
            m->nz[by * 4 + bx] = (uint8_t)n;
            for (int j = 0; j < 16; j++) kf.lum[by * 4 + bx][zigzag[j]] = t[j];
        }
    }
    if (cbp & 0x30) {
        for (int comp = 0; comp < 2; comp++) {
            int16_t cdc[4] = { 0, 0, 0, 0 };
            if (residuo(b, -1, cdc, 0, 4) < 0) return -1;
            for (int i = 0; i < 4; i++) kf.cdc[comp][i] = cdc[i];
        }
        if (cbp & 0x20)
            for (int comp = 0; comp < 2; comp++)
                for (int i = 0; i < 4; i++) {
                    int16_t t[16];
                    memset(t, 0, sizeof t);
                    int n = residuo(b, nc_croma(c, comp, i & 1, i >> 1), t, 0, 15);
                    if (n < 0) return -1;
                    m->nz[16 + comp * 4 + i] = (uint8_t)n;
                    for (int j = 0; j < 15; j++) kf.cro[comp][i][zigzag[j + 1]] = t[j];
                }
    }
    aplicar_residuo(h, c, m, intra, modo16, modo_croma, cbp, &kf);
    return 0;
}
