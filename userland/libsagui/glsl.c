/* glsl.c - compilador de GLSL ES 1.00 (WebGL 1) para uma máquina virtual de 4 vias.
 *
 * Etapas: pré-processador (#define com e sem argumentos, #if/#ifdef/#elif/#else
 * com expressões e defined(), #undef, #extension, #version, #pragma, #line),
 * léxico, parser (declarações, structs, arrays, funções, todas as expressões e
 * comandos da linguagem), checagem de tipos com sobrecarga, e geração de
 * código. Funções do usuário são expandidas no lugar (GLSL ES não tem
 * recursão), então a VM não precisa de pilha de chamadas.
 *
 * A VM roda 4 "vias" ao mesmo tempo: no fragmento, um quadrado de 2x2 pixels
 * (dFdx/dFdy/fwidth são diferenças entre vias, como no hardware); no vértice,
 * 4 vértices. Cada valor é um vetor de até 16 floats por via. Controle de fluxo
 * divergente usa máscaras (if/else, laços com break/continue, return, discard).
 * Inteiros e booleanos são guardados como float (exatos até 2^24, o que o
 * GLSL ES 1.00 exige). */
#include <sagui.h>
#include "glsl.h"

static float g_sqrt(float x) { float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
static double x87_1(int op, double x) { double r; switch (op) { case 0: __asm__("fsin" : "=t"(r) : "0"(x)); break; case 1: __asm__("fcos" : "=t"(r) : "0"(x)); break; default: __asm__("fsqrt" : "=t"(r) : "0"(x)); } return r; }
static double x87_atan2(double y, double x) { double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static double x87_log2(double x) { double r; __asm__("fld1; fxch; fyl2x" : "=t"(r) : "0"(x)); return r; }
static double x87_exp2(double x)
{
    /* 2^x = 2^int * 2^frac (f2xm1 vale em [-1, 1]) */
    double i, r;
    __asm__("frndint" : "=t"(i) : "0"(x));
    double f = x - i;
    __asm__("f2xm1" : "=t"(r) : "0"(f));
    r += 1;
    __asm__("fscale" : "=t"(r) : "0"(r), "u"(i));
    return r;
}
static float g_floor(float x) { float i = (float)(int)x; if (x < 0 && i != x) i -= 1; return (x > 8388608.0f || x < -8388608.0f) ? x : i; }

/* ================================================================ memória */
typedef struct pd { struct pd *prox; int usado, cap; } pd;
typedef struct { pd *p; } arena;
static void *anovo(arena *a, int n)
{
    n = (n + 7) & ~7;
    if (!a->p || a->p->usado + n > a->p->cap) {
        int cap = n > 60000 ? n : 65536;
        pd *b = aloca(sizeof(pd) + (unsigned)cap);
        if (!b) return NULL;
        b->prox = a->p; b->usado = 0; b->cap = cap; a->p = b;
    }
    void *r = (char *)(a->p + 1) + a->p->usado;
    a->p->usado += n;
    memset(r, 0, (size_t)n);
    return r;
}
static void alivre(arena *a) { while (a->p) { pd *b = a->p; a->p = b->prox; libera(b); } }

/* ================================================================ tipos */
enum { T_VOID, T_FLOAT, T_INT, T_BOOL, T_SAMPLER2D, T_SAMPLERCUBE, T_STRUCT };
typedef struct tipo {
    uint8_t base;        /* T_FLOAT/T_INT/T_BOOL/samplers/struct */
    uint8_t lin, col;    /* vetor: lin = n, col = 1; matriz: lin = col = n; escalar 1x1 */
    int arr;             /* 0 = não é array; senão o tamanho */
    struct st *s;        /* struct */
} tipo;
typedef struct campo { char nome[64]; tipo t; int desl; } campo;
typedef struct st { char nome[64]; campo c[32]; int nc, tam; } st;

static int tam_tipo(const tipo *t);
static int tam_elem(const tipo *t)
{
    if (t->base == T_STRUCT) return t->s->tam;
    if (t->base == T_SAMPLER2D || t->base == T_SAMPLERCUBE) return 1;
    return t->lin * t->col;
}
static int tam_tipo(const tipo *t) { return tam_elem(t) * (t->arr ? t->arr : 1); }
static int tipo_igual(const tipo *a, const tipo *b) { return a->base == b->base && a->lin == b->lin && a->col == b->col && a->arr == b->arr && a->s == b->s; }

/* ================================================================ erros */
typedef struct comp comp;
struct comp {
    arena ar;
    const char *fonte; int nfonte;
    char *pp; int npp;                 /* texto depois do pré-processador */
    int tipo_shader;                   /* 0 vértice, 1 fragmento */
    char erro[256]; int falhou;
    /* léxico */
    const char *p; int linha;
    int tk; char lex[64]; double num; int num_int;
    /* símbolos */
    struct sim *escopo[64]; int nescopo;
    st *structs[32]; int nstructs;
    struct fn *fns;
    /* saída */
    glsl_prog *prog;
};
static void falha(comp *c, const char *m, const char *x)
{
    if (c->falhou) return;
    c->falhou = 1;
    formatar_texto(c->erro, sizeof c->erro, "ERROR: 0:%d: %s%s%s", c->linha, m, x ? " " : "", x ? x : "");
}

/* ================================================================ pré-processador */
typedef struct macro { char nome[64]; char *corpo; int nargs; char args[16][64]; struct macro *prox; int funcao; } macro;
static int eh_id0(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int eh_id(char c) { return eh_id0(c) || (c >= '0' && c <= '9'); }
static macro *achar_macro(macro *m, const char *s, int n) { for (; m; m = m->prox) if ((int)tam_texto(m->nome) == n && !compara_texto_n(m->nome, s, (size_t)n)) return m; return NULL; }

typedef struct { char *b; int n, c; arena *a; } tbuf;
static void tb_por(tbuf *t, const char *s, int n)
{
    if (t->n + n + 1 > t->c) {
        int c = (t->c ? t->c * 2 : 4096) + n;
        char *d = anovo(t->a, c);
        if (!d) return;
        if (t->n) memcpy(d, t->b, (size_t)t->n);
        t->b = d; t->c = c;
    }
    memcpy(t->b + t->n, s, (size_t)n); t->n += n; t->b[t->n] = 0;
}

/* expande macros numa linha (recursivo até 32 níveis) */
static void expandir(comp *c, macro *ms, const char *s, int n, tbuf *o, int prof)
{
    int i = 0;
    while (i < n) {
        if (eh_id0(s[i])) {
            int j = i; while (j < n && eh_id(s[j])) j++;
            macro *m = prof < 32 ? achar_macro(ms, s + i, j - i) : NULL;
            if (!m) { tb_por(o, s + i, j - i); i = j; continue; }
            if (!m->funcao) { expandir(c, ms, m->corpo, (int)tam_texto(m->corpo), o, prof + 1); i = j; continue; }
            int k = j; while (k < n && (s[k] == ' ' || s[k] == '\t')) k++;
            if (k >= n || s[k] != '(') { tb_por(o, s + i, j - i); i = j; continue; }
            /* argumentos */
            const char *av[8]; int an[8], na = 0, prof2 = 0, ini = k + 1;
            k++;
            while (k < n) {
                if (s[k] == '(') prof2++;
                else if (s[k] == ')') { if (!prof2) break; prof2--; }
                else if (s[k] == ',' && !prof2) { if (na < 8) { av[na] = s + ini; an[na] = k - ini; na++; } ini = k + 1; }
                k++;
            }
            if (na < 8 && (k > ini || m->nargs)) { av[na] = s + ini; an[na] = k - ini; na++; }
            /* corpo com os argumentos (já expandidos) no lugar dos parâmetros */
            tbuf corpo = { 0, 0, 0, &c->ar };
            const char *b = m->corpo; int bn = (int)tam_texto(b);
            for (int q = 0; q < bn;) {
                if (eh_id0(b[q])) {
                    int r = q; while (r < bn && eh_id(b[r])) r++;
                    int pa = -1;
                    for (int z = 0; z < m->nargs; z++) if ((int)tam_texto(m->args[z]) == r - q && !compara_texto_n(m->args[z], b + q, (size_t)(r - q))) pa = z;
                    if (pa >= 0 && pa < na) { tbuf ex = { 0, 0, 0, &c->ar }; expandir(c, ms, av[pa], an[pa], &ex, prof + 1); if (ex.n) tb_por(&corpo, ex.b, ex.n); }
                    else tb_por(&corpo, b + q, r - q);
                    q = r; continue;
                }
                tb_por(&corpo, b + q, 1); q++;
            }
            if (corpo.n) expandir(c, ms, corpo.b, corpo.n, o, prof + 1);
            i = k + 1;
            continue;
        }
        if (s[i] >= '0' && s[i] <= '9') {   /* número inteiro (com sufixos) passa direto */
            int j = i; while (j < n && (eh_id(s[j]) || s[j] == '.')) j++;
            tb_por(o, s + i, j - i); i = j; continue;
        }
        tb_por(o, s + i, 1); i++;
    }
}

/* avaliador de #if: inteiros, defined, operadores do C */
typedef struct { const char *p; const char *f; macro *ms; comp *c; } pe;
static long pp_expr(pe *e, int prec);
static void pp_esp(pe *e) { while (e->p < e->f && (*e->p == ' ' || *e->p == '\t')) e->p++; }
static long pp_prim(pe *e)
{
    pp_esp(e);
    if (e->p >= e->f) return 0;
    char ch = *e->p;
    if (ch == '(') { e->p++; long v = pp_expr(e, 0); pp_esp(e); if (e->p < e->f && *e->p == ')') e->p++; return v; }
    if (ch == '!') { e->p++; return !pp_prim(e); }
    if (ch == '-') { e->p++; return -pp_prim(e); }
    if (ch == '+') { e->p++; return pp_prim(e); }
    if (ch == '~') { e->p++; return ~pp_prim(e); }
    if (ch >= '0' && ch <= '9') {
        long v = 0;
        if (ch == '0' && e->p + 1 < e->f && (e->p[1] | 32) == 'x') { e->p += 2; while (e->p < e->f && ((*e->p >= '0' && *e->p <= '9') || ((*e->p | 32) >= 'a' && (*e->p | 32) <= 'f'))) { v = v * 16 + (*e->p <= '9' ? *e->p - '0' : (*e->p | 32) - 'a' + 10); e->p++; } }
        else while (e->p < e->f && *e->p >= '0' && *e->p <= '9') v = v * 10 + (*e->p++ - '0');
        while (e->p < e->f && eh_id(*e->p)) e->p++;
        return v;
    }
    if (eh_id0(ch)) {
        const char *a = e->p; while (e->p < e->f && eh_id(*e->p)) e->p++;
        int n = (int)(e->p - a);
        if (n == 7 && !compara_texto_n(a, "defined", 7)) {
            pp_esp(e);
            int par = e->p < e->f && *e->p == '(';
            if (par) e->p++;
            pp_esp(e);
            const char *b = e->p; while (e->p < e->f && eh_id(*e->p)) e->p++;
            int r = achar_macro(e->ms, b, (int)(e->p - b)) != NULL;
            pp_esp(e);
            if (par && e->p < e->f && *e->p == ')') e->p++;
            return r;
        }
        macro *m = achar_macro(e->ms, a, n);
        if (m && !m->funcao) { pe e2 = { m->corpo, m->corpo + tam_texto(m->corpo), e->ms, e->c }; return pp_expr(&e2, 0); }
        if (n == 10 && !compara_texto_n(a, "GL_ES", 5)) return 1;
        return 0;   /* identificador desconhecido vale 0 */
    }
    e->p++;
    return 0;
}
static int pp_op(pe *e, int *prec, int *len)
{
    pp_esp(e);
    if (e->p >= e->f) return 0;
    const char *p = e->p; char a = p[0], b = e->p + 1 < e->f ? p[1] : 0;
    *len = 2;
    if (a == '|' && b == '|') { *prec = 1; return 1; }
    if (a == '&' && b == '&') { *prec = 2; return 2; }
    if (a == '=' && b == '=') { *prec = 6; return 3; }
    if (a == '!' && b == '=') { *prec = 6; return 4; }
    if (a == '<' && b == '=') { *prec = 7; return 5; }
    if (a == '>' && b == '=') { *prec = 7; return 6; }
    if (a == '<' && b == '<') { *prec = 8; return 7; }
    if (a == '>' && b == '>') { *prec = 8; return 8; }
    *len = 1;
    switch (a) {
    case '|': *prec = 3; return 9; case '^': *prec = 4; return 10; case '&': *prec = 5; return 11;
    case '<': *prec = 7; return 12; case '>': *prec = 7; return 13;
    case '+': *prec = 9; return 14; case '-': *prec = 9; return 15;
    case '*': *prec = 10; return 16; case '/': *prec = 10; return 17; case '%': *prec = 10; return 18;
    }
    return 0;
}
static long pp_expr(pe *e, int prec)
{
    long v = pp_prim(e);
    for (;;) {
        int pr, len, op = pp_op(e, &pr, &len);
        if (!op || pr <= prec) return v;
        e->p += len;
        long d = pp_expr(e, pr);
        switch (op) {
        case 1: v = v || d; break; case 2: v = v && d; break; case 3: v = v == d; break; case 4: v = v != d; break;
        case 5: v = v <= d; break; case 6: v = v >= d; break; case 7: v = v << d; break; case 8: v = v >> d; break;
        case 9: v = v | d; break; case 10: v = v ^ d; break; case 11: v = v & d; break; case 12: v = v < d; break; case 13: v = v > d; break;
        case 14: v = v + d; break; case 15: v = v - d; break; case 16: v = v * d; break;
        case 17: v = d ? v / d : 0; break; case 18: v = d ? v % d : 0; break;
        }
    }
}

static int preprocessar(comp *c)
{
    tbuf o = { 0, 0, 0, &c->ar };
    macro *ms = NULL;
    /* macros prontas */
    static const char *const pre[] = { "GL_ES 1", "__VERSION__ 100", "GL_FRAGMENT_PRECISION_HIGH 1", "GL_OES_standard_derivatives 1" };
    for (unsigned i = 0; i < sizeof pre / sizeof pre[0]; i++) {
        macro *m = anovo(&c->ar, sizeof(macro));
        if (!m) return 0;
        const char *sp = pre[i]; int k = 0; while (sp[k] != ' ') { m->nome[k] = sp[k]; k++; }
        m->corpo = (char *)sp + k + 1; m->prox = ms; ms = m;
    }
    /* tira comentários e junta linhas com \\ */
    tbuf limpo = { 0, 0, 0, &c->ar };
    const char *s = c->fonte; int n = c->nfonte;
    for (int i = 0; i < n;) {
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '/') { while (i < n && s[i] != '\n') i++; continue; }
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) { if (s[i] == '\n') tb_por(&limpo, "\n", 1); i++; } i += 2; tb_por(&limpo, " ", 1); continue; }
        if (s[i] == '\\' && i + 1 < n && s[i + 1] == '\n') { i += 2; continue; }
        if (s[i] == '\r') { i++; continue; }
        tb_por(&limpo, s + i, 1); i++;
    }
    /* linhas */
    int ativo[64], ja[64], pilha = 0;   /* ja: algum ramo do #if já foi tomado */
    ativo[0] = 1;
    const char *t = limpo.b ? limpo.b : ""; int tn = limpo.n;
    for (int i = 0; i < tn;) {
        int f = i; while (f < tn && t[f] != '\n') f++;
        int a = i; while (a < f && (t[a] == ' ' || t[a] == '\t')) a++;
        int vivo = ativo[pilha];
        if (a < f && t[a] == '#') {
            a++; while (a < f && (t[a] == ' ' || t[a] == '\t')) a++;
            int d0 = a; while (a < f && eh_id(t[a])) a++;
            int dn = a - d0;
            const char *dir = t + d0;
            while (a < f && (t[a] == ' ' || t[a] == '\t')) a++;
            #define DIR(x) (dn == (int)sizeof(x) - 1 && !compara_texto_n(dir, x, sizeof(x) - 1))
            if (DIR("ifdef") || DIR("ifndef")) {
                int b = a; while (b < f && eh_id(t[b])) b++;
                int def = achar_macro(ms, t + a, b - a) != NULL;
                int v = DIR("ifdef") ? def : !def;
                if (pilha < 63) { pilha++; ativo[pilha] = vivo && v; ja[pilha] = v; }
            } else if (DIR("if")) {
                pe e = { t + a, t + f, ms, c };
                int v = vivo ? pp_expr(&e, 0) != 0 : 0;
                if (pilha < 63) { pilha++; ativo[pilha] = vivo && v; ja[pilha] = v; }
            } else if (DIR("elif")) {
                int pai = pilha > 0 ? ativo[pilha - 1] : 1;
                pe e = { t + a, t + f, ms, c };
                int v = !ja[pilha] && pai ? pp_expr(&e, 0) != 0 : 0;
                ativo[pilha] = pai && v; if (v) ja[pilha] = 1;
            } else if (DIR("else")) {
                int pai = pilha > 0 ? ativo[pilha - 1] : 1;
                ativo[pilha] = pai && !ja[pilha]; ja[pilha] = 1;
            } else if (DIR("endif")) { if (pilha > 0) pilha--; }
            else if (vivo && DIR("define")) {
                int b = a; while (b < f && eh_id(t[b])) b++;
                macro *m = anovo(&c->ar, sizeof(macro));
                if (!m) return 0;
                int k = b - a < 47 ? b - a : 47; memcpy(m->nome, t + a, (size_t)k);
                if (b < f && t[b] == '(') {
                    m->funcao = 1;
                    b++;
                    while (b < f && t[b] != ')') {
                        while (b < f && (t[b] == ' ' || t[b] == ',')) b++;
                        int q = b; while (b < f && eh_id(t[b])) b++;
                        if (b > q && m->nargs < 16) { int kk = b - q < 63 ? b - q : 63; memcpy(m->args[m->nargs], t + q, (size_t)kk); m->nargs++; }
                        while (b < f && t[b] == ' ') b++;
                    }
                    b++;
                }
                while (b < f && (t[b] == ' ' || t[b] == '\t')) b++;
                int e = f; while (e > b && (t[e - 1] == ' ' || t[e - 1] == '\t')) e--;
                m->corpo = anovo(&c->ar, e - b + 1);
                if (m->corpo) memcpy(m->corpo, t + b, (size_t)(e - b));
                /* redefinir substitui */
                for (macro **pp = &ms; *pp; pp = &(*pp)->prox) if (!compara_texto((*pp)->nome, m->nome)) { *pp = (*pp)->prox; break; }
                m->prox = ms; ms = m;
            } else if (vivo && DIR("undef")) {
                int b = a; while (b < f && eh_id(t[b])) b++;
                for (macro **pp = &ms; *pp; pp = &(*pp)->prox) if ((int)tam_texto((*pp)->nome) == b - a && !compara_texto_n((*pp)->nome, t + a, (size_t)(b - a))) { *pp = (*pp)->prox; break; }
            } else if (vivo && DIR("error")) { c->linha = 0; falha(c, "#error", NULL); return 0; }
            /* #version, #extension, #pragma, #line: nada a fazer */
            #undef DIR
            tb_por(&o, "\n", 1);
        } else {
            if (vivo) expandir(c, ms, t + i, f - i, &o, 0);
            tb_por(&o, "\n", 1);
        }
        i = f + 1;
    }
    c->pp = o.b ? o.b : (char *)""; c->npp = o.n;
    return 1;
}

/* ================================================================ léxico */
enum { K_FIM = 256, K_ID, K_NUM, K_INTN, K_TIPO,
       K_ATTRIBUTE, K_CONST, K_UNIFORM, K_VARYING, K_BREAK, K_CONTINUE, K_DO, K_FOR, K_WHILE, K_IF, K_ELSE, K_IN, K_OUT, K_INOUT,
       K_TRUE, K_FALSE, K_RETURN, K_DISCARD, K_STRUCT, K_PRECISION, K_HIGHP, K_MEDIUMP, K_LOWP, K_INVARIANT,
       K_INC, K_DEC, K_LE, K_GE, K_EQ, K_NE, K_AND, K_OR, K_XOR, K_ADDA, K_SUBA, K_MULA, K_DIVA };
static const struct { const char *s; int k; } chaves[] = {
    {"attribute",K_ATTRIBUTE},{"const",K_CONST},{"uniform",K_UNIFORM},{"varying",K_VARYING},{"break",K_BREAK},{"continue",K_CONTINUE},{"do",K_DO},
    {"for",K_FOR},{"while",K_WHILE},{"if",K_IF},{"else",K_ELSE},{"in",K_IN},{"out",K_OUT},{"inout",K_INOUT},{"true",K_TRUE},{"false",K_FALSE},
    {"return",K_RETURN},{"discard",K_DISCARD},{"struct",K_STRUCT},{"precision",K_PRECISION},{"highp",K_HIGHP},{"mediump",K_MEDIUMP},{"lowp",K_LOWP},
    {"invariant",K_INVARIANT},
};
static const struct { const char *s; uint8_t base, lin, col; } tipos_b[] = {
    {"void",T_VOID,0,0},{"float",T_FLOAT,1,1},{"int",T_INT,1,1},{"bool",T_BOOL,1,1},
    {"vec2",T_FLOAT,2,1},{"vec3",T_FLOAT,3,1},{"vec4",T_FLOAT,4,1},{"ivec2",T_INT,2,1},{"ivec3",T_INT,3,1},{"ivec4",T_INT,4,1},
    {"bvec2",T_BOOL,2,1},{"bvec3",T_BOOL,3,1},{"bvec4",T_BOOL,4,1},{"mat2",T_FLOAT,2,2},{"mat3",T_FLOAT,3,3},{"mat4",T_FLOAT,4,4},
    {"sampler2D",T_SAMPLER2D,1,1},{"samplerCube",T_SAMPLERCUBE,1,1},
};
static tipo tipo_lido;
static void lex(comp *c)
{
    const char *p = c->p;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { c->linha++; p++; continue; }
        break;
    }
    if (!*p) { c->tk = K_FIM; c->p = p; return; }
    if (eh_id0(*p)) {
        const char *a = p; while (eh_id(*p)) p++;
        int n = (int)(p - a);
        if (n > 63) n = 63;
        memcpy(c->lex, a, (size_t)n); c->lex[n] = 0;
        c->p = p;
        for (unsigned i = 0; i < sizeof chaves / sizeof chaves[0]; i++) if (!compara_texto(chaves[i].s, c->lex)) { c->tk = chaves[i].k; return; }
        for (unsigned i = 0; i < sizeof tipos_b / sizeof tipos_b[0]; i++) if (!compara_texto(tipos_b[i].s, c->lex)) {
            memset(&tipo_lido, 0, sizeof tipo_lido);
            tipo_lido.base = tipos_b[i].base; tipo_lido.lin = tipos_b[i].lin; tipo_lido.col = tipos_b[i].col;
            c->tk = K_TIPO; return;
        }
        for (int i = 0; i < c->nstructs; i++) if (!compara_texto(c->structs[i]->nome, c->lex)) {
            memset(&tipo_lido, 0, sizeof tipo_lido); tipo_lido.base = T_STRUCT; tipo_lido.s = c->structs[i]; tipo_lido.lin = tipo_lido.col = 1;
            c->tk = K_TIPO; return;
        }
        c->tk = K_ID; return;
    }
    if ((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9')) {
        double v = 0; int inteiro = 1;
        if (*p == '0' && (p[1] | 32) == 'x') { p += 2; while ((*p >= '0' && *p <= '9') || ((*p | 32) >= 'a' && (*p | 32) <= 'f')) { v = v * 16 + (*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10); p++; } }
        else if (*p == '0' && p[1] >= '0' && p[1] <= '7') { while (*p >= '0' && *p <= '7') v = v * 8 + (*p++ - '0'); }
        else {
            while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
            if (*p == '.') { inteiro = 0; p++; double f = 0.1; while (*p >= '0' && *p <= '9') { v += f * (*p++ - '0'); f /= 10; } }
            if ((*p | 32) == 'e') {
                inteiro = 0; p++; int es = 1, e = 0;
                if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
                while (*p >= '0' && *p <= '9') e = e * 10 + (*p++ - '0');
                while (e--) v = es > 0 ? v * 10 : v / 10;
            }
        }
        if ((*p | 32) == 'f') { inteiro = 0; p++; }
        while (eh_id(*p)) p++;   /* sufixos (u, etc.) */
        c->num = v; c->tk = inteiro ? K_INTN : K_NUM; c->p = p;
        return;
    }
    char a = *p, b = p[1];
    c->p = p + 2;
    if (a == '+' && b == '+') { c->tk = K_INC; return; } if (a == '-' && b == '-') { c->tk = K_DEC; return; }
    if (a == '<' && b == '=') { c->tk = K_LE; return; } if (a == '>' && b == '=') { c->tk = K_GE; return; }
    if (a == '=' && b == '=') { c->tk = K_EQ; return; } if (a == '!' && b == '=') { c->tk = K_NE; return; }
    if (a == '&' && b == '&') { c->tk = K_AND; return; } if (a == '|' && b == '|') { c->tk = K_OR; return; } if (a == '^' && b == '^') { c->tk = K_XOR; return; }
    if (a == '+' && b == '=') { c->tk = K_ADDA; return; } if (a == '-' && b == '=') { c->tk = K_SUBA; return; }
    if (a == '*' && b == '=') { c->tk = K_MULA; return; } if (a == '/' && b == '=') { c->tk = K_DIVA; return; }
    c->p = p + 1;
    c->tk = (uint8_t)a;
}
static void exige(comp *c, int k)
{
    if (c->tk != k) { char b[24]; if (k < 256) formatar_texto(b, sizeof b, "'%c'", k); else formatar_texto(b, sizeof b, "token %d", k); falha(c, "esperava", b); return; }
    lex(c);
}

/* ================================================================ AST */
enum { N_NUM, N_VAR, N_BIN, N_UN, N_ATR, N_COND, N_CHAMA, N_CAMPO, N_IDX, N_SEQ, N_POS, N_PRE, N_CONSTRUTOR };
typedef struct sim {
    char nome[64];
    tipo t;
    int slot;            /* posição na memória da VM (floats por via) */
    int classe;          /* 0 local, 1 uniform, 2 attribute, 3 varying, 4 const, 5 embutida */
    struct sim *prox;
    double cval[16]; int tem_cval;   /* const com valor conhecido */
} sim;
typedef struct no {
    int k, op;
    tipo t;
    struct no *a, *b, *c, **args; int nargs;
    double v[16];        /* constante */
    sim *s;              /* variável */
    struct fn *f;        /* chamada de função do usuário */
    int embutida;        /* chamada de função embutida (índice) */
    char campo[64];       /* swizzle ou nome de campo */
    int sw[4], nsw;      /* swizzle resolvido */
    int linha, ok;
} no;
typedef struct cmd {
    int k;               /* 0 expr, 1 decl, 2 bloco, 3 if, 4 for, 5 while, 6 do, 7 return, 8 break, 9 continue, 10 discard */
    no *e, *e2, *e3; struct cmd *a, *b; struct cmd *filhos, *prox;
    sim *decl; no *init;
} cmd;
typedef struct fn {
    char nome[64]; tipo ret; sim *params[16]; int qual[16]; int np; cmd *corpo; struct fn *prox; int definida;
} fn;

static sim *achar_sim(comp *c, const char *n)
{
    for (int i = c->nescopo - 1; i >= 0; i--) for (sim *s = c->escopo[i]; s; s = s->prox) if (!compara_texto(s->nome, n)) return s;
    return NULL;
}
static sim *novo_sim(comp *c, const char *n, const tipo *t, int classe)
{
    sim *s = anovo(&c->ar, sizeof(sim));
    if (!s) return NULL;
    copia_texto(s->nome, n, sizeof s->nome); s->t = *t; s->classe = classe; s->slot = -1;
    s->prox = c->escopo[c->nescopo - 1]; c->escopo[c->nescopo - 1] = s;
    return s;
}

/* ---- parser de expressões */
static no *expr(comp *c);
static no *atrib(comp *c);
static no *nno(comp *c, int k) { no *n = anovo(&c->ar, sizeof(no)); if (!n) { falha(c, "sem memoria", NULL); static no z; return &z; } n->k = k; n->linha = c->linha; return n; }
static tipo t_esc(int base) { tipo t; memset(&t, 0, sizeof t); t.base = (uint8_t)base; t.lin = t.col = 1; return t; }
static tipo t_vec(int base, int n) { tipo t = t_esc(base); t.lin = (uint8_t)n; return t; }

static int ler_tipo_struct(comp *c, tipo *out);
static no *primaria(comp *c)
{
    no *n;
    if (c->tk == K_NUM || c->tk == K_INTN) {
        n = nno(c, N_NUM); n->v[0] = c->num; n->t = t_esc(c->tk == K_NUM ? T_FLOAT : T_INT);
        lex(c); return n;
    }
    if (c->tk == K_TRUE || c->tk == K_FALSE) { n = nno(c, N_NUM); n->v[0] = c->tk == K_TRUE; n->t = t_esc(T_BOOL); lex(c); return n; }
    if (c->tk == '(') { lex(c); n = expr(c); exige(c, ')'); return n; }
    if (c->tk == K_TIPO) {   /* construtor: vec3(...), mat4(...), Struct(...) */
        tipo t = tipo_lido; lex(c);
        if (c->tk == '[') { lex(c); if (c->tk == K_INTN) { t.arr = (int)c->num; lex(c); } exige(c, ']'); }
        n = nno(c, N_CONSTRUTOR); n->t = t;
        exige(c, '(');
        no *args[20]; int na = 0;
        if (c->tk != ')') for (;;) { if (na < 20) args[na++] = atrib(c); else atrib(c); if (c->tk != ',') break; lex(c); }
        exige(c, ')');
        n->args = anovo(&c->ar, (int)sizeof(no *) * (na + 1)); if (n->args) memcpy(n->args, args, sizeof(no *) * (size_t)na); n->nargs = na;
        return n;
    }
    if (c->tk == K_ID) {
        char nome[64]; copia_texto(nome, c->lex, sizeof nome);
        lex(c);
        if (c->tk == '(') {
            n = nno(c, N_CHAMA); copia_texto(n->campo, "", sizeof n->campo);
            n->s = NULL;
            lex(c);
            no *args[20]; int na = 0;
            if (c->tk == K_TIPO && !compara_texto(c->lex, "void")) lex(c);
            if (c->tk != ')') for (;;) { if (na < 20) args[na++] = atrib(c); else atrib(c); if (c->tk != ',') break; lex(c); }
            exige(c, ')');
            n->args = anovo(&c->ar, (int)sizeof(no *) * (na + 1)); if (n->args) memcpy(n->args, args, sizeof(no *) * (size_t)na); n->nargs = na;
            /* o nome fica num sim de mentira para a checagem depois */
            sim *s = anovo(&c->ar, sizeof(sim)); if (s) copia_texto(s->nome, nome, sizeof s->nome);
            n->s = s; n->embutida = -1;
            return n;
        }
        n = nno(c, N_VAR);
        n->s = achar_sim(c, nome);
        if (!n->s) { falha(c, "identificador nao declarado:", nome); return n; }
        n->t = n->s->t;
        return n;
    }
    falha(c, "expressao invalida", NULL);
    lex(c);
    return nno(c, N_NUM);
}
static int componente(char ch)
{
    switch (ch) { case 'x': case 'r': case 's': return 0; case 'y': case 'g': case 't': return 1; case 'z': case 'b': case 'p': return 2; case 'w': case 'a': case 'q': return 3; }
    return -1;
}
static no *posfixa(comp *c)
{
    no *n = primaria(c);
    for (;;) {
        if (c->tk == '.') {
            lex(c);
            no *m = nno(c, N_CAMPO); m->a = n;
            copia_texto(m->campo, c->lex, sizeof m->campo);
            lex(c);
            n = m;
        } else if (c->tk == '[') {
            lex(c); no *m = nno(c, N_IDX); m->a = n; m->b = expr(c); exige(c, ']'); n = m;
        } else if (c->tk == K_INC || c->tk == K_DEC) {
            no *m = nno(c, N_POS); m->op = c->tk; m->a = n; lex(c); n = m;
        } else return n;
    }
}
static no *unaria(comp *c)
{
    if (c->tk == '-' || c->tk == '+' || c->tk == '!' || c->tk == '~') { int op = c->tk; lex(c); no *n = nno(c, N_UN); n->op = op; n->a = unaria(c); return n; }
    if (c->tk == K_INC || c->tk == K_DEC) { int op = c->tk; lex(c); no *n = nno(c, N_PRE); n->op = op; n->a = unaria(c); return n; }
    return posfixa(c);
}
static int prec_bin(int tk)
{
    switch (tk) {
    case K_OR: return 1; case K_XOR: return 2; case K_AND: return 3;
    case K_EQ: case K_NE: return 6; case '<': case '>': case K_LE: case K_GE: return 7;
    case '+': case '-': return 9; case '*': case '/': case '%': return 10;
    }
    return 0;
}
static no *binaria(comp *c, int prec)
{
    no *a = unaria(c);
    for (;;) {
        int p = prec_bin(c->tk);
        if (!p || p <= prec) return a;
        int op = c->tk; lex(c);
        no *n = nno(c, N_BIN); n->op = op; n->a = a; n->b = binaria(c, p);
        a = n;
    }
}
static no *condicional(comp *c)
{
    no *n = binaria(c, 0);
    if (c->tk == '?') { lex(c); no *m = nno(c, N_COND); m->a = n; m->b = expr(c); exige(c, ':'); m->c = condicional(c); return m; }
    return n;
}
static no *atrib(comp *c)
{
    no *n = condicional(c);
    if (c->tk == '=' || c->tk == K_ADDA || c->tk == K_SUBA || c->tk == K_MULA || c->tk == K_DIVA) {
        int op = c->tk; lex(c);
        no *m = nno(c, N_ATR); m->op = op; m->a = n; m->b = atrib(c);
        return m;
    }
    return n;
}
static no *expr(comp *c)
{
    no *n = atrib(c);
    while (c->tk == ',') { lex(c); no *m = nno(c, N_SEQ); m->a = n; m->b = atrib(c); n = m; }
    return n;
}

/* ---- comandos e declarações */
static cmd *comando(comp *c);
static cmd *ncmd(comp *c, int k) { cmd *m = anovo(&c->ar, sizeof(cmd)); if (!m) { falha(c, "sem memoria", NULL); static cmd z; return &z; } m->k = k; return m; }
static int qualificadores(comp *c, int *classe)
{
    int q = 1;
    *classe = 0;
    for (;;) {
        if (c->tk == K_CONST) { *classe = 4; lex(c); }
        else if (c->tk == K_UNIFORM) { *classe = 1; lex(c); }
        else if (c->tk == K_ATTRIBUTE) { *classe = 2; lex(c); }
        else if (c->tk == K_VARYING) { *classe = 3; lex(c); }
        else if (c->tk == K_INVARIANT || c->tk == K_HIGHP || c->tk == K_MEDIUMP || c->tk == K_LOWP) lex(c);
        else break;
        q = 1;
    }
    return q;
}
static int ler_tipo_struct(comp *c, tipo *out)
{
    /* struct Nome { campos } */
    lex(c);
    st *s = anovo(&c->ar, sizeof(st));
    if (!s) return 0;
    if (c->tk == K_ID) { copia_texto(s->nome, c->lex, sizeof s->nome); lex(c); }
    exige(c, '{');
    while (c->tk != '}' && c->tk != K_FIM && !c->falhou) {
        int cl; qualificadores(c, &cl);
        if (c->tk != K_TIPO) { falha(c, "tipo esperado no struct", NULL); return 0; }
        tipo t = tipo_lido; lex(c);
        for (;;) {
            if (c->tk != K_ID || s->nc >= 32) { falha(c, "campo esperado", NULL); return 0; }
            campo *f = &s->c[s->nc++];
            copia_texto(f->nome, c->lex, sizeof f->nome); f->t = t; lex(c);
            if (c->tk == '[') { lex(c); f->t.arr = (int)c->num; lex(c); exige(c, ']'); }
            f->desl = s->tam; s->tam += tam_tipo(&f->t);
            if (c->tk != ',') break;
            lex(c);
        }
        exige(c, ';');
    }
    exige(c, '}');
    if (c->nstructs < 32 && s->nome[0]) c->structs[c->nstructs++] = s;
    memset(out, 0, sizeof *out); out->base = T_STRUCT; out->s = s; out->lin = out->col = 1;
    return 1;
}
static int avaliar_const(comp *c, no *n, double *v);
static cmd *declaracao(comp *c, int classe, tipo t)
{
    cmd *lista = NULL, **fim = &lista;
    for (;;) {
        if (c->tk != K_ID) { falha(c, "nome esperado na declaracao", NULL); return lista; }
        char nome[64]; copia_texto(nome, c->lex, sizeof nome); lex(c);
        tipo tt = t;
        if (c->tk == '[') {
            lex(c);
            no *e = condicional(c);
            double v[16]; if (avaliar_const(c, e, v)) tt.arr = (int)v[0]; else falha(c, "tamanho de array precisa ser constante", NULL);
            exige(c, ']');
        }
        cmd *d = ncmd(c, 1);
        d->decl = novo_sim(c, nome, &tt, classe);
        if (c->tk == '=') { lex(c); d->init = atrib(c); }
        if (classe == 4 && d->init && d->decl) {
            double v[16];
            if (avaliar_const(c, d->init, v)) { memcpy(d->decl->cval, v, sizeof v); d->decl->tem_cval = 1; }
        }
        *fim = d; fim = &d->prox;
        if (c->tk != ',') break;
        lex(c);
    }
    exige(c, ';');
    return lista;
}
static cmd *bloco(comp *c)
{
    exige(c, '{');
    cmd *b = ncmd(c, 2), **fim = &b->filhos;
    if (c->nescopo < 64) c->escopo[c->nescopo++] = NULL;
    while (c->tk != '}' && c->tk != K_FIM && !c->falhou) {
        cmd *s = comando(c);
        while (s) { *fim = s; fim = &s->prox; cmd *n = s->prox; s->prox = NULL; s = n; }
    }
    c->nescopo--;
    exige(c, '}');
    return b;
}
static cmd *comando(comp *c)
{
    if (c->tk == '{') return bloco(c);
    if (c->tk == ';') { lex(c); return ncmd(c, 2); }
    if (c->tk == K_IF) {
        lex(c); exige(c, '(');
        cmd *m = ncmd(c, 3); m->e = expr(c); exige(c, ')');
        if (c->nescopo < 64) c->escopo[c->nescopo++] = NULL;
        m->a = comando(c);
        c->nescopo--;
        if (c->tk == K_ELSE) { lex(c); if (c->nescopo < 64) c->escopo[c->nescopo++] = NULL; m->b = comando(c); c->nescopo--; }
        return m;
    }
    if (c->tk == K_FOR) {
        lex(c); exige(c, '(');
        cmd *m = ncmd(c, 4);
        if (c->nescopo < 64) c->escopo[c->nescopo++] = NULL;
        if (c->tk == K_TIPO || c->tk == K_CONST || c->tk == K_HIGHP || c->tk == K_MEDIUMP || c->tk == K_LOWP) {
            int cl; qualificadores(c, &cl);
            tipo t = tipo_lido; lex(c);
            m->a = declaracao(c, 0, t);
        } else { if (c->tk != ';') { m->a = ncmd(c, 0); m->a->e = expr(c); } exige(c, ';'); }
        if (c->tk != ';') m->e = expr(c);
        exige(c, ';');
        if (c->tk != ')') m->e3 = expr(c);
        exige(c, ')');
        m->b = comando(c);
        c->nescopo--;
        return m;
    }
    if (c->tk == K_WHILE) { lex(c); exige(c, '('); cmd *m = ncmd(c, 5); m->e = expr(c); exige(c, ')'); m->b = comando(c); return m; }
    if (c->tk == K_DO) { lex(c); cmd *m = ncmd(c, 6); m->b = comando(c); exige(c, K_WHILE); exige(c, '('); m->e = expr(c); exige(c, ')'); exige(c, ';'); return m; }
    if (c->tk == K_RETURN) { lex(c); cmd *m = ncmd(c, 7); if (c->tk != ';') m->e = expr(c); exige(c, ';'); return m; }
    if (c->tk == K_BREAK) { lex(c); exige(c, ';'); return ncmd(c, 8); }
    if (c->tk == K_CONTINUE) { lex(c); exige(c, ';'); return ncmd(c, 9); }
    if (c->tk == K_DISCARD) { lex(c); exige(c, ';'); return ncmd(c, 10); }
    if (c->tk == K_TIPO || c->tk == K_CONST || c->tk == K_HIGHP || c->tk == K_MEDIUMP || c->tk == K_LOWP || c->tk == K_STRUCT) {
        int cl; qualificadores(c, &cl);
        tipo t;
        if (c->tk == K_STRUCT) { if (!ler_tipo_struct(c, &t)) return NULL; if (c->tk == ';') { lex(c); return NULL; } }
        else {
            /* "vec3(1.0).x;" seria uma expressão: tipo seguido de '(' */
            const char *salvo = c->p; int tk = c->tk, lin = c->linha; tipo tl = tipo_lido; char lx[64]; copia_texto(lx, c->lex, sizeof lx);
            lex(c);
            if (c->tk == '(') { c->p = salvo; c->tk = tk; c->linha = lin; tipo_lido = tl; copia_texto(c->lex, lx, sizeof c->lex); goto expressao; }
            t = tl;
        }
        return declaracao(c, cl, t);
    }
expressao:;
    cmd *m = ncmd(c, 0); m->e = expr(c); exige(c, ';');
    return m;
}

/* ---- topo: funções, globais, precision */
static void topo(comp *c)
{
    while (c->tk != K_FIM && !c->falhou) {
        if (c->tk == K_PRECISION) { while (c->tk != ';' && c->tk != K_FIM) lex(c); lex(c); continue; }
        if (c->tk == ';') { lex(c); continue; }
        int cl; qualificadores(c, &cl);
        tipo t;
        if (c->tk == K_STRUCT) { if (!ler_tipo_struct(c, &t)) return; if (c->tk == ';') { lex(c); continue; } }
        else if (c->tk == K_TIPO) { t = tipo_lido; lex(c); }
        else if (c->tk == K_ID && cl) { /* "invariant gl_Position;" */ while (c->tk != ';' && c->tk != K_FIM) lex(c); lex(c); continue; }
        else { falha(c, "declaracao invalida:", c->lex); return; }
        if (c->tk == K_ID) {
            /* função? */
            const char *salvo = c->p; int lin = c->linha; char nome[64]; copia_texto(nome, c->lex, sizeof nome);
            lex(c);
            if (c->tk == '(') {
                lex(c);
                fn *f = anovo(&c->ar, sizeof(fn));
                if (!f) return;
                copia_texto(f->nome, nome, sizeof f->nome); f->ret = t;
                if (c->nescopo < 64) c->escopo[c->nescopo++] = NULL;
                if (c->tk == K_TIPO && tipo_lido.base == T_VOID) lex(c);
                while (c->tk != ')' && c->tk != K_FIM && !c->falhou) {
                    int q = 0;
                    for (;;) {
                        if (c->tk == K_IN) { q = 0; lex(c); } else if (c->tk == K_OUT) { q = 1; lex(c); } else if (c->tk == K_INOUT) { q = 2; lex(c); }
                        else if (c->tk == K_CONST || c->tk == K_HIGHP || c->tk == K_MEDIUMP || c->tk == K_LOWP) lex(c);
                        else break;
                    }
                    if (c->tk != K_TIPO) { falha(c, "tipo do parametro", NULL); break; }
                    tipo pt = tipo_lido; lex(c);
                    char pn[64] = "";
                    if (c->tk == K_ID) { copia_texto(pn, c->lex, sizeof pn); lex(c); }
                    if (c->tk == '[') { lex(c); pt.arr = (int)c->num; lex(c); exige(c, ']'); }
                    if (f->np < 16) { f->qual[f->np] = q; f->params[f->np++] = novo_sim(c, pn[0] ? pn : "_", &pt, 0); }
                    if (c->tk == ',') lex(c);
                }
                exige(c, ')');
                if (c->tk == ';') { lex(c); c->nescopo--; f->prox = c->fns; c->fns = f; continue; }   /* protótipo */
                f->corpo = bloco(c);
                f->definida = 1;
                c->nescopo--;
                /* a definição substitui o protótipo de mesma assinatura */
                f->prox = c->fns; c->fns = f;
                continue;
            }
            c->p = salvo; c->linha = lin; c->tk = K_ID; copia_texto(c->lex, nome, sizeof c->lex);
        }
        cmd *d = declaracao(c, cl, t);
        /* globais: entram na lista do programa (inicializadores rodam no começo do main) */
        cmd **fim = (cmd **)&c->prog->globais;
        while (*fim) fim = &(*fim)->prox;
        *fim = d;
    }
}

/* ================================================================ constantes (avaliação em tempo de compilação) */
static int checar(comp *c, no *n);
static int avaliar_const(comp *c, no *n, double *v)
{
    if (!checar(c, n)) return 0;
    switch (n->k) {
    case N_NUM: memcpy(v, n->v, sizeof n->v); return 1;
    case N_VAR: if (n->s && n->s->tem_cval) { memcpy(v, n->s->cval, sizeof n->s->cval); return 1; } return 0;
    case N_UN: { if (!avaliar_const(c, n->a, v)) return 0; int k = tam_tipo(&n->t); for (int i = 0; i < k; i++) v[i] = n->op == '-' ? -v[i] : n->op == '!' ? !v[i] : v[i]; return 1; }
    case N_BIN: {
        double a[16], b[16];
        if (!avaliar_const(c, n->a, a) || !avaliar_const(c, n->b, b)) return 0;
        int ka = tam_tipo(&n->a->t), kb = tam_tipo(&n->b->t), k = tam_tipo(&n->t);
        if (n->a->t.col > 1 || n->b->t.col > 1) return 0;   /* matrizes: deixa para a VM */
        for (int i = 0; i < k; i++) {
            double x = a[ka == 1 ? 0 : i], y = b[kb == 1 ? 0 : i];
            switch (n->op) {
            case '+': v[i] = x + y; break; case '-': v[i] = x - y; break; case '*': v[i] = x * y; break;
            case '/': v[i] = n->t.base == T_INT ? (y ? (double)(int)(x / y) : 0) : x / y; break;
            case '<': v[i] = x < y; break; case '>': v[i] = x > y; break; case K_LE: v[i] = x <= y; break; case K_GE: v[i] = x >= y; break;
            case K_EQ: v[i] = x == y; break; case K_NE: v[i] = x != y; break; case K_AND: v[i] = x && y; break; case K_OR: v[i] = x || y; break;
            default: return 0;
            }
        }
        if (n->op == K_EQ || n->op == K_NE) { int r = 1; for (int i = 0; i < (ka > kb ? ka : kb); i++) if (!(a[ka == 1 ? 0 : i] == b[kb == 1 ? 0 : i])) r = 0; v[0] = n->op == K_EQ ? r : !r; }
        return 1;
    }
    case N_CONSTRUTOR: {
        double tmp[64]; int k = 0;
        for (int i = 0; i < n->nargs; i++) { double x[16]; if (!avaliar_const(c, n->args[i], x)) return 0; int ka = tam_tipo(&n->args[i]->t); for (int j = 0; j < ka && k < 64; j++) tmp[k++] = x[j]; }
        int kt = tam_tipo(&n->t);
        if (n->t.col > 1) return 0;
        for (int i = 0; i < kt; i++) v[i] = k == 1 ? tmp[0] : i < k ? tmp[i] : 0;
        if (n->t.base == T_INT) for (int i = 0; i < kt; i++) v[i] = (double)(int)v[i];
        if (n->t.base == T_BOOL) for (int i = 0; i < kt; i++) v[i] = v[i] != 0;
        return 1;
    }
    default: return 0;
    }
}

/* ================================================================ checagem de tipos */

static int eh_escalar(const tipo *t) { return !t->arr && t->base != T_STRUCT && t->lin == 1 && t->col == 1; }
static int eh_vetor(const tipo *t) { return !t->arr && t->base != T_STRUCT && t->col == 1 && t->lin > 1; }
static int eh_matriz(const tipo *t) { return !t->arr && t->col > 1; }
#include "glsl_emb.h"   /* tabela das funções embutidas (gerada à mão) */

static int checar(comp *c, no *n)
{
    if (!n || c->falhou) return 0;
    if (n->t.base || n->k == N_NUM) { if (n->k != N_CHAMA && n->k != N_BIN && n->k != N_UN && n->k != N_CAMPO && n->k != N_IDX && n->k != N_COND && n->k != N_ATR && n->k != N_CONSTRUTOR && n->k != N_SEQ && n->k != N_POS && n->k != N_PRE) return 1; }
    switch (n->k) {
    case N_NUM: case N_VAR: return 1;
    case N_SEQ: if (!checar(c, n->a) || !checar(c, n->b)) return 0; n->t = n->b->t; return 1;
    case N_UN: case N_POS: case N_PRE:
        if (!checar(c, n->a)) return 0;
        n->t = n->a->t;
        if (n->k == N_UN && n->op == '!' && n->t.base != T_BOOL) { falha(c, "! precisa de bool", NULL); return 0; }
        return 1;
    case N_COND:
        if (!checar(c, n->a) || !checar(c, n->b) || !checar(c, n->c)) return 0;
        n->t = n->b->t; return 1;
    case N_BIN: {
        if (!checar(c, n->a) || !checar(c, n->b)) return 0;
        tipo *a = &n->a->t, *b = &n->b->t;
        int op = n->op;
        if (op == K_AND || op == K_OR || op == K_XOR || op == '<' || op == '>' || op == K_LE || op == K_GE || op == K_EQ || op == K_NE) { n->t = t_esc(T_BOOL); return 1; }
        if (a->base != b->base && !(a->base == T_FLOAT && b->base == T_INT) && !(a->base == T_INT && b->base == T_FLOAT)) { falha(c, "tipos diferentes na operacao", NULL); return 0; }
        if (op == '*' && (eh_matriz(a) || eh_matriz(b))) {
            if (eh_matriz(a) && eh_matriz(b)) n->t = *a;
            else if (eh_matriz(a) && eh_vetor(b)) n->t = *b;           /* mat * vec */
            else if (eh_vetor(a) && eh_matriz(b)) n->t = *a;           /* vec * mat */
            else n->t = eh_matriz(a) ? *a : *b;                        /* escalar * mat */
            return 1;
        }
        n->t = eh_escalar(a) ? *b : *a;
        return 1;
    }
    case N_ATR:
        if (!checar(c, n->a) || !checar(c, n->b)) return 0;
        n->t = n->a->t;
        if (!tipo_igual(&n->a->t, &n->b->t) && !(n->op != '=' && eh_escalar(&n->b->t) && n->b->t.base == n->a->t.base)
            && !(n->op == K_MULA && (eh_matriz(&n->a->t) || eh_matriz(&n->b->t)))) { falha(c, "atribuicao com tipos diferentes", NULL); return 0; }
        return 1;
    case N_CAMPO: {
        if (!checar(c, n->a)) return 0;
        tipo *t = &n->a->t;
        if (t->base == T_STRUCT && !t->arr) {
            for (int i = 0; i < t->s->nc; i++) if (!compara_texto(t->s->c[i].nome, n->campo)) { n->t = t->s->c[i].t; n->nsw = -1; n->sw[0] = t->s->c[i].desl; return 1; }
            falha(c, "campo inexistente:", n->campo); return 0;
        }
        if (n->a->t.col > 1 || t->arr) { falha(c, "acesso invalido com .", n->campo); return 0; }
        int k = (int)tam_texto(n->campo);
        if (k < 1 || k > 4) { falha(c, "swizzle invalido", n->campo); return 0; }
        for (int i = 0; i < k; i++) { int x = componente(n->campo[i]); if (x < 0 || x >= t->lin) { falha(c, "swizzle fora do vetor:", n->campo); return 0; } n->sw[i] = x; }
        n->nsw = k;
        n->t = t_vec(t->base, k);
        return 1;
    }
    case N_IDX: {
        if (!checar(c, n->a) || !checar(c, n->b)) return 0;
        tipo t = n->a->t;
        if (t.arr) { t.arr = 0; n->t = t; return 1; }
        if (t.col > 1) { n->t = t_vec(t.base, t.lin); return 1; }   /* coluna da matriz */
        if (t.lin > 1) { n->t = t_esc(t.base); return 1; }
        falha(c, "indice em escalar", NULL); return 0;
    }
    case N_CONSTRUTOR:
        for (int i = 0; i < n->nargs; i++) if (!checar(c, n->args[i])) return 0;
        return 1;
    case N_CHAMA: {
        for (int i = 0; i < n->nargs; i++) if (!checar(c, n->args[i])) return 0;
        /* função do usuário com os tipos certos */
        for (fn *f = c->fns; f; f = f->prox) {
            if (compara_texto(f->nome, n->s->nome) || f->np != n->nargs) continue;
            int ok = 1;
            for (int i = 0; i < f->np && ok; i++) if (!tipo_igual(&f->params[i]->t, &n->args[i]->t)) {
                /* aceita int literal em parâmetro float? GLSL ES não converte: exige igual */
                ok = 0;
            }
            if (!ok) continue;
            /* prefere a definição ao protótipo */
            fn *def = f;
            if (!f->definida) for (fn *g = c->fns; g; g = g->prox) if (g->definida && !compara_texto(g->nome, f->nome) && g->np == f->np) { int ig = 1; for (int i = 0; i < g->np; i++) if (!tipo_igual(&g->params[i]->t, &f->params[i]->t)) ig = 0; if (ig) def = g; }
            n->f = def; n->t = def->ret; return 1;
        }
        int e = emb_achar(n->s->nome, n->args, n->nargs, &n->t);
        if (e < 0) { falha(c, "funcao sem sobrecarga para esses argumentos:", n->s->nome); return 0; }
        n->embutida = e;
        return 1;
    }
    }
    return 1;
}

/* ================================================================ geração de código (ver glsl_gen.h) */
#include "glsl_gen.h"

/* ================================================================ API */
glsl_prog *glsl_compilar(const char *fonte, int n, int tipo_shader, char *erro, int max_erro)
{
    comp *c = aloca(sizeof(comp));
    if (!c) return NULL;
    memset(c, 0, sizeof *c);
    c->fonte = fonte; c->nfonte = n; c->tipo_shader = tipo_shader; c->linha = 1;
    glsl_prog *p = aloca(sizeof(glsl_prog));
    if (!p) { libera(c); return NULL; }
    memset(p, 0, sizeof *p);
    c->prog = p; p->tipo = tipo_shader;
    if (!preprocessar(c)) goto fim;
    c->p = c->pp; c->linha = 1;
    c->escopo[c->nescopo++] = NULL;
    embutidas_globais(c);
    lex(c);
    topo(c);
    if (!c->falhou) gerar(c);
fim:
    if (c->falhou) {
        copia_texto(erro, c->erro, (size_t)max_erro);
        glsl_liberar(p); p = NULL;
    } else erro[0] = 0;
    alivre(&c->ar);
    libera(c);
    return p;
}
