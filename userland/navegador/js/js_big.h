/* js_big.h - BigInt (incluído por js.c): inteiros de tamanho qualquer.
 *
 * Sinal e magnitude, dígitos de 16 bits (as contas cabem em 32 bits: sem
 * divisão de 64 bits no ambiente sem libc). Valores imutáveis, alocados no
 * coletor. Operações bit a bit seguem o complemento de dois infinito da
 * especificação (-1n & 0xffn === 0xffn). */

typedef struct bint { int neg, n; uint16_t d[]; } bint;   /* n dígitos, sem zeros à esquerda; zero: n = 0 */

static bint *bi_novo(int n) { bint *b = novo(sizeof(bint) + (unsigned)(n > 0 ? n : 1) * 2); b->n = n; return b; }
static bint *bi_norm(bint *b) { while (b->n && !b->d[b->n - 1]) b->n--; if (!b->n) b->neg = 0; return b; }
static val vG(bint *b) { val v; v.t = BI; v.n = 0; v.g = b; return v; }
static bint *bi_u32(uint32_t x, int neg) { bint *b = bi_novo(2); b->d[0] = (uint16_t)x; b->d[1] = (uint16_t)(x >> 16); b->neg = neg; return bi_norm(b); }
static bint *bi_copia(const bint *a, int n) { bint *b = bi_novo(n > a->n ? n : a->n); memcpy(b->d, a->d, (size_t)a->n * 2); b->n = a->n; b->neg = a->neg; return b; }

/* inteiro exato de um double (já conferido que é inteiro e finito) */
static bint *bi_de_double(double x)
{
    int neg = x < 0; if (neg) x = -x;
    int n = 0; double t = x; while (t >= 1) { t /= 65536.0; n++; }
    bint *b = bi_novo(n);
    for (int i = n - 1; i >= 0; i--) {
        double p = 1; for (int k = 0; k < i; k++) p *= 65536.0;
        double q = m_func(1, x / p, 0);   /* floor */
        if (q > 65535) q = 65535;
        b->d[i] = (uint16_t)q; x -= q * p;
    }
    b->neg = neg;
    return bi_norm(b);
}
static double bi_para_double(const bint *b)
{
    double r = 0;
    for (int i = b->n - 1; i >= 0; i--) r = r * 65536.0 + b->d[i];
    return b->neg ? -r : r;
}
static int bi_cmp_abs(const bint *a, const bint *b)
{
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--) if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}
static int bi_cmp(const bint *a, const bint *b)
{
    if (a->neg != b->neg) return a->neg ? -1 : 1;
    int c = bi_cmp_abs(a, b);
    return a->neg ? -c : c;
}
static bint *bi_soma_abs(const bint *a, const bint *b)
{
    if (a->n < b->n) { const bint *t = a; a = b; b = t; }
    bint *r = bi_novo(a->n + 1); uint32_t c = 0;
    for (int i = 0; i < a->n; i++) { c += a->d[i] + (i < b->n ? b->d[i] : 0u); r->d[i] = (uint16_t)c; c >>= 16; }
    r->d[a->n] = (uint16_t)c; r->n = a->n + 1;
    return r;
}
static bint *bi_sub_abs(const bint *a, const bint *b)   /* |a| >= |b| */
{
    bint *r = bi_novo(a->n); int32_t c = 0;
    for (int i = 0; i < a->n; i++) { c += (int32_t)a->d[i] - (int32_t)(i < b->n ? b->d[i] : 0u); r->d[i] = (uint16_t)c; c = c < 0 ? -1 : 0; }
    r->n = a->n;
    return r;
}
static bint *bi_soma(const bint *a, const bint *b)
{
    bint *r;
    if (a->neg == b->neg) { r = bi_soma_abs(a, b); r->neg = a->neg; }
    else if (bi_cmp_abs(a, b) >= 0) { r = bi_sub_abs(a, b); r->neg = a->neg; }
    else { r = bi_sub_abs(b, a); r->neg = b->neg; }
    return bi_norm(r);
}
static bint *bi_neg(const bint *a) { bint *r = bi_copia(a, a->n); r->neg = a->n ? !a->neg : 0; return r; }
static bint *bi_sub(const bint *a, const bint *b) { return bi_soma(a, bi_neg(b)); }
static bint *bi_mul(const bint *a, const bint *b)
{
    if (!a->n || !b->n) return bi_novo(0);
    bint *r = bi_novo(a->n + b->n);
    memset(r->d, 0, (size_t)(a->n + b->n) * 2); r->n = a->n + b->n;
    for (int i = 0; i < a->n; i++) {
        uint32_t c = 0;
        for (int j = 0; j < b->n; j++) { c += (uint32_t)a->d[i] * b->d[j] + r->d[i + j]; r->d[i + j] = (uint16_t)c; c >>= 16; }
        for (int k = i + b->n; c; k++) { c += r->d[k]; r->d[k] = (uint16_t)c; c >>= 16; }
    }
    r->neg = a->neg != b->neg;
    return bi_norm(r);
}
/* divisão truncada (como o JS): q e r com o sinal da especificação */
static void bi_divmod(const bint *a, const bint *b, bint **q, bint **r)
{
    if (!b->n) erro("RangeError", "Division by zero", NULL);
    if (bi_cmp_abs(a, b) < 0) { if (q) *q = bi_novo(0); if (r) *r = bi_copia(a, a->n); return; }
    bint *qq = bi_novo(a->n), *rr = bi_novo(b->n + 1);
    memset(qq->d, 0, (size_t)a->n * 2); qq->n = a->n; rr->n = 0;
    if (b->n == 1) {   /* divisor de um dígito: direto */
        uint32_t resto = 0, dv = b->d[0];
        for (int i = a->n - 1; i >= 0; i--) { uint32_t cur = resto << 16 | a->d[i]; qq->d[i] = (uint16_t)(cur / dv); resto = cur % dv; }
        rr->d[0] = (uint16_t)resto; rr->n = 1;
    } else {
        /* longa, bit a bit no resto: simples e certa (BigInts de sites são pequenos) */
        for (int i = a->n * 16 - 1; i >= 0; i--) {
            /* rr = rr * 2 + bit */
            uint32_t c = (a->d[i >> 4] >> (i & 15)) & 1u;
            for (int k = 0; k < rr->n; k++) { uint32_t v = (uint32_t)rr->d[k] << 1 | c; rr->d[k] = (uint16_t)v; c = v >> 16; }
            if (c) rr->d[rr->n++] = (uint16_t)c;
            if (bi_cmp_abs(rr, b) >= 0) {
                int32_t br = 0;
                for (int k = 0; k < rr->n; k++) { br += (int32_t)rr->d[k] - (int32_t)(k < b->n ? b->d[k] : 0u); rr->d[k] = (uint16_t)br; br = br < 0 ? -1 : 0; }
                while (rr->n && !rr->d[rr->n - 1]) rr->n--;
                qq->d[i >> 4] |= (uint16_t)(1u << (i & 15));
            }
        }
    }
    qq->neg = a->neg != b->neg; rr->neg = a->neg;
    if (q) *q = bi_norm(qq);
    if (r) *r = bi_norm(rr);
}
static bint *bi_pow(const bint *a, const bint *e)
{
    if (e->neg) erro("RangeError", "Exponent must be non-negative", NULL);
    bint *r = bi_u32(1, 0), *b = bi_copia(a, a->n);
    for (int i = 0; i < e->n * 16; i++) {
        if ((e->d[i >> 4] >> (i & 15)) & 1u) r = bi_mul(r, b);
        if (i + 1 < e->n * 16) { int resta = 0; for (int k = i + 1; k < e->n * 16; k++) if ((e->d[k >> 4] >> (k & 15)) & 1u) { resta = 1; break; } if (!resta) break; b = bi_mul(b, b); }
    }
    return r;
}
/* complemento de dois em n dígitos (n já cabe o número e o sinal) */
static void bi_c2(const bint *a, uint16_t *o, int n)
{
    for (int i = 0; i < n; i++) o[i] = i < a->n ? a->d[i] : 0;
    if (!a->neg) return;
    uint32_t c = 1;
    for (int i = 0; i < n; i++) { c += (uint16_t)~o[i]; o[i] = (uint16_t)c; c >>= 16; }
}
static bint *bi_de_c2(const uint16_t *o, int n)
{
    bint *r = bi_novo(n); r->n = n;
    int neg = n && (o[n - 1] & 0x8000);
    if (!neg) { memcpy(r->d, o, (size_t)n * 2); return bi_norm(r); }
    uint32_t c = 1;
    for (int i = 0; i < n; i++) { c += (uint16_t)~o[i]; r->d[i] = (uint16_t)c; c >>= 16; }
    r->neg = 1;
    return bi_norm(r);
}
static bint *bi_bits(int op, const bint *a, const bint *b)
{
    int n = (a->n > b->n ? a->n : b->n) + 1;
    uint16_t *x = novo((unsigned)n * 2), *y = novo((unsigned)n * 2);
    bi_c2(a, x, n); bi_c2(b, y, n);
    for (int i = 0; i < n; i++) x[i] = (uint16_t)(op == '&' ? x[i] & y[i] : op == '|' ? x[i] | y[i] : x[i] ^ y[i]);
    return bi_de_c2(x, n);
}
static bint *bi_shl(const bint *a, long long s)
{
    if (s < 0) {   /* >>: piso (arredonda para -infinito) */
        s = -s;
        if (!a->neg) {
            int w = (int)(s >> 4), bt = (int)(s & 15);
            if (w >= a->n) return bi_novo(0);
            bint *r = bi_novo(a->n - w); r->n = a->n - w;
            for (int i = 0; i < r->n; i++) { uint32_t v = a->d[i + w] | (uint32_t)(i + w + 1 < a->n ? a->d[i + w + 1] : 0) << 16; r->d[i] = (uint16_t)(v >> bt); }
            return bi_norm(r);
        }
        /* negativo: -((|a| - 1) >> s) - 1 */
        bint *m = bi_shl(bi_sub(bi_neg(a), bi_u32(1, 0)), -s);
        return bi_sub(bi_neg(m), bi_u32(1, 0));
    }
    if (s > 1 << 24) erro("RangeError", "Maximum BigInt size exceeded", NULL);
    int w = (int)(s >> 4), bt = (int)(s & 15);
    bint *r = bi_novo(a->n + w + 1); r->n = a->n + w + 1;
    memset(r->d, 0, (size_t)r->n * 2);
    for (int i = 0; i < a->n; i++) { uint32_t v = (uint32_t)a->d[i] << bt; r->d[i + w] |= (uint16_t)v; r->d[i + w + 1] |= (uint16_t)(v >> 16); }
    r->neg = a->neg;
    return bi_norm(r);
}
/* BigInt.asUintN / asIntN */
static bint *bi_as(int com_sinal, long long bits, const bint *a)
{
    if (bits <= 0) return bi_novo(0);
    int n = (int)((bits + 15) >> 4) + 1;
    if (n < a->n + 1) n = a->n + 1;
    uint16_t *o = novo((unsigned)n * 2);
    bi_c2(a, o, n);
    int w = (int)(bits >> 4), bt = (int)(bits & 15);
    for (int i = 0; i < n; i++) { if (i > w || (i == w && !bt)) o[i] = 0; else if (i == w) o[i] &= (uint16_t)((1u << bt) - 1); }
    bint *r = bi_novo(n); memcpy(r->d, o, (size_t)n * 2); r->n = n; r = bi_norm(r);
    if (com_sinal) {
        int alto = (int)((bits - 1) >> 4), b2 = (int)((bits - 1) & 15);
        if (alto < r->n && (r->d[alto] >> b2) & 1u) r = bi_sub(r, bi_shl(bi_u32(1, 0), bits));
    }
    return r;
}
static str *bi_texto(const bint *a, int base)
{
    if (!a->n) return I("0");
    int cap = a->n * 17 + 2; char *t = novo((unsigned)cap); int k = cap;
    bint *x = bi_copia(a, a->n); x->neg = 0;
    while (x->n) {
        uint32_t resto = 0;
        for (int i = x->n - 1; i >= 0; i--) { uint32_t cur = resto << 16 | x->d[i]; x->d[i] = (uint16_t)(cur / (uint32_t)base); resto = cur % (uint32_t)base; }
        bi_norm(x);
        t[--k] = "0123456789abcdefghijklmnopqrstuvwxyz"[resto];
    }
    if (a->neg) t[--k] = '-';
    return novo_str(t + k, cap - k);
}
/* texto -> BigInt (StringToBigInt): espaços em volta, sinal só no decimal, 0x/0o/0b; NULL se inválido */
static bint *bi_de_texto(const char *s, int n, int literal)
{
    int i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f' || s[i] == '\v')) i++;
    while (n > i && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\f' || s[n - 1] == '\v')) n--;
    if (i == n) return literal ? NULL : bi_novo(0);
    int neg = 0, base = 10;
    if (s[i] == '0' && i + 1 < n && ((s[i + 1] | 32) == 'x' || (s[i + 1] | 32) == 'o' || (s[i + 1] | 32) == 'b')) { base = (s[i + 1] | 32) == 'x' ? 16 : (s[i + 1] | 32) == 'o' ? 8 : 2; i += 2; }
    else if (!literal && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
    if (i == n) return NULL;
    bint *r = bi_novo(0);
    bint *bb = bi_u32((uint32_t)base, 0);
    for (; i < n; i++) {
        char c = s[i]; int d;
        if (c == '_' && literal) continue;
        if (c >= '0' && c <= '9') d = c - '0'; else if ((c | 32) >= 'a' && (c | 32) <= 'z') d = (c | 32) - 'a' + 10; else return NULL;
        if (d >= base) return NULL;
        r = bi_soma(bi_mul(r, bb), bi_u32((uint32_t)d, 0));
    }
    if (neg && r->n) r = bi_neg(r);
    return r;
}
static int eh_inteiro_d(double x) { return !d_nan(x) && x != D_INF && x != -D_INF && m_func(1, x, 0) == x; }

/* BigInt(v) */
static bint *para_big(val v)
{
    v = prim(v);
    switch (v.t) {
    case BI: return v.g;
    case BO: return bi_u32(v.b ? 1 : 0, 0);
    case NU: if (!eh_inteiro_d(v.n)) erro("RangeError", "The number cannot be converted to a BigInt because it is not an integer", NULL); return bi_de_double(v.n);
    case ST: { bint *r = bi_de_texto(v.s->s, v.s->n, 0); if (!r) erro("SyntaxError", "Cannot convert to BigInt: ", v.s->s); return r; }
    default: erro("TypeError", "Cannot convert to BigInt", NULL);
    }
}
/* BigInt com Number (comparação): -1, 0, 1; 2 = NaN */
static int bi_cmp_num(const bint *a, double x)
{
    if (d_nan(x)) return 2;
    if (x == D_INF) return -1;
    if (x == -D_INF) return 1;
    double f = m_func(1, x, 0);   /* floor */
    bint *b = bi_de_double(f);
    int c = bi_cmp(a, b);
    if (c) return c;
    return f < x ? -1 : 0;
}
/* operação aritmética com dois BigInt */
static val bi_op(int op, bint *a, bint *b)
{
    switch (op) {
    case '+': return vG(bi_soma(a, b));
    case '-': return vG(bi_sub(a, b));
    case '*': return vG(bi_mul(a, b));
    case '/': { bint *q; bi_divmod(a, b, &q, NULL); return vG(q); }
    case '%': { bint *r; bi_divmod(a, b, NULL, &r); return vG(r); }
    case OP(O_POW): return vG(bi_pow(a, b));
    case '&': case '|': case '^': return vG(bi_bits(op, a, b));
    case OP(O_SHL): case OP(O_SHR): {
        if (b->n > 2) erro("RangeError", "Maximum BigInt size exceeded", NULL);
        long long s = (long long)bi_para_double(b);
        return vG(bi_shl(a, op == OP(O_SHL) ? s : -s));
    }
    case OP(O_URS): erro("TypeError", "BigInts have no unsigned right shift, use >> instead", NULL);
    default: erro("TypeError", "Cannot mix BigInt and other types, use explicit conversions", NULL);
    }
}

/* ---- funções do JavaScript ---- */
static val n_bigint(val este, val *a, int n) { (void)este; return vG(para_big(n ? a[0] : vU())); }
static bint *este_big(val v) { if (v.t == BI) return v.g; if (v.t == OB) { val p = get(v, I("__primitivo")); if (p.t == BI) return p.g; } erro("TypeError", "BigInt.prototype method called on incompatible receiver", NULL); }
static val n_big_texto(val este, val *a, int n)
{
    bint *b = este_big(este);
    int base = n && a[0].t != U ? (int)to_num(a[0]) : 10;
    if (base < 2 || base > 36) erro("RangeError", "toString() radix must be between 2 and 36", NULL);
    return vS(bi_texto(b, base));
}
static val n_big_valor(val este, val *a, int n) { (void)a; (void)n; return vG(este_big(este)); }
static val n_big_as(int s, val *a, int n)
{
    double bits = n ? to_num(a[0]) : 0;
    if (d_nan(bits)) bits = 0;
    if (bits < 0 || bits > 9007199254740991.0) erro("RangeError", "Invalid value: not (convertible to) a safe integer", NULL);
    return vG(bi_as(s, (long long)bits, para_big(n > 1 ? a[1] : vU())));
}
static val n_big_asint(val este, val *a, int n) { (void)este; return n_big_as(1, a, n); }
static val n_big_asuint(val este, val *a, int n) { (void)este; return n_big_as(0, a, n); }
/* comparação com pelo menos um BigInt (o outro pode ser Number, String ou Boolean) */
static int bi_cmp_v(val a, val b)
{
    if (a.t == BI && b.t == BI) return bi_cmp(a.g, b.g);
    if (a.t == BI) {
        if (b.t == ST) { bint *x = bi_de_texto(b.s->s, b.s->n, 0); return x ? bi_cmp(a.g, x) : 2; }
        if (b.t == BO) return bi_cmp(a.g, bi_u32((uint32_t)b.b, 0));
        if (b.t == NU) return bi_cmp_num(a.g, b.n);
        return 2;
    }
    int c = bi_cmp_v(b, a);
    return c == 2 ? 2 : -c;
}
static int bi_nao_zero(const bint *a) { return a->n != 0; }
