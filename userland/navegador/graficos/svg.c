/* svg.c - SVG 1.1/2 para o Cipó: XML, estilo (atributos, <style>, style=),
 * herança, viewBox e preserveAspectRatio, formas, caminhos exatos, use/symbol,
 * gradientes (bbox ou espaço do usuário), clipPath, opacidade de grupo,
 * traço com tracejado e texto simples. Desenha pelo Canvas 2D (canvas2d.c):
 * mesmo rasterizador com antialias, no tamanho em que a imagem aparece.
 *
 * svg_rasterizar(xml, n, l, a, cor) -> pixels ARGB (aloca) ou NULL
 * svg_tamanho(xml, n, &l, &a)       -> tamanho intrínseco (width/height ou viewBox) */
#include "sagui.h"
#include "cipo.h"

/* canvas2d.c */
int  tela2d_nova(int l, int a); void tela2d_liberar(int id);
const uint32_t *tela2d_pixels(int id, int *l, int *a);
void tela2d_salvar(int id); void tela2d_restaurar(int id);
void tela2d_matriz(int id, const double m[6], int definir); void tela2d_ler_matriz(int id, double m[6]);
void tela2d_alfa(int id, double a); void tela2d_cor(int id, int traco, uint32_t argb); void tela2d_usar_grad(int id, int traco, int g);
void tela2d_linha(int id, double lw, int cap, int join, double miter); void tela2d_tracejado(int id, const double *p, int n, double desl);
int  tela2d_grad_novo(int radial, const double p[6]); void tela2d_grad_parada(int g, double pos, uint32_t argb); void tela2d_grad_liberar(int g);
void tela2d_caminho_novo(int id); void tela2d_mover(int id, double x, double y); void tela2d_linha_para(int id, double x, double y);
void tela2d_quad(int id, double cx, double cy, double x, double y);
void tela2d_bezier(int id, double a, double b, double c, double d, double x, double y);
void tela2d_elipse(int id, double x, double y, double rx, double ry, double rot, double a0, double a1, int anti);
void tela2d_fechar(int id);
void tela2d_preencher(int id, int parimpar); void tela2d_tracar(int id); void tela2d_recortar(int id, int parimpar);
void tela2d_texto(int id, const char *s, double x, double y, double tam, int alinha, int base, int traco, double maxw);
void tela2d_desenhar(int id, int src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh);
uint32_t tela2d_cor_css(const char *s, int *ok);

static double s_sqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return x > 0 ? r : 0; }
static double s_sin(double x) { double r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static double s_cos(double x) { double r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
static double s_atan2(double y, double x) { double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static double s_abs(double x) { return x < 0 ? -x : x; }
#define PI 3.14159265358979323846

/* ------------------------------------------------------------ memória (arena da renderização) */
typedef struct pedaco { struct pedaco *prox; int usado, cap; } pedaco;
static pedaco *arena;
static void *mem(int n)
{
    n = (n + 7) & ~7;
    if (!arena || arena->usado + n > arena->cap) {
        int cap = n > 65000 ? n : 65536;
        pedaco *p = aloca(sizeof(pedaco) + (unsigned)cap);
        if (!p) return NULL;
        p->prox = arena; p->usado = 0; p->cap = cap; arena = p;
    }
    void *r = (char *)(arena + 1) + arena->usado;
    arena->usado += n;
    memset(r, 0, (size_t)n);
    return r;
}
static void mem_fim(void) { while (arena) { pedaco *p = arena; arena = p->prox; libera(p); } }
static char *dup(const char *s, int n) { char *d = mem(n + 1); if (d) { memcpy(d, s, (size_t)n); d[n] = 0; } return d; }

/* ------------------------------------------------------------ XML */
typedef struct atr { const char *k, *v; struct atr *prox; } atr;
typedef struct el { char tag[24]; atr *at; struct el *pai, *filho, *ult, *prox; const char *texto; int ntexto; } el;

static int esp(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int igual(const char *a, const char *b) { return compara_texto(a, b) == 0; }
static int igual_n(const char *a, const char *b, int n) { return compara_texto_n(a, b, (size_t)n) == 0; }

/* entidades básicas e numéricas, no lugar */
static char *entidades(const char *s, int n)
{
    char *d = mem(n * 4 + 1); int k = 0;
    if (!d) return (char *)"";
    for (int i = 0; i < n;) {
        if (s[i] == '&') {
            int j = i + 1; while (j < n && j - i < 12 && s[j] != ';') j++;
            if (j < n && s[j] == ';') {
                const char *e = s + i + 1; int t = j - i - 1;
                uint32_t cp = 0;
                if (t > 1 && e[0] == '#') { if (e[1] == 'x' || e[1] == 'X') for (int q = 2; q < t; q++) { char c = e[q]; cp = cp * 16 + (uint32_t)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10); } else for (int q = 1; q < t; q++) cp = cp * 10 + (uint32_t)(e[q] - '0'); }
                else if (t == 3 && igual_n(e, "amp", 3)) cp = '&'; else if (t == 2 && igual_n(e, "lt", 2)) cp = '<'; else if (t == 2 && igual_n(e, "gt", 2)) cp = '>';
                else if (t == 4 && igual_n(e, "quot", 4)) cp = '"'; else if (t == 4 && igual_n(e, "apos", 4)) cp = '\''; else if (t == 4 && igual_n(e, "nbsp", 4)) cp = 0xA0;
                if (cp) { k += utf8_por(d + k, cp); i = j + 1; continue; }
            }
        }
        d[k++] = s[i++];
    }
    d[k] = 0;
    return d;
}
static el *xml_ler(const char *s, int n)
{
    el *raiz = mem(sizeof(el)), *cur = raiz;
    if (!raiz) return NULL;
    copia_texto(raiz->tag, "#doc", sizeof raiz->tag);
    int i = 0;
    while (i < n) {
        if (s[i] != '<') {
            int j = i; while (j < n && s[j] != '<') j++;
            if (cur && cur != raiz) {   /* texto (para <style> e <text>) */
                el *t = mem(sizeof(el));
                if (t) { copia_texto(t->tag, "#t", sizeof t->tag); t->texto = entidades(s + i, j - i); t->ntexto = (int)tam_texto(t->texto); t->pai = cur; if (cur->ult) cur->ult->prox = t; else cur->filho = t; cur->ult = t; }
            }
            i = j; continue;
        }
        if (i + 4 <= n && igual_n(s + i, "<!--", 4)) { int j = i + 4; while (j + 3 <= n && !igual_n(s + j, "-->", 3)) j++; i = j + 3; continue; }
        if (i + 9 <= n && igual_n(s + i, "<![CDATA[", 9)) {
            int j = i + 9; while (j + 3 <= n && !igual_n(s + j, "]]>", 3)) j++;
            if (cur && cur != raiz) { el *t = mem(sizeof(el)); if (t) { copia_texto(t->tag, "#t", sizeof t->tag); t->texto = dup(s + i + 9, j - i - 9); t->ntexto = j - i - 9; t->pai = cur; if (cur->ult) cur->ult->prox = t; else cur->filho = t; cur->ult = t; } }
            i = j + 3; continue;
        }
        if (i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!')) { while (i < n && s[i] != '>') i++; i++; continue; }
        if (i + 1 < n && s[i + 1] == '/') {   /* fecha */
            int j = i + 2; while (j < n && s[j] != '>') j++;
            if (cur && cur->pai) cur = cur->pai;
            i = j + 1; continue;
        }
        int j = i + 1; while (j < n && !esp(s[j]) && s[j] != '>' && s[j] != '/') j++;
        el *e = mem(sizeof(el));
        if (!e) break;
        int tn = j - i - 1; if (tn > 23) tn = 23;
        const char *tg = s + i + 1;
        for (int q = 0; q < j - i - 1; q++) if (tg[q] == ':') { tn = j - i - 1 - q - 1; tg += q + 1; if (tn > 23) tn = 23; break; }   /* svg:path */
        memcpy(e->tag, tg, (size_t)tn); e->tag[tn] = 0;
        int p = j, sozinho = 0;
        while (p < n && s[p] != '>') {
            while (p < n && esp(s[p])) p++;
            if (p < n && s[p] == '/') { sozinho = 1; p++; continue; }
            if (p >= n || s[p] == '>') break;
            int k0 = p; while (p < n && s[p] != '=' && !esp(s[p]) && s[p] != '>' && s[p] != '/') p++;
            int k1 = p;
            while (p < n && esp(s[p])) p++;
            if (p < n && s[p] == '=') {
                p++; while (p < n && esp(s[p])) p++;
                char q = s[p] == '"' || s[p] == '\'' ? s[p++] : 0;
                int v0 = p; while (p < n && (q ? s[p] != q : !esp(s[p]) && s[p] != '>')) p++;
                atr *a = mem(sizeof(atr));
                if (a) { a->k = dup(s + k0, k1 - k0); a->v = entidades(s + v0, p - v0); a->prox = e->at; e->at = a; }
                if (q && p < n) p++;
            } else if (k1 == k0) p++;
        }
        e->pai = cur;
        if (cur->ult) cur->ult->prox = e; else cur->filho = e;
        cur->ult = e;
        if (!sozinho) cur = e;
        i = p + 1;
    }
    return raiz;
}
/* nomes sem distinguir maiúsculas: o SVG embutido no HTML chega com eles em minúsculas (viewbox) */
static int igual_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x = (char)(x + 32); if (y >= 'A' && y <= 'Z') y = (char)(y + 32); if (x != y) return 0; }
    return *a == *b;
}
static const char *at_de(const el *e, const char *k)
{
    for (atr *a = e->at; a; a = a->prox) {
        const char *n = a->k;
        if (igual_ci(n, k)) return a->v;
        const char *c = n; while (*c && *c != ':') c++;   /* xlink:href */
        if (*c == ':' && igual_ci(c + 1, k)) return a->v;
    }
    return NULL;
}
static el *por_id(el *r, const char *id)
{
    for (el *c = r->filho; c; c = c->prox) {
        const char *v = at_de(c, "id");
        if (v && igual(v, id)) return c;
        el *x = por_id(c, id);
        if (x) return x;
    }
    return NULL;
}

/* ------------------------------------------------------------ números */
static const char *num(const char *s, double *v, int *ok)
{
    while (esp(*s) || *s == ',') s++;
    const char *i = s;
    double sg = 1, r = 0, f = 0.1; int dig = 0;
    if (*s == '-') { sg = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { r = r * 10 + (*s++ - '0'); dig = 1; }
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { r += f * (*s++ - '0'); f /= 10; dig = 1; } }
    if (dig && (*s == 'e' || *s == 'E') && (s[1] == '-' || s[1] == '+' || (s[1] >= '0' && s[1] <= '9'))) {
        s++; int es = 1, ex = 0; if (*s == '-') { es = -1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') ex = ex * 10 + (*s++ - '0');
        while (ex-- > 0) r = es > 0 ? r * 10 : r / 10;
    }
    *ok = dig; *v = sg * r;
    return dig ? s : i;
}
/* comprimento com unidade (% relativo a ref) */
static double medida(const char *s, double ref, double padrao)
{
    if (!s) return padrao;
    int ok; double v; const char *f = num(s, &v, &ok);
    if (!ok) return padrao;
    if (*f == '%') return v * ref / 100;
    if (f[0] == 'e' && f[1] == 'm') return v * 16;
    if (f[0] == 'p' && f[1] == 't') return v * 4 / 3;
    if (f[0] == 'm' && f[1] == 'm') return v * 96 / 25.4;
    if (f[0] == 'c' && f[1] == 'm') return v * 96 / 2.54;
    if (f[0] == 'i' && f[1] == 'n') return v * 96;
    return v;
}

/* ------------------------------------------------------------ matrizes */
typedef struct { double m[6]; } mat;
static void mult(mat *r, const mat *a, const mat *b)
{
    mat o;
    o.m[0] = a->m[0] * b->m[0] + a->m[2] * b->m[1]; o.m[1] = a->m[1] * b->m[0] + a->m[3] * b->m[1];
    o.m[2] = a->m[0] * b->m[2] + a->m[2] * b->m[3]; o.m[3] = a->m[1] * b->m[2] + a->m[3] * b->m[3];
    o.m[4] = a->m[0] * b->m[4] + a->m[2] * b->m[5] + a->m[4]; o.m[5] = a->m[1] * b->m[4] + a->m[3] * b->m[5] + a->m[5];
    *r = o;
}
static mat ident(void) { mat m = { { 1, 0, 0, 1, 0, 0 } }; return m; }
static mat ler_transform(const char *s)
{
    mat r = ident();
    while (s && *s) {
        while (esp(*s) || *s == ',') s++;
        const char *n = s; while (*s && *s != '(') s++;
        if (!*s) break;
        int tn = (int)(s - n); while (tn && esp(n[tn - 1])) tn--;
        s++;
        double v[6] = { 0 }; int k = 0, ok;
        while (*s && *s != ')' && k < 6) { const char *q = num(s, &v[k], &ok); if (!ok) break; s = q; k++; while (esp(*s) || *s == ',') s++; }
        while (*s && *s != ')') s++;
        if (*s) s++;
        mat t = ident();
        if (tn == 6 && igual_n(n, "matrix", 6) && k == 6) for (int i = 0; i < 6; i++) t.m[i] = v[i];
        else if (tn == 9 && igual_n(n, "translate", 9)) { t.m[4] = v[0]; t.m[5] = k > 1 ? v[1] : 0; }
        else if (tn == 5 && igual_n(n, "scale", 5)) { t.m[0] = v[0]; t.m[3] = k > 1 ? v[1] : v[0]; }
        else if (tn == 6 && igual_n(n, "rotate", 6)) {
            double a = v[0] * PI / 180, c = s_cos(a), sn = s_sin(a);
            t.m[0] = c; t.m[1] = sn; t.m[2] = -sn; t.m[3] = c;
            if (k == 3) { mat a1 = ident(), a2 = ident(); a1.m[4] = v[1]; a1.m[5] = v[2]; a2.m[4] = -v[1]; a2.m[5] = -v[2]; mult(&t, &a1, &t); mult(&t, &t, &a2); }
        } else if (tn == 5 && igual_n(n, "skewX", 5)) { double a = v[0] * PI / 180; t.m[2] = s_sin(a) / s_cos(a); }
        else if (tn == 5 && igual_n(n, "skewY", 5)) { double a = v[0] * PI / 180; t.m[1] = s_sin(a) / s_cos(a); }
        mult(&r, &r, &t);
    }
    return r;
}

/* ------------------------------------------------------------ CSS (<style> simples) */
typedef struct regra { const char *sel; int nsel; const char *decl; int ndecl; int espec, ordem; struct regra *prox; } regra;
static regra *regras; static int n_regras;
static void css_ler(const char *s, int n)
{
    int i = 0;
    while (i < n) {
        while (i < n && esp(s[i])) i++;
        if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++; i += 2; continue; }
        if (i < n && s[i] == '@') {   /* @media etc.: pula o bloco */
            int prof = 0; while (i < n) { if (s[i] == '{') prof++; else if (s[i] == '}') { if (--prof <= 0) { i++; break; } } else if (s[i] == ';' && !prof) { i++; break; } i++; }
            continue;
        }
        int a = i; while (i < n && s[i] != '{') i++;
        if (i >= n) break;
        int b = i; i++;
        int c = i; while (i < n && s[i] != '}') i++;
        int d = i; i++;
        /* um seletor por regra (vírgula separa) */
        for (int p = a; p < b;) {
            int q = p; while (q < b && s[q] != ',') q++;
            int x = p, y = q; while (x < y && esp(s[x])) x++; while (y > x && esp(s[y - 1])) y--;
            if (y > x) {
                regra *r = mem(sizeof(regra));
                if (!r) return;
                r->sel = s + x; r->nsel = y - x; r->decl = s + c; r->ndecl = d - c; r->ordem = n_regras++;
                int e = 0; for (int k = x; k < y; k++) { if (s[k] == '#') e += 100; else if (s[k] == '.' || s[k] == '[' || s[k] == ':') e += 10; else if ((k == x || esp(s[k - 1]) || s[k - 1] == '>') && s[k] != '*' && !esp(s[k]) && s[k] != '>') e += 1; }
                r->espec = e; r->prox = regras; regras = r;
            }
            p = q + 1;
        }
    }
}
/* um seletor composto (tag, .classe, #id, [atr]) casa com o elemento */
static int casa_simples(const el *e, const char *s, int n)
{
    int i = 0;
    if (i < n && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != '*') {
        int j = i; while (j < n && s[j] != '.' && s[j] != '#' && s[j] != '[' && s[j] != ':') j++;
        if (!((int)tam_texto(e->tag) == j - i && igual_n(e->tag, s + i, j - i))) return 0;
        i = j;
    }
    if (i < n && s[i] == '*') i++;
    while (i < n) {
        char t = s[i++];
        int j = i; while (j < n && s[j] != '.' && s[j] != '#' && s[j] != '[' && s[j] != ':' && s[j] != ']') j++;
        if (t == '.') {
            const char *cl = at_de(e, "class"); int ok = 0;
            for (const char *p = cl; p && *p;) { while (esp(*p)) p++; const char *q = p; while (*q && !esp(*q)) q++; if (q - p == j - i && igual_n(p, s + i, j - i)) ok = 1; p = q; }
            if (!ok) return 0;
        } else if (t == '#') { const char *id = at_de(e, "id"); if (!id || (int)tam_texto(id) != j - i || !igual_n(id, s + i, j - i)) return 0; }
        else if (t == '[') { char k[32]; int m = j - i < 31 ? j - i : 31; memcpy(k, s + i, (size_t)m); k[m] = 0; char *eq = k; while (*eq && *eq != '=') eq++; *eq = 0; if (!at_de(e, k)) return 0; if (j < n && s[j] == ']') j++; }
        else if (t == ':') { while (j < n && s[j] != '.' && s[j] != '#') j++; }   /* pseudo-classe: ignora */
        i = j;
    }
    return 1;
}
static int casa(const el *e, const char *s, int n)
{
    /* último composto casa com e; os de antes com algum ancestral (descendência e >) */
    int f = n; while (f > 0 && esp(s[f - 1])) f--;
    int i = f; while (i > 0 && !esp(s[i - 1]) && s[i - 1] != '>') i--;
    if (!casa_simples(e, s + i, f - i)) return 0;
    int resto = i; while (resto > 0 && (esp(s[resto - 1]) || s[resto - 1] == '>')) resto--;
    if (!resto) return 1;
    for (const el *p = e->pai; p && p->tag[0] != '#'; p = p->pai) if (casa(p, s, resto)) return 1;
    return 0;
}

/* ------------------------------------------------------------ estilo */
typedef struct {
    uint32_t cor;                 /* color (currentColor) */
    int fill_tipo; uint32_t fill; const char *fill_url;     /* 0 none, 1 cor, 2 url */
    int stroke_tipo; uint32_t stroke; const char *stroke_url;
    double fill_op, stroke_op, op, lw, miter, dash[16], dash_desl, tam_fonte;
    int ndash, cap, join, parimpar, visivel, clip_parimpar, ancora, negrito;
} estilo;

static void decl(estilo *st, const char *k, int nk, const char *v, int nv, estilo *pai);
static void decls(estilo *st, const char *s, int n, estilo *pai)
{
    int i = 0;
    while (i < n) {
        while (i < n && (esp(s[i]) || s[i] == ';')) i++;
        int a = i; while (i < n && s[i] != ':' && s[i] != ';') i++;
        if (i >= n || s[i] != ':') { while (i < n && s[i] != ';') i++; continue; }
        int b = i++; while (b > a && esp(s[b - 1])) b--;
        while (i < n && esp(s[i])) i++;
        int c = i, prof = 0; while (i < n && (s[i] != ';' || prof)) { if (s[i] == '(') prof++; else if (s[i] == ')') prof--; i++; }
        int d = i; while (d > c && esp(s[d - 1])) d--;
        if (d - c > 10 && igual_n(s + d - 10, "!important", 10)) { d -= 10; while (d > c && esp(s[d - 1])) d--; }
        decl(st, s + a, b - a, s + c, d - c, pai);
    }
}
static int tinta(const char *v, int nv, uint32_t cor_atual, uint32_t *c, const char **url)
{
    char b[96]; int k = nv < 95 ? nv : 95; memcpy(b, v, (size_t)k); b[k] = 0;
    if (igual(b, "none") || igual(b, "transparent")) return 0;
    if (igual_n(b, "url(", 4)) {
        const char *p = b + 4; while (*p == ' ' || *p == '\'' || *p == '"') p++;
        if (*p == '#') p++;
        const char *q = p; while (*q && *q != ')' && *q != '\'' && *q != '"') q++;
        *url = dup(p, (int)(q - p));
        /* reserva depois do url(): url(#g) red */
        const char *r = acha_char(b, ')');
        if (r) { r++; while (*r == ' ') r++; int ok; uint32_t x = tela2d_cor_css(r, &ok); *c = ok ? x : 0; } else *c = 0;
        return 2;
    }
    if (igual(b, "currentColor") || igual(b, "currentcolor")) { *c = cor_atual; return 1; }
    int ok; uint32_t x = tela2d_cor_css(b, &ok);
    if (!ok) return -1;
    *c = x; return 1;
}
static void decl(estilo *st, const char *k, int nk, const char *v, int nv, estilo *pai)
{
    #define K(s) (nk == (int)sizeof(s) - 1 && igual_n(k, s, nk))
    char b[128]; int n = nv < 127 ? nv : 127; memcpy(b, v, (size_t)n); b[n] = 0;
    if (igual(b, "inherit") && pai) {
        if (K("fill")) { st->fill_tipo = pai->fill_tipo; st->fill = pai->fill; st->fill_url = pai->fill_url; }
        else if (K("stroke")) { st->stroke_tipo = pai->stroke_tipo; st->stroke = pai->stroke; st->stroke_url = pai->stroke_url; }
        return;
    }
    if (K("fill")) { uint32_t c = 0; const char *u = NULL; int t = tinta(v, nv, st->cor, &c, &u); if (t >= 0) { st->fill_tipo = t; st->fill = c; st->fill_url = u; } }
    else if (K("stroke")) { uint32_t c = 0; const char *u = NULL; int t = tinta(v, nv, st->cor, &c, &u); if (t >= 0) { st->stroke_tipo = t; st->stroke = c; st->stroke_url = u; } }
    else if (K("color")) { int ok; uint32_t c = tela2d_cor_css(b, &ok); if (ok) st->cor = c; }
    else if (K("fill-opacity")) { int ok; double x; num(b, &x, &ok); if (ok) st->fill_op = b[n - 1] == '%' ? x / 100 : x; }
    else if (K("stroke-opacity")) { int ok; double x; num(b, &x, &ok); if (ok) st->stroke_op = b[n - 1] == '%' ? x / 100 : x; }
    else if (K("opacity")) { int ok; double x; num(b, &x, &ok); if (ok) st->op = b[n - 1] == '%' ? x / 100 : x; }
    else if (K("stroke-width")) st->lw = medida(b, 100, st->lw);
    else if (K("stroke-miterlimit")) { int ok; double x; num(b, &x, &ok); if (ok && x >= 1) st->miter = x; }
    else if (K("stroke-linecap")) st->cap = igual(b, "round") ? 1 : igual(b, "square") ? 2 : 0;
    else if (K("stroke-linejoin")) st->join = igual(b, "round") ? 1 : igual(b, "bevel") ? 2 : 0;
    else if (K("fill-rule")) st->parimpar = igual(b, "evenodd");
    else if (K("clip-rule")) st->clip_parimpar = igual(b, "evenodd");
    else if (K("stroke-dasharray")) {
        st->ndash = 0;
        if (!igual(b, "none")) { const char *p = b; int ok; double x; while (st->ndash < 16 && *p) { const char *q = num(p, &x, &ok); if (!ok) break; st->dash[st->ndash++] = x; p = q; } }
    }
    else if (K("stroke-dashoffset")) st->dash_desl = medida(b, 100, 0);
    else if (K("visibility")) st->visivel = !(igual(b, "hidden") || igual(b, "collapse"));
    else if (K("font-size")) st->tam_fonte = medida(b, 16, st->tam_fonte);
    else if (K("font-weight")) st->negrito = igual(b, "bold") || (b[0] >= '6' && b[0] <= '9' && n == 3);
    else if (K("text-anchor")) st->ancora = igual(b, "middle") ? 2 : igual(b, "end") ? 1 : 0;
    #undef K
}
static const char *const props[] = { "fill", "stroke", "color", "fill-opacity", "stroke-opacity", "opacity", "stroke-width", "stroke-miterlimit",
    "stroke-linecap", "stroke-linejoin", "fill-rule", "clip-rule", "stroke-dasharray", "stroke-dashoffset", "visibility", "font-size", "font-weight", "text-anchor" };
/* estilo computado: herdado do pai, atributos de apresentação, regras do <style>, style= */
static void estilizar(const el *e, estilo *st, estilo *pai)
{
    if (pai) { *st = *pai; st->op = 1; }   /* opacity não herda */
    const char *c = at_de(e, "color");
    if (c) { int ok; uint32_t x = tela2d_cor_css(c, &ok); if (ok) st->cor = x; }
    for (unsigned i = 0; i < sizeof props / sizeof props[0]; i++) {
        const char *v = at_de(e, props[i]);
        if (v) decl(st, props[i], (int)tam_texto(props[i]), v, (int)tam_texto(v), pai);
    }
    /* regras, da menor especificidade para a maior (e na ordem) */
    regra *casam[64]; int nc = 0;
    for (regra *r = regras; r && nc < 64; r = r->prox) if (casa(e, r->sel, r->nsel)) casam[nc++] = r;
    for (int i = 1; i < nc; i++) { regra *x = casam[i]; int j = i - 1; while (j >= 0 && (casam[j]->espec > x->espec || (casam[j]->espec == x->espec && casam[j]->ordem > x->ordem))) { casam[j + 1] = casam[j]; j--; } casam[j + 1] = x; }
    for (int i = 0; i < nc; i++) decls(st, casam[i]->decl, casam[i]->ndecl, pai);
    const char *s = at_de(e, "style");
    if (s) decls(st, s, (int)tam_texto(s), pai);
}

/* ------------------------------------------------------------ caminhos */
typedef struct { int t; double mx, my, Mx, My, cx, cy; } caixa;   /* caixa delimitadora (espaço do usuário) */
static void cx_ponto(caixa *b, double x, double y)
{
    if (!b->t) { b->mx = b->Mx = x; b->my = b->My = y; b->t = 1; return; }
    if (x < b->mx) b->mx = x; if (x > b->Mx) b->Mx = x;
    if (y < b->my) b->my = y; if (y > b->My) b->My = y;
}
static int T_;   /* tela em uso */
static void P_mover(caixa *b, double x, double y) { tela2d_mover(T_, x, y); cx_ponto(b, x, y); }
static void P_linha(caixa *b, double x, double y) { tela2d_linha_para(T_, x, y); cx_ponto(b, x, y); }
/* arco do SVG (F.6.5) com bezier por trechos de até 90 graus */
static void P_arco(caixa *b, double x1, double y1, double rx, double ry, double rot, int grande, int varre, double x2, double y2)
{
    if (x1 == x2 && y1 == y2) return;
    rx = s_abs(rx); ry = s_abs(ry);
    if (rx == 0 || ry == 0) { P_linha(b, x2, y2); return; }
    double phi = rot * PI / 180, cf = s_cos(phi), sf = s_sin(phi);
    double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2, xp = cf * dx + sf * dy, yp = -sf * dx + cf * dy;
    double lam = xp * xp / (rx * rx) + yp * yp / (ry * ry);
    if (lam > 1) { double s = s_sqrt(lam); rx *= s; ry *= s; }
    double nu = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp, de = rx * rx * yp * yp + ry * ry * xp * xp;
    double co = de > 0 && nu > 0 ? s_sqrt(nu / de) : 0;
    if (grande == varre) co = -co;
    double cxp = co * rx * yp / ry, cyp = -co * ry * xp / rx;
    double cx = cf * cxp - sf * cyp + (x1 + x2) / 2, cy = sf * cxp + cf * cyp + (y1 + y2) / 2;
    double ux = (xp - cxp) / rx, uy = (yp - cyp) / ry, vx = (-xp - cxp) / rx, vy = (-yp - cyp) / ry;
    double t1 = s_atan2(uy, ux), dt = s_atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!varre && dt > 0) dt -= 2 * PI; else if (varre && dt < 0) dt += 2 * PI;
    int seg = (int)(s_abs(dt) / (PI / 2) + 0.999); if (seg < 1) seg = 1;
    double d = dt / seg, k = 4.0 / 3 * s_sin(d / 4) / s_cos(d / 4);
    for (int i = 0; i < seg; i++) {
        double a0 = t1 + d * i, a1 = a0 + d;
        double c0 = s_cos(a0), s0 = s_sin(a0), c1 = s_cos(a1), s1 = s_sin(a1);
        double p1x = c0 - k * s0, p1y = s0 + k * c0, p2x = c1 + k * s1, p2y = s1 - k * c1;
        #define TX(px, py) (cx + rx * (px) * cf - ry * (py) * sf)
        #define TY(px, py) (cy + rx * (px) * sf + ry * (py) * cf)
        double ex = i == seg - 1 ? x2 : TX(c1, s1), ey = i == seg - 1 ? y2 : TY(c1, s1);
        tela2d_bezier(T_, TX(p1x, p1y), TY(p1x, p1y), TX(p2x, p2y), TY(p2x, p2y), ex, ey);
        cx_ponto(b, TX(p1x, p1y), TY(p1x, p1y)); cx_ponto(b, TX(p2x, p2y), TY(p2x, p2y)); cx_ponto(b, ex, ey);
        #undef TX
        #undef TY
    }
}
static void caminho(const char *d, caixa *b)
{
    double x = 0, y = 0, sx = 0, sy = 0, qx = 0, qy = 0;
    char cmd = 0, ult = 0;
    const char *p = d;
    int ok;
    while (p && *p) {
        while (esp(*p) || *p == ',') p++;
        if (!*p) break;
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) cmd = *p++;
        else if (!cmd) return;
        int rel = cmd >= 'a';
        char C = (char)(rel ? cmd - 32 : cmd);
        double v[7];
        int need = C == 'M' || C == 'L' || C == 'T' ? 2 : C == 'H' || C == 'V' ? 1 : C == 'C' ? 6 : C == 'S' || C == 'Q' ? 4 : C == 'A' ? 7 : 0;
        for (int i = 0; i < need; i++) {
            if (C == 'A' && (i == 3 || i == 4)) {   /* bandeiras coladas: "0 01" ou "011" */
                while (esp(*p) || *p == ',') p++;
                if (*p != '0' && *p != '1') return;
                v[i] = *p++ - '0'; continue;
            }
            const char *q = num(p, &v[i], &ok);
            if (!ok) return;
            p = q;
        }
        double bx = rel ? x : 0, by = rel ? y : 0;
        switch (C) {
        case 'M': x = bx + v[0]; y = by + v[1]; sx = x; sy = y; P_mover(b, x, y); cmd = rel ? 'l' : 'L'; break;
        case 'L': x = bx + v[0]; y = by + v[1]; P_linha(b, x, y); break;
        case 'H': x = (rel ? x : 0) + v[0]; P_linha(b, x, y); break;
        case 'V': y = (rel ? y : 0) + v[0]; P_linha(b, x, y); break;
        case 'C': tela2d_bezier(T_, bx + v[0], by + v[1], bx + v[2], by + v[3], bx + v[4], by + v[5]);
            cx_ponto(b, bx + v[0], by + v[1]); cx_ponto(b, bx + v[2], by + v[3]);
            qx = bx + v[2]; qy = by + v[3]; x = bx + v[4]; y = by + v[5]; cx_ponto(b, x, y); break;
        case 'S': { double c1x = ult == 'C' || ult == 'S' ? 2 * x - qx : x, c1y = ult == 'C' || ult == 'S' ? 2 * y - qy : y;
            tela2d_bezier(T_, c1x, c1y, bx + v[0], by + v[1], bx + v[2], by + v[3]);
            cx_ponto(b, c1x, c1y); cx_ponto(b, bx + v[0], by + v[1]);
            qx = bx + v[0]; qy = by + v[1]; x = bx + v[2]; y = by + v[3]; cx_ponto(b, x, y); break; }
        case 'Q': tela2d_quad(T_, bx + v[0], by + v[1], bx + v[2], by + v[3]); cx_ponto(b, bx + v[0], by + v[1]);
            qx = bx + v[0]; qy = by + v[1]; x = bx + v[2]; y = by + v[3]; cx_ponto(b, x, y); break;
        case 'T': { double cx = ult == 'Q' || ult == 'T' ? 2 * x - qx : x, cy = ult == 'Q' || ult == 'T' ? 2 * y - qy : y;
            tela2d_quad(T_, cx, cy, bx + v[0], by + v[1]); cx_ponto(b, cx, cy);
            qx = cx; qy = cy; x = bx + v[0]; y = by + v[1]; cx_ponto(b, x, y); break; }
        case 'A': P_arco(b, x, y, v[0], v[1], v[2], (int)v[3], (int)v[4], bx + v[5], by + v[6]); x = bx + v[5]; y = by + v[6]; break;
        case 'Z': tela2d_fechar(T_); x = sx; y = sy; cmd = 0; break;
        default: return;
        }
        ult = C;
    }
}

/* ------------------------------------------------------------ desenho */
static el *raiz_doc;
static int gradiente(const char *id, const caixa *b, double op)
{
    el *g = por_id(raiz_doc, id);
    if (!g) return -1;
    int radial = igual(g->tag, "radialGradient");
    if (!radial && !igual(g->tag, "linearGradient")) return -1;
    /* atributos e paradas podem vir de outro gradiente (href) */
    el *src = g; const char *un = NULL, *tf = NULL; const char *atv[6] = { 0 };
    static const char *const nl[4] = { "x1", "y1", "x2", "y2" }, *const nr[6] = { "cx", "cy", "r", "fx", "fy", "fr" };
    el *stops = NULL;
    for (el *q = g; q; ) {
        if (!un) un = at_de(q, "gradientUnits");
        if (!tf) tf = at_de(q, "gradientTransform");
        for (int i = 0; i < (radial ? 6 : 4); i++) if (!atv[i]) atv[i] = at_de(q, radial ? nr[i] : nl[i]);
        if (!stops) for (el *c = q->filho; c; c = c->prox) if (igual(c->tag, "stop")) { stops = q; break; }
        const char *h = at_de(q, "href");
        q = h && h[0] == '#' ? por_id(raiz_doc, h + 1) : NULL;
        if (q == src) break;
    }
    int bbox = !un || !igual(un, "userSpaceOnUse");
    double p[6];
    if (radial) {
        double cx = medida(atv[0], 100, 50) , cy = medida(atv[1], 100, 50), r = medida(atv[2], 100, 50);
        if (!bbox) { cx = medida(atv[0], 100, 50); cy = medida(atv[1], 100, 50); r = medida(atv[2], 100, 50); }
        /* em bbox, % e frações valem de 0 a 1 */
        if (bbox) { cx = atv[0] ? (acha_char(atv[0], '%') ? medida(atv[0], 1, .5) : medida(atv[0], 1, .5)) : .5; cy = atv[1] ? medida(atv[1], 1, .5) : .5; r = atv[2] ? medida(atv[2], 1, .5) : .5; }
        double fx = atv[3] ? (bbox ? medida(atv[3], 1, cx) : medida(atv[3], 100, cx)) : cx, fy = atv[4] ? (bbox ? medida(atv[4], 1, cy) : medida(atv[4], 100, cy)) : cy;
        double fr = atv[5] ? (bbox ? medida(atv[5], 1, 0) : medida(atv[5], 100, 0)) : 0;
        p[0] = fx; p[1] = fy; p[2] = fr; p[3] = cx; p[4] = cy; p[5] = r;
    } else {
        if (bbox) { p[0] = atv[0] ? medida(atv[0], 1, 0) : 0; p[1] = atv[1] ? medida(atv[1], 1, 0) : 0; p[2] = atv[2] ? medida(atv[2], 1, 1) : 1; p[3] = atv[3] ? medida(atv[3], 1, 0) : 0; }
        else { p[0] = medida(atv[0], 100, 0); p[1] = medida(atv[1], 100, 0); p[2] = medida(atv[2], 100, 100); p[3] = medida(atv[3], 100, 0); }
        p[4] = p[5] = 0;
    }
    /* o canvas interpreta o gradiente no espaço do usuário da hora de pintar: a
     * matriz do bbox e o gradientTransform entram na matriz atual (ver pintar) */
    int id_g = tela2d_grad_novo(radial, p);
    if (id_g < 0) return -1;
    if (stops) {
        double ult = 0;
        for (el *c = stops->filho; c; c = c->prox) {
            if (!igual(c->tag, "stop")) continue;
            estilo s; memset(&s, 0, sizeof s);
            const char *o = at_de(c, "offset");
            double off = 0; if (o) { int ok; num(o, &off, &ok); if (acha_char(o, '%')) off /= 100; }
            if (off < ult) off = ult; if (off > 1) off = 1; ult = off;
            const char *sc = at_de(c, "stop-color"), *so = at_de(c, "stop-opacity");
            const char *st = at_de(c, "style");
            char cor[64] = "black", opv[16] = "1";
            if (sc) copia_texto(cor, sc, sizeof cor);
            if (so) copia_texto(opv, so, sizeof opv);
            for (const char *q = st; q && *q;) {
                while (*q == ' ' || *q == ';') q++;
                const char *k0 = q; while (*q && *q != ':') q++;
                if (!*q) break;
                int kn = (int)(q - k0); q++; while (*q == ' ') q++;
                const char *v0 = q; while (*q && *q != ';') q++;
                int vn = (int)(q - v0); while (vn && esp(v0[vn - 1])) vn--;
                if (kn == 10 && igual_n(k0, "stop-color", 10)) { int m = vn < 63 ? vn : 63; memcpy(cor, v0, (size_t)m); cor[m] = 0; }
                if (kn == 12 && igual_n(k0, "stop-opacity", 12)) { int m = vn < 15 ? vn : 15; memcpy(opv, v0, (size_t)m); opv[m] = 0; }
            }
            int ok; uint32_t c32 = tela2d_cor_css(cor, &ok); if (!ok) c32 = 0xFF000000u;
            double sop; num(opv, &sop, &ok); if (!ok) sop = 1;
            uint32_t a = (uint32_t)((c32 >> 24) * sop * op + 0.5); if (a > 255) a = 255;
            tela2d_grad_parada(id_g, off, a << 24 | (c32 & 0xFFFFFF));
        }
    }
    (void)b; (void)tf;
    return id_g;
}
/* matriz do gradiente: bbox (objectBoundingBox) e gradientTransform */
static void matriz_grad(const char *id, const caixa *b, mat *m)
{
    *m = ident();
    el *g = por_id(raiz_doc, id);
    const char *un = NULL, *tf = NULL;
    for (el *q = g; q;) {
        if (!un) un = at_de(q, "gradientUnits");
        if (!tf) tf = at_de(q, "gradientTransform");
        const char *h = at_de(q, "href");
        el *n = h && h[0] == '#' ? por_id(raiz_doc, h + 1) : NULL;
        if (n == g) break;
        q = n;
    }
    if (!un || !igual(un, "userSpaceOnUse")) {
        mat bb = { { b->Mx - b->mx, 0, 0, b->My - b->my, b->mx, b->my } };
        if (bb.m[0] == 0) bb.m[0] = 1e-9;
        if (bb.m[3] == 0) bb.m[3] = 1e-9;
        *m = bb;
    }
    if (tf) { mat t = ler_transform(tf); mult(m, m, &t); }
}

static mat m_atual;
static void aplicar(const mat *m) { tela2d_matriz(T_, m->m, 1); }

/* pinta o caminho atual (já construído) com fill e stroke do estilo */
static void pintar(const estilo *st, const caixa *b, double op_extra)
{
    if (!st->visivel) return;
    double op = st->op * op_extra;
    for (int pass = 0; pass < 2; pass++) {
        int tipo = pass ? st->stroke_tipo : st->fill_tipo;
        if (!tipo) continue;
        if (pass && st->lw <= 0) continue;
        double o = op * (pass ? st->stroke_op : st->fill_op);
        int g = -1;
        if (tipo == 2) {
            const char *u = pass ? st->stroke_url : st->fill_url;
            g = u ? gradiente(u, b, 1) : -1;
            if (g < 0) {   /* paint server ausente: a cor reserva, ou nada */
                uint32_t c = pass ? st->stroke : st->fill;
                if (!(c >> 24)) continue;
                tipo = 1;
            }
        }
        if (tipo == 1) {
            uint32_t c = pass ? st->stroke : st->fill;
            uint32_t a = (uint32_t)((c >> 24) * o + 0.5); if (a > 255) a = 255;
            tela2d_cor(T_, pass, a << 24 | (c & 0xFFFFFF));
        } else {
            /* o gradiente fica no espaço da matriz atual x matriz do gradiente */
            mat mg, m2;
            matriz_grad(pass ? st->stroke_url : st->fill_url, b, &mg);
            mult(&m2, &m_atual, &mg);
            /* o caminho já está em coordenadas da tela: só troca a matriz para a pintura */
            tela2d_matriz(T_, m2.m, 1);
            tela2d_usar_grad(T_, pass, g);
            tela2d_alfa(T_, o > 1 ? 1 : o);
        }
        if (pass) {
            tela2d_linha(T_, st->lw, st->cap, st->join, st->miter);
            tela2d_tracejado(T_, st->dash, st->ndash, st->dash_desl);
            if (tipo == 2) aplicar(&m_atual);   /* largura do traço é medida no espaço do elemento */
            if (tipo == 2) { tela2d_usar_grad(T_, 1, g); }
            tela2d_tracar(T_);
        } else tela2d_preencher(T_, st->parimpar);
        tela2d_alfa(T_, 1);
        aplicar(&m_atual);
        if (g >= 0) tela2d_grad_liberar(g);
    }
}

static void desenhar_el(el *e, estilo *pai, int prof);
static void forma(el *e, const estilo *st)
{
    caixa b; memset(&b, 0, sizeof b);
    tela2d_caminho_novo(T_);
    const char *t = e->tag;
    if (igual(t, "path")) { const char *d = at_de(e, "d"); if (d) caminho(d, &b); }
    else if (igual(t, "rect")) {
        double x = medida(at_de(e, "x"), 100, 0), y = medida(at_de(e, "y"), 100, 0), w = medida(at_de(e, "width"), 100, 0), h = medida(at_de(e, "height"), 100, 0);
        if (w <= 0 || h <= 0) return;
        const char *arx = at_de(e, "rx"), *ary = at_de(e, "ry");
        double rx = medida(arx, 100, -1), ry = medida(ary, 100, -1);
        if (rx < 0) rx = ry; if (ry < 0) ry = rx; if (rx < 0) rx = ry = 0;
        if (rx > w / 2) rx = w / 2; if (ry > h / 2) ry = h / 2;
        if (rx > 0 && ry > 0) {
            P_mover(&b, x + rx, y); P_linha(&b, x + w - rx, y);
            P_arco(&b, x + w - rx, y, rx, ry, 0, 0, 1, x + w, y + ry); P_linha(&b, x + w, y + h - ry);
            P_arco(&b, x + w, y + h - ry, rx, ry, 0, 0, 1, x + w - rx, y + h); P_linha(&b, x + rx, y + h);
            P_arco(&b, x + rx, y + h, rx, ry, 0, 0, 1, x, y + h - ry); P_linha(&b, x, y + ry);
            P_arco(&b, x, y + ry, rx, ry, 0, 0, 1, x + rx, y);
        } else { P_mover(&b, x, y); P_linha(&b, x + w, y); P_linha(&b, x + w, y + h); P_linha(&b, x, y + h); }
        tela2d_fechar(T_);
    } else if (igual(t, "circle") || igual(t, "ellipse")) {
        double cx = medida(at_de(e, "cx"), 100, 0), cy = medida(at_de(e, "cy"), 100, 0), rx, ry;
        if (igual(t, "circle")) rx = ry = medida(at_de(e, "r"), 100, 0);
        else { rx = medida(at_de(e, "rx"), 100, -1); ry = medida(at_de(e, "ry"), 100, -1); if (rx < 0) rx = ry; if (ry < 0) ry = rx; }
        if (rx <= 0 || ry <= 0) return;
        tela2d_mover(T_, cx + rx, cy);
        tela2d_elipse(T_, cx, cy, rx, ry, 0, 0, 2 * PI, 0);
        tela2d_fechar(T_);
        cx_ponto(&b, cx - rx, cy - ry); cx_ponto(&b, cx + rx, cy + ry);
    } else if (igual(t, "line")) {
        P_mover(&b, medida(at_de(e, "x1"), 100, 0), medida(at_de(e, "y1"), 100, 0));
        P_linha(&b, medida(at_de(e, "x2"), 100, 0), medida(at_de(e, "y2"), 100, 0));
        estilo s2 = *st; s2.fill_tipo = 0;   /* linha não tem preenchimento */
        pintar(&s2, &b, 1);
        return;
    } else if (igual(t, "polyline") || igual(t, "polygon")) {
        const char *p = at_de(e, "points"); int ok, k = 0; double x, y;
        while (p && *p) { const char *q = num(p, &x, &ok); if (!ok) break; q = num(q, &y, &ok); if (!ok) break; if (k++) P_linha(&b, x, y); else P_mover(&b, x, y); p = q; }
        if (igual(t, "polygon")) tela2d_fechar(T_);
    } else return;
    pintar(st, &b, 1);
}
/* viewBox + preserveAspectRatio: matriz do viewport (w x h) para o conteúdo */
static mat matriz_vb(const char *vb, const char *par, double w, double h)
{
    mat m = ident();
    double v[4]; int ok, k = 0;
    for (const char *p = vb; p && k < 4; k++) { p = num(p, &v[k], &ok); if (!ok) break; }
    if (k < 4 || v[2] <= 0 || v[3] <= 0) return m;
    double sx = w / v[2], sy = h / v[3];
    int ax = 1, ay = 1, fatia = 0, nenhum = 0;   /* 0 min, 1 mid, 2 max */
    if (par) {
        if (igual_n(par, "none", 4)) nenhum = 1;
        const char *x = par; while (*x && *x != 'x') x++;
        if (*x == 'x') { ax = igual_n(x + 1, "Min", 3) ? 0 : igual_n(x + 1, "Max", 3) ? 2 : 1; const char *y = x + 4; ay = igual_n(y + 1, "Min", 3) ? 0 : igual_n(y + 1, "Max", 3) ? 2 : 1; }
        for (const char *q = par; *q; q++) if (igual_n(q, "slice", 5)) fatia = 1;
    }
    if (!nenhum) { double s = fatia ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy); sx = sy = s; }
    double tx = -v[0] * sx + (w - v[2] * sx) * ax / 2, ty = -v[1] * sy + (h - v[3] * sy) * ay / 2;
    m.m[0] = sx; m.m[3] = sy; m.m[4] = tx; m.m[5] = ty;
    return m;
}
static void recorte(const char *id, const caixa *bb)
{
    el *c = por_id(raiz_doc, id);
    if (!c || !igual(c->tag, "clipPath")) return;
    const char *un = at_de(c, "clipPathUnits");
    mat salvo = m_atual;
    if (un && igual(un, "objectBoundingBox") && bb) { mat b = { { bb->Mx - bb->mx, 0, 0, bb->My - bb->my, bb->mx, bb->my } }; mult(&m_atual, &m_atual, &b); }
    const char *tf = at_de(c, "transform");
    if (tf) { mat t = ler_transform(tf); mult(&m_atual, &m_atual, &t); }
    aplicar(&m_atual);
    tela2d_caminho_novo(T_);
    caixa b; memset(&b, 0, sizeof b);
    int parimpar = 0;
    for (el *f = c->filho; f; f = f->prox) {
        if (f->tag[0] == '#') continue;
        estilo s; memset(&s, 0, sizeof s); s.visivel = 1; estilizar(f, &s, NULL);
        parimpar = s.clip_parimpar;
        const char *t = f->tag;
        mat sv = m_atual;
        const char *ft = at_de(f, "transform");
        if (ft) { mat tt = ler_transform(ft); mult(&m_atual, &m_atual, &tt); aplicar(&m_atual); }
        if (igual(t, "path")) { const char *d = at_de(f, "d"); if (d) caminho(d, &b); }
        else if (igual(t, "rect")) { double x = medida(at_de(f, "x"), 100, 0), y = medida(at_de(f, "y"), 100, 0), w = medida(at_de(f, "width"), 100, 0), h = medida(at_de(f, "height"), 100, 0); P_mover(&b, x, y); P_linha(&b, x + w, y); P_linha(&b, x + w, y + h); P_linha(&b, x, y + h); tela2d_fechar(T_); }
        else if (igual(t, "circle")) { double cx = medida(at_de(f, "cx"), 100, 0), cy = medida(at_de(f, "cy"), 100, 0), r = medida(at_de(f, "r"), 100, 0); tela2d_mover(T_, cx + r, cy); tela2d_elipse(T_, cx, cy, r, r, 0, 0, 2 * PI, 0); tela2d_fechar(T_); }
        else if (igual(t, "ellipse")) { double cx = medida(at_de(f, "cx"), 100, 0), cy = medida(at_de(f, "cy"), 100, 0), rx = medida(at_de(f, "rx"), 100, 0), ry = medida(at_de(f, "ry"), 100, 0); tela2d_mover(T_, cx + rx, cy); tela2d_elipse(T_, cx, cy, rx, ry, 0, 0, 2 * PI, 0); tela2d_fechar(T_); }
        else if (igual(t, "polygon")) { const char *p = at_de(f, "points"); int ok, k = 0; double x, y; while (p && *p) { const char *q = num(p, &x, &ok); if (!ok) break; q = num(q, &y, &ok); if (!ok) break; if (k++) P_linha(&b, x, y); else P_mover(&b, x, y); p = q; } tela2d_fechar(T_); }
        m_atual = sv; aplicar(&m_atual);
    }
    tela2d_recortar(T_, parimpar);
    m_atual = salvo; aplicar(&m_atual);
}
static void texto(el *e, const estilo *st)
{
    double x = medida(at_de(e, "x"), 100, 0), y = medida(at_de(e, "y"), 100, 0);
    for (el *c = e->filho; c; c = c->prox) {
        const char *s = NULL;
        if (c->tag[0] == '#' && c->texto) s = c->texto;
        else if (igual(c->tag, "tspan") && c->filho && c->filho->texto) { s = c->filho->texto; const char *tx = at_de(c, "x"), *ty = at_de(c, "y"); if (tx) x = medida(tx, 100, x); if (ty) y = medida(ty, 100, y); }
        if (!s) continue;
        while (esp(*s)) s++;
        if (!*s) continue;
        if (st->fill_tipo == 1) {
            uint32_t a = (uint32_t)((st->fill >> 24) * st->fill_op * st->op + 0.5);
            tela2d_cor(T_, 0, (a > 255 ? 255 : a) << 24 | (st->fill & 0xFFFFFF));
            tela2d_texto(T_, s, x, y, st->tam_fonte, st->ancora, 0, 0, 0);
        }
    }
}
/* <g opacity> e afins: o grupo inteiro numa tela à parte, composta com a opacidade */
static void desenhar_filhos(el *e, estilo *st, int prof) { for (el *c = e->filho; c; c = c->prox) if (c->tag[0] != '#') desenhar_el(c, st, prof + 1); }
static void desenhar_el(el *e, estilo *pai, int prof)
{
    if (prof > 40) return;
    const char *t = e->tag;
    if (igual(t, "defs") || igual(t, "clipPath") || igual(t, "mask") || igual(t, "symbol") || igual(t, "linearGradient") || igual(t, "radialGradient") ||
        igual(t, "pattern") || igual(t, "filter") || igual(t, "style") || igual(t, "title") || igual(t, "desc") || igual(t, "metadata") || igual(t, "marker") || igual(t, "script")) return;
    estilo st; estilizar(e, &st, pai);
    const char *disp = at_de(e, "display");
    if (disp && igual(disp, "none")) return;
    const char *sty = at_de(e, "style");
    if (sty) for (const char *q = sty; *q; q++) if (igual_n(q, "display:none", 12) || igual_n(q, "display: none", 13)) return;
    mat salvo = m_atual;
    const char *tf = at_de(e, "transform");
    if (tf) { mat m = ler_transform(tf); mult(&m_atual, &m_atual, &m); }
    int usa_clip = 0;
    const char *cp = at_de(e, "clip-path");
    if (cp && igual_n(cp, "url(", 4)) {
        const char *p = cp + 4; while (*p == '#' || *p == '\'' || *p == '"') p++;
        const char *q = p; while (*q && *q != ')' && *q != '\'' && *q != '"') q++;
        tela2d_salvar(T_); usa_clip = 1;
        recorte(dup(p, (int)(q - p)), NULL);
    }
    aplicar(&m_atual);
    int grupo = (igual(t, "g") || igual(t, "svg") || igual(t, "use") || igual(t, "a")) && st.op < 0.999;
    int t_salva = T_, tg = -1, gl = 0, ga = 0;
    double op_g = st.op;
    if (grupo) {
        const uint32_t *px0 = tela2d_pixels(T_, &gl, &ga);
        (void)px0;
        tg = tela2d_nova(gl, ga);
        if (tg >= 0) { T_ = tg; aplicar(&m_atual); st.op = 1; }
    }
    if (igual(t, "g") || igual(t, "a") || igual(t, "switch")) desenhar_filhos(e, &st, prof);
    else if (igual(t, "svg") && prof > 0) {   /* svg aninhado: novo viewport */
        double x = medida(at_de(e, "x"), 100, 0), y = medida(at_de(e, "y"), 100, 0), w = medida(at_de(e, "width"), 100, 100), h = medida(at_de(e, "height"), 100, 100);
        mat m = { { 1, 0, 0, 1, x, y } }; mult(&m_atual, &m_atual, &m);
        const char *vb = at_de(e, "viewBox");
        if (vb) { mat v = matriz_vb(vb, at_de(e, "preserveAspectRatio"), w, h); mult(&m_atual, &m_atual, &v); }
        aplicar(&m_atual);
        desenhar_filhos(e, &st, prof);
    } else if (igual(t, "use")) {
        const char *h = at_de(e, "href");
        el *r = h && h[0] == '#' ? por_id(raiz_doc, h + 1) : NULL;
        if (r && r != e) {
            double x = medida(at_de(e, "x"), 100, 0), y = medida(at_de(e, "y"), 100, 0);
            mat m = { { 1, 0, 0, 1, x, y } }; mult(&m_atual, &m_atual, &m);
            aplicar(&m_atual);
            if (igual(r->tag, "symbol")) {
                const char *vb = at_de(r, "viewBox");
                double w = medida(at_de(e, "width"), 100, 0), hh = medida(at_de(e, "height"), 100, 0);
                if (vb && w > 0 && hh > 0) { mat v = matriz_vb(vb, at_de(r, "preserveAspectRatio"), w, hh); mult(&m_atual, &m_atual, &v); aplicar(&m_atual); }
                estilo s2; estilizar(r, &s2, &st);
                desenhar_filhos(r, &s2, prof);
            } else desenhar_el(r, &st, prof + 1);
        }
    } else if (igual(t, "text")) texto(e, &st);
    else forma(e, &st);
    if (grupo && tg >= 0) {
        T_ = t_salva;
        double id6[6] = { 1, 0, 0, 1, 0, 0 };
        tela2d_matriz(T_, id6, 1);
        tela2d_alfa(T_, op_g < 0 ? 0 : op_g);
        tela2d_desenhar(T_, tg, 0, 0, gl, ga, 0, 0, gl, ga);
        tela2d_alfa(T_, 1);
        tela2d_liberar(tg);
    }
    if (usa_clip) tela2d_restaurar(T_);
    m_atual = salvo;
    aplicar(&m_atual);
}

static el *achar_svg(el *r) { for (el *c = r->filho; c; c = c->prox) { if (igual(c->tag, "svg")) return c; el *x = achar_svg(c); if (x) return x; } return NULL; }
static void css_de(el *e) { for (el *c = e->filho; c; c = c->prox) { if (igual(c->tag, "style")) for (el *t = c->filho; t; t = t->prox) if (t->texto) css_ler(t->texto, t->ntexto); css_de(c); } }

bool svg_tamanho(const char *xml, int n, int *l, int *a)
{
    el *r = xml_ler(xml, n), *s = r ? achar_svg(r) : NULL;
    bool ok = false;
    if (s) {
        const char *w = at_de(s, "width"), *h = at_de(s, "height"), *vb = at_de(s, "viewBox");
        double v[4] = { 0, 0, 0, 0 }; int k = 0, o;
        for (const char *p = vb; p && k < 4; k++) { p = num(p, &v[k], &o); if (!o) break; }
        double W = w && !acha_char(w, '%') ? medida(w, 100, 0) : 0, H = h && !acha_char(h, '%') ? medida(h, 100, 0) : 0;
        if (W <= 0 && H > 0 && k == 4 && v[3] > 0) W = H * v[2] / v[3];
        if (H <= 0 && W > 0 && k == 4 && v[2] > 0) H = W * v[3] / v[2];
        if (W <= 0 || H <= 0) { if (k == 4 && v[2] > 0 && v[3] > 0) { W = v[2]; H = v[3]; } else { W = 300; H = 150; } }
        *l = (int)(W + 0.5); *a = (int)(H + 0.5);
        if (*l < 1) *l = 1;
        if (*a < 1) *a = 1;
        ok = true;
    }
    mem_fim(); regras = NULL; n_regras = 0;
    return ok;
}

uint32_t *svg_rasterizar(const char *xml, int n, int l, int a, uint32_t cor)
{
    if (l <= 0 || a <= 0 || l > 4096 || a > 4096) return NULL;
    regras = NULL; n_regras = 0;
    el *r = xml_ler(xml, n), *s = r ? achar_svg(r) : NULL;
    uint32_t *px = NULL;
    if (s) {
        raiz_doc = r;
        css_de(r);
        T_ = tela2d_nova(l, a);
        if (T_ >= 0) {
            estilo st; memset(&st, 0, sizeof st);
            st.cor = cor; st.fill_tipo = 1; st.fill = 0xFF000000u; st.stroke_tipo = 0;
            st.fill_op = st.stroke_op = st.op = 1; st.lw = 1; st.miter = 4; st.visivel = 1; st.tam_fonte = 16;
            const char *vb = at_de(s, "viewBox");
            m_atual = vb ? matriz_vb(vb, at_de(s, "preserveAspectRatio"), l, a) : ident();
            if (!vb) {   /* sem viewBox: width/height dão a escala */
                int wl, wa;
                const char *w = at_de(s, "width"), *h = at_de(s, "height");
                double W = w && !acha_char(w, '%') ? medida(w, 100, l) : l, H = h && !acha_char(h, '%') ? medida(h, 100, a) : a;
                (void)wl; (void)wa;
                if (W > 0 && H > 0) { m_atual.m[0] = l / W; m_atual.m[3] = a / H; }
            }
            estilo raiz; estilizar(s, &raiz, &st);
            aplicar(&m_atual);
            for (el *c = s->filho; c; c = c->prox) if (c->tag[0] != '#') desenhar_el(c, &raiz, 1);
            int tl, ta;
            const uint32_t *tp = tela2d_pixels(T_, &tl, &ta);
            if (tp && tl == l && ta == a) {
                px = aloca((unsigned)(l * a) * 4);
                if (px) memcpy(px, tp, (size_t)(l * a) * 4);
            }
            tela2d_liberar(T_);
        }
    }
    mem_fim(); regras = NULL; n_regras = 0;
    return px;
}
