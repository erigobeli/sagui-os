/* js.c - motor de JavaScript do Cipó.
 *
 * Árvore sintática + interpretador, com objetos, protótipos, closures, classes,
 * arrow functions, desestruturação, exceções, expressões regulares e um DOM de
 * verdade montado do HTML. O que dá para escrever em JavaScript mora em
 * js_base.h, que roda no começo de cada página.
 *
 * Números: IEEE 754 de dupla precisão (js_num.h). Textos: UTF-8 por dentro,
 * medidos em unidades UTF-16. Limites: não há coletor (a memória some com a
 * página), não há BigInt nem módulos. */
#include "sagui.h"
#include "js_base.h"

typedef long long i64;
typedef unsigned long long u64;
#define MEM_MAX (160u * 1024 * 1024)
#define PILHA_TAM (4u * 1024 * 1024)

/* Texto do JS: guardado em UTF-8 (WTF-8: metades de par soltas em 3 bytes),
 * medido em unidades UTF-16 como a linguagem manda. n = bytes, u = unidades;
 * ascii = 1 quando byte e unidade coincidem. cu/cb: cache do último índice. */
typedef struct str { int n; unsigned h; struct str *in; short kw, dom; int u, cu, cb; uint8_t ascii; char s[]; } str;
typedef struct obj obj;
enum { U, NUL, BO, NU, ST, OB, AC, BI, SY };   /* BI: BigInt (js_big.h); SY: Symbol (s = chave única "@@...") */
typedef struct bint bint;
typedef struct { int t; union { double n; str *s; obj *o; int b; bint *g; }; } val;
typedef struct { str *k; val v; uint8_t at; } prop;   /* at: atributos negados (0 = gravável, enumerável, configurável) */
enum { A_NW = 1, A_NE = 2, A_NC = 4 };
typedef struct no no;
typedef struct env env;
typedef val (*nat)(val este, val *a, int n);
enum { C_OBJ, C_ARR, C_FN, C_EL, C_BUF, C_TA, C_PX };   /* C_PX: Proxy */
typedef struct dn dn;
struct obj {
    int cl; obj *proto; prop *p; int np, cp; int *idx, cidx;
    val *a; int na, ca;
    no *fn; env *e; nat f; struct quadro *q; obj *casa; val ligado_este; obj *ligado; val *ligados; int nlig;
    no *campos; dn *d;
    uint8_t *mem; int mem_n;            /* ArrayBuffer */
    struct obj *vbuf; int voff, vlen, vtipo;   /* typed array: buffer, deslocamento, elementos, tipo */
    uint8_t estilo;                     /* objeto el.style: escrever nele muda a página */
    uint8_t reino;                      /* janela (página ou iframe) onde nasceu: funções rodam nela */
    void **pc;                          /* Error: pilha capturada (n, depois pares função/trecho) */
    struct obj *px_a, *px_h;            /* Proxy: alvo e tratador (tratador NULL = revogado) */
    uint8_t trava;                      /* 1 não extensível, 2 selado, 3 congelado */
};
typedef struct quadro { val este; obj *func; val novo; } quadro;
struct env { env *pai; int n, c; str **k; val *v; uint8_t *cst; };   /* cst: 1 = const (NULL = nenhuma) */
typedef struct fdef { str *nome; no *params; no *corpo; str **vars; int nv, cv; int seta, expr, usa_args, assinc, gerador, estrito; const char *src; int nsrc; } fdef;   /* src: texto (Function.prototype.toString) */
struct no { short k, op; no *a, *b, *c, *d, *prox; str *s; double n; fdef *f; const char *fonte; };

enum { K_NUM, K_STR, K_TPL, K_ID, K_THIS, K_SUPER, K_NULL, K_TRUE, K_FALSE, K_RE, K_ARR, K_OBJ, K_PROP, K_FUNC, K_CLASS,
       K_SEQ, K_COND, K_BIN, K_AND, K_OR, K_NN, K_UN, K_UPD, K_ASG, K_MEM, K_IDX, K_CALL, K_NEW, K_SPREAD, K_AWAIT, K_TIPO, K_DEL, K_NEWT, K_YIELD, K_BIG, K_TPLSTR,
       S_VAR, S_DECL, S_EXP, S_BLOCK, S_IF, S_FOR, S_FORIN, S_WHILE, S_DO, S_RET, S_BRK, S_CONT, S_THROW, S_TRY, S_SWITCH, S_CASE, S_LABEL, S_FUNC, S_CLASS, S_VAZIO };
enum { KW_NADA, KW_VAR, KW_LET, KW_CONST, KW_FUNCTION, KW_RETURN, KW_IF, KW_ELSE, KW_FOR, KW_WHILE, KW_DO, KW_BREAK, KW_CONTINUE, KW_NEW, KW_DELETE, KW_TYPEOF,
       KW_VOID, KW_THIS, KW_NULL, KW_TRUE, KW_FALSE, KW_IN, KW_INSTANCEOF, KW_THROW, KW_TRY, KW_CATCH, KW_FINALLY, KW_SWITCH, KW_CASE, KW_DEFAULT, KW_CLASS, KW_EXTENDS,
       KW_SUPER, KW_ASYNC, KW_AWAIT, KW_OF, KW_GET, KW_SET, KW_STATIC };
static const char *const kws[] = { "", "var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "break", "continue", "new", "delete", "typeof",
    "void", "this", "null", "true", "false", "in", "instanceof", "throw", "try", "catch", "finally", "switch", "case", "default", "class", "extends",
    "super", "async", "await", "of", "get", "set", "static" };

/* ------------------------------------------------------------ memória
 * Coletor de lixo marca-e-varre conservador. Cada alocação tem um cabeçalho
 * de 4 bytes (tamanho | MARCA | LIVRE) e um bit num mapa por bloco que diz
 * onde ela começa (para achar o dono de um ponteiro que aponta para o meio).
 * Raízes: .data/.bss do programa, a parte usada de cada pilha onde o JS roda
 * (a do interpretador, a de cada corrotina e a dos ganchos externos) e os
 * registradores. O que não é alcançado vira bloco livre (vizinhos se juntam);
 * bloco de 2 MB todo livre volta ao sistema. */
#ifndef MOTOR_HOST
typedef unsigned long uintptr_t;
#endif
typedef struct bloco { struct bloco *prox; unsigned tam, usado; uint32_t *ini; } bloco;
#define BLOCO_CAB 16
#define GC_MARCA 1u
#define GC_LIVRE 2u
static bloco *blocos;
static char *ar; static unsigned ar_u, ar_c, ar_total;
static bloco *bl_atual;
static bloco *tab_bl[512]; static int n_bl;            /* blocos em ordem de endereço */
static uintptr_t gc_min = ~(uintptr_t)0, gc_max;
static uint32_t *livre_peq[129]; static uint32_t *livre_grande;
static unsigned bytes_blocos, gc_limiar = 24u << 20, gc_coletas, gc_ultimo_vivo;
static int gc_proibido;
static const js_hooks_t *gancho;
static void lancar_fatal(const char *m);
static void gc_coletar(void);

static unsigned t_suave;   /* limite que vira exceção comum (para testar funções desconhecidas) */
static unsigned cont_novo;
static val vT(const char *s);
static __attribute__((noreturn)) void lancar(val v);
/* limite de __tentar: lança um texto simples (construir um Error rodaria JS e checaria o limite de novo) */
static int no_limite;
static __attribute__((noreturn, noinline)) void limite_suave(void) { no_limite = 1; val v = vT("limite de tempo"); no_limite = 0; lancar(v); }
static __attribute__((noreturn)) void erro(const char *tipo, const char *m, const char *x);

static void bit_por(bloco *b, uint32_t *h, int v)
{
    unsigned w = (unsigned)(h - (uint32_t *)b);
    if (v) b->ini[w >> 5] |= 1u << (w & 31); else b->ini[w >> 5] &= ~(1u << (w & 31));
}
static bloco *bloco_de(uintptr_t p)
{
    if (p < gc_min || p >= gc_max) return NULL;
    int a = 0, z = n_bl - 1;
    while (a <= z) {
        int m = (a + z) / 2;
        bloco *b = tab_bl[m];
        if (p < (uintptr_t)b) z = m - 1;
        else if (p >= (uintptr_t)b + b->tam) a = m + 1;
        else return b;
    }
    return NULL;
}
static void livre_por(uint32_t *h)
{
    unsigned n = *h & ~3u;
    if (n <= 512) { h[1] = (uint32_t)(uintptr_t)livre_peq[n >> 2]; livre_peq[n >> 2] = h; }
    else { h[1] = (uint32_t)(uintptr_t)livre_grande; livre_grande = h; }
}
static uint32_t *livre_tirar(unsigned n)
{
    if (n <= 512 && livre_peq[n >> 2]) { uint32_t *h = livre_peq[n >> 2]; livre_peq[n >> 2] = (uint32_t *)(uintptr_t)h[1]; return h; }
    for (uint32_t **pp = &livre_grande; *pp; pp = (uint32_t **)(void *)&(*pp)[1]) {
        uint32_t *h = *pp, t = *h & ~3u;
        if (t < n) continue;
        *pp = (uint32_t *)(uintptr_t)h[1];
        if (t - n >= 20) {   /* sobra vira outro livre */
            uint32_t *r = h + 1 + n / 4;
            *r = (t - n - 4) | GC_LIVRE;
            bit_por(bloco_de((uintptr_t)h), r, 1);
            livre_por(r);
            *h = n;
        }
        return h;
    }
    return NULL;
}
static bloco *bloco_novo(unsigned t)
{
    if (n_bl >= 512) return NULL;
    bloco *b = aloca(t);
    if (!b) return NULL;
    uint32_t *ini = aloca(t / 32 + 8);
    if (!ini) { libera(b); return NULL; }
    memset(b, 0, t); memset(ini, 0, t / 32 + 8);
    b->tam = t; b->usado = BLOCO_CAB; b->ini = ini;
    b->prox = blocos; blocos = b;
    int i = n_bl;
    while (i > 0 && (uintptr_t)tab_bl[i - 1] > (uintptr_t)b) { tab_bl[i] = tab_bl[i - 1]; i--; }
    tab_bl[i] = b; n_bl++;
    if ((uintptr_t)b < gc_min) gc_min = (uintptr_t)b;
    if ((uintptr_t)b + t > gc_max) gc_max = (uintptr_t)b + t;
    bytes_blocos += t; ar_total += t;
    return b;
}
static void *novo(unsigned n)
{
    if (t_suave && !no_limite && !(++cont_novo & 0x3FFF) && tempo_ms() > t_suave) limite_suave();
    n = (n + 3) & ~3u;
    if (n < 4) n = 4;
    uint32_t *h = livre_tirar(n);
    if (h) {
        unsigned t = *h & ~3u;
        *h = t;
        memset(h + 1, 0, t);
        return h + 1;
    }
    if (n + 4 > ar_c - ar_u) {
        unsigned t = n + 4 + BLOCO_CAB > (2u << 20) ? ((n + 4 + BLOCO_CAB + 4095) & ~4095u) : (2u << 20);
        if (bytes_blocos + t > gc_limiar && !gc_proibido) {
            gc_coletar();
            h = livre_tirar(n);
            if (h) { unsigned tt = *h & ~3u; *h = tt; memset(h + 1, 0, tt); return h + 1; }
        }
        if (ar_total + t > MEM_MAX) lancar_fatal("memoria");
        bloco *b = bloco_novo(t);
        if (!b) lancar_fatal("memoria");
        if (t > (2u << 20)) {   /* objeto grande: bloco só dele */
            h = (uint32_t *)((char *)b + BLOCO_CAB);
            *h = n; bit_por(b, h, 1); b->usado = BLOCO_CAB + 4 + n;
            return h + 1;
        }
        /* o resto do bloco anterior vira livre */
        if (bl_atual && ar_c - ar_u >= 8) {
            uint32_t *r = (uint32_t *)(ar + ar_u);
            *r = (ar_c - ar_u - 4) | GC_LIVRE; bit_por(bl_atual, r, 1); livre_por(r);
            bl_atual->usado = ar_c;
        }
        bl_atual = b; ar = (char *)b; ar_u = BLOCO_CAB; ar_c = t;
    }
    h = (uint32_t *)(ar + ar_u);
    *h = n; bit_por(bl_atual, h, 1);
    ar_u += n + 4; bl_atual->usado = ar_u;
    return h + 1;
}

/* ------------------------------------------------------------ números */
static u64 udiv(u64 a, u64 b, u64 *resto)
{
    if (!(a >> 32) && !(b >> 32)) { unsigned x = (unsigned)a, y = (unsigned)b; if (resto) *resto = x % y; return x / y; }
    u64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) { r = (r << 1) | ((a >> i) & 1); q <<= 1; if (r >= b) { r -= b; q |= 1; } }
    if (resto) *resto = r;
    return q;
}
#include "js_num.h"
/* ToInt32 (sem divisão de 64 bits): parte inteira módulo 2^32 */
static int para_i32(double d)
{
    if (!d_fin(d) || d == 0) return 0;
    u64 b = d_bits(d); int ex = (int)((b >> 52) & 0x7FF) - 1075;
    u64 m = (b & (((u64)1 << 52) - 1)) | ((u64)1 << 52);
    uint32_t v;
    if (ex >= 32) v = 0;
    else if (ex >= 0) v = (uint32_t)(m << ex);
    else if (ex > -53) v = (uint32_t)(m >> -ex);
    else v = 0;
    return (int)((b >> 63) ? (uint32_t)(0u - v) : v);
}
static double ler_numero(const char *s, int n, int *usado) { return ler_decimal(s, n, usado); }

/* ------------------------------------------------------------ textos */
static str **tab; static int tab_c, tab_n;
/* bytes e unidades UTF-16 do caractere em p (byte inválido: 1 byte, 1 unidade) */
static inline int u8_tam(const char *p, int resto, int *unid)
{
    uint8_t c = (uint8_t)p[0];
    int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    if (k > resto) k = 1;
    for (int i = 1; i < k; i++) if (((uint8_t)p[i] & 0xC0) != 0x80) { k = 1; break; }
    *unid = k == 4 ? 2 : 1;
    return k;
}
static void medir_str(str *r)
{
    int u = 0, a = 1;
    for (int i = 0; i < r->n;) {
        if ((uint8_t)r->s[i] < 0x80) { i++; u++; continue; }
        a = 0;
        int un; i += u8_tam(r->s + i, r->n - i, &un); u += un;
    }
    r->u = u; r->ascii = (uint8_t)a; r->cu = r->cb = 0;
}
static str *novo_str(const char *s, int n)
{
    str *r = novo(sizeof(str) + (unsigned)n + 1);
    if (s) memcpy(r->s, s, (size_t)n);
    r->n = n; r->s[n] = 0;
    if (s) medir_str(r); else { r->u = n; r->ascii = 1; r->cu = r->cb = 0; }
    return r;
}
/* unidade UTF-16 -> byte (no começo do caractere que a contém); *meio = 1 se cai na 2a metade de um par */
static int u_para_b(str *s, int u, int *meio)
{
    *meio = 0;
    if (u <= 0) return 0;
    if (u >= s->u) return s->n;
    if (s->ascii) return u;
    int cu = 0, cb = 0;
    if (s->cu <= u) { cu = s->cu; cb = s->cb; }
    while (cu < u && cb < s->n) {
        int un, k = u8_tam(s->s + cb, s->n - cb, &un);
        if (cu + un > u) { *meio = 1; break; }
        cu += un; cb += k;
    }
    if (!*meio) { s->cu = cu; s->cb = cb; }
    return cb;
}
static int b_para_u(str *s, int b)
{
    if (s->ascii) return b;
    int cu = 0, cb = 0;
    if (s->cb <= b) { cu = s->cu; cb = s->cb; }
    while (cb < b && cb < s->n) { int un; cb += u8_tam(s->s + cb, s->n - cb, &un); cu += un; }
    s->cu = cu; s->cb = cb;
    return cu;
}
/* código do caractere no byte b (WTF-8) */
static uint32_t cp_em(const char *p, int resto, int *k)
{
    int un; *k = u8_tam(p, resto, &un);
    uint8_t c = (uint8_t)p[0];
    if (*k == 1) return c;
    uint32_t v = *k == 2 ? (c & 0x1Fu) : *k == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (int i = 1; i < *k; i++) v = v << 6 | ((uint8_t)p[i] & 0x3Fu);
    return v;
}
/* unidade UTF-16 no índice u (-1 fora) */
static int unidade(str *s, int u)
{
    if (u < 0 || u >= s->u) return -1;
    if (s->ascii) return (uint8_t)s->s[u];
    int meio, b = u_para_b(s, u, &meio), k;
    uint32_t c = cp_em(s->s + b, s->n - b, &k);
    if (c >= 0x10000) { c -= 0x10000; return meio ? (int)(0xDC00 + (c & 0x3FF)) : (int)(0xD800 + (c >> 10)); }
    return (int)c;
}
/* escreve um código (ou metade de par) em WTF-8 */
static int wtf8_por(char *d, uint32_t c)
{
    if (c < 0x80) { d[0] = (char)c; return 1; }
    if (c < 0x800) { d[0] = (char)(0xC0 | c >> 6); d[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { d[0] = (char)(0xE0 | c >> 12); d[1] = (char)(0x80 | ((c >> 6) & 63)); d[2] = (char)(0x80 | (c & 63)); return 3; }
    d[0] = (char)(0xF0 | c >> 18); d[1] = (char)(0x80 | ((c >> 12) & 63)); d[2] = (char)(0x80 | ((c >> 6) & 63)); d[3] = (char)(0x80 | (c & 63)); return 4;
}
/* fatia [u0, u1) em unidades; metades de par viram surrogates soltos */
static str *fatia_u(str *s, int u0, int u1)
{
    if (u0 < 0) u0 = 0;
    if (u1 > s->u) u1 = s->u;
    if (u1 <= u0) return novo_str("", 0);
    if (s->ascii) return novo_str(s->s + u0, u1 - u0);
    int m0, m1, b0 = u_para_b(s, u0, &m0), b1 = u_para_b(s, u1, &m1);
    str *r = novo_str(NULL, (b1 - b0) + 8);
    int k = 0, i = b0;
    if (m0) { int kk; uint32_t c = cp_em(s->s + b0, s->n - b0, &kk) - 0x10000; k += wtf8_por(r->s, 0xDC00 + (c & 0x3FF)); i += kk; }
    if (i < b1) { memcpy(r->s + k, s->s + i, (size_t)(b1 - i)); k += b1 - i; }
    if (m1) { int kk; uint32_t c = cp_em(s->s + b1, s->n - b1, &kk) - 0x10000; k += wtf8_por(r->s + k, 0xD800 + (c >> 10)); }
    r->n = k; r->s[k] = 0;
    medir_str(r);
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
static val vN(double n) { val v; v.t = NU; v.n = n; return v; }
static val vI(int n) { return vN((double)n); }
static val vS(str *s) { val v; v.t = ST; v.n = 0; v.s = s; return v; }
static val vY(str *s) { val v; v.t = SY; v.n = 0; v.s = s; return v; }
static val vT(const char *s) { return vS(novo_str(s, (int)tam_texto(s))); }
static val vO(obj *o) { val v; v.t = OB; v.n = 0; v.o = o; return v; }

static obj *global, *p_obj, *p_fn, *p_arr, *p_str, *p_num, *p_bool, *p_el, *p_big, *p_sym;
static int reino_atual;   /* 0 = a página; 1.. = iframes (js_quadros.h) */
static quadro q_global, *qa;
typedef struct salto { void *b[5]; struct salto *ant; } salto;
static salto *topo_s;
static val exc; static int fatal, prof;
/* pilha de chamadas para Error.stack: prof é a profundidade (os saltos já a restauram) */
#define PILHA_CH 256
static struct { obj *f; no *site; } pilha_ch[PILHA_CH];
static char *lim_pilha;
static int gerador_entrando;
static val produzir(val v, int delega);
static unsigned t_limite; static unsigned passos;

static __attribute__((noinline, noreturn)) void lancar(val v) { exc = v; __builtin_longjmp(topo_s->b, 1); }
static void lancar_fatal(const char *m) { registrar("js: abortado: %s\n", m); fatal = 1; exc = vU(); __builtin_longjmp(topo_s->b, 1); }
static val chamar(val f, val este, val *a, int n);
static val chamar_reino(val f, val este, val *a, int n, val novo_t);
static val construir(val f, val *a, int n);
static val get(val b, str *k);
static no *no_atual;   /* último acesso a membro: dá o lugar do código no Error.stack */
/* Textos já lidos pelo parser: um trecho "antes" de um nó nunca sai do texto dele. */
static struct { const char *ini, *fim; } fontes_lidas[64]; static int n_fontes_lidas;
static void fonte_lida(const char *s, int n) { int i = n_fontes_lidas++ & 63; fontes_lidas[i].ini = s; fontes_lidas[i].fim = s + n; }
static const char *antes_de(const char *f, int volta)
{
    for (int j = 1; j <= 64; j++) {   /* do mais novo ao mais velho */
        int i = (n_fontes_lidas - j) & 63;
        if (fontes_lidas[i].ini && f >= fontes_lidas[i].ini && f <= fontes_lidas[i].fim)
            return f - fontes_lidas[i].ini < volta ? fontes_lidas[i].ini : f - volta;
    }
    return f;
}
static __attribute__((noreturn)) void erro(const char *tipo, const char *m, const char *x)
{
    char b[240];
    formatar_texto(b, sizeof b, "%s%s", m, x ? x : "");
    val a = vT(b), c = get(vO(global), I(tipo));
    if (c.t != OB) { exc = a; lancar(a); }
    lancar(construir(c, &a, 1));
}

static obj *novo_obj(obj *proto) { obj *o = novo(sizeof(obj)); o->proto = proto; o->reino = (uint8_t)reino_atual; return o; }
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
static unsigned dom_ger;   /* sobe a cada mudança do DOM ou de um el.style (layout em cache vence) */
static void (*ds_escrever)(obj *o, str *k, val v);   /* el.dataset.x = v vira atributo data-x */
static void por_prop(obj *o, str *k, val v)
{
    if (o->estilo == 2 && ds_escrever) ds_escrever(o, k, v);
    else if (o->estilo) dom_ger++;
    prop *p = achar(o, k);
    if (p) { p->v = v; return; }
    if (o->np == o->cp) {
        int c = o->cp ? o->cp * 2 : 4;
        prop *n = novo((unsigned)c * sizeof(prop));
        if (o->np) memcpy(n, o->p, (size_t)o->np * sizeof(prop));
        o->p = n; o->cp = c;
        if (c >= 16) reindexar(o);
    }
    o->p[o->np].k = k; o->p[o->np].v = v; o->p[o->np].at = 0; o->np++;
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
static val prim_h(val v, int texto);
static str *bi_texto(const bint *a, int base);
static int bi_nao_zero(const bint *a);
static int bi_cmp_v(val a, val b);   /* js_big.h: -1/0/1, 2 = incomparável */
static str *num_str(double n)
{
    char b[40];
    int k = d_para_texto(n, b);
    return novo_str(b, k);
}
static str *to_str(val v)
{
    switch (v.t) {
    case U: return I("undefined");
    case NUL: return I("null");
    case BO: return I(v.b ? "true" : "false");
    case NU: return num_str(v.n);
    case ST: return v.s;
    case BI: return bi_texto(v.g, 10);
    case SY: erro("TypeError", "Cannot convert a Symbol value to a string", NULL);
    default: return to_str(prim_h(v, 1));
    }
}
/* ToNumber de um texto: espaços do JS em volta, 0x/0o/0b, Infinity, decimal */
static int eh_branco_js(uint32_t c);
static double str_num(str *s)
{
    int i = 0, n = s->n, k;
    while (i < n) { uint32_t c = cp_em(s->s + i, n - i, &k); if (!eh_branco_js(c) && c != 0x2028 && c != 0x2029) break; i += k; }
    while (n > i) { int b = n - 1; while (b > i && ((uint8_t)s->s[b] & 0xC0) == 0x80) b--; uint32_t c = cp_em(s->s + b, n - b, &k); if (!eh_branco_js(c) && c != 0x2028 && c != 0x2029) break; n = b; }
    if (i == n) return 0;
    if (s->s[i] == '0' && i + 2 < n + 1 && i + 1 < n && ((s->s[i + 1] | 32) == 'x' || (s->s[i + 1] | 32) == 'o' || (s->s[i + 1] | 32) == 'b')) {
        int base = (s->s[i + 1] | 32) == 'x' ? 16 : (s->s[i + 1] | 32) == 'o' ? 8 : 2, u;
        double r = ler_base(s->s + i + 2, n - i - 2, base, &u);
        return u == n - i - 2 ? r : D_NAN;
    }
    int neg = 0, u;
    if (s->s[i] == '-' || s->s[i] == '+') neg = s->s[i++] == '-';
    if (n - i == 8 && !memcmp(s->s + i, "Infinity", 8)) return neg ? -D_INF : D_INF;
    double r = ler_decimal(s->s + i, n - i, &u);
    if (!u || u != n - i) return D_NAN;
    return neg ? -r : r;
}
static double to_num(val v)
{
    switch (v.t) {
    case U: return D_NAN;
    case NUL: return 0;
    case BO: return v.b;
    case NU: return v.n;
    case ST: return str_num(v.s);
    case BI: erro("TypeError", "Cannot convert a BigInt value to a number", NULL);
    case SY: erro("TypeError", "Cannot convert a Symbol value to a number", NULL);
    default: return to_num(prim(v));
    }
}
static int to_bool(val v)
{
    switch (v.t) {
    case U: case NUL: return 0;
    case BO: return v.b;
    case NU: return v.n != 0 && !d_nan(v.n);
    case ST: return v.s->n != 0;
    case BI: return bi_nao_zero(v.g);
    default: return 1;
    }
}
static int to_i(val v) { return para_i32(to_num(v)); }
/* ToPrimitive: Symbol.toPrimitive com a dica; senão valueOf/toString (toString primeiro na dica "string") */
static val prim_h(val v, int texto)
{
    if (v.t != OB) return v;
    val tp = get(v, I("@@toPrimitive"));
    if (tp.t != U && tp.t != NUL) {
        val h = vT(texto == 1 ? "string" : texto == 2 ? "number" : "default");
        val r = chamar(tp, v, &h, 1);
        if (r.t == OB) erro("TypeError", "Cannot convert object to primitive value", NULL);
        return r;
    }
    static const char *const ms[2] = { "valueOf", "toString" };
    for (int j = 0; j < 2; j++) {
        int i = texto == 1 ? 1 - j : j;
        val f = get(v, I(ms[i]));
        if (f.t == OB && f.o->cl == C_FN) { val r = chamar(f, v, NULL, 0); if (r.t != OB) return r; }
    }
    erro("TypeError", "Cannot convert object to primitive value", NULL);
}
static val prim(val v) { return prim_h(v, 0); }
static val concat(str *a, str *b)
{
    str *r = novo_str(NULL, a->n + b->n);
    memcpy(r->s, a->s, (size_t)a->n);
    /* metade alta no fim de a + metade baixa no começo de b = um caractere só */
    if (a->n >= 3 && b->n >= 3 && (uint8_t)a->s[a->n - 3] == 0xED && ((uint8_t)a->s[a->n - 2] & 0xF0) == 0xA0 &&
        (uint8_t)b->s[0] == 0xED && ((uint8_t)b->s[1] & 0xF0) == 0xB0) {
        uint32_t hi = 0xD000 | ((uint32_t)(a->s[a->n - 2] & 63) << 6) | (uint32_t)(a->s[a->n - 1] & 63);
        uint32_t lo = 0xD000 | ((uint32_t)(b->s[1] & 63) << 6) | (uint32_t)(b->s[2] & 63);
        int k = a->n - 3;
        k += wtf8_por(r->s + k, 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00));
        memcpy(r->s + k, b->s + 3, (size_t)(b->n - 3));
        r->n = k + b->n - 3; r->s[r->n] = 0;
        medir_str(r);
        return vS(r);
    }
    memcpy(r->s + a->n, b->s, (size_t)b->n);
    r->s[r->n] = 0;
    r->u = a->u + b->u; r->ascii = a->ascii && b->ascii; r->cu = r->cb = 0;
    return vS(r);
}
static int str_igual(str *a, str *b) { return a == b || (a->n == b->n && !memcmp(a->s, b->s, (size_t)a->n)); }
static int estrito(val a, val b)
{
    if (a.t != b.t) return 0;
    switch (a.t) {
    case U: case NUL: return 1;
    case BO: return a.b == b.b;
    case NU: return a.n == b.n;
    case ST: return str_igual(a.s, b.s);
    case BI: return bi_cmp_v(a, b) == 0;
    case SY: return a.s == b.s;
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
    if (a.t == SY || b.t == SY) return 0;
    if (a.t == BI || b.t == BI) return bi_cmp_v(a, b) == 0;
    double x = to_num(a), y = to_num(b);
    return x == y;
}
static int indice(str *k)
{
    if (!k->n || k->n > 9 || (k->s[0] == '0' && k->n > 1)) return -1;
    int r = 0;
    for (int i = 0; i < k->n; i++) { if (k->s[i] < '0' || k->s[i] > '9') return -1; r = r * 10 + k->s[i] - '0'; }
    return r;
}

/* ------------------------------------------------------------ propriedades */
static int modo_estrito(void);
static int el_get(obj *o, str *k, val *r);
static int el_set(obj *o, str *k, val v);
static str *s_length, *s_prototype, *s_constructor, *s_arguments, *s_name;

/* typed arrays: 0 Int8, 1 Uint8, 2 Uint8Clamped, 3 Int16, 4 Uint16, 5 Int32, 6 Uint32, 7 Float32, 8 Float64,
   9 BigInt64, 10 BigUint64 */
static const int ta_tam[11] = { 1, 1, 1, 2, 2, 4, 4, 4, 8, 8, 8 };
static void bi_c2(const bint *a, uint16_t *o, int n);
static bint *bi_de_c2(const uint16_t *o, int n);
static bint *bi_norm(bint *b);
static bint *para_big(val v);
static val vG(bint *g);
static double to_num(val v);
static int para_i32(double n);
static val vN(double n);
static val ta_ler(obj *o, int i)
{
    uint8_t *p = o->vbuf->mem + o->voff + i * ta_tam[o->vtipo];
    switch (o->vtipo) {
    case 0: return vI((int8_t)p[0]);
    case 1: case 2: return vI(p[0]);
    case 3: return vI((int16_t)(p[0] | p[1] << 8));
    case 4: return vI(p[0] | p[1] << 8);
    case 5: return vI((int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24));
    case 6: { uint32_t u = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; return vN((double)u); }
    case 7: { float q; memcpy(&q, p, 4); return vN((double)q); }   /* Float32: IEEE 754 */
    case 9: case 10: {
        uint16_t d[5]; for (int k = 0; k < 4; k++) d[k] = (uint16_t)(p[2 * k] | p[2 * k + 1] << 8);
        d[4] = 0; return vG(bi_de_c2(d, o->vtipo == 9 ? 4 : 5));
    }
    default: { double q; memcpy(&q, p, 8); return vN(q); }      /* Float64 */
    }
}
static void ta_gravar(obj *o, int i, val v)
{
    uint8_t *p = o->vbuf->mem + o->voff + i * ta_tam[o->vtipo];
    if (o->vtipo >= 9) {   /* BigInt64/BigUint64: ToBigInt, módulo 2^64 */
        if (v.t == NU || v.t == U || v.t == NUL) erro("TypeError", "Cannot convert value to a BigInt", NULL);
        uint16_t d[4]; bi_c2(para_big(v), d, 4);
        for (int k = 0; k < 4; k++) { p[2 * k] = (uint8_t)d[k]; p[2 * k + 1] = (uint8_t)(d[k] >> 8); }
        return;
    }
    double n = to_num(v);
    if (o->vtipo == 2) {   /* Uint8Clamped: limita e arredonda meio para o par */
        int x;
        if (d_nan(n) || n <= 0) x = 0; else if (n >= 255) x = 255;
        else { double f = m_floor(n), r = n - f; x = (int)f; if (r > 0.5 || (r == 0.5 && (x & 1))) x++; }
        p[0] = (uint8_t)x; return;
    }
    if (o->vtipo == 7) { float q = (float)n; memcpy(p, &q, 4); return; }
    if (o->vtipo == 8) { memcpy(p, &n, 8); return; }
    uint32_t u = (uint32_t)para_i32(n);
    for (int b = 0; b < ta_tam[o->vtipo]; b++) p[b] = (uint8_t)(u >> (8 * b));
}

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
/* Proxy: devolve a armadilha `nome` do tratador (ou U); revogado dá TypeError */
static val px_arm(obj *p, const char *nome)
{
    if (!p->px_h) erro("TypeError", "Cannot perform operation on a revoked proxy", NULL);
    val h = vO(p->px_h);
    val f = get(h, I(nome));
    if (f.t == U || f.t == NUL) return vU();
    if (f.t != OB || (f.o->cl != C_FN && f.o->cl != C_PX)) erro("TypeError", "proxy trap is not a function: ", nome);
    return f;
}
static int px_chamavel(obj *o) { while (o && o->cl == C_PX) o = o->px_a; return o && o->cl == C_FN; }
static val get(val b, str *k)
{
    k = intern(k);
    switch (b.t) {
    case U: case NUL: erro("TypeError", "Cannot read properties of null: ", k->s);
    case ST:
        if (k == s_length) return vI(b.s->u);
        { int i = indice(k); if (i >= 0) return i < b.s->u ? vS(fatia_u(b.s, i, i + 1)) : vU(); }
        return get_o(p_str, k, b);
    case NU: return get_o(p_num, k, b);
    case BO: return get_o(p_bool, k, b);
    case BI: return get_o(p_big ? p_big : p_obj, k, b);
    case SY: return get_o(p_sym ? p_sym : p_obj, k, b);
    default: break;
    }
    obj *o = b.o;
    if (o->cl == C_PX) {
        val f = px_arm(o, "get");
        if (f.t == U) return get(vO(o->px_a), k);
        val ar[3] = { vO(o->px_a), vS(k), b };
        return chamar(f, vO(o->px_h), ar, 3);
    }
    if (o->cl == C_ARR) {
        if (k == s_length) return vI(o->na);
        int i = indice(k);
        if (i >= 0) return i < o->na ? o->a[i] : vU();
    } else if (o->cl == C_TA) {
        if (k == s_length) return vI(o->vlen);
        int i = indice(k);
        if (i >= 0) return i < o->vlen ? ta_ler(o, i) : vU();
        if (!achar(o, k)) {
            if (k == I("byteLength")) return vI(o->vlen * ta_tam[o->vtipo]);
            if (k == I("byteOffset")) return vI(o->voff);
            if (k == I("buffer")) return vO(o->vbuf);
        }
    } else if (o->cl == C_BUF) {
        if (k == I("byteLength") && !achar(o, k)) return vI(o->mem_n);
    } else if (o->cl == C_EL) {
        val r;
        if (!achar(o, k) && el_get(o, k, &r)) return r;
    } else if (o->cl == C_FN && (k == s_length || k == (s_name ? s_name : (s_name = I("name")))) && !achar(o, k)) {
        if (k == s_length) {
            if (o->ligado) { val l = get(vO(o->ligado), s_length); int n = (l.t == NU ? (int)l.n : 0) - o->nlig; return vI(n > 0 ? n : 0); }
            int n = 0; if (o->fn && o->fn->f) for (no *p = o->fn->f->params; p; p = p->prox) { if (p->a && (p->a->k == K_ASG || p->a->k == K_SPREAD)) break; n++; }
            return vI(n);
        }
        if (o->ligado) { val t = get(vO(o->ligado), k); str *nt = t.t == ST ? t.s : I(""); str *r = novo_str(NULL, nt->n + 6); memcpy(r->s, "bound ", 6); memcpy(r->s + 6, nt->s, (size_t)nt->n); r->s[r->n] = 0; medir_str(r); return vS(r); }
        return vS(o->fn && o->fn->f && o->fn->f->nome ? o->fn->f->nome : I(""));
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
    if (o->cl == C_PX) {
        val f = px_arm(o, "set");
        if (f.t == U) { put(vO(o->px_a), k, v); return; }
        val ar[4] = { vO(o->px_a), vS(k), v, b };
        chamar(f, vO(o->px_h), ar, 4);
        return;
    }
    if (o->trava == 3 && o->cl != C_TA) { if (modo_estrito()) erro("TypeError", "Cannot assign to read only property of frozen object: ", k->s); return; }
    if (o->cl == C_TA) {
        int i = indice(k);
        if (i >= 0) { if (i < o->vlen) ta_gravar(o, i, v); return; }
        if (k == s_length) return;
    }
    if (o->cl == C_ARR && o->trava) {
        int i = indice(k);
        if (i >= o->na || (k == s_length && to_i(v) != o->na && o->trava >= 2)) return;
    }
    if (o->cl == C_ARR) {
        if (k == s_length) { int n = to_i(v); if (n < o->na) o->na = n < 0 ? 0 : n; else while (o->na < n && n < 1000000) arr_push(o, vU()); return; }
        int i = indice(k);
        if (i >= 0 && i < 4000000) { while (o->na <= i) arr_push(o, vU()); o->a[i] = v; return; }
    } else if (o->cl == C_EL && el_set(o, k, v)) return;
    for (obj *c = o; c; c = c->proto) {
        prop *p = achar(c, k);
        if (p) {
            if (p->v.t == AC) { prop *x = achar(p->v.o, I("set")); if (x && x->v.t == OB) chamar(x->v, b, &v, 1); else if (modo_estrito()) erro("TypeError", "Cannot set property which has only a getter: ", k->s); return; }
            if (p->at & A_NW) { if (modo_estrito()) erro("TypeError", "Cannot assign to read only property: ", k->s); return; }
            break;
        }
    }
    if (o->trava && !achar(o, k)) { if (modo_estrito()) erro("TypeError", "Cannot add property, object is not extensible: ", k->s); return; }
    por_prop(o, k, v);
}
static str *chave(val k) { if (k.t == SY) return k.s; if (k.t == OB) { k = prim_h(k, 1); if (k.t == SY) return k.s; } return intern(to_str(k)); }
static val get_i(val b, val k)
{
    int i;
    if (k.t == NU && k.n >= 0 && k.n < 2147483647.0 && (double)(i = (int)k.n) == k.n) {
        if (b.t == OB && b.o->cl == C_ARR) return i < b.o->na ? b.o->a[i] : vU();
        if (b.t == ST) return i < b.s->u ? vS(fatia_u(b.s, i, i + 1)) : vU();
        if (b.t == OB && b.o->cl == C_TA) return i < b.o->vlen ? ta_ler(b.o, i) : vU();
    }
    return get(b, chave(k));
}
static void put_i(val b, val k, val v)
{
    int i;
    if (k.t == NU && k.n >= 0 && k.n < 2147483647.0 && (double)(i = (int)k.n) == k.n) {
        if (b.t == OB && b.o->cl == C_ARR && i < b.o->na) { b.o->a[i] = v; return; }
        if (b.t == OB && b.o->cl == C_TA) { if (i < b.o->vlen) ta_gravar(b.o, i, v); return; }
    }
    put(b, chave(k), v);
}

#pragma GCC push_options
#pragma GCC optimize("Os")   /* léxico e parser: tamanho (rodam uma vez por script) */
/* ------------------------------------------------------------ léxico */
enum { T_FIM = 0, T_NUM = 256, T_STR, T_TPL, T_ID, T_RE, T_BIG, T_OP = 300 };
static const char *const ops[] = { ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "?\?=", "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "**" };
enum { O_URSA, O_3P, O_SEQ, O_SNE, O_POWA, O_SHLA, O_SHRA, O_URS, O_ANDA, O_ORA, O_NNA, O_SETA, O_EQ, O_NE, O_LE, O_GE, O_AND, O_OR, O_NN, O_OPT, O_INC, O_DEC, O_ADDA, O_SUBA, O_MULA, O_DIVA, O_MODA, O_BANDA, O_BORA, O_XORA, O_SHL, O_SHR, O_POW };
#define OP(x) (T_OP + (x))
typedef struct { const char *src; int tam, p, tk, nl, ant, ini, fa; double num; str *s, *re_f; } lex_t;   /* fa: fim do token anterior */
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
        break;
    default: tb_por(k, c); return p;   /* bytes do fonte (UTF-8) passam como estão */
    }
    /* \x, \u: escreve em WTF-8; metade baixa logo após uma alta vira um caractere só */
    if (v >= 0xDC00 && v <= 0xDFFF && *k >= 3 && (uint8_t)tb[*k - 3] == 0xED && ((uint8_t)tb[*k - 2] & 0xF0) == 0xA0) {
        uint32_t hi = 0xD000 | ((uint32_t)(tb[*k - 2] & 63) << 6) | (uint32_t)(tb[*k - 1] & 63);
        *k -= 3;
        v = (int)(0x10000 + ((hi - 0xD800) << 10) + ((uint32_t)v - 0xDC00));
    }
    char w[4]; int nw = wtf8_por(w, (uint32_t)v);
    for (int i = 0; i < nw; i++) tb_por(k, w[i]);
    return p;
}
static void prox(void)
{
    const char *s = L.src; int n = L.tam, p = L.p;
    L.ant = L.tk; L.nl = 0; L.fa = p;
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
        int ini_num = p;
        if (c == '0' && p + 1 < n && ((s[p + 1] | 32) == 'x' || (s[p + 1] | 32) == 'b' || (s[p + 1] | 32) == 'o')) {
            int b = (s[p + 1] | 32) == 'x' ? 16 : (s[p + 1] | 32) == 'b' ? 2 : 8;
            p += 2;
            int u; L.num = ler_base(s + p, n - p, b, &u); p += u;
        } else {
            /* separadores numéricos (1_000) saem antes da conversão */
            int f = p; char tmp[400]; int kk = 0;
            while (f < n && kk < 398 && ((s[f] >= '0' && s[f] <= '9') || s[f] == '.' || s[f] == '_' || ((s[f] | 32) == 'e' && f + 1 < n) || ((s[f] == '+' || s[f] == '-') && f > p && (s[f - 1] | 32) == 'e'))) { if (s[f] != '_') tmp[kk++] = s[f]; f++; }
            int u; L.num = ler_numero(tmp, kk, &u);
            int consumidos = 0, q = p;
            while (consumidos < u && q < f) { if (s[q] != '_') consumidos++; q++; }
            p = q;
        }
        if (p < n && s[p] == 'n') {   /* BigInt: o texto do literal (sem os _) */
            char tb2[512]; int kb = 0;
            for (int z = ini_num; z < p && kb < 510; z++) if (s[z] != '_') tb2[kb++] = s[z];
            L.s = novo_str(tb2, kb);
            p++;
            L.tk = T_BIG; L.p = p; return;
        }
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
        int valor = a == T_NUM || a == T_BIG || a == T_STR || a == T_TPL || a == T_RE || a == ')' || a == ']' || a == '}' || (a == T_ID && !(kwv == KW_RETURN || kwv == KW_TYPEOF || kwv == KW_IN || kwv == KW_DELETE || kwv == KW_VOID || kwv == KW_THROW || kwv == KW_CASE || kwv == KW_ELSE || kwv == KW_NEW || kwv == KW_INSTANCEOF));
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
static fdef *cf; static int nfuncs, vem_async, vem_gerador, em_classe;
/* #privado: chave escondida "@@#nome\x01<classe>" (não aparece em keys; só o código da classe a escreve) */
static int classe_atual, n_classes;
static str *priv_chave(str *nome)
{
    char b[300]; int k = 0; b[k++] = '@'; b[k++] = '@'; b[k++] = '#';
    for (int i = 0; i < nome->n && k < 270; i++) b[k++] = nome->s[i];
    b[k++] = '\x01'; k += formatar_texto(b + k, sizeof b - (size_t)k, "%d", classe_atual);
    return intern(novo_str(b, k));
}
/* diretiva "use strict" no começo de um corpo (o token atual é a string) */
static int diretiva_estrita(void) { return L.tk == T_STR && L.s && L.s->n == 10 && !memcmp(L.s->s, "use strict", 10); }
static no *N(int k) { no *x = novo(sizeof(no)); x->k = (short)k; x->fonte = L.src && L.ini >= 0 && L.ini < L.tam ? L.src + L.ini : NULL; return x; }
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
        int k = 0, p0 = p;
        while (p < n && s[p] != '`' && !(s[p] == '$' && p + 1 < n && s[p + 1] == '{')) {
            if (s[p] == '\\' && p + 1 < n) p = escape(s, p + 1, n, &k);
            else tb_por(&k, s[p++]);
        }
        no *x = N(K_STR); x->s = novo_str(tb, k); lpor(&l, x);
        x->b = N(K_STR); x->b->s = novo_str(s + p0, p - p0);   /* texto cru (String.raw / tag) */
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
    case T_BIG: x = N(K_BIG); x->s = L.s; prox(); return x;
    case T_STR: x = N(K_STR); x->s = L.s; prox(); return x;
    case '#': prox(); if (L.tk != T_ID) sintaxe("nome privado"); x = N(K_STR); x->s = priv_chave(L.s); prox(); return x;   /* #x in obj */
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
            int priv = 0;
            if (L.tk == '#') { prox(); priv = 1; }
            if (L.tk != T_ID) sintaxe("nome depois do ponto");
            no *m = N2(K_MEM, x, NULL); m->s = priv ? priv_chave(L.s) : L.s; m->op = (short)opt; prox(); x = m;
        } else if (L.tk == '[') { prox(); x = N2(K_IDX, x, expr()); exige(']'); }
        else if (L.tk == '(' && !sem_call) { prox(); x = N2(K_CALL, x, argumentos()); }
        else if (L.tk == T_TPL) {   /* tag`...`: tag(strings, ...valores) */
            no *t = modelo(); lista a = { 0, 0 };
            no *h = N(K_SEQ); h->a = N(K_TPLSTR); h->a->a = t; lpor(&a, h);
            for (no *c = t->a; c; c = c->prox) if (c->k != K_STR) { no *v = N(K_SEQ); v->a = c->a; lpor(&a, v); }
            x = N2(K_CALL, x, a.pri);
        }
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
/* Começo do texto de uma função para Function.prototype.toString: o parser
 * chega aqui já depois de "function nome" (ou do nome do método, ou do "(" da
 * seta); volta pelo código até o começo da declaração. */
static int id_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || (uint8_t)c >= 0x80; }
static const char *volta_ws(const char *q, const char *ini) { while (q > ini && (uint8_t)q[-1] <= ' ') q--; return q; }
static const char *volta_palavra(const char *q, const char *ini, const char *w)
{
    int n = (int)tam_texto(w); const char *r = volta_ws(q, ini);
    if (r - ini >= n && !memcmp(r - n, w, (size_t)n) && (r - n == ini || !id_char(r[-n - 1]))) return r - n;
    return NULL;
}
static const char *inicio_funcao(const char *f, int seta, int metodo)
{
    const char *ini = L.src, *q = f, *r;
    if (seta == 1) { q = volta_ws(q, ini); if (q > ini && q[-1] == '(') q--; if ((r = volta_palavra(q, ini, "async"))) q = r; return q; }
    if (seta == 3) {   /* x => já lido: volta pela seta e pelo nome */
        q = volta_ws(q, ini); if (q - ini >= 2 && q[-2] == '=' && q[-1] == '>') q -= 2;
        q = volta_ws(q, ini); while (q > ini && id_char(q[-1])) q--;
    }
    if (seta) { if ((r = volta_palavra(q, ini, "async"))) q = r; return q; }
    /* f aponta para o "(" dos parâmetros: antes vem o nome (opcional), "*", "function" / get / set / static / async */
    q = volta_ws(q, ini);
    if (q > ini && (q[-1] == '"' || q[-1] == '\'')) { char c = q[-1]; q--; while (q > ini && q[-1] != c) q--; if (q > ini) q--; }
    else if (q > ini && q[-1] == ']') { int prof_ = 0; while (q > ini) { q--; if (*q == ']') prof_++; else if (*q == '[' && --prof_ == 0) break; } }
    else while (q > ini && id_char(q[-1])) q--;
    if (q > ini && q[-1] == '#') q--;
    for (;;) {
        const char *s0 = volta_ws(q, ini);
        if (s0 > ini && s0[-1] == '*') { q = s0 - 1; continue; }
        if ((r = volta_palavra(q, ini, "function"))) { q = r; if ((r = volta_palavra(q, ini, "async"))) q = r; return q; }
        if (metodo && ((r = volta_palavra(q, ini, "get")) || (r = volta_palavra(q, ini, "set")) || (r = volta_palavra(q, ini, "async")))) { q = r; continue; }
        return q;
    }
}
static no *funcao(str *nome, int metodo, int seta)
{
    (void)metodo;
    no *f = N(K_FUNC); fdef *d = novo(sizeof(fdef)), *ant = cf;
    f->f = d; d->nome = nome; d->seta = !!seta; d->assinc = vem_async; vem_async = 0; d->gerador = vem_gerador; vem_gerador = 0;
    d->estrito = (ant && ant->estrito) || em_classe;
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
    else { exige('{'); if (diretiva_estrita()) d->estrito = 1; d->corpo = bloco_corpo(); }
    if (f->fonte && L.src && L.fa > f->fonte - L.src && L.fa <= L.tam) { d->src = inicio_funcao(f->fonte, seta, metodo); d->nsrc = (int)(L.src + L.fa - d->src); }
    cf = ant;
    if (ant && d->seta && d->usa_args) ant->usa_args = 1;
    return f;
}
static no *classe_(void);
static no *classe__(void);
static no *classe(void) { em_classe++; no *c = classe_(); em_classe--; return c; }   /* corpo de classe é sempre estrito */
static no *funcao(str *nome, int metodo, int seta);
static no *classe_(void)
{
    int cl_ant = classe_atual; classe_atual = ++n_classes;
    no *r = classe__();
    classe_atual = cl_ant;
    return r;
}
static no *classe__(void)
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
        if (est && L.tk == '{') {   /* static { ... }: roda uma vez com this = a classe */
            no *f = N(K_FUNC); fdef *d = novo(sizeof(fdef)), *ant = cf;
            f->f = d; d->estrito = 1; nfuncs++; cf = d;
            prox(); d->corpo = bloco_corpo();
            cf = ant;
            m->op = 5; m->n = 1; m->b = f; m->s = I("");
            lpor(&l, m); continue;
        }
        int priv = 0;
        if (L.tk == '#') { prox(); priv = 1; }
        if (aceita('*')) vem_gerador = 1;
        chave_prop(m);
        if (priv) m->s = priv_chave(m->s);
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
    fonte_lida(s, n);
    em_classe = 0;   /* erro de sintaxe dentro de uma classe sai por longjmp sem descer o contador */
    lex_t sv = L; fdef *ant = cf;
    memset(&L, 0, sizeof L);
    L.src = s; L.tam = n; L.tk = ';';
    cf = d;
    prox();
    if (diretiva_estrita()) d->estrito = 1;
    no *b = N(S_BLOCK); lista l = { 0, 0 };
    while (L.tk != T_FIM) lpor(&l, stmt());
    b->a = l.pri;
    cf = ant; L = sv;
    return b;
}

#pragma GCC pop_options
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
        if (e->cst) { uint8_t *nc = novo((unsigned)c); memcpy(nc, e->cst, (size_t)e->n); e->cst = nc; }
        e->k = nk; e->v = nv; e->c = c;
    }
    if (e->cst) e->cst[e->n] = 0;
    e->k[e->n] = k; e->v[e->n++] = v;
}
static void decl_const(env *e, str *k, val v)
{
    decl(e, k, v);
    if (!e) return;
    for (int i = 0; i < e->n; i++) if (e->k[i] == k) {
        if (!e->cst) { e->cst = novo((unsigned)e->c); memset(e->cst, 0, (size_t)e->c); }
        e->cst[i] = 1; return;
    }
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
static int prog_estrito;   /* o script global que está rodando tem "use strict" */
static int modo_estrito(void)
{
    if (qa && qa != &q_global && qa->func && qa->func->fn && qa->func->fn->f) return qa->func->fn->f->estrito;
    return prog_estrito;
}
static void var_set(env *e, str *k, val v)
{
    for (env *f = e; f; f = f->pai) for (int i = 0; i < f->n; i++) if (f->k[i] == k) {
        if (f->cst && f->cst[i]) erro("TypeError", "Assignment to constant variable.", NULL);
        f->v[i] = v; return;
    }
    if (modo_estrito()) { int tem = 0; for (obj *c = global; c; c = c->proto) if (achar(c, k)) { tem = 1; break; } if (!tem) erro("ReferenceError", k->s, " is not defined"); }
    put(vO(global), k, v);
}
static val nova_funcao(no *x, env *e)
{
    obj *o = novo_obj(p_fn);
    o->cl = C_FN; o->fn = x; o->e = e;
    /* arrow: this/new.target de quem a criou. Cópia, não ponteiro: o quadro de
     * quem criou mora na pilha C e morre antes de uma arrow chamada depois
     * (timer, evento, corrotina async) */
    if (x->f->seta) { quadro *c = novo(sizeof(quadro)); *c = *qa; o->q = c; }
    return vO(o);
}
static void hoist(no *l, env *e) { for (; l; l = l->prox) if (l->k == S_FUNC) decl(e, l->s, nova_funcao(l->a, e)); }
static void ligar(no *p, val v, env *e, int d);
static void ligar_id(no *p, val v, env *e, int d) { if (d == 2) decl_const(e, p->s, v); else if (d) decl(e, p->s, v); else var_set(e, p->s, v); }
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
static val get_chaves_proprias(val v) { val f = get(vO(global), I("Object")); f = get(f, I("keys")); return chamar(f, vU(), &v, 1); }
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
        if (v.t == U || v.t == NUL) erro("TypeError", "Cannot destructure 'undefined' or 'null'", NULL);
        str *usadas[64]; int nu = 0;
        for (no *h = p->a; h; h = h->prox) {
            if (h->op == 3) {   /* ...resto: as próprias enumeráveis que sobraram */
                obj *r = novo_obj(p_obj);
                if (v.t == OB) {
                    val ks = get_chaves_proprias(v);
                    for (int i = 0; i < ks.o->na; i++) {
                        str *k = intern(ks.o->a[i].s); int fora = 0;
                        for (int j = 0; j < nu; j++) if (usadas[j] == k) { fora = 1; break; }
                        if (!fora) por_prop(r, k, get(v, k));
                    }
                }
                atribuir(h->a, vO(r), e, d); continue;
            }
            str *k = h->c ? chave(ev(h->c, e)) : intern(h->s);
            if (nu < 64) usadas[nu++] = k;
            atribuir(h->b, get(v, k), e, d);
        }
        return;
    }
    atribuir(p, v, e, d);
}
/* RangeError de pilha montado sem rodar JavaScript (o construtor do Error
 * recursaria de novo, já sem pilha) */
static __attribute__((noreturn, noinline)) void estouro_pilha(void)
{
    static unsigned ult;
    if (no_atual && no_atual->fonte && tempo_ms() - ult > 2000) {   /* onde a recursão estava (para o log) */
        char b[90]; int k = 0; const char *q = antes_de(no_atual->fonte, 40);
        for (int z = 0; z < 80 && q[z] && k < 88; z++) b[k++] = q[z] == '\n' ? ' ' : q[z];
        b[k] = 0; ult = tempo_ms();
        registrar("js: pilha estourou perto de [%s]\n", b);
    }
    val c = get(vO(global), I("RangeError")), pr = c.t == OB ? get(c, s_prototype) : vU();
    obj *o = novo_obj(pr.t == OB ? pr.o : p_obj);
    por_prop(o, I("message"), vT("Maximum call stack size exceeded"));
    por_prop(o, I("name"), vT("RangeError"));
    exc = vO(o);
    lancar(exc);
}
static void chk(void)
{
    char c;
    if (t_suave && !(passos & 1023) && tempo_ms() > t_suave) limite_suave();
    if (&c < lim_pilha) estouro_pilha();
    if (!(++passos & 8191) && tempo_ms() > t_limite) lancar_fatal("tempo");
}
static val novo_gerador(obj *o, val este, val *a, int n);
static val executar_fn_(obj *o, val este, val *a, int n, val novo_t);
static val nova_corrotina(obj *o, val este, val *a, int n);
/* async function: corrotina numa pilha própria. Roda até o primeiro await e
 * devolve a Promise; cada await suspende e é retomado por microtask quando o
 * valor se resolve (o __async do js_base conduz). Exceção vira rejeição. */
static val executar_fn(obj *o, val este, val *a, int n, val novo_t)
{
    fdef *d = o->fn->f;
    if (!d->assinc || d->gerador || gerador_entrando) return executar_fn_(o, este, a, n, novo_t);
    return nova_corrotina(o, este, a, n);
}
static val executar_fn_(obj *o, val este, val *a, int n, val novo_t)
{
    fdef *d = o->fn->f;
    if (d->gerador && !gerador_entrando) return novo_gerador(o, este, a, n);
    gerador_entrando = 0;
    env *e = novo_env(o->e, d->nv + 4);
    quadro q, *ant = qa;
    if (prof < PILHA_CH) { pilha_ch[prof].f = o; pilha_ch[prof].site = no_atual; }
    prof++;
    if (d->seta) qa = o->q;
    else { q.este = (este.t == U || este.t == NUL) && !o->fn->f->estrito ? vO(global) : este; q.func = o; q.novo = novo_t; qa = &q; }   /* não estrito: this vazio = janela */
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
    if (!d->seta && novo_t.t == OB && r.t != OB && q.este.t == OB) r = q.este;   /* super() pode ter trocado o this */
    qa = ant;
    prof--;
    return r;
}
static val chamar_n(val f, val este, val *a, int n, val novo_t)
{
    if (f.t == OB && f.o->cl == C_PX && px_chamavel(f.o)) {
        obj *p = f.o;
        int cons = novo_t.t != U;
        val t = px_arm(p, cons ? "construct" : "apply");
        obj *l = novo_arr(n); for (int i = 0; i < n; i++) arr_push(l, a[i]);
        if (cons) {
            if (t.t == U) return construir(vO(p->px_a), a, n);
            val ar[3] = { vO(p->px_a), vO(l), novo_t.t == OB && novo_t.o == p ? vO(p->px_a) : novo_t };
            val r = chamar(t, vO(p->px_h), ar, 3);
            if (r.t != OB) erro("TypeError", "proxy [[Construct]] must return an object", NULL);
            return r;
        }
        if (t.t == U) return chamar(vO(p->px_a), este, a, n);
        val ar[3] = { vO(p->px_a), este, vO(l) };
        return chamar(t, vO(p->px_h), ar, 3);
    }
    if (f.t != OB || f.o->cl != C_FN) erro("TypeError", "is not a function", NULL);
    obj *o = f.o;
    chk();
    if (o->reino != reino_atual) return chamar_reino(f, este, a, n, novo_t);   /* função de outra janela: roda lá */
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
static val n_symbol(val este, val *a, int n);
static val construir(val f, val *a, int n)
{
    if (f.t == OB && f.o->f == n_symbol) erro("TypeError", "Symbol is not a constructor", NULL);
    if (f.t == OB && f.o->cl == C_PX && px_chamavel(f.o)) return chamar_n(f, vU(), a, n, f);
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
#pragma GCC push_options
#pragma GCC optimize("Os")   /* BigInt: código frio, otimizado para tamanho */
#include "js_big.h"
#pragma GCC pop_options
static val aritmetica(int op, val a, val b)
{
    if (op == '+') {
        a = prim(a); b = prim(b);
        if (a.t == ST || b.t == ST) return concat(to_str(a), to_str(b));
        if (a.t == BI || b.t == BI) { if (a.t != BI || b.t != BI) erro("TypeError", "Cannot mix BigInt and other types, use explicit conversions", NULL); return bi_op('+', a.g, b.g); }
        return vN(to_num(a) + to_num(b));
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
            if (a.s->ascii && b.s->ascii) {
                int m = a.s->n < b.s->n ? a.s->n : b.s->n;
                c = memcmp(a.s->s, b.s->s, (size_t)m);
                if (!c) c = a.s->n - b.s->n;
            } else {   /* ordem das unidades UTF-16 */
                int m = a.s->u < b.s->u ? a.s->u : b.s->u; c = 0;
                for (int i = 0; i < m && !c; i++) c = unidade(a.s, i) - unidade(b.s, i);
                if (!c) c = a.s->u - b.s->u;
            }
        } else if (a.t == BI || b.t == BI) {
            c = bi_cmp_v(a, b);
            if (c == 2) return vB(0);
        } else {
            double x = to_num(a), y = to_num(b);
            if (d_nan(x) || d_nan(y)) return vB(0);
            c = x < y ? -1 : x > y;
        }
        return vB(op == '<' ? c < 0 : op == '>' ? c > 0 : op == OP(O_LE) ? c <= 0 : c >= 0);
    }
    case 'i': {
        if (b.t != OB) erro("TypeError", "in: not an object", NULL);
        str *k = chave(a);
        while (b.o->cl == C_PX) {
            val f = px_arm(b.o, "has");
            if (f.t == U) { b = vO(b.o->px_a); continue; }
            val ar[2] = { vO(b.o->px_a), vS(k) };
            return vB(to_bool(chamar(f, vO(b.o->px_h), ar, 2)));
        }
        if (b.o->cl == C_ARR) { int i = indice(k); if (i >= 0) return vB(i < b.o->na); if (k == s_length) return vB(1); }
        for (obj *c = b.o; c; c = c->proto) if (achar(c, k)) return vB(1);
        return vB(b.o->cl == C_EL ? ({ val r; el_get(b.o, k, &r); }) : 0);
    }
    case 'o': {
        if (b.t == OB) {   /* Symbol.hasInstance próprio (o padrão de Function.prototype é o algoritmo abaixo) */
            val h = get(b, I("@@hasInstance"));
            if (h.t == OB && (h.o->cl == C_FN || h.o->cl == C_PX)) return vB(to_bool(chamar(h, b, &a, 1)));
        }
        if (b.t != OB || (b.o->cl != C_FN && !(b.o->cl == C_PX && px_chamavel(b.o)))) erro("TypeError", "Right-hand side of 'instanceof' is not callable", NULL);
        if (a.t == OB && a.o->cl == C_PX) { val gp = get(vO(global), I("Object")); gp = get(gp, I("getPrototypeOf")); a = chamar(gp, vU(), &a, 1); if (a.t != OB) return vB(0); val p = get(b, s_prototype); for (obj *c = a.o; c; c = c->proto) if (p.t == OB && c == p.o) return vB(1); return vB(0); }
        if (a.t != OB) return vB(0);
        val p = get(b, s_prototype);
        for (obj *c = a.o->proto; c; c = c->proto) if (p.t == OB && c == p.o) return vB(1);
        return vB(0);
    }
    default: break;
    }
    if (a.t == BI || b.t == BI || ((a.t == OB || b.t == OB) && (prim(a).t == BI || prim(b).t == BI))) {
        a = prim(a); b = prim(b);
        if (a.t != BI || b.t != BI) erro("TypeError", "Cannot mix BigInt and other types, use explicit conversions", NULL);
        return bi_op(op, a.g, b.g);
    }
    double x = to_num(a), y = to_num(b);
    switch (op) {
    case '-': return vN(x - y);
    case '*': return vN(x * y);
    case '/': return vN(x / y);
    case '%': return vN(m_func(30, x, y));
    case OP(O_POW): return vN(m_pow(x, y));
    default: break;
    }
    int p = para_i32(x), q = para_i32(y);
    switch (op) {
    case '&': return vI(p & q);
    case '|': return vI(p | q);
    case '^': return vI(p ^ q);
    case OP(O_SHL): return vI((int)((unsigned)p << (q & 31)));
    case OP(O_SHR): return vI(p >> (q & 31));
    case OP(O_URS): return vN((double)((unsigned)p >> (q & 31)));
    default: return vU();
    }
}
/* name de uma função anônima recebida por nome (var f = function(){}, {m: () => 1}, f = class {}) */
static void nomear(val v, no *ini, str *nome)
{
    if (v.t != OB || v.o->cl != C_FN || !nome || !ini) return;
    if (!((ini->k == K_FUNC && !ini->f->nome) || (ini->k == K_CLASS && !ini->s))) return;
    if (achar(v.o, I("name"))) return;
    por_prop(v.o, I("name"), vS(nome)); prop *p = achar(v.o, I("name")); if (p) p->at = A_NW | A_NE;
}
static void pp_oculta(obj *o, str *k, val v) { por_prop(o, k, v); prop *p = achar(o, k); if (p) p->at |= A_NE; }
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
    pp_oculta(pr, s_constructor, vO(c));
    if (x->s) { por_prop(c, I("name"), vS(x->s)); achar(c, I("name"))->at = A_NW | A_NE; }
    por_prop(c, s_prototype, vO(pr));
    if (pai.t == OB) { c->proto = pai.o; val pp = get(pai, s_prototype); if (pp.t == OB) pr->proto = pp.o; }
    else if (x->a && pai.t == NUL) pr->proto = NULL;
    c->casa = pr;
    if (x->s) decl(ce, x->s, vO(c));
    for (no *m = x->b; m; m = m->prox) {
        if (m->b == ctor && ctor) continue;
        obj *alvo = m->n ? c : pr;
        str *k = m->c ? chave(ev(m->c, ce)) : m->s;
        if (m->op == 5) continue;
        if (m->op == 4) { if (m->n) { quadro q = { vO(c), c, vU() }, *ant = qa; qa = &q; val v = m->b ? ev(m->b, ce) : vU(); qa = ant; por_prop(c, k, v); } continue; }
        val f = nova_funcao(m->b, ce);
        f.o->casa = alvo;
        if (m->op) {
            prop *p = achar(alvo, k); obj *ac;
            if (p && p->v.t == AC) ac = p->v.o; else { ac = novo_obj(NULL); val w; w.t = AC; w.n = 0; w.o = ac; por_prop(alvo, k, w); }
            por_prop(ac, I(m->op == 1 ? "get" : "set"), f);
            achar(alvo, k)->at |= A_NE;
        } else pp_oculta(alvo, k, f);   /* métodos de classe não são enumeráveis */
    }
    for (no *m = x->b; m; m = m->prox) if (m->op == 5) { val f = nova_funcao(m->b, ce); f.o->casa = c; chamar(f, vO(c), NULL, 0); }
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
    case K_TPLSTR: {   /* o mesmo objeto congelado a cada avaliação do mesmo lugar */
        if (x->f) return vO((obj *)x->f);
        obj *r = novo_arr(0), *w = novo_arr(0);
        for (no *c = x->a->a; c; c = c->prox) if (c->k == K_STR) { arr_push(r, vS(c->s)); arr_push(w, vS(c->b ? c->b->s : c->s)); }
        w->trava = 3; por_prop(r, I("raw"), vO(w)); prop *pr = achar(r, I("raw")); if (pr) pr->at = A_NW | A_NE | A_NC; r->trava = 3;
        x->f = (fdef *)r;
        return vO(r);
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
            if (h->op == 3) {   /* {...x}: próprias enumeráveis (inclui índices de array/string e símbolos) */
                val s = ev(h->a, e);
                if (s.t == OB || s.t == ST) {
                    val ks = get_chaves_proprias(s);
                    for (int i = 0; i < ks.o->na; i++) { str *k = intern(ks.o->a[i].s); por_prop(r, k, get(s, k)); }
                    if (s.t == OB) for (int i = 0; i < s.o->np; i++) { str *k = s.o->p[i].k; if (k && k->n > 2 && k->s[0] == '@' && k->s[1] == '@' && k->s[2] != '#' && !(s.o->p[i].at & A_NE)) por_prop(r, k, get(s, k)); }
                }
                continue;
            }
            str *k = h->c ? chave(ev(h->c, e)) : h->s;
            val v = ev(h->b, e);
            if (!h->op) nomear(v, h->b, k);
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
    case K_BIG: { bint *b = bi_de_texto(x->s->s, x->s->n, 1); if (!b) erro("SyntaxError", "Invalid BigInt literal ", x->s->s); return vG(b); }
    case K_UN: {
        val a = ev(x->a, e);
        switch (x->op) {
        case '!': return vB(!to_bool(a));
        case '-': if (a.t == BI) return vG(bi_neg(a.g)); return vN(-to_num(a));
        case '+': return vN(to_num(a));
        case '~': if (a.t == BI) return vG(bi_sub(bi_neg(a.g), bi_u32(1, 0))); return vI(~to_i(a));
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
        static const char *const n[] = { "undefined", "object", "boolean", "number", "string", "object", "object", "bigint", "symbol" };
        if (a.t == OB && (a.o->cl == C_FN || (a.o->cl == C_PX && px_chamavel(a.o)))) return vS(I("function"));
        return vS(I(n[a.t]));
    }
    case K_DEL: {
        no *t = x->a;
        if (t->k != K_MEM && t->k != K_IDX) return vB(1);
        val o = ev(t->a, e);
        str *k = t->k == K_MEM ? intern(t->s) : chave(ev(t->b, e));
        while (o.t == OB && o.o->cl == C_PX) {
            val f = px_arm(o.o, "deleteProperty");
            if (f.t != U) { val ar[2] = { vO(o.o->px_a), vS(k) }; return vB(to_bool(chamar(f, vO(o.o->px_h), ar, 2))); }
            o = vO(o.o->px_a);
        }
        if (o.t == OB && (o.o->trava >= 2 || (achar(o.o, k) && (achar(o.o, k)->at & A_NC)))) {
            if (!achar(o.o, k)) return vB(1);
            if (modo_estrito()) erro("TypeError", "Cannot delete property: ", k->s);
            return vB(0);
        }
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
        if (v.t == BI) {   /* ++/-- de BigInt */
            val m = vG(bi_soma(v.g, bi_u32(1, x->op < 0)));
            if (t->k == K_ID) var_set(e, t->s, m); else if (t->k == K_MEM) put(o, t->s, m); else put_i(o, k, m);
            return x->n ? m : v;
        }
        double n = to_num(v), m = n + (double)x->op;
        if (t->k == K_ID) var_set(e, t->s, vN(m)); else if (t->k == K_MEM) put(o, t->s, vN(m)); else put_i(o, k, vN(m));
        return vN(x->n ? m : n);
    }
    case K_ASG: {
        no *t = x->a;
        if (x->op == '=') {
            if (t->k == K_ID) {
                val v = ev(x->b, e);
                nomear(v, x->b, t->s);
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
        no_atual = x;
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
            if (pai.t == OB && pai.o != p_fn) { campos(pai.o, qa->este); val r = chamar_n(pai, qa->este, a, n, qa->novo); if (r.t == OB) { if (r.o != qa->este.o && cl) campos(cl, r); qa->este = r; } }
            return qa->este;
        }
        if (c->k == K_MEM) {
            if (c->a->k == K_SUPER) { obj *h = casa_atual(); este = qa->este; f = h && h->proto ? get_o(h->proto, intern(c->s), este) : vU(); }
            else {
                este = ev(c->a, e);
                if (c->op && (este.t == U || este.t == NUL)) return vU();
                no_atual = c;
                f = get(este, c->s);
            }
        } else if (c->k == K_IDX) { este = ev(c->a, e); if (c->op && (este.t == U || este.t == NUL)) return vU(); f = get_i(este, ev(c->b, e)); }
        else f = ev(c, e);
        if (x->op && (f.t == U || f.t == NUL)) return vU();
        int n = avaliar_args(x->b, e, &a);
        if (f.t != OB || (f.o->cl != C_FN && !(f.o->cl == C_PX && px_chamavel(f.o)))) {
            /* como o V8: "nome is not a function" (sem efeitos colaterais: nada de getters aqui) */
            erro("TypeError", c->k == K_MEM || c->k == K_ID ? c->s->s : "expression", " is not a function");
        }
        no_atual = x;   /* lugar da chamada: Error.stack */
        return chamar(f, este, a, n);
    }
    case K_NEW: { val f = ev(x->a, e), *a; int n = avaliar_args(x->b, e, &a); no_atual = x; return construir(f, a, n); }
    case K_AWAIT: return aguardar(ev(x->a, e));
    case K_YIELD: return produzir(x->a ? ev(x->a, e) : vU(), x->op);
    default: return vU();
    }
}
static int laco(int c, str *meu)
{
    if (!(++passos & 1023)) {   /* laço sem chamadas também respeita os limites de tempo */
        if (t_suave && tempo_ms() > t_suave) limite_suave();
        if (tempo_ms() > t_limite) lancar_fatal("tempo");
    }
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
            if (d->a->k == K_ID) nomear(v, d->b, d->a->s);
            if (!x->op) { if (d->a->k == K_ID) var_set(e, d->a->s, v); else ligar(d->a, v, e, 0); }
            else if (d->a->k == K_ID) { if (x->op == 2) decl_const(e, d->a->s, v); else decl(e, d->a->s, v); }
            else ligar(d->a, v, e, x->op == 2 ? 2 : 1);
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
        if (x->op && o.t == OB && o.o->cl != C_ARR) {
            /* for-of com o protocolo de iteração, um valor por vez (gerador infinito + break funciona) */
            val itf = get(o, I("@@iterator"));
            if (itf.t == OB && (itf.o->cl == C_FN || itf.o->cl == C_PX)) {
                val it = chamar(itf, o, NULL, 0);
                if (it.t != OB) erro("TypeError", "Result of the Symbol.iterator method is not an object", NULL);
                val nx = get(it, I("next"));
                for (;;) {
                    val r = chamar(nx, it, NULL, 0);
                    if (r.t != OB) erro("TypeError", "Iterator result is not an object", NULL);
                    if (to_bool(get(r, I("done")))) break;
                    env *f = e; no *alvo = x->a; int d = 0;
                    if (alvo->k == S_VAR) { d = alvo->op != 0; if (d) f = novo_env(e, 2); alvo = alvo->a->a; }
                    atribuir(alvo, get(r, I("value")), f, d);
                    int c = ex(x->c, f), q = laco(c, meu);
                    if (q) {   /* saída antecipada: iterator.return() */
                        val rf = get(it, I("return"));
                        if (rf.t == OB && rf.o->cl == C_FN) { val sv = rv; str *sr = rot; chamar(rf, it, NULL, 0); rv = sv; rot = sr; }
                        if (q == 1) break;
                        return c;
                    }
                }
                return C_N;
            }
        }
        if (x->op && o.t == OB && o.o->cl == C_ARR) {   /* array: lê o tamanho a cada volta, como o iterador do array */
            for (int i = 0; i < o.o->na; i++) {
                env *f = e; no *alvo = x->a; int d = 0;
                if (alvo->k == S_VAR) { d = alvo->op != 0; if (d) f = novo_env(e, 2); alvo = alvo->a->a; }
                atribuir(alvo, o.o->a[i], f, d);
                int c = ex(x->c, f), r = laco(c, meu);
                if (r == 1) break;
                if (r == 2) return c;
            }
            return C_N;
        }
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
    int ant = prog_estrito; prog_estrito = h->estrito;
    val r = vU();
    for (no *x = p->a; x; x = x->prox) {
        if (x->k == S_EXP) r = ev(x->a, NULL);
        else if (ex(x, NULL) == C_RET) break;
    }
    prog_estrito = ant;
    return r;
}
/* texto longo no log: uma linha por \n, cortada em pedaços que cabem no registrar */
static void registrar_linhas(const char *pre, const char *t)
{
    while (*t) {
        char b[208]; int k = 0; while (t[k] && t[k] != '\n' && k < 200) { b[k] = t[k]; k++; }
        b[k] = 0; registrar("%s%s\n", pre, b);
        t += k; if (*t == '\n') t++;
    }
}
/* Roda um programa no escopo global. true se terminou sem exceção. */
static int __attribute__((noinline)) rodar(const char *s, int n, val *res)
{
    salto b; salto *volatile ant = topo_s;
    b.ant = ant;
    if (__builtin_setjmp(b.b)) {
        topo_s = ant; qa = &q_global; prog_estrito = 0;
        if (fatal) return 0;
        val m = exc;
        if (m.t == OB) { val t = get_o(m.o, I("stack"), m); val w = get_o(m.o, I("message"), m); val nm = get_o(m.o, I("name"), m); registrar("js: excecao %s: %s\n", nm.t == ST ? nm.s->s : "?", w.t == ST ? w.s->s : "?"); if (t.t == ST && t.s->n) registrar_linhas("js: ", t.s->s); }
        else if (m.t == ST) registrar("js: excecao %s\n", m.s->s);
        return 0;
    }
    topo_s = &b;
    val r = avaliar(s, n);
    if (res) *res = r;
    topo_s = ant;
    return 1;
}



/* ---- geradores: cada um roda numa pilha própria; yield troca de pilha ---- */
#define GER_PILHA (1024 * 1024)   /* async: o mesmo fôlego de uma chamada comum */
typedef struct ger {
    obj *fo; val este; val *args; int n;
    char *pilha;
    void *esp_g, *esp_c;   /* pilhas salvas do gerador e de quem chamou */
    int estado;            /* 0 novo, 1 parado, 2 rodando, 3 fim */
    int modo; val troca;   /* o que o next/throw/return mandou */
    int res_tipo; val res; /* 0 yield, 1 fim, 2 exceção */
    int assinc;            /* corrotina de async function: await suspende aqui */
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

static val nova_corrotina(obj *o, val este, val *a, int n)
{
    ger *g = novo(sizeof(ger));
    memset(g, 0, sizeof *g);
    g->fo = o; g->este = este; g->n = n; g->assinc = 1;
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
    return chamar(get(vO(global), I("__async")), vU(), &id, 1);
}

static __attribute__((noinline, noreturn)) void ger_entrada(void)
{
    ger *g = ger_novo;
    lim_pilha = g->pilha + 32768;   /* folga para montar o RangeError */
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
    t_suave = n > 0 && a[0].t == NU ? tempo_ms() + (unsigned)a[0].n : 0;
    if (t_limite < tempo_ms() + 5000) t_limite = tempo_ms() + 5000;   /* ferramenta de diagnóstico: estende a página */
    return vU();
}
/* Error.stack: __pilha_cap(o) guarda só ponteiros (barato: o Google cria muitos
 * erros); __pilha(o) monta o texto no formato do V8 ("    at nome (trecho)")
 * quando alguém lê. Sem arquivo/linha: o trecho do código-fonte faz esse papel.
 * A captura pula o quadro do próprio construtor do erro. */
static val n_pilha_cap(val este, val *a, int n)
{
    (void)este;
    val o = n > 0 ? a[0] : vU(); if (o.t != OB) return vU();
    int topo = prof < PILHA_CH ? prof : PILHA_CH, k = 0;
    void **pc = novo(sizeof(void *) * 21);
    for (int i = topo - 2; i >= -1 && k < 10; i--, k++) {
        pc[1 + 2 * k] = i >= 0 ? pilha_ch[i].f : NULL;
        pc[2 + 2 * k] = i == topo - 2 ? pilha_ch[topo - 1 >= 0 ? topo - 1 : 0].site : pilha_ch[i + 1].site;
    }
    pc[0] = (void *)(uintptr_t)k;
    o.o->pc = pc;
    return vU();
}
static int trecho_de(char *b, int k, int max, no *x)
{
    if (!x || !x->fonte) { const char *t = "<anonymous>"; while (*t && k < max) b[k++] = *t++; return k; }
    const char *q = antes_de(x->fonte, 24);
    for (int z = 0; z < 56 && q[z] && k < max; z++) b[k++] = q[z] == '\n' || q[z] == '\r' ? ' ' : q[z];
    return k;
}
static val n_pilha(val este, val *a, int n)
{
    (void)este;
    val o = n > 0 ? a[0] : vU(); if (o.t != OB || !o.o->pc) return vT("");
    void **pc = o.o->pc; int nq = (int)(uintptr_t)pc[0];
    static char b[2048]; int k = 0, max = (int)sizeof b - 8;
    for (int q = 0; q < nq && k < max - 80; q++) {
        obj *f = pc[1 + 2 * q]; no *onde = pc[2 + 2 * q];
        str *nm = f && f->fn ? f->fn->f->nome : NULL;
        const char *pre = q ? "\n    at " : "    at ";
        while (*pre) b[k++] = *pre++;
        const char *t = !f ? "<global>" : nm ? nm->s : "<anonymous>";
        while (*t && k < max - 60) b[k++] = *t++;
        b[k++] = ' '; b[k++] = '(';
        k = trecho_de(b, k, max - 2, onde);
        b[k++] = ')';
    }
    b[k] = 0;
    return vT(b);
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
    int ok = tentar_chamada(a[0], a[1], (unsigned)a[2].n);
    if (!ok && fatal) __builtin_longjmp(topo_s->b, 1);
    arr_push(r, vB(ok)); arr_push(r, ok ? r_tentar : exc);
    return vO(r);
}
/* __gnext(id, modo, valor) -> [done, value] */
static val aguardar(val v)
{
    if (ger_rodando && ger_rodando->assinc) return produzir_um(v);   /* suspende a corrotina */
    val f = get(vO(global), I("__esperar"));   /* await fora de async (topo de módulo) */
    return chamar(f, vU(), &v, 1);
}
static val n_gnext(val este, val *a, int n)
{
    (void)este;
    int id = n > 0 ? (int)to_num(a[0]) : -1, modo = n > 1 ? (int)to_num(a[1]) : 0;
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

#pragma GCC push_options
#pragma GCC optimize("Os")   /* biblioteca nativa: tamanho; o laço do intérprete (ev/ex) fica em -O2 */
#include "js_nativo.h"
#pragma GCC pop_options
