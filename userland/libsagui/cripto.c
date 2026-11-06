/* cripto.c - o que o TLS 1.3 precisa, escrito a partir das especificações:
 * SHA-256 (FIPS 180-4), HMAC e HKDF (RFC 2104 e 5869), AES-128 (FIPS 197),
 * GCM (NIST SP 800-38D) e X25519 (RFC 7748).
 *
 * Prioridade: ser pequeno e fácil de conferir, não ser rápido. Só usa
 * inteiros. Não há proteção contra ataques de tempo no AES e no GHASH. */
#include "sagui.h"
#include "cripto.h"

/* ------------------------------------------------------------ SHA-256 */

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define ROTD(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_bloco(sha256_t *c, const uint8_t *p)
{
    uint32_t w[64], s[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t a = w[i - 15], b = w[i - 2];
        w[i] = w[i - 16] + (ROTD(a, 7) ^ ROTD(a, 18) ^ (a >> 3)) + w[i - 7] + (ROTD(b, 17) ^ ROTD(b, 19) ^ (b >> 10));
    }
    memcpy(s, c->h, sizeof s);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = s[7] + (ROTD(s[4], 6) ^ ROTD(s[4], 11) ^ ROTD(s[4], 25)) + ((s[4] & s[5]) ^ (~s[4] & s[6])) + K[i] + w[i];
        uint32_t t2 = (ROTD(s[0], 2) ^ ROTD(s[0], 13) ^ ROTD(s[0], 22)) + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        for (int k = 7; k > 0; k--) s[k] = s[k - 1];
        s[4] += t1;
        s[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) c->h[i] += s[i];
}

void sha256_iniciar(sha256_t *c)
{
    static const uint32_t h0[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(c->h, h0, sizeof h0);
    c->n = 0;
    c->total = 0;
}

void sha256_somar(sha256_t *c, const void *dados, unsigned n)
{
    const uint8_t *p = dados;
    c->total += n;
    while (n) {
        unsigned k = 64 - c->n < n ? 64 - c->n : n;
        memcpy(c->buf + c->n, p, k);
        c->n += k; p += k; n -= k;
        if (c->n == 64) { sha_bloco(c, c->buf); c->n = 0; }
    }
}

/* Não estraga o contexto: o TLS pede o resumo parcial da conversa. */
void sha256_fim(const sha256_t *origem, uint8_t saida[32])
{
    sha256_t c = *origem;
    uint64_t bits = c.total * 8;
    uint8_t um = 0x80, zero = 0, tam[8];
    sha256_somar(&c, &um, 1);
    while (c.n != 56) sha256_somar(&c, &zero, 1);
    for (int i = 0; i < 8; i++) tam[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_somar(&c, tam, 8);
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 4; k++) saida[4 * i + k] = (uint8_t)(c.h[i] >> (24 - 8 * k));
}

void sha256(const void *d, unsigned n, uint8_t saida[32])
{
    sha256_t c;
    sha256_iniciar(&c);
    sha256_somar(&c, d, n);
    sha256_fim(&c, saida);
}

void hmac_sha256(const uint8_t *chave, unsigned nc, const void *d, unsigned n, uint8_t saida[32])
{
    uint8_t k[64], t[32];
    memset(k, 0, sizeof k);
    if (nc > 64) sha256(chave, nc, k); else memcpy(k, chave, nc);
    sha256_t c;
    for (int i = 0; i < 64; i++) k[i] ^= 0x36;
    sha256_iniciar(&c);
    sha256_somar(&c, k, 64);
    sha256_somar(&c, d, n);
    sha256_fim(&c, t);
    for (int i = 0; i < 64; i++) k[i] ^= 0x36 ^ 0x5c;
    sha256_iniciar(&c);
    sha256_somar(&c, k, 64);
    sha256_somar(&c, t, 32);
    sha256_fim(&c, saida);
}

void hkdf_extrair(const uint8_t *sal, unsigned ns, const uint8_t *ikm, unsigned ni, uint8_t prk[32])
{
    hmac_sha256(sal, ns, ikm, ni, prk);
}

/* Até 32 bytes: é tudo o que o TLS 1.3 com SHA-256 pede de uma vez. */
void hkdf_rotulo(const uint8_t segredo[32], const char *rotulo, const uint8_t *ctx, unsigned nctx, uint8_t *saida, unsigned n)
{
    uint8_t info[100], t[32];
    unsigned nr = (unsigned)tam_texto(rotulo), k = 0;
    info[k++] = 0;
    info[k++] = (uint8_t)n;
    info[k++] = (uint8_t)(6 + nr);
    memcpy(info + k, "tls13 ", 6); k += 6;
    memcpy(info + k, rotulo, nr); k += nr;
    info[k++] = (uint8_t)nctx;
    if (nctx) memcpy(info + k, ctx, nctx);
    k += nctx;
    info[k++] = 1;
    hmac_sha256(segredo, 32, info, k, t);
    memcpy(saida, t, n);
}

/* ------------------------------------------------------------ AES-128 */

static uint8_t caixa[256];

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }

/* A caixa S é calculada: inverso em GF(2^8) e a transformação afim. */
static void montar_caixa(void)
{
    if (caixa[0]) return;
    uint8_t p = 1, q = 1;
    do {
        p = (uint8_t)(p ^ xtime(p));         /* p *= 3 */
        q ^= (uint8_t)(q << 1); q ^= (uint8_t)(q << 2); q ^= (uint8_t)(q << 4);   /* q /= 3 */
        if (q & 0x80) q ^= 0x09;
        uint8_t r = q;
        uint8_t v = q;
        for (int i = 1; i <= 4; i++) { r = (uint8_t)((r << 1) | (r >> 7)); v ^= r; }
        caixa[p] = v ^ 0x63;
    } while (p != 1);
    caixa[0] = 0x63;
}

void aes_chave(aes_t *a, const uint8_t chave[16])
{
    montar_caixa();
    memcpy(a->rk, chave, 16);
    uint8_t rc = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4];
        memcpy(t, a->rk + i - 4, 4);
        if (i % 16 == 0) {
            uint8_t x = t[0];
            t[0] = caixa[t[1]] ^ rc; t[1] = caixa[t[2]]; t[2] = caixa[t[3]]; t[3] = caixa[x];
            rc = xtime(rc);
        }
        for (int k = 0; k < 4; k++) a->rk[i + k] = a->rk[i + k - 16] ^ t[k];
    }
}

void aes_bloco(const aes_t *a, const uint8_t e[16], uint8_t s[16])
{
    uint8_t b[16], t[16];
    for (int i = 0; i < 16; i++) b[i] = e[i] ^ a->rk[i];
    for (int r = 1; r <= 10; r++) {
        for (int c = 0; c < 4; c++)              /* SubBytes e ShiftRows */
            for (int l = 0; l < 4; l++) t[4 * c + l] = caixa[b[4 * ((c + l) & 3) + l]];
        if (r < 10)
            for (int c = 0; c < 4; c++) {        /* MixColumns */
                uint8_t *x = t + 4 * c, tudo = x[0] ^ x[1] ^ x[2] ^ x[3], p = x[0];
                x[0] ^= tudo ^ xtime(x[0] ^ x[1]);
                x[1] ^= tudo ^ xtime(x[1] ^ x[2]);
                x[2] ^= tudo ^ xtime(x[2] ^ x[3]);
                x[3] ^= tudo ^ xtime(x[3] ^ p);
            }
        for (int i = 0; i < 16; i++) b[i] = t[i] ^ a->rk[16 * r + i];
    }
    memcpy(s, b, 16);
}

/* ------------------------------------------------------------ GCM */

static void gf_mult(uint8_t y[16], const uint8_t h[16])
{
    uint32_t z[4] = { 0, 0, 0, 0 }, v[4];
    for (int i = 0; i < 4; i++)
        v[i] = (uint32_t)h[4 * i] << 24 | (uint32_t)h[4 * i + 1] << 16 | (uint32_t)h[4 * i + 2] << 8 | h[4 * i + 3];
    for (int i = 0; i < 128; i++) {
        if (y[i >> 3] & (0x80 >> (i & 7))) { z[0] ^= v[0]; z[1] ^= v[1]; z[2] ^= v[2]; z[3] ^= v[3]; }
        uint32_t baixo = v[3] & 1;
        v[3] = v[3] >> 1 | v[2] << 31;
        v[2] = v[2] >> 1 | v[1] << 31;
        v[1] = v[1] >> 1 | v[0] << 31;
        v[0] >>= 1;
        if (baixo) v[0] ^= 0xE1000000;
    }
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) y[4 * i + k] = (uint8_t)(z[i] >> (24 - 8 * k));
}

static void ghash_somar(uint8_t y[16], const uint8_t h[16], const uint8_t *d, unsigned n)
{
    while (n) {
        unsigned k = n < 16 ? n : 16;
        for (unsigned i = 0; i < k; i++) y[i] ^= d[i];
        gf_mult(y, h);
        d += k; n -= k;
    }
}

static void gcm_marca(const aes_t *a, const uint8_t iv[12], const uint8_t *aad, unsigned na,
                      const uint8_t *c, unsigned n, uint8_t marca[16])
{
    uint8_t h[16], y[16], z[16], j0[16], tam[16];
    memset(z, 0, 16);
    aes_bloco(a, z, h);
    memset(y, 0, 16);
    ghash_somar(y, h, aad, na);
    ghash_somar(y, h, c, n);
    memset(tam, 0, 16);
    uint64_t ba = (uint64_t)na * 8, bc = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) { tam[i] = (uint8_t)(ba >> (56 - 8 * i)); tam[8 + i] = (uint8_t)(bc >> (56 - 8 * i)); }
    ghash_somar(y, h, tam, 16);
    memcpy(j0, iv, 12);
    j0[12] = j0[13] = j0[14] = 0; j0[15] = 1;
    aes_bloco(a, j0, z);
    for (int i = 0; i < 16; i++) marca[i] = y[i] ^ z[i];
}

static void gcm_ctr(const aes_t *a, const uint8_t iv[12], uint8_t *d, unsigned n)
{
    uint8_t ctr[16], k[16];
    uint32_t cont = 2;
    memcpy(ctr, iv, 12);
    while (n) {
        for (int i = 0; i < 4; i++) ctr[12 + i] = (uint8_t)(cont >> (24 - 8 * i));
        aes_bloco(a, ctr, k);
        unsigned q = n < 16 ? n : 16;
        for (unsigned i = 0; i < q; i++) d[i] ^= k[i];
        d += q; n -= q; cont++;
    }
}

void gcm_cifrar(const aes_t *a, const uint8_t iv[12], const uint8_t *aad, unsigned na, uint8_t *d, unsigned n, uint8_t marca[16])
{
    gcm_ctr(a, iv, d, n);
    gcm_marca(a, iv, aad, na, d, n, marca);
}

bool gcm_decifrar(const aes_t *a, const uint8_t iv[12], const uint8_t *aad, unsigned na, uint8_t *d, unsigned n, const uint8_t marca[16])
{
    uint8_t m[16], dif = 0;
    gcm_marca(a, iv, aad, na, d, n, m);
    for (int i = 0; i < 16; i++) dif |= m[i] ^ marca[i];
    if (dif) return false;
    gcm_ctr(a, iv, d, n);
    return true;
}

/* ------------------------------------------------------------ X25519 */

/* Números módulo 2^255 - 19 em oito palavras de 32 bits. */
typedef uint32_t fe[8];

static void fe_dobrar_resto(uint32_t *r, uint32_t c)
{
    for (int volta = 0; volta < 2; volta++) {
        uint64_t v = (uint64_t)c * 38;
        for (int i = 0; i < 8; i++) { v += r[i]; r[i] = (uint32_t)v; v >>= 32; }
        c = (uint32_t)v;
    }
}

static void fe_soma(fe r, const fe a, const fe b)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) { v += (uint64_t)a[i] + b[i]; r[i] = (uint32_t)v; v >>= 32; }
    fe_dobrar_resto(r, (uint32_t)v);
}

static void fe_sub(fe r, const fe a, const fe b)
{
    uint32_t emp = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t v = (uint64_t)a[i] - b[i] - emp;
        r[i] = (uint32_t)v;
        emp = (uint32_t)(v >> 32) & 1;
    }
    /* Passou de zero: 2^256 = 38, então tira 38. */
    for (int volta = 0; volta < 2 && emp; volta++) {
        uint32_t tira = 38;
        emp = 0;
        for (int i = 0; i < 8; i++) {
            uint64_t v = (uint64_t)r[i] - tira;
            r[i] = (uint32_t)v;
            tira = (uint32_t)(v >> 32) & 1;
        }
        emp = tira;
    }
}

static void fe_mult(fe r, const fe a, const fe b)
{
    uint32_t t[16];
    memset(t, 0, sizeof t);
    for (int i = 0; i < 8; i++) {
        uint64_t c = 0;
        for (int k = 0; k < 8; k++) {
            c += (uint64_t)a[i] * b[k] + t[i + k];
            t[i + k] = (uint32_t)c;
            c >>= 32;
        }
        t[i + 8] = (uint32_t)c;
    }
    uint64_t c = 0;
    for (int i = 0; i < 8; i++) {
        c += (uint64_t)t[i + 8] * 38 + t[i];
        r[i] = (uint32_t)c;
        c >>= 32;
    }
    fe_dobrar_resto(r, (uint32_t)c);
}

static void fe_troca(fe a, fe b, uint32_t sim)
{
    uint32_t m = 0 - sim;
    for (int i = 0; i < 8; i++) { uint32_t t = m & (a[i] ^ b[i]); a[i] ^= t; b[i] ^= t; }
}

/* a^(p-2): p-2 tem todos os bits ligados, menos os bits 2 e 4. */
static void fe_inverso(fe r, const fe a)
{
    fe c;
    memcpy(c, a, sizeof c);
    for (int i = 253; i >= 0; i--) {
        fe_mult(c, c, c);
        if (i != 2 && i != 4) fe_mult(c, c, a);
    }
    memcpy(r, c, sizeof c);
}

void x25519(uint8_t saida[32], const uint8_t escalar[32], const uint8_t ponto[32])
{
    uint8_t k[32];
    fe x1, x2, z2, x3, z3, A, AA, B, BB, E, C, D, DA, CB, a24, t;
    memcpy(k, escalar, 32);
    k[0] &= 248; k[31] &= 127; k[31] |= 64;
    for (int i = 0; i < 8; i++)
        x1[i] = (uint32_t)ponto[4 * i] | (uint32_t)ponto[4 * i + 1] << 8 | (uint32_t)ponto[4 * i + 2] << 16 | (uint32_t)ponto[4 * i + 3] << 24;
    x1[7] &= 0x7FFFFFFF;
    memset(x2, 0, sizeof x2); x2[0] = 1;
    memset(z2, 0, sizeof z2);
    memcpy(x3, x1, sizeof x3);
    memset(z3, 0, sizeof z3); z3[0] = 1;
    memset(a24, 0, sizeof a24); a24[0] = 121665;
    uint32_t troca = 0;
    for (int i = 254; i >= 0; i--) {
        uint32_t bit = (k[i >> 3] >> (i & 7)) & 1;
        troca ^= bit;
        fe_troca(x2, x3, troca);
        fe_troca(z2, z3, troca);
        troca = bit;
        fe_soma(A, x2, z2);  fe_mult(AA, A, A);
        fe_sub(B, x2, z2);   fe_mult(BB, B, B);
        fe_sub(E, AA, BB);
        fe_soma(C, x3, z3);  fe_sub(D, x3, z3);
        fe_mult(DA, D, A);   fe_mult(CB, C, B);
        fe_soma(t, DA, CB);  fe_mult(x3, t, t);
        fe_sub(t, DA, CB);   fe_mult(t, t, t);  fe_mult(z3, x1, t);
        fe_mult(x2, AA, BB);
        fe_mult(t, a24, E);  fe_soma(t, AA, t);  fe_mult(z2, E, t);
    }
    fe_troca(x2, x3, troca);
    fe_troca(z2, z3, troca);
    fe_inverso(z2, z2);
    fe_mult(x2, x2, z2);
    /* Forma final: tira p enquanto couber. */
    for (int volta = 0; volta < 2; volta++) {
        uint32_t s[8];
        uint64_t v = 19;
        for (int i = 0; i < 8; i++) { v += x2[i]; s[i] = (uint32_t)v; v >>= 32; }
        if (v || (s[7] >> 31)) { s[7] &= 0x7FFFFFFF; memcpy(x2, s, sizeof s); }
    }
    for (int i = 0; i < 8; i++)
        for (int b = 0; b < 4; b++) saida[4 * i + b] = (uint8_t)(x2[i] >> (8 * b));
}

/* ---- SHA-512 e SHA-384: usados só nas assinaturas de certificados ---- */
static const uint64_t k512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL };

static uint64_t rr64(uint64_t x, int n) { return x >> n | x << (64 - n); }

static void bloco512(uint64_t *h, const uint8_t *p)
{
    uint64_t w[80], v[8];
    for (int i = 0; i < 16; i++) {
        uint64_t t = 0;
        for (int k = 0; k < 8; k++) t = t << 8 | p[i * 8 + k];
        w[i] = t;
    }
    for (int i = 16; i < 80; i++) {
        uint64_t a = w[i - 15], b = w[i - 2];
        w[i] = w[i - 16] + (rr64(a, 1) ^ rr64(a, 8) ^ a >> 7) + w[i - 7] + (rr64(b, 19) ^ rr64(b, 61) ^ b >> 6);
    }
    memcpy(v, h, sizeof v);
    for (int i = 0; i < 80; i++) {
        uint64_t t1 = v[7] + (rr64(v[4], 14) ^ rr64(v[4], 18) ^ rr64(v[4], 41)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k512[i] + w[i];
        uint64_t t2 = (rr64(v[0], 28) ^ rr64(v[0], 34) ^ rr64(v[0], 39)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        for (int k = 7; k > 0; k--) v[k] = v[k - 1];
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) h[i] += v[i];
}

static void sha2_grande(uint64_t *h, const void *d, unsigned n, uint8_t *saida, int tam)
{
    const uint8_t *p = d;
    uint8_t b[256];
    unsigned resto = n;
    while (resto >= 128) { bloco512(h, p); p += 128; resto -= 128; }
    memset(b, 0, sizeof b);
    memcpy(b, p, resto);
    b[resto] = 0x80;
    unsigned t = resto < 112 ? 128 : 256;
    uint32_t bits = n << 3;
    b[t - 5] = (uint8_t)(n >> 29);
    b[t - 4] = (uint8_t)(bits >> 24); b[t - 3] = (uint8_t)(bits >> 16); b[t - 2] = (uint8_t)(bits >> 8); b[t - 1] = (uint8_t)bits;
    bloco512(h, b);
    if (t == 256) bloco512(h, b + 128);
    for (int i = 0; i < tam; i++) saida[i] = (uint8_t)(h[i / 8] >> (56 - 8 * (i % 8)));
}

void sha512(const void *d, unsigned n, uint8_t saida[64])
{
    uint64_t h[8] = { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                      0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };
    sha2_grande(h, d, n, saida, 64);
}

void sha384(const void *d, unsigned n, uint8_t saida[48])
{
    uint64_t h[8] = { 0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
                      0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL };
    sha2_grande(h, d, n, saida, 48);
}
