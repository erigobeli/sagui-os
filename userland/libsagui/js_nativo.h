/* js_nativo.h - biblioteca nativa, expressões regulares e DOM do motor (js.c). */
#define ARG(i) ((i) < n ? a[i] : vU())

/* ---- Object, Function ---- */
static val chaves_de(obj *o, int herda)
{
    obj *r = novo_arr(0);
    if (o->cl == C_ARR) for (int i = 0; i < o->na; i++) arr_push(r, vS(num_str(de_int(i))));
    for (obj *c = o; c; c = herda ? c->proto : NULL) {
        if (c == p_obj || c == p_arr || c == p_fn || c == p_str || c == p_el) break;
        for (int i = 0; i < c->np; i++) {
            if (!c->p[i].k || c->p[i].k->s[0] == '_') continue;
            if (c != o && (c->p[i].v.t == OB && c->p[i].v.o->cl == C_FN)) continue;
            if (c->p[i].k == s_constructor || c->p[i].k == s_prototype) continue;
            arr_push(r, vS(c->p[i].k));
        }
    }
    return vO(r);
}
static val n_chaves(val este, val *a, int n)
{
    (void)este; val v = ARG(0);
    if (v.t == ST) { obj *r = novo_arr(0); for (int i = 0; i < v.s->n; i++) arr_push(r, vS(num_str(de_int(i)))); return vO(r); }
    return v.t == OB ? chaves_de(v.o, 1) : vO(novo_arr(0));
}
static val n_keys(val este, val *a, int n) { (void)este; val v = ARG(0); return v.t == OB ? chaves_de(v.o, 0) : n_chaves(este, a, n); }
static val n_getproto(val este, val *a, int n)
{
    (void)este; val v = ARG(0);
    if (v.t == ST) return vO(p_str);
    return v.t == OB && v.o->proto ? vO(v.o->proto) : vNul();
}
static val n_setproto(val este, val *a, int n) { (void)este; if (ARG(0).t == OB) a[0].o->proto = ARG(1).t == OB ? a[1].o : NULL; return ARG(0); }
static val n_create(val este, val *a, int n) { (void)este; return vO(novo_obj(ARG(0).t == OB ? a[0].o : NULL)); }
static val n_defprop(val este, val *a, int n)
{
    (void)este;
    val o = ARG(0), d = ARG(2);
    if (o.t != OB || d.t != OB) erro("TypeError", "defineProperty", NULL);
    str *k = chave(ARG(1));
    val g = get(d, I("get")), s = get(d, I("set"));
    if (g.t == OB || s.t == OB) {
        obj *ac = novo_obj(NULL); val w; w.t = AC; w.n = 0; w.o = ac;
        por_prop(ac, I("get"), g); por_prop(ac, I("set"), s);
        por_prop(o.o, k, w);
    } else por_prop(o.o, k, get(d, I("value")));
    return o;
}
static val n_hasown(val este, val *a, int n)
{
    str *k = chave(ARG(0));
    if (este.t == ST) { int i = indice(k); return vB(k == s_length || (i >= 0 && i < este.s->n)); }
    if (este.t != OB) return vB(0);
    if (este.o->cl == C_ARR) { int i = indice(k); if (i >= 0) return vB(i < este.o->na); if (k == s_length) return vB(1); }
    return vB(achar(este.o, k) != NULL);
}
static val n_objstr(val este, val *a, int n)
{
    (void)a; (void)n;
    if (este.t == OB && este.o->cl == C_ARR) return vS(I("[object Array]"));
    if (este.t == OB && este.o->cl == C_FN) return vS(I("[object Function]"));
    if (este.t == ST) return vS(I("[object String]"));
    if (este.t == NU) return vS(I("[object Number]"));
    if (este.t == U) return vS(I("[object Undefined]"));
    if (este.t == NUL) return vS(I("[object Null]"));
    return vS(I("[object Object]"));
}
static val n_mesmo(val este, val *a, int n) { (void)a; (void)n; return este; }
static val n_object(val este, val *a, int n) { (void)este; val v = ARG(0); return v.t == OB ? v : vO(novo_obj(p_obj)); }
static val n_call(val este, val *a, int n) { return chamar(este, ARG(0), n > 1 ? a + 1 : NULL, n > 1 ? n - 1 : 0); }
static val n_apply(val este, val *a, int n)
{
    val l = ARG(1);
    if (l.t != OB) return chamar(este, ARG(0), NULL, 0);
    l = para_vetor(l);
    return chamar(este, ARG(0), l.o->a, l.o->na);
}
static val n_bind(val este, val *a, int n)
{
    if (este.t != OB) erro("TypeError", "bind", NULL);
    obj *f = novo_obj(p_fn);
    f->cl = C_FN; f->ligado = este.o; f->ligado_este = ARG(0);
    if (n > 1) { f->nlig = n - 1; f->ligados = novo((unsigned)n * sizeof(val)); memcpy(f->ligados, a + 1, (size_t)(n - 1) * sizeof(val)); }
    return vO(f);
}
static val n_fnstr(val este, val *a, int n) { (void)este; (void)a; (void)n; return vS(I("function () { [native code] }")); }

/* ---- Array ---- */
static obj *arr_de(val v) { if (v.t != OB || v.o->cl != C_ARR) erro("TypeError", "not an array", NULL); return v.o; }
static val n_array(val este, val *a, int n)
{
    (void)este;
    obj *r = novo_arr(0);
    if (n == 1 && a[0].t == NU) { int c = to_i(a[0]); for (int i = 0; i < c && i < 4000000; i++) arr_push(r, vU()); }
    else for (int i = 0; i < n; i++) arr_push(r, a[i]);
    return vO(r);
}
static val n_isarr(val este, val *a, int n) { (void)este; return vB(ARG(0).t == OB && a[0].o->cl == C_ARR); }
static val n_push(val este, val *a, int n) { obj *o = arr_de(este); for (int i = 0; i < n; i++) arr_push(o, a[i]); return vI(o->na); }
static val n_pop(val este, val *a, int n) { (void)a; (void)n; obj *o = arr_de(este); return o->na ? o->a[--o->na] : vU(); }
static val n_shift(val este, val *a, int n)
{
    (void)a; (void)n; obj *o = arr_de(este);
    if (!o->na) return vU();
    val r = o->a[0];
    o->a++; o->na--; o->ca--;
    return r;
}
static val n_unshift(val este, val *a, int n)
{
    obj *o = arr_de(este);
    for (int i = 0; i < n; i++) arr_push(o, vU());
    memmove(o->a + n, o->a, (size_t)(o->na - n) * sizeof(val));
    for (int i = 0; i < n; i++) o->a[i] = a[i];
    return vI(o->na);
}
static int rel(val v, int tam, int padrao)
{
    if (v.t == U) return padrao;
    i64 x = to_num(v);
    if (x == NANV) return 0;
    if (x == INFV) return tam;
    if (x == -INFV) return 0;
    int i = para_i32(x);
    if (i < 0) { i += tam; return i < 0 ? 0 : i; }
    return i > tam ? tam : i;
}
static val n_aslice(val este, val *a, int n)
{
    val l = para_vetor(este); obj *o = l.o;
    int i = rel(ARG(0), o->na, 0), f = rel(ARG(1), o->na, o->na);
    obj *r = novo_arr(f > i ? f - i : 0);
    for (; i < f; i++) arr_push(r, o->a[i]);
    return vO(r);
}
static val n_splice(val este, val *a, int n)
{
    obj *o = arr_de(este);
    int i = rel(ARG(0), o->na, 0), c = n < 2 ? o->na - i : to_i(a[1]);
    if (c < 0) c = 0;
    if (c > o->na - i) c = o->na - i;
    obj *r = novo_arr(c);
    for (int j = 0; j < c; j++) arr_push(r, o->a[i + j]);
    int ins = n > 2 ? n - 2 : 0, nn = o->na - c + ins;
    while (o->na < nn) arr_push(o, vU());
    memmove(o->a + i + ins, o->a + i + c, (size_t)(nn - i - ins) * sizeof(val));
    for (int j = 0; j < ins; j++) o->a[i + j] = a[j + 2];
    o->na = nn;
    return vO(r);
}
static val n_join(val este, val *a, int n)
{
    val l = para_vetor(este); obj *o = l.o;
    str *sep = ARG(0).t == U ? I(",") : to_str(a[0]);
    int t = 0;
    str **ps = novo((unsigned)(o->na + 1) * sizeof(str *));
    for (int i = 0; i < o->na; i++) { val v = o->a[i]; ps[i] = v.t == U || v.t == NUL ? I("") : to_str(v); t += ps[i]->n + sep->n; }
    str *r = novo_str(NULL, t > sep->n ? t - sep->n : 0);
    int k = 0;
    for (int i = 0; i < o->na; i++) {
        if (i) { memcpy(r->s + k, sep->s, (size_t)sep->n); k += sep->n; }
        memcpy(r->s + k, ps[i]->s, (size_t)ps[i]->n); k += ps[i]->n;
    }
    return vS(r);
}
static val n_aindex(val este, val *a, int n)
{
    val l = para_vetor(este); obj *o = l.o;
    for (int i = rel(ARG(1), o->na, 0); i < o->na; i++) if (estrito(o->a[i], ARG(0))) return vI(i);
    return vI(-1);
}
static val n_concat(val este, val *a, int n)
{
    obj *o = arr_de(este), *r = novo_arr(o->na + n);
    for (int i = 0; i < o->na; i++) arr_push(r, o->a[i]);
    for (int i = 0; i < n; i++) {
        if (a[i].t == OB && a[i].o->cl == C_ARR) for (int j = 0; j < a[i].o->na; j++) arr_push(r, a[i].o->a[j]);
        else arr_push(r, a[i]);
    }
    return vO(r);
}
static val n_reverse(val este, val *a, int n)
{
    (void)a; (void)n; obj *o = arr_de(este);
    for (int i = 0, j = o->na - 1; i < j; i++, j--) { val t = o->a[i]; o->a[i] = o->a[j]; o->a[j] = t; }
    return este;
}

/* ---- String ---- */
static str *este_str(val v) { return to_str(v); }
static val sub(str *s, int i, int f) { if (f <= i) return vS(I("")); return vS(novo_str(s->s + i, f - i)); }
static val n_string(val este, val *a, int n) { (void)este; return n ? vS(to_str(a[0])) : vS(I("")); }
static val n_charat(val este, val *a, int n) { str *s = este_str(este); int i = to_i(ARG(0)); return i >= 0 && i < s->n ? vS(intern_n(s->s + i, 1)) : vS(I("")); }
static val n_charcode(val este, val *a, int n) { str *s = este_str(este); int i = to_i(ARG(0)); return i >= 0 && i < s->n ? vI((uint8_t)s->s[i]) : vN(NANV); }
static int busca(str *s, str *t, int de)
{
    for (int i = de < 0 ? 0 : de; i + t->n <= s->n; i++) if (!memcmp(s->s + i, t->s, (size_t)t->n)) return i;
    return -1;
}
static val n_sindex(val este, val *a, int n) { return vI(busca(este_str(este), to_str(ARG(0)), to_i(ARG(1)))); }
static val n_slast(val este, val *a, int n)
{
    str *s = este_str(este), *t = to_str(ARG(0));
    for (int i = s->n - t->n; i >= 0; i--) if (!memcmp(s->s + i, t->s, (size_t)t->n)) return vI(i);
    return vI(-1);
}
static val n_sslice(val este, val *a, int n) { str *s = este_str(este); return sub(s, rel(ARG(0), s->n, 0), rel(ARG(1), s->n, s->n)); }
static val n_substring(val este, val *a, int n)
{
    str *s = este_str(este);
    int i = to_i(ARG(0)), f = ARG(1).t == U ? s->n : to_i(a[1]);
    if (i < 0) i = 0;
    if (f < 0) f = 0;
    if (i > s->n) i = s->n;
    if (f > s->n) f = s->n;
    return i < f ? sub(s, i, f) : sub(s, f, i);
}
static val n_substr(val este, val *a, int n)
{
    str *s = este_str(este);
    int i = rel(ARG(0), s->n, 0), c = ARG(1).t == U ? s->n - i : to_i(a[1]);
    return sub(s, i, i + c > s->n ? s->n : i + c);
}
static val caixa(val este, int alta)
{
    str *s = este_str(este), *r = novo_str(s->s, s->n);
    for (int i = 0; i < r->n; i++) {
        uint8_t c = (uint8_t)r->s[i];
        if (alta && ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c != 0xF7 && c != 0xFF))) r->s[i] = (char)(c - 32);
        if (!alta && ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))) r->s[i] = (char)(c + 32);
    }
    return vS(r);
}
static val n_lower(val este, val *a, int n) { (void)a; (void)n; return caixa(este, 0); }
static val n_upper(val este, val *a, int n) { (void)a; (void)n; return caixa(este, 1); }
static val n_trim(val este, val *a, int n)
{
    (void)a; (void)n; str *s = este_str(este);
    int i = 0, f = s->n;
    while (i < f && (uint8_t)s->s[i] <= ' ') i++;
    while (f > i && (uint8_t)s->s[f - 1] <= ' ') f--;
    return sub(s, i, f);
}
static val n_ssplit(val este, val *a, int n)
{
    str *s = este_str(este); obj *r = novo_arr(0);
    if (ARG(0).t == U) { arr_push(r, vS(s)); return vO(r); }
    str *t = to_str(a[0]);
    if (!t->n) { for (int i = 0; i < s->n; i++) arr_push(r, vS(intern_n(s->s + i, 1))); return vO(r); }
    int i = 0, j;
    while ((j = busca(s, t, i)) >= 0) { arr_push(r, sub(s, i, j)); i = j + t->n; }
    arr_push(r, sub(s, i, s->n));
    return vO(r);
}
static val n_fromcode(val este, val *a, int n)
{
    (void)este; str *r = novo_str(NULL, n);
    for (int i = 0; i < n; i++) { int c = to_i(a[i]); r->s[i] = c > 255 ? '?' : (char)c; }
    return vS(r);
}

/* ---- Number e globais ---- */
static val n_number(val este, val *a, int n) { (void)este; return n ? vN(to_num(a[0])) : vI(0); }
static val n_boolean(val este, val *a, int n) { (void)este; return vB(to_bool(ARG(0))); }
static val n_numstr(val este, val *a, int n)
{
    i64 v = to_num(este); int b = ARG(0).t == U ? 10 : to_i(a[0]);
    if (b == 10 || b < 2 || b > 36 || esp(v)) return vS(num_str(v));
    char t[80]; int k = 79, neg = v < 0;
    u64 x = (neg ? -(u64)v : (u64)v) >> FR;
    t[79] = 0;
    do { u64 r; x = udiv(x, (unsigned)b, &r); t[--k] = "0123456789abcdefghijklmnopqrstuvwxyz"[(int)r]; } while (x);
    if (neg) t[--k] = '-';
    return vT(t + k);
}
static val n_tofixed(val este, val *a, int n)
{
    i64 v = to_num(este); int d = to_i(ARG(0));
    if (esp(v)) return vS(num_str(v));
    if (d > 6) d = 6;
    i64 e = UM; for (int i = 0; i < d; i++) e *= 10;
    u64 x = v < 0 ? -(u64)v : (u64)v;
    u64 h = x >> FR, f = x & (UM - 1), m = 1;
    for (int i = 0; i < d; i++) m *= 10;
    u64 q = (f * m + (UM >> 1)) >> FR;
    if (q >= m) { q -= m; h++; }
    char b[48]; int k = 47; b[47] = 0;
    for (int i = 0; i < d; i++) { u64 r; q = udiv(q, 10, &r); b[--k] = (char)('0' + (int)r); }
    if (d) b[--k] = '.';
    do { u64 r; h = udiv(h, 10, &r); b[--k] = (char)('0' + (int)r); } while (h);
    if (v < 0) b[--k] = '-';
    return vT(b + k);
}
static val n_parseint(val este, val *a, int n)
{
    (void)este; str *s = to_str(ARG(0));
    int i = 0, b = to_i(ARG(1)), neg = 0, tem = 0;
    i64 r = 0;
    while (i < s->n && (uint8_t)s->s[i] <= ' ') i++;
    if (i < s->n && (s->s[i] == '-' || s->s[i] == '+')) neg = s->s[i++] == '-';
    if ((!b || b == 16) && i + 1 < s->n && s->s[i] == '0' && (s->s[i + 1] | 32) == 'x') { i += 2; b = 16; }
    if (!b) b = 10;
    for (; i < s->n; i++) {
        char c = s->s[i];
        int d = dig(c) ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'z' ? (c | 32) - 'a' + 10 : 99;
        if (d >= b) break;
        r = nsoma(nmul(r, de_int(b)), de_int(d)); tem = 1;
    }
    return tem ? vN(neg ? -r : r) : vN(NANV);
}
static val n_parsefloat(val este, val *a, int n)
{
    (void)este; str *s = to_str(ARG(0));
    int i = 0, neg = 0, u;
    while (i < s->n && (uint8_t)s->s[i] <= ' ') i++;
    if (i < s->n && (s->s[i] == '-' || s->s[i] == '+')) neg = s->s[i++] == '-';
    if (i >= s->n || !(dig(s->s[i]) || s->s[i] == '.')) return vN(NANV);
    i64 r = ler_numero(s->s + i, s->n - i, &u);
    return vN(neg ? -r : r);
}
static val n_isnan(val este, val *a, int n) { (void)este; return vB(to_num(ARG(0)) == NANV); }
static val n_log(val este, val *a, int n) { (void)este; registrar("js: %s\n", to_str(ARG(0))->s); return vU(); }
static val n_eval(val este, val *a, int n) { (void)este; str *s = to_str(ARG(0)); return avaliar(s->s, s->n); }
static val n_now(val este, val *a, int n) { (void)este; (void)a; (void)n; return vN((i64)(1798761600000ULL + tempo_ms()) << FR); }
static unsigned semente = 2463534242u;
static val n_rand(val este, val *a, int n)
{
    (void)este; (void)a; (void)n;
    semente ^= semente << 13; semente ^= semente >> 17; semente ^= semente << 5; semente ^= tempo_ms();
    return vN(semente & (UM - 1));
}
static val n_alert(val este, val *a, int n) { (void)este; if (gancho) gancho->alerta(to_str(ARG(0))->s); return vU(); }
static val n_baixar(val este, val *a, int n)
{
    (void)este; unsigned t = 0;
    if (!gancho || tempo_ms() > t_limite) return vNul();
    uint8_t *d = gancho->baixar(to_str(ARG(0))->s, &t);
    if (!d) return vNul();
    str *s = novo_str((char *)d, (int)t);
    libera(d);
    return vS(s);
}

/* ---- Expressões regulares: casamento com volta atrás ---- */
enum { R_CH, R_ANY, R_SET, R_INI, R_FIM, R_B, R_NB, R_GRP, R_REF };
typedef struct rn { int t, min, max, guloso, cap, olhar, neg; uint8_t c; uint8_t *set; struct rn *alts, *prox_alt, *prox; } rn;
typedef struct { const char *s; int n, ic, ml, ng; int *caps; } rctx;
typedef struct ct { rn *n; struct ct *up; int tipo, cap, ini, cont; } ct;
static const char *rp, *rfim; static int rng;
static rn *r_alt(void);
static void set_add(uint8_t *s, int c) { s[c >> 3] |= (uint8_t)(1 << (c & 7)); }
static void set_classe(uint8_t *s, char c)
{
    int neg = c == 'D' || c == 'W' || c == 'S';
    uint8_t t[32]; memset(t, 0, 32);
    switch (c | 32) {
    case 'd': for (int i = '0'; i <= '9'; i++) set_add(t, i); break;
    case 'w': for (int i = 0; i < 128; i++) if (dig((char)i) || (id_ini((uint8_t)i) && i != '$')) set_add(t, i); break;
    default: set_add(t, ' '); for (int i = 9; i <= 13; i++) set_add(t, i); set_add(t, 0xA0);
    }
    for (int i = 0; i < 32; i++) s[i] |= neg ? (uint8_t)~t[i] : t[i];
}
static int r_escape(void)
{
    char c = *rp++;
    switch (c) {
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case 'f': return 12; case 'v': return 11; case '0': return 0;
    case 'x': { int v = hexv(rp[0]) * 16 + hexv(rp[1]); rp += 2; return v; }
    case 'u': { int v = 0; for (int i = 0; i < 4 && rp < rfim; i++) v = v * 16 + hexv(*rp++); return v > 255 ? '?' : v; }
    default: return (uint8_t)c;
    }
}
static rn *r_atomo(void)
{
    rn *x = novo(sizeof(rn));
    x->min = x->max = 1; x->guloso = 1;
    char c = *rp++;
    switch (c) {
    case '.': x->t = R_ANY; break;
    case '^': x->t = R_INI; break;
    case '$': x->t = R_FIM; break;
    case '(':
        x->t = R_GRP; x->cap = -1;
        if (rp + 1 < rfim && rp[0] == '?') {
            char d = rp[1];
            if (d == ':') rp += 2;
            else if (d == '=' || d == '!') { x->olhar = 1; x->neg = d == '!'; rp += 2; }
            else if (d == '<' && (rp[2] == '=' || rp[2] == '!')) erro("SyntaxError", "lookbehind", NULL);
            else if (d == '<') { while (rp < rfim && *rp != '>') rp++; rp++; x->cap = ++rng; }
        } else x->cap = ++rng;
        x->alts = r_alt();
        if (rp < rfim) rp++;
        break;
    case '[': {
        x->t = R_SET; x->set = novo(32);
        int neg = rp < rfim && *rp == '^';
        if (neg) rp++;
        while (rp < rfim && *rp != ']') {
            int v;
            if (*rp == '\\') {
                rp++;
                char e = *rp;
                if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') { rp++; set_classe(x->set, e); continue; }
                if (e == 'b') { rp++; v = 8; } else v = r_escape();
            } else v = (uint8_t)*rp++;
            if (rp + 1 < rfim && *rp == '-' && rp[1] != ']') {
                rp++;
                int f;
                if (*rp == '\\') { rp++; f = r_escape(); } else f = (uint8_t)*rp++;
                for (int i = v; i <= f; i++) set_add(x->set, i);
            } else set_add(x->set, v);
        }
        rp++;
        if (neg) for (int i = 0; i < 32; i++) x->set[i] = (uint8_t)~x->set[i];
        break;
    }
    case '\\': {
        char e = *rp;
        if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') { rp++; x->t = R_SET; x->set = novo(32); set_classe(x->set, e); }
        else if (e == 'b') { rp++; x->t = R_B; }
        else if (e == 'B') { rp++; x->t = R_NB; }
        else if (e >= '1' && e <= '9') { rp++; x->t = R_REF; x->cap = e - '0'; }
        else { x->t = R_CH; x->c = (uint8_t)r_escape(); }
        break;
    }
    default: x->t = R_CH; x->c = (uint8_t)c;
    }
    if (rp < rfim) {
        char q = *rp;
        if (q == '*') { x->min = 0; x->max = 1 << 30; rp++; }
        else if (q == '+') { x->min = 1; x->max = 1 << 30; rp++; }
        else if (q == '?') { x->min = 0; x->max = 1; rp++; }
        else if (q == '{' && rp + 1 < rfim && dig(rp[1])) {
            const char *p = rp + 1; int m = 0, M;
            while (dig(*p)) m = m * 10 + *p++ - '0';
            M = m;
            if (*p == ',') { p++; if (*p == '}') M = 1 << 30; else { M = 0; while (dig(*p)) M = M * 10 + *p++ - '0'; } }
            if (*p == '}') { x->min = m; x->max = M; rp = p + 1; } else return x;
        } else return x;
        if (rp < rfim && *rp == '?') { x->guloso = 0; rp++; }
    }
    return x;
}
static rn *r_seq(void)
{
    rn *pri = NULL, *ult = NULL;
    while (rp < rfim && *rp != '|' && *rp != ')') { rn *a = r_atomo(); if (ult) ult->prox = a; else pri = a; ult = a; }
    rn *h = novo(sizeof(rn));
    h->prox = pri;
    return h;
}
static rn *r_alt(void)
{
    rn *pri = r_seq(), *u = pri;
    while (rp < rfim && *rp == '|') { rp++; u->prox_alt = r_seq(); u = u->prox_alt; }
    return pri;
}
static rctx R;
static int palavra_em(int i) { if (i < 0 || i >= R.n) return 0; uint8_t c = (uint8_t)R.s[i]; return dig((char)c) || (c < 128 && id_ini(c) && c != '$'); }
static uint8_t baixa(uint8_t c) { return R.ic && c >= 'A' && c <= 'Z' ? (uint8_t)(c + 32) : c; }
static int um(rn *x, int i)
{
    if (i >= R.n) return 0;
    uint8_t c = (uint8_t)R.s[i];
    switch (x->t) {
    case R_CH: return baixa(c) == baixa(x->c);
    case R_ANY: return c != '\n' && c != '\r';
    default: {
        if (x->set[c >> 3] & (1 << (c & 7))) return 1;
        if (R.ic) { uint8_t o = c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : baixa(c); return !!(x->set[o >> 3] & (1 << (o & 7))); }
        return 0;
    }
    }
}
static int rm(rn *x, int i, ct *k);
static int rep(rn *x, int i, int c, ct *k);
static int fim_ct(int i, ct *k)
{
    if (!k) { R.caps[1] = i; return 1; }
    if (k->tipo == 2) return 1;   /* fim de lookahead */
    int s = -1, s2 = -1;
    if (k->cap > 0) { s = R.caps[k->cap * 2]; s2 = R.caps[k->cap * 2 + 1]; R.caps[k->cap * 2] = k->ini; R.caps[k->cap * 2 + 1] = i; }
    int r;
    if (k->cont == 1 << 30 && 0) r = 0;
    if (i == k->ini && k->cont > k->n->min) r = 0;   /* repetição vazia */
    else r = rep(k->n, i, k->cont, k->up);
    if (!r && k->cap > 0) { R.caps[k->cap * 2] = s; R.caps[k->cap * 2 + 1] = s2; }
    return r;
}
static int grp(rn *x, int i, int c, ct *k)
{
    ct n = { x, k, 1, x->cap, i, c + 1 };
    for (rn *a = x->alts; a; a = a->prox_alt) if (rm(a->prox, i, &n)) return 1;
    return 0;
}
static int rep(rn *x, int i, int c, ct *k)
{
    char l; if (&l < lim_pilha) return 0;
    if (x->t != R_GRP) { /* só chega aqui por grupos */ }
    if (c < x->min) return grp(x, i, c, k);
    if (x->guloso) { if (c < x->max && grp(x, i, c, k)) return 1; return rm(x->prox, i, k); }
    if (rm(x->prox, i, k)) return 1;
    return c < x->max && grp(x, i, c, k);
}
static int rm(rn *x, int i, ct *k)
{
    char l; if (&l < lim_pilha) return 0;
    for (;;) {
        if (!x) return fim_ct(i, k);
        switch (x->t) {
        case R_INI: if (i && !(R.ml && R.s[i - 1] == '\n')) return 0; x = x->prox; continue;
        case R_FIM: if (i != R.n && !(R.ml && R.s[i] == '\n')) return 0; x = x->prox; continue;
        case R_B: case R_NB: if ((palavra_em(i - 1) != palavra_em(i)) != (x->t == R_B)) return 0; x = x->prox; continue;
        case R_REF: {
            int a = R.caps[x->cap * 2], b = R.caps[x->cap * 2 + 1];
            if (a >= 0) { if (i + b - a > R.n || memcmp(R.s + a, R.s + i, (size_t)(b - a))) return 0; i += b - a; }
            x = x->prox; continue;
        }
        case R_GRP:
            if (x->olhar) {
                ct f = { x, NULL, 2, -1, i, 0 }; int ok = 0;
                for (rn *a = x->alts; a && !ok; a = a->prox_alt) ok = rm(a->prox, i, &f);
                if (ok == x->neg) return 0;
                x = x->prox; continue;
            }
            return rep(x, i, 0, k);
        default: {
            int c = 0;
            if (x->min == 1 && x->max == 1) { if (!um(x, i)) return 0; i++; x = x->prox; continue; }
            if (x->guloso) {
                while (c < x->max && um(x, i + c)) c++;
                if (c < x->min) return 0;
                for (; c >= x->min; c--) if (rm(x->prox, i + c, k)) return 1;
                return 0;
            }
            while (c < x->min) { if (!um(x, i + c)) return 0; c++; }
            for (;;) { if (rm(x->prox, i + c, k)) return 1; if (c >= x->max || !um(x, i + c)) return 0; c++; }
        }
        }
    }
}
/* __re(fonte, bandeiras, texto, início) -> [início, fim, c1i, c1f, ...] ou null */
static val n_re(val este, val *a, int n)
{
    (void)este;
    str *src_r = to_str(ARG(0)), *fl = to_str(ARG(1)), *s = to_str(ARG(2));
    static str *ult; static rn *comp; static int comp_n; static str *ult_f;
    src_r = intern(src_r); fl = intern(fl);
    if (src_r != ult || fl != ult_f) {
        rp = src_r->s; rfim = rp + src_r->n; rng = 0;
        comp = r_alt(); comp_n = rng; ult = src_r; ult_f = fl;
    }
    int *caps = novo((unsigned)(comp_n + 1) * 2 * sizeof(int));
    rctx sv = R;
    R.s = s->s; R.n = s->n; R.ng = comp_n; R.caps = caps; R.ic = R.ml = 0;
    int sticky = 0;
    for (int i = 0; i < fl->n; i++) { if (fl->s[i] == 'i') R.ic = 1; if (fl->s[i] == 'm') R.ml = 1; if (fl->s[i] == 'y') sticky = 1; }
    for (int i = to_i(ARG(3)); i <= s->n; i++) {
        for (int j = 0; j < (comp_n + 1) * 2; j++) caps[j] = -1;
        chk();
        int ok = 0;
        for (rn *h = comp; h && !ok; h = h->prox_alt) ok = rm(h->prox, i, NULL);
        if (ok) {
            caps[0] = i;
            obj *r = novo_arr((comp_n + 1) * 2);
            for (int j = 0; j < (comp_n + 1) * 2; j++) arr_push(r, vI(caps[j]));
            R = sv;
            return vO(r);
        }
        if (sticky) break;
    }
    R = sv;
    return vNul();
}

/* ---- DOM ---- */
typedef struct at { str *k, *v; struct at *prox; } at;
struct dn { int tipo; str *tag, *txt; at *at; dn *pai, *pri, *ult, *ant, *prox; obj *w; int feito; };
static dn *raiz; static int sujo;
static str *s_style, *s_onclick, *s_ls, *s_click;
#define MAX_CLIQUES 512
static dn *cliques[MAX_CLIQUES]; static int qtd_cliques;

static dn *d_novo(int tipo, str *tag) { dn *d = novo(sizeof(dn)); d->tipo = tipo; d->tag = tag; return d; }
static void d_tirar(dn *c)
{
    dn *p = c->pai;
    if (!p) return;
    if (c->ant) c->ant->prox = c->prox; else p->pri = c->prox;
    if (c->prox) c->prox->ant = c->ant; else p->ult = c->ant;
    c->pai = c->ant = c->prox = NULL;
}
static void d_por(dn *p, dn *c, dn *antes)
{
    d_tirar(c);
    c->pai = p; c->prox = antes;
    c->ant = antes ? antes->ant : p->ult;
    if (c->ant) c->ant->prox = c; else p->pri = c;
    if (antes) antes->ant = c; else p->ult = c;
}
static str *attr(dn *d, str *k) { for (at *a = d->at; a; a = a->prox) if (a->k == k) return a->v; return NULL; }
static void attr_por(dn *d, str *k, str *v)
{
    for (at *a = d->at; a; a = a->prox) if (a->k == k) { a->v = v; return; }
    at *a = novo(sizeof(at)); a->k = k; a->v = v; a->prox = NULL;
    if (!d->at) { d->at = a; return; }
    at *u = d->at; while (u->prox) u = u->prox;
    u->prox = a;
}
static str *baixo_n(const char *s, int n)
{
    char b[64]; if (n > 63) n = 63;
    for (int i = 0; i < n; i++) b[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    return intern_n(b, n);
}
static int em_lista(str *t, const char *l)
{
    int n = t->n;
    for (const char *p = l; *p; ) {
        const char *q = p; while (*q && *q != ' ') q++;
        if (q - p == n && !memcmp(p, t->s, (size_t)n)) return 1;
        p = *q ? q + 1 : q;
    }
    return 0;
}
static int igual_nc(const char *a, const char *b, int n) { for (int i = 0; i < n; i++) { char x = a[i], y = b[i]; if (x >= 'A' && x <= 'Z') x += 32; if (x != y) return 0; } return 1; }
static void html_ler(const char *h, int n, dn *base)
{
    dn *cur = base;
    int i = 0;
    while (i < n) {
        if (h[i] != '<' || i + 1 >= n || !(id_ini((uint8_t)h[i + 1]) || h[i + 1] == '/' || h[i + 1] == '!' || h[i + 1] == '?')) {
            int j = i + 1;
            while (j < n && h[j] != '<') j++;
            dn *t = d_novo(3, NULL); t->txt = novo_str(h + i, j - i); d_por(cur, t, NULL);
            i = j;
            continue;
        }
        if (h[i + 1] == '!' || h[i + 1] == '?') {
            if (i + 3 < n && h[i + 2] == '-' && h[i + 3] == '-') { i += 4; while (i + 2 < n && !(h[i] == '-' && h[i + 1] == '-' && h[i + 2] == '>')) i++; i += 3; }
            else { while (i < n && h[i] != '>') i++; i++; }
            continue;
        }
        if (h[i + 1] == '/') {
            int j = i + 2, k = j;
            while (k < n && h[k] != '>' && (uint8_t)h[k] > ' ') k++;
            str *t = baixo_n(h + j, k - j);
            while (k < n && h[k] != '>') k++;
            i = k + 1;
            for (dn *c = cur; c != base && c; c = c->pai) if (c->tag == t) { cur = c->pai; break; }
            continue;
        }
        int j = i + 1, k = j;
        while (k < n && h[k] != '>' && h[k] != '/' && (uint8_t)h[k] > ' ') k++;
        str *t = baixo_n(h + j, k - j);
        dn *e = d_novo(1, t);
        int fecha = 0;
        while (k < n && h[k] != '>') {
            if ((uint8_t)h[k] <= ' ') { k++; continue; }
            if (h[k] == '/') { fecha = k + 1 < n && h[k + 1] == '>'; k++; continue; }
            int a0 = k;
            while (k < n && h[k] != '=' && h[k] != '>' && h[k] != '/' && (uint8_t)h[k] > ' ') k++;
            str *nm = baixo_n(h + a0, k - a0), *v = I("");
            while (k < n && (uint8_t)h[k] <= ' ') k++;
            if (k < n && h[k] == '=') {
                k++;
                while (k < n && (uint8_t)h[k] <= ' ') k++;
                if (k < n && (h[k] == '"' || h[k] == '\'')) { char q = h[k++]; int v0 = k; while (k < n && h[k] != q) k++; v = novo_str(h + v0, k - v0); k++; }
                else { int v0 = k; while (k < n && h[k] != '>' && (uint8_t)h[k] > ' ') k++; v = novo_str(h + v0, k - v0); }
            }
            if (k == a0) k++;
            else attr_por(e, nm, v);
        }
        i = k + 1;
        if (em_lista(t, "li tr td th option dd dt p")) {
            for (dn *c = cur; c && c != base; c = c->pai) {
                if (c->tag == t || (t->s[0] == 't' && t->n == 2 && (c->tag->s[0] == 't' && (c->tag->s[1] == 'd' || c->tag->s[1] == 'h')) && t->s[1] != 'r')) { cur = c->pai; break; }
                if (em_lista(c->tag, "ul ol table div")) break;
            }
        } else if (em_lista(t, "div ul ol table h1 h2 h3 h4 section") && cur != base && cur->tag && cur->tag->n == 1 && cur->tag->s[0] == 'p') cur = cur->pai;
        d_por(cur, e, NULL);
        if (em_lista(t, "area base br col embed hr img input link meta param source track wbr") || fecha) continue;
        if (em_lista(t, "script style textarea title")) {
            int f = i;
            while (f < n && !(h[f] == '<' && f + 1 < n && h[f + 1] == '/' && f + 2 + t->n <= n && igual_nc(h + f + 2, t->s, t->n))) f++;
            if (f > i) { dn *x = d_novo(3, NULL); x->txt = novo_str(h + i, f - i); d_por(e, x, NULL); }
            while (f < n && h[f] != '>') f++;
            i = f + 1;
            continue;
        }
        cur = e;
    }
}
typedef struct { char *b; int n, c; } buf;
static void b_por(buf *b, const char *s, int n)
{
    if (b->n + n + 1 > b->c) { int c = (b->c ? b->c * 2 : 65536) + n; char *d = novo((unsigned)c); if (b->n) memcpy(d, b->b, (size_t)b->n); b->b = d; b->c = c; }
    memcpy(b->b + b->n, s, (size_t)n); b->n += n; b->b[b->n] = 0;
}
static void b_t(buf *b, const char *s) { b_por(b, s, (int)tam_texto(s)); }
static void b_attr(buf *b, str *k, const char *v, int n)
{
    b_t(b, " "); b_por(b, k->s, k->n); b_t(b, "=\"");
    for (int i = 0; i < n; i++) { if (v[i] == '"') b_t(b, "&quot;"); else b_por(b, v + i, 1); }
    b_t(b, "\"");
}
static void estilo_b(buf *b, obj *st)
{
    for (int i = 0; i < st->np; i++) {
        str *k = st->p[i].k;
        if (!k || st->p[i].v.t == U || st->p[i].v.t == NUL || st->p[i].v.t == OB) continue;
        for (int j = 0; j < k->n; j++) {
            char c = k->s[j];
            if (c >= 'A' && c <= 'Z') { char t[2] = { '-', (char)(c + 32) }; b_por(b, t, 2); } else b_por(b, &c, 1);
        }
        b_t(b, ":");
        str *v = to_str(st->p[i].v);
        b_por(b, v->s, v->n);
        b_t(b, ";");
    }
}
static int tem_clique(dn *d)
{
    if (attr(d, s_onclick)) return 1;
    if (!d->w) return 0;
    if (achar(d->w, s_onclick)) return 1;
    prop *l = achar(d->w, s_ls);
    return l && l->v.t == OB && achar(l->v.o, s_click);
}
static void serial(buf *b, dn *d, int final);
static void serial_filhos(buf *b, dn *d, int final) { for (dn *c = d->pri; c; c = c->prox) serial(b, c, final); }
static void serial(buf *b, dn *d, int final)
{
    if (d->tipo == 3) { b_por(b, d->txt->s, d->txt->n); return; }
    if (d->tipo == 9) { serial_filhos(b, d, final); return; }
    if (final && d->tag->n == 6 && !memcmp(d->tag->s, "script", 6)) return;
    int id = -1;
    if (final && tem_clique(d) && qtd_cliques < MAX_CLIQUES) { id = qtd_cliques; cliques[qtd_cliques++] = d; }
    int e_link = d->tag->n == 1 && d->tag->s[0] == 'a';
    int vazio = em_lista(d->tag, "area base br col embed hr img input link meta param source track wbr");
    char alvo[32];
    formatar_texto(alvo, sizeof alvo, "sagui-js:%d", id);
    if (id >= 0 && vazio) {
        str *v = attr(d, I("value"));
        b_t(b, "<a href=\""); b_t(b, alvo); b_t(b, "\">["); b_t(b, v ? v->s : "ok"); b_t(b, "]</a>");
        return;
    }
    b_t(b, "<"); b_por(b, d->tag->s, d->tag->n);
    prop *st = d->w ? achar(d->w, s_style) : NULL;
    for (at *a = d->at; a; a = a->prox) {
        if (id >= 0 && e_link && a->k == I("href")) continue;
        if (st && a->k == s_style) continue;
        b_attr(b, a->k, a->v->s, a->v->n);
    }
    if (st && st->v.t == OB) {
        buf t = { 0, 0, 0 };
        str *o = attr(d, s_style);
        if (o) { b_por(&t, o->s, o->n); b_t(&t, ";"); }
        estilo_b(&t, st->v.o);
        if (t.n) b_attr(b, s_style, t.b, t.n);
    }
    if (id >= 0 && e_link) b_attr(b, I("href"), alvo, (int)tam_texto(alvo));
    b_t(b, ">");
    if (vazio) return;
    if (id >= 0 && !e_link) { b_t(b, "<a href=\""); b_t(b, alvo); b_t(b, "\">"); }
    serial_filhos(b, d, final);
    if (id >= 0 && !e_link) b_t(b, "</a>");
    b_t(b, "</"); b_por(b, d->tag->s, d->tag->n); b_t(b, ">");
}
static void texto_de(buf *b, dn *d)
{
    if (d->tipo == 3) {
        const char *s = d->txt->s; int n = d->txt->n;
        for (int i = 0; i < n; i++) {
            if (s[i] == '&') {
                if (i + 4 < n && !memcmp(s + i, "&amp;", 5)) { b_t(b, "&"); i += 4; continue; }
                if (i + 3 < n && !memcmp(s + i, "&lt;", 4)) { b_t(b, "<"); i += 3; continue; }
                if (i + 3 < n && !memcmp(s + i, "&gt;", 4)) { b_t(b, ">"); i += 3; continue; }
            }
            b_por(b, s + i, 1);
        }
        return;
    }
    for (dn *c = d->pri; c; c = c->prox) texto_de(b, c);
}
static str *escapar(str *t)
{
    buf b = { 0, 0, 0 };
    for (int i = 0; i < t->n; i++) { char c = t->s[i]; if (c == '&') b_t(&b, "&amp;"); else if (c == '<') b_t(&b, "&lt;"); else if (c == '>') b_t(&b, "&gt;"); else b_por(&b, &c, 1); }
    return b.n ? novo_str(b.b, b.n) : I("");
}
static val embrulhar(dn *d)
{
    if (!d) return vNul();
    if (!d->w) { d->w = novo_obj(p_el); d->w->cl = C_EL; d->w->d = d; }
    return vO(d->w);
}
static dn *de_val(val v) { if (v.t != OB || v.o->cl != C_EL) erro("TypeError", "not a Node", NULL); return v.o->d; }
static dn *por_tag(dn *d, str *t)
{
    for (dn *c = d->pri; c; c = c->prox) {
        if (c->tipo == 1 && c->tag == t) return c;
        dn *r = por_tag(c, t);
        if (r) return r;
    }
    return NULL;
}
static dn *prox_el(dn *d, int ant) { while (d && d->tipo != 1) d = ant ? d->ant : d->prox; return d; }
static void limpar(dn *d) { while (d->pri) d_tirar(d->pri); }
static void por_texto(dn *d, str *t) { limpar(d); if (t->n) { dn *x = d_novo(3, NULL); x->txt = t; d_por(d, x, NULL); } sujo = 1; }
enum { D_NADA, D_TYPE, D_NAME, D_TAG, D_PAI, D_PAIEL, D_PRI, D_ULT, D_PROX, D_ANT, D_PRIEL, D_ULTEL, D_PROXEL, D_ANTEL, D_FILHOS, D_NOS, D_TEXTO, D_INNERT, D_INNER, D_OUTER,
       D_ID, D_CLASS, D_VALUE, D_HREF, D_SRC, D_DATA, D_NVALUE, D_TITLE, D_BODY, D_HEAD, D_DOCEL, D_STYLE, D_NCHILD, D_DOC, D_ALT, D_NAMEA, D_TYPEA, D_CHECKED };
static const char *const dnomes[] = { "", "nodeType", "nodeName", "tagName", "parentNode", "parentElement", "firstChild", "lastChild", "nextSibling", "previousSibling", "firstElementChild", "lastElementChild", "nextElementSibling", "previousElementSibling", "children", "childNodes", "textContent", "innerText", "innerHTML", "outerHTML",
    "id", "className", "value", "href", "src", "data", "nodeValue", "title", "body", "head", "documentElement", "style", "childElementCount", "ownerDocument", "alt", "name", "type", "checked" };
static val filhos(dn *d, int so_el)
{
    obj *r = novo_arr(0);
    for (dn *c = d->pri; c; c = c->prox) if (!so_el || c->tipo == 1) arr_push(r, embrulhar(c));
    return vO(r);
}
static val attr_val(dn *d, const char *n) { str *v = d->tipo == 1 ? attr(d, I(n)) : NULL; return vS(v ? v : I("")); }
static int el_get(obj *o, str *k, val *r)
{
    dn *d = o->d;
    if (!k->dom) return 0;
    switch (k->dom) {
    case D_TYPE: *r = vI(d->tipo); break;
    case D_NAME: case D_TAG:
        if (d->tipo != 1) { *r = vS(I(d->tipo == 3 ? "#text" : "#document")); break; }
        *r = caixa(vS(d->tag), 1); break;
    case D_PAI: *r = embrulhar(d->pai); break;
    case D_PAIEL: *r = embrulhar(d->pai && d->pai->tipo == 1 ? d->pai : NULL); break;
    case D_PRI: *r = embrulhar(d->pri); break;
    case D_ULT: *r = embrulhar(d->ult); break;
    case D_PROX: *r = embrulhar(d->prox); break;
    case D_ANT: *r = embrulhar(d->ant); break;
    case D_PRIEL: *r = embrulhar(prox_el(d->pri, 0)); break;
    case D_ULTEL: *r = embrulhar(prox_el(d->ult, 1)); break;
    case D_PROXEL: *r = embrulhar(prox_el(d->prox, 0)); break;
    case D_ANTEL: *r = embrulhar(prox_el(d->ant, 1)); break;
    case D_FILHOS: *r = filhos(d, 1); break;
    case D_NOS: *r = filhos(d, 0); break;
    case D_NCHILD: *r = vI(filhos(d, 1).o->na); break;
    case D_TEXTO: case D_INNERT: case D_DATA: case D_NVALUE: {
        if (d->tipo == 1 && (k->dom == D_DATA || k->dom == D_NVALUE)) { *r = k->dom == D_DATA ? vU() : vNul(); break; }
        buf b = { 0, 0, 0 }; texto_de(&b, d); *r = b.n ? vS(novo_str(b.b, b.n)) : vS(I("")); break;
    }
    case D_INNER: { buf b = { 0, 0, 0 }; serial_filhos(&b, d, 0); *r = b.n ? vS(novo_str(b.b, b.n)) : vS(I("")); break; }
    case D_OUTER: { buf b = { 0, 0, 0 }; serial(&b, d, 0); *r = b.n ? vS(novo_str(b.b, b.n)) : vS(I("")); break; }
    case D_ID: *r = attr_val(d, "id"); break;
    case D_CLASS: *r = attr_val(d, "class"); break;
    case D_VALUE: *r = attr_val(d, "value"); break;
    case D_HREF: *r = attr_val(d, "href"); break;
    case D_SRC: *r = attr_val(d, "src"); break;
    case D_ALT: *r = attr_val(d, "alt"); break;
    case D_NAMEA: *r = attr_val(d, "name"); break;
    case D_TYPEA: *r = attr_val(d, "type"); break;
    case D_CHECKED: *r = vB(d->tipo == 1 && attr(d, I("checked"))); break;
    case D_TITLE:
        if (d->tipo == 9) { dn *t = por_tag(d, I("title")); buf b = { 0, 0, 0 }; if (t) texto_de(&b, t); *r = b.n ? vS(novo_str(b.b, b.n)) : vS(I("")); }
        else *r = attr_val(d, "title");
        break;
    case D_BODY: { dn *t = por_tag(raiz, I("body")); *r = embrulhar(t ? t : raiz); break; }
    case D_HEAD: { dn *t = por_tag(raiz, I("head")); *r = embrulhar(t ? t : raiz); break; }
    case D_DOCEL: { dn *t = por_tag(raiz, I("html")); *r = embrulhar(t ? t : raiz); break; }
    case D_DOC: *r = embrulhar(raiz); break;
    case D_STYLE: { val s = vO(novo_obj(p_obj)); por_prop(o, s_style, s); *r = s; break; }
    default: return 0;
    }
    return 1;
}
static int el_set(obj *o, str *k, val v)
{
    dn *d = o->d;
    if (!k->dom) return 0;
    switch (k->dom) {
    case D_TEXTO: case D_INNERT: if (d->tipo == 3) { d->txt = escapar(to_str(v)); sujo = 1; } else por_texto(d, escapar(to_str(v))); return 1;
    case D_DATA: case D_NVALUE: if (d->tipo == 3) { d->txt = escapar(to_str(v)); sujo = 1; } return 1;
    case D_INNER: { str *s = to_str(v); limpar(d); html_ler(s->s, s->n, d); sujo = 1; return 1; }
    case D_ID: attr_por(d, I("id"), to_str(v)); sujo = 1; return 1;
    case D_CLASS: attr_por(d, I("class"), to_str(v)); sujo = 1; return 1;
    case D_VALUE: case D_HREF: case D_SRC: case D_ALT: case D_NAMEA: case D_TYPEA: attr_por(d, I(dnomes[k->dom]), to_str(v)); sujo = 1; return 1;
    case D_CHECKED: if (to_bool(v)) attr_por(d, I("checked"), I("")); return 1;
    case D_TITLE:
        if (d->tipo == 9) {
            str *s = to_str(v);
            dn *t = por_tag(d, I("title"));
            if (t) por_texto(t, escapar(s));
            if (gancho) gancho->titulo(s->s);
        } else attr_por(d, I("title"), to_str(v));
        return 1;
    default: return 0;
    }
}
/* Seletores: compostos tag#id.classe[attr=v], descendentes e vírgula. */
static int tem_classe(dn *d, const char *c, int n)
{
    str *v = attr(d, I("class"));
    if (!v) return 0;
    for (int i = 0; i + n <= v->n; i++)
        if (!memcmp(v->s + i, c, (size_t)n) && (!i || (uint8_t)v->s[i - 1] <= ' ') && (i + n == v->n || (uint8_t)v->s[i + n] <= ' ')) return 1;
    return 0;
}
static int sel_comp(dn *d, const char *s, int n)
{
    if (d->tipo != 1) return 0;
    int i = 0;
    while (i < n) {
        char c = s[i];
        int j = i + 1;
        if (c == '[') {
            while (j < n && s[j] != ']') j++;
            int e = i + 1; while (e < j && s[e] != '=') e++;
            int f = e; if (f > i + 1 && (s[f - 1] == '~' || s[f - 1] == '^' || s[f - 1] == '$' || s[f - 1] == '*' || s[f - 1] == '|')) f--;
            str *v = attr(d, baixo_n(s + i + 1, f - i - 1));
            if (!v) return 0;
            if (e < j) {
                int a = e + 1, b = j;
                if (b > a && (s[a] == '"' || s[a] == '\'')) { a++; b--; }
                if (v->n != b - a || memcmp(v->s, s + a, (size_t)(b - a))) return 0;
            }
            i = j + 1;
            continue;
        }
        if (c == ':') { while (j < n && s[j] != '.' && s[j] != '#' && s[j] != '[') j++; i = j; continue; }
        int ini = c == '.' || c == '#' ? i + 1 : i;
        j = ini;
        while (j < n && s[j] != '.' && s[j] != '#' && s[j] != '[' && s[j] != ':') j++;
        if (c == '.') { if (!tem_classe(d, s + ini, j - ini)) return 0; }
        else if (c == '#') { str *v = attr(d, I("id")); if (!v || v->n != j - ini || memcmp(v->s, s + ini, (size_t)(j - ini))) return 0; }
        else if (c != '*' && d->tag != baixo_n(s + ini, j - ini)) return 0;
        i = j;
    }
    return 1;
}
static int sel_uma(dn *d, const char *s, int n)
{
    while (n > 0 && ((uint8_t)s[n - 1] <= ' ' || s[n - 1] == '>' || s[n - 1] == '+' || s[n - 1] == '~')) n--;
    int i = n;
    while (i > 0 && (uint8_t)s[i - 1] > ' ' && s[i - 1] != '>') i--;
    if (!sel_comp(d, s + i, n - i)) return 0;
    while (i > 0 && ((uint8_t)s[i - 1] <= ' ' || s[i - 1] == '>')) i--;
    if (!i) return 1;
    for (dn *p = d->pai; p; p = p->pai) if (sel_uma(p, s, i)) return 1;
    return 0;
}
static int sel(dn *d, str *q)
{
    int i = 0;
    for (int j = 0; j <= q->n; j++)
        if (j == q->n || q->s[j] == ',') {
            int a = i;
            while (a < j && (uint8_t)q->s[a] <= ' ') a++;
            if (j > a && sel_uma(d, q->s + a, j - a)) return 1;
            i = j + 1;
        }
    return 0;
}
static void sel_todos(dn *d, str *q, obj *r, int um_so)
{
    for (dn *c = d->pri; c; c = c->prox) {
        if (um_so && r->na) return;
        if (c->tipo == 1 && sel(c, q)) arr_push(r, embrulhar(c));
        sel_todos(c, q, r, um_so);
    }
}
static val n_qsa(val este, val *a, int n) { obj *r = novo_arr(0); sel_todos(de_val(este), to_str(ARG(0)), r, 0); return vO(r); }
static val n_qs(val este, val *a, int n) { obj *r = novo_arr(0); sel_todos(de_val(este), to_str(ARG(0)), r, 1); return r->na ? r->a[0] : vNul(); }
static val n_matches(val este, val *a, int n) { return vB(sel(de_val(este), to_str(ARG(0)))); }
static dn *por_id(dn *d, str *id)
{
    for (dn *c = d->pri; c; c = c->prox) {
        if (c->tipo == 1) { str *v = attr(c, I("id")); if (v && str_igual(v, id)) return c; }
        dn *r = por_id(c, id);
        if (r) return r;
    }
    return NULL;
}
static val n_byid(val este, val *a, int n) { (void)este; return embrulhar(por_id(raiz, to_str(ARG(0)))); }
static val n_create_el(val este, val *a, int n) { (void)este; str *t = to_str(ARG(0)); return embrulhar(d_novo(1, baixo_n(t->s, t->n))); }
static val n_create_txt(val este, val *a, int n) { (void)este; dn *t = d_novo(3, NULL); t->txt = escapar(to_str(ARG(0))); return embrulhar(t); }
static val n_append(val este, val *a, int n) { d_por(de_val(este), de_val(ARG(0)), NULL); sujo = 1; return a[0]; }
static val n_insert(val este, val *a, int n) { d_por(de_val(este), de_val(ARG(0)), ARG(1).t == OB ? de_val(a[1]) : NULL); sujo = 1; return a[0]; }
static val n_remove_ch(val este, val *a, int n) { (void)este; d_tirar(de_val(ARG(0))); sujo = 1; return a[0]; }
static val n_getattr(val este, val *a, int n) { str *t = to_str(ARG(0)), *v = attr(de_val(este), baixo_n(t->s, t->n)); return v ? vS(v) : vNul(); }
static val n_setattr(val este, val *a, int n) { str *t = to_str(ARG(0)); attr_por(de_val(este), baixo_n(t->s, t->n), to_str(ARG(1))); sujo = 1; return vU(); }
static val n_rmattr(val este, val *a, int n)
{
    dn *d = de_val(este); str *t = to_str(ARG(0)), *k = baixo_n(t->s, t->n);
    for (at **p = &d->at; *p; p = &(*p)->prox) if ((*p)->k == k) { *p = (*p)->prox; sujo = 1; break; }
    return vU();
}
static dn *script_atual; static buf escrito;
static val n_write(val este, val *a, int n)
{
    (void)este;
    for (int i = 0; i < n; i++) { str *s = to_str(a[i]); b_por(&escrito, s->s, s->n); }
    return vU();
}

/* ---- Início, páginas e cliques ---- */
static void iniciar(const js_hooks_t *h, const char *url)
{
    gancho = h;
    for (unsigned i = 1; i < sizeof kws / sizeof *kws; i++) I(kws[i])->kw = (short)i;
    for (unsigned i = 1; i < sizeof dnomes / sizeof *dnomes; i++) I(dnomes[i])->dom = (short)i;
    s_length = I("length"); s_prototype = I("prototype"); s_constructor = I("constructor"); s_arguments = I("arguments");
    s_style = I("style"); s_onclick = I("onclick"); s_ls = I("__ls"); s_click = I("click");
    p_obj = novo_obj(NULL); p_fn = novo_obj(p_obj); p_fn->cl = C_FN; p_fn->f = n_mesmo;
    p_arr = novo_obj(p_obj); p_arr->cl = C_ARR; p_str = novo_obj(p_obj); p_num = novo_obj(p_obj); p_bool = novo_obj(p_obj); p_el = novo_obj(p_obj);
    global = novo_obj(p_obj);
    q_global.este = vO(global); q_global.func = NULL; q_global.novo = vU(); qa = &q_global;
    static const struct { const char *n; nat f; obj **p; } ctor[] = { { "Object", n_object, &p_obj }, { "Function", n_mesmo, &p_fn }, { "Array", n_array, &p_arr }, { "String", n_string, &p_str }, { "Number", n_number, &p_num }, { "Boolean", n_boolean, &p_bool }, { "Node", n_mesmo, &p_el } };
    for (unsigned i = 0; i < sizeof ctor / sizeof *ctor; i++) {
        val c = novo_nat(ctor[i].f, ctor[i].n);
        por_prop(c.o, s_prototype, vO(*ctor[i].p));
        por_prop(*ctor[i].p, s_constructor, c);
        por_prop(global, I(ctor[i].n), c);
    }
    obj *O = get(vO(global), I("Object")).o, *A = get(vO(global), I("Array")).o, *S = get(vO(global), I("String")).o;
    reg(O, "keys", n_keys); reg(O, "getOwnPropertyNames", n_keys); reg(O, "getPrototypeOf", n_getproto); reg(O, "setPrototypeOf", n_setproto); reg(O, "create", n_create); reg(O, "defineProperty", n_defprop);
    reg(p_obj, "hasOwnProperty", n_hasown); reg(p_obj, "toString", n_objstr); reg(p_obj, "valueOf", n_mesmo);
    reg(p_fn, "call", n_call); reg(p_fn, "apply", n_apply); reg(p_fn, "bind", n_bind); reg(p_fn, "toString", n_fnstr);
    reg(A, "isArray", n_isarr);
    reg(p_arr, "push", n_push); reg(p_arr, "pop", n_pop); reg(p_arr, "shift", n_shift); reg(p_arr, "unshift", n_unshift); reg(p_arr, "slice", n_aslice); reg(p_arr, "splice", n_splice);
    reg(p_arr, "join", n_join); reg(p_arr, "toString", n_join); reg(p_arr, "indexOf", n_aindex); reg(p_arr, "concat", n_concat); reg(p_arr, "reverse", n_reverse);
    reg(S, "fromCharCode", n_fromcode);
    reg(p_str, "charAt", n_charat); reg(p_str, "charCodeAt", n_charcode); reg(p_str, "indexOf", n_sindex); reg(p_str, "lastIndexOf", n_slast); reg(p_str, "slice", n_sslice);
    reg(p_str, "substring", n_substring); reg(p_str, "substr", n_substr); reg(p_str, "toLowerCase", n_lower); reg(p_str, "toUpperCase", n_upper); reg(p_str, "trim", n_trim);
    reg(p_str, "__split", n_ssplit); reg(p_str, "toString", n_string); reg(p_str, "valueOf", n_mesmo);
    reg(p_num, "toString", n_numstr); reg(p_num, "toFixed", n_tofixed); reg(p_num, "valueOf", n_mesmo);
    reg(p_bool, "toString", n_string); reg(p_bool, "valueOf", n_mesmo);
    reg(p_el, "querySelectorAll", n_qsa); reg(p_el, "querySelector", n_qs); reg(p_el, "matches", n_matches); reg(p_el, "getElementById", n_byid);
    reg(p_el, "createElement", n_create_el); reg(p_el, "createTextNode", n_create_txt); reg(p_el, "appendChild", n_append); reg(p_el, "insertBefore", n_insert); reg(p_el, "removeChild", n_remove_ch);
    reg(p_el, "getAttribute", n_getattr); reg(p_el, "setAttribute", n_setattr); reg(p_el, "removeAttribute", n_rmattr); reg(p_el, "write", n_write); reg(p_el, "writeln", n_write);
    reg(global, "__chaves", n_chaves); reg(global, "parseInt", n_parseint); reg(global, "parseFloat", n_parsefloat); reg(global, "isNaN", n_isnan);
    reg(global, "__gnext", n_gnext); reg(global, "__escopo", n_escopo); reg(global, "__limite", n_limite); reg(global, "__tentar", n_tentar);
    reg(global, "__log", n_log); reg(global, "__eval", n_eval); reg(global, "eval", n_eval); reg(global, "__now", n_now); reg(global, "__rand", n_rand); reg(global, "alert", n_alert);
    reg(global, "__baixar", n_baixar); reg(global, "__re", n_re);
    por_prop(global, I("NaN"), vN(NANV)); por_prop(global, I("Infinity"), vN(INFV)); por_prop(global, I("undefined"), vU());
    por_prop(global, I("window"), vO(global)); por_prop(global, I("self"), vO(global)); por_prop(global, I("globalThis"), vO(global));
    por_prop(global, I("document"), embrulhar(raiz));
    por_prop(global, I("__url"), vT(url));
    if (!rodar(JS_BASE, (int)tam_texto(JS_BASE), NULL)) registrar("js: base falhou\n");
}
static dn *prox_script(dn *d)
{
    for (dn *c = d->pri; c; c = c->prox) {
        if (c->tipo == 1 && c->tag == I("script") && !c->feito) return c;
        dn *r = prox_script(c);
        if (r) return r;
    }
    return NULL;
}
static void rodar_global(const char *nome)
{
    char b[48];
    int n = formatar_texto(b, sizeof b, "%s()", nome);
    rodar(b, n, NULL);
}
static int feitos, parados;
static void rodar_um(dn *s)
{
    str *tipo = attr(s, I("type")), *src_a = attr(s, I("src"));
    s->feito = 1;
    if (tipo && tipo->n && !em_lista(tipo, "text/javascript application/javascript application/x-javascript")) return;
    const char *c = NULL; int n = 0; uint8_t *d = NULL; unsigned t = 0;
    if (src_a) {
        if (tempo_ms() > t_limite || !gancho->baixar) return;
        val pode = get(vO(global), I("__permite"));
        if (pode.t == OB) { val u = vS(src_a); if (!to_bool(chamar(pode, vU(), &u, 1))) return; }
        d = gancho->baixar(src_a->s, &t);
        if (!d) return;
        c = (const char *)d; n = (int)t;
    } else if (s->pri) { c = s->pri->txt->s; n = s->pri->txt->n; }
    if (!c) return;
    script_atual = s;
    escrito.n = 0;
    int ok = rodar(c, n, NULL);
    if (d) libera(d);
    if (ok) feitos++; else parados++;
    if (escrito.n) {
        dn tmp; memset(&tmp, 0, sizeof tmp); tmp.tipo = 9;
        html_ler(escrito.b, escrito.n, &tmp);
        while (tmp.pri) d_por(s->pai, tmp.pri, s->prox);
        sujo = 1;
    }
    script_atual = NULL;
}
static const char *pg_h; static int pg_n; static const char *pg_url; static const js_hooks_t *pg_g;
static char *saida; static int saida_n; static int clique_id;
static int __attribute__((noinline)) proteger(void (*f)(void))
{
    salto b; salto *volatile ant = topo_s;
    b.ant = ant;
    if (__builtin_setjmp(b.b)) { topo_s = ant; qa = &q_global; return 0; }
    topo_s = &b;
    f();
    topo_s = ant;
    return 1;
}
static void serial_final(void)
{
    buf b = { 0, 0, 0 };
    qtd_cliques = 0;
    serial(&b, raiz, 1);
    saida = aloca((size_t)b.n + 1);
    if (!saida) return;
    if (b.n) memcpy(saida, b.b, (size_t)b.n);
    saida[b.n] = 0;
    saida_n = b.n;
}
static void pagina_scripts(void)
{
    raiz = d_novo(9, NULL);
    html_ler(pg_h, pg_n, raiz);
    iniciar(pg_g, pg_url);
    sujo = 0; feitos = parados = 0;
    dn *s;
    while (!fatal && (s = prox_script(raiz))) { rodar_um(s); if (!fatal) rodar_global("__micro"); }
    if (!fatal) rodar_global("__fim");
    /* o que os scripts criaram durante o load */
    while (!fatal && (s = prox_script(raiz))) { rodar_um(s); if (!fatal) rodar_global("__fim"); }
}
static void pagina_interna(void)
{
    if (proteger(pagina_scripts) == 0 && !raiz) return;
    registrar("cipo: js %d scripts, %d parados%s\n", feitos, parados, fatal ? ", cortado" : "");
    fatal = 0;
    if (sujo && raiz) { ar_total = 0; proteger(serial_final); }
}
static void clique_interno(void)
{
    if (clique_id < 0 || clique_id >= qtd_cliques) return;
    val alvo = embrulhar(cliques[clique_id]);
    t_limite = tempo_ms() + 3000;
    sujo = 0;
    val f = get(vO(global), I("__clicar"));
    salto b; (void)b;
    chamar(f, vU(), &alvo, 1);
}
static void clique_seguro(void)
{
    proteger(clique_interno);
    fatal = 0;
    ar_total = 0;
    proteger(serial_final);
}
static char *pilha_js;
static void na_pilha(void (*f)(void))
{
    if (!pilha_js) { pilha_js = aloca(PILHA_TAM); if (!pilha_js) return; }
    lim_pilha = pilha_js + 65536;
    char *topo = pilha_js + PILHA_TAM - 16;
    __asm__ volatile("movl %%esp, %%ebx\n\tmovl %0, %%esp\n\tcall *%1\n\tmovl %%ebx, %%esp"
                     :: "r"(topo), "r"(f) : "ebx", "eax", "ecx", "edx", "memory", "cc");
}
void js_fim(void)
{
    while (blocos) { bloco *b = blocos; blocos = b->prox; libera(b); }
    ar = NULL; ar_u = ar_c = ar_total = 0;
    tab = NULL; tab_c = tab_n = 0; tb = NULL; tb_c = 0;
    raiz = NULL; global = NULL; qtd_cliques = 0; topo_s = NULL; fatal = 0;
    memset(&escrito, 0, sizeof escrito);
}
/* Roda os scripts da página. Devolve o HTML novo (aloca) ou NULL se nada mudou. */
char *js_pagina(const char *h, int n, const char *url, const js_hooks_t *g, int *saida_tam)
{
    js_fim();
    pg_h = h; pg_n = n; pg_url = url; pg_g = g;
    saida = NULL; saida_n = 0;
    t_limite = tempo_ms() + 15000;
    na_pilha(pagina_interna);
    *saida_tam = saida_n;
    return saida;
}
char *js_clicar(int id, int *saida_tam)
{
    if (!global) return NULL;
    clique_id = id; saida = NULL; saida_n = 0;
    na_pilha(clique_seguro);
    *saida_tam = saida_n;
    return saida;
}
