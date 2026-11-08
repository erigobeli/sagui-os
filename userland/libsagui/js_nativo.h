static str *video_src;   /* último src dado a um <video> pelo JavaScript */
/* js_nativo.h - biblioteca nativa, expressões regulares e DOM do motor (js.c). */
#define ARG(i) ((i) < n ? a[i] : vU())

/* ---- Object, Function ---- */
static val chaves_de(obj *o, int herda)
{
    obj *r = novo_arr(0);
    if (o->cl == C_ARR) for (int i = 0; i < o->na; i++) arr_push(r, vS(num_str((double)i)));
    for (obj *c = o; c; c = herda ? c->proto : NULL) {
        if (c == p_obj || c == p_arr || c == p_fn || c == p_str || c == p_el) break;
        for (int i = 0; i < c->np; i++) {
            if (!c->p[i].k || (c->p[i].k->s[0] == '_' && c->p[i].k->s[1] == '_')) continue;   /* __x: internas do Cipó */
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
    if (v.t == ST) { obj *r = novo_arr(0); for (int i = 0; i < v.s->n; i++) arr_push(r, vS(num_str((double)i))); return vO(r); }
    return v.t == OB ? chaves_de(v.o, 1) : vO(novo_arr(0));
}
/* __def: copia todas as propriedades (inclusive as internas __x, que o for-in não mostra) */
static val n_copiar(val este, val *a, int n)
{
    (void)este;
    if (ARG(0).t != OB || ARG(1).t != OB) return vU();
    obj *d = a[0].o, *o = a[1].o;
    for (int i = 0; i < o->np; i++) if (o->p[i].k) por_prop(d, o->p[i].k, o->p[i].v);
    return vU();
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
    double x = to_num(v);
    if (d_nan(x)) return 0;
    if (x >= tam) return tam;
    if (x <= -tam) return 0;
    int i = (int)m_trunc(x);
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
    medir_str(r);
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

/* ---- String (índices em unidades UTF-16; texto guardado em WTF-8) ---- */
static str *este_str(val v) { return to_str(v); }
static val sub(str *s, int i, int f) { if (f <= i) return vS(I("")); return vS(fatia_u(s, i, f)); }
static val n_string(val este, val *a, int n) { (void)este; return n ? vS(to_str(a[0])) : vS(I("")); }
static val n_charat(val este, val *a, int n) { str *s = este_str(este); int i = to_i(ARG(0)); return i >= 0 && i < s->u ? vS(fatia_u(s, i, i + 1)) : vS(I("")); }
static val n_charcode(val este, val *a, int n) { str *s = este_str(este); int c = unidade(s, to_i(ARG(0))); return c >= 0 ? vI(c) : vN(D_NAN); }
static val n_codepoint(val este, val *a, int n)
{
    str *s = este_str(este); int i = to_i(ARG(0)), c = unidade(s, i);
    if (c < 0) return vU();
    if (c >= 0xD800 && c <= 0xDBFF) { int d = unidade(s, i + 1); if (d >= 0xDC00 && d <= 0xDFFF) return vI(0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00)); }
    return vI(c);
}
/* busca em bytes (UTF-8 se sincroniza sozinho) com início e resultado em unidades */
static int busca(str *s, str *t, int de)
{
    int meio, b = u_para_b(s, de < 0 ? 0 : de, &meio);
    for (int i = b; i + t->n <= s->n; i++) if (!memcmp(s->s + i, t->s, (size_t)t->n)) return b_para_u(s, i);
    return -1;
}
static val n_sindex(val este, val *a, int n) { return vI(busca(este_str(este), to_str(ARG(0)), to_i(ARG(1)))); }
static val n_slast(val este, val *a, int n)
{
    str *s = este_str(este), *t = to_str(ARG(0));
    for (int i = s->n - t->n; i >= 0; i--) if (!memcmp(s->s + i, t->s, (size_t)t->n)) return vI(b_para_u(s, i));
    return vI(-1);
}
static val n_sslice(val este, val *a, int n) { str *s = este_str(este); return sub(s, rel(ARG(0), s->u, 0), rel(ARG(1), s->u, s->u)); }
static val n_substring(val este, val *a, int n)
{
    str *s = este_str(este);
    int i = to_i(ARG(0)), f = ARG(1).t == U ? s->u : to_i(a[1]);
    if (i < 0) i = 0;
    if (f < 0) f = 0;
    if (i > s->u) i = s->u;
    if (f > s->u) f = s->u;
    return i < f ? sub(s, i, f) : sub(s, f, i);
}
static val n_substr(val este, val *a, int n)
{
    str *s = este_str(este);
    int i = rel(ARG(0), s->u, 0), c = ARG(1).t == U ? s->u - i : to_i(a[1]);
    return sub(s, i, i + c > s->u ? s->u : i + c);
}
/* maiúsculas/minúsculas por código: Latin-1, Latin Extended-A, grego, cirílico; ß -> SS */
static uint32_t js_maiusc(uint32_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c == 0xB5) return 0x39C;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        bool pi = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        return (pi ? !(c & 1) : (c & 1)) ? c - 1 : c;
    }
    if (c == 0x131) return 'I';
    if (c == 0x17F) return 'S';
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c == 0x3C2) return 0x3A3;
    if (c >= 0x3AC && c <= 0x3AF) return c == 0x3AC ? 0x386 : c - 37;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}
static uint32_t js_minusc(uint32_t c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
    if (c == 0x178) return 0xFF;
    if (c == 0x130) return 'i';
    if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        bool pi = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        return (pi ? (c & 1) : !(c & 1)) ? c + 1 : c;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c == 0x386) return 0x3AC;
    if (c >= 0x388 && c <= 0x38A) return c + 37;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}
static val caixa(val este, int alta)
{
    str *s = este_str(este);
    if (s->ascii) {
        str *r = novo_str(s->s, s->n);
        for (int i = 0; i < r->n; i++) { char c = r->s[i]; if (alta && c >= 'a' && c <= 'z') r->s[i] = (char)(c - 32); if (!alta && c >= 'A' && c <= 'Z') r->s[i] = (char)(c + 32); }
        return vS(r);
    }
    char *b = novo((unsigned)s->n * 2 + 4); int k = 0;
    for (int i = 0; i < s->n;) {
        int kk; uint32_t c = cp_em(s->s + i, s->n - i, &kk); i += kk;
        if (alta && c == 0xDF) { b[k++] = 'S'; b[k++] = 'S'; continue; }
        k += wtf8_por(b + k, alta ? js_maiusc(c) : js_minusc(c));
    }
    return vS(novo_str(b, k));
}
static val n_lower(val este, val *a, int n) { (void)a; (void)n; return caixa(este, 0); }
static val n_upper(val este, val *a, int n) { (void)a; (void)n; return caixa(este, 1); }
/* espaço em branco do JS: ASCII, NBSP, BOM, U+1680, U+2000-200A, 2028/2029, 202F, 205F, 3000 */
static int eh_branco_js(uint32_t c)
{
    return c == ' ' || (c >= 9 && c <= 13) || c == 0xA0 || c == 0xFEFF || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
static val n_trim(val este, val *a, int n)
{
    (void)a; (void)n; str *s = este_str(este);
    int i = 0, f = s->n, k;
    while (i < f && eh_branco_js(cp_em(s->s + i, f - i, &k))) i += k;
    while (f > i) {
        int b = f - 1; while (b > i && ((uint8_t)s->s[b] & 0xC0) == 0x80) b--;
        if (!eh_branco_js(cp_em(s->s + b, f - b, &k))) break;
        f = b;
    }
    return vS(novo_str(s->s + i, f - i));
}
static val n_ssplit(val este, val *a, int n)
{
    str *s = este_str(este); obj *r = novo_arr(0);
    if (ARG(0).t == U) { arr_push(r, vS(s)); return vO(r); }
    str *t = to_str(a[0]);
    if (!t->n) { for (int i = 0; i < s->u; i++) arr_push(r, vS(fatia_u(s, i, i + 1))); return vO(r); }
    int i = 0, j;
    for (;;) {
        int meio, b = u_para_b(s, i, &meio), achou = -1;
        for (int q = b; q + t->n <= s->n; q++) if (!memcmp(s->s + q, t->s, (size_t)t->n)) { achou = q; break; }
        if (achou < 0) break;
        arr_push(r, vS(novo_str(s->s + b, achou - b)));
        j = b_para_u(s, achou + t->n);
        i = j;
    }
    { int meio, b = u_para_b(s, i, &meio); arr_push(r, vS(novo_str(s->s + b, s->n - b))); }
    return vO(r);
}
static val n_fromcode(val este, val *a, int n)
{
    (void)este;
    char *b = novo((unsigned)n * 3 + 4); int k = 0;
    for (int i = 0; i < n; i++) {
        uint32_t c = (uint32_t)to_i(a[i]) & 0xFFFF;
        /* par alta+baixa vira um caractere */
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n) {
            uint32_t d = (uint32_t)to_i(a[i + 1]) & 0xFFFF;
            if (d >= 0xDC00 && d <= 0xDFFF) { k += wtf8_por(b + k, 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00)); i++; continue; }
        }
        k += wtf8_por(b + k, c);
    }
    return vS(novo_str(b, k));
}
static val n_fromcodepoint(val este, val *a, int n)
{
    (void)este;
    char *b = novo((unsigned)n * 4 + 4); int k = 0;
    for (int i = 0; i < n; i++) { int c = to_i(a[i]); if (c < 0 || c > 0x10FFFF) erro("RangeError", "Invalid code point", NULL); k += wtf8_por(b + k, (uint32_t)c); }
    return vS(novo_str(b, k));
}

/* ---- JSON (RFC 8259) ---- */
typedef struct { const char *s; int n, i; } jctx;
static __attribute__((noreturn)) void json_erro(jctx *j)
{
    char m[96];
    if (j->i >= j->n) formatar_texto(m, sizeof m, "Unexpected end of JSON input");
    else formatar_texto(m, sizeof m, "Unexpected token '%c' in JSON at position %d", j->s[j->i], j->i);
    erro("SyntaxError", m, NULL);
}
static void json_esp(jctx *j) { while (j->i < j->n && (j->s[j->i] == ' ' || j->s[j->i] == '\t' || j->s[j->i] == '\n' || j->s[j->i] == '\r')) j->i++; }
static int json_hex4(jctx *j)
{
    int v = 0;
    for (int k = 0; k < 4; k++) { if (j->i >= j->n || hexv(j->s[j->i]) < 0) json_erro(j); v = v * 16 + hexv(j->s[j->i++]); }
    return v;
}
static str *json_texto(jctx *j)
{
    j->i++;   /* aspas */
    int ini = j->i;
    while (j->i < j->n && j->s[j->i] != '"' && j->s[j->i] != '\\') { if ((uint8_t)j->s[j->i] < 0x20) json_erro(j); j->i++; }
    if (j->i < j->n && j->s[j->i] == '"') { str *r = novo_str(j->s + ini, j->i - ini); j->i++; return r; }
    int cap = (j->n - ini) + 8, k = j->i - ini;
    char *b = novo((unsigned)cap);
    memcpy(b, j->s + ini, (size_t)k);
    for (;;) {
        if (j->i >= j->n) json_erro(j);
        char c = j->s[j->i];
        if (c == '"') { j->i++; break; }
        if ((uint8_t)c < 0x20) json_erro(j);
        if (c != '\\') { b[k++] = c; j->i++; continue; }
        j->i++;
        if (j->i >= j->n) json_erro(j);
        char e = j->s[j->i++];
        switch (e) {
        case '"': b[k++] = '"'; break; case '\\': b[k++] = '\\'; break; case '/': b[k++] = '/'; break;
        case 'b': b[k++] = 8; break; case 'f': b[k++] = 12; break; case 'n': b[k++] = '\n'; break; case 'r': b[k++] = '\r'; break; case 't': b[k++] = '\t'; break;
        case 'u': {
            uint32_t v = (uint32_t)json_hex4(j);
            if (v >= 0xD800 && v <= 0xDBFF && j->i + 5 < j->n && j->s[j->i] == '\\' && j->s[j->i + 1] == 'u') {
                int sv = j->i; j->i += 2;
                uint32_t d = (uint32_t)json_hex4(j);
                if (d >= 0xDC00 && d <= 0xDFFF) v = 0x10000 + ((v - 0xD800) << 10) + (d - 0xDC00); else j->i = sv;
            }
            k += wtf8_por(b + k, v);
            break;
        }
        default: j->i--; json_erro(j);
        }
    }
    return novo_str(b, k);
}
static val json_valor(jctx *j, int prof)
{
    if (prof > 512) erro("RangeError", "JSON nesting too deep", NULL);
    json_esp(j);
    if (j->i >= j->n) json_erro(j);
    char c = j->s[j->i];
    if (c == '{') {
        j->i++;
        obj *o = novo_obj(p_obj);
        json_esp(j);
        if (j->i < j->n && j->s[j->i] == '}') { j->i++; return vO(o); }
        for (;;) {
            json_esp(j);
            if (j->i >= j->n || j->s[j->i] != '"') json_erro(j);
            str *k = json_texto(j);
            json_esp(j);
            if (j->i >= j->n || j->s[j->i] != ':') json_erro(j);
            j->i++;
            val v = json_valor(j, prof + 1);
            por_prop(o, intern(k), v);
            json_esp(j);
            if (j->i < j->n && j->s[j->i] == ',') { j->i++; continue; }
            if (j->i < j->n && j->s[j->i] == '}') { j->i++; return vO(o); }
            json_erro(j);
        }
    }
    if (c == '[') {
        j->i++;
        obj *r = novo_arr(0);
        json_esp(j);
        if (j->i < j->n && j->s[j->i] == ']') { j->i++; return vO(r); }
        for (;;) {
            arr_push(r, json_valor(j, prof + 1));
            json_esp(j);
            if (j->i < j->n && j->s[j->i] == ',') { j->i++; continue; }
            if (j->i < j->n && j->s[j->i] == ']') { j->i++; return vO(r); }
            json_erro(j);
        }
    }
    if (c == '"') return vS(json_texto(j));
    if (c == 't' && j->i + 4 <= j->n && !memcmp(j->s + j->i, "true", 4)) { j->i += 4; return vB(1); }
    if (c == 'f' && j->i + 5 <= j->n && !memcmp(j->s + j->i, "false", 5)) { j->i += 5; return vB(0); }
    if (c == 'n' && j->i + 4 <= j->n && !memcmp(j->s + j->i, "null", 4)) { j->i += 4; return vNul(); }
    if (c == '-' || (c >= '0' && c <= '9')) {
        int ini = j->i, neg = c == '-';
        if (neg) j->i++;
        if (j->i >= j->n || !(j->s[j->i] >= '0' && j->s[j->i] <= '9')) json_erro(j);
        if (j->s[j->i] == '0' && j->i + 1 < j->n && j->s[j->i + 1] >= '0' && j->s[j->i + 1] <= '9') { j->i++; json_erro(j); }
        int u; double v = ler_numero(j->s + j->i, j->n - j->i, &u);
        /* o número do JSON não aceita "1." nem ".5" */
        const char *q = j->s + j->i;
        for (int t = 0; t < u; t++) if (q[t] == '.' && (t + 1 >= u || !(q[t + 1] >= '0' && q[t + 1] <= '9'))) { j->i += t + 1; json_erro(j); }
        j->i += u;
        (void)ini;
        return vN(neg ? -v : v);
    }
    json_erro(j);
}
static val n_json_parse(val este, val *a, int n)
{
    (void)este;
    str *s = to_str(ARG(0));
    jctx j = { s->s, s->n, 0 };
    val v = json_valor(&j, 0);
    json_esp(&j);
    if (j.i < j.n) json_erro(&j);
    return v;
}
/* texto entre aspas no formato do JSON.stringify */
static val n_json_q(val este, val *a, int n)
{
    (void)este;
    str *s = to_str(ARG(0));
    char *b = novo((unsigned)s->n * 6 + 3); int k = 0;
    b[k++] = '"';
    for (int i = 0; i < s->n;) {
        uint8_t c = (uint8_t)s->s[i];
        if (c == '"' || c == '\\') { b[k++] = '\\'; b[k++] = (char)c; i++; continue; }
        if (c < 0x20) {
            b[k++] = '\\';
            if (c == '\n') b[k++] = 'n'; else if (c == '\r') b[k++] = 'r'; else if (c == '\t') b[k++] = 't'; else if (c == 8) b[k++] = 'b'; else if (c == 12) b[k++] = 'f';
            else { formatar_texto(b + k, 6, "u%04x", c); k += 5; }
            i++; continue;
        }
        if (c == 0xED && i + 2 < s->n && ((uint8_t)s->s[i + 1] & 0xE0) == 0xA0) {   /* metade de par solta: \udxxx */
            uint32_t v = 0xD000 | ((uint32_t)(s->s[i + 1] & 63) << 6) | (uint32_t)(s->s[i + 2] & 63);
            formatar_texto(b + k, 7, "\\u%04x", v); k += 6; i += 3; continue;
        }
        b[k++] = (char)c; i++;
    }
    b[k++] = '"';
    return vS(novo_str(b, k));
}

/* ---- Number e globais ---- */
static val n_number(val este, val *a, int n) { (void)este; return n ? vN(to_num(a[0])) : vI(0); }
static val n_boolean(val este, val *a, int n) { (void)este; return vB(to_bool(ARG(0))); }
/* Number.prototype.toString(raiz): mesmo algoritmo do V8 (DoubleToRadixCString):
 * fração até a precisão do número (delta = meia distância ao vizinho) */
static val n_numstr(val este, val *a, int n)
{
    double v = to_num(este); int b = ARG(0).t == U ? 10 : to_i(a[0]);
    if (b < 2 || b > 36) erro("RangeError", "toString() radix must be between 2 and 36", NULL);
    if (b == 10 || !d_fin(v) || d_nan(v)) return vS(num_str(v));
    static const char cs[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static char buf[2200];
    int pos = 1100, ini = pos;
    int neg = v < 0; if (neg) v = -v;
    double inteiro = m_floor(v), fr = v - inteiro;
    double delta = 0.5 * (d_de(d_bits(v) + 1) - v);
    double minimo = d_de(1);
    if (delta < minimo) delta = minimo;
    int fp = pos;
    if (fr >= delta) {
        buf[fp++] = '.';
        do {
            fr *= b; delta *= b;
            int dg = (int)fr;
            buf[fp++] = cs[dg];
            fr -= dg;
            if (fr > 0.5 || (fr == 0.5 && (dg & 1))) {
                if (fr + delta > 1) {
                    /* arredonda para cima e propaga */
                    for (;;) {
                        fp--;
                        if (fp == pos) { inteiro += 1; break; }
                        char c = buf[fp];
                        int d = c > '9' ? c - 'a' + 10 : c - '0';
                        if (d + 1 < b) { buf[fp++] = cs[d + 1]; break; }
                    }
                    break;
                }
            }
        } while (fr >= delta && fp < 2190);
    }
    /* parte inteira: dígitos de baixo para cima, exatos enquanto o expoente permite */
    while (((d_bits(inteiro / b) >> 52) & 0x7FF) > 1075) { inteiro /= b; buf[--ini] = '0'; }
    do {
        double resto = m_func(30, inteiro, b);
        buf[--ini] = cs[(int)resto];
        inteiro = (inteiro - resto) / b;
    } while (inteiro > 0 && ini > 1);
    if (neg) buf[--ini] = '-';
    return vS(novo_str(buf + ini, fp - ini));
}
/* toFixed / toPrecision / toExponential pela expansão decimal exata */
static val n_numfmt(val este, val *a, int n)
{
    double v = to_num(este); int modo = to_i(ARG(0)), p = ARG(1).t == U ? -1 : to_i(a[1]);
    if (d_nan(v)) return vT("NaN");
    char out[1300]; int k = 0;
    if (modo == 0) {   /* toFixed */
        if (p < 0 || p > 100) erro("RangeError", "toFixed() digits argument must be between 0 and 100", NULL);
        if (v >= 1e21 || v <= -1e21) return vS(num_str(v));
    } else if (modo == 1 && p >= 0 && (p < 1 || p > 100)) erro("RangeError", "toPrecision() argument must be between 1 and 100", NULL);
    else if (modo == 2 && p >= 0 && p > 100) erro("RangeError", "toExponential() argument must be between 0 and 100", NULL);
    if (modo == 1 && p < 0) return vS(num_str(v));
    if (v < 0 || (v == 0 && (d_bits(v) >> 63) && modo != 0)) { out[k++] = '-'; v = -v; }
    if (v < 0) v = -v;
    if (!d_fin(v)) { memcpy(out + k, "Infinity", 8); k += 8; return vS(novo_str(out, k)); }
    static char ip[1200], fp[1200], d[2500];
    int nip, nfp;
    d_exato(v, ip, &nip, fp, &nfp);
    if (modo == 0) {
        /* d = inteiro + fração, arredondado em p casas */
        int nd = 0;
        memcpy(d, ip, (size_t)nip); nd = nip;
        memcpy(d + nd, fp, (size_t)nfp); nd += nfp;
        int m = nip + p;
        if (arred_decimal(d, nd, m)) { nip++; m++; }
        memcpy(out + k, d, (size_t)nip); k += nip;
        if (p) { out[k++] = '.'; memcpy(out + k, d + nip, (size_t)p); k += p; }
        return vS(novo_str(out, k));
    }
    /* dígitos significativos e expoente */
    int nd = 0, e;
    if (nip == 1 && ip[0] == '0') {
        int z = 0; while (z < nfp && fp[z] == '0') z++;
        if (z == nfp) { e = 0; d[nd++] = '0'; }
        else { e = -(z + 1); memcpy(d, fp + z, (size_t)(nfp - z)); nd = nfp - z; }
    } else { e = nip - 1; memcpy(d, ip, (size_t)nip); nd = nip; memcpy(d + nd, fp, (size_t)nfp); nd += nfp; }
    int sig;
    if (modo == 2 && p < 0) {   /* toExponential(): só os dígitos necessários */
        char sd[32]; int kk, ns = v == 0 ? 1 : d_curto(v, sd, &kk);
        if (v == 0) { sd[0] = '0'; kk = 1; }
        memcpy(d, sd, (size_t)ns); nd = ns; e = kk - 1; sig = ns;
    } else {
        sig = modo == 1 ? p : p + 1;
        if (!(nd == 1 && d[0] == '0') && arred_decimal(d, nd, sig)) e++;
        if (nd == 1 && d[0] == '0') for (int i = 1; i < sig; i++) d[i] = '0';
    }
    if (modo == 1 && e >= -6 && e < sig) {
        if (e >= 0) { memcpy(out + k, d, (size_t)(e + 1)); k += e + 1; if (sig > e + 1) { out[k++] = '.'; memcpy(out + k, d + e + 1, (size_t)(sig - e - 1)); k += sig - e - 1; } }
        else { out[k++] = '0'; out[k++] = '.'; for (int i = 0; i < -e - 1; i++) out[k++] = '0'; memcpy(out + k, d, (size_t)sig); k += sig; }
        return vS(novo_str(out, k));
    }
    out[k++] = d[0];
    if (sig > 1) { out[k++] = '.'; memcpy(out + k, d + 1, (size_t)(sig - 1)); k += sig - 1; }
    out[k++] = 'e'; out[k++] = e < 0 ? '-' : '+';
    k += formatar_texto(out + k, 8, "%d", e < 0 ? -e : e);
    return vS(novo_str(out, k));
}
static val n_tofixed(val este, val *a, int n) { val b[2] = { vI(0), ARG(0).t == U ? vI(0) : a[0] }; return n_numfmt(este, b, 2); }
/* parseInt: espaços do JS, sinal, 0x, raiz 2..36 */
static val n_parseint(val este, val *a, int n)
{
    (void)este; str *s = to_str(ARG(0));
    int i = 0, b = to_i(ARG(1)), neg = 0, k;
    while (i < s->n) { uint32_t c = cp_em(s->s + i, s->n - i, &k); if (!eh_branco_js(c) && c != 0x2028 && c != 0x2029) break; i += k; }
    if (i < s->n && (s->s[i] == '-' || s->s[i] == '+')) neg = s->s[i++] == '-';
    if (b && (b < 2 || b > 36)) return vN(D_NAN);
    if ((!b || b == 16) && i + 1 < s->n && s->s[i] == '0' && (s->s[i + 1] | 32) == 'x') { i += 2; b = 16; }
    if (!b) b = 10;
    int u;
    double r = b == 10 ? D_NAN : ler_base(s->s + i, s->n - i, b, &u);
    if (b == 10) {   /* decimal: só os dígitos, valor correto */
        int f = i; while (f < s->n && dig(s->s[f])) f++;
        r = f > i ? ler_decimal(s->s + i, f - i, &u) : D_NAN;
    }
    return vN(neg ? -r : r);
}
static val n_parsefloat(val este, val *a, int n)
{
    (void)este; str *s = to_str(ARG(0));
    int i = 0, neg = 0, u, k;
    while (i < s->n) { uint32_t c = cp_em(s->s + i, s->n - i, &k); if (!eh_branco_js(c) && c != 0x2028 && c != 0x2029) break; i += k; }
    if (i < s->n && (s->s[i] == '-' || s->s[i] == '+')) neg = s->s[i++] == '-';
    if (s->n - i >= 8 && !memcmp(s->s + i, "Infinity", 8)) return vN(neg ? -D_INF : D_INF);
    double r = ler_decimal(s->s + i, s->n - i, &u);
    return vN(neg ? -r : r);
}
static val n_isnan(val este, val *a, int n) { (void)este; return vB(d_nan(to_num(ARG(0)))); }
static val n_mat(val este, val *a, int n) { (void)este; return vN(m_func(to_i(ARG(0)), to_num(ARG(1)), to_num(ARG(2)))); }

/* ---- ArrayBuffer e typed arrays ---- */
static obj *buf_novo(int n, val proto)
{
    if (n < 0) n = 0;
    if (n > 64 * 1024 * 1024) erro("RangeError", "Array buffer allocation failed", NULL);
    obj *b = novo_obj(proto.t == OB ? proto.o : p_obj);
    b->cl = C_BUF;
    b->mem = novo((unsigned)n + 8);
    memset(b->mem, 0, (size_t)n + 8);
    b->mem_n = n;
    return b;
}
static val n_novo_buf(val este, val *a, int n) { (void)este; return vO(buf_novo(to_i(ARG(0)), ARG(1))); }
static val n_buf_slice(val este, val *a, int n)
{
    if (este.t != OB || este.o->cl != C_BUF) erro("TypeError", "not an ArrayBuffer", NULL);
    obj *b = este.o; int t = b->mem_n;
    int i = n > 0 && a[0].t != U ? to_i(a[0]) : 0, f = n > 1 && a[1].t != U ? to_i(a[1]) : t;
    if (i < 0) i += t; if (f < 0) f += t;
    if (i < 0) i = 0; if (f > t) f = t; if (f < i) f = i;
    obj *r = buf_novo(f - i, vO(b->proto));
    memcpy(r->mem, b->mem + i, (size_t)(f - i));
    return vO(r);
}
/* __novo_ta(tipo, arg, deslocamento, tamanho, proto) */
static val n_novo_ta(val este, val *a, int n)
{
    (void)este;
    int tipo = to_i(ARG(0));
    if (tipo < 0 || tipo > 8) tipo = 1;
    val x = ARG(1), proto = ARG(4);
    obj *o = novo_obj(proto.t == OB ? proto.o : p_obj);
    o->cl = C_TA; o->vtipo = tipo;
    int es = ta_tam[tipo];
    if (x.t == OB && x.o->cl == C_BUF) {
        int off = ARG(2).t == U ? 0 : to_i(ARG(2));
        int len = ARG(3).t == U ? (x.o->mem_n - off) / es : to_i(ARG(3));
        if (off < 0 || off > x.o->mem_n || len < 0 || off + len * es > x.o->mem_n) erro("RangeError", "Invalid typed array length", NULL);
        o->vbuf = x.o; o->voff = off; o->vlen = len;
        return vO(o);
    }
    if (x.t == U || x.t == NUL || x.t == NU || x.t == BO || x.t == ST) {
        int len = x.t == NU ? to_i(x) : 0;
        if (len < 0) erro("RangeError", "Invalid typed array length", NULL);
        o->vbuf = buf_novo(len * es, vU()); o->vlen = len;
        return vO(o);
    }
    /* cópia de array, typed array ou iterável */
    val v = x;
    if (x.o->cl != C_ARR && x.o->cl != C_TA) v = para_vetor(x);
    int len = v.o->cl == C_TA ? v.o->vlen : v.o->na;
    o->vbuf = buf_novo(len * es, vU()); o->vlen = len;
    for (int i = 0; i < len; i++) ta_gravar(o, i, v.o->cl == C_TA ? ta_ler(v.o, i) : v.o->a[i]);
    return vO(o);
}
/* __ta_info(t): [tipo, buffer, deslocamento, tamanho] ou null */
static val n_ta_info(val este, val *a, int n)
{
    (void)este;
    val x = ARG(0);
    if (x.t != OB || x.o->cl != C_TA) return vNul();
    obj *r = novo_arr(4);
    arr_push(r, vI(x.o->vtipo)); arr_push(r, vO(x.o->vbuf)); arr_push(r, vI(x.o->voff)); arr_push(r, vI(x.o->vlen));
    return vO(r);
}
/* __ta_copiar(destino, origem, deslocamento): rápido entre typed arrays */
static val n_ta_copiar(val este, val *a, int n)
{
    (void)este;
    val d = ARG(0), o = ARG(1); int off = to_i(ARG(2));
    if (d.t != OB || d.o->cl != C_TA) return vU();
    if (o.t == OB && o.o->cl == C_TA) {
        int len = o.o->vlen;
        if (off < 0 || off + len > d.o->vlen) erro("RangeError", "offset is out of bounds", NULL);
        if (d.o->vtipo == o.o->vtipo) { int es = ta_tam[d.o->vtipo]; memmove(d.o->vbuf->mem + d.o->voff + off * es, o.o->vbuf->mem + o.o->voff, (size_t)(len * es)); return vU(); }
        for (int i = 0; i < len; i++) ta_gravar(d.o, off + i, ta_ler(o.o, i));
        return vU();
    }
    val v = para_vetor(o);
    if (off < 0 || off + v.o->na > d.o->vlen) erro("RangeError", "offset is out of bounds", NULL);
    for (int i = 0; i < v.o->na; i++) ta_gravar(d.o, off + i, v.o->a[i]);
    return vU();
}
/* Bytes UTF-8 de fora -> texto do JS; sequência inválida vira U+FFFD (como o TextDecoder). */
static str *de_utf8(const uint8_t *p, int len)
{
    char *b = novo((unsigned)len * 3 + 4); int k = 0;
    for (int i = 0; i < len;) {
        uint8_t c = p[i];
        if (c < 0x80) { b[k++] = (char)c; i++; continue; }
        int ext = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        uint32_t cp = ext == 1 ? c & 31u : ext == 2 ? c & 15u : c & 7u;
        int ok = ext > 0 && i + ext < len;
        if (ok) for (int e = 1; e <= ext; e++) { if (i + e >= len || (p[i + e] & 0xC0) != 0x80) { ok = 0; break; } cp = cp << 6 | (p[i + e] & 63u); }
        /* recusa sobrelongos, surrogates e > U+10FFFF */
        if (ok && ((ext == 1 && cp < 0x80) || (ext == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (ext == 3 && (cp < 0x10000 || cp > 0x10FFFF)))) ok = 0;
        if (!ok) { k += wtf8_por(b + k, 0xFFFD); i++; continue; }
        memcpy(b + k, p + i, (size_t)ext + 1); k += ext + 1; i += ext + 1;
    }
    return novo_str(b, k);
}
static obj *bytes_ta(const uint8_t *d, int n, obj *proto)
{
    obj *o = novo_obj(proto ? proto : p_obj);
    o->cl = C_TA; o->vtipo = 1;
    o->vbuf = buf_novo(n, vU()); o->vlen = n;
    if (n) memcpy(o->vbuf->mem, d, (size_t)n);
    return o;
}
/* texto do JS -> bytes UTF-8 (metade de par solta vira U+FFFD) */
static val n_utf8_cod(val este, val *a, int n)
{
    (void)este;
    str *t = to_str(ARG(0));
    char *b = novo((unsigned)t->n + 4); int k = 0;
    for (int i = 0; i < t->n;) {
        if ((uint8_t)t->s[i] == 0xED && i + 2 < t->n && ((uint8_t)t->s[i + 1] & 0xE0) == 0xA0) { k += wtf8_por(b + k, 0xFFFD); i += 3; continue; }
        b[k++] = t->s[i++];
    }
    return vO(bytes_ta((const uint8_t *)b, k, ARG(1).t == OB ? ARG(1).o : NULL));
}
static val n_utf8_dec(val este, val *a, int n)
{
    (void)este;
    val x = ARG(0);
    const uint8_t *p; int len;
    if (x.t == OB && x.o->cl == C_TA) { p = x.o->vbuf->mem + x.o->voff; len = x.o->vlen * ta_tam[x.o->vtipo]; }
    else if (x.t == OB && x.o->cl == C_BUF) { p = x.o->mem; len = x.o->mem_n; }
    else return vT("");
    return vS(de_utf8(p, len));
}
static val n_log(val este, val *a, int n) { (void)este; registrar("js: %s\n", to_str(ARG(0))->s); return vU(); }
static val n_eval(val este, val *a, int n) { (void)este; str *s = to_str(ARG(0)); return avaliar(s->s, s->n); }
/* Date.now: relógio do sistema (RTC, tomado como UTC) + milissegundos do contador */
static double agora_base; static unsigned agora_ms0;
static val n_now(val este, val *a, int n)
{
    (void)este; (void)a; (void)n;
    if (!agora_base) {
        data_t d; memset(&d, 0, sizeof d);
        ler_data(&d);
        int y = d.ano, m = d.mes, dd = d.dia;
        y -= m <= 2;
        int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
        int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        double dias = (double)era * 146097 + doe - 719468;
        agora_base = dias * 86400000.0 + d.hora * 3600000.0 + d.minuto * 60000.0 + d.segundo * 1000.0;
        agora_ms0 = tempo_ms();
    }
    return vN(agora_base + (double)(tempo_ms() - agora_ms0));
}
static unsigned semente = 2463534242u;
static val n_rand(val este, val *a, int n)
{
    (void)este; (void)a; (void)n;
    /* 53 bits aleatórios em [0, 1) */
    static u64 x = 0x9E3779B97F4A7C15ULL;
    x ^= (u64)tempo_ms() * 0xBF58476D1CE4E5B9ULL;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    (void)semente;
    return vN((double)(x >> 11) * (1.0 / 9007199254740992.0));
}
static val n_alert(val este, val *a, int n) { (void)este; if (gancho) gancho->alerta(to_str(ARG(0))->s); return vU(); }
static val n_baixar(val este, val *a, int n)
{
    (void)este; unsigned t = 0;
    if (!gancho || tempo_ms() > t_limite) return vNul();
    uint8_t *d = gancho->baixar(to_str(ARG(0))->s, &t);
    if (!d) return vNul();
    str *s = de_utf8(d, (int)t);
    libera(d);
    return vS(s);
}

/* __pedir_n(url, método, corpo, cabeçalhos, proto Uint8Array) ->
 * [código, bytes, cabeçalhos da resposta, URL final] ou null (falha de rede) */
static val n_pedir(val este, val *a, int n)
{
    (void)este; unsigned t = 0;
    if (!gancho || tempo_ms() > t_limite) return vNul();
    str *url = to_str(ARG(0)), *met = ARG(1).t == U ? I("GET") : to_str(ARG(1)), *cab = ARG(3).t == ST ? ARG(3).s : I("");
    const void *cp = ""; int cn = 0;
    val cv = ARG(2);
    if (cv.t == OB && cv.o->cl == C_TA) { cp = cv.o->vbuf->mem + cv.o->voff; cn = cv.o->vlen * ta_tam[cv.o->vtipo]; }   /* corpo binário: bytes exatos */
    else if (cv.t == OB && cv.o->cl == C_BUF) { cp = cv.o->mem; cn = cv.o->mem_n; }
    else if (cv.t != U && cv.t != NUL) { str *cs = to_str(cv); cp = cs->s; cn = cs->n; }   /* texto: UTF-8 */
    static char m[16], c[2048], rc[8192], fin[2048];
    copia_texto(m, met->s, sizeof m);
    copia_texto(c, cab->s, sizeof c);
    int prep = !(m[0] == 'G' && m[1] == 'E' && m[2] == 'T' && !m[3]) || c[0];
    if (prep) http_preparar(m, c, cp, cn);
    int codigo = 200; rc[0] = 0; copia_texto(fin, url->s, sizeof fin);
    uint8_t *d = gancho->resposta ? gancho->resposta(url->s, &t, &codigo, rc, sizeof rc, fin, sizeof fin) : gancho->baixar(url->s, &t);
    if (prep) http_preparar(NULL, NULL, NULL, 0);
    if (!d && !codigo) return vNul();
    obj *r = novo_arr(4);
    arr_push(r, vI(codigo));
    arr_push(r, vO(bytes_ta(d ? d : (const uint8_t *)"", d ? (int)t : 0, ARG(4).t == OB ? ARG(4).o : NULL)));
    arr_push(r, vS(de_utf8((const uint8_t *)rc, (int)tam_texto(rc))));
    arr_push(r, vS(de_utf8((const uint8_t *)fin, (int)tam_texto(fin))));
    if (d) libera(d);
    return vO(r);
}


/* ---- Expressões regulares: casamento com volta atrás ---- */
enum { R_CH, R_ANY, R_SET, R_INI, R_FIM, R_B, R_NB, R_GRP, R_REF };
/* Conjunto [...]: bits para < 256, faixas para o resto, e as classes negadas
 * (\D \W \S) que aceitam todo código alto (\S menos os espaços Unicode). */
typedef struct { uint8_t b[32]; int *ra; int nra, cra; uint8_t alto, neg; } rset;
typedef struct rn { int t, min, max, guloso, cap, olhar, neg; uint32_t c; rset *set; struct rn *alts, *prox_alt, *prox; } rn;
/* posição no texto = byte * 2 + metade (1 = na unidade baixa de um par, só sem a bandeira u) */
typedef struct { const char *s; int n, ic, ml, un, ng, ponto_tudo; int *caps; } rctx;
typedef struct ct { rn *n; struct ct *up; int tipo, cap, ini, cont; } ct;
static const char *rp, *rfim; static int rng, r_unicode;
static str *r_nomes[64];   /* nomes dos grupos (?<nome>...) por número */
static rn *r_alt(void);
static void set_cp(rset *s, uint32_t a, uint32_t f)
{
    for (uint32_t c = a; c <= f && c < 256; c++) s->b[c >> 3] |= (uint8_t)(1 << (c & 7));
    if (f >= 256) {
        if (s->nra + 2 > s->cra) { int c = s->cra ? s->cra * 2 : 8; int *n = novo((unsigned)c * sizeof(int)); if (s->nra) memcpy(n, s->ra, (size_t)s->nra * sizeof(int)); s->ra = n; s->cra = c; }
        s->ra[s->nra++] = (int)(a < 256 ? 256 : a); s->ra[s->nra++] = (int)f;
    }
}
static void set_classe(rset *s, char c)
{
    switch (c) {
    case 'd': set_cp(s, '0', '9'); break;
    case 'w': set_cp(s, '0', '9'); set_cp(s, 'A', 'Z'); set_cp(s, 'a', 'z'); set_cp(s, '_', '_'); break;
    case 's': for (uint32_t k = 0; k < 256; k++) if (eh_branco_js(k)) set_cp(s, k, k);
              set_cp(s, 0x1680, 0x1680); set_cp(s, 0x2000, 0x200A); set_cp(s, 0x2028, 0x2029); set_cp(s, 0x202F, 0x202F); set_cp(s, 0x205F, 0x205F); set_cp(s, 0x3000, 0x3000); set_cp(s, 0xFEFF, 0xFEFF); break;
    default: {   /* D W S: complemento nos baixos; altos todos (S: menos espaços) */
        rset t; memset(&t, 0, sizeof t);
        set_classe(&t, (char)(c | 32));
        for (int k = 0; k < 32; k++) s->b[k] |= (uint8_t)~t.b[k];
        s->alto = c == 'S' ? (s->alto == 1 ? 1 : 2) : 1;
    }
    }
}
static int set_tem(const rset *s, uint32_t c)
{
    int r;
    if (c < 256) r = !!(s->b[c >> 3] & (1 << (c & 7)));
    else {
        r = s->alto == 1 || (s->alto == 2 && !eh_branco_js(c));
        for (int k = 0; !r && k < s->nra; k += 2) if ((int)c >= s->ra[k] && (int)c <= s->ra[k + 1]) r = 1;
    }
    return s->neg ? !r : r;
}
/* código de um escape; \u alto+\u baixo vira um código só */
static uint32_t r_escape(void)
{
    char c = *rp++;
    switch (c) {
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case 'f': return 12; case 'v': return 11; case '0': return 0;
    case 'c': if (rp < rfim) return (uint32_t)(*rp++ & 31); return 'c';
    case 'x': { if (rp + 1 < rfim && hexv(rp[0]) >= 0 && hexv(rp[1]) >= 0) { int v = hexv(rp[0]) * 16 + hexv(rp[1]); rp += 2; return (uint32_t)v; } return 'x'; }
    case 'u': {
        uint32_t v = 0;
        if (rp < rfim && *rp == '{') { rp++; while (rp < rfim && *rp != '}') v = v * 16 + (uint32_t)hexv(*rp++); rp++; return v; }
        for (int i = 0; i < 4 && rp < rfim; i++) v = v * 16 + (uint32_t)hexv(*rp++);
        if (r_unicode && v >= 0xD800 && v <= 0xDBFF && rp + 5 < rfim && rp[0] == '\\' && rp[1] == 'u') {
            uint32_t d = 0; for (int i = 0; i < 4; i++) d = d * 16 + (uint32_t)hexv(rp[2 + i]);
            if (d >= 0xDC00 && d <= 0xDFFF) { rp += 6; v = 0x10000 + ((v - 0xD800) << 10) + (d - 0xDC00); }
        }
        return v;
    }
    default: { rp--; int k; uint32_t v = cp_em(rp, (int)(rfim - rp), &k); rp += k; return v; }
    }
}
/* próximo código do padrão (literal), em WTF-8 */
static uint32_t r_lit(void) { int k; uint32_t v = cp_em(rp, (int)(rfim - rp), &k); rp += k; return v; }
static rn *r_ch(uint32_t c) { rn *x = novo(sizeof(rn)); x->min = x->max = 1; x->guloso = 1; x->t = R_CH; x->c = c; return x; }
static rn *r_atomo(void)
{
    rn *x = novo(sizeof(rn)), *ret = x;
    x->min = x->max = 1; x->guloso = 1;
    char c = *rp;
    switch (c) {
    case '.': rp++; x->t = R_ANY; break;
    case '^': rp++; x->t = R_INI; break;
    case '$': rp++; x->t = R_FIM; break;
    case '(':
        rp++;
        x->t = R_GRP; x->cap = -1;
        if (rp + 1 < rfim && rp[0] == '?') {
            char d = rp[1];
            if (d == ':') rp += 2;
            else if (d == '=' || d == '!') { x->olhar = 1; x->neg = d == '!'; rp += 2; }
            else if (d == '<' && (rp[2] == '=' || rp[2] == '!')) { x->olhar = 2; x->neg = rp[2] == '!'; rp += 3; }   /* lookbehind */
            else if (d == '<') {
                const char *n0 = rp + 2; while (rp < rfim && *rp != '>') rp++;
                x->cap = ++rng;
                if (x->cap < 64) r_nomes[x->cap] = intern_n(n0, (int)(rp - n0));
                rp++;
            }
        } else x->cap = ++rng;
        x->alts = r_alt();
        if (rp < rfim) rp++;
        break;
    case '[': {
        rp++;
        x->t = R_SET; x->set = novo(sizeof(rset));
        if (rp < rfim && *rp == '^') { x->set->neg = 1; rp++; }
        while (rp < rfim && *rp != ']') {
            uint32_t v;
            if (*rp == '\\') {
                rp++;
                char e = *rp;
                if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') { rp++; set_classe(x->set, e); continue; }
                if (e == 'b') { rp++; v = 8; } else v = r_escape();
            } else v = r_lit();
            if (!r_unicode && v >= 0x10000) {   /* sem u: um caractere astral no conjunto são duas unidades */
                set_cp(x->set, 0xD800 + ((v - 0x10000) >> 10), 0xD800 + ((v - 0x10000) >> 10));
                v = 0xDC00 + ((v - 0x10000) & 0x3FF);
            }
            if (rp + 1 < rfim && *rp == '-' && rp[1] != ']') {
                rp++;
                uint32_t f;
                if (*rp == '\\') { rp++; f = r_escape(); } else f = r_lit();
                if (f >= v) set_cp(x->set, v, f);
            } else set_cp(x->set, v, v);
        }
        rp++;
        break;
    }
    case '\\': {
        rp++;
        char e = *rp;
        if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') { rp++; x->t = R_SET; x->set = novo(sizeof(rset)); set_classe(x->set, e); }
        else if (e == 'b') { rp++; x->t = R_B; }
        else if (e == 'B') { rp++; x->t = R_NB; }
        else if (e >= '1' && e <= '9') {
            rp++; x->t = R_REF; x->cap = e - '0';
            if (rp < rfim && *rp >= '0' && *rp <= '9' && x->cap * 10 + (*rp - '0') <= rng) x->cap = x->cap * 10 + (*rp++ - '0');
        }
        else if (e == 'k' && rp + 1 < rfim && rp[1] == '<') {   /* \k<nome> */
            const char *n0 = rp + 2, *f = n0; while (f < rfim && *f != '>') f++;
            str *nm = intern_n(n0, (int)(f - n0));
            x->t = R_REF; x->cap = 0;
            for (int g = 1; g <= rng && g < 64; g++) if (r_nomes[g] == nm) x->cap = g;
            rp = f < rfim ? f + 1 : f;
        }
        else { x->t = R_CH; x->c = r_escape(); }
        break;
    }
    default: x->t = R_CH; x->c = r_lit();
    }
    /* sem u, caractere astral literal = metade alta seguida da baixa */
    if (x->t == R_CH && !r_unicode && x->c >= 0x10000) {
        uint32_t v = x->c - 0x10000;
        x->c = 0xD800 + (v >> 10);
        rn *b = r_ch(0xDC00 + (v & 0x3FF));
        x->prox = b; x = b;
        /* o quantificador vale só para a metade baixa (como no JS sem u) */
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
            if (*p == '}') { x->min = m; x->max = M; rp = p + 1; } else return ret;
        } else return ret;
        if (rp < rfim && *rp == '?') { x->guloso = 0; rp++; }
    }
    return ret;
}
static rn *r_seq(void)
{
    rn *pri = NULL, *ult = NULL;
    while (rp < rfim && *rp != '|' && *rp != ')') {
        rn *a = r_atomo();
        if (ult) ult->prox = a; else pri = a;
        ult = a; while (ult->prox) ult = ult->prox;   /* astral sem u gera dois nós */
    }
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
/* unidade (sem u) ou código (com u) na posição p; *q = posição seguinte; -1 no fim */
static int32_t r_ler(int p, int *q)
{
    int b = p >> 1, meio = p & 1;
    if (b >= R.n) return -1;
    int k; uint32_t c = cp_em(R.s + b, R.n - b, &k);
    if (c >= 0x10000 && !R.un) {
        c -= 0x10000;
        if (meio) { *q = (b + k) * 2; return (int32_t)(0xDC00 + (c & 0x3FF)); }
        *q = p + 1; return (int32_t)(0xD800 + (c >> 10));
    }
    *q = (b + k) * 2;
    return (int32_t)c;
}
/* volta uma unidade/código (desfaz exatamente um r_ler) */
static int r_volta(int p)
{
    if (p & 1) return p - 1;
    int b = p >> 1;
    if (!b) return 0;
    int a = b - 1;
    while (a > 0 && ((uint8_t)R.s[a] & 0xC0) == 0x80) a--;
    if (!R.un && (uint8_t)R.s[a] >= 0xF0) return a * 2 + 1;
    return a * 2;
}
static int32_t r_antes(int p) { if (!p) return -1; int q; return r_ler(r_volta(p), &q); }
static int palavra_cp(int32_t c) { return c >= 0 && c < 128 && (dig((char)c) || (id_ini((uint8_t)c) && c != '$')); }
static uint32_t r_dobra(uint32_t c) { return R.ic ? js_minusc(c) : c; }
static int um(rn *x, int p, int *q)
{
    int32_t c = r_ler(p, q);
    if (c < 0) return 0;
    switch (x->t) {
    case R_CH: return r_dobra((uint32_t)c) == r_dobra(x->c);
    case R_ANY: return R.ponto_tudo || (c != '\n' && c != '\r' && c != 0x2028 && c != 0x2029);
    default:
        if (set_tem(x->set, (uint32_t)c)) return 1;
        if (R.ic) { uint32_t a = js_minusc((uint32_t)c), b = js_maiusc((uint32_t)c); return (a != (uint32_t)c && set_tem(x->set, a)) || (b != (uint32_t)c && set_tem(x->set, b)); }
        return 0;
    }
}
static int rm(rn *x, int i, ct *k);
static int rep(rn *x, int i, int c, ct *k);
static int fim_ct(int i, ct *k)
{
    if (!k) { R.caps[1] = i; return 1; }
    if (k->tipo == 2) return 1;   /* fim de lookahead */
    if (k->tipo == 3) return i == k->cont;   /* fim de lookbehind: tem que acabar na posição dada */
    int s = -1, s2 = -1;
    if (k->cap > 0) { s = R.caps[k->cap * 2]; s2 = R.caps[k->cap * 2 + 1]; R.caps[k->cap * 2] = k->ini; R.caps[k->cap * 2 + 1] = i; }
    int r;
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
        case R_INI: { if (i) { int32_t a = r_antes(i); if (!(R.ml && (a == '\n' || a == '\r' || a == 0x2028 || a == 0x2029))) return 0; } x = x->prox; continue; }
        case R_FIM: { int q; int32_t c = r_ler(i, &q); if (c >= 0 && !(R.ml && (c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029))) return 0; x = x->prox; continue; }
        case R_B: case R_NB: { int q; if ((palavra_cp(r_antes(i)) != palavra_cp(r_ler(i, &q))) != (x->t == R_B)) return 0; x = x->prox; continue; }
        case R_REF: {
            int a = R.caps[x->cap * 2], b = R.caps[x->cap * 2 + 1];
            if (a >= 0) {
                int p = a, j = i;
                while (p < b) {
                    int pq, jq; int32_t u = r_ler(p, &pq), v = r_ler(j, &jq);
                    if (v < 0 || r_dobra((uint32_t)u) != r_dobra((uint32_t)v)) return 0;
                    p = pq; j = jq;
                }
                i = j;
            }
            x = x->prox; continue;
        }
        case R_GRP:
            if (x->olhar == 1) {
                ct f = { x, NULL, 2, -1, i, 0 }; int ok = 0;
                for (rn *a = x->alts; a && !ok; a = a->prox_alt) ok = rm(a->prox, i, &f);
                if (ok == x->neg) return 0;
                x = x->prox; continue;
            }
            if (x->olhar == 2) {
                /* lookbehind: algum início q <= i em que o conteúdo casa terminando exatamente em i
                 * (do mais longe para o mais perto: o maior trecho, como a busca para trás do JS) */
                int ok = 0, q = 0;
                for (;;) {
                    ct f = { x, NULL, 3, -1, q, i };
                    for (rn *a = x->alts; a && !ok; a = a->prox_alt) ok = rm(a->prox, q, &f);
                    if (ok || q >= i) break;
                    int nq; if (r_ler(q, &nq) < 0) break; q = nq;
                }
                if (ok == x->neg) return 0;
                x = x->prox; continue;
            }
            return rep(x, i, 0, k);
        default: {
            int c = 0, p = i, q;
            if (x->min == 1 && x->max == 1) { if (!um(x, i, &q)) return 0; i = q; x = x->prox; continue; }
            if (x->guloso) {
                while (c < x->max && um(x, p, &q)) { p = q; c++; }
                if (c < x->min) return 0;
                for (; c >= x->min; c--) { if (rm(x->prox, p, k)) return 1; p = r_volta(p); }
                return 0;
            }
            while (c < x->min) { if (!um(x, p, &q)) return 0; p = q; c++; }
            for (;;) { if (rm(x->prox, p, k)) return 1; if (c >= x->max || !um(x, p, &q)) return 0; p = q; c++; }
        }
        }
    }
}
/* posição interna -> índice em unidades UTF-16 */
static int r_unid(str *s, int p) { return p < 0 ? -1 : b_para_u(s, p >> 1) + (p & 1); }
/* __re(fonte, bandeiras, texto, início) -> [início, fim, c1i, c1f, ...] em unidades, ou null */
static val n_re(val este, val *a, int n)
{
    (void)este;
    str *src_r = to_str(ARG(0)), *fl = to_str(ARG(1)), *s = to_str(ARG(2));
    static str *ult; static rn *comp; static int comp_n; static str *ult_f;
    src_r = intern(src_r); fl = intern(fl);
    int un = 0, sticky = 0, ic = 0, ml = 0;
    int pt = 0;
    for (int i = 0; i < fl->n; i++) { if (fl->s[i] == 'i') ic = 1; if (fl->s[i] == 'm') ml = 1; if (fl->s[i] == 'y') sticky = 1; if (fl->s[i] == 'u' || fl->s[i] == 'v') un = 1; if (fl->s[i] == 's') pt = 1; }
    if (src_r != ult || fl != ult_f) {
        rp = src_r->s; rfim = rp + src_r->n; rng = 0; r_unicode = un; memset(r_nomes, 0, sizeof r_nomes);
        comp = r_alt(); comp_n = rng; ult = src_r; ult_f = fl;
    }
    int *caps = novo((unsigned)(comp_n + 1) * 2 * sizeof(int));
    rctx sv = R;
    R.s = s->s; R.n = s->n; R.ng = comp_n; R.caps = caps; R.ic = ic; R.ml = ml; R.un = un; R.ponto_tudo = pt;
    int ini = to_i(ARG(3));
    if (ini > s->u) { R = sv; return vNul(); }
    int meio, p = u_para_b(s, ini < 0 ? 0 : ini, &meio) * 2 + (meio && !un ? 1 : 0);
    for (;;) {
        for (int j = 0; j < (comp_n + 1) * 2; j++) caps[j] = -1;
        chk();
        int ok = 0;
        for (rn *h = comp; h && !ok; h = h->prox_alt) ok = rm(h->prox, p, NULL);
        if (ok) {
            caps[0] = p;
            obj *r = novo_arr((comp_n + 1) * 2);
            for (int j = 0; j < (comp_n + 1) * 2; j++) arr_push(r, vI(r_unid(s, caps[j])));
            /* nomes dos grupos (para match.groups) */
            obj *nm = novo_arr(comp_n + 1);
            for (int g = 0; g <= comp_n; g++) arr_push(nm, g && g < 64 && r_nomes[g] ? vS(r_nomes[g]) : vNul());
            por_prop(r, I("nomes"), vO(nm));
            R = sv;
            return vO(r);
        }
        if (sticky) break;
        int q;
        if (r_ler(p, &q) < 0) break;
        p = q;
    }
    R = sv;
    return vNul();
}

/* ---- DOM ---- */
typedef struct at { str *k, *v; struct at *prox; } at;
struct dn { int tipo; str *tag, *txt; at *at; dn *pai, *pri, *ult, *ant, *prox; obj *w; int feito; dn *sombra; int nk; unsigned nk_ger; };
static dn *raiz; static int sujo;
#define SUJAR() (sujo = 1, dom_ger++)
static unsigned ger_serial;   /* dom_ger da última serialização */
static int tique_ms, vivo;   /* página viva */
static str *s_style, *s_onclick, *s_ls, *s_click;
#define MAX_CLIQUES 512
static dn *cliques[MAX_CLIQUES]; static int qtd_cliques;
static dn *dcampos[MAX_CLIQUES]; static int qtd_campos;   /* input/textarea/select na ordem da serialização */
static int qtd_n; static unsigned ser_ger;   /* data-sagui-n: número de cada elemento na serialização (e de qual) */

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
    if (c->tipo == 11) { while (c->pri) d_por(p, c->pri, antes); return; }   /* fragmento: muda os filhos */
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
/* "cor: x; --v: y" -> propriedades do objeto style (camelCase; --custom como está) */
static void estilo_ler(obj *st, const char *t, int n)
{
    int i = 0;
    while (i < n) {
        while (i < n && (t[i] == ' ' || t[i] == ';' || t[i] == '\n' || t[i] == '\t')) i++;
        int k0 = i;
        while (i < n && t[i] != ':' && t[i] != ';') i++;
        if (i >= n || t[i] != ':') { while (i < n && t[i] != ';') i++; continue; }
        int k1 = i++;
        while (k1 > k0 && t[k1 - 1] == ' ') k1--;
        while (i < n && t[i] == ' ') i++;
        int v0 = i, prof = 0;
        while (i < n && (prof || t[i] != ';')) { if (t[i] == '(') prof++; else if (t[i] == ')') prof--; i++; }
        int v1 = i;
        while (v1 > v0 && (t[v1 - 1] == ' ' || t[v1 - 1] == '\n')) v1--;
        if (k1 <= k0) continue;
        char kb[96]; int kn = 0;
        bool custom = k1 - k0 > 2 && t[k0] == '-' && t[k0 + 1] == '-';
        for (int j = k0; j < k1 && kn < 95; j++) {
            char c = t[j];
            if (!custom && c == '-' && j + 1 < k1 && j > k0) { char u = t[++j]; kb[kn++] = (char)(u >= 'a' && u <= 'z' ? u - 32 : u); continue; }
            kb[kn++] = (char)(!custom && c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        uint8_t e = st->estilo; st->estilo = 0;
        por_prop(st, intern_n(kb, kn), vS(novo_str(t + v0, v1 - v0)));
        st->estilo = e;
    }
}
static void estilo_b(buf *b, obj *st)
{
    for (int i = 0; i < st->np; i++) {
        str *k = st->p[i].k;
        if (!k || st->p[i].v.t == U || st->p[i].v.t == NUL || st->p[i].v.t == OB || st->p[i].v.t == AC) continue;
        if (st->p[i].v.t == ST && !st->p[i].v.s->n) continue;   /* removida */
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
static dn *host_atual;   /* hospedeiro da sombra sendo achatada (serialização final) */
static void serial_filhos(buf *b, dn *d, int final) { for (dn *c = d->pri; c; c = c->prox) serial(b, c, final); }
/* <slot>: põe os filhos leves do hospedeiro que pertencem a ele; sem nenhum, o conteúdo reserva. */
static void serial_slot(buf *b, dn *slot, int final)
{
    dn *h = host_atual; str *nome = attr(slot, I("name")); int achou = 0;
    host_atual = NULL;
    for (dn *c = h->pri; c; c = c->prox) {
        str *sa = c->tipo == 1 ? attr(c, I("slot")) : NULL;
        int meu = nome && nome->n ? (sa && sa->n == nome->n && !memcmp(sa->s, nome->s, (size_t)nome->n)) : !(sa && sa->n);
        if (!meu) continue;
        if (c->tipo == 3 && !nome) { int so_branco = 1; for (int i = 0; i < c->txt->n; i++) if ((unsigned char)c->txt->s[i] > ' ') so_branco = 0; if (so_branco) continue; }
        achou = 1; serial(b, c, final);
    }
    host_atual = h;
    if (!achou) serial_filhos(b, slot, final);
}
/* CSS da sombra: :host(X) vira tag-do-hospedeiro + X, :host vira a tag. */
static void serial_css_sombra(buf *b, dn *st)
{
    str *tag = host_atual->tag;
    for (dn *c = st->pri; c; c = c->prox) {
        if (c->tipo != 3) continue;
        const char *s = c->txt->s; int n = c->txt->n;
        for (int i = 0; i < n; i++) {
            if (i + 5 <= n && !memcmp(s + i, ":host", 5) && !(i + 5 < n && s[i + 5] == '-')) {
                b_por(b, tag->s, tag->n); i += 4;
                if (i + 1 < n && s[i + 1] == '(') {
                    int prof = 0, j = i + 2;
                    for (; j < n; j++) { if (s[j] == '(') prof++; else if (s[j] == ')') { if (!prof) break; prof--; } }
                    b_por(b, s + i + 2, j - i - 2); i = j;
                }
                continue;
            }
            b_por(b, s + i, 1);
        }
    }
}
static void serial(buf *b, dn *d, int final)
{
    if (d->tipo == 3) { b_por(b, d->txt->s, d->txt->n); return; }
    if (d->tipo == 9 || d->tipo == 11) { serial_filhos(b, d, final); return; }
    if (final && host_atual && d->tag == I("slot")) { serial_slot(b, d, final); return; }
    if (final && d->tag->n == 6 && !memcmp(d->tag->s, "script", 6)) return;
    int id = -1, cid = -1;
    if (final && tem_clique(d) && qtd_cliques < MAX_CLIQUES) { id = qtd_cliques; cliques[qtd_cliques++] = d; }
    if (final && em_lista(d->tag, "input textarea select button") && qtd_campos < MAX_CLIQUES) { cid = qtd_campos; dcampos[qtd_campos++] = d; }
    int e_link = d->tag->n == 1 && d->tag->s[0] == 'a';
    int vazio = em_lista(d->tag, "area base br col embed hr img input link meta param source track wbr");
    char alvo[32];
    formatar_texto(alvo, sizeof alvo, "sagui-js:%d", id);
    b_t(b, "<"); b_por(b, d->tag->s, d->tag->n);
    prop *st = d->w ? achar(d->w, s_style) : NULL;
    for (at *a = d->at; a; a = a->prox) {
        if (id >= 0 && e_link && a->k == I("href")) continue;
        if (st && a->k == s_style) continue;
        b_attr(b, a->k, a->v->s, a->v->n);
    }
    if (st && st->v.t == OB) {   /* com el.style criado, ele é o atributo (já leu o que havia) */
        buf t = { 0, 0, 0 };
        estilo_b(&t, st->v.o);
        if (t.n) b_attr(b, s_style, t.b, t.n);
    }
    if (id >= 0 && e_link) b_attr(b, I("href"), alvo, (int)tam_texto(alvo));
    /* clique do JavaScript e campos: o próprio elemento leva a marca, sem mudar a árvore */
    char num[16];
    if (final) {   /* todo elemento: o motor devolve a caixa dele por este número */
        d->nk = qtd_n++; d->nk_ger = ser_ger;
        formatar_texto(num, sizeof num, "%d", d->nk); b_attr(b, I("data-sagui-n"), num, (int)tam_texto(num));
    }
    if (id >= 0 && !e_link) { formatar_texto(num, sizeof num, "%d", id); b_attr(b, I("data-sagui-js"), num, (int)tam_texto(num)); }
    if (cid >= 0) { formatar_texto(num, sizeof num, "%d", cid); b_attr(b, I("data-sagui-campo"), num, (int)tam_texto(num)); }
    b_t(b, ">");
    if (vazio) return;
    if (final && d->sombra) { dn *h = host_atual; host_atual = d; serial_filhos(b, d->sombra, final); host_atual = h; }
    else if (final && host_atual && d->tag == I("style")) serial_css_sombra(b, d);
    else serial_filhos(b, d, final);
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
static void por_texto(dn *d, str *t) { limpar(d); if (t->n) { dn *x = d_novo(3, NULL); x->txt = t; d_por(d, x, NULL); } SUJAR(); }
enum { D_NADA, D_TYPE, D_NAME, D_TAG, D_PAI, D_PAIEL, D_PRI, D_ULT, D_PROX, D_ANT, D_PRIEL, D_ULTEL, D_PROXEL, D_ANTEL, D_FILHOS, D_NOS, D_TEXTO, D_INNERT, D_INNER, D_OUTER,
       D_ID, D_CLASS, D_VALUE, D_HREF, D_SRC, D_DATA, D_NVALUE, D_TITLE, D_BODY, D_HEAD, D_DOCEL, D_STYLE, D_NCHILD, D_DOC, D_ALT, D_NAMEA, D_TYPEA, D_CHECKED, D_CWIN, D_CDOC, D_DEFVIEW };
static const char *const dnomes[] = { "", "nodeType", "nodeName", "tagName", "parentNode", "parentElement", "firstChild", "lastChild", "nextSibling", "previousSibling", "firstElementChild", "lastElementChild", "nextElementSibling", "previousElementSibling", "children", "childNodes", "textContent", "innerText", "innerHTML", "outerHTML",
    "id", "className", "value", "href", "src", "data", "nodeValue", "title", "body", "head", "documentElement", "style", "childElementCount", "ownerDocument", "alt", "name", "type", "checked", "contentWindow", "contentDocument", "defaultView" };
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
        if (d->tipo != 1) { *r = vS(I(d->tipo == 3 ? "#text" : d->tipo == 11 ? "#document-fragment" : "#document")); break; }
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
    /* iframes compartilham a janela e o documento da página */
    case D_CWIN: case D_DEFVIEW: if (k->dom == D_DEFVIEW && d->tipo != 9) return 0; *r = vO(global); break;
    case D_CDOC: *r = embrulhar(raiz); break;
    case D_STYLE: {
        val c = get(vO(global), I("CSSStyleDeclaration")), pr = c.t == OB ? get(c, s_prototype) : vU();
        val s = vO(novo_obj(pr.t == OB ? pr.o : p_obj));
        str *at0 = attr(d, s_style);
        if (at0) estilo_ler(s.o, at0->s, at0->n);
        s.o->estilo = 1; por_prop(o, s_style, s); *r = s; break;
    }
    default: return 0;
    }
    return 1;
}
static int el_set(obj *o, str *k, val v)
{
    dn *d = o->d;
    if (!k->dom) return 0;
    switch (k->dom) {
    case D_TEXTO: case D_INNERT: if (d->tipo == 3) { d->txt = escapar(to_str(v)); SUJAR(); } else por_texto(d, escapar(to_str(v))); return 1;
    case D_DATA: case D_NVALUE: if (d->tipo == 3) { d->txt = escapar(to_str(v)); SUJAR(); } return 1;
    case D_INNER: { str *s = to_str(v); limpar(d); html_ler(s->s, s->n, d); SUJAR(); return 1; }
    case D_ID: attr_por(d, I("id"), to_str(v)); SUJAR(); return 1;
    case D_CLASS: attr_por(d, I("class"), to_str(v)); SUJAR(); return 1;
    case D_VALUE: case D_HREF: case D_SRC: case D_ALT: case D_NAMEA: case D_TYPEA:
        if (k->dom == D_SRC && d->tipo == 1 && (d->tag == I("video") || d->tag == I("audio") || d->tag == I("source"))) {
            str *u = to_str(v);
            registrar("js: video src (%d)\n", u->n);
            video_src = u;
        }
        attr_por(d, I(dnomes[k->dom]), to_str(v)); SUJAR(); return 1;
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
static val n_create_el(val este, val *a, int n) { (void)este; str *t = to_str(ARG(0)); if (t->n == 5 && (t->s[0] | 32) == 'v' && (t->s[1] | 32) == 'i' && (t->s[2] | 32) == 'd') registrar("js: criou <video>\n"); return embrulhar(d_novo(1, baixo_n(t->s, t->n))); }
static val n_create_txt(val este, val *a, int n) { (void)este; dn *t = d_novo(3, NULL); t->txt = escapar(to_str(ARG(0))); return embrulhar(t); }
static val n_sombra(val este, val *a, int n) { (void)este; dn *d = de_val(ARG(0)); if (!d) return vU(); if (!d->sombra) { d->sombra = d_novo(11, NULL); SUJAR(); } return embrulhar(d->sombra); }
static val n_frag(val este, val *a, int n) { (void)este; (void)a; (void)n; return embrulhar(d_novo(11, NULL)); }
static val n_append(val este, val *a, int n) { d_por(de_val(este), de_val(ARG(0)), NULL); SUJAR(); return a[0]; }
static val n_insert(val este, val *a, int n) { d_por(de_val(este), de_val(ARG(0)), ARG(1).t == OB ? de_val(a[1]) : NULL); SUJAR(); return a[0]; }
static val n_remove_ch(val este, val *a, int n) { (void)este; d_tirar(de_val(ARG(0))); SUJAR(); return a[0]; }
static val n_getattr(val este, val *a, int n) { str *t = to_str(ARG(0)), *v = attr(de_val(este), baixo_n(t->s, t->n)); return v ? vS(v) : vNul(); }
static val n_setattr(val este, val *a, int n) { str *t = to_str(ARG(0)); dn *d = de_val(este); if (t->n == 3 && !memcmp(t->s, "src", 3) && (d->tag == I("video") || d->tag == I("source"))) { registrar("js: video src attr\n"); video_src = to_str(ARG(1)); } str *k = baixo_n(t->s, t->n); attr_por(d, k, to_str(ARG(1)));
    if (k == s_style && d->w) { prop *st = achar(d->w, s_style); if (st && st->v.t == OB) { st->v.o->np = 0; if (st->v.o->idx) memset(st->v.o->idx, 0, (size_t)st->v.o->cidx * sizeof(int)); str *v = to_str(ARG(1)); estilo_ler(st->v.o, v->s, v->n); } }
    SUJAR(); return vU(); }
static val n_rmattr(val este, val *a, int n)
{
    dn *d = de_val(este); str *t = to_str(ARG(0)), *k = baixo_n(t->s, t->n);
    for (at **p = &d->at; *p; p = &(*p)->prox) if ((*p)->k == k) { *p = (*p)->prox; SUJAR(); break; }
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

/* ---- Canvas 2D: um despachante só (__c2(op, ...)); o estado fica em canvas2d.c */
int  tela2d_nova(int l, int a); void tela2d_tamanho(int id, int l, int a);
void tela2d_salvar(int id); void tela2d_restaurar(int id);
void tela2d_matriz(int id, const double m[6], int definir); void tela2d_ler_matriz(int id, double m[6]);
void tela2d_alfa(int id, double a); void tela2d_composicao(int id, int op);
void tela2d_cor(int id, int traco, uint32_t argb); void tela2d_usar_grad(int id, int traco, int g);
void tela2d_linha(int id, double lw, int cap, int join, double miter); void tela2d_suave(int id, int s);
void tela2d_tracejado(int id, const double *p, int n, double desl);
int  tela2d_grad_novo(int radial, const double p[6]); void tela2d_grad_parada(int g, double pos, uint32_t argb);
void tela2d_caminho_novo(int id); void tela2d_mover(int id, double x, double y); void tela2d_linha_para(int id, double x, double y);
void tela2d_quad(int id, double cx, double cy, double x, double y);
void tela2d_bezier(int id, double a, double b, double c, double d, double x, double y);
void tela2d_arco(int id, double x, double y, double r, double a0, double a1, int anti);
void tela2d_arco_para(int id, double x1, double y1, double x2, double y2, double r);
void tela2d_elipse(int id, double x, double y, double rx, double ry, double rot, double a0, double a1, int anti);
void tela2d_ret(int id, double x, double y, double w, double h); void tela2d_fechar(int id);
void tela2d_preencher(int id, int parimpar); void tela2d_tracar(int id); void tela2d_recortar(int id, int parimpar);
void tela2d_ret_cheio(int id, double x, double y, double w, double h); void tela2d_ret_limpar(int id, double x, double y, double w, double h);
void tela2d_ret_traco(int id, double x, double y, double w, double h);
void tela2d_texto(int id, const char *s, double x, double y, double tam, int alinha, int base, int traco, double maxw);
double tela2d_medir(const char *s, double tam);
void tela2d_desenhar(int id, int src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh);
int  tela2d_de_pixels(const uint32_t *px, int l, int a, int alfa);
void tela2d_ler(int id, int x, int y, int w, int h, uint8_t *rgba);
void tela2d_escrever(int id, const uint8_t *rgba, int w, int h, int dx, int dy, int rx, int ry, int rw, int rh);
uint32_t tela2d_cor_css(const char *s, int *ok);
bool imagem_decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a);


/* ---- Medidas do layout (getBoundingClientRect & cia.) ----
 * Como num navegador, uma medida pedida pelo script força o layout do DOM
 * atual: serializa, o Cipó diagrama (gancho layout) e as caixas ficam em
 * cache até o DOM ou algum el.style mudar. */
static int *lay; static int lay_cap, lay_n, lay_ok; static unsigned lay_ger = 0xFFFFFFFFu, lay_ser;
static int jan_l = 1024, jan_a = 768, jan_rx, jan_ry;
void js_janela(int l, int a, int rx, int ry) { jan_l = l; jan_a = a; jan_rx = rx; jan_ry = ry; }
static int layout_atual(void)
{
    if (lay_ger == dom_ger) return lay_ok;
    lay_ger = dom_ger; lay_ok = 0;
    if (!gancho || !gancho->layout || !raiz) return 0;
    buf b = { 0, 0, 0 };
    qtd_cliques = 0; qtd_campos = 0; qtd_n = 0; ser_ger++;
    serial(&b, raiz, 1);
    if (qtd_n > lay_cap) {
        int c = qtd_n + 256;
        int *nl = aloca((unsigned)c * 16 * sizeof(int));
        if (!nl) return 0;
        if (lay) libera(lay);
        lay = nl; lay_cap = c;
    }
    lay_n = qtd_n; lay_ser = ser_ger;
    char *h = aloca((unsigned)b.n + 1);
    if (!h) return 0;
    memcpy(h, b.b, (size_t)b.n); h[b.n] = 0;
    lay_ok = gancho->layout(h, b.n, lay, lay_n);
    libera(h);
    return lay_ok;
}
static const int *caixa_de(dn *d)
{
    if (!d || d->tipo != 1 || !layout_atual()) return NULL;
    if (d->nk_ger != lay_ser || d->nk < 0 || d->nk >= lay_n) return NULL;   /* fora do documento */
    return lay + d->nk * 16;
}
static val n_caixa(val este, val *a, int n)
{
    (void)este;
    const int *c = caixa_de(de_val(ARG(0)));
    if (!c) return vNul();
    obj *r = novo_arr(0);
    for (int i = 0; i < 15; i++) arr_push(r, vI(c[i]));
    return vO(r);
}
static val n_estilo_c(val este, val *a, int n)
{
    (void)este;
    dn *d = de_val(ARG(0));
    if (!caixa_de(d) || !gancho->estilo) return vNul();
    char o[256];
    return gancho->estilo(d->nk, to_str(ARG(1))->s, o, sizeof o) ? vT(o) : vNul();
}
static int rol_pedido, rol_px_ped, rol_py_ped;
static val n_rolar_c(val este, val *a, int n) { (void)este; rol_pedido = 1; rol_px_ped = to_i(ARG(0)); rol_py_ped = to_i(ARG(1)); return vU(); }
int js_rolagem_pedida(int *x, int *y) { if (!rol_pedido) return 0; rol_pedido = 0; *x = rol_px_ped; *y = rol_py_ped; return 1; }
static val n_jan(val este, val *a, int n) { (void)este; (void)a; (void)n; obj *r = novo_arr(0); arr_push(r, vI(jan_l)); arr_push(r, vI(jan_a)); arr_push(r, vI(jan_rx)); arr_push(r, vI(jan_ry)); return vO(r); }
/* el.dataset: DOMStringMap montado dos atributos data-* (camelCase); escrever reflete no atributo */
static void ds_escreve(obj *o, str *k, val v)
{
    dn *d = o->d;
    if (!d) return;
    char b[160]; int n = 0;
    memcpy(b, "data-", 5); n = 5;
    for (int i = 0; i < k->n && n < 150; i++) {
        char c = k->s[i];
        if (c >= 'A' && c <= 'Z') { b[n++] = '-'; b[n++] = (char)(c + 32); } else b[n++] = c;
    }
    attr_por(d, intern_n(b, n), to_str(v));
    SUJAR();
}
static val n_dataset(val este, val *a, int n)
{
    (void)este;
    dn *d = de_val(ARG(0));
    obj *o = novo_obj(p_obj);
    for (at *x = d->at; x; x = x->prox) {
        if (x->k->n <= 5 || memcmp(x->k->s, "data-", 5)) continue;
        char b[160]; int k = 0;
        for (int i = 5; i < x->k->n && k < 159; i++) {
            char c = x->k->s[i];
            if (c == '-' && i + 1 < x->k->n && x->k->s[i + 1] >= 'a' && x->k->s[i + 1] <= 'z') { b[k++] = (char)(x->k->s[++i] - 32); continue; }
            b[k++] = c;
        }
        por_prop(o, intern_n(b, k), vS(x->v));
    }
    o->d = d; o->estilo = 2;
    return vO(o);
}
static val n_script_atual(val este, val *a, int n) { (void)este; (void)a; (void)n; return script_atual ? embrulhar(script_atual) : vNul(); }
static double NA(val *a, int n, int i) { return i < n ? to_num(a[i]) : 0; }
static val n_c2(val este, val *a, int n)
{
    (void)este;
    int op = to_i(ARG(0)), id = to_i(ARG(1));
    switch (op) {
    case 0: return vI(tela2d_nova(to_i(ARG(1)), to_i(ARG(2))));
    case 1: tela2d_tamanho(id, to_i(ARG(2)), to_i(ARG(3))); break;
    case 2: tela2d_salvar(id); break;
    case 3: tela2d_restaurar(id); break;
    case 4: { double m[6]; for (int i = 0; i < 6; i++) m[i] = NA(a, n, 2 + i); tela2d_matriz(id, m, to_i(ARG(8))); break; }
    case 5: { double m[6]; tela2d_ler_matriz(id, m); obj *r = novo_arr(0); for (int i = 0; i < 6; i++) arr_push(r, vN(m[i])); return vO(r); }
    case 6: tela2d_alfa(id, NA(a, n, 2)); break;
    case 7: tela2d_composicao(id, to_i(ARG(2))); break;
    case 8: { int ok; uint32_t c = tela2d_cor_css(to_str(ARG(3))->s, &ok); if (ok) tela2d_cor(id, to_i(ARG(2)), c); return vB(ok); }
    case 9: tela2d_usar_grad(id, to_i(ARG(2)), to_i(ARG(3))); break;
    case 10: tela2d_linha(id, NA(a, n, 2), to_i(ARG(3)), to_i(ARG(4)), NA(a, n, 5)); break;
    case 11: tela2d_suave(id, to_i(ARG(2))); break;
    case 14: {   /* setLineDash(lista) + lineDashOffset */
        val l = ARG(2); double p[16]; int k = 0;
        if (l.t == OB) for (int i = 0; i < l.o->na && k < 16; i++) p[k++] = to_num(l.o->a[i]);
        tela2d_tracejado(id, p, k, NA(a, n, 3));
        break;
    }
    case 12: { double p[6]; for (int i = 0; i < 6; i++) p[i] = NA(a, n, 2 + i); return vI(tela2d_grad_novo(to_i(ARG(1)), p)); }
    case 13: { int ok; uint32_t c = tela2d_cor_css(to_str(ARG(3))->s, &ok); if (ok) tela2d_grad_parada(id, NA(a, n, 2), c); return vB(ok); }
    case 20: tela2d_caminho_novo(id); break;
    case 21: tela2d_mover(id, NA(a, n, 2), NA(a, n, 3)); break;
    case 22: tela2d_linha_para(id, NA(a, n, 2), NA(a, n, 3)); break;
    case 23: tela2d_quad(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5)); break;
    case 24: tela2d_bezier(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5), NA(a, n, 6), NA(a, n, 7)); break;
    case 25: tela2d_arco(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5), NA(a, n, 6), to_bool(ARG(7))); break;
    case 26: tela2d_arco_para(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5), NA(a, n, 6)); break;
    case 27: tela2d_elipse(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5), NA(a, n, 6), NA(a, n, 7), NA(a, n, 8), to_bool(ARG(9))); break;
    case 28: tela2d_ret(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5)); break;
    case 29: tela2d_fechar(id); break;
    case 30: tela2d_preencher(id, to_i(ARG(2))); break;
    case 31: tela2d_tracar(id); break;
    case 32: tela2d_recortar(id, to_i(ARG(2))); break;
    case 33: tela2d_ret_cheio(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5)); break;
    case 34: tela2d_ret_limpar(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5)); break;
    case 35: tela2d_ret_traco(id, NA(a, n, 2), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5)); break;
    case 40: tela2d_texto(id, to_str(ARG(2))->s, NA(a, n, 3), NA(a, n, 4), NA(a, n, 5), to_i(ARG(6)), to_i(ARG(7)), to_i(ARG(8)), NA(a, n, 9)); break;
    case 41: return vN(tela2d_medir(to_str(ARG(1))->s, NA(a, n, 2)));
    case 42: tela2d_desenhar(id, to_i(ARG(2)), NA(a, n, 3), NA(a, n, 4), NA(a, n, 5), NA(a, n, 6), NA(a, n, 7), NA(a, n, 8), NA(a, n, 9), NA(a, n, 10)); break;
    case 43: {   /* getImageData -> Uint8ClampedArray */
        int x = to_i(ARG(2)), y = to_i(ARG(3)), w = to_i(ARG(4)), h = to_i(ARG(5));
        if (w <= 0 || h <= 0 || (long)w * h > 4096L * 4096) erro("RangeError", "getImageData: tamanho inválido", NULL);
        obj *o = bytes_ta(NULL, 0, ARG(6).t == OB ? ARG(6).o : NULL);
        o->vtipo = 2; o->vbuf = buf_novo(w * h * 4, vU()); o->vlen = w * h * 4;
        tela2d_ler(id, x, y, w, h, o->vbuf->mem);
        return vO(o);
    }
    case 44: {   /* putImageData(dados, w, h, dx, dy, rx, ry, rw, rh) */
        val d = ARG(2);
        if (d.t != OB || d.o->cl != C_TA) break;
        int w = to_i(ARG(3)), h = to_i(ARG(4));
        if ((long)w * h * 4 > (long)d.o->vlen * ta_tam[d.o->vtipo]) break;
        tela2d_escrever(id, d.o->vbuf->mem + d.o->voff, w, h, to_i(ARG(5)), to_i(ARG(6)), to_i(ARG(7)), to_i(ARG(8)), to_i(ARG(9)), to_i(ARG(10)));
        break;
    }
    case 45: {   /* imagem de um endereço -> tela (ou -1) */
        if (!gancho || !gancho->baixar) return vI(-1);
        unsigned k = 0;
        uint8_t *d = gancho->baixar(to_str(ARG(1))->s, &k);
        if (!d) return vI(-1);
        uint32_t *px = NULL; int l = 0, al = 0;
        bool ok = imagem_decodificar(d, k, &px, &l, &al);
        libera(d);
        if (!ok) return vI(-1);
        int alfa = 0;
        for (int i = 0; i < l * al && !alfa; i++) if (px[i] >> 24) alfa = 1;
        int t = tela2d_de_pixels(px, l, al, alfa);
        libera(px);
        obj *r = novo_arr(0); arr_push(r, vI(t)); arr_push(r, vI(l)); arr_push(r, vI(al));
        return vO(r);
    }
    case 46: {   /* cor CSS normalizada como o canvas devolve no getter */
        int ok; uint32_t c = tela2d_cor_css(to_str(ARG(1))->s, &ok);
        if (!ok) return vNul();
        char b[48];
        if ((c >> 24) == 255) formatar_texto(b, sizeof b, "#%02x%02x%02x", (c >> 16) & 255, (c >> 8) & 255, c & 255);
        else {
            int al = (int)(c >> 24), cent = (al * 1000 + 127) / 255;   /* alfa com até 3 casas */
            char fa[12];
            if (al == 0) copia_texto(fa, "0", sizeof fa);
            else formatar_texto(fa, sizeof fa, "0.%03d", cent >= 1000 ? 999 : cent);
            int k = (int)tam_texto(fa); while (k > 2 && fa[k - 1] == '0') fa[--k] = 0;
            formatar_texto(b, sizeof b, "rgba(%d, %d, %d, %s)", (c >> 16) & 255, (c >> 8) & 255, c & 255, fa);
        }
        return vT(b);
    }
    }
    return vU();
}
static void iniciar(const js_hooks_t *h, const char *url)
{
    gancho = h;
    ds_escrever = ds_escreve;
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
    reg(S, "fromCharCode", n_fromcode); reg(S, "fromCodePoint", n_fromcodepoint); reg(p_str, "codePointAt", n_codepoint);
    reg(p_str, "charAt", n_charat); reg(p_str, "charCodeAt", n_charcode); reg(p_str, "indexOf", n_sindex); reg(p_str, "lastIndexOf", n_slast); reg(p_str, "slice", n_sslice);
    reg(p_str, "substring", n_substring); reg(p_str, "substr", n_substr); reg(p_str, "toLowerCase", n_lower); reg(p_str, "toUpperCase", n_upper); reg(p_str, "trim", n_trim);
    reg(p_str, "__split", n_ssplit); reg(p_str, "toString", n_string); reg(p_str, "valueOf", n_mesmo);
    reg(p_num, "toString", n_numstr); reg(p_num, "toFixed", n_tofixed); reg(p_num, "valueOf", n_mesmo);
    reg(p_bool, "toString", n_string); reg(p_bool, "valueOf", n_mesmo);
    reg(p_el, "querySelectorAll", n_qsa); reg(p_el, "querySelector", n_qs); reg(p_el, "matches", n_matches); reg(p_el, "getElementById", n_byid);
    reg(p_el, "createElement", n_create_el); reg(p_el, "createTextNode", n_create_txt); reg(p_el, "appendChild", n_append); reg(p_el, "insertBefore", n_insert); reg(p_el, "removeChild", n_remove_ch);
    reg(p_el, "getAttribute", n_getattr); reg(p_el, "setAttribute", n_setattr); reg(p_el, "removeAttribute", n_rmattr); reg(p_el, "write", n_write); reg(p_el, "writeln", n_write);
    reg(global, "__chaves", n_chaves); reg(global, "parseInt", n_parseint); reg(global, "parseFloat", n_parsefloat); reg(global, "isNaN", n_isnan);
    reg(global, "__gnext", n_gnext); reg(global, "__novo_buf", n_novo_buf); reg(global, "__buf_slice", n_buf_slice); reg(global, "__novo_ta", n_novo_ta); reg(global, "__ta_info", n_ta_info); reg(global, "__ta_copiar", n_ta_copiar); reg(global, "__utf8_cod", n_utf8_cod); reg(global, "__utf8_dec", n_utf8_dec); reg(global, "__c2", n_c2); reg(global, "__copiar", n_copiar); reg(global, "__script_atual", n_script_atual); reg(global, "__dataset", n_dataset); reg(global, "__caixa", n_caixa); reg(global, "__estilo_c", n_estilo_c); reg(global, "__jan", n_jan); reg(global, "__rolar_c", n_rolar_c); reg(global, "__escopo", n_escopo); reg(global, "__limite", n_limite); reg(global, "__tentar", n_tentar);
    reg(global, "__log", n_log); reg(global, "__eval", n_eval); reg(global, "eval", n_eval); reg(global, "__now", n_now); reg(global, "__rand", n_rand); reg(global, "alert", n_alert);
    reg(global, "__baixar", n_baixar);  reg(global, "__json_parse", n_json_parse); reg(global, "__json_q", n_json_q); reg(global, "__pedir_n", n_pedir); reg(global, "__sombra", n_sombra); reg(global, "__frag", n_frag); reg(global, "__re", n_re);
    por_prop(global, I("NaN"), vN(D_NAN)); por_prop(global, I("Infinity"), vN(D_INF)); reg(global, "__mat", n_mat); reg(global, "__numfmt", n_numfmt); por_prop(global, I("undefined"), vU());
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
        SUJAR();
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
    qtd_cliques = 0; qtd_campos = 0; qtd_n = 0;
    ger_serial = dom_ger; ser_ger++;
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
static void antes_serial(void) { rodar_global("__antes_serial"); }
static void pagina_interna(void)
{
    if (proteger(pagina_scripts) == 0 && !raiz) return;
    registrar("cipo: js %d scripts, %d parados%s\n", feitos, parados, fatal ? ", cortado" : "");
    fatal = 0;
    if (raiz) proteger(antes_serial);
    /* Sempre serializa: mesmo sem mudar o DOM, os scripts podem ter posto
     * cliques (data-sagui-js), e os campos levam data-sagui-campo. */
    if (raiz) { ar_total = 0; proteger(serial_final); }
}
static int clique_cancelado;
int js_clique_cancelado(void) { return clique_cancelado; }
static void clique_interno(void)
{
    clique_cancelado = 0;
    if (clique_id < 0 || clique_id >= qtd_cliques) return;
    val alvo = embrulhar(cliques[clique_id]);
    t_limite = tempo_ms() + 3000;
    sujo = 0;
    val f = get(vO(global), I("__clicar"));
    salto b; (void)b;
    clique_cancelado = to_bool(chamar(f, vU(), &alvo, 1));
}
static void clique_seguro(void)
{
    proteger(clique_interno);
    proteger(antes_serial);
    fatal = 0;
    ar_total = 0;
    proteger(serial_final);
}
/* Digitação num campo: o valor muda sem sujar o DOM; só os eventos input
 * (o que os scripts fizerem com eles) podem pedir uma página nova. */
static int campo_id; static const char *campo_v; static int campo_tecla;
static void campo_interno(void)
{
    if (campo_id < 0 || campo_id >= qtd_campos) return;
    dn *d = dcampos[campo_id];
    attr_por(d, I("value"), novo_str(campo_v, (int)tam_texto(campo_v)));
    if (d->tag == I("textarea")) { limpar(d); if (campo_v[0]) { dn *x = d_novo(3, NULL); x->txt = escapar(novo_str(campo_v, (int)tam_texto(campo_v))); d_por(d, x, NULL); } }
    sujo = 0;
    t_limite = tempo_ms() + 3000;
    val a[2] = { embrulhar(d), vI(campo_tecla) };
    chamar(get(vO(global), I("__digitar")), vU(), a, 2);
}
static void campo_seguro(void)
{
    proteger(campo_interno);
    proteger(antes_serial);
    fatal = 0;
    if (sujo && raiz) { ar_total = 0; proteger(serial_final); }
}
static int envio_cancelado;
static void envio_interno(void)
{
    envio_cancelado = 0;
    if (campo_id < 0 || campo_id >= qtd_campos) return;
    sujo = 0;
    t_limite = tempo_ms() + 3000;
    val a = embrulhar(dcampos[campo_id]);
    envio_cancelado = to_bool(chamar(get(vO(global), I("__enviar")), vU(), &a, 1));
}
static void envio_seguro(void)
{
    proteger(envio_interno);
    proteger(antes_serial);
    fatal = 0;
    if (sujo && raiz) { ar_total = 0; proteger(serial_final); }
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
    tela2d_liberar_todas();
    lay_ger = 0xFFFFFFFFu; lay_ok = 0; lay_n = 0;
    video_src = NULL;
    while (blocos) { bloco *b = blocos; blocos = b->prox; libera(b); }
    ar = NULL; ar_u = ar_c = ar_total = 0;
    tab = NULL; tab_c = tab_n = 0; tb = NULL; tb_c = 0;
    raiz = NULL; global = NULL; vivo = 1; qtd_cliques = 0; topo_s = NULL; fatal = 0;
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
/* Página viva: roda os timers que venceram em ms e devolve o HTML se o DOM mudou. */
static void tique_interno(void)
{
    t_limite = tempo_ms() + 3000;   /* um quadro pesado (canvas com milhares de pontos) não é laço infinito */
    val a = vI(tique_ms);
    vivo = to_bool(chamar(get(vO(global), I("__tique")), vU(), &a, 1));
}
static void tique_seguro(void)
{
    sujo = 0;
    if (!proteger(tique_interno)) vivo = 0;
    proteger(antes_serial);
    fatal = 0;
    if ((sujo || dom_ger != ger_serial) && raiz) { ar_total = 0; proteger(serial_final); }
}
char *js_tique(int ms, int *saida_tam)
{
    *saida_tam = 0;
    if (!global || !raiz) return NULL;
    tique_ms = ms; saida = NULL; saida_n = 0;
    na_pilha(tique_seguro);
    *saida_tam = saida_n;
    return saida;
}
int js_vivo(void) { return global && vivo; }
int js_ativo(void) { return global != NULL; }
/* rolagem e tamanho: o script recebe scroll/resize e os observadores olham de novo */
static int evento_jan;
static void janela_interno(void)
{
    t_limite = tempo_ms() + 500;
    val a = vI(evento_jan);
    chamar(get(vO(global), I("__janela_mudou")), vU(), &a, 1);
}
static void janela_seguro(void)
{
    sujo = 0;
    proteger(janela_interno);
    proteger(antes_serial);
    fatal = 0;
    if ((sujo || dom_ger != ger_serial) && raiz) { ar_total = 0; proteger(serial_final); }
}
char *js_rolar(int rx, int ry, int *saida_tam)
{
    *saida_tam = 0;
    if (!global || !raiz) return NULL;
    jan_rx = rx; jan_ry = ry; evento_jan = 0; saida = NULL; saida_n = 0;
    na_pilha(janela_seguro);
    *saida_tam = saida_n;
    return saida;
}
char *js_redimensionar(int l, int a, int *saida_tam)
{
    *saida_tam = 0;
    jan_l = l; jan_a = a;
    if (!global || !raiz) return NULL;
    evento_jan = 1; saida = NULL; saida_n = 0;
    na_pilha(janela_seguro);
    *saida_tam = saida_n;
    return saida;
}
int js_enviar(int id, char **html, int *saida_tam)
{
    *html = NULL; *saida_tam = 0;
    if (!global) return 0;
    campo_id = id; saida = NULL; saida_n = 0;
    na_pilha(envio_seguro);
    *html = saida; *saida_tam = saida_n;
    return envio_cancelado;
}
char *js_campo(int id, const char *valor, int tecla, int *saida_tam)
{
    *saida_tam = 0;
    if (!global) return NULL;
    campo_id = id; campo_v = valor; campo_tecla = tecla; saida = NULL; saida_n = 0;
    na_pilha(campo_seguro);
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

/* src que o JavaScript deu a um <video> (o player do YouTube monta a URL do formato 18); NULL se nenhum. Limpa ao ler. */
const char *js_video(void)
{
    static char u[4096];
    if (!video_src) return NULL;
    copia_texto(u, video_src->s, sizeof u);
    video_src = NULL;
    return u;
}
