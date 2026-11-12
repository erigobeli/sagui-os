/* certificado.c - confere quem é o servidor TLS: cadeia X.509, nome e assinatura.
 *
 * Aceita RSA de 2048 a 4096 bits (PKCS#1 e PSS) e ECDSA P-256 e P-384. As
 * autoridades em que confiamos ficam em /etc/raizes.bin (tools/mkraizes.py).
 * Não consulta revogação (CRL, OCSP), nem Certificate Transparency. */
#include "sagui.h"
#include "cripto.h"

#define NL 130               /* 4096 bits e folga */
#define EL 12                /* limbs de um número da curva (P-384) */
#define CADEIA_MAX 65536
#define MAX_CERTS 10

/* ---- aritmética de Montgomery ---- */
typedef struct { uint32_t m[NL], r2[NL]; int n; uint32_t inv; } mont_t;

static void de_bytes(uint32_t *o, int nl, const uint8_t *b, unsigned t)
{
    memset(o, 0, (size_t)nl * 4);
    for (unsigned i = 0; i < t; i++) {
        unsigned k = i / 4;
        if ((int)k < nl) o[k] |= (uint32_t)b[t - 1 - i] << (8 * (i % 4));
    }
}

static void para_bytes(uint8_t *b, unsigned t, const uint32_t *a)
{
    for (unsigned i = 0; i < t; i++) b[t - 1 - i] = (uint8_t)(a[i / 4] >> (8 * (i % 4)));
}

static int comparar(const uint32_t *a, const uint32_t *b, int n)
{
    for (int i = n - 1; i >= 0; i--) if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}

static bool zerado(const uint32_t *a, int n) { for (int i = 0; i < n; i++) if (a[i]) return false; return true; }

static uint32_t somar(uint32_t *o, const uint32_t *a, const uint32_t *b, int n)
{
    uint64_t c = 0;
    for (int i = 0; i < n; i++) { c += (uint64_t)a[i] + b[i]; o[i] = (uint32_t)c; c >>= 32; }
    return (uint32_t)c;
}

static uint32_t subtrair(uint32_t *o, const uint32_t *a, const uint32_t *b, int n)
{
    uint32_t emprestado = 0;
    for (int i = 0; i < n; i++) {
        uint64_t d = (uint64_t)a[i] - b[i] - emprestado;
        o[i] = (uint32_t)d;
        emprestado = (uint32_t)(d >> 32) & 1;
    }
    return emprestado;
}

static void madd(const mont_t *c, uint32_t *o, const uint32_t *a, const uint32_t *b)
{
    uint32_t k = somar(o, a, b, c->n);
    if (k || comparar(o, c->m, c->n) >= 0) subtrair(o, o, c->m, c->n);
}

static void msub(const mont_t *c, uint32_t *o, const uint32_t *a, const uint32_t *b)
{
    if (subtrair(o, a, b, c->n)) somar(o, o, c->m, c->n);
}

static void mmul(const mont_t *c, uint32_t *o, const uint32_t *a, const uint32_t *b)
{
    static uint32_t t[NL + 2];
    int n = c->n;
    memset(t, 0, (size_t)(n + 2) * 4);
    for (int i = 0; i < n; i++) {
        uint64_t k = 0;
        for (int j = 0; j < n; j++) { k += (uint64_t)a[i] * b[j] + t[j]; t[j] = (uint32_t)k; k >>= 32; }
        k += t[n]; t[n] = (uint32_t)k; t[n + 1] = (uint32_t)(k >> 32);
        uint32_t q = t[0] * c->inv;
        k = (uint64_t)q * c->m[0] + t[0];
        k >>= 32;
        for (int j = 1; j < n; j++) { k += (uint64_t)q * c->m[j] + t[j]; t[j - 1] = (uint32_t)k; k >>= 32; }
        k += t[n]; t[n - 1] = (uint32_t)k;
        t[n] = t[n + 1] + (uint32_t)(k >> 32);
    }
    if (t[n] || comparar(t, c->m, n) >= 0) subtrair(t, t, c->m, n);
    memcpy(o, t, (size_t)n * 4);
}

static void mont_preparar(mont_t *c, const uint32_t *m, int n)
{
    memset(c, 0, sizeof *c);
    memcpy(c->m, m, (size_t)n * 4);
    c->n = n;
    uint32_t x = 1;
    for (int i = 0; i < 5; i++) x *= 2 - m[0] * x;
    c->inv = 0 - x;
    c->r2[0] = 1;
    for (int i = 0; i < 64 * n; i++) madd(c, c->r2, c->r2, c->r2);
}

/* o = base^exp, com base e o no domínio de Montgomery. */
static void mpot(const mont_t *c, uint32_t *o, const uint32_t *base, const uint32_t *exp, int bits)
{
    static uint32_t r[NL], um[NL];
    memset(um, 0, sizeof um);
    um[0] = 1;
    mmul(c, r, um, c->r2);
    for (int i = bits - 1; i >= 0; i--) {
        mmul(c, r, r, r);
        if (exp[i / 32] >> (i % 32) & 1) mmul(c, r, r, base);
    }
    memcpy(o, r, (size_t)c->n * 4);
}

/* ---- resumos ---- */
static void resumo(int t, const void *d, unsigned n, uint8_t *o)
{
    if (t == 32) sha256(d, n, o);
    else if (t == 48) sha384(d, n, o);
    else sha512(d, n, o);
}

/* ---- RSA ---- */
static bool rsa_abrir(const uint8_t *nb, unsigned nt, uint32_t e, const uint8_t *sig, unsigned st, uint8_t *saida)
{
    static mont_t c;
    static uint32_t m[NL], s[NL], ex[NL];
    while (nt && !nb[0]) { nb++; nt--; }
    if (nt < 256 || nt > 512 || st != nt) return false;
    int nl = (int)(nt + 3) / 4;
    de_bytes(m, nl, nb, nt);
    if (!(m[0] & 1)) return false;
    de_bytes(s, nl, sig, st);
    if (comparar(s, m, nl) >= 0) return false;
    mont_preparar(&c, m, nl);
    mmul(&c, s, s, c.r2);
    memset(ex, 0, sizeof ex);
    ex[0] = e;
    mpot(&c, s, s, ex, 32);
    memset(ex, 0, sizeof ex);
    ex[0] = 1;
    mmul(&c, s, s, ex);
    para_bytes(saida, nt, s);
    return true;
}

static bool rsa_pkcs1(const uint8_t *nb, unsigned nt, uint32_t e, const uint8_t *sig, unsigned st, const uint8_t *h, int hl)
{
    static const uint8_t p256[] = { 0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20 };
    static const uint8_t p384[] = { 0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30 };
    static const uint8_t p512[] = { 0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40 };
    static uint8_t em[512];
    const uint8_t *pre = hl == 32 ? p256 : hl == 48 ? p384 : p512;
    while (nt && !nb[0]) { nb++; nt--; }
    if (!rsa_abrir(nb, nt, e, sig, st, em)) return false;
    unsigned fim = nt - (unsigned)hl - 19;
    if (em[0] != 0 || em[1] != 1 || em[fim - 1] != 0 || fim < 11) return false;
    for (unsigned i = 2; i + 1 < fim; i++) if (em[i] != 0xFF) return false;
    return memcmp(em + fim, pre, 19) == 0 && memcmp(em + fim + 19, h, (size_t)hl) == 0;
}

static bool rsa_pss(const uint8_t *nb, unsigned nt, uint32_t e, const uint8_t *sig, unsigned st, const uint8_t *h, int hl)
{
    static uint8_t em[512], bloco[8 + 64 + 64];
    uint8_t q[64 + 4], d[64];
    while (nt && !nb[0]) { nb++; nt--; }
    if (!rsa_abrir(nb, nt, e, sig, st, em)) return false;
    int zeros = 1;
    for (uint8_t t = nb[0]; !(t & 0x80); t = (uint8_t)(t << 1)) zeros++;
    if (zeros == 8) return false;      /* tamanho de chave que nenhum site usa */
    if (em[nt - 1] != 0xBC) return false;
    unsigned ndb = nt - (unsigned)hl - 1;
    const uint8_t *hh = em + ndb;
    if (em[0] & ~(0xFF >> zeros)) return false;
    for (unsigned i = 0, cont = 0; i < ndb; cont++) {     /* MGF1 */
        memcpy(q, hh, (size_t)hl);
        q[hl] = (uint8_t)(cont >> 24); q[hl + 1] = (uint8_t)(cont >> 16); q[hl + 2] = (uint8_t)(cont >> 8); q[hl + 3] = (uint8_t)cont;
        resumo(hl, q, (unsigned)hl + 4, d);
        for (int k = 0; k < hl && i < ndb; k++) em[i++] ^= d[k];
    }
    em[0] &= (uint8_t)(0xFF >> zeros);
    unsigned p = ndb - (unsigned)hl - 1;
    for (unsigned i = 0; i < p; i++) if (em[i]) return false;
    if (em[p] != 1) return false;
    memset(bloco, 0, 8);
    memcpy(bloco + 8, h, (size_t)hl);
    memcpy(bloco + 8 + hl, em + p + 1, (size_t)hl);
    resumo(hl, bloco, 8 + 2 * (unsigned)hl, d);
    return memcmp(d, hh, (size_t)hl) == 0;
}

/* ---- ECDSA ---- */
static const char *const curvas[2][4] = {
    { "ffffffff00000001000000000000000000000000ffffffffffffffffffffffff",
      "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551",
      "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296",
      "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5" },
    { "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffff0000000000000000ffffffff",
      "ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf581a0db248b0a77aecec196accc52973",
      "aa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a385502f25dbf55296c3a545e3872760ab7",
      "3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c00a60b1ce1d7e819d7a431d7c90ea0e5f" },
};

static void de_hexa(uint32_t *o, int nl, const char *s)
{
    uint8_t b[48];
    unsigned t = (unsigned)tam_texto(s) / 2;
    for (unsigned i = 0; i < t; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) { char c = s[2 * i + (unsigned)k]; v = v * 16 + (c <= '9' ? c - '0' : c - 'a' + 10); }
        b[i] = (uint8_t)v;
    }
    de_bytes(o, nl, b, t);
}

typedef struct { uint32_t x[EL], y[EL], z[EL]; } ponto_t;
static mont_t cp, cn;

static void ec_dobrar(ponto_t *o, const ponto_t *a)
{
    uint32_t d[EL], g[EL], b[EL], al[EL], t[EL], u[EL];
    const mont_t *c = &cp;
    if (zerado(a->z, c->n)) { *o = *a; return; }
    mmul(c, d, a->z, a->z);
    mmul(c, g, a->y, a->y);
    mmul(c, b, a->x, g);
    msub(c, t, a->x, d);
    madd(c, u, a->x, d);
    mmul(c, al, t, u);
    madd(c, t, al, al); madd(c, al, t, al);          /* 3(x-d)(x+d) */
    madd(c, t, a->y, a->z);
    mmul(c, t, t, t);
    msub(c, t, t, g);
    msub(c, o->z, t, d);
    madd(c, b, b, b); madd(c, b, b, b);              /* 4 beta */
    mmul(c, t, al, al);
    msub(c, t, t, b); msub(c, t, t, b);
    memcpy(o->x, t, sizeof t);
    msub(c, u, b, t);
    mmul(c, u, al, u);
    mmul(c, g, g, g);
    madd(c, g, g, g); madd(c, g, g, g); madd(c, g, g, g);
    msub(c, o->y, u, g);
}

static void ec_somar(ponto_t *o, const ponto_t *a, const ponto_t *b)
{
    uint32_t z1[EL], z2[EL], u1[EL], u2[EL], s1[EL], s2[EL], h[EL], r[EL], h2[EL], h3[EL], t[EL];
    const mont_t *c = &cp;
    int n = c->n;
    if (zerado(a->z, n)) { *o = *b; return; }
    if (zerado(b->z, n)) { *o = *a; return; }
    mmul(c, z1, a->z, a->z);
    mmul(c, z2, b->z, b->z);
    mmul(c, u1, a->x, z2);
    mmul(c, u2, b->x, z1);
    mmul(c, s1, a->y, b->z); mmul(c, s1, s1, z2);
    mmul(c, s2, b->y, a->z); mmul(c, s2, s2, z1);
    msub(c, h, u2, u1);
    msub(c, r, s2, s1);
    if (zerado(h, n)) {
        if (zerado(r, n)) { ponto_t k = *a; ec_dobrar(o, &k); }
        else memset(o, 0, sizeof *o);
        return;
    }
    mmul(c, h2, h, h);
    mmul(c, h3, h2, h);
    mmul(c, u1, u1, h2);
    ponto_t q;
    memset(&q, 0, sizeof q);
    mmul(c, t, r, r);
    msub(c, t, t, h3);
    msub(c, t, t, u1); msub(c, t, t, u1);
    memcpy(q.x, t, sizeof t);
    msub(c, u1, u1, t);
    mmul(c, u1, r, u1);
    mmul(c, s1, s1, h3);
    msub(c, q.y, u1, s1);
    mmul(c, q.z, a->z, b->z);
    mmul(c, q.z, q.z, h);
    *o = q;
}

/* qual: 0 = P-256, 1 = P-384. q = ponto sem compressão (04 x y). */
static bool ecdsa(int qual, const uint8_t *q, unsigned nq, const uint8_t *h, int hl, const uint8_t *r, unsigned nr, const uint8_t *s, unsigned ns)
{
    int nl = qual ? 12 : 8;
    unsigned nb = qual ? 48 : 32;
    uint32_t t[NL], e[EL], rr[EL], ss[EL], w[EL], u1[EL], u2[EL], um[EL], x[EL];
    if (nq != 2 * nb + 1 || q[0] != 4) return false;
    while (nr && !r[0]) { r++; nr--; }
    while (ns && !s[0]) { s++; ns--; }
    if (!nr || !ns || nr > nb || ns > nb) return false;
    memset(t, 0, sizeof t);
    de_hexa(t, nl, curvas[qual][0]); mont_preparar(&cp, t, nl);
    de_hexa(t, nl, curvas[qual][1]); mont_preparar(&cn, t, nl);
    de_bytes(rr, nl, r, nr);
    de_bytes(ss, nl, s, ns);
    if (zerado(rr, nl) || zerado(ss, nl) || comparar(rr, cn.m, nl) >= 0 || comparar(ss, cn.m, nl) >= 0) return false;
    de_bytes(e, nl, h, (unsigned)hl < nb ? (unsigned)hl : nb);
    if (comparar(e, cn.m, nl) >= 0) subtrair(e, e, cn.m, nl);
    memset(um, 0, sizeof um); um[0] = 1;
    uint32_t dois[EL];
    memset(dois, 0, sizeof dois); dois[0] = 2;
    subtrair(t, cn.m, dois, nl);
    mmul(&cn, w, ss, cn.r2);
    mpot(&cn, w, w, t, nl * 32);                     /* w = 1/s */
    mmul(&cn, e, e, cn.r2);
    mmul(&cn, rr, rr, cn.r2);
    mmul(&cn, u1, e, w); mmul(&cn, u1, u1, um);
    mmul(&cn, u2, rr, w); mmul(&cn, u2, u2, um);

    ponto_t g, p, gp, a;
    memset(&g, 0, sizeof g); memset(&p, 0, sizeof p); memset(&a, 0, sizeof a);
    de_hexa(g.x, nl, curvas[qual][2]); de_hexa(g.y, nl, curvas[qual][3]);
    de_bytes(p.x, nl, q + 1, nb); de_bytes(p.y, nl, q + 1 + nb, nb);
    if (comparar(p.x, cp.m, nl) >= 0 || comparar(p.y, cp.m, nl) >= 0) return false;
    mmul(&cp, g.x, g.x, cp.r2); mmul(&cp, g.y, g.y, cp.r2); mmul(&cp, g.z, um, cp.r2);
    mmul(&cp, p.x, p.x, cp.r2); mmul(&cp, p.y, p.y, cp.r2); memcpy(p.z, g.z, sizeof p.z);
    ec_somar(&gp, &g, &p);
    for (int i = nl * 32 - 1; i >= 0; i--) {
        ponto_t k = a;
        ec_dobrar(&a, &k);
        int b1 = u1[i / 32] >> (i % 32) & 1, b2 = u2[i / 32] >> (i % 32) & 1;
        if (b1 || b2) { k = a; ec_somar(&a, &k, b1 && b2 ? &gp : b1 ? &g : &p); }
    }
    if (zerado(a.z, nl)) return false;
    subtrair(t, cp.m, dois, nl);
    mpot(&cp, w, a.z, t, nl * 32);
    mmul(&cp, w, w, w);
    mmul(&cp, x, a.x, w);
    mmul(&cp, x, x, um);
    if (comparar(x, cn.m, nl) >= 0) subtrair(x, x, cn.m, nl);
    de_bytes(rr, nl, r, nr);
    return comparar(x, rr, nl) == 0;
}

/* ---- ECDH em P-256, para TLS 1.2 ---- */
static void p256_preparar(void)
{
    uint32_t t[NL];
    memset(t, 0, sizeof t);
    de_hexa(t, 8, curvas[0][0]);
    mont_preparar(&cp, t, 8);
}

/* o = k * p, em Montgomery. */
static void ec_vezes(ponto_t *o, const uint8_t k[32], const ponto_t *p)
{
    ponto_t a;
    memset(&a, 0, sizeof a);
    for (int i = 0; i < 256; i++) {
        ponto_t c = a;
        ec_dobrar(&a, &c);
        if (k[i / 8] >> (7 - i % 8) & 1) { c = a; ec_somar(&a, &c, p); }
    }
    *o = a;
}

static void ec_afim(const ponto_t *p, uint8_t *x, uint8_t *y)
{
    uint32_t e[EL], z[EL], um[EL], dois[EL], v[EL];
    memset(um, 0, sizeof um); um[0] = 1;
    memset(dois, 0, sizeof dois); dois[0] = 2;
    subtrair(e, cp.m, dois, 8);
    mpot(&cp, z, p->z, e, 256);
    mmul(&cp, e, z, z);
    mmul(&cp, v, p->x, e); mmul(&cp, v, v, um);
    para_bytes(x, 32, v);
    if (y) { mmul(&cp, e, e, z); mmul(&cp, v, p->y, e); mmul(&cp, v, v, um); para_bytes(y, 32, v); }
}

void p256_publica(const uint8_t priv[32], uint8_t pub[65])
{
    ponto_t g, r;
    uint32_t um[EL];
    p256_preparar();
    memset(&g, 0, sizeof g);
    memset(um, 0, sizeof um); um[0] = 1;
    de_hexa(g.x, 8, curvas[0][2]); de_hexa(g.y, 8, curvas[0][3]);
    mmul(&cp, g.x, g.x, cp.r2); mmul(&cp, g.y, g.y, cp.r2); mmul(&cp, g.z, um, cp.r2);
    ec_vezes(&r, priv, &g);
    pub[0] = 4;
    ec_afim(&r, pub + 1, pub + 33);
}

/* Segredo comum. Recusa ponto fora da curva: y² = x³ - 3x + b. */
bool p256_comum(const uint8_t priv[32], const uint8_t *q, unsigned nq, uint8_t saida[32])
{
    ponto_t p, r;
    uint32_t um[EL], b[EL], t[EL], u[EL];
    if (nq != 65 || q[0] != 4) return false;
    p256_preparar();
    memset(&p, 0, sizeof p);
    memset(um, 0, sizeof um); um[0] = 1;
    de_bytes(p.x, 8, q + 1, 32); de_bytes(p.y, 8, q + 33, 32);
    if (comparar(p.x, cp.m, 8) >= 0 || comparar(p.y, cp.m, 8) >= 0) return false;
    mmul(&cp, p.x, p.x, cp.r2); mmul(&cp, p.y, p.y, cp.r2); mmul(&cp, p.z, um, cp.r2);
    de_hexa(b, 8, "5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b");
    mmul(&cp, b, b, cp.r2);
    mmul(&cp, t, p.x, p.x); mmul(&cp, t, t, p.x);
    msub(&cp, t, t, p.x); msub(&cp, t, t, p.x); msub(&cp, t, t, p.x);
    madd(&cp, t, t, b);
    mmul(&cp, u, p.y, p.y);
    if (comparar(t, u, 8) != 0) return false;
    ec_vezes(&r, priv, &p);
    if (zerado(r.z, 8)) return false;
    ec_afim(&r, saida, NULL);
    return true;
}

/* ---- DER ---- */
static const uint8_t *der(const uint8_t *p, const uint8_t *fim, int *tag, const uint8_t **c, unsigned *n)
{
    if (fim - p < 2) return NULL;
    *tag = *p++;
    unsigned t = *p++;
    if (t & 0x80) {
        int k = t & 0x7F;
        if (k < 1 || k > 3 || fim - p < k) return NULL;
        t = 0;
        while (k--) t = t << 8 | *p++;
    }
    if ((unsigned)(fim - p) < t) return NULL;
    *c = p;
    *n = t;
    return p + t;
}

typedef struct {
    int tipo;                  /* 1 RSA, 2 P-256, 3 P-384 */
    const uint8_t *n, *q; unsigned nn, nq; uint32_t e;
} chave_t;

typedef struct {
    const uint8_t *tbs, *sig, *emissor, *dono, *alg, *nomes;
    unsigned ntbs, nsig, nemissor, ndono, nalg, nnomes;
    char desde[16], ate[16];
    bool ca;
    chave_t chave;
} cert_t;

static bool oid_igual(const uint8_t *o, unsigned n, const char *hexa)
{
    unsigned t = (unsigned)tam_texto(hexa) / 2;
    if (n != t) return false;
    for (unsigned i = 0; i < t; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) { char c = hexa[2 * i + (unsigned)k]; v = v * 16 + (c <= '9' ? c - '0' : c - 'a' + 10); }
        if (o[i] != v) return false;
    }
    return true;
}

static bool ler_tempo(int tag, const uint8_t *c, unsigned n, char *o)
{
    if (tag == 0x17 && n == 13) { o[0] = c[0] >= '5' ? '1' : '2'; o[1] = c[0] >= '5' ? '9' : '0'; memcpy(o + 2, c, 12); }
    else if (tag == 0x18 && n == 15) memcpy(o, c, 14);
    else return false;
    o[14] = 0;
    return true;
}

static bool ler_chave(const uint8_t *p, unsigned n, chave_t *k)
{
    const uint8_t *fim = p + n, *c, *o, *a, *b;
    unsigned nc, no, na, nb;
    int t;
    const uint8_t *r = der(p, fim, &t, &c, &nc);
    if (!r || t != 0x30) return false;
    const uint8_t *ra = der(c, c + nc, &t, &o, &no);   /* OID do algoritmo */
    if (!ra || t != 6) return false;
    const uint8_t *bits = r;
    if (!der(bits, fim, &t, &a, &na) || t != 3 || na < 2 || a[0]) return false;
    a++; na--;
    if (oid_igual(o, no, "2a864886f70d010101")) {
        const uint8_t *s;
        unsigned ns;
        if (!der(a, a + na, &t, &s, &ns) || t != 0x30) return false;
        const uint8_t *x = der(s, s + ns, &t, &b, &nb);
        if (!x || t != 2) return false;
        k->tipo = 1; k->n = b; k->nn = nb;
        if (!der(x, s + ns, &t, &b, &nb) || t != 2 || nb > 4 || !nb) return false;
        k->e = 0;
        for (unsigned i = 0; i < nb; i++) k->e = k->e << 8 | b[i];
        return true;
    }
    if (oid_igual(o, no, "2a8648ce3d0201")) {
        if (!der(ra, c + nc, &t, &b, &nb) || t != 6) return false;
        k->tipo = oid_igual(b, nb, "2a8648ce3d030107") ? 2 : oid_igual(b, nb, "2b81040022") ? 3 : 0;
        k->q = a; k->nq = na;
        return k->tipo != 0;
    }
    return false;
}

static bool ler_extensoes(const uint8_t *p, const uint8_t *fim, cert_t *x)
{
    const uint8_t *c, *e, *o, *v;
    unsigned nc, ne, no, nv;
    int t;
    if (!der(p, fim, &t, &c, &nc) || t != 0x30) return false;
    for (const uint8_t *q = c; q < c + nc;) {
        q = der(q, c + nc, &t, &e, &ne);
        if (!q || t != 0x30) return false;
        const uint8_t *r = der(e, e + ne, &t, &o, &no);
        if (!r || t != 6) return false;
        bool critica = false;
        r = der(r, e + ne, &t, &v, &nv);
        if (r && t == 1) { critica = nv == 1 && v[0]; r = der(r, e + ne, &t, &v, &nv); }
        if (!r || t != 4) return false;
        if (oid_igual(o, no, "551d13")) {
            const uint8_t *s, *b;
            unsigned ns, nb;
            if (der(v, v + nv, &t, &s, &ns) && t == 0x30 && ns && der(s, s + ns, &t, &b, &nb) && t == 1 && nb == 1 && b[0]) x->ca = true;
        } else if (oid_igual(o, no, "551d11")) {
            const uint8_t *s;
            unsigned ns;
            if (der(v, v + nv, &t, &s, &ns) && t == 0x30) { x->nomes = s; x->nnomes = ns; }
        } else if (critica && !oid_igual(o, no, "551d0f") && !oid_igual(o, no, "551d25") && !oid_igual(o, no, "551d20")) {
            return false;      /* restrição que não entendemos: não dá para aceitar */
        }
    }
    return true;
}

static bool ler_cert(const uint8_t *p, unsigned n, cert_t *x)
{
    const uint8_t *fim = p + n, *c, *f, *a, *b;
    unsigned nc, nf, na, nb;
    int t;
    memset(x, 0, sizeof *x);
    if (!der(p, fim, &t, &c, &nc) || t != 0x30) return false;
    const uint8_t *r = der(c, c + nc, &t, &f, &nf);
    if (!r || t != 0x30) return false;
    x->tbs = c;
    x->ntbs = (unsigned)(r - c);
    const uint8_t *s = der(r, c + nc, &t, &a, &na);
    if (!s || t != 0x30 || !der(a, a + na, &t, &x->alg, &x->nalg) || t != 6) return false;
    if (!der(s, c + nc, &t, &b, &nb) || t != 3 || nb < 2 || b[0]) return false;
    x->sig = b + 1; x->nsig = nb - 1;
    const uint8_t *q = f, *fim_t = f + nf, *v;
    unsigned nv;
    const uint8_t *z = der(q, fim_t, &t, &v, &nv);
    if (z && t == 0xA0) q = z;
    if (!(q = der(q, fim_t, &t, &v, &nv))) return false;           /* número de série */
    if (!(q = der(q, fim_t, &t, &v, &nv))) return false;           /* algoritmo */
    x->emissor = q;
    if (!(z = der(q, fim_t, &t, &v, &nv))) return false;
    x->nemissor = (unsigned)(z - q); q = z;
    if (!(z = der(q, fim_t, &t, &v, &nv)) || t != 0x30) return false;   /* validade */
    { const uint8_t *m, *w = v; unsigned nm;
      if (!(w = der(w, v + nv, &t, &m, &nm)) || !ler_tempo(t, m, nm, x->desde)) return false;
      if (!der(w, v + nv, &t, &m, &nm) || !ler_tempo(t, m, nm, x->ate)) return false; }
    q = z;
    x->dono = q;
    if (!(z = der(q, fim_t, &t, &v, &nv))) return false;
    x->ndono = (unsigned)(z - q); q = z;
    if (!(z = der(q, fim_t, &t, &v, &nv))) return false;
    if (!ler_chave(v, nv, &x->chave)) return false;
    q = z;
    while (q < fim_t) {
        z = der(q, fim_t, &t, &v, &nv);
        if (!z) return false;
        if (t == 0xA3 && !ler_extensoes(v, v + nv, x)) return false;
        q = z;
    }
    return true;
}

static bool assinatura_ecdsa(const chave_t *k, const uint8_t *h, int hl, const uint8_t *sig, unsigned ns)
{
    const uint8_t *c, *r, *s;
    unsigned nc, nr, nss;
    int t;
    if (k->tipo < 2 || !der(sig, sig + ns, &t, &c, &nc) || t != 0x30) return false;
    const uint8_t *p = der(c, c + nc, &t, &r, &nr);
    if (!p || t != 2 || !der(p, c + nc, &t, &s, &nss) || t != 2) return false;
    return ecdsa(k->tipo - 2, k->q, k->nq, h, hl, r, nr, s, nss);
}

/* A assinatura de x confere com a chave k? */
static bool assinado_por(const cert_t *x, const chave_t *k)
{
    uint8_t h[64];
    int hl = 0;
    bool rsa = false;
    if (oid_igual(x->alg, x->nalg, "2a864886f70d01010b")) { hl = 32; rsa = true; }
    else if (oid_igual(x->alg, x->nalg, "2a864886f70d01010c")) { hl = 48; rsa = true; }
    else if (oid_igual(x->alg, x->nalg, "2a864886f70d01010d")) { hl = 64; rsa = true; }
    else if (oid_igual(x->alg, x->nalg, "2a8648ce3d040302")) hl = 32;
    else if (oid_igual(x->alg, x->nalg, "2a8648ce3d040303")) hl = 48;
    else if (oid_igual(x->alg, x->nalg, "2a8648ce3d040304")) hl = 64;
    else return false;
    resumo(hl, x->tbs, x->ntbs, h);
    if (rsa) return k->tipo == 1 && rsa_pkcs1(k->n, k->nn, k->e, x->sig, x->nsig, h, hl);
    return assinatura_ecdsa(k, h, hl, x->sig, x->nsig);
}

/* ---- autoridades ---- */
static uint8_t *raizes;
static unsigned raizes_n;

static void carregar_raizes(void)
{
    if (raizes) return;
    int fd = abrir("/etc/raizes.bin", ABRIR_LER);
    if (fd < 0) return;
    unsigned cap = 262144;
    uint8_t *d = aloca(cap);
    if (!d) { fechar(fd); return; }
    int r;
    while (raizes_n < cap && (r = ler(fd, d + raizes_n, cap - raizes_n)) > 0) raizes_n += (unsigned)r;
    fechar(fd);
    raizes = d;
}

/* Registro: tipo(1) resumo do nome(16) tamanho(2) chave expoente(4). */
static bool raiz_assinou(const cert_t *x)
{
    uint8_t h[32];
    sha256(x->emissor, x->nemissor, h);
    for (unsigned p = 0; p + 23 <= raizes_n;) {
        const uint8_t *e = raizes + p;
        unsigned t = (unsigned)e[17] << 8 | e[18];
        if (p + 23 + t > raizes_n) return false;
        if (memcmp(e + 1, h, 16) == 0) {
            chave_t k;
            memset(&k, 0, sizeof k);
            k.tipo = e[0];
            if (k.tipo == 1) {
                k.n = e + 19; k.nn = t;
                k.e = (uint32_t)e[19 + t] << 24 | (uint32_t)e[20 + t] << 16 | (uint32_t)e[21 + t] << 8 | e[22 + t];
            } else { k.q = e + 19; k.nq = t; }
            if (assinado_por(x, &k)) return true;
        }
        p += 23 + t;
    }
    return false;
}

static bool nome_bate(const uint8_t *n, unsigned t, const char *host)
{
    unsigned th = (unsigned)tam_texto(host), i = 0;
    if (t > 2 && n[0] == '*' && n[1] == '.') {
        while (i < th && host[i] != '.') i++;
        if (!i || i == th) return false;
        n++; t--;
        if (th - i != t) return false;
        host += i;
        th = t;
    }
    if (th != t) return false;
    for (i = 0; i < t; i++) {
        char a = (char)n[i], b = host[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
    }
    return true;
}

static bool ip_de(const char *h, uint8_t o[4])
{
    for (int i = 0; i < 4; i++) {
        int v = 0, d = 0;
        while (*h >= '0' && *h <= '9') { v = v * 10 + *h++ - '0'; d++; }
        if (!d || v > 255 || *h != (i < 3 ? '.' : 0)) return false;
        o[i] = (uint8_t)v;
        h++;
    }
    return true;
}

static bool host_bate(const cert_t *x, const char *host)
{
    uint8_t ip[4];
    bool eh_ip = ip_de(host, ip);
    const uint8_t *c;
    unsigned n;
    int t;
    for (const uint8_t *p = x->nomes; p && p < x->nomes + x->nnomes;) {
        p = der(p, x->nomes + x->nnomes, &t, &c, &n);
        if (!p) return false;
        if (t == 0x82 && !eh_ip && nome_bate(c, n, host)) return true;
        if (t == 0x87 && eh_ip && n == 4 && memcmp(c, ip, 4) == 0) return true;
    }
    return false;
}

/* ---- o que o TLS chama ---- */
static uint8_t cadeia[CADEIA_MAX];
static cert_t certs[MAX_CERTS];
static int qtd_certs;

static bool na_validade(const cert_t *x, const char *agora)
{
    for (int i = 0; i < 14; i++) if (x->desde[i] != agora[i]) { if (x->desde[i] > agora[i]) return false; break; }
    for (int i = 0; i < 14; i++) if (x->ate[i] != agora[i]) return x->ate[i] > agora[i];
    return true;
}

const char *cert_cadeia(const uint8_t *m, unsigned n, const char *host, bool v13)
{
    data_t d;
    char agora[16];
    if (n > CADEIA_MAX || n < 4) return "certificados grandes demais";
    memcpy(cadeia, m, n);
    if (ler_data(&d) < 0 || d.ano < 2024) return "relógio do sistema errado: não dá para conferir o certificado";
    formatar_texto(agora, sizeof agora, "%d%d%d%d%d%d%d%d%d%d", d.ano, d.mes / 10, d.mes % 10, d.dia / 10, d.dia % 10,
                   d.hora / 10, d.hora % 10, d.minuto / 10, d.minuto % 10, d.segundo / 10);
    agora[13] = (char)('0' + d.segundo % 10);
    agora[14] = 0;
    carregar_raizes();
    if (!raizes_n) return "lista de autoridades ausente";
    unsigned k = v13 ? 1u + cadeia[0] : 0;
    if (k + 3 > n) return "certificado malformado";
    k += 3;
    qtd_certs = 0;
    while (k + 3 <= n && qtd_certs < MAX_CERTS) {
        unsigned t = (unsigned)cadeia[k] << 16 | (unsigned)cadeia[k + 1] << 8 | cadeia[k + 2];
        k += 3;
        if (k + t + (v13 ? 2 : 0) > n) return "certificado malformado";
        if (!ler_cert(cadeia + k, t, &certs[qtd_certs])) {
            if (!qtd_certs) return "certificado com formato ou chave não aceitos";
        } else qtd_certs++;
        k += t;
        if (v13) k += 2u + ((unsigned)cadeia[k] << 8 | cadeia[k + 1]);
    }
    if (!qtd_certs) return "o servidor não mandou certificado";
    const cert_t *x = &certs[0];
    if (!host_bate(x, host)) return "o certificado é de outro site";
    if (!na_validade(x, agora)) return "certificado vencido ou ainda não válido";
    for (int passo = 0; passo < MAX_CERTS; passo++) {
        if (raiz_assinou(x)) return NULL;
        const cert_t *prox = NULL;
        for (int i = 1; i < qtd_certs && !prox; i++) {
            const cert_t *c = &certs[i];
            if (c != x && c->ca && c->ndono == x->nemissor && memcmp(c->dono, x->emissor, c->ndono) == 0 &&
                na_validade(c, agora) && assinado_por(x, &c->chave)) prox = c;
        }
        if (!prox) return passo || qtd_certs == 1 ? "autoridade desconhecida" : "cadeia de certificados inválida";
        x = prox;
    }
    return "cadeia de certificados longa demais";
}

const char *cert_prova(const uint8_t *m, unsigned n, const uint8_t transcricao[32])
{
    static const char rotulo[] = "TLS 1.3, server CertificateVerify";
    uint8_t conteudo[64 + 34 + 32], h[64];
    if (n < 4 || !qtd_certs) return "prova do servidor malformada";
    unsigned alg = (unsigned)m[0] << 8 | m[1], t = (unsigned)m[2] << 8 | m[3];
    if (t + 4 > n) return "prova do servidor malformada";
    memset(conteudo, ' ', 64);
    memcpy(conteudo + 64, rotulo, 34);
    memcpy(conteudo + 98, transcricao, 32);
    const chave_t *k = &certs[0].chave;
    int hl = alg == 0x0403 || alg == 0x0804 ? 32 : alg == 0x0503 || alg == 0x0805 ? 48 : alg == 0x0806 ? 64 : 0;
    if (!hl) return "assinatura do servidor de tipo não aceito";
    resumo(hl, conteudo, sizeof conteudo, h);
    bool ok = alg >> 8 == 8 ? k->tipo == 1 && rsa_pss(k->n, k->nn, k->e, m + 4, t, h, hl) : assinatura_ecdsa(k, h, hl, m + 4, t);
    return ok ? NULL : "a assinatura do servidor não confere";
}

/* TLS 1.2: m = algoritmo, tamanho e assinatura sobre "dados" (ServerKeyExchange). */
const char *cert_prova12(const uint8_t *m, unsigned n, const uint8_t *dados, unsigned nd)
{
    uint8_t h[64];
    if (n < 4 || !qtd_certs) return "prova do servidor malformada";
    unsigned alg = (unsigned)m[0] << 8 | m[1], t = (unsigned)m[2] << 8 | m[3];
    if (t + 4 > n) return "prova do servidor malformada";
    int hl = 0;
    switch (alg) {
    case 0x0401: case 0x0403: case 0x0804: hl = 32; break;
    case 0x0501: case 0x0503: case 0x0805: hl = 48; break;
    case 0x0601: case 0x0806: hl = 64; break;
    default: return "assinatura do servidor de tipo não aceito";
    }
    resumo(hl, dados, nd, h);
    const chave_t *k = &certs[0].chave;
    bool ok;
    if (alg >> 8 == 8) ok = k->tipo == 1 && rsa_pss(k->n, k->nn, k->e, m + 4, t, h, hl);
    else if ((alg & 255) == 1) ok = k->tipo == 1 && rsa_pkcs1(k->n, k->nn, k->e, m + 4, t, h, hl);
    else ok = assinatura_ecdsa(k, h, hl, m + 4, t);
    return ok ? NULL : "a assinatura do servidor não confere";
}
