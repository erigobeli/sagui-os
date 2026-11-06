/* imagem.c - decodifica PNG e JPEG para pixels 0x00RRGGBB.
 *
 * PNG (RFC 2083 e 1951): todos os tipos de cor e profundidades, sem
 * entrelaçamento. Transparência vira mistura com branco.
 * JPEG (ITU T.81): sequencial de Huffman, 8 bits, cinza ou YCbCr. JPEG
 * progressivo também é aceito.
 * Tudo com inteiros, porque os programas não usam a FPU. */
#include "sagui.h"

#define MAX_PIXELS (4096u * 1024u)

/* ------------------------------------------------------------ inflate */

typedef struct { const uint8_t *p; unsigned n, pos; uint32_t bits; int qtd; bool erro; } leitor_t;
typedef struct { uint16_t cont[16], sim[320]; } huff_t;

static uint32_t pegar(leitor_t *l, int n)
{
    while (l->qtd < n) {
        if (l->pos >= l->n) { l->erro = true; return 0; }
        l->bits |= (uint32_t)l->p[l->pos++] << l->qtd;
        l->qtd += 8;
    }
    uint32_t v = l->bits & ((1u << n) - 1);
    if (n == 0) v = 0;
    l->bits >>= n;
    l->qtd -= n;
    return v;
}

static void montar_huff(huff_t *h, const uint8_t *tam, int n)
{
    uint16_t desl[16];
    memset(h->cont, 0, sizeof h->cont);
    for (int i = 0; i < n; i++) h->cont[tam[i]]++;
    h->cont[0] = 0;
    desl[1] = 0;
    for (int i = 1; i < 15; i++) desl[i + 1] = (uint16_t)(desl[i] + h->cont[i]);
    for (int i = 0; i < n; i++) if (tam[i]) h->sim[desl[tam[i]]++] = (uint16_t)i;
}

static int simbolo(leitor_t *l, const huff_t *h)
{
    int cod = 0, prim = 0, ind = 0;
    for (int b = 1; b <= 15; b++) {
        cod |= (int)pegar(l, 1);
        if (l->erro) return -1;
        int c = h->cont[b];
        if (cod - c < prim) return h->sim[ind + cod - prim];
        ind += c;
        prim = (prim + c) << 1;
        cod <<= 1;
    }
    return -1;
}

static bool blocos(leitor_t *l, const huff_t *hl, const huff_t *hd, uint8_t *s, unsigned max, unsigned *o)
{
    static const uint16_t base_c[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const uint8_t extra_c[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const uint16_t base_d[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const uint8_t extra_d[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    for (;;) {
        int c = simbolo(l, hl);
        if (c < 0) return false;
        if (c < 256) { if (*o >= max) return false; s[(*o)++] = (uint8_t)c; continue; }
        if (c == 256) return true;
        c -= 257;
        if (c >= 29) return false;
        unsigned tam = base_c[c] + pegar(l, extra_c[c]);
        int d = simbolo(l, hd);
        if (d < 0 || d >= 30) return false;
        unsigned dist = base_d[d] + pegar(l, extra_d[d]);
        if (dist > *o || *o + tam > max) return false;
        for (unsigned i = 0; i < tam; i++, (*o)++) s[*o] = s[*o - dist];
    }
}

static bool inflar(const uint8_t *p, unsigned n, uint8_t *s, unsigned max)
{
    static huff_t hl, hd;
    leitor_t l = { p, n, 0, 0, 0, false };
    unsigned o = 0;
    bool ultimo;
    do {
        ultimo = pegar(&l, 1);
        unsigned tipo = pegar(&l, 2);
        if (l.erro) return false;
        if (tipo == 0) {
            l.bits = 0; l.qtd = 0;
            if (l.pos + 4 > n) return false;
            unsigned t = (unsigned)p[l.pos] | (unsigned)p[l.pos + 1] << 8;
            l.pos += 4;
            if (l.pos + t > n || o + t > max) return false;
            memcpy(s + o, p + l.pos, t);
            l.pos += t; o += t;
        } else if (tipo == 1 || tipo == 2) {
            uint8_t tam[320];
            if (tipo == 1) {
                int i = 0;
                for (; i < 144; i++) tam[i] = 8;
                for (; i < 256; i++) tam[i] = 9;
                for (; i < 280; i++) tam[i] = 7;
                for (; i < 288; i++) tam[i] = 8;
                montar_huff(&hl, tam, 288);
                for (i = 0; i < 30; i++) tam[i] = 5;
                montar_huff(&hd, tam, 30);
            } else {
                static const uint8_t ordem[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                int nl = (int)pegar(&l, 5) + 257, nd = (int)pegar(&l, 5) + 1, nc = (int)pegar(&l, 4) + 4;
                memset(tam, 0, 19);
                for (int i = 0; i < nc; i++) tam[ordem[i]] = (uint8_t)pegar(&l, 3);
                montar_huff(&hl, tam, 19);
                int i = 0;
                while (i < nl + nd) {
                    int c = simbolo(&l, &hl);
                    if (c < 0) return false;
                    if (c < 16) { tam[i++] = (uint8_t)c; continue; }
                    int ant = 0, rep;
                    if (c == 16) { if (!i) return false; ant = tam[i - 1]; rep = 3 + (int)pegar(&l, 2); }
                    else if (c == 17) rep = 3 + (int)pegar(&l, 3);
                    else rep = 11 + (int)pegar(&l, 7);
                    if (i + rep > nl + nd) return false;
                    while (rep--) tam[i++] = (uint8_t)ant;
                }
                uint8_t tam_d[32];
                memcpy(tam_d, tam + nl, (size_t)nd);
                montar_huff(&hl, tam, nl);
                montar_huff(&hd, tam_d, nd);
            }
            if (!blocos(&l, &hl, &hd, s, max, &o)) return false;
        } else return false;
    } while (!ultimo);
    return o == max;
}

/* ------------------------------------------------------------ PNG */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint32_t sobre_branco(int r, int g, int b, int a)
{
    r = (r * a + 255 * (255 - a)) / 255;
    g = (g * a + 255 * (255 - a)) / 255;
    b = (b * a + 255 * (255 - a)) / 255;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

static int absoluto(int v) { return v < 0 ? -v : v; }

static bool png(const uint8_t *d, unsigned n, uint32_t **saida, int *pl, int *pa)
{
    unsigned k = 8, nz = 0, npal = 0, ntr = 0;
    uint32_t l = 0, a = 0;
    int prof = 0, cor = 0;
    uint8_t pal[768], tr[256];
    uint8_t *z = aloca(n);
    if (!z) return false;
    bool ok = false;
    uint8_t *cru = NULL;
    while (k + 12 <= n) {
        uint32_t t = be32(d + k);
        const uint8_t *c = d + k + 8;
        if (t > n - k - 12) break;
        if (!memcmp(d + k + 4, "IHDR", 4)) {
            l = be32(c); a = be32(c + 4); prof = c[8]; cor = c[9];
            if (c[12]) { registrar("imagem: PNG entrelacado nao suportado\n"); goto fim; }
        } else if (!memcmp(d + k + 4, "PLTE", 4)) { npal = t > 768 ? 768 : t; memcpy(pal, c, npal); }
        else if (!memcmp(d + k + 4, "tRNS", 4)) { ntr = t > 256 ? 256 : t; memcpy(tr, c, ntr); }
        else if (!memcmp(d + k + 4, "IDAT", 4)) { memcpy(z + nz, c, t); nz += t; }
        else if (!memcmp(d + k + 4, "IEND", 4)) break;
        k += t + 12;
    }
    if (!l || !a || l > 8192 || a > 8192 || l * a > MAX_PIXELS || nz < 6) goto fim;
    int canais = cor == 2 ? 3 : cor == 4 ? 2 : cor == 6 ? 4 : 1;
    unsigned linha = (l * (unsigned)canais * (unsigned)prof + 7) / 8, bpp = (unsigned)canais * (unsigned)prof / 8;
    if (!bpp) bpp = 1;
    cru = aloca((linha + 1) * a);
    uint32_t *px = aloca(l * a * 4);
    if (!cru || !px) { libera(px); goto fim; }
    if (!inflar(z + 2, nz - 2, cru, (linha + 1) * a)) { libera(px); goto fim; }
    for (uint32_t y = 0; y < a; y++) {
        uint8_t *r = cru + y * (linha + 1), f = *r++, *ac = y ? r - linha - 1 : NULL;
        for (unsigned i = 0; i < linha; i++) {
            int e = i >= bpp ? r[i - bpp] : 0, c = ac ? ac[i] : 0, ec = ac && i >= bpp ? ac[i - bpp] : 0, v = 0;
            if (f == 1) v = e;
            else if (f == 2) v = c;
            else if (f == 3) v = (e + c) / 2;
            else if (f == 4) {
                int p = e + c - ec, pe = absoluto(p - e), pc = absoluto(p - c), pec = absoluto(p - ec);
                v = pe <= pc && pe <= pec ? e : pc <= pec ? c : ec;
            }
            r[i] = (uint8_t)(r[i] + v);
        }
        for (uint32_t x = 0; x < l; x++) {
            int v[4];
            for (int c = 0; c < canais; c++) {
                unsigned bit = (x * (unsigned)canais + (unsigned)c) * (unsigned)prof;
                if (prof == 16) v[c] = r[bit / 8];
                else if (prof == 8) v[c] = r[bit / 8];
                else v[c] = (r[bit / 8] >> (8 - prof - (int)(bit & 7))) & ((1 << prof) - 1);
            }
            uint32_t s;
            if (cor == 3) {
                unsigned i = (unsigned)v[0];
                s = i * 3 + 2 < npal ? sobre_branco(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2], i < ntr ? tr[i] : 255) : 0;
            } else if (cor == 2) s = sobre_branco(v[0], v[1], v[2], 255);
            else if (cor == 6) s = sobre_branco(v[0], v[1], v[2], v[3]);
            else {
                int g = prof < 8 ? v[0] * 255 / ((1 << prof) - 1) : v[0];
                s = sobre_branco(g, g, g, cor == 4 ? v[1] : 255);
            }
            px[y * l + x] = s;
        }
    }
    *saida = px; *pl = (int)l; *pa = (int)a;
    ok = true;
fim:
    libera(z);
    libera(cru);
    return ok;
}

/* ------------------------------------------------------------ JPEG */

static const uint8_t ziguezague[64] = {
    0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };

/* cos((2x+1)u pi/16) c(u)/2, vezes 1024. */
static const int16_t base_cos[8][8] = {
    {362, 502, 473, 426, 362, 284, 196, 100},
    {362, 426, 196, -100, -362, -502, -473, -284},
    {362, 284, -196, -502, -362, 100, 473, 426},
    {362, 100, -473, -284, 362, 426, -196, -502},
    {362, -100, -473, 284, 362, -426, -196, 502},
    {362, -284, -196, 502, -362, -100, 473, -426},
    {362, -426, 196, 100, -362, 502, -473, 284},
    {362, -502, 473, -426, 362, -284, 196, -100},
};

typedef struct { uint8_t cont[17], val[256]; bool tem; } jhuff_t;
typedef struct { int id, h, v, q, dc, ac, pred, passo, bl, ba; uint8_t *plano; int16_t *coef; } comp_t;
typedef struct { const uint8_t *p; unsigned n, pos; uint32_t bits; int qtd; bool fim; } jbits_t;

static int jbit(jbits_t *b)
{
    if (!b->qtd) {
        unsigned v = 0;
        if (b->pos < b->n && !b->fim) {
            v = b->p[b->pos];
            if (v == 0xFF) {
                unsigned s = b->pos + 1 < b->n ? b->p[b->pos + 1] : 0xD9;
                if (s == 0) b->pos += 2;
                else { b->fim = true; v = 0; }   /* marcador: completa com zeros */
            } else b->pos++;
        }
        b->bits = v;
        b->qtd = 8;
    }
    b->qtd--;
    return (int)(b->bits >> b->qtd) & 1;
}

static int jbits(jbits_t *b, int n) { int v = 0; while (n--) v = v << 1 | jbit(b); return v; }

static int jsimbolo(jbits_t *b, const jhuff_t *h)
{
    int cod = 0, prim = 0, ind = 0;
    for (int t = 1; t <= 16; t++) {
        cod = cod << 1 | jbit(b);
        int c = h->cont[t];
        if (cod - prim < c) return h->val[ind + cod - prim];
        ind += c;
        prim = (prim + c) << 1;
    }
    return -1;
}

static int estender(int v, int t) { return t && v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }

static void idct(const int *f, uint8_t *s, int passo)
{
    int t[64];
    for (int v = 0; v < 8; v++)
        for (int x = 0; x < 8; x++) {
            int a = 0;
            for (int u = 0; u < 8; u++) a += base_cos[x][u] * f[v * 8 + u];
            t[v * 8 + x] = a >> 8;
        }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            int a = 0;
            for (int v = 0; v < 8; v++) a += base_cos[y][v] * t[v * 8 + x];
            a = (a >> 12) + 128;
            s[y * passo + x] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a);
        }
}

static uint8_t satura(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* ---- JPEG progressivo ----
 * A foto chega em várias passadas: primeiro o grosso, depois os detalhes. Os
 * coeficientes de cada bloco ficam guardados até a última passada. */
typedef struct { int ss, se, ah, al; } passada_t;

static bool bloco_dc(jbits_t *b, comp_t *c, int16_t *f, const passada_t *p, const jhuff_t *hdc)
{
    if (p->ah) { if (jbit(b)) f[0] = (int16_t)(f[0] | 1 << p->al); return true; }
    int s = jsimbolo(b, &hdc[c->dc]);
    if (s < 0 || s > 15) return false;
    c->pred += estender(jbits(b, s), s);
    f[0] = (int16_t)(c->pred * (1 << p->al));
    return true;
}

static void refinar(jbits_t *b, int16_t *c, int bit)
{
    if (jbit(b) && !(*c & bit)) *c = (int16_t)(*c + (*c > 0 ? bit : -bit));
}

static bool bloco_ac(jbits_t *b, int16_t *f, const passada_t *p, const jhuff_t *h, int *fim_faixa)
{
    int bit = 1 << p->al;
    if (!p->ah) {
        if (*fim_faixa) { --*fim_faixa; return true; }
        for (int k = p->ss; k <= p->se; k++) {
            int rs = jsimbolo(b, h);
            if (rs < 0) return false;
            int r = rs >> 4, s = rs & 15;
            if (!s) {
                if (r < 15) { *fim_faixa = (1 << r) - 1 + jbits(b, r); break; }
                k += 15;
                continue;
            }
            k += r;
            if (k > 63) return false;
            f[ziguezague[k]] = (int16_t)(estender(jbits(b, s), s) * bit);
        }
        return true;
    }
    int k = p->ss;
    if (!*fim_faixa) {
        while (k <= p->se) {
            int rs = jsimbolo(b, h);
            if (rs < 0) return false;
            int r = rs >> 4, s = rs & 15, novo = 0;
            if (!s) {
                if (r < 15) { *fim_faixa = (1 << r) + jbits(b, r); break; }
            } else novo = jbit(b) ? bit : -bit;
            for (; k <= p->se; k++) {
                int16_t *c = &f[ziguezague[k]];
                if (*c) refinar(b, c, bit);
                else if (!r) { if (novo) *c = (int16_t)novo; k++; break; }
                else r--;
            }
        }
    }
    if (*fim_faixa) {
        for (; k <= p->se; k++) {
            int16_t *c = &f[ziguezague[k]];
            if (*c) refinar(b, c, bit);
        }
        --*fim_faixa;
    }
    return true;
}

/* Decodifica uma passada. Dado ruim encerra a passada, e a foto sai com o que veio. */
static void passada(jbits_t *b, comp_t **sc, int ns, passada_t *p, int mx, int my, int reinicio,
                    int l, int a, int hmax, int vmax, const jhuff_t *hdc, const jhuff_t *hac)
{
    int nb_l = mx, nb_a = my;
    if (ns == 1) {   /* passada de um componente só: blocos da imagem, sem enchimento */
        nb_l = ((l * sc[0]->h + hmax - 1) / hmax + 7) / 8;
        nb_a = ((a * sc[0]->v + vmax - 1) / vmax + 7) / 8;
    }
    int resta = reinicio, fim_faixa = 0;
    for (int y = 0; y < nb_a; y++)
        for (int x = 0; x < nb_l; x++) {
            if (reinicio && !resta) {
                b->qtd = 0; b->fim = false;
                while (b->pos + 1 < b->n && !(b->p[b->pos] == 0xFF && b->p[b->pos + 1] >= 0xD0 && b->p[b->pos + 1] <= 0xD7)) b->pos++;
                b->pos += 2;
                for (int i = 0; i < ns; i++) sc[i]->pred = 0;
                fim_faixa = 0;
                resta = reinicio;
            }
            resta--;
            for (int i = 0; i < ns; i++) {
                comp_t *c = sc[i];
                int vs = ns == 1 ? 1 : c->v, hs = ns == 1 ? 1 : c->h;
                for (int v = 0; v < vs; v++)
                    for (int h = 0; h < hs; h++) {
                        int16_t *f = c->coef + (size_t)((y * vs + v) * c->bl + x * hs + h) * 64;
                        bool ok = p->ss ? bloco_ac(b, f, p, &hac[c->ac], &fim_faixa) : bloco_dc(b, c, f, p, hdc);
                        if (!ok) return;
                    }
            }
        }
}

static bool jpeg_planos(comp_t *cs, int nc, int mx, int my)
{
    for (int i = 0; i < nc; i++) {
        cs[i].bl = mx * cs[i].h;
        cs[i].ba = my * cs[i].v;
        cs[i].passo = cs[i].bl * 8;
        cs[i].plano = aloca((size_t)cs[i].passo * (size_t)(cs[i].ba * 8));
        if (!cs[i].plano) return false;
    }
    return true;
}

static bool jpeg_pixels(comp_t *cs, int nc, int l, int a, int hmax, int vmax, uint32_t **saida)
{
    uint32_t *px = aloca((size_t)l * (size_t)a * 4);
    if (!px) return false;
    for (int yy = 0; yy < a; yy++)
        for (int xx = 0; xx < l; xx++) {
            int Y = cs[0].plano[(yy * cs[0].v / vmax) * cs[0].passo + xx * cs[0].h / hmax];
            if (nc == 1) { px[yy * l + xx] = (uint32_t)Y * 0x010101; continue; }
            int cb = cs[1].plano[(yy * cs[1].v / vmax) * cs[1].passo + xx * cs[1].h / hmax] - 128;
            int cr = cs[2].plano[(yy * cs[2].v / vmax) * cs[2].passo + xx * cs[2].h / hmax] - 128;
            px[yy * l + xx] = (uint32_t)satura(Y + (359 * cr >> 8)) << 16 |
                              (uint32_t)satura(Y - ((88 * cb + 183 * cr) >> 8)) << 8 |
                              satura(Y + (454 * cb >> 8));
        }
    *saida = px;
    return true;
}

static bool jpeg(const uint8_t *d, unsigned n, uint32_t **saida, int *pl, int *pa)
{
    static uint8_t q[4][64];
    static jhuff_t hdc[4], hac[4];
    comp_t cs[3];
    int nc = 0, l = 0, a = 0, hmax = 1, vmax = 1, reinicio = 0;
    bool ok = false, progressivo = false;
    int mx = 0, my = 0;
    memset(cs, 0, sizeof cs);
    unsigned k = 2;
    while (k + 4 <= n) {
        if (d[k] != 0xFF) { k++; continue; }
        unsigned m = d[k + 1];
        if (m == 0xFF || m == 0) { k++; continue; }
        if (m >= 0xD0 && m <= 0xD7) { k += 2; continue; }
        if (m == 0xD9) break;
        unsigned t = (unsigned)d[k + 2] << 8 | d[k + 3];
        const uint8_t *c = d + k + 4;
        if (k + 2 + t > n) break;
        if (m == 0xDB) {
            for (unsigned i = 0; i + 65 <= t - 2; i += 65) {
                if (c[i] >> 4) goto fim;
                for (int z = 0; z < 64; z++) q[c[i] & 3][z] = c[i + 1 + (unsigned)z];
            }
        } else if (m == 0xC4) {
            unsigned i = 0;
            while (i + 17 <= t - 2) {
                jhuff_t *h = (c[i] >> 4 ? hac : hdc) + (c[i] & 3);
                unsigned tot = 0;
                for (int b = 1; b <= 16; b++) { h->cont[b] = c[i + (unsigned)b]; tot += h->cont[b]; }
                if (tot > 256 || i + 17 + tot > t - 2) goto fim;
                memcpy(h->val, c + i + 17, tot);
                h->tem = true;
                i += 17 + tot;
            }
        } else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            progressivo = m == 0xC2;
            if (c[0] != 8) goto fim;
            a = c[1] << 8 | c[2]; l = c[3] << 8 | c[4]; nc = c[5];
            if (nc != 1 && nc != 3) goto fim;
            if (!l || !a || (unsigned)l * (unsigned)a > MAX_PIXELS) goto fim;
            for (int i = 0; i < nc; i++) {
                cs[i].id = c[6 + i * 3];
                cs[i].h = c[7 + i * 3] >> 4; cs[i].v = c[7 + i * 3] & 15;
                cs[i].q = c[8 + i * 3] & 3;
                if (cs[i].h < 1 || cs[i].h > 2 || cs[i].v < 1 || cs[i].v > 2) goto fim;
                if (cs[i].h > hmax) hmax = cs[i].h;
                if (cs[i].v > vmax) vmax = cs[i].v;
            }
        } else if (m == 0xDD) {
            reinicio = c[0] << 8 | c[1];
        } else if (m == 0xDA && progressivo) {
            int ns = c[0];
            if (!nc || ns < 1 || ns > nc) goto fim;
            if (!cs[0].plano) {
                mx = (l + 8 * hmax - 1) / (8 * hmax); my = (a + 8 * vmax - 1) / (8 * vmax);
                if (!jpeg_planos(cs, nc, mx, my)) goto fim;
                for (int i = 0; i < nc; i++) {
                    size_t tam = (size_t)cs[i].bl * (size_t)cs[i].ba * 128;
                    cs[i].coef = aloca(tam);
                    if (!cs[i].coef) goto fim;
                    memset(cs[i].coef, 0, tam);
                }
            }
            comp_t *sc[3];
            for (int i = 0; i < ns; i++) {
                sc[i] = NULL;
                for (int z = 0; z < nc; z++)
                    if (cs[z].id == c[1 + i * 2]) { sc[i] = &cs[z]; cs[z].dc = c[2 + i * 2] >> 4 & 3; cs[z].ac = c[2 + i * 2] & 3; cs[z].pred = 0; }
                if (!sc[i]) goto fim;
            }
            passada_t p = { c[1 + ns * 2], c[2 + ns * 2], c[3 + ns * 2] >> 4, c[3 + ns * 2] & 15 };
            if (p.se > 63 || p.ss > p.se || p.al > 13 || (p.ss && ns != 1)) goto fim;
            jbits_t b = { d, n, k + 2 + t, 0, 0, false };
            passada(&b, sc, ns, &p, mx, my, reinicio, l, a, hmax, vmax, hdc, hac);
            k = b.pos;
            continue;
        } else if (m == 0xDA) {
            if (!nc || c[0] != nc) goto fim;
            for (int i = 0; i < nc; i++)
                for (int z = 0; z < nc; z++)
                    if (cs[z].id == c[1 + i * 2]) { cs[z].dc = c[2 + i * 2] >> 4 & 3; cs[z].ac = c[2 + i * 2] & 3; }
            int mcu_l = 8 * hmax, mcu_a = 8 * vmax;
            mx = (l + mcu_l - 1) / mcu_l; my = (a + mcu_a - 1) / mcu_a;
            if (!jpeg_planos(cs, nc, mx, my)) goto fim;
            jbits_t b = { d, n, k + 2 + t, 0, 0, false };
            int resta = reinicio;
            for (int y = 0; y < my; y++)
                for (int x = 0; x < mx; x++) {
                    if (reinicio && !resta) {
                        b.qtd = 0; b.fim = false;
                        while (b.pos + 1 < n && !(d[b.pos] == 0xFF && d[b.pos + 1] >= 0xD0 && d[b.pos + 1] <= 0xD7)) b.pos++;
                        b.pos += 2;
                        for (int i = 0; i < nc; i++) cs[i].pred = 0;
                        resta = reinicio;
                    }
                    resta--;
                    for (int i = 0; i < nc; i++)
                        for (int v = 0; v < cs[i].v; v++)
                            for (int h = 0; h < cs[i].h; h++) {
                                int f[64];
                                memset(f, 0, sizeof f);
                                int s = jsimbolo(&b, &hdc[cs[i].dc]);
                                if (s < 0 || s > 15) goto fim;
                                cs[i].pred += estender(jbits(&b, s), s);
                                f[0] = cs[i].pred * q[cs[i].q][0];
                                for (int z = 1; z < 64; z++) {
                                    s = jsimbolo(&b, &hac[cs[i].ac]);
                                    if (s < 0) goto fim;
                                    int zeros = s >> 4, tam = s & 15;
                                    if (!tam) { if (zeros < 15) break; z += 15; continue; }
                                    z += zeros;
                                    if (z > 63) break;
                                    f[ziguezague[z]] = estender(jbits(&b, tam), tam) * q[cs[i].q][z];
                                }
                                idct(f, cs[i].plano + ((y * cs[i].v + v) * 8) * cs[i].passo + (x * cs[i].h + h) * 8, cs[i].passo);
                            }
                }
            ok = jpeg_pixels(cs, nc, l, a, hmax, vmax, saida);
            if (ok) { *pl = l; *pa = a; }
            goto fim;
        }
        k += 2 + t;
    }
    if (progressivo && cs[0].coef) {   /* fim do arquivo: junta as passadas */
        for (int i = 0; i < nc; i++)
            for (int y = 0; y < cs[i].ba; y++)
                for (int x = 0; x < cs[i].bl; x++) {
                    const int16_t *e = cs[i].coef + (size_t)(y * cs[i].bl + x) * 64;
                    int f[64];
                    for (int z = 0; z < 64; z++) f[ziguezague[z]] = e[ziguezague[z]] * q[cs[i].q][z];
                    idct(f, cs[i].plano + y * 8 * cs[i].passo + x * 8, cs[i].passo);
                }
        ok = jpeg_pixels(cs, nc, l, a, hmax, vmax, saida);
        if (ok) { *pl = l; *pa = a; }
    }
fim:
    for (int i = 0; i < 3; i++) { libera(cs[i].plano); libera(cs[i].coef); }
    return ok;
}

bool imagem_decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a)
{
    static const uint8_t assinatura[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    if (n > 8 && !memcmp(d, assinatura, 8)) return png(d, n, px, l, a);
    if (n > 4 && d[0] == 0xFF && d[1] == 0xD8) return jpeg(d, n, px, l, a);
    return false;
}
