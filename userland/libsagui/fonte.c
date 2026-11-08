/* fonte.c - fontes da web: TrueType (glyf), WOFF (zlib) e WOFF2 (Brotli +
 * transformação de glyf), contornos com composições e rasterizador com
 * antialias por área acumulada (cobertura exata de cada pixel).
 *
 * Sem hinting: o desenho é o contorno escalado, como no macOS. CFF (fontes
 * OpenType com contornos PostScript) fica de fora: fonte_abrir devolve NULL. */
#include <sagui.h>

uint8_t *brotli_descomprimir(const uint8_t *in, unsigned n, unsigned *saida_n, unsigned max);
bool inflar_zlib(const uint8_t *p, unsigned n, uint8_t *s, unsigned max);

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static int s16(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }
static unsigned u32(const uint8_t *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

typedef struct { int16_t x, y; uint8_t on; } fpt;

/* glifo já decodificado (WOFF2) */
typedef struct { uint8_t tipo; uint16_t ncont, npts; uint32_t pts, ends; const uint8_t *comp; } gdec;

#define CACHE_G 512
typedef struct { uint16_t gid; uint16_t px; int16_t l, a, ox, oy; uint8_t *cob; } gcache;

struct fonte {
    uint8_t *blocos[24]; int nblocos;      /* memória a liberar */
    const uint8_t *cmap, *glyf, *loca, *aw;
    unsigned n_cmap, n_glyf, n_loca;
    int cmap_fmt, aw_passo;
    int upem, ngl, nhm, loca_longo, asc, desc;
    gdec *gd;                               /* WOFF2: glifos decodificados */
    fpt *gpts; uint16_t *gends;
    gcache cache[CACHE_G];
    unsigned cache_bytes;
};

static void *guardar(fonte_t *f, unsigned n)
{
    if (f->nblocos >= 24) return NULL;
    uint8_t *p = aloca(n ? n : 1);
    if (p) f->blocos[f->nblocos++] = p;
    return p;
}

void fonte_fechar(fonte_t *f)
{
    if (!f) return;
    for (int i = 0; i < CACHE_G; i++) if (f->cache[i].cob) libera(f->cache[i].cob);
    for (int i = 0; i < f->nblocos; i++) libera(f->blocos[i]);
    libera(f);
}

/* ------------------------------------------------------------ tabelas */

typedef struct { uint32_t tag; const uint8_t *p; unsigned n; int transf; } tabela;
#define TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static const tabela *achar(const tabela *t, int n, uint32_t tag)
{
    for (int i = 0; i < n; i++) if (t[i].tag == tag) return &t[i];
    return NULL;
}

static int ler_cmap(fonte_t *f, const uint8_t *p, unsigned n)
{
    if (n < 4) return 0;
    unsigned nt = u16(p + 2);
    const uint8_t *melhor = NULL; int fmt = 0;
    for (unsigned i = 0; i < nt && 4 + i * 8 + 8 <= n; i++) {
        unsigned plat = u16(p + 4 + i * 8), enc = u16(p + 6 + i * 8), off = u32(p + 8 + i * 8);
        if (off + 4 > n) continue;
        unsigned fm = u16(p + off);
        bool uni = plat == 0 || (plat == 3 && (enc == 1 || enc == 10));
        if (!uni) continue;
        if (fm == 12 && fmt != 12) { melhor = p + off; fmt = 12; }
        else if (fm == 4 && !fmt) { melhor = p + off; fmt = 4; }
    }
    if (!melhor) return 0;
    f->cmap = melhor; f->cmap_fmt = fmt; f->n_cmap = n - (unsigned)(melhor - p);
    return 1;
}

static unsigned gid_de(const fonte_t *f, unsigned cp)
{
    const uint8_t *c = f->cmap;
    if (f->cmap_fmt == 4) {
        if (cp > 0xFFFF) return 0;
        unsigned segx2 = u16(c + 6);
        const uint8_t *fim = c + 14, *ini = fim + segx2 + 2, *delta = ini + segx2, *rng = delta + segx2;
        if ((unsigned)(rng + segx2 - c) > f->n_cmap) return 0;
        for (unsigned i = 0; i < segx2; i += 2) {
            if (cp > u16(fim + i)) continue;
            unsigned s = u16(ini + i);
            if (cp < s) return 0;
            unsigned ro = u16(rng + i);
            if (!ro) return (cp + u16(delta + i)) & 0xFFFF;
            const uint8_t *g = rng + i + ro + (cp - s) * 2;
            if ((unsigned)(g + 2 - c) > f->n_cmap) return 0;
            unsigned v = u16(g);
            return v ? (v + u16(delta + i)) & 0xFFFF : 0;
        }
        return 0;
    }
    unsigned ng = u32(c + 12);
    for (unsigned i = 0; i < ng && 16 + i * 12 + 12 <= f->n_cmap; i++) {
        const uint8_t *g = c + 16 + i * 12;
        if (cp >= u32(g) && cp <= u32(g + 4)) return u32(g + 8) + (cp - u32(g));
    }
    return 0;
}

/* ------------------------------------------------------------ WOFF2 */

static unsigned base128(const uint8_t **p, const uint8_t *fim)
{
    unsigned v = 0;
    for (int i = 0; i < 5 && *p < fim; i++) { uint8_t b = *(*p)++; v = v << 7 | (b & 0x7F); if (!(b & 0x80)) break; }
    return v;
}
static unsigned u255(const uint8_t **p, const uint8_t *fim)
{
    if (*p >= fim) return 0;
    uint8_t c = *(*p)++;
    if (c == 253) { if (*p + 2 > fim) return 0; unsigned v = u16(*p); *p += 2; return v; }
    if (c == 255) { if (*p >= fim) return 0; return 253u + *(*p)++; }
    if (c == 254) { if (*p >= fim) return 0; return 506u + *(*p)++; }
    return c;
}

static const uint32_t woff2_tags[18] = {
    TAG('c','m','a','p'), TAG('h','e','a','d'), TAG('h','h','e','a'), TAG('h','m','t','x'), TAG('m','a','x','p'), TAG('n','a','m','e'),
    TAG('O','S','/','2'), TAG('p','o','s','t'), TAG('c','v','t',' '), TAG('f','p','g','m'), TAG('g','l','y','f'), TAG('l','o','c','a'),
    TAG('p','r','e','p'), TAG('C','F','F',' '), TAG('V','O','R','G'), TAG('E','B','D','T'), TAG('E','B','L','C'), TAG('g','a','s','p'),
};

/* tamanho em bytes de uma composição (formato glyf) e se tem instruções */
static int tam_composto(const uint8_t *p, const uint8_t *fim, int *instr)
{
    const uint8_t *q = p;
    *instr = 0;
    for (;;) {
        if (q + 4 > fim) return -1;
        unsigned fl = u16(q);
        q += 4 + ((fl & 1) ? 4 : 2);
        if (fl & 8) q += 2; else if (fl & 0x40) q += 4; else if (fl & 0x80) q += 8;
        if (fl & 0x100) *instr = 1;
        if (!(fl & 0x20)) break;
    }
    return q > fim ? -1 : (int)(q - p);
}

static int woff2_glyf(fonte_t *f, const uint8_t *d, unsigned n)
{
    if (n < 36) return 0;
    unsigned ng = u16(d + 4);
    unsigned tam[7];
    for (int i = 0; i < 7; i++) tam[i] = u32(d + 8 + i * 4);
    const uint8_t *s[7], *fs[7];
    unsigned off = 36;
    for (int i = 0; i < 7; i++) { if (off + tam[i] > n) return 0; s[i] = d + off; fs[i] = d + off + tam[i]; off += tam[i]; }
    const uint8_t *ncont = s[0], *npts = s[1], *flags = s[2], *gl = s[3], *comp = s[4];
    (void)ncont;
    f->ngl = (int)ng;
    f->gd = guardar(f, ng * (unsigned)sizeof(gdec));
    unsigned tot_pts = tam[2] + 1, tot_cont = 0;
    for (unsigned g = 0; g < ng && s[0] + 2 * g + 2 <= fs[0]; g++) { int c = s16(s[0] + 2 * g); if (c > 0) tot_cont += (unsigned)c; }
    f->gpts = guardar(f, tot_pts * (unsigned)sizeof(fpt));
    f->gends = guardar(f, (tot_cont + 1) * 2);
    if (!f->gd || !f->gpts || !f->gends) return 0;
    unsigned pi = 0, ei = 0;
    for (unsigned g = 0; g < ng; g++) {
        gdec *G = &f->gd[g];
        memset(G, 0, sizeof *G);
        if (s[0] + 2 > fs[0]) return 0;
        int nc = s16(s[0]); s[0] += 2;
        if (nc == 0) continue;
        if (nc < 0) {
            int instr;
            int k = tam_composto(comp, fs[4], &instr);
            if (k < 0) return 0;
            G->tipo = 2; G->comp = comp; comp += k;
            if (instr) { unsigned il = u255(&gl, fs[3]); s[6] += il; }
            continue;
        }
        G->tipo = 1; G->ncont = (uint16_t)nc; G->ends = ei; G->pts = pi;
        unsigned tot = 0;
        for (int c = 0; c < nc; c++) {
            tot += u255(&npts, fs[1]);
            if (ei >= tot_cont) return 0;
            f->gends[ei++] = (uint16_t)(tot - 1);
        }
        if (pi + tot > tot_pts) return 0;
        int x = 0, y = 0;
        for (unsigned i = 0; i < tot; i++) {
            if (flags >= fs[2]) return 0;
            unsigned fl = *flags++, t = fl & 0x7F;
            int dx, dy;
            #define SINAL(f, v) (((f) & 1) ? (v) : -(v))
            if (t < 10) { if (gl + 1 > fs[3]) return 0; dx = 0; dy = SINAL(t, (int)(((t & 14) << 7) + gl[0])); gl += 1; }
            else if (t < 20) { if (gl + 1 > fs[3]) return 0; dx = SINAL(t, (int)((((t - 10) & 14) << 7) + gl[0])); dy = 0; gl += 1; }
            else if (t < 84) { if (gl + 1 > fs[3]) return 0; unsigned b0 = t - 20, b1 = gl[0]; dx = SINAL(t, (int)(1 + (b0 & 0x30) + (b1 >> 4))); dy = SINAL(t >> 1, (int)(1 + ((b0 & 0x0c) << 2) + (b1 & 0x0f))); gl += 1; }
            else if (t < 120) { if (gl + 2 > fs[3]) return 0; unsigned b0 = t - 84; dx = SINAL(t, (int)(1 + ((b0 / 12) << 8) + gl[0])); dy = SINAL(t >> 1, (int)(1 + (((b0 % 12) >> 2) << 8) + gl[1])); gl += 2; }
            else if (t < 124) { if (gl + 3 > fs[3]) return 0; unsigned b2 = gl[1]; dx = SINAL(t, (int)((gl[0] << 4) + (b2 >> 4))); dy = SINAL(t >> 1, (int)(((b2 & 0x0f) << 8) + gl[2])); gl += 3; }
            else { if (gl + 4 > fs[3]) return 0; dx = SINAL(t, (int)((gl[0] << 8) + gl[1])); dy = SINAL(t >> 1, (int)((gl[2] << 8) + gl[3])); gl += 4; }
            #undef SINAL
            x += dx; y += dy;
            fpt *p = &f->gpts[pi + i];
            p->x = (int16_t)x; p->y = (int16_t)y; p->on = !(fl & 0x80);
        }
        G->npts = (uint16_t)tot; pi += tot;
        unsigned il = u255(&gl, fs[3]); s[6] += il;
    }
    return 1;
}

static fonte_t *montar_fonte(fonte_t *f, tabela *t, int nt)
{
    const tabela *head = achar(t, nt, TAG('h','e','a','d')), *hhea = achar(t, nt, TAG('h','h','e','a')), *maxp = achar(t, nt, TAG('m','a','x','p'));
    const tabela *hmtx = achar(t, nt, TAG('h','m','t','x')), *cmap = achar(t, nt, TAG('c','m','a','p')), *glyf = achar(t, nt, TAG('g','l','y','f')), *loca = achar(t, nt, TAG('l','o','c','a'));
    if (!head || head->n < 54 || !hhea || hhea->n < 36 || !maxp || maxp->n < 6 || !hmtx || !cmap || !glyf) goto ruim;
    f->upem = (int)u16(head->p + 18);
    f->loca_longo = s16(head->p + 50);
    f->ngl = (int)u16(maxp->p + 4);
    f->asc = s16(hhea->p + 4); f->desc = s16(hhea->p + 6);
    f->nhm = (int)u16(hhea->p + 34);
    if (f->upem < 16 || f->nhm < 1) goto ruim;
    if (!ler_cmap(f, cmap->p, cmap->n)) goto ruim;
    if (hmtx->transf) { f->aw = hmtx->p + 1; f->aw_passo = 2; if (hmtx->n < 1 + (unsigned)f->nhm * 2) goto ruim; }
    else { f->aw = hmtx->p; f->aw_passo = 4; if (hmtx->n < (unsigned)f->nhm * 4) goto ruim; }
    if (glyf->transf) { if (!woff2_glyf(f, glyf->p, glyf->n)) goto ruim; }
    else {
        if (!loca) goto ruim;
        f->glyf = glyf->p; f->n_glyf = glyf->n; f->loca = loca->p; f->n_loca = loca->n;
    }
    return f;
ruim:
    fonte_fechar(f);
    return NULL;
}

fonte_t *fonte_abrir(const uint8_t *d, unsigned n)
{
    if (n < 12) return NULL;
    fonte_t *f = aloca(sizeof(fonte_t));
    if (!f) return NULL;
    memset(f, 0, sizeof *f);
    tabela t[64]; int nt = 0;
    uint32_t sig = u32(d);
    if (sig == 0x00010000 || sig == TAG('t','r','u','e')) {
        uint8_t *c = guardar(f, n);
        if (!c) { fonte_fechar(f); return NULL; }
        memcpy(c, d, n);
        unsigned k = u16(c + 4);
        for (unsigned i = 0; i < k && nt < 64 && 12 + i * 16 + 16 <= n; i++) {
            const uint8_t *e = c + 12 + i * 16;
            unsigned off = u32(e + 8), len = u32(e + 12);
            if (off > n || len > n - off) continue;
            t[nt].tag = u32(e); t[nt].p = c + off; t[nt].n = len; t[nt].transf = 0; nt++;
        }
        return montar_fonte(f, t, nt);
    }
    if (sig == TAG('w','O','F','F')) {
        if (n < 44 || u32(d + 4) == TAG('O','T','T','O')) { fonte_fechar(f); return NULL; }
        unsigned k = u16(d + 12);
        for (unsigned i = 0; i < k && nt < 64 && 44 + i * 20 + 20 <= n; i++) {
            const uint8_t *e = d + 44 + i * 20;
            uint32_t tag = u32(e);
            unsigned off = u32(e + 4), clen = u32(e + 8), olen = u32(e + 12);
            if (off > n || clen > n - off || olen > (8u << 20)) continue;
            bool util = tag == TAG('c','m','a','p') || tag == TAG('h','e','a','d') || tag == TAG('h','h','e','a') || tag == TAG('h','m','t','x') ||
                        tag == TAG('m','a','x','p') || tag == TAG('g','l','y','f') || tag == TAG('l','o','c','a');
            if (!util) continue;
            uint8_t *c = guardar(f, olen);
            if (!c) { fonte_fechar(f); return NULL; }
            if (clen < olen) { if (clen < 2 || !inflar_zlib(d + off + 2, clen - 2, c, olen)) { fonte_fechar(f); return NULL; } }
            else memcpy(c, d + off, olen);
            t[nt].tag = tag; t[nt].p = c; t[nt].n = olen; t[nt].transf = 0; nt++;
        }
        return montar_fonte(f, t, nt);
    }
    if (sig == TAG('w','O','F','2')) {
        if (n < 48 || u32(d + 4) == TAG('O','T','T','O') || u32(d + 4) == TAG('t','t','c','f')) { fonte_fechar(f); return NULL; }
        unsigned k = u16(d + 12), comp = u32(d + 20);
        const uint8_t *p = d + 48, *fim = d + n;
        unsigned soma = 0;
        struct { uint32_t tag; unsigned tam; int transf; } dir[64];
        for (unsigned i = 0; i < k && i < 64; i++) {
            if (p >= fim) { fonte_fechar(f); return NULL; }
            uint8_t fl = *p++;
            uint32_t tag;
            if ((fl & 63) == 63) { if (p + 4 > fim) { fonte_fechar(f); return NULL; } tag = u32(p); p += 4; }
            else tag = (fl & 63) < 18 ? woff2_tags[fl & 63] : TAG('?', '?', '?', (unsigned)(fl & 63));
            unsigned ver = fl >> 6, olen = base128(&p, fim), tlen = olen;
            bool gl = tag == TAG('g','l','y','f') || tag == TAG('l','o','c','a');
            int transf = gl ? ver == 0 : ver != 0;
            if (transf) tlen = base128(&p, fim);
            dir[i].tag = tag; dir[i].tam = tlen; dir[i].transf = transf;
            soma += tlen;
        }
        if (k > 64 || p + comp > fim) { fonte_fechar(f); return NULL; }
        unsigned dn = 0;
        uint8_t *dd = brotli_descomprimir(p, comp, &dn, soma + 16);
        if (!dd) { fonte_fechar(f); return NULL; }
        f->blocos[f->nblocos++] = dd;
        unsigned off = 0;
        for (unsigned i = 0; i < k; i++) {
            if (off + dir[i].tam > dn) { fonte_fechar(f); return NULL; }
            t[nt].tag = dir[i].tag; t[nt].p = dd + off; t[nt].n = dir[i].tam; t[nt].transf = dir[i].transf; nt++;
            off += dir[i].tam;
        }
        return montar_fonte(f, t, nt);
    }
    fonte_fechar(f);
    return NULL;
}

/* ------------------------------------------------------------ contornos */

#define MAXP 4096
typedef struct { fpt *p; uint16_t *ends; int np, nc; } contorno;

/* transformação afim em 1/16384 (F2Dot14) + deslocamento */
typedef struct { int a, b, c, d, dx, dy; } afim;

static void por_ponto(contorno *k, int x, int y, int on, const afim *m)
{
    if (k->np >= MAXP) return;
    fpt *q = &k->p[k->np++];
    q->x = (int16_t)(((long long)x * m->a + (long long)y * m->c) / 16384 + m->dx);
    q->y = (int16_t)(((long long)x * m->b + (long long)y * m->d) / 16384 + m->dy);
    q->on = (uint8_t)on;
}

static void glifo_contorno(const fonte_t *f, unsigned gid, contorno *k, const afim *m, int prof)
{
    if (gid >= (unsigned)f->ngl || prof > 6) return;
    const uint8_t *comp = NULL, *cfim = NULL;
    if (f->gd) {
        const gdec *G = &f->gd[gid];
        if (G->tipo == 1) {
            int base = k->np;
            for (unsigned i = 0; i < G->npts; i++) { const fpt *p = &f->gpts[G->pts + i]; por_ponto(k, p->x, p->y, p->on, m); }
            for (unsigned c = 0; c < G->ncont && k->nc < MAXP; c++) k->ends[k->nc++] = (uint16_t)(base + f->gends[G->ends + c]);
            return;
        }
        if (G->tipo != 2) return;
        comp = G->comp; cfim = comp + 65536;
    } else {
        unsigned o0, o1;
        if (f->loca_longo) { if ((gid + 2) * 4 > f->n_loca) return; o0 = u32(f->loca + gid * 4); o1 = u32(f->loca + gid * 4 + 4); }
        else { if ((gid + 2) * 2 > f->n_loca) return; o0 = u16(f->loca + gid * 2) * 2; o1 = u16(f->loca + gid * 2 + 2) * 2; }
        if (o1 <= o0 || o1 > f->n_glyf || o1 - o0 < 10) return;
        const uint8_t *g = f->glyf + o0, *gfim = f->glyf + o1;
        int nc = s16(g);
        if (nc < 0) { comp = g + 10; cfim = gfim; }
        else {
            const uint8_t *e = g + 10;
            if (e + nc * 2 + 2 > gfim) return;
            int np = (int)u16(e + (nc - 1) * 2) + 1;
            int base = k->np;
            for (int c = 0; c < nc && k->nc < MAXP; c++) k->ends[k->nc++] = (uint16_t)(base + (int)u16(e + c * 2));
            const uint8_t *q = e + nc * 2;
            q += 2 + u16(q);   /* instruções */
            /* flags */
            static uint8_t fl[MAXP];
            if (np > MAXP || k->np + np > MAXP) return;
            for (int i = 0; i < np;) {
                if (q >= gfim) return;
                uint8_t v = *q++;
                fl[i++] = v;
                if (v & 8) { if (q >= gfim) return; int r = *q++; while (r-- && i < np) fl[i++] = v; }
            }
            int x = 0;
            const uint8_t *qx = q;
            /* x */
            static int16_t vx[MAXP];
            for (int i = 0; i < np; i++) {
                if (fl[i] & 2) { if (qx >= gfim) return; int v = *qx++; x += (fl[i] & 16) ? v : -v; }
                else if (!(fl[i] & 16)) { if (qx + 2 > gfim) return; x += s16(qx); qx += 2; }
                vx[i] = (int16_t)x;
            }
            int y = 0;
            for (int i = 0; i < np; i++) {
                if (fl[i] & 4) { if (qx >= gfim) return; int v = *qx++; y += (fl[i] & 32) ? v : -v; }
                else if (!(fl[i] & 32)) { if (qx + 2 > gfim) return; y += s16(qx); qx += 2; }
                por_ponto(k, vx[i], y, fl[i] & 1, m);
            }
            return;
        }
    }
    /* composição */
    const uint8_t *q = comp;
    for (;;) {
        if (q + 4 > cfim) return;
        unsigned fl = u16(q), sub = u16(q + 2);
        q += 4;
        int a1, a2;
        if (fl & 1) { a1 = s16(q); a2 = s16(q + 2); q += 4; } else { a1 = (int8_t)q[0]; a2 = (int8_t)q[1]; q += 2; }
        afim t = { 16384, 0, 0, 16384, 0, 0 };
        if (fl & 8) { t.a = t.d = s16(q); q += 2; }
        else if (fl & 0x40) { t.a = s16(q); t.d = s16(q + 2); q += 4; }
        else if (fl & 0x80) { t.a = s16(q); t.b = s16(q + 2); t.c = s16(q + 4); t.d = s16(q + 6); q += 8; }
        if (fl & 2) { t.dx = a1; t.dy = a2; }
        /* compõe com a transformação de fora */
        afim r;
        r.a = (int)(((long long)t.a * m->a + (long long)t.b * m->c) / 16384);
        r.b = (int)(((long long)t.a * m->b + (long long)t.b * m->d) / 16384);
        r.c = (int)(((long long)t.c * m->a + (long long)t.d * m->c) / 16384);
        r.d = (int)(((long long)t.c * m->b + (long long)t.d * m->d) / 16384);
        r.dx = (int)(((long long)t.dx * m->a + (long long)t.dy * m->c) / 16384) + m->dx;
        r.dy = (int)(((long long)t.dx * m->b + (long long)t.dy * m->d) / 16384) + m->dy;
        glifo_contorno(f, sub, k, &r, prof + 1);
        if (!(fl & 0x20)) break;
    }
}

/* ------------------------------------------------------------ rasterizador */

static inline float fpiso(float v) { int i = (int)v; return (float)(v < (float)i ? i - 1 : i); }
static inline float fteto(float v) { int i = (int)v; return (float)(v > (float)i ? i + 1 : i); }
static inline float fabs_f(float v) { return v < 0 ? -v : v; }

typedef struct { float *a; int w, h; } acum;

static void linha(acum *r, float x0, float y0, float x1, float y1)
{
    if (fabs_f(y0 - y1) < 1e-6f) return;
    float dir = 1.0f;
    if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1.0f; }
    float dxdy = (x1 - x0) / (y1 - y0);
    float x = x0;
    int ya = y0 < 0 ? 0 : (int)y0;
    if (y0 < 0) x -= y0 * dxdy;
    int yb = (int)fteto(y1); if (yb > r->h) yb = r->h;
    for (int y = ya; y < yb; y++) {
        float *l = r->a + y * r->w;
        float lo = (float)y > y0 ? (float)y : y0, hi = (float)(y + 1) < y1 ? (float)(y + 1) : y1;
        float dy = hi - lo;
        float xn = x + dxdy * dy, d = dy * dir;
        float xa = x < xn ? x : xn, xb = x < xn ? xn : x;
        if (xa < 0) xa = 0;
        if (xb < 0) xb = 0;
        float xaf = fpiso(xa); int xai = (int)xaf;
        float xbc = fteto(xb); int xbi = (int)xbc;
        if (xai < 0 || xbi >= r->w) { x = xn; continue; }
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

static void quad(acum *r, float x0, float y0, float x1, float y1, float x2, float y2)
{
    float ddx = x0 - 2 * x1 + x2, ddy = y0 - 2 * y1 + y2;
    float dd = fabs_f(ddx) + fabs_f(ddy);
    int n = 1 + (int)(dd * 0.7f);
    if (n > 16) n = 16;
    float px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / (float)n, u = 1 - t;
        float qx = u * u * x0 + 2 * u * t * x1 + t * t * x2, qy = u * u * y0 + 2 * u * t * y1 + t * t * y2;
        linha(r, px, py, qx, qy);
        px = qx; py = qy;
    }
}

static int px_cache(int px) { return px > 200 ? 200 : px; }

const uint8_t *fonte_glifo(fonte_t *f, unsigned cp, int px, int *l, int *a, int *ox, int *oy)
{
    px = px_cache(px);
    unsigned gid = gid_de(f, cp);
    unsigned h = (gid * 31u + (unsigned)px * 7u) % CACHE_G;
    gcache *c = &f->cache[h];
    if (c->cob && c->gid == gid && c->px == px) { *l = c->l; *a = c->a; *ox = c->ox; *oy = c->oy; return c->cob; }
    static fpt pts[MAXP]; static uint16_t ends[MAXP];
    contorno k = { pts, ends, 0, 0 };
    afim id = { 16384, 0, 0, 16384, 0, 0 };
    glifo_contorno(f, gid, &k, &id, 0);
    if (!k.np) return NULL;
    float esc = (float)px / (float)f->upem;
    int xmin = 32767, xmax = -32768, ymin = 32767, ymax = -32768;
    for (int i = 0; i < k.np; i++) {
        if (pts[i].x < xmin) xmin = pts[i].x;
        if (pts[i].x > xmax) xmax = pts[i].x;
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }
    int bx0 = (int)fpiso((float)xmin * esc) - 1, bx1 = (int)fteto((float)xmax * esc) + 1;
    int by0 = (int)fpiso((float)-ymax * esc) - 1, by1 = (int)fteto((float)-ymin * esc) + 1;
    int w = bx1 - bx0 + 2, hh = by1 - by0;
    if (w <= 0 || hh <= 0 || w > 600 || hh > 600) return NULL;
    acum r = { aloca((unsigned)(w * hh + 4) * sizeof(float)), w, hh };
    if (!r.a) return NULL;
    memset(r.a, 0, (unsigned)(w * hh + 4) * sizeof(float));
    int ini = 0;
    for (int c2 = 0; c2 < k.nc; c2++) {
        int fim = ends[c2];
        if (fim < ini || fim >= k.np) break;
        int n = fim - ini + 1;
        /* ponto de partida: um ponto sobre a curva (ou o meio de dois de controle) */
        float sx, sy; int i0 = -1;
        for (int i = 0; i < n; i++) if (pts[ini + i].on) { i0 = i; break; }
        #define PX(p) ((float)(p).x * esc - (float)bx0)
        #define PY(p) ((float)-(p).y * esc - (float)by0)
        if (i0 < 0) { sx = (PX(pts[ini]) + PX(pts[ini + (1 % n)])) / 2; sy = (PY(pts[ini]) + PY(pts[ini + (1 % n)])) / 2; i0 = 0; }
        else { sx = PX(pts[ini + i0]); sy = PY(pts[ini + i0]); }
        float cx = sx, cy = sy, qx = 0, qy = 0; int tem_q = 0;
        for (int j = 1; j <= n; j++) {
            const fpt *p = &pts[ini + (i0 + j) % n];
            float x = PX(*p), y = PY(*p);
            if (p->on) {
                if (tem_q) quad(&r, cx, cy, qx, qy, x, y); else linha(&r, cx, cy, x, y);
                cx = x; cy = y; tem_q = 0;
            } else {
                if (tem_q) { float mx = (qx + x) / 2, my = (qy + y) / 2; quad(&r, cx, cy, qx, qy, mx, my); cx = mx; cy = my; }
                qx = x; qy = y; tem_q = 1;
            }
        }
        if (tem_q) quad(&r, cx, cy, qx, qy, sx, sy); else linha(&r, cx, cy, sx, sy);
        #undef PX
        #undef PY
        ini = fim + 1;
    }
    uint8_t *cob = aloca((unsigned)(w * hh));
    if (!cob) { libera(r.a); return NULL; }
    for (int y = 0; y < hh; y++) {
        float s = 0;
        for (int x = 0; x < w; x++) {
            s += r.a[y * w + x];
            float v = fabs_f(s);
            if (v > 1) v = 1;
            cob[y * w + x] = (uint8_t)(v * 255.0f + 0.5f);
        }
    }
    libera(r.a);
    if (c->cob) { f->cache_bytes -= (unsigned)(c->l * c->a); libera(c->cob); }
    c->cob = cob; c->gid = (uint16_t)gid; c->px = (uint16_t)px; c->l = (int16_t)w; c->a = (int16_t)hh; c->ox = (int16_t)bx0; c->oy = (int16_t)by0;
    f->cache_bytes += (unsigned)(w * hh);
    *l = w; *a = hh; *ox = bx0; *oy = by0;
    return cob;
}

/* avanço em 1/64 de pixel */
int fonte_avanco(fonte_t *f, unsigned cp, int px)
{
    unsigned gid = gid_de(f, cp);
    unsigned i = gid < (unsigned)f->nhm ? gid : (unsigned)f->nhm - 1;
    unsigned aw = u16(f->aw + i * (unsigned)f->aw_passo);
    if (px > 500) px = 500;
    return (int)(aw * (unsigned)px * 64u / (unsigned)f->upem);   /* sem divisão de 64 bits (não há libgcc) */
}
bool fonte_tem(fonte_t *f, unsigned cp) { return gid_de(f, cp) != 0; }
int fonte_ascendente(fonte_t *f, int px) { return f->asc * px / f->upem; }
int fonte_descendente(fonte_t *f, int px) { return -f->desc * px / f->upem; }
