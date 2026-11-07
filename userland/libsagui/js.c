/* js.c - motor de JavaScript do Cipó.
 *
 * Árvore sintática + interpretador, com objetos, protótipos, closures, classes,
 * arrow functions, desestruturação, exceções, expressões regulares e um DOM de
 * verdade montado do HTML. O que dá para escrever em JavaScript mora em
 * js_base.h, que roda no começo de cada página.
 *
 * Limites conhecidos: números são ponto fixo Q43.20 (os programas não usam a
 * FPU), textos são Latin-1, não há coletor (a memória some com a página), async
 * roda de forma síncrona e não há geradores nem módulos. */
#include "sagui.h"
#include "js_base.h"

typedef long long i64;
typedef unsigned long long u64;
#define FR 20
#define UM (1LL << FR)
#define NANV ((i64)0x8000000000000000ULL)
#define INFV 0x7fffffffffffffffLL
#define MEM_MAX (160u * 1024 * 1024)
#define PILHA_TAM (4u * 1024 * 1024)

typedef struct str { int n; unsigned h; struct str *in; short kw, dom; char s[]; } str;
typedef struct obj obj;
enum { U, NUL, BO, NU, ST, OB, AC };
typedef struct { int t; union { i64 n; str *s; obj *o; int b; }; } val;
typedef struct { str *k; val v; } prop;
typedef struct no no;
typedef struct env env;
typedef val (*nat)(val este, val *a, int n);
enum { C_OBJ, C_ARR, C_FN, C_EL };
typedef struct dn dn;
struct obj {
    int cl; obj *proto; prop *p; int np, cp; int *idx, cidx;
    val *a; int na, ca;
    no *fn; env *e; nat f; struct quadro *q; obj *casa; val ligado_este; obj *ligado; val *ligados; int nlig;
    no *campos; dn *d;
};
typedef struct quadro { val este; obj *func; val novo; } quadro;
struct env { env *pai; int n, c; str **k; val *v; };
typedef struct fdef { str *nome; no *params; no *corpo; str **vars; int nv, cv; int seta, expr, usa_args, assinc, gerador; } fdef;
struct no { short k, op; no *a, *b, *c, *d, *prox; str *s; i64 n; fdef *f; };

enum { K_NUM, K_STR, K_TPL, K_ID, K_THIS, K_SUPER, K_NULL, K_TRUE, K_FALSE, K_RE, K_ARR, K_OBJ, K_PROP, K_FUNC, K_CLASS,
       K_SEQ, K_COND, K_BIN, K_AND, K_OR, K_NN, K_UN, K_UPD, K_ASG, K_MEM, K_IDX, K_CALL, K_NEW, K_SPREAD, K_AWAIT, K_TIPO, K_DEL, K_NEWT, K_YIELD,
       S_VAR, S_DECL, S_EXP, S_BLOCK, S_IF, S_FOR, S_FORIN, S_WHILE, S_DO, S_RET, S_BRK, S_CONT, S_THROW, S_TRY, S_SWITCH, S_CASE, S_LABEL, S_FUNC, S_CLASS, S_VAZIO };
enum { KW_NADA, KW_VAR, KW_LET, KW_CONST, KW_FUNCTION, KW_RETURN, KW_IF, KW_ELSE, KW_FOR, KW_WHILE, KW_DO, KW_BREAK, KW_CONTINUE, KW_NEW, KW_DELETE, KW_TYPEOF,
       KW_VOID, KW_THIS, KW_NULL, KW_TRUE, KW_FALSE, KW_IN, KW_INSTANCEOF, KW_THROW, KW_TRY, KW_CATCH, KW_FINALLY, KW_SWITCH, KW_CASE, KW_DEFAULT, KW_CLASS, KW_EXTENDS,
       KW_SUPER, KW_ASYNC, KW_AWAIT, KW_OF, KW_GET, KW_SET, KW_STATIC };
static const char *const kws[] = { "", "var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "break", "continue", "new", "delete", "typeof",
    "void", "this", "null", "true", "false", "in", "instanceof", "throw", "try", "catch", "finally", "switch", "case", "default", "class", "extends",
    "super", "async", "await", "of", "get", "set", "static" };

/* ------------------------------------------------------------ memória */
typedef struct bloco { struct bloco *prox; } bloco;
static bloco *blocos;
static char *ar; static unsigned ar_u, ar_c, ar_total;
static const js_hooks_t *gancho;
static void lancar_fatal(const char *m);

static void *novo(unsigned n)
{
    n = (n + 3) & ~3u;
    if (n > ar_c - ar_u) {
        unsigned t = n + 16 > (2u << 20) ? n + 16 : (2u << 20);
        if (ar_total + t > MEM_MAX) lancar_fatal("memoria");
        bloco *b = aloca(t);
        if (!b) lancar_fatal("memoria");
        memset(b, 0, t);
        b->prox = blocos; blocos = b;
        ar = (char *)b; ar_u = 8; ar_c = t; ar_total += t;
    }
    void *p = ar + ar_u;
    ar_u += n;
    return p;
}

/* ------------------------------------------------------------ números */
static int bits64(u64 x) { unsigned h = (unsigned)(x >> 32), l = (unsigned)x; return h ? 64 - __builtin_clz(h) : l ? 32 - __builtin_clz(l) : 0; }
static u64 udiv(u64 a, u64 b, u64 *resto)
{
    if (!(a >> 32) && !(b >> 32)) { unsigned x = (unsigned)a, y = (unsigned)b; if (resto) *resto = x % y; return x / y; }
    u64 q = 0, r = 0;
    for (int i = bits64(a) - 1; i >= 0; i--) { r = (r << 1) | ((a >> i) & 1); q <<= 1; if (r >= b) { r -= b; q |= 1; } }
    if (resto) *resto = r;
    return q;
}
static i64 de_int(int i) { return (i64)i << FR; }
static int esp(i64 a) { return a == NANV || a == INFV || a == -INFV; }
static i64 nsoma(i64 a, i64 b)
{
    if (esp(a) || esp(b)) { if (a == NANV || b == NANV) return NANV; if (esp(a) && esp(b) && a != b) return NANV; return esp(a) ? a : b; }
    i64 r = (i64)((u64)a + (u64)b);
    if (((a ^ r) & (b ^ r)) < 0) return a < 0 ? -INFV : INFV;
    return r;
}
static i64 nmul(i64 a, i64 b)
{
    if (a == NANV || b == NANV) return NANV;
    int neg = (a < 0) != (b < 0);
    if (esp(a) || esp(b)) return !a || !b ? NANV : neg ? -INFV : INFV;
    u64 x = a < 0 ? -(u64)a : (u64)a, y = b < 0 ? -(u64)b : (u64)b;
    if (bits64(x) + bits64(y) - FR > 62) return neg ? -INFV : INFV;
    u64 xh = x >> FR, xl = x & (UM - 1), yh = y >> FR, yl = y & (UM - 1);
    u64 r = ((xh * yh) << FR) + xh * yl + xl * yh + ((xl * yl) >> FR);
    return neg ? -(i64)r : (i64)r;
}
static i64 ndiv(i64 a, i64 b)
{
    if (a == NANV || b == NANV) return NANV;
    int neg = (a < 0) != (b < 0);
    if (!b) return !a ? NANV : a < 0 ? -INFV : INFV;
    if (esp(a)) return esp(b) ? NANV : neg ? -INFV : INFV;
    if (esp(b)) return 0;
    u64 x = a < 0 ? -(u64)a : (u64)a, y = b < 0 ? -(u64)b : (u64)b, r, q = udiv(x, y, &r);
    if (bits64(q) > 62 - FR) return neg ? -INFV : INFV;
    for (int i = 0; i < FR; i++) { r <<= 1; q <<= 1; if (r >= y) { r -= y; q |= 1; } }
    return neg ? -(i64)q : (i64)q;
}
static i64 nmod(i64 a, i64 b)
{
    if (a == NANV || b == NANV || !b || a == INFV || a == -INFV) return NANV;
    if (esp(b)) return a;
    u64 x = a < 0 ? -(u64)a : (u64)a, y = b < 0 ? -(u64)b : (u64)b, r;
    udiv(x, y, &r);
    return a < 0 ? -(i64)r : (i64)r;
}
static int para_i32(i64 n) { return esp(n) ? 0 : (int)(unsigned)(n >> FR) + (n < 0 && (n & (UM - 1)) ? 1 : 0); }
static i64 ler_numero(const char *s, int n, int *usado)
{
    int i = 0; u64 v = 0; int sat = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') { if (v < 800000000000ULL) v = v * 10 + (u64)(s[i] - '0'); else sat = 1; i++; }
    i64 r = sat || v >> 42 ? INFV : (i64)(v << FR);
    if (i < n && s[i] == '.') {
        i++;
        u64 f = 0, e = 1; int d = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') { if (d < 9) { f = f * 10 + (u64)(s[i] - '0'); e *= 10; d++; } i++; }
        if (r != INFV) r += (i64)udiv((f << FR) + (e >> 1), e, NULL);
    }
    if (i + 1 < n && (s[i] == 'e' || s[i] == 'E') && ((s[i + 1] >= '0' && s[i + 1] <= '9') || ((s[i + 1] == '-' || s[i + 1] == '+') && i + 2 < n && s[i + 2] >= '0' && s[i + 2] <= '9'))) {
        i++;
        int neg = s[i] == '-', x = 0;
        if (s[i] == '-' || s[i] == '+') i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') { if (x < 400) x = x * 10 + s[i] - '0'; i++; }
        for (int k = 0; k < x && r && !esp(r); k++) r = neg ? ndiv(r, de_int(10)) : nmul(r, de_int(10));
    }
    if (usado) *usado = i;
    return r;
}

/* ------------------------------------------------------------ textos */
static str **tab; static int tab_c, tab_n;
static str *novo_str(const char *s, int n)
{
    str *r = novo(sizeof(str) + (unsigned)n + 1);
    if (s) memcpy(r->s, s, (size_t)n);
    r->n = n; r->s[n] = 0;
    return r;
}
static unsigned hash(const char *s, int n) { unsigned h = 2166136261u; for (int i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u; return h | 1; }
static str *intern_n(const char *s, int n)
{
    if ((tab_n + 1) * 2 > tab_c) {
        int c = tab_c ? tab_c * 2 : 8192;
        str **t = novo((unsigned)c * sizeof(str *));
        for (int i = 0; i < tab_c; i++) if (tab[i]) { int j = (int)(tab[i]->h & (unsigned)(c - 1)); while (t[j]) j = (j + 1) & (c - 1); t[j] = tab[i]; }
        tab = t; tab_c = c;
    }
    unsigned h = hash(s, n);
    int j = (int)(h & (unsigned)(tab_c - 1));
    while (tab[j]) { if (tab[j]->h == h && tab[j]->n == n && !memcmp(tab[j]->s, s, (size_t)n)) return tab[j]; j = (j + 1) & (tab_c - 1); }
    str *r = novo_str(s, n);
    r->h = h; r->in = r; tab[j] = r; tab_n++;
    return r;
}
static str *intern(str *s) { if (!s->in) s->in = intern_n(s->s, s->n); return s->in; }
static str *I(const char *s) { return intern_n(s, (int)tam_texto(s)); }

/* ------------------------------------------------------------ valores */
static val vU(void) { val v; v.t = U; v.n = 0; return v; }
static val vNul(void) { val v; v.t = NUL; v.n = 0; return v; }
static val vB(int b) { val v; v.t = BO; v.n = 0; v.b = !!b; return v; }
static val vN(i64 n) { val v; v.t = NU; v.n = n; return v; }
static val vI(int n) { return vN(de_int(n)); }
static val vS(str *s) { val v; v.t = ST; v.n = 0; v.s = s; return v; }
static val vT(const char *s) { return vS(novo_str(s, (int)tam_texto(s))); }
static val vO(obj *o) { val v; v.t = OB; v.n = 0; v.o = o; return v; }

static obj *global, *p_obj, *p_fn, *p_arr, *p_str, *p_num, *p_bool, *p_el;
static quadro q_global, *qa;
typedef struct salto { void *b[5]; struct salto *ant; } salto;
static salto *topo_s;
static val exc; static int fatal, prof;
static char *lim_pilha;
static int gerador_entrando;
static val produzir(val v, int delega);
static unsigned t_limite; static unsigned passos;

static __attribute__((noinline, noreturn)) void lancar(val v) { exc = v; __builtin_longjmp(topo_s->b, 1); }
static void lancar_fatal(const char *m) { registrar("js: abortado: %s\n", m); fatal = 1; exc = vU(); __builtin_longjmp(topo_s->b, 1); }
static val chamar(val f, val este, val *a, int n);
static val construir(val f, val *a, int n);
static val get(val b, str *k);
static __attribute__((noreturn)) void erro(const char *tipo, const char *m, const char *x)
{
    char b[160];
    formatar_texto(b, sizeof b, "%s%s", m, x ? x : "");
    val a = vT(b), c = get(vO(global), I(tipo));
    if (c.t != OB) { exc = a; lancar(a); }
    lancar(construir(c, &a, 1));
}

static obj *novo_obj(obj *proto) { obj *o = novo(sizeof(obj)); o->proto = proto; return o; }
static obj *novo_arr(int cap)
{
    obj *o = novo_obj(p_arr);
    o->cl = C_ARR;
    if (cap) { o->a = novo((unsigned)cap * sizeof(val)); o->ca = cap; }
    return o;
}
static void arr_push(obj *o, val v)
{
    if (o->na == o->ca) {
        int c = o->ca ? o->ca * 2 : 4;
        val *a = novo((unsigned)c * sizeof(val));
        if (o->na) memcpy(a, o->a, (size_t)o->na * sizeof(val));
        o->a = a; o->ca = c;
    }
    o->a[o->na++] = v;
}
static prop *achar(obj *o, str *k)
{
    if (o->idx) {
        int m = o->cidx - 1, j = (int)(k->h & (unsigned)m);
        while (o->idx[j]) { prop *p = &o->p[o->idx[j] - 1]; if (p->k == k) return p; j = (j + 1) & m; }
        return NULL;
    }
    for (int i = 0; i < o->np; i++) if (o->p[i].k == k) return &o->p[i];
    return NULL;
}
static void reindexar(obj *o)
{
    int c = 32;
    while (c < o->cp * 2) c *= 2;
    o->idx = novo((unsigned)c * sizeof(int)); o->cidx = c;
    for (int i = 0; i < o->np; i++) if (o->p[i].k) { int j = (int)(o->p[i].k->h & (unsigned)(c - 1)); while (o->idx[j]) j = (j + 1) & (c - 1); o->idx[j] = i + 1; }
}
static void por_prop(obj *o, str *k, val v)
{
    prop *p = achar(o, k);
    if (p) { p->v = v; return; }
    if (o->np == o->cp) {
        int c = o->cp ? o->cp * 2 : 4;
        prop *n = novo((unsigned)c * sizeof(prop));
        if (o->np) memcpy(n, o->p, (size_t)o->np * sizeof(prop));
        o->p = n; o->cp = c;
        if (c >= 16) reindexar(o);
    }
    o->p[o->np].k = k; o->p[o->np].v = v; o->np++;
    if (o->idx) { int m = o->cidx - 1, j = (int)(k->h & (unsigned)m); while (o->idx[j]) j = (j + 1) & m; o->idx[j] = o->np; }
}
static val novo_nat(nat f, const char *nome)
{
    obj *o = novo_obj(p_fn);
    o->cl = C_FN; o->f = f;
    por_prop(o, I("name"), vT(nome));
    return vO(o);
}
static void reg(obj *o, const char *n, nat f) { por_prop(o, I(n), novo_nat(f, n)); }

static val prim(val v);
static str *num_str(i64 n)
{
    char b[40]; int k = 39;
    b[39] = 0;
    if (n == NANV) return I("NaN");
    if (n == INFV) return I("Infinity");
    if (n == -INFV) return I("-Infinity");
    u64 x = n < 0 ? -(u64)n : (u64)n, ip = x >> FR;
    unsigned fr = (unsigned)(((x & (UM - 1)) * 1000000 + (UM >> 1)) >> FR);
    if (fr >= 1000000) { fr -= 1000000; ip++; }
    if (fr) {
        int d = 6;
        while (fr % 10 == 0) { fr /= 10; d--; }
        while (d--) { b[--k] = (char)('0' + fr % 10); fr /= 10; }
        b[--k] = '.';
    }
    do { u64 r; ip = udiv(ip, 10, &r); b[--k] = (char)('0' + (int)r); } while (ip);
    if (n < 0 && (b[k] != '0' || b[k + 1])) b[--k] = '-';
    return novo_str(b + k, 39 - k);
}
static str *to_str(val v)
{
    switch (v.t) {
    case U: return I("undefined");
    case NUL: return I("null");
    case BO: return I(v.b ? "true" : "false");
    case NU: return num_str(v.n);
    case ST: return v.s;
    default: return to_str(prim(v));
    }
}
static i64 str_num(str *s)
{
    int i = 0, n = s->n, neg = 0, u;
    while (i < n && (uint8_t)s->s[i] <= ' ') i++;
    while (n > i && (uint8_t)s->s[n - 1] <= ' ') n--;
    if (i == n) return 0;
    if (s->s[i] == '0' && i + 1 < n && (s->s[i + 1] | 32) == 'x') {
        i64 r = 0;
        for (i += 2; i < n; i++) { char c = s->s[i]; int d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; if (d < 0) return NANV; r = r * 16 + d; }
        return r << FR;
    }
    if (s->s[i] == '-' || s->s[i] == '+') neg = s->s[i++] == '-';
    if (n - i == 8 && !memcmp(s->s + i, "Infinity", 8)) return neg ? -INFV : INFV;
    if (i == n || !((s->s[i] >= '0' && s->s[i] <= '9') || s->s[i] == '.')) return NANV;
    i64 r = ler_numero(s->s + i, n - i, &u);
    if (u != n - i) return NANV;
    return neg ? -r : r;
}
static i64 to_num(val v)
{
    switch (v.t) {
    case U: return NANV;
    case NUL: return 0;
    case BO: return de_int(v.b);
    case NU: return v.n;
    case ST: return str_num(v.s);
    default: return to_num(prim(v));
    }
}
static int to_bool(val v)
{
    switch (v.t) {
    case U: case NUL: return 0;
    case BO: return v.b;
    case NU: return v.n != 0 && v.n != NANV;
    case ST: return v.s->n != 0;
    default: return 1;
    }
}
static int to_i(val v) { return para_i32(to_num(v)); }
static val prim(val v)
{
    if (v.t != OB) return v;
    static const char *const ms[2] = { "valueOf", "toString" };
    for (int i = 0; i < 2; i++) {
        val f = get(v, I(ms[i]));
        if (f.t == OB && f.o->cl == C_FN) { val r = chamar(f, v, NULL, 0); if (r.t != OB) return r; }
    }
    return vS(I("[object Object]"));
}
static val concat(str *a, str *b)
{
    str *r = novo_str(NULL, a->n + b->n);
    memcpy(r->s, a->s, (size_t)a->n);
    memcpy(r->s + a->n, b->s, (size_t)b->n);
    return vS(r);
}
static int str_igual(str *a, str *b) { return a == b || (a->n == b->n && !memcmp(a->s, b->s, (size_t)a->n)); }
static int estrito(val a, val b)
{
    if (a.t != b.t) return 0;
    switch (a.t) {
    case U: case NUL: return 1;
    case BO: return a.b == b.b;
    case NU: return a.n == b.n && a.n != NANV;
    case ST: return str_igual(a.s, b.s);
    default: return a.o == b.o;
    }
}
static int solto(val a, val b)
{
    if (a.t == b.t) return estrito(a, b);
    if ((a.t == U || a.t == NUL) && (b.t == U || b.t == NUL)) return 1;
    if (a.t == U || a.t == NUL || b.t == U || b.t == NUL) return 0;
    if (a.t == OB) return solto(prim(a), b);
    if (b.t == OB) return solto(a, prim(b));
    i64 x = to_num(a), y = to_num(b);
    return x == y && x != NANV;
}
static int indice(str *k)
{
    if (!k->n || k->n > 9 || (k->s[0] == '0' && k->n > 1)) return -1;
    int r = 0;
    for (int i = 0; i < k->n; i++) { if (k->s[i] < '0' || k->s[i] > '9') return -1; r = r * 10 + k->s[i] - '0'; }
    return r;
}

/* ------------------------------------------------------------ propriedades */
static int el_get(obj *o, str *k, val *r);
static int el_set(obj *o, str *k, val v);
static str *s_length, *s_prototype, *s_constructor, *s_arguments;

static val get_o(obj *o, str *k, val este)
{
    for (obj *c = o; c; c = c->proto) {
        prop *p = achar(c, k);
        if (p) {
            if (p->v.t == AC) { obj *g = p->v.o; prop *x = achar(g, I("get")); return x && x->v.t == OB ? chamar(x->v, este, NULL, 0) : vU(); }
            return p->v;
        }
    }
    return vU();
}
static val get(val b, str *k)
{
    k = intern(k);
    switch (b.t) {
    case U: case NUL: erro("TypeError", "Cannot read properties of null: ", k->s);
    case ST:
        if (k == s_length) return vI(b.s->n);
        { int i = indice(k); if (i >= 0) return i < b.s->n ? vS(intern_n(b.s->s + i, 1)) : vU(); }
        return get_o(p_str, k, b);
    case NU: return get_o(p_num, k, b);
    case BO: return get_o(p_bool, k, b);
    default: break;
    }
    obj *o = b.o;
    if (o->cl == C_ARR) {
        if (k == s_length) return vI(o->na);
        int i = indice(k);
        if (i >= 0) return i < o->na ? o->a[i] : vU();
    } else if (o->cl == C_EL) {
        val r;
        if (!achar(o, k) && el_get(o, k, &r)) return r;
    } else if (o->cl == C_FN && k == s_prototype && !achar(o, k) && o->fn) {
        obj *p = novo_obj(p_obj);
        por_prop(p, s_constructor, b);
        por_prop(o, k, vO(p));
    }
    return get_o(o, k, b);
}
static void put(val b, str *k, val v)
{
    k = intern(k);
    if (b.t == U || b.t == NUL) erro("TypeError", "Cannot set properties of null: ", k->s);
    if (b.t != OB) return;
    obj *o = b.o;
    if (o->cl == C_ARR) {
        if (k == s_length) { int n = to_i(v); if (n < o->na) o->na = n < 0 ? 0 : n; else while (o->na < n && n < 1000000) arr_push(o, vU()); return; }
        int i = indice(k);
        if (i >= 0 && i < 4000000) { while (o->na <= i) arr_push(o, vU()); o->a[i] = v; return; }
    } else if (o->cl == C_EL && el_set(o, k, v)) return;
    for (obj *c = o; c; c = c->proto) {
        prop *p = achar(c, k);
        if (p) {
            if (p->v.t == AC) { prop *x = achar(p->v.o, I("set")); if (x && x->v.t == OB) chamar(x->v, b, &v, 1); return; }
            break;
        }
    }
    por_prop(o, k, v);
}
static str *chave(val k) { return intern(to_str(k)); }
static val get_i(val b, val k)
{
    if (k.t == NU && b.t == OB && b.o->cl == C_ARR && k.n >= 0 && !(k.n & (UM - 1))) { i64 i = k.n >> FR; return i < b.o->na ? b.o->a[(int)i] : vU(); }
    if (k.t == NU && b.t == ST && k.n >= 0 && !(k.n & (UM - 1))) { i64 i = k.n >> FR; return i < b.s->n ? vS(intern_n(b.s->s + (int)i, 1)) : vU(); }
    return get(b, chave(k));
}
static void put_i(val b, val k, val v)
{
    if (k.t == NU && b.t == OB && b.o->cl == C_ARR && k.n >= 0 && !(k.n & (UM - 1)) && (k.n >> FR) < b.o->na) { b.o->a[(int)(k.n >> FR)] = v; return; }
    put(b, chave(k), v);
}

/* ------------------------------------------------------------ léxico */
enum { T_FIM = 0, T_NUM = 256, T_STR, T_TPL, T_ID, T_RE, T_OP = 300 };
static const char *const ops[] = { ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "?\?=", "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "**" };
enum { O_URSA, O_3P, O_SEQ, O_SNE, O_POWA, O_SHLA, O_SHRA, O_URS, O_ANDA, O_ORA, O_NNA, O_SETA, O_EQ, O_NE, O_LE, O_GE, O_AND, O_OR, O_NN, O_OPT, O_INC, O_DEC, O_ADDA, O_SUBA, O_MULA, O_DIVA, O_MODA, O_BANDA, O_BORA, O_XORA, O_SHL, O_SHR, O_POW };
#define OP(x) (T_OP + (x))
typedef struct { const char *src; int tam, p, tk, nl, ant, ini; i64 num; str *s, *re_f; } lex_t;
static lex_t L;
static __attribute__((noreturn)) void sintaxe(const char *m)
{
    char b[80]; int l = 1;
    for (int i = 0; i < L.ini && i < L.tam; i++) if (L.src[i] == '\n') l++;
    formatar_texto(b, sizeof b, "%s (linha %d, byte %d)", m, l, L.ini);
    erro("SyntaxError", b, NULL);
}
static int id_ini(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80; }
static int dig(char c) { return c >= '0' && c <= '9'; }
static int hexv(char c) { return dig(c) ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static char *tb; static int tb_c;
static void tb_por(int *k, char c)
{
    if (*k + 2 > tb_c) { int n = tb_c ? tb_c * 2 : 65536; char *d = novo((unsigned)n); if (*k) memcpy(d, tb, (size_t)*k); tb = d; tb_c = n; }
    tb[(*k)++] = c;
}
/* Lê uma sequência \x depois da barra; devolve a nova posição. */
static int escape(const char *s, int p, int fim, int *k)
{
    char c = s[p++];
    int v = -1;
    switch (c) {
    case 'n': v = '\n'; break; case 't': v = '\t'; break; case 'r': v = '\r'; break; case 'b': v = 8; break;
    case 'f': v = 12; break; case 'v': v = 11; break; case '0': v = 0; break;
    case '\r': if (p < fim && s[p] == '\n') p++; return p;
    case '\n': return p;
    case 'x': if (p + 1 < fim && hexv(s[p]) >= 0 && hexv(s[p + 1]) >= 0) { v = hexv(s[p]) * 16 + hexv(s[p + 1]); p += 2; } else v = 'x'; break;
    case 'u':
        v = 0;
        if (p < fim && s[p] == '{') { p++; while (p < fim && s[p] != '}') v = v * 16 + hexv(s[p++]); p++; }
        else for (int i = 0; i < 4 && p < fim; i++) v = v * 16 + hexv(s[p++]);
        if (v > 255) v = '?';
        break;
    default: v = (uint8_t)c;
    }
    tb_por(k, (char)v);
    return p;
}
static void prox(void)
{
    const char *s = L.src; int n = L.tam, p = L.p;
    L.ant = L.tk; L.nl = 0;
    for (;;) {
        while (p < n && ((uint8_t)s[p] <= ' ')) { if (s[p] == '\n') L.nl = 1; p++; }
        if (p + 1 < n && s[p] == '/' && s[p + 1] == '/') { while (p < n && s[p] != '\n') p++; continue; }
        if (p + 1 < n && s[p] == '/' && s[p + 1] == '*') { p += 2; while (p + 1 < n && !(s[p] == '*' && s[p + 1] == '/')) { if (s[p] == '\n') L.nl = 1; p++; } p += 2; continue; }
        if (p + 3 < n && !memcmp(s + p, "<!--", 4)) { while (p < n && s[p] != '\n') p++; continue; }
        break;
    }
    L.ini = p;
    if (p >= n) { L.tk = T_FIM; L.p = p; return; }
    uint8_t c = (uint8_t)s[p];
    if (dig((char)c) || (c == '.' && p + 1 < n && dig(s[p + 1]))) {
        if (c == '0' && p + 1 < n && ((s[p + 1] | 32) == 'x' || (s[p + 1] | 32) == 'b' || (s[p + 1] | 32) == 'o')) {
            int b = (s[p + 1] | 32) == 'x' ? 16 : (s[p + 1] | 32) == 'b' ? 2 : 8;
            u64 v = 0; p += 2;
            while (p < n && hexv(s[p]) >= 0 && hexv(s[p]) < b) v = v * (unsigned)b + (unsigned)hexv(s[p++]);
            L.num = v >> 43 ? INFV : (i64)(v << FR);
        } else { int u; L.num = ler_numero(s + p, n - p, &u); p += u; }
        if (p < n && s[p] == 'n') p++;
        L.tk = T_NUM; L.p = p; return;
    }
    if (id_ini(c) || c == '\\') {
        int i = p;
        while (p < n && (id_ini((uint8_t)s[p]) || dig(s[p]))) p++;
        L.s = intern_n(s + i, p - i);
        L.tk = T_ID; L.p = p; return;
    }
    if (c == '"' || c == '\'') {
        int k = 0, i = ++p;
        while (p < n && s[p] != (char)c && s[p] != '\\' && s[p] != '\n') p++;
        if (p < n && s[p] == (char)c) { L.s = novo_str(s + i, p - i); L.p = p + 1; L.tk = T_STR; return; }
        p = i;
        while (p < n && s[p] != (char)c) {
            if (s[p] == '\\' && p + 1 < n) p = escape(s, p + 1, n, &k);
            else tb_por(&k, s[p++]);
        }
        L.s = novo_str(tb, k); L.p = p + 1; L.tk = T_STR; return;
    }
    if (c == '`') { L.p = p + 1; L.tk = T_TPL; return; }
    if (c == '/') {
        int a = L.ant;
        int kwv = a == T_ID ? L.s->kw : 0;
        int valor = a == T_NUM || a == T_STR || a == T_TPL || a == T_RE || a == ')' || a == ']' || a == '}' || (a == T_ID && !(kwv == KW_RETURN || kwv == KW_TYPEOF || kwv == KW_IN || kwv == KW_DELETE || kwv == KW_VOID || kwv == KW_THROW || kwv == KW_CASE || kwv == KW_ELSE || kwv == KW_NEW || kwv == KW_INSTANCEOF));
        if (!valor) {
            int i = ++p, cl = 0;
            while (p < n && (s[p] != '/' || cl) && s[p] != '\n') { if (s[p] == '\\') p++; else if (s[p] == '[') cl = 1; else if (s[p] == ']') cl = 0; p++; }
            L.s = novo_str(s + i, p - i);
            i = ++p;
            while (p < n && id_ini((uint8_t)s[p])) p++;
            L.re_f = novo_str(s + i, p - i);
            L.tk = T_RE; L.p = p; return;
        }
    }
    for (unsigned i = 0; i < sizeof ops / sizeof *ops; i++) {
        int t = (int)tam_texto(ops[i]);
        if (ops[i][0] == (char)c && p + t <= n && !memcmp(s + p, ops[i], (size_t)t)) {
            if (i == O_OPT && p + 2 < n && dig(s[p + 2])) continue;
            L.tk = OP((int)i); L.p = p + t; return;
        }
    }
    L.tk = c; L.p = p + 1;
}
static int kw(int k) { return L.tk == T_ID && L.s->kw == k; }
static int aceita(int t) { if (L.tk == t) { prox(); return 1; } return 0; }
static void exige(int t) { if (L.tk != t) sintaxe("sintaxe"); prox(); }
static int aceita_kw(int k) { if (kw(k)) { prox(); return 1; } return 0; }

/* ------------------------------------------------------------ sintaxe */
static fdef *cf; static int nfuncs, vem_async, vem_gerador;
static no *N(int k) { no *x = novo(sizeof(no)); x->k = (short)k; return x; }
static no *N2(int k, no *a, no *b) { no *x = N(k); x->a = a; x->b = b; return x; }
static no *expr(void), *atrib(int sem_in), *stmt(void), *funcao(str *nome, int metodo, int seta);
static no *expr_in(int sem_in);
static no *classe(void);
static void var_nome(str *k)
{
    for (int i = 0; i < cf->nv; i++) if (cf->vars[i] == k) return;
    if (cf->nv == cf->cv) { int c = cf->cv ? cf->cv * 2 : 8; str **v = novo((unsigned)c * sizeof(str *)); if (cf->nv) memcpy(v, cf->vars, (size_t)cf->nv * sizeof(str *)); cf->vars = v; cf->cv = c; }
    cf->vars[cf->nv++] = k;
}
static void var_padrao(no *p)
{
    if (!p) return;
    if (p->k == K_ID) var_nome(p->s);
    else if (p->k == K_ASG) var_padrao(p->a);
    else if (p->k == K_SPREAD) var_padrao(p->a);
    else if (p->k == K_ARR) for (no *e = p->a; e; e = e->prox) var_padrao(e->a);
    else if (p->k == K_OBJ) for (no *e = p->a; e; e = e->prox) var_padrao(e->op == 3 ? e->a : e->b);
}
typedef struct { no *pri, *ult; } lista;
static void lpor(lista *l, no *x) { if (l->ult) l->ult->prox = x; else l->pri = x; l->ult = x; }

static no *modelo(void)
{
    /* L.p está logo depois da crase. */
    const char *s = L.src; int n = L.tam, p = L.p;
    no *t = N(K_TPL); lista l = { 0, 0 };
    for (;;) {
        int k = 0;
        while (p < n && s[p] != '`' && !(s[p] == '$' && p + 1 < n && s[p + 1] == '{')) {
            if (s[p] == '\\' && p + 1 < n) p = escape(s, p + 1, n, &k);
            else tb_por(&k, s[p++]);
        }
        no *x = N(K_STR); x->s = novo_str(tb, k); lpor(&l, x);
        if (p >= n || s[p] == '`') { p++; break; }
        L.p = p + 2; L.tk = ';';
        prox();
        no *e = expr();
        if (L.tk != '}') sintaxe("modelo");
        no *h = N2(K_SEQ, e, NULL); lpor(&l, h);
        p = L.p;
    }
    t->a = l.pri;
    L.p = p; L.tk = T_STR;
    prox();
    return t;
}
static void chave_prop(no *pr)
{
    if (L.tk == T_ID || L.tk == T_STR) { pr->s = intern(L.s); prox(); }
    else if (L.tk == T_NUM) { pr->s = intern(num_str(L.num)); prox(); }
    else if (aceita('[')) { pr->c = atrib(0); exige(']'); }
    else sintaxe("nome de propriedade");
}
static no *objeto(void)
{
    no *o = N(K_OBJ); lista l = { 0, 0 };
    while (L.tk != '}') {
        no *pr = N(K_PROP);
        if (aceita(OP(O_3P))) { pr->op = 3; pr->a = atrib(0); }
        else {
            int mod = 0;
            if ((kw(KW_GET) || kw(KW_SET) || kw(KW_ASYNC))) {
                lex_t sv = L; int m = L.s->kw;
                prox();
                if (L.tk == ',' || L.tk == ':' || L.tk == '(' || L.tk == '}' || L.tk == '=') L = sv;
                else mod = m;
            }
            if (aceita('*')) vem_gerador = 1;
            int era_id = L.tk == T_ID; str *nm = L.s;
            chave_prop(pr);
            if (mod == KW_GET) pr->op = 1; else if (mod == KW_SET) pr->op = 2;
            if (L.tk == '(') { vem_async = mod == KW_ASYNC; pr->b = funcao(pr->s, 1, 0); }
            else if (aceita(':')) pr->b = atrib(0);
            else if (era_id) {
                no *id = N(K_ID); id->s = nm;
                if (nm == s_arguments) cf->usa_args = 1;
                if (aceita('=')) { no *d = N2(K_ASG, id, atrib(0)); d->op = '='; pr->b = d; } else pr->b = id;
            } else sintaxe("objeto");
        }
        lpor(&l, pr);
        if (!aceita(',')) break;
    }
    exige('}');
    o->a = l.pri;
    return o;
}
static no *vetor(void)
{
    no *o = N(K_ARR); lista l = { 0, 0 };
    while (L.tk != ']') {
        no *h = N(K_SEQ);
        if (L.tk != ',') {
            if (aceita(OP(O_3P))) h->a = N2(K_SPREAD, atrib(0), NULL);
            else h->a = atrib(0);
        }
        lpor(&l, h);
        if (!aceita(',')) break;
    }
    exige(']');
    o->a = l.pri;
    return o;
}
/* Depois do "(": isto é a lista de parâmetros de uma seta? */
static int e_seta(void)
{
    if (L.tk == OP(O_3P)) return 1;
    if (L.tk != ')' && L.tk != T_ID && L.tk != '{' && L.tk != '[') return 0;
    if (L.tk == T_ID && L.s->kw == KW_FUNCTION) return 0;
    lex_t sv = L;
    int d = 1, r = 0, primeiro = 1;
    while (d && L.tk != T_FIM) {
        if (L.tk == '(' || L.tk == '[' || L.tk == '{') d++;
        else if (L.tk == ')' || L.tk == ']' || L.tk == '}') d--;
        else if (L.tk == T_TPL) { int p = L.p, q = 1; while (p < L.tam && q) { if (L.src[p] == '\\') p++; else if (L.src[p] == '`') q = 0; p++; } L.p = p; L.tk = T_STR; }
        if (!d) break;
        prox();
        if (primeiro && sv.tk == T_ID && d == 1 && L.tk != ',' && L.tk != ')' && L.tk != '=') { L = sv; return 0; }
        primeiro = 0;
    }
    if (L.tk == ')') { prox(); r = L.tk == OP(O_SETA); }
    L = sv;
    return r;
}
static no *primario(void)
{
    no *x;
    switch (L.tk) {
    case T_NUM: x = N(K_NUM); x->n = L.num; prox(); return x;
    case T_STR: x = N(K_STR); x->s = L.s; prox(); return x;
    case T_TPL: return modelo();
    case T_RE: x = N(K_RE); x->s = L.s; x->a = N(K_STR); x->a->s = L.re_f; prox(); return x;
    case '(':
        prox();
        if (e_seta()) return funcao(NULL, 0, 1);
        x = expr(); exige(')');
        return x;
    case '[': prox(); return vetor();
    case '{': prox(); return objeto();
    case T_ID: break;
    default: sintaxe("expressao");
    }
    str *s = L.s;
    switch (s->kw) {
    case KW_FUNCTION: {
        prox();
        if (aceita('*')) vem_gerador = 1;
        str *nm = NULL;
        if (L.tk == T_ID) { nm = L.s; prox(); }
        return funcao(nm, 0, 0);
    }
    case KW_ASYNC: {
        lex_t sv = L;
        prox();
        if (!L.nl && kw(KW_FUNCTION)) { no *r; prox(); if (aceita('*')) vem_gerador = 1; str *nm = NULL; if (L.tk == T_ID) { nm = L.s; prox(); } vem_async = 1; r = funcao(nm, 0, 0); return r; }
        if (!L.nl && L.tk == '(') { prox(); if (e_seta()) { vem_async = 1; return funcao(NULL, 0, 1); } }
        else if (!L.nl && L.tk == T_ID) { lex_t s2 = L; prox(); if (L.tk == OP(O_SETA)) { L = s2; vem_async = 1; return funcao(NULL, 0, 2); } }
        L = sv;
        break;
    }
    case KW_CLASS: return classe();
    case KW_THIS: prox(); return N(K_THIS);
    case KW_NULL: prox(); return N(K_NULL);
    case KW_TRUE: prox(); return N(K_TRUE);
    case KW_FALSE: prox(); return N(K_FALSE);
    case KW_SUPER: prox(); return N(K_SUPER);
    default: break;
    }
    prox();
    if (L.tk == OP(O_SETA)) {
        no *f;
        x = N(K_ID); x->s = s;
        prox();
        f = funcao(NULL, 0, 3);
        f->f->params = N(S_DECL); f->f->params->a = x;
        return f;
    }
    x = N(K_ID); x->s = s;
    if (s == s_arguments) cf->usa_args = 1;
    return x;
}
static no *argumentos(void)
{
    lista l = { 0, 0 };
    while (L.tk != ')') {
        no *h = N(K_SEQ);
        if (aceita(OP(O_3P))) h->a = N2(K_SPREAD, atrib(0), NULL); else h->a = atrib(0);
        lpor(&l, h);
        if (!aceita(',')) break;
    }
    exige(')');
    return l.pri;
}
static no *sufixos(no *x, int sem_call)
{
    for (;;) {
        if (L.tk == '.' || L.tk == OP(O_OPT)) {
            int opt = L.tk != '.';
            prox();
            if (opt && L.tk == '(') { prox(); no *c = N2(K_CALL, x, argumentos()); c->op = 1; x = c; continue; }
            if (opt && L.tk == '[') { prox(); no *m = N2(K_IDX, x, expr()); m->op = 1; exige(']'); x = m; continue; }
            if (L.tk != T_ID) sintaxe("nome depois do ponto");
            no *m = N2(K_MEM, x, NULL); m->s = L.s; m->op = (short)opt; prox(); x = m;
        } else if (L.tk == '[') { prox(); x = N2(K_IDX, x, expr()); exige(']'); }
        else if (L.tk == '(' && !sem_call) { prox(); x = N2(K_CALL, x, argumentos()); }
        else if (L.tk == T_TPL) { no *t = modelo(); no *h = N(K_SEQ); h->a = t; x = N2(K_CALL, x, h); }
        else return x;
    }
}
static no *novo_expr(void)
{
    prox();
    if (L.tk == '.') { prox(); prox(); return N(K_NEWT); }
    no *c = kw(KW_NEW) ? novo_expr() : primario();
    c = sufixos(c, 1);
    no *x = N2(K_NEW, c, NULL);
    if (aceita('(')) x->b = argumentos();
    return x;
}
static no *unario(void)
{
    int t = L.tk;
    if (t == '!' || t == '-' || t == '+' || t == '~') { prox(); no *x = N2(K_UN, unario(), NULL); x->op = (short)t; return x; }
    if (t == OP(O_INC) || t == OP(O_DEC)) { prox(); no *x = N2(K_UPD, unario(), NULL); x->op = t == OP(O_INC) ? 1 : -1; x->n = 1; return x; }
    if (t == T_ID) {
        int k = L.s->kw;
        if (k == KW_TYPEOF) { prox(); return N2(K_TIPO, unario(), NULL); }
        if (k == KW_VOID) { prox(); no *x = N2(K_UN, unario(), NULL); x->op = 'v'; return x; }
        if (k == KW_DELETE) { prox(); return N2(K_DEL, unario(), NULL); }
        if (k == KW_AWAIT) { lex_t sv = L; prox(); if (L.tk == ')' || L.tk == ';' || L.tk == '=' || L.tk == ',' || L.tk == '.') L = sv; else return N2(K_AWAIT, unario(), NULL); }
    }
    no *x = kw(KW_NEW) ? novo_expr() : primario();
    x = sufixos(x, 0);
    if ((L.tk == OP(O_INC) || L.tk == OP(O_DEC)) && !L.nl) { no *u = N2(K_UPD, x, NULL); u->op = L.tk == OP(O_INC) ? 1 : -1; prox(); return u; }
    if (L.tk == OP(O_POW)) { prox(); no *b = N2(K_BIN, x, unario()); b->op = OP(O_POW); return b; }
    return x;
}
static int prec(int t, int sem_in)
{
    if (t == T_ID) return L.s->kw == KW_INSTANCEOF || (L.s->kw == KW_IN && !sem_in) ? 7 : 0;
    switch (t) {
    case OP(O_NN): return 1; case OP(O_OR): return 1; case OP(O_AND): return 2; case '|': return 3; case '^': return 4; case '&': return 5;
    case OP(O_EQ): case OP(O_NE): case OP(O_SEQ): case OP(O_SNE): return 6;
    case '<': case '>': case OP(O_LE): case OP(O_GE): return 7;
    case OP(O_SHL): case OP(O_SHR): case OP(O_URS): return 8;
    case '+': case '-': return 9; case '*': case '/': case '%': return 10;
    default: return 0;
    }
}
static no *binario(int m, int sem_in)
{
    no *a = unario();
    for (;;) {
        int t = L.tk, p = prec(t, sem_in);
        if (!p || p < m) return a;
        int w = t == T_ID ? L.s->kw : 0;
        prox();
        no *b = binario(p + 1, sem_in);
        if (t == OP(O_AND)) a = N2(K_AND, a, b);
        else if (t == OP(O_OR)) a = N2(K_OR, a, b);
        else if (t == OP(O_NN)) a = N2(K_NN, a, b);
        else { a = N2(K_BIN, a, b); a->op = w == KW_IN ? 'i' : w ? 'o' : (short)t; }
    }
}
static str *s_yield;
static no *atrib(int sem_in)
{
    if (!s_yield) s_yield = I("yield");
    if (cf && cf->gerador && L.tk == T_ID && L.s == s_yield) {
        prox();
        no *y = N(K_YIELD);
        if (!L.nl && aceita('*')) y->op = 1;
        if (y->op || (!L.nl && L.tk != ')' && L.tk != ']' && L.tk != '}' && L.tk != ',' && L.tk != ';' && L.tk != ':' && L.tk != T_FIM))
            y->a = atrib(sem_in);
        return y;
    }
    no *a = binario(1, sem_in);
    int t = L.tk;
    if (t == '?') {
        prox();
        no *x = atrib(0);
        exige(':');
        no *c = N2(K_COND, a, x); c->c = atrib(sem_in);
        return c;
    }
    int o = 0;
    switch (t) {
    case '=': o = '='; break;
    case OP(O_ADDA): o = '+'; break; case OP(O_SUBA): o = '-'; break; case OP(O_MULA): o = '*'; break; case OP(O_DIVA): o = '/'; break; case OP(O_MODA): o = '%'; break;
    case OP(O_BANDA): o = '&'; break; case OP(O_BORA): o = '|'; break; case OP(O_XORA): o = '^'; break;
    case OP(O_SHLA): o = OP(O_SHL); break; case OP(O_SHRA): o = OP(O_SHR); break; case OP(O_URSA): o = OP(O_URS); break; case OP(O_POWA): o = OP(O_POW); break;
    case OP(O_ANDA): o = OP(O_AND); break; case OP(O_ORA): o = OP(O_OR); break; case OP(O_NNA): o = OP(O_NN); break;
    default: return a;
    }
    prox();
    no *x = N2(K_ASG, a, atrib(sem_in)); x->op = (short)o;
    return x;
}
static no *expr_in(int sem_in)
{
    no *a = atrib(sem_in);
    while (L.tk == ',') { prox(); a = N2(K_SEQ, a, atrib(sem_in)); a->op = 1; }
    return a;
}
static no *expr(void) { return expr_in(0); }
static void fim_stmt(void) { if (L.tk == ';') { prox(); return; } if (L.tk == '}' || L.tk == T_FIM || L.nl) return; sintaxe("falta ;"); }
static no *bloco_corpo(void)
{
    no *b = N(S_BLOCK); lista l = { 0, 0 };
    while (L.tk != '}' && L.tk != T_FIM) {
        no *s = stmt();
        if (s->k == S_FUNC || s->k == S_CLASS || (s->k == S_VAR && s->op)) b->op = 1;
        lpor(&l, s);
    }
    exige('}');
    b->a = l.pri;
    return b;
}
/* seta: 0 function, 1 seta com "(" já consumido, 2 async x =>, 3 x => (já depois do =>) */
static no *funcao(str *nome, int metodo, int seta)
{
    (void)metodo;
    no *f = N(K_FUNC); fdef *d = novo(sizeof(fdef)), *ant = cf;
    f->f = d; d->nome = nome; d->seta = !!seta; d->assinc = vem_async; vem_async = 0; d->gerador = vem_gerador; vem_gerador = 0;
    nfuncs++;
    cf = d;
    if (seta != 3) {
        lista l = { 0, 0 };
        if (seta == 2) { no *h = N(S_DECL); h->a = N(K_ID); h->a->s = L.s; prox(); lpor(&l, h); }
        else {
            if (!seta) exige('(');
            while (L.tk != ')') {
                no *h = N(S_DECL);
                if (aceita(OP(O_3P))) h->a = N2(K_SPREAD, atrib(0), NULL); else h->a = atrib(0);
                lpor(&l, h);
                if (!aceita(',')) break;
            }
            exige(')');
        }
        d->params = l.pri;
    }
    if (seta == 1 || seta == 2) exige(OP(O_SETA));
    if (seta && L.tk != '{') { d->expr = 1; d->corpo = atrib(0); }
    else { exige('{'); d->corpo = bloco_corpo(); }
    cf = ant;
    if (ant && d->seta && d->usa_args) ant->usa_args = 1;
    return f;
}
static no *classe(void)
{
    prox();
    no *c = N(K_CLASS); lista l = { 0, 0 };
    if (L.tk == T_ID && !kw(KW_EXTENDS)) { c->s = L.s; prox(); }
    if (aceita_kw(KW_EXTENDS)) c->a = sufixos(primario(), 0);
    exige('{');
    while (L.tk != '}') {
        if (aceita(';')) continue;
        no *m = N(K_PROP);
        int est = 0, mod = 0;
        for (;;) {
            if (!(kw(KW_STATIC) || kw(KW_GET) || kw(KW_SET) || kw(KW_ASYNC))) break;
            lex_t sv = L; int w = L.s->kw;
            prox();
            if (L.tk == '(' || L.tk == '=' || L.tk == ';' || L.tk == '}') { L = sv; break; }
            if (w == KW_STATIC) est = 1; else mod = w;
        }
        if (L.tk == '#') { prox(); }
        if (aceita('*')) vem_gerador = 1;
        chave_prop(m);
        m->op = mod == KW_GET ? 1 : mod == KW_SET ? 2 : 0;
        m->n = est;
        if (L.tk == '(') { vem_async = mod == KW_ASYNC; m->b = funcao(m->s, 1, 0); }
        else { m->op = 4; if (aceita('=')) { m->b = atrib(0); } fim_stmt(); }
        lpor(&l, m);
    }
    exige('}');
    c->b = l.pri;
    return c;
}
static no *var_decl(int tipo, int sem_in)
{
    no *v = N(S_VAR); lista l = { 0, 0 };
    v->op = (short)tipo;
    do {
        no *d = N(S_DECL);
        if (L.tk == T_ID) { d->a = N(K_ID); d->a->s = L.s; prox(); }
        else if (aceita('[')) d->a = vetor();
        else if (aceita('{')) d->a = objeto();
        else sintaxe("nome da variavel");
        if (!tipo) var_padrao(d->a);
        if (aceita('=')) d->b = atrib(sem_in);
        lpor(&l, d);
    } while (aceita(','));
    v->a = l.pri;
    return v;
}
static no *stmt(void)
{
    no *x;
    int t = L.tk;
    if (t == '{') { prox(); return bloco_corpo(); }
    if (t == ';') { prox(); return N(S_VAZIO); }
    if (t == T_ID) {
        switch (L.s->kw) {
        case KW_VAR: prox(); x = var_decl(0, 0); fim_stmt(); return x;
        case KW_CONST: prox(); x = var_decl(2, 0); fim_stmt(); return x;
        case KW_LET: { lex_t sv = L; prox(); if (L.tk == T_ID || L.tk == '[' || L.tk == '{') { x = var_decl(1, 0); fim_stmt(); return x; } L = sv; break; }
        case KW_ASYNC: { lex_t sv = L; prox(); if (kw(KW_FUNCTION) && !L.nl) vem_async = 1; else { L = sv; break; } }
        /* fallthrough */
        case KW_FUNCTION: {
            prox();
            if (aceita('*')) vem_gerador = 1;
            if (L.tk != T_ID) sintaxe("nome da funcao");
            str *nm = L.s; prox();
            x = N(S_FUNC); x->s = nm; x->a = funcao(nm, 0, 0);
            return x;
        }
        case KW_CLASS: { no *c = classe(); x = N2(S_CLASS, c, NULL); x->s = c->s; return x; }
        case KW_IF:
            prox(); exige('(');
            x = N(S_IF); x->a = expr(); exige(')');
            x->b = stmt();
            if (aceita_kw(KW_ELSE)) x->c = stmt();
            return x;
        case KW_WHILE: prox(); exige('('); x = N(S_WHILE); x->a = expr(); exige(')'); x->b = stmt(); return x;
        case KW_DO:
            prox(); x = N(S_DO); x->b = stmt();
            if (!aceita_kw(KW_WHILE)) sintaxe("do sem while");
            exige('('); x->a = expr(); exige(')'); aceita(';');
            return x;
        case KW_FOR: {
            prox();
            if (kw(KW_AWAIT)) prox();
            exige('(');
            no *ini = NULL;
            int nf = nfuncs;
            if (L.tk != ';') {
                if (kw(KW_VAR) || kw(KW_LET) || kw(KW_CONST)) { int k = L.s->kw == KW_VAR ? 0 : 1; prox(); ini = var_decl(k, 1); }
                else ini = expr_in(1);
            }
            if (kw(KW_IN) || kw(KW_OF)) {
                x = N(S_FORIN); x->op = (short)kw(KW_OF);
                prox();
                x->a = ini; x->b = expr(); exige(')'); x->c = stmt();
                return x;
            }
            exige(';');
            x = N(S_FOR); x->a = ini;
            if (L.tk != ';') x->b = expr();
            exige(';');
            if (L.tk != ')') x->c = expr();
            exige(')');
            x->d = stmt();
            x->op = (short)(nfuncs != nf);
            return x;
        }
        case KW_RETURN:
            prox(); x = N(S_RET);
            if (L.tk != ';' && L.tk != '}' && L.tk != T_FIM && !L.nl) x->a = expr();
            fim_stmt();
            return x;
        case KW_BREAK: case KW_CONTINUE:
            x = N(L.s->kw == KW_BREAK ? S_BRK : S_CONT); prox();
            if (L.tk == T_ID && !L.nl) { x->s = L.s; prox(); }
            fim_stmt();
            return x;
        case KW_THROW: prox(); x = N(S_THROW); x->a = expr(); fim_stmt(); return x;
        case KW_TRY:
            prox(); x = N(S_TRY); exige('{'); x->a = bloco_corpo();
            if (aceita_kw(KW_CATCH)) {
                if (aceita('(')) { if (L.tk == T_ID) { x->b = N(K_ID); x->b->s = L.s; prox(); } else if (aceita('{')) x->b = objeto(); else if (aceita('[')) x->b = vetor(); exige(')'); }
                exige('{'); x->c = bloco_corpo();
            }
            if (aceita_kw(KW_FINALLY)) { exige('{'); x->d = bloco_corpo(); }
            return x;
        case KW_SWITCH: {
            prox(); exige('('); x = N(S_SWITCH); x->a = expr(); exige(')'); exige('{');
            lista l = { 0, 0 };
            while (L.tk != '}') {
                no *c = N(S_CASE); lista b = { 0, 0 };
                if (aceita_kw(KW_DEFAULT)) c->op = 1; else { if (!aceita_kw(KW_CASE)) sintaxe("case"); c->a = expr(); }
                exige(':');
                while (L.tk != '}' && !kw(KW_CASE) && !kw(KW_DEFAULT) && L.tk != T_FIM) lpor(&b, stmt());
                c->b = b.pri;
                lpor(&l, c);
            }
            exige('}');
            x->b = l.pri;
            return x;
        }
        default: break;
        }
        if (!L.s->kw || L.s->kw >= KW_ASYNC) {
            lex_t sv = L; str *nm = L.s;
            prox();
            if (L.tk == ':') { prox(); x = N(S_LABEL); x->s = nm; x->a = stmt(); return x; }
            L = sv;
        }
    }
    x = N(S_EXP); x->a = expr();
    fim_stmt();
    return x;
}
static no *programa(const char *s, int n, fdef *d)
{
    lex_t sv = L; fdef *ant = cf;
    memset(&L, 0, sizeof L);
    L.src = s; L.tam = n; L.tk = ';';
    cf = d;
    prox();
    no *b = N(S_BLOCK); lista l = { 0, 0 };
    while (L.tk != T_FIM) lpor(&l, stmt());
    b->a = l.pri;
    cf = ant; L = sv;
    return b;
}

/* ------------------------------------------------------------ execução */
enum { C_N, C_BRK, C_CONT, C_RET };
static val rv; static str *rot;
static val ev(no *x, env *e);
static int ex(no *x, env *e);

static env *novo_env(env *pai, int c)
{
    env *e = novo(sizeof(env));
    e->pai = pai; e->c = c;
    if (c) { e->k = novo((unsigned)c * sizeof(str *)); e->v = novo((unsigned)c * sizeof(val)); }
    return e;
}
static void decl(env *e, str *k, val v)
{
    if (!e) { por_prop(global, k, v); return; }
    for (int i = 0; i < e->n; i++) if (e->k[i] == k) { e->v[i] = v; return; }
    if (e->n == e->c) {
        int c = e->c ? e->c * 2 : 4;
        str **nk = novo((unsigned)c * sizeof(str *)); val *nv = novo((unsigned)c * sizeof(val));
        if (e->n) { memcpy(nk, e->k, (size_t)e->n * sizeof(str *)); memcpy(nv, e->v, (size_t)e->n * sizeof(val)); }
        e->k = nk; e->v = nv; e->c = c;
    }
    e->k[e->n] = k; e->v[e->n++] = v;
}
static val *var_ref(env *e, str *k)
{
    for (; e; e = e->pai) for (int i = 0; i < e->n; i++) if (e->k[i] == k) return &e->v[i];
    return NULL;
}
static val var_get(env *e, str *k)
{
    val *r = var_ref(e, k);
    if (r) return *r;
    for (obj *c = global; c; c = c->proto) { prop *p = achar(c, k); if (p) return p->v.t == AC ? get(vO(global), k) : p->v; }
    if (k->s[0] == 'u' && k == I("undefined")) return vU();
    erro("ReferenceError", k->s, " is not defined");
}
static void var_set(env *e, str *k, val v)
{
    val *r = var_ref(e, k);
    if (r) *r = v; else put(vO(global), k, v);
}
static val nova_funcao(no *x, env *e)
{
    obj *o = novo_obj(p_fn);
    o->cl = C_FN; o->fn = x; o->e = e;
    if (x->f->seta) o->q = qa;
    return vO(o);
}
static void hoist(no *l, env *e) { for (; l; l = l->prox) if (l->k == S_FUNC) decl(e, l->s, nova_funcao(l->a, e)); }
static void ligar(no *p, val v, env *e, int d);
static void ligar_id(no *p, val v, env *e, int d) { if (d) decl(e, p->s, v); else var_set(e, p->s, v); }
static void atribuir(no *t, val v, env *e, int d)
{
    switch (t->k) {
    case K_ID: ligar_id(t, v, e, d); return;
    case K_MEM: put(ev(t->a, e), t->s, v); return;
    case K_IDX: { val o = ev(t->a, e); put_i(o, ev(t->b, e), v); return; }
    default: ligar(t, v, e, d);
    }
}
static val para_vetor(val v);
static void ligar(no *p, val v, env *e, int d)
{
    if (p->k == K_ASG) { if (v.t == U) v = ev(p->b, e); atribuir(p->a, v, e, d); return; }
    if (p->k == K_ARR) {
        val a = para_vetor(v); int i = 0;
        for (no *h = p->a; h; h = h->prox, i++) {
            if (!h->a) continue;
            if (h->a->k == K_SPREAD) { obj *r = novo_arr(0); for (int j = i; j < a.o->na; j++) arr_push(r, a.o->a[j]); atribuir(h->a->a, vO(r), e, d); break; }
            atribuir(h->a, i < a.o->na ? a.o->a[i] : vU(), e, d);
        }
        return;
    }
    if (p->k == K_OBJ) {
        for (no *h = p->a; h; h = h->prox) {
            if (h->op == 3) { atribuir(h->a, vO(novo_obj(p_obj)), e, d); continue; }
            atribuir(h->b, h->c ? get_i(v, ev(h->c, e)) : get(v, h->s), e, d);
        }
        return;
    }
    atribuir(p, v, e, d);
}
static unsigned t_suave;   /* limite que vira exceção comum (para testar funções desconhecidas) */
static void chk(void)
{
    char c;
    if (t_suave && !(passos & 1023) && tempo_ms() > t_suave) erro("Error", "limite de tempo", NULL);
    if (&c < lim_pilha) erro("RangeError", "Maximum call stack size exceeded", NULL);
    if (!(++passos & 8191) && tempo_ms() > t_limite) lancar_fatal("tempo");
}
static val novo_gerador(obj *o, val este, val *a, int n);
static val executar_fn(obj *o, val este, val *a, int n, val novo_t)
{
    fdef *d = o->fn->f;
    if (d->gerador && !gerador_entrando) return novo_gerador(o, este, a, n);
    gerador_entrando = 0;
    env *e = novo_env(o->e, d->nv + 4);
    quadro q, *ant = qa;
    if (d->seta) qa = o->q;
    else { q.este = este; q.func = o; q.novo = novo_t; qa = &q; }
    int i = 0;
    for (no *p = d->params; p; p = p->prox, i++) {
        if (p->a->k == K_SPREAD) { obj *r = novo_arr(0); for (int j = i; j < n; j++) arr_push(r, a[j]); ligar(p->a->a, vO(r), e, 1); break; }
        if (p->a->k == K_ID) decl(e, p->a->s, i < n ? a[i] : vU());
        else ligar(p->a, i < n ? a[i] : vU(), e, 1);
    }
    if (d->usa_args) { obj *r = novo_arr(n); for (int j = 0; j < n; j++) arr_push(r, a[j]); decl(e, s_arguments, vO(r)); }
    if (d->nome && !d->seta && !var_ref(e, d->nome)) decl(e, d->nome, vO(o));
    for (int j = 0; j < d->nv; j++) { int ja = 0; for (int m = 0; m < e->n; m++) if (e->k[m] == d->vars[j]) ja = 1; if (!ja) decl(e, d->vars[j], vU()); }
    val r = vU();
    if (d->expr) r = ev(d->corpo, e);
    else {
        hoist(d->corpo->a, e);
        for (no *s = d->corpo->a; s; s = s->prox) if (ex(s, e) == C_RET) { r = rv; break; }
    }
    qa = ant;
    if (d->assinc) r = chamar(get(vO(global), I("__resolvido")), vU(), &r, 1);
    return r;
}
static val chamar_n(val f, val este, val *a, int n, val novo_t)
{
    if (f.t != OB || f.o->cl != C_FN) erro("TypeError", "is not a function", NULL);
    obj *o = f.o;
    chk();
    if (o->ligado) {
        val *b = novo((unsigned)(o->nlig + n + 1) * sizeof(val));
        if (o->nlig) memcpy(b, o->ligados, (size_t)o->nlig * sizeof(val));
        if (n) memcpy(b + o->nlig, a, (size_t)n * sizeof(val));
        return chamar(vO(o->ligado), o->ligado_este, b, o->nlig + n);
    }
    if (o->f) return o->f(este, a, n);
    return executar_fn(o, este, a, n, novo_t);
}
static val chamar(val f, val este, val *a, int n) { return chamar_n(f, este, a, n, vU()); }
static void campos(obj *c, val este)
{
    for (no *m = c->campos; m; m = m->prox) {
        if (m->op != 4 || m->n) continue;
        quadro q = { este, c, vU() }, *ant = qa;
        qa = &q;
        val v = m->b ? ev(m->b, c->e) : vU();
        qa = ant;
        put(este, m->c ? chave(ev(m->c, c->e)) : m->s, v);
    }
}
static val construir(val f, val *a, int n)
{
    if (f.t != OB || f.o->cl != C_FN) erro("TypeError", "is not a constructor", NULL);
    val p = get(f, s_prototype);
    obj *o = novo_obj(p.t == OB ? p.o : p_obj);
    val este = vO(o);
    campos(f.o, este);
    val r = chamar_n(f, este, a, n, f);
    return r.t == OB ? r : este;
}
static int avaliar_args(no *l, env *e, val **saida)
{
    int c = 0;
    int sp = 0;
    for (no *h = l; h; h = h->prox) { c++; if (h->a->k == K_SPREAD) sp = 1; }
    if (!sp) {
        val *a = novo((unsigned)(c + 1) * sizeof(val)); int i = 0;
        for (no *h = l; h; h = h->prox) a[i++] = ev(h->a, e);
        *saida = a;
        return c;
    }
    obj *r = novo_arr(c + 4);
    for (no *h = l; h; h = h->prox) {
        if (h->a->k == K_SPREAD) { val v = para_vetor(ev(h->a->a, e)); for (int i = 0; i < v.o->na; i++) arr_push(r, v.o->a[i]); }
        else arr_push(r, ev(h->a, e));
    }
    *saida = r->a;
    return r->na;
}
static val aritmetica(int op, val a, val b)
{
    if (op == '+') {
        a = prim(a); b = prim(b);
        if (a.t == ST || b.t == ST) return concat(to_str(a), to_str(b));
        return vN(nsoma(to_num(a), to_num(b)));
    }
    switch (op) {
    case OP(O_SEQ): return vB(estrito(a, b));
    case OP(O_SNE): return vB(!estrito(a, b));
    case OP(O_EQ): return vB(solto(a, b));
    case OP(O_NE): return vB(!solto(a, b));
    case '<': case '>': case OP(O_LE): case OP(O_GE): {
        a = prim(a); b = prim(b);
        int c;
        if (a.t == ST && b.t == ST) {
            int m = a.s->n < b.s->n ? a.s->n : b.s->n;
            c = memcmp(a.s->s, b.s->s, (size_t)m);
            if (!c) c = a.s->n - b.s->n;
        } else {
            i64 x = to_num(a), y = to_num(b);
            if (x == NANV || y == NANV) return vB(0);
            c = x < y ? -1 : x > y;
        }
        return vB(op == '<' ? c < 0 : op == '>' ? c > 0 : op == OP(O_LE) ? c <= 0 : c >= 0);
    }
    case 'i': {
        if (b.t != OB) erro("TypeError", "in: not an object", NULL);
        str *k = chave(a);
        if (b.o->cl == C_ARR) { int i = indice(k); if (i >= 0) return vB(i < b.o->na); if (k == s_length) return vB(1); }
        for (obj *c = b.o; c; c = c->proto) if (achar(c, k)) return vB(1);
        return vB(b.o->cl == C_EL ? ({ val r; el_get(b.o, k, &r); }) : 0);
    }
    case 'o': {
        if (b.t != OB || b.o->cl != C_FN) erro("TypeError", "instanceof: not callable", NULL);
        if (a.t != OB) return vB(0);
        val p = get(b, s_prototype);
        for (obj *c = a.o->proto; c; c = c->proto) if (p.t == OB && c == p.o) return vB(1);
        return vB(0);
    }
    default: break;
    }
    i64 x = to_num(a), y = to_num(b);
    switch (op) {
    case '-': return vN(nsoma(x, y == NANV ? NANV : -y));
    case '*': return vN(nmul(x, y));
    case '/': return vN(ndiv(x, y));
    case '%': return vN(nmod(x, y));
    case OP(O_POW): {
        if (y < 0 || (y & (UM - 1))) { val g = get(vO(global), I("__pow")), ar2[2] = { a, b }; return chamar(g, vU(), ar2, 2); }
        i64 r = UM;
        for (int i = para_i32(y); i > 0 && !esp(r); i--) r = nmul(r, x);
        return vN(r);
    }
    default: break;
    }
    int p = para_i32(x), q = para_i32(y);
    switch (op) {
    case '&': return vI(p & q);
    case '|': return vI(p | q);
    case '^': return vI(p ^ q);
    case OP(O_SHL): return vI((int)((unsigned)p << (q & 31)));
    case OP(O_SHR): return vI(p >> (q & 31));
    case OP(O_URS): return vN((i64)(u64)((unsigned)p >> (q & 31)) << FR);
    default: return vU();
    }
}
static val nova_classe(no *x, env *e)
{
    env *ce = novo_env(e, 2);
    val pai = x->a ? ev(x->a, e) : vU();
    no *ctor = NULL;
    static str *s_ct;
    if (!s_ct) s_ct = I("constructor");
    for (no *m = x->b; m; m = m->prox) if (m->s == s_ct && !m->n) ctor = m->b;
    obj *c = novo_obj(p_fn);
    c->cl = C_FN; c->e = ce; c->campos = x->b;
    if (ctor) c->fn = ctor;
    else {
        static no *vazio_ctor, *herda_ctor;
        if (!vazio_ctor) {
            fdef *d = novo(sizeof(fdef));
            vazio_ctor = programa("(function(){})", 14, d)->a->a; herda_ctor = programa("(function(){super(...arguments)})", 33, d)->a->a;
            herda_ctor->f->usa_args = 1;
        }
        c->fn = x->a ? herda_ctor : vazio_ctor;
    }
    obj *pr = novo_obj(p_obj);
    por_prop(pr, s_constructor, vO(c));
    por_prop(c, s_prototype, vO(pr));
    if (pai.t == OB) { c->proto = pai.o; val pp = get(pai, s_prototype); if (pp.t == OB) pr->proto = pp.o; }
    else if (x->a && pai.t == NUL) pr->proto = NULL;
    c->casa = pr;
    if (x->s) decl(ce, x->s, vO(c));
    for (no *m = x->b; m; m = m->prox) {
        if (m->b == ctor && ctor) continue;
        obj *alvo = m->n ? c : pr;
        str *k = m->c ? chave(ev(m->c, ce)) : m->s;
        if (m->op == 4) { if (m->n) { quadro q = { vO(c), c, vU() }, *ant = qa; qa = &q; val v = m->b ? ev(m->b, ce) : vU(); qa = ant; por_prop(c, k, v); } continue; }
        val f = nova_funcao(m->b, ce);
        f.o->casa = alvo;
        if (m->op) {
            prop *p = achar(alvo, k); obj *ac;
            if (p && p->v.t == AC) ac = p->v.o; else { ac = novo_obj(NULL); val w; w.t = AC; w.n = 0; w.o = ac; por_prop(alvo, k, w); }
            por_prop(ac, I(m->op == 1 ? "get" : "set"), f);
        } else por_prop(alvo, k, f);
    }
    return vO(c);
}
static obj *casa_atual(void) { obj *f = qa->func; return f ? f->casa : NULL; }
static val aguardar(val v);
static val ev(no *x, env *e)
{
    chk();
    switch (x->k) {
    case K_NUM: return vN(x->n);
    case K_STR: return vS(x->s);
    case K_TPL: {
        val r = vS(I(""));
        for (no *h = x->a; h; h = h->prox) r = concat(r.s, to_str(h->k == K_STR ? vS(h->s) : ev(h->a, e)));
        return r;
    }
    case K_ID: return var_get(e, x->s);
    case K_THIS: return qa->este;
    case K_NULL: return vNul();
    case K_TRUE: return vB(1);
    case K_FALSE: return vB(0);
    case K_NEWT: return qa->novo;
    case K_RE: { val a[2] = { vS(x->s), vS(x->a->s) }; return construir(get(vO(global), I("RegExp")), a, 2); }
    case K_ARR: {
        obj *r = novo_arr(0);
        for (no *h = x->a; h; h = h->prox) {
            if (!h->a) arr_push(r, vU());
            else if (h->a->k == K_SPREAD) { val v = para_vetor(ev(h->a->a, e)); for (int i = 0; i < v.o->na; i++) arr_push(r, v.o->a[i]); }
            else arr_push(r, ev(h->a, e));
        }
        return vO(r);
    }
    case K_OBJ: {
        obj *r = novo_obj(p_obj);
        for (no *h = x->a; h; h = h->prox) {
            if (h->op == 3) {
                val s = ev(h->a, e);
                if (s.t == OB) for (int i = 0; i < s.o->np; i++) if (s.o->p[i].k) por_prop(r, s.o->p[i].k, get(s, s.o->p[i].k));
                continue;
            }
            str *k = h->c ? chave(ev(h->c, e)) : h->s;
            val v = ev(h->b, e);
            if (v.t == OB && v.o->cl == C_FN && h->b->k == K_FUNC) v.o->casa = r;
            if (h->op == 1 || h->op == 2) {
                prop *p = achar(r, k); obj *ac;
                if (p && p->v.t == AC) ac = p->v.o; else { ac = novo_obj(NULL); val w; w.t = AC; w.n = 0; w.o = ac; por_prop(r, k, w); }
                por_prop(ac, I(h->op == 1 ? "get" : "set"), v);
            } else por_prop(r, k, v);
        }
        return vO(r);
    }
    case K_FUNC: return nova_funcao(x, e);
    case K_CLASS: return nova_classe(x, e);
    case K_SEQ: if (x->op) { ev(x->a, e); return ev(x->b, e); } return ev(x->a, e);
    case K_COND: return to_bool(ev(x->a, e)) ? ev(x->b, e) : ev(x->c, e);
    case K_AND: { val a = ev(x->a, e); return to_bool(a) ? ev(x->b, e) : a; }
    case K_OR: { val a = ev(x->a, e); return to_bool(a) ? a : ev(x->b, e); }
    case K_NN: { val a = ev(x->a, e); return a.t == U || a.t == NUL ? ev(x->b, e) : a; }
    case K_BIN: { val a = ev(x->a, e); return aritmetica(x->op, a, ev(x->b, e)); }
    case K_UN: {
        val a = ev(x->a, e);
        switch (x->op) {
        case '!': return vB(!to_bool(a));
        case '-': { i64 n = to_num(a); return vN(n == NANV ? n : -n); }
        case '+': return vN(to_num(a));
        case '~': return vI(~to_i(a));
        default: return vU();
        }
    }
    case K_TIPO: {
        val a;
        if (x->a->k == K_ID) {
            val *r = var_ref(e, x->a->s);
            if (r) a = *r;
            else { int tem = 0; for (obj *c = global; c; c = c->proto) if (achar(c, x->a->s)) tem = 1; if (!tem) return vS(I("undefined")); a = var_get(e, x->a->s); }
        } else a = ev(x->a, e);
        static const char *const n[] = { "undefined", "object", "boolean", "number", "string", "object" };
        if (a.t == OB && a.o->cl == C_FN) return vS(I("function"));
        return vS(I(n[a.t]));
    }
    case K_DEL: {
        no *t = x->a;
        if (t->k != K_MEM && t->k != K_IDX) return vB(1);
        val o = ev(t->a, e);
        str *k = t->k == K_MEM ? intern(t->s) : chave(ev(t->b, e));
        if (o.t == OB) {
            prop *p = achar(o.o, k);
            if (p) { int i = (int)(p - o.o->p); memmove(p, p + 1, (size_t)(o.o->np - i - 1) * sizeof(prop)); o.o->np--; if (o.o->idx) reindexar(o.o); }
            int j = o.o->cl == C_ARR ? indice(k) : -1;
            if (j >= 0 && j < o.o->na) o.o->a[j] = vU();
        }
        return vB(1);
    }
    case K_UPD: {
        no *t = x->a;
        val o = vU(), k = vU(), v;
        if (t->k == K_ID) v = var_get(e, t->s);
        else if (t->k == K_MEM) { o = ev(t->a, e); v = get(o, t->s); }
        else { o = ev(t->a, e); k = ev(t->b, e); v = get_i(o, k); }
        i64 n = to_num(v), m = nsoma(n, de_int(x->op));
        if (t->k == K_ID) var_set(e, t->s, vN(m)); else if (t->k == K_MEM) put(o, t->s, vN(m)); else put_i(o, k, vN(m));
        return vN(x->n ? m : n);
    }
    case K_ASG: {
        no *t = x->a;
        if (x->op == '=') {
            if (t->k == K_ID) {
                val v = ev(x->b, e);
                if (v.t == OB && v.o->cl == C_FN && v.o->fn && !v.o->fn->f->nome) { /* sem nome */ }
                var_set(e, t->s, v);
                return v;
            }
            if (t->k == K_MEM) { val o = ev(t->a, e); val v = ev(x->b, e); put(o, t->s, v); return v; }
            if (t->k == K_IDX) { val o = ev(t->a, e), k = ev(t->b, e); val v = ev(x->b, e); put_i(o, k, v); return v; }
            val v = ev(x->b, e);
            ligar(t, v, e, 0);
            return v;
        }
        val o = vU(), k = vU(), v;
        if (t->k == K_ID) v = var_get(e, t->s);
        else if (t->k == K_MEM) { o = ev(t->a, e); v = get(o, t->s); }
        else if (t->k == K_IDX) { o = ev(t->a, e); k = ev(t->b, e); v = get_i(o, k); }
        else sintaxe("atribuicao");
        if (x->op == OP(O_AND)) { if (!to_bool(v)) return v; v = ev(x->b, e); }
        else if (x->op == OP(O_OR)) { if (to_bool(v)) return v; v = ev(x->b, e); }
        else if (x->op == OP(O_NN)) { if (v.t != U && v.t != NUL) return v; v = ev(x->b, e); }
        else v = aritmetica(x->op, v, ev(x->b, e));
        if (t->k == K_ID) var_set(e, t->s, v); else if (t->k == K_MEM) put(o, t->s, v); else put_i(o, k, v);
        return v;
    }
    case K_MEM: {
        if (x->a->k == K_SUPER) { obj *c = casa_atual(); return c && c->proto ? get_o(c->proto, intern(x->s), qa->este) : vU(); }
        val o = ev(x->a, e);
        if (x->op && (o.t == U || o.t == NUL)) return vU();
        if (o.t == U || o.t == NUL) { if (x->a->k == K_MEM || x->a->k == K_ID) { /* mensagem igual */ } }
        return get(o, x->s);
    }
    case K_IDX: {
        val o = ev(x->a, e);
        if (x->op && (o.t == U || o.t == NUL)) return vU();
        return get_i(o, ev(x->b, e));
    }
    case K_CALL: {
        no *c = x->a;
        val f, este = vU(), *a;
        if (c->k == K_SUPER) {
            obj *cl = qa->func; val pai = vU();
            if (cl && cl->proto) pai = vO(cl->proto);
            int n = avaliar_args(x->b, e, &a);
            if (pai.t == OB && pai.o != p_fn) { campos(pai.o, qa->este); val r = chamar_n(pai, qa->este, a, n, qa->novo); if (r.t == OB) qa->este = r; }
            return qa->este;
        }
        if (c->k == K_MEM) {
            if (c->a->k == K_SUPER) { obj *h = casa_atual(); este = qa->este; f = h && h->proto ? get_o(h->proto, intern(c->s), este) : vU(); }
            else {
                este = ev(c->a, e);
                if (c->op && (este.t == U || este.t == NUL)) return vU();
                f = get(este, c->s);
            }
        } else if (c->k == K_IDX) { este = ev(c->a, e); if (c->op && (este.t == U || este.t == NUL)) return vU(); f = get_i(este, ev(c->b, e)); }
        else f = ev(c, e);
        if (x->op && (f.t == U || f.t == NUL)) return vU();
        int n = avaliar_args(x->b, e, &a);
        if (f.t != OB || f.o->cl != C_FN) erro("TypeError", c->k == K_MEM || c->k == K_ID ? c->s->s : "expressao", " is not a function");
        return chamar(f, este, a, n);
    }
    case K_NEW: { val f = ev(x->a, e), *a; int n = avaliar_args(x->b, e, &a); return construir(f, a, n); }
    case K_AWAIT: return aguardar(ev(x->a, e));
    case K_YIELD: return produzir(x->a ? ev(x->a, e) : vU(), x->op);
    default: return vU();
    }
}
static int laco(int c, str *meu)
{
    /* 0 segue, 1 sai normal, 2 propaga */
    if (c == C_BRK) { if (!rot || rot == meu) { rot = NULL; return 1; } return 2; }
    if (c == C_CONT) { if (!rot || rot == meu) { rot = NULL; return 0; } return 2; }
    return c == C_RET ? 2 : 0;
}
static str *rotulo_atual;
static val para_vetor(val v)
{
    if (v.t == OB && v.o->cl == C_ARR) return v;
    val f = get(vO(global), I("__vetor"));
    return chamar(f, vU(), &v, 1);
}
typedef struct { no *x; env *e; int c; int pego; } tent_t;
static __attribute__((noinline)) int tentar(no *b, env *e, int *c)
{
    salto s; salto *volatile ant = topo_s; quadro *volatile q = qa; volatile int p = prof;
    s.ant = ant;
    if (__builtin_setjmp(s.b)) { topo_s = ant; qa = q; prof = p; return 1; }
    topo_s = &s;
    *c = ex(b, e);
    topo_s = ant;
    return 0;
}
static int ex(no *x, env *e)
{
    str *meu = rotulo_atual;
    rotulo_atual = NULL;
    chk();
    switch (x->k) {
    case S_VAR:
        for (no *d = x->a; d; d = d->prox) {
            if (!d->b && !x->op) continue;
            val v = d->b ? ev(d->b, e) : vU();
            if (!x->op) { if (d->a->k == K_ID) var_set(e, d->a->s, v); else ligar(d->a, v, e, 0); }
            else if (d->a->k == K_ID) decl(e, d->a->s, v);
            else ligar(d->a, v, e, 1);
        }
        return C_N;
    case S_EXP: ev(x->a, e); return C_N;
    case S_BLOCK: {
        env *b = x->op ? novo_env(e, 4) : e;
        if (x->op) hoist(x->a, b);
        for (no *s = x->a; s; s = s->prox) { int c = ex(s, b); if (c) return c; }
        return C_N;
    }
    case S_IF:
        if (to_bool(ev(x->a, e))) return ex(x->b, e);
        return x->c ? ex(x->c, e) : C_N;
    case S_WHILE:
        while (to_bool(ev(x->a, e))) { int c = ex(x->b, e), r = laco(c, meu); if (r == 1) break; if (r == 2) return c; }
        return C_N;
    case S_DO:
        do { int c = ex(x->b, e), r = laco(c, meu); if (r == 1) break; if (r == 2) return c; } while (to_bool(ev(x->a, e)));
        return C_N;
    case S_FOR: {
        env *f = e;
        int lex = x->a && x->a->k == S_VAR && x->a->op;
        if (lex) f = novo_env(e, 4);
        if (x->a) { if (x->a->k == S_VAR) ex(x->a, f); else ev(x->a, f); }
        for (;;) {
            if (x->b && !to_bool(ev(x->b, f))) break;
            int c = ex(x->d, f), r = laco(c, meu);
            if (r == 1) break;
            if (r == 2) return c;
            if (lex && x->op) { env *g = novo_env(e, f->n); for (int i = 0; i < f->n; i++) decl(g, f->k[i], f->v[i]); f = g; }
            if (x->c) ev(x->c, f);
        }
        return C_N;
    }
    case S_FORIN: {
        val o = ev(x->b, e), l;
        if (x->op) l = para_vetor(o);
        else if (o.t == U || o.t == NUL) return C_N;
        else { val f = get(vO(global), I("__chaves")); l = chamar(f, vU(), &o, 1); }
        for (int i = 0; i < l.o->na; i++) {
            env *f = e; no *alvo = x->a;
            int d = 0;
            if (alvo->k == S_VAR) { d = alvo->op != 0; if (d) f = novo_env(e, 2); alvo = alvo->a->a; }
            atribuir(alvo, l.o->a[i], f, d);
            int c = ex(x->c, f), r = laco(c, meu);
            if (r == 1) break;
            if (r == 2) return c;
        }
        return C_N;
    }
    case S_RET: rv = x->a ? ev(x->a, e) : vU(); return C_RET;
    case S_BRK: rot = x->s; return C_BRK;
    case S_CONT: rot = x->s; return C_CONT;
    case S_THROW: lancar(ev(x->a, e));
    case S_TRY: {
        int c = C_N, pego = tentar(x->a, e, &c);
        val pend = exc; int tem = 0;
        if (pego && fatal) __builtin_longjmp(topo_s->b, 1);
        if (pego) {
            if (x->c) {
                env *f = novo_env(e, 2);
                if (x->b) ligar(x->b, exc, f, 1);
                if (x->d) { if (tentar(x->c, f, &c)) { if (fatal) __builtin_longjmp(topo_s->b, 1); tem = 1; pend = exc; } }
                else c = ex(x->c, f);
            } else tem = 1;
        }
        if (x->d) { val r = rv; str *ro = rot; int k = ex(x->d, e); if (k) return k; rv = r; rot = ro; }
        if (tem) lancar(pend);
        return c;
    }
    case S_SWITCH: {
        val v = ev(x->a, e);
        env *f = novo_env(e, 4);
        no *c = x->b, *ini = NULL;
        for (; c; c = c->prox) if (!c->op && estrito(v, ev(c->a, f))) { ini = c; break; }
        if (!ini) for (c = x->b; c; c = c->prox) if (c->op) ini = c;
        for (c = x->b; c; c = c->prox) hoist(c->b, f);
        for (c = ini; c; c = c->prox)
            for (no *s = c->b; s; s = s->prox) {
                int k = ex(s, f);
                if (k == C_BRK && !rot) return C_N;
                if (k) return k;
            }
        return C_N;
    }
    case S_LABEL: {
        rotulo_atual = x->s;
        int c = ex(x->a, e);
        if (c == C_BRK && rot == x->s) { rot = NULL; return C_N; }
        return c;
    }
    case S_FUNC: return C_N;
    case S_CLASS: decl(e, x->s, nova_classe(x->a, e)); return C_N;
    default: return C_N;
    }
}
static val avaliar(const char *s, int n)
{
    fdef *h = novo(sizeof(fdef));
    no *p = programa(s, n, h);
    for (int i = 0; i < h->nv; i++) if (!achar(global, h->vars[i])) por_prop(global, h->vars[i], vU());
    hoist(p->a, NULL);
    val r = vU();
    for (no *x = p->a; x; x = x->prox) {
        if (x->k == S_EXP) r = ev(x->a, NULL);
        else if (ex(x, NULL) == C_RET) break;
    }
    return r;
}
/* Roda um programa no escopo global. true se terminou sem exceção. */
static int __attribute__((noinline)) rodar(const char *s, int n, val *res)
{
    salto b; salto *volatile ant = topo_s;
    b.ant = ant;
    if (__builtin_setjmp(b.b)) {
        topo_s = ant; qa = &q_global;
        if (fatal) return 0;
        val m = exc;
        if (m.t == OB) { val t = get_o(m.o, I("stack_txt"), m); (void)t; val w = get_o(m.o, I("message"), m); val nm = get_o(m.o, I("name"), m); registrar("js: excecao %s: %s\n", nm.t == ST ? nm.s->s : "?", w.t == ST ? w.s->s : "?"); }
        else if (m.t == ST) registrar("js: excecao %s\n", m.s->s);
        return 0;
    }
    topo_s = &b;
    val r = avaliar(s, n);
    if (res) *res = r;
    topo_s = ant;
    return 1;
}
static val aguardar(val v)
{
    val f = get(vO(global), I("__esperar"));
    return chamar(f, vU(), &v, 1);
}


/* ---- geradores: cada um roda numa pilha própria; yield troca de pilha ---- */
#define GER_PILHA (192 * 1024)
typedef struct ger {
    obj *fo; val este; val *args; int n;
    char *pilha;
    void *esp_g, *esp_c;   /* pilhas salvas do gerador e de quem chamou */
    int estado;            /* 0 novo, 1 parado, 2 rodando, 3 fim */
    int modo; val troca;   /* o que o next/throw/return mandou */
    int res_tipo; val res; /* 0 yield, 1 fim, 2 exceção */
    salto *g_topo; quadro *g_qa; int g_prof; char *g_lim;   /* estado do intérprete dentro do gerador */
} ger;
static ger **gers; static int n_gers, c_gers;
static ger *ger_rodando, *ger_novo;
static obj *ger_retorno;   /* exceção especial usada por return() */

/* Troca de pilha: salva ebp/ebx/esi/edi e esp em *salvar e continua em novo. */
void ger_troca(void **salvar, void *novo);
__asm__(".text\n.globl ger_troca\nger_troca:\n"
        "\tmovl 4(%esp), %eax\n\tmovl 8(%esp), %edx\n"
        "\tpushl %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl %esp, (%eax)\n\tmovl %edx, %esp\n"
        "\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static val novo_gerador(obj *o, val este, val *a, int n)
{
    ger *g = novo(sizeof(ger));
    memset(g, 0, sizeof *g);
    g->fo = o; g->este = este; g->n = n;
    g->args = novo((unsigned)(n + 1) * sizeof(val));
    if (n) memcpy(g->args, a, (size_t)n * sizeof(val));
    if (n_gers == c_gers) {
        c_gers = c_gers ? c_gers * 2 : 64;
        ger **ng = novo((unsigned)c_gers * sizeof(ger *));
        if (n_gers) memcpy(ng, gers, (size_t)n_gers * sizeof(ger *));
        gers = ng;
    }
    gers[n_gers] = g;
    val id = vI(n_gers++);
    return chamar(get(vO(global), I("__gerador")), vU(), &id, 1);
}

static __attribute__((noinline, noreturn)) void ger_entrada(void)
{
    ger *g = ger_novo;
    lim_pilha = g->pilha + 8192;
    salto b; b.ant = NULL;
    topo_s = &b;
    if (__builtin_setjmp(b.b)) {
        g = ger_rodando;
        if (!fatal && exc.t == OB && exc.o == ger_retorno) { g->res_tipo = 1; g->res = g->troca; }
        else { g->res_tipo = 2; g->res = exc; }
    } else {
        gerador_entrando = 1;
        g->res = executar_fn(g->fo, g->este, g->args, g->n, vU());
        g->res_tipo = 1;
    }
    g->estado = 3;
    void *lixo;
    ger_troca(&lixo, g->esp_c);
    __builtin_unreachable();
}

static __attribute__((noinline)) void retomar(ger *g)
{
    salto *c_topo = topo_s; quadro *c_qa = qa; int c_prof = prof;
    char *c_lim = lim_pilha; ger *c_ger = ger_rodando;
    ger_rodando = g;
    if (g->estado == 0) {
        g->pilha = aloca(GER_PILHA);
        if (!g->pilha) { g->estado = 3; g->res_tipo = 2; g->res = vT("sem memoria para gerador"); ger_rodando = c_ger; return; }
        uint32_t *t = (uint32_t *)(g->pilha + GER_PILHA - 64);
        t[0] = 0; t[1] = 0; t[2] = 0; t[3] = 0;          /* edi esi ebx ebp */
        t[4] = (uint32_t)ger_entrada;                     /* ret */
        t[5] = 0;
        g->esp_g = t;
        ger_novo = g;
    } else {
        topo_s = g->g_topo; qa = g->g_qa; prof = g->g_prof; lim_pilha = g->g_lim;
    }
    g->estado = 2;
    ger_troca(&g->esp_c, g->esp_g);
    topo_s = c_topo; qa = c_qa; prof = c_prof; lim_pilha = c_lim; ger_rodando = c_ger;
}

static __attribute__((noinline)) val produzir_um(val v)
{
    ger *g = ger_rodando;
    if (!g) erro("SyntaxError", "yield fora de gerador", NULL);
    g->res = v; g->res_tipo = 0;
    g->g_topo = topo_s; g->g_qa = qa; g->g_prof = prof; g->g_lim = lim_pilha;
    g->estado = 1;
    ger_troca(&g->esp_g, g->esp_c);
    g = ger_rodando;
    if (g->modo == 1) lancar(g->troca);
    if (g->modo == 2) { if (!ger_retorno) ger_retorno = novo_obj(p_obj); lancar(vO(ger_retorno)); }
    return g->troca;
}

static val produzir(val v, int delega)
{
    if (!delega) return produzir_um(v);
    val arr = para_vetor(v);
    for (int i = 0; i < arr.o->na; i++) produzir_um(arr.o->a[i]);
    return vU();
}


static int contar_nos(no *x, int prof_)
{
    int c = 0;
    for (; x && c < 100000; x = x->prox) {
        c++;
        if (prof_ < 200) c += contar_nos(x->a, prof_ + 1) + contar_nos(x->b, prof_ + 1) + contar_nos(x->c, prof_ + 1) + contar_nos(x->d, prof_ + 1);
    }
    return c;
}
/* __escopo(f): [[nome, função, nº de parâmetros], ...] das variáveis do escopo onde f nasceu. */
static val n_escopo(val este, val *a, int n)
{
    (void)este;
    obj *r = novo_arr(16);
    if (n < 1 || a[0].t != OB || a[0].o->cl != C_FN || !a[0].o->e) return vO(r);
    env *e = a[0].o->e;
    for (int i = 0; i < e->n; i++) {
        val v = e->v[i];
        if (v.t != OB || v.o->cl != C_FN || !v.o->fn) continue;
        int np = 0;
        for (no *q = v.o->fn->f->params; q; q = q->prox) np++;
        int ns = contar_nos(v.o->fn->f->corpo, 0);
        obj *t = novo_arr(4);
        arr_push(t, vS(e->k[i])); arr_push(t, v); arr_push(t, vI(np)); arr_push(t, vI(ns));
        arr_push(r, vO(t));
    }
    return vO(r);
}
static val n_limite(val este, val *a, int n)
{
    (void)este;
    t_suave = n > 0 && a[0].t == NU ? tempo_ms() + (unsigned)(a[0].n >> FR) : 0;
    if (t_limite < tempo_ms() + 5000) t_limite = tempo_ms() + 5000;   /* ferramenta de diagnóstico: estende a página */
    return vU();
}
/* __tentar(f, arg, ms): chama f(arg) com limite de tempo; [true, valor] ou [false, erro]. */
static val r_tentar;
static __attribute__((noinline)) int tentar_chamada(val f, val arg, unsigned ms)
{
    salto b; salto *volatile ant = topo_s; quadro *volatile q = qa; volatile int p = prof;
    b.ant = ant;
    if (__builtin_setjmp(b.b)) { topo_s = ant; qa = q; prof = p; t_suave = 0; return 0; }
    topo_s = &b;
    t_suave = tempo_ms() + ms;
    if (t_limite < t_suave + 5000) t_limite = t_suave + 5000;
    r_tentar = chamar(f, vU(), &arg, 1);
    t_suave = 0;
    topo_s = ant;
    return 1;
}
static val n_tentar(val este, val *a, int n)
{
    (void)este;
    obj *r = novo_arr(2);
    if (n < 3) return vO(r);
    int ok = tentar_chamada(a[0], a[1], (unsigned)(a[2].n >> FR));
    if (!ok && fatal) __builtin_longjmp(topo_s->b, 1);
    arr_push(r, vB(ok)); arr_push(r, ok ? r_tentar : exc);
    return vO(r);
}
/* __gnext(id, modo, valor) -> [done, value] */
static val n_gnext(val este, val *a, int n)
{
    (void)este;
    int id = n > 0 ? (int)(to_num(a[0]) >> FR) : -1, modo = n > 1 ? (int)(to_num(a[1]) >> FR) : 0;
    val v = n > 2 ? a[2] : vU();
    if (id < 0 || id >= n_gers) erro("TypeError", "gerador invalido", NULL);
    ger *g = gers[id];
    obj *r = novo_arr(2);
    if (g->estado == 2) erro("TypeError", "Generator is already running", NULL);
    if (g->estado == 0 && modo) g->estado = 3;
    if (g->estado == 3) {
        if (modo == 1) lancar(v);
        arr_push(r, vB(1)); arr_push(r, modo == 2 ? v : vU());
        return vO(r);
    }
    g->modo = modo; g->troca = v;
    retomar(g);
    if (fatal) __builtin_longjmp(topo_s->b, 1);
    if (g->estado == 3 && g->pilha) { libera(g->pilha); g->pilha = NULL; }
    if (g->res_tipo == 2) lancar(g->res);
    arr_push(r, vB(g->res_tipo == 1)); arr_push(r, g->res);
    return vO(r);
}

#include "js_nativo.h"
