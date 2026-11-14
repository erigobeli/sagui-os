/* js_num.h - números do JavaScript em IEEE 754 de dupla precisão.
 *
 * Sem libgcc nem libm: inteiros grandes (bignum) para as conversões exatas,
 * SSE2 para a aritmética (o js.o é compilado com -mfpmath=sse) e a FPU x87
 * (long double, 64 bits de mantissa) para as funções do Math.
 *   - número -> texto: dígitos mais curtos que voltam ao mesmo número
 *     (Burger & Dybvig, "Printing Floating-Point Numbers Quickly and Accurately");
 *   - texto -> número: arredondamento correto (meio para o par) com bignum;
 *   - toFixed/toPrecision/toExponential: expansão decimal exata do binário. */

typedef union { double d; u64 b; } dbits;
static inline u64 d_bits(double d) { dbits x; x.d = d; return x.b; }
static inline double d_de(u64 b) { dbits x; x.b = b; return x.d; }
static inline int d_nan(double d) { return d != d; }
static inline int d_fin(double d) { return ((d_bits(d) >> 52) & 0x7FF) != 0x7FF; }
#define D_NAN d_de(0x7FF8000000000000ULL)
#define D_INF d_de(0x7FF0000000000000ULL)

/* ------------------------------------------------------------ bignum */
#define BG 160   /* 5120 bits */
typedef struct { uint32_t w[BG]; int n; } big;
static void bg_zero(big *a) { a->n = 0; }
static void bg_u64(big *a, u64 v) { a->n = 0; while (v) { a->w[a->n++] = (uint32_t)v; v >>= 32; } }
static void bg_norm(big *a) { while (a->n && !a->w[a->n - 1]) a->n--; }
static int bg_cmp(const big *a, const big *b)
{
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--) if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}
static void bg_add(big *r, const big *a, const big *b)   /* r pode ser a ou b */
{
    int n = a->n > b->n ? a->n : b->n; u64 c = 0;
    for (int i = 0; i < n; i++) { u64 s = c + (i < a->n ? a->w[i] : 0) + (i < b->n ? b->w[i] : 0); r->w[i] = (uint32_t)s; c = s >> 32; }
    r->n = n;
    if (c && n < BG) r->w[r->n++] = (uint32_t)c;
}
static void bg_sub(big *a, const big *b)   /* a -= b, a >= b */
{
    int64_t e = 0;
    for (int i = 0; i < a->n; i++) { int64_t s = (int64_t)a->w[i] - (i < b->n ? b->w[i] : 0) + e; a->w[i] = (uint32_t)s; e = s < 0 ? -1 : 0; }
    bg_norm(a);
}
static void bg_mul_peq(big *a, uint32_t m)
{
    u64 c = 0;
    for (int i = 0; i < a->n; i++) { u64 p = (u64)a->w[i] * m + c; a->w[i] = (uint32_t)p; c = p >> 32; }
    if (c && a->n < BG) a->w[a->n++] = (uint32_t)c;
}
static void bg_mul_p10(big *a, int e) { while (e >= 9) { bg_mul_peq(a, 1000000000u); e -= 9; } static const uint32_t p[9] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 }; if (e) bg_mul_peq(a, p[e]); }
static void bg_mul_p5(big *a, int e) { while (e >= 13) { bg_mul_peq(a, 1220703125u); e -= 13; } uint32_t m = 1; while (e--) m *= 5; if (m > 1) bg_mul_peq(a, m); }
static void bg_shl(big *a, int s)
{
    if (!a->n || s <= 0) return;
    int w = s / 32, b = s % 32;
    int n = a->n + w + 1; if (n > BG) n = BG;
    for (int i = n - 1; i >= 0; i--) {
        int k = i - w;
        uint32_t hi = k >= 0 && k < a->n ? a->w[k] : 0, lo = k - 1 >= 0 && k - 1 < a->n ? a->w[k - 1] : 0;
        a->w[i] = b ? (hi << b) | (lo >> (32 - b)) : hi;
    }
    a->n = n; bg_norm(a);
}
static int bg_bits(const big *a) { if (!a->n) return 0; return (a->n - 1) * 32 + 32 - __builtin_clz(a->w[a->n - 1]); }
/* divide por um uint32 (divl: sem __udivdi3) e devolve o resto */
static uint32_t bg_div_peq(big *a, uint32_t d)
{
    uint32_t r = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        uint32_t q, lo = a->w[i];
        __asm__("divl %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(r), "rm"(d));
        a->w[i] = q;
    }
    bg_norm(a);
    return r;
}
/* q = floor(a / b) para q pequeno (até 9): subtrai */
static int bg_quoc_peq(big *a, const big *b) { int q = 0; while (bg_cmp(a, b) >= 0) { bg_sub(a, b); q++; } return q; }

/* ------------------------------------------------------------ montagem do double */
/* valor = q * 2^e2 (+ algo abaixo, se sticky), arredondado ao par mais próximo */
static double d_compor(u64 q, int e2, int sticky)
{
    if (!q) return 0.0;
    while (!(q >> 63)) { q <<= 1; e2--; }
    int E = e2 + 63;
    if (E > 1023) return D_INF;
    int guarda = E >= -1022 ? 53 : 53 - (-1022 - E);
    if (guarda < 0) return 0.0;
    int drop = 64 - guarda;
    u64 m, resto, meio;
    if (drop >= 64) { m = 0; resto = q; meio = (u64)1 << 63; if (drop > 64) { sticky |= resto != 0; resto = 0; } }
    else { m = q >> drop; resto = q & (((u64)1 << drop) - 1); meio = (u64)1 << (drop - 1); }
    if (resto > meio || (resto == meio && (sticky || (m & 1)))) m++;
    u64 bits;
    if (E >= -1022) {
        if (m >> 53) { m >>= 1; E++; if (E > 1023) return D_INF; }
        bits = ((u64)(E + 1023) << 52) | (m & (((u64)1 << 52) - 1));
    } else bits = m;   /* subnormal (se arredondou para 2^52 vira o menor normal sozinho) */
    return d_de(bits);
}
static double bg_para_double(const big *a, int e2, int sticky)
{
    int nb = bg_bits(a);
    if (!nb) return 0.0;
    if (nb <= 64) { u64 q = a->n > 1 ? ((u64)a->w[1] << 32) | a->w[0] : a->w[0]; return d_compor(q, e2, sticky); }
    int s = nb - 64;
    big t = *a;
    /* pega os 64 bits do topo; o resto vira sticky */
    int w = s / 32, b = s % 32;
    for (int i = 0; i < w && !sticky; i++) if (t.w[i]) sticky = 1;
    if (b && (t.w[w] & ((1u << b) - 1))) sticky = 1;
    /* janela de 96 bits a partir da palavra w, deslocada b bits: são exatamente os 64 do topo */
    u64 q, hi = w + 2 < t.n ? t.w[w + 2] : 0, mi = w + 1 < t.n ? t.w[w + 1] : 0, lo = t.w[w];
    q = b ? (hi << (64 - b)) | (mi << (32 - b)) | (lo >> b) : (mi << 32) | lo;
    return d_compor(q, e2 + s, sticky);
}

/* ------------------------------------------------------------ texto -> número */
/* lê decimal JS (dígitos, ponto, expoente); *usado = bytes; NaN se nada */
static double ler_decimal(const char *s, int n, int *usado)
{
    int i = 0, sig = 0, exp10 = 0, sticky = 0, tem = 0;
    static big d; bg_zero(&d);
    while (i < n && s[i] == '0') { i++; tem = 1; }
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        tem = 1;
        if (sig < 780) { bg_mul_peq(&d, 10); big t; bg_u64(&t, (u64)(s[i] - '0')); bg_add(&d, &d, &t); sig++; }
        else { exp10++; if (s[i] != '0') sticky = 1; }
        i++;
    }
    if (i < n && s[i] == '.') {
        int j = i + 1;
        if (j < n && s[j] >= '0' && s[j] <= '9') tem = 1;
        if (tem) {
            i++;
            while (i < n && s[i] >= '0' && s[i] <= '9') {
                if (!d.n && s[i] == '0') { exp10--; i++; continue; }
                if (sig < 780) { bg_mul_peq(&d, 10); big t; bg_u64(&t, (u64)(s[i] - '0')); bg_add(&d, &d, &t); exp10--; sig++; }
                else if (s[i] != '0') sticky = 1;
                i++;
            }
        }
    }
    if (!tem) { if (usado) *usado = 0; return D_NAN; }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        int j = i + 1, neg = 0, x = 0, dg = 0;
        if (j < n && (s[j] == '+' || s[j] == '-')) neg = s[j++] == '-';
        while (j < n && s[j] >= '0' && s[j] <= '9') { if (x < 100000) x = x * 10 + s[j] - '0'; j++; dg = 1; }
        if (dg) { exp10 += neg ? -x : x; i = j; }
    }
    if (usado) *usado = i;
    if (!d.n) return 0.0;
    int bits = bg_bits(&d);
    if (exp10 + (bits * 30103) / 100000 > 310) return D_INF;
    if (exp10 + (bits * 30103) / 100000 < -345) return 0.0;
    if (exp10 >= 0) { bg_mul_p10(&d, exp10); return bg_para_double(&d, 0, sticky); }
    /* d / 10^k: quociente de ~66 bits por divisão binária longa */
    static big T, R, U;
    bg_u64(&T, 1); bg_mul_p10(&T, -exp10);
    R = d;
    int k = 63 - bg_bits(&R) + bg_bits(&T);   /* quociente em [2^62, 2^64) */
    if (k > 0) bg_shl(&R, k);
    else if (k < 0) bg_shl(&T, -k);
    u64 q = 0;
    for (int b = 63; b >= 0; b--) {
        U = T; bg_shl(&U, b);
        if (bg_cmp(&R, &U) >= 0) { bg_sub(&R, &U); q |= (u64)1 << b; }
    }
    return d_compor(q, -k, sticky || R.n);
}
/* inteiro em base 2..36 (prefixos 0x/0o/0b, parseInt): exato até onde o double guarda, arredondado depois */
static double ler_base(const char *s, int n, int base, int *usado)
{
    static big d; bg_zero(&d);
    int i = 0, dg = 0;
    for (; i < n; i++) {
        char c = s[i]; int v = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'z' ? (c | 32) - 'a' + 10 : 99;
        if (v >= base) break;
        if (bg_bits(&d) < BG * 32 - 40) { bg_mul_peq(&d, (uint32_t)base); big t; bg_u64(&t, (u64)v); bg_add(&d, &d, &t); }
        dg = 1;
    }
    if (usado) *usado = i;
    return dg ? bg_para_double(&d, 0, 0) : D_NAN;
}

/* ------------------------------------------------------------ número -> texto */
/* decompõe x > 0 finito: x = f * 2^e */
static void d_partes(double x, u64 *f, int *e)
{
    u64 b = d_bits(x);
    int ex = (int)((b >> 52) & 0x7FF);
    u64 m = b & (((u64)1 << 52) - 1);
    if (ex) { *f = m | ((u64)1 << 52); *e = ex - 1075; } else { *f = m; *e = -1074; }
}
/* dígitos mais curtos: d[0..n) e k com x = 0.d1d2... * 10^k */
static int d_curto(double x, char *d, int *k)
{
    u64 f; int e; d_partes(x, &f, &e);
    static big r, s, mm, mp, t;
    int par = !(f & 1), mpm_dif = 0;
    if (e >= 0) {
        if (f != ((u64)1 << 52)) { bg_u64(&r, f); bg_shl(&r, e + 1); bg_u64(&s, 2); bg_u64(&mp, 1); bg_shl(&mp, e); mm = mp; }
        else { bg_u64(&r, f); bg_shl(&r, e + 2); bg_u64(&s, 4); bg_u64(&mp, 1); bg_shl(&mp, e + 1); bg_u64(&mm, 1); bg_shl(&mm, e); mpm_dif = 1; }
    } else {
        if (e == -1074 || f != ((u64)1 << 52)) { bg_u64(&r, f); bg_shl(&r, 1); bg_u64(&s, 1); bg_shl(&s, 1 - e); bg_u64(&mp, 1); bg_u64(&mm, 1); }
        else { bg_u64(&r, f); bg_shl(&r, 2); bg_u64(&s, 1); bg_shl(&s, 2 - e); bg_u64(&mp, 2); bg_u64(&mm, 1); mpm_dif = 1; }
    }
    (void)mpm_dif;
    int len = 64 - __builtin_clzll(f);
    double est = (double)(e + len - 1) * 0.30102999566398114 - 1e-10;
    int kk = (int)est; if ((double)kk < est) kk++;   /* teto */
    if (kk >= 0) bg_mul_p10(&s, kk); else { bg_mul_p10(&r, -kk); bg_mul_p10(&mp, -kk); bg_mul_p10(&mm, -kk); }
    /* acerto da estimativa */
    bg_add(&t, &r, &mp);
    int c = bg_cmp(&t, &s);
    if (par ? c >= 0 : c > 0) kk++;
    else { bg_mul_peq(&r, 10); bg_mul_peq(&mp, 10); bg_mul_peq(&mm, 10); }
    int n = 0;
    for (;;) {
        int dig = bg_quoc_peq(&r, &s);
        int c1 = bg_cmp(&r, &mm);
        int tc1 = par ? c1 <= 0 : c1 < 0;
        bg_add(&t, &r, &mp);
        int c2 = bg_cmp(&t, &s);
        int tc2 = par ? c2 >= 0 : c2 > 0;
        if (!tc1 && !tc2) { d[n++] = (char)('0' + dig); bg_mul_peq(&r, 10); bg_mul_peq(&mp, 10); bg_mul_peq(&mm, 10); if (n > 20) break; continue; }
        if (tc1 && !tc2) d[n++] = (char)('0' + dig);
        else if (!tc1 && tc2) d[n++] = (char)('0' + dig + 1);
        else { t = r; bg_shl(&t, 1); d[n++] = (char)('0' + (bg_cmp(&t, &s) < 0 ? dig : dig + 1)); }
        break;
    }
    *k = kk;
    return n;
}
/* Number::toString (radix 10) */
static int d_para_texto(double x, char *b)
{
    if (d_nan(x)) { memcpy(b, "NaN", 4); return 3; }
    if (x == 0) { b[0] = '0'; b[1] = 0; return 1; }
    int k = 0, p = 0;
    if (x < 0) { b[p++] = '-'; x = -x; }
    if (!d_fin(x)) { memcpy(b + p, "Infinity", 9); return p + 8; }
    char d[32]; int kk, n = d_curto(x, d, &kk);
    if (n <= kk && kk <= 21) { memcpy(b + p, d, (size_t)n); p += n; for (int i = n; i < kk; i++) b[p++] = '0'; }
    else if (0 < kk && kk <= 21) { memcpy(b + p, d, (size_t)kk); p += kk; b[p++] = '.'; memcpy(b + p, d + kk, (size_t)(n - kk)); p += n - kk; }
    else if (-6 < kk && kk <= 0) { b[p++] = '0'; b[p++] = '.'; for (int i = 0; i < -kk; i++) b[p++] = '0'; memcpy(b + p, d, (size_t)n); p += n; }
    else {
        int ex = kk - 1;
        b[p++] = d[0];
        if (n > 1) { b[p++] = '.'; memcpy(b + p, d + 1, (size_t)(n - 1)); p += n - 1; }
        b[p++] = 'e'; b[p++] = ex < 0 ? '-' : '+';
        if (ex < 0) ex = -ex;
        char t[8]; int q = 0; do { t[q++] = (char)('0' + ex % 10); ex /= 10; } while (ex);
        while (q) b[p++] = t[--q];
    }
    (void)k;
    b[p] = 0;
    return p;
}
/* expansão decimal exata de x >= 0 finito: inteiros em ip (texto), fração em fp */
static void d_exato(double x, char *ip, int *nip, char *fp, int *nfp)
{
    u64 f; int e; d_partes(x, &f, &e);
    static big a;
    bg_u64(&a, f);
    int casas = 0;
    if (e >= 0) bg_shl(&a, e); else { bg_mul_p5(&a, -e); casas = -e; }   /* f / 2^k = f * 5^k / 10^k */
    static char dig[2000]; int nd = 0;
    if (!a.n) dig[nd++] = '0';
    while (a.n) { uint32_t r = bg_div_peq(&a, 1000000000u); for (int i = 0; i < 9; i++) { dig[nd++] = (char)('0' + r % 10); r /= 10; } }
    while (nd > 1 && dig[nd - 1] == '0') nd--;
    /* dig está ao contrário; as últimas 'casas' posições são a fração */
    while (nd < casas + 1) dig[nd++] = '0';
    *nip = 0; for (int i = nd - 1; i >= casas; i--) ip[(*nip)++] = dig[i];
    while (*nip > 1 && ip[0] == '0') { memmove(ip, ip + 1, (size_t)--(*nip)); }
    *nfp = 0; for (int i = casas - 1; i >= 0; i--) fp[(*nfp)++] = dig[i];
}
/* arredonda "metade para cima" o texto decimal d[0..n) mantendo m dígitos; devolve 1 se cresceu um dígito */
static int arred_decimal(char *d, int n, int m)
{
    if (m >= n) { for (int i = n; i < m; i++) d[i] = '0'; return 0; }
    if (d[m] >= '5') {
        int i = m - 1;
        while (i >= 0 && d[i] == '9') d[i--] = '0';
        if (i < 0) { memmove(d + 1, d, (size_t)m); d[0] = '1'; return 1; }
        d[i]++;
    }
    return 0;
}

/* ------------------------------------------------------------ Math (x87, long double) */
typedef long double ld;
static ld x_sin(ld x) { ld r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static ld x_cos(ld x) { ld r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
static ld x_atan2(ld y, ld x) { ld r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static ld x_log2(ld x) { ld r; __asm__("fld1; fxch; fyl2x" : "=t"(r) : "0"(x)); return r; }
static ld x_ln(ld x) { ld r; __asm__("fldln2; fxch; fyl2x" : "=t"(r) : "0"(x)); return r; }
static ld x_ln1p(ld x) { ld r; __asm__("fldln2; fxch; fyl2xp1" : "=t"(r) : "0"(x)); return r; }   /* |x| < 1 - sqrt(2)/2 */
static ld x_sqrt(ld x) { ld r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
static ld x_rnd(ld x) { ld r; __asm__("frndint" : "=t"(r) : "0"(x)); return r; }
static ld x_esc(ld x, ld e) { ld r; __asm__("fscale" : "=t"(r) : "0"(x), "u"(e)); return r; }
static ld x_2xm1(ld x) { ld r; __asm__("f2xm1" : "=t"(r) : "0"(x)); return r; }   /* |x| <= 1 */
/* resto exato (fprem) */
static ld x_rem(ld x, ld y) { ld r; __asm__("1: fprem; fnstsw %%ax; testw $0x400, %%ax; jnz 1b" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc"); return r; }
static ld x_rem1(ld x, ld y) { ld r; __asm__("1: fprem1; fnstsw %%ax; testw $0x400, %%ax; jnz 1b" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc"); return r; }
static const ld X_LOG2E = 1.44269504088896340735992468100189214L;
static const ld X_2PI = 6.28318530717958647692528676655900577L;
static ld x_exp2(ld t)
{
    if (t > 16500) return (ld)D_INF;
    if (t < -16500) return 0;
    ld i = x_rnd(t), fr = t - i;
    return x_esc(x_2xm1(fr) + 1, i);
}
static ld x_exp(ld x) { return x_exp2(x * X_LOG2E); }
static ld x_expm1(ld x)
{
    ld t = x * X_LOG2E;
    if (t > -1 && t < 1) return x_2xm1(t);
    return x_exp2(t) - 1;
}
static ld x_log1p(ld x)
{
    if (x > -0.29 && x < 0.29) return x_ln1p(x);
    return x_ln(1 + x);
}
/* redução para as trigonométricas (fsin vale até 2^63) */
static ld x_reduz(ld x) { if (x > 9.2e18L || x < -9.2e18L) return x_rem1(x, X_2PI); return x; }
static double m_floor(double x)
{
    if (!d_fin(x) || x == 0) return x;
    u64 b = d_bits(x); int ex = (int)((b >> 52) & 0x7FF) - 1023;
    if (ex >= 52) return x;
    if (ex < 0) return x < 0 ? -1.0 : (b >> 63 ? -0.0 : 0.0);
    u64 m = (((u64)1 << 52) - 1) >> ex;
    if (!(b & m)) return x;
    if (b >> 63) b += m + 1;
    return d_de(b & ~m) ;
}
static double m_trunc(double x) { return x < 0 ? -m_floor(-x) : m_floor(x); }
static double m_ceil(double x) { double r = -m_floor(-x); return r == 0 && x < 0 ? -0.0 : r; }
static double m_round(double x)   /* Math.round: meio para +infinito; -0 preservado */
{
    if (!d_fin(x) || x == 0) return x;
    if (x > 0 && x < 0.5) return 0.0;
    if (x < 0 && x >= -0.5) return -0.0;
    double f = m_floor(x);
    return x - f >= 0.5 ? f + 1 : f;
}
static double m_pow(double x, double y)
{
    if (d_nan(y)) return D_NAN;
    if (y == 0) return 1;
    if (d_nan(x)) return D_NAN;
    double ay = y < 0 ? -y : y, ax = x < 0 ? -x : x;
    int y_int = d_fin(y) && m_floor(y) == y;
    int y_impar = y_int && ay < 9007199254740992.0 && (((u64)ay) & 1);
    if (!d_fin(y)) {
        if (ax == 1) return D_NAN;
        return (ax > 1) == (y > 0) ? D_INF : 0.0;
    }
    if (!d_fin(x)) {
        if (x > 0) return y > 0 ? D_INF : 0.0;
        if (y > 0) return y_impar ? -D_INF : D_INF;
        return y_impar ? -0.0 : 0.0;
    }
    if (x == 0) {
        int neg = d_bits(x) >> 63;
        if (y > 0) return neg && y_impar ? -0.0 : 0.0;
        return neg && y_impar ? -D_INF : D_INF;
    }
    if (x < 0 && !y_int) return D_NAN;
    ld r;
    if (y_int && ay <= 4096) {   /* potência inteira por quadrados, em 64 bits de mantissa */
        ld b = ax; unsigned e = (unsigned)ay; r = 1;
        while (e) { if (e & 1) r *= b; b *= b; e >>= 1; }
        if (y < 0) r = 1 / r;
    } else r = x_exp2((ld)y * x_log2(ax));
    double d = (double)r;
    return x < 0 && y_impar ? -d : d;
}
/* n_mat(op, a, b): as funções do Math */
static double m_func(int op, double a, double b)
{
    ld x = a;
    switch (op) {
    case 0: return a < 0 ? -a : a == 0 ? 0.0 : a;   /* abs */
    case 1: return m_floor(a);
    case 2: return m_ceil(a);
    case 3: return m_round(a);
    case 4: return m_trunc(a);
    case 5: return d_nan(a) ? a : a > 0 ? 1.0 : a < 0 ? -1.0 : a;   /* sign */
    case 6: return a < 0 ? D_NAN : (double)x_sqrt(x);
    case 7: { if (!d_fin(a) || a == 0) return a; ld r = x_exp2(x_log2(x < 0 ? -x : x) / 3); r = r - (r * r * r - (x < 0 ? -x : x)) / (3 * r * r); return (double)(a < 0 ? -r : r); }   /* cbrt */
    case 8: return d_fin(a) ? (double)x_sin(x_reduz(x)) : D_NAN;
    case 9: return d_fin(a) ? (double)x_cos(x_reduz(x)) : D_NAN;
    case 10: { if (!d_fin(a)) return D_NAN; ld r = x_reduz(x); return (double)(x_sin(r) / x_cos(r)); }
    case 11: return a > 1 || a < -1 ? D_NAN : (double)x_atan2(x, x_sqrt((1 - x) * (1 + x)));   /* asin */
    case 12: return a > 1 || a < -1 ? D_NAN : (double)x_atan2(x_sqrt((1 - x) * (1 + x)), x);   /* acos */
    case 13: return (double)x_atan2(x, 1);   /* atan */
    case 14: return (double)x_atan2(x, (ld)b);   /* atan2(a, b) */
    case 15: if (d_nan(a)) return a; if (a == D_INF) return a; if (a == -D_INF) return 0.0; return (double)x_exp(x);
    case 16: if (d_nan(a) || a < 0) return D_NAN; if (a == 0) return -D_INF; if (a == D_INF) return a; return (double)x_ln(x);
    case 17: if (d_nan(a) || a < 0) return D_NAN; if (a == 0) return -D_INF; if (a == D_INF) return a; return (double)x_log2(x);
    case 18: { if (d_nan(a) || a < 0) return D_NAN; if (a == 0) return -D_INF; if (a == D_INF) return a; ld r; __asm__("fldlg2; fxch; fyl2x" : "=t"(r) : "0"(x)); double d = (double)r; double rd = m_round(d); if (d != rd && (double)(rd - d) < 1e-15 && (double)(d - rd) < 1e-15) { double p = m_pow(10, rd); if (p == a) return rd; } return d; }
    case 19: if (d_nan(a)) return a; if (a == D_INF) return a; if (a == -D_INF) return -1.0; if (a == 0) return a; return (double)x_expm1(x);
    case 20: if (d_nan(a) || a < -1) return D_NAN; if (a == -1) return -D_INF; if (a == 0 || a == D_INF) return a; return (double)x_log1p(x);
    case 21: return d_fin(a) ? (double)((x_expm1(x) - x_expm1(-x)) / 2) : a;   /* sinh */
    case 22: return d_fin(a) ? (double)((x_exp(x) + x_exp(-x)) / 2) : (d_nan(a) ? a : D_INF);   /* cosh */
    case 23: { if (d_nan(a) || a == 0) return a; if (a > 22) return 1.0; if (a < -22) return -1.0; ld e2 = x_expm1(2 * x); return (double)(e2 / (e2 + 2)); }   /* tanh */
    case 24: { if (!d_fin(a) || a == 0) return a; ld ax = x < 0 ? -x : x; ld r = ax > 1e9L ? x_ln(ax) + 0.69314718055994530942L : x_log1p(ax + ax * ax / (1 + x_sqrt(1 + ax * ax))); return (double)(a < 0 ? -r : r); }   /* asinh */
    case 25: if (a < 1 || d_nan(a)) return D_NAN; if (a == D_INF) return a; return (double)x_ln(x + x_sqrt(x * x - 1));   /* acosh */
    case 26: if (a > 1 || a < -1 || d_nan(a)) return D_NAN; if (a == 1) return D_INF; if (a == -1) return -D_INF; if (a == 0) return a; return (double)(x_log1p(2 * x / (1 - x)) / 2);   /* atanh */
    case 27: return m_pow(a, b);
    case 28: { if (!d_fin(a) || !d_fin(b)) { if (a == D_INF || a == -D_INF || b == D_INF || b == -D_INF) return D_INF; return D_NAN; } return (double)x_sqrt((ld)a * a + (ld)b * b); }   /* hypot de 2 */
    case 29: return (double)(float)a;   /* fround */
    case 30: if (!d_fin(b) && !d_nan(b) && d_fin(a)) return a; if (b == 0 || !d_fin(a) || d_nan(b)) return D_NAN; { ld r = x_rem(x, (ld)b); double d = (double)r; return d == 0 ? (a < 0 ? -0.0 : 0.0) : d; }   /* % */
    }
    return D_NAN;
}
