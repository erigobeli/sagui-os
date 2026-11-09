/* canvas2d.c - o <canvas> 2D do Cipó (HTML, "The 2D rendering context").
 *
 * Cada tela guarda pixels ARGB sem pré-multiplicar (alfa no byte de cima),
 * como as imagens do motor, que as mostra direto. Caminhos são guardados já
 * em coordenadas da tela (a matriz vale no momento em que cada ponto entra,
 * como manda a especificação) e preenchidos por acumulação de área com
 * antialias (o mesmo método do rasterizador de fontes). O traço vira
 * polígonos (segmentos, junções e pontas) no espaço do usuário, todos com a
 * mesma orientação, e é preenchido pela regra nonzero.
 *
 * Texto: sem acesso às fontes da página, usa a fonte do sistema escalada,
 * que é o que um navegador faz quando a fonte pedida não está carregada. */
#include <sagui.h>

extern const unsigned char fonte_sagui[256][16];

/* ------------------------------------------------------------ matemática (x87) */
static double c_sqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return x > 0 ? r : 0; }
static double c_sin(double x) { double r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static double c_cos(double x) { double r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
static double c_atan2(double y, double x) { double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static double c_abs(double x) { return x < 0 ? -x : x; }
static double c_floor(double x) { int i = (int)x; return (double)(x < i ? i - 1 : i); }
static double c_ceil(double x) { int i = (int)x; return (double)(x > i ? i + 1 : i); }
static double c_min(double a, double b) { return a < b ? a : b; }
static double c_max(double a, double b) { return a > b ? a : b; }
#define PI 3.14159265358979323846

/* ------------------------------------------------------------ estado */
typedef struct { double m[6]; } mat_t;   /* a b c d e f: x' = a x + c y + e, y' = b x + d y + f */

typedef struct grad {
    int radial, usado, n;
    double p[6];                 /* linear: x0 y0 x1 y1; radial: x0 y0 r0 x1 y1 r1 */
    double pos[32]; uint32_t cor[32];
} grad_t;

typedef struct {
    mat_t m;
    double alfa;
    int comp;
    uint32_t cor[2]; int grad[2];   /* [0] preenchimento, [1] traço; grad -1 = cor */
    double lw, miter; int cap, join;
    uint8_t *recorte;               /* máscara (l x a), NULL = sem recorte */
    int suave;                      /* imageSmoothingEnabled */
    double traco[16]; int ntraco; double traco_desl;   /* setLineDash / lineDashOffset */
} estado_t;

typedef struct { double x, y; } pt_t;
typedef struct { int ini, n, fechado; } sub_t;

typedef struct {
    int usada, l, a;
    uint32_t *px;
    estado_t st, pilha[32]; int np;
    pt_t *pts; int npts, cpts;
    sub_t *subs; int nsubs, csubs;
    int aberto;                     /* há subcaminho aberto (moveTo feito) */
} tela_t;

#define MAX_TELAS 64
static tela_t telas[MAX_TELAS];
static grad_t grads[256];
static unsigned versao;

unsigned tela2d_versao(void) { return versao; }
void tela2d_mudou(void) { versao++; }
/* WebGL desenha num buffer próprio e só copia para a tela quando alguém lê */
void (*tela2d_sincronizar)(int id);
uint32_t *tela2d_px_escrita(int id, int *l, int *a);

static tela_t *T(int id) { return id >= 0 && id < MAX_TELAS && telas[id].usada ? &telas[id] : NULL; }

static void estado_inicial(estado_t *s)
{
    memset(s, 0, sizeof *s);
    s->m.m[0] = s->m.m[3] = 1;
    s->alfa = 1;
    s->cor[0] = s->cor[1] = 0xFF000000u;
    s->grad[0] = s->grad[1] = -1;
    s->lw = 1; s->miter = 10;
    s->suave = 1;
}

static void tela_limpar_estado(tela_t *t)
{
    if (t->st.recorte) libera(t->st.recorte);
    for (int i = 0; i < t->np; i++) if (t->pilha[i].recorte) libera(t->pilha[i].recorte);
    t->np = 0;
    estado_inicial(&t->st);
    t->npts = t->nsubs = 0; t->aberto = 0;
}

void tela2d_tamanho(int id, int l, int a);
int tela2d_nova(int l, int a)
{
    for (int i = 0; i < MAX_TELAS; i++) if (!telas[i].usada) {
        tela_t *t = &telas[i];
        memset(t, 0, sizeof *t);
        t->usada = 1;
        estado_inicial(&t->st);
        tela2d_tamanho(i, l, a);
        return i;
    }
    return -1;
}

/* Mudar width/height zera os pixels e o estado (HTML: "set bitmap dimensions"). */
void tela2d_tamanho(int id, int l, int a)
{
    tela_t *t = T(id);
    if (!t) return;
    if (l < 0) l = 0;
    if (a < 0) a = 0;
    if (l > 4096) l = 4096;
    if (a > 4096) a = 4096;
    if ((long)l * a > 4096L * 2048) a = 4096 * 2048 / (l ? l : 1);
    if (t->px) libera(t->px);
    t->px = l && a ? aloca((unsigned)(l * a) * 4) : NULL;
    if (t->px) memset(t->px, 0, (unsigned)(l * a) * 4);
    t->l = t->px ? l : 0; t->a = t->px ? a : 0;
    tela_limpar_estado(t);
    versao++;
}

void tela2d_liberar_todas(void)
{
    for (int i = 0; i < MAX_TELAS; i++) if (telas[i].usada) {
        tela_t *t = &telas[i];
        tela_limpar_estado(t);
        if (t->px) libera(t->px);
        if (t->pts) libera(t->pts);
        if (t->subs) libera(t->subs);
        memset(t, 0, sizeof *t);
    }
    memset(grads, 0, sizeof grads);
}

uint32_t *tela2d_px_escrita(int id, int *l, int *a)
{
    tela_t *t = T(id);
    if (!t || !t->px) { *l = *a = 0; return NULL; }
    *l = t->l; *a = t->a; versao++;
    return t->px;
}
const uint32_t *tela2d_pixels(int id, int *l, int *a)
{
    if (tela2d_sincronizar) tela2d_sincronizar(id);
    tela_t *t = T(id);
    if (!t || !t->px) { *l = *a = 0; return NULL; }
    *l = t->l; *a = t->a;
    return t->px;
}

/* ------------------------------------------------------------ cores CSS */
static const struct { const char *n; uint32_t c; } nomes_cor[] = {
    {"aliceblue",0xf0f8ff},{"antiquewhite",0xfaebd7},{"aqua",0x00ffff},{"aquamarine",0x7fffd4},{"azure",0xf0ffff},{"beige",0xf5f5dc},
    {"bisque",0xffe4c4},{"black",0x000000},{"blanchedalmond",0xffebcd},{"blue",0x0000ff},{"blueviolet",0x8a2be2},{"brown",0xa52a2a},
    {"burlywood",0xdeb887},{"cadetblue",0x5f9ea0},{"chartreuse",0x7fff00},{"chocolate",0xd2691e},{"coral",0xff7f50},{"cornflowerblue",0x6495ed},
    {"cornsilk",0xfff8dc},{"crimson",0xdc143c},{"cyan",0x00ffff},{"darkblue",0x00008b},{"darkcyan",0x008b8b},{"darkgoldenrod",0xb8860b},
    {"darkgray",0xa9a9a9},{"darkgreen",0x006400},{"darkgrey",0xa9a9a9},{"darkkhaki",0xbdb76b},{"darkmagenta",0x8b008b},{"darkolivegreen",0x556b2f},
    {"darkorange",0xff8c00},{"darkorchid",0x9932cc},{"darkred",0x8b0000},{"darksalmon",0xe9967a},{"darkseagreen",0x8fbc8f},{"darkslateblue",0x483d8b},
    {"darkslategray",0x2f4f4f},{"darkslategrey",0x2f4f4f},{"darkturquoise",0x00ced1},{"darkviolet",0x9400d3},{"deeppink",0xff1493},{"deepskyblue",0x00bfff},
    {"dimgray",0x696969},{"dimgrey",0x696969},{"dodgerblue",0x1e90ff},{"firebrick",0xb22222},{"floralwhite",0xfffaf0},{"forestgreen",0x228b22},
    {"fuchsia",0xff00ff},{"gainsboro",0xdcdcdc},{"ghostwhite",0xf8f8ff},{"gold",0xffd700},{"goldenrod",0xdaa520},{"gray",0x808080},
    {"green",0x008000},{"greenyellow",0xadff2f},{"grey",0x808080},{"honeydew",0xf0fff0},{"hotpink",0xff69b4},{"indianred",0xcd5c5c},
    {"indigo",0x4b0082},{"ivory",0xfffff0},{"khaki",0xf0e68c},{"lavender",0xe6e6fa},{"lavenderblush",0xfff0f5},{"lawngreen",0x7cfc00},
    {"lemonchiffon",0xfffacd},{"lightblue",0xadd8e6},{"lightcoral",0xf08080},{"lightcyan",0xe0ffff},{"lightgoldenrodyellow",0xfafad2},{"lightgray",0xd3d3d3},
    {"lightgreen",0x90ee90},{"lightgrey",0xd3d3d3},{"lightpink",0xffb6c1},{"lightsalmon",0xffa07a},{"lightseagreen",0x20b2aa},{"lightskyblue",0x87cefa},
    {"lightslategray",0x778899},{"lightslategrey",0x778899},{"lightsteelblue",0xb0c4de},{"lightyellow",0xffffe0},{"lime",0x00ff00},{"limegreen",0x32cd32},
    {"linen",0xfaf0e6},{"magenta",0xff00ff},{"maroon",0x800000},{"mediumaquamarine",0x66cdaa},{"mediumblue",0x0000cd},{"mediumorchid",0xba55d3},
    {"mediumpurple",0x9370db},{"mediumseagreen",0x3cb371},{"mediumslateblue",0x7b68ee},{"mediumspringgreen",0x00fa9a},{"mediumturquoise",0x48d1cc},{"mediumvioletred",0xc71585},
    {"midnightblue",0x191970},{"mintcream",0xf5fffa},{"mistyrose",0xffe4e1},{"moccasin",0xffe4b5},{"navajowhite",0xffdead},{"navy",0x000080},
    {"oldlace",0xfdf5e6},{"olive",0x808000},{"olivedrab",0x6b8e23},{"orange",0xffa500},{"orangered",0xff4500},{"orchid",0xda70d6},
    {"palegoldenrod",0xeee8aa},{"palegreen",0x98fb98},{"paleturquoise",0xafeeee},{"palevioletred",0xdb7093},{"papayawhip",0xffefd5},{"peachpuff",0xffdab9},
    {"peru",0xcd853f},{"pink",0xffc0cb},{"plum",0xdda0dd},{"powderblue",0xb0e0e6},{"purple",0x800080},{"rebeccapurple",0x663399},
    {"red",0xff0000},{"rosybrown",0xbc8f8f},{"royalblue",0x4169e1},{"saddlebrown",0x8b4513},{"salmon",0xfa8072},{"sandybrown",0xf4a460},
    {"seagreen",0x2e8b57},{"seashell",0xfff5ee},{"sienna",0xa0522d},{"silver",0xc0c0c0},{"skyblue",0x87ceeb},{"slateblue",0x6a5acd},
    {"slategray",0x708090},{"slategrey",0x708090},{"snow",0xfffafa},{"springgreen",0x00ff7f},{"steelblue",0x4682b4},{"tan",0xd2b48c},
    {"teal",0x008080},{"thistle",0xd8bfd8},{"tomato",0xff6347},{"turquoise",0x40e0d0},{"violet",0xee82ee},{"wheat",0xf5deb3},
    {"white",0xffffff},{"whitesmoke",0xf5f5f5},{"yellow",0xffff00},{"yellowgreen",0x9acd32},
};

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static int arred(double x) { return (int)(x < 0 ? x - 0.5 : x + 0.5); }
static int faixa(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* número com % opcional; devolve o valor e se era percentual */
static const char *num(const char *p, double *v, int *pct, int *ok)
{
    while (*p == ' ' || *p == ',' || *p == '/') p++;
    double s = 1, r = 0, f = 0.1; int dig = 0;
    if (*p == '-') { s = -1; p++; } else if (*p == '+') p++;
    while (*p >= '0' && *p <= '9') { r = r * 10 + (*p++ - '0'); dig = 1; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { r += f * (*p++ - '0'); f /= 10; dig = 1; } }
    if ((*p | 32) == 'e' && (p[1] == '-' || p[1] == '+' || (p[1] >= '0' && p[1] <= '9'))) {
        p++; int es = 1, e = 0; if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
        while (*p >= '0' && *p <= '9') e = e * 10 + (*p++ - '0');
        while (e--) r = es > 0 ? r * 10 : r / 10;
    }
    *pct = 0;
    if (*p == '%') { *pct = 1; p++; }
    else if (p[0] == 'd' && p[1] == 'e' && p[2] == 'g') p += 3;
    else if (p[0] == 't' && p[1] == 'u' && p[2] == 'r' && p[3] == 'n') { r *= 360; p += 4; }
    else if (p[0] == 'r' && p[1] == 'a' && p[2] == 'd') { r = r * 180 / PI; p += 3; }
    *v = s * r; *ok = dig;
    return p;
}

static double hue(double p, double q, double t)
{
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

/* Cor CSS (nomes, #hex, rgb[a](), hsl[a]()); ok = 0 se não entendeu. */
uint32_t tela2d_cor_css(const char *s, int *ok)
{
    *ok = 0;
    while (*s == ' ') s++;
    char b[32]; int n = 0;
    while (s[n] && s[n] != '(' && n < 31) { b[n] = (char)(s[n] | ((s[n] >= 'A' && s[n] <= 'Z') ? 32 : 0)); n++; }
    while (n && b[n - 1] == ' ') n--;
    b[n] = 0;
    if (s[0] == '#') {
        int d[8], k = 0;
        for (const char *p = s + 1; *p && *p != ' ' && k < 8; p++) { int v = hexv(*p); if (v < 0) return 0; d[k++] = v; }
        if (k == 3 || k == 4) { *ok = 1; return (uint32_t)((k == 4 ? d[3] * 17 : 255) << 24 | d[0] * 17 << 16 | d[1] * 17 << 8 | d[2] * 17); }
        if (k == 6 || k == 8) { *ok = 1; return (uint32_t)((k == 8 ? d[6] * 16 + d[7] : 255) << 24 | (d[0] * 16 + d[1]) << 16 | (d[2] * 16 + d[3]) << 8 | (d[4] * 16 + d[5])); }
        return 0;
    }
    if (!compara_texto(b, "transparent")) { *ok = 1; return 0; }
    if (!compara_texto(b, "currentcolor")) { *ok = 1; return 0xFF000000u; }
    const char *par = acha_char(s, '(');
    if (par && (!compara_texto(b, "rgb") || !compara_texto(b, "rgba") || !compara_texto(b, "hsl") || !compara_texto(b, "hsla"))) {
        double v[4] = { 0, 0, 0, 1 }; int pc[4] = { 0 }, k = 0, okn;
        const char *p = par + 1;
        while (k < 4) {
            while (*p == ' ') p++;
            if (*p == ')' || !*p) break;
            p = num(p, &v[k], &pc[k], &okn);
            if (!okn) return 0;
            k++;
        }
        if (k < 3) return 0;
        double a = pc[3] ? v[3] / 100 : v[3];
        int A = faixa(arred(a * 255));
        if (b[0] == 'r') {
            int c[3];
            for (int i = 0; i < 3; i++) c[i] = faixa(arred(pc[i] ? v[i] * 255 / 100 : v[i]));
            *ok = 1;
            return (uint32_t)A << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
        }
        double h = v[0] / 360; h -= c_floor(h);
        double sa = v[1] / 100, l = v[2] / 100;
        sa = c_min(1, c_max(0, sa)); l = c_min(1, c_max(0, l));
        double q = l < 0.5 ? l * (1 + sa) : l + sa - l * sa, pp = 2 * l - q;
        int R = faixa(arred(hue(pp, q, h + 1.0 / 3) * 255)), G = faixa(arred(hue(pp, q, h) * 255)), B = faixa(arred(hue(pp, q, h - 1.0 / 3) * 255));
        *ok = 1;
        return (uint32_t)A << 24 | (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
    }
    int lo = 0, hi = (int)(sizeof nomes_cor / sizeof nomes_cor[0]) - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2, c = compara_texto(b, nomes_cor[m].n);
        if (!c) { *ok = 1; return 0xFF000000u | nomes_cor[m].c; }
        if (c < 0) hi = m - 1; else lo = m + 1;
    }
    return 0;
}

/* ------------------------------------------------------------ estado: API */
void tela2d_salvar(int id)
{
    tela_t *t = T(id);
    if (!t || t->np >= 32) return;
    estado_t s = t->st;
    if (s.recorte) {   /* a máscara é copiada: restore volta à de antes */
        uint8_t *c = aloca((unsigned)(t->l * t->a));
        if (c) memcpy(c, s.recorte, (unsigned)(t->l * t->a));
        s.recorte = c;
    }
    t->pilha[t->np++] = s;
}
void tela2d_restaurar(int id)
{
    tela_t *t = T(id);
    if (!t || !t->np) return;
    if (t->st.recorte) libera(t->st.recorte);
    t->st = t->pilha[--t->np];
}
static void mat_mult(mat_t *r, const mat_t *a, const mat_t *b)   /* r = a * b (b aplicada primeiro) */
{
    mat_t o;
    o.m[0] = a->m[0] * b->m[0] + a->m[2] * b->m[1];
    o.m[1] = a->m[1] * b->m[0] + a->m[3] * b->m[1];
    o.m[2] = a->m[0] * b->m[2] + a->m[2] * b->m[3];
    o.m[3] = a->m[1] * b->m[2] + a->m[3] * b->m[3];
    o.m[4] = a->m[0] * b->m[4] + a->m[2] * b->m[5] + a->m[4];
    o.m[5] = a->m[1] * b->m[4] + a->m[3] * b->m[5] + a->m[5];
    *r = o;
}
/* definir = setTransform; senão transform (multiplica pela direita) */
void tela2d_matriz(int id, const double m[6], int definir)
{
    tela_t *t = T(id);
    if (!t) return;
    for (int i = 0; i < 6; i++) if (m[i] != m[i] || m[i] > 1e300 || m[i] < -1e300) return;   /* NaN/inf: ignora */
    mat_t b; memcpy(b.m, m, sizeof b.m);
    if (definir) t->st.m = b; else mat_mult(&t->st.m, &t->st.m, &b);
}
void tela2d_ler_matriz(int id, double m[6]) { tela_t *t = T(id); if (t) memcpy(m, t->st.m.m, sizeof t->st.m.m); }
void tela2d_alfa(int id, double a) { tela_t *t = T(id); if (t && a >= 0 && a <= 1) t->st.alfa = a; }
void tela2d_composicao(int id, int op) { tela_t *t = T(id); if (t) t->st.comp = op; }
void tela2d_cor(int id, int traco, uint32_t argb) { tela_t *t = T(id); if (t) { t->st.cor[traco & 1] = argb; t->st.grad[traco & 1] = -1; } }
void tela2d_usar_grad(int id, int traco, int g) { tela_t *t = T(id); if (t && g >= 0 && g < 256 && grads[g].usado) t->st.grad[traco & 1] = g; }
void tela2d_linha(int id, double lw, int cap, int join, double miter)
{
    tela_t *t = T(id);
    if (!t) return;
    if (lw > 0) t->st.lw = lw;
    t->st.cap = cap; t->st.join = join;
    if (miter > 0) t->st.miter = miter;
}
void tela2d_suave(int id, int s) { tela_t *t = T(id); if (t) t->st.suave = s; }
void tela2d_tracejado(int id, const double *p, int n, double desl)
{
    tela_t *t = T(id);
    if (!t) return;
    double soma = 0;
    for (int i = 0; i < n && i < 16; i++) soma += p[i];
    if (n > 16 || soma <= 0) n = 0;   /* tudo zero: traço contínuo */
    for (int i = 0; i < n; i++) t->st.traco[i] = p[i];
    t->st.ntraco = n; t->st.traco_desl = desl;
}

int tela2d_grad_novo(int radial, const double p[6])
{
    for (int i = 0; i < 256; i++) if (!grads[i].usado) {
        memset(&grads[i], 0, sizeof grads[i]);
        grads[i].usado = 1; grads[i].radial = radial;
        memcpy(grads[i].p, p, sizeof grads[i].p);
        return i;
    }
    return -1;
}
void tela2d_grad_parada(int g, double pos, uint32_t argb)
{
    if (g < 0 || g >= 256 || !grads[g].usado || grads[g].n >= 32) return;
    grad_t *G = &grads[g];
    if (pos < 0) pos = 0;
    if (pos > 1) pos = 1;
    int i = G->n;   /* mesma posição: a nova vem depois (fica a ordem de inclusão) */
    while (i > 0 && G->pos[i - 1] > pos) { G->pos[i] = G->pos[i - 1]; G->cor[i] = G->cor[i - 1]; i--; }
    G->pos[i] = pos; G->cor[i] = argb; G->n++;
}

/* ------------------------------------------------------------ caminho */
static void por_pt(tela_t *t, double x, double y)
{
    if (t->npts == t->cpts) {
        int c = t->cpts ? t->cpts * 2 : 256;
        pt_t *n = realoca(t->pts, (unsigned)c * sizeof(pt_t));
        if (!n) return;
        t->pts = n; t->cpts = c;
    }
    t->pts[t->npts].x = x; t->pts[t->npts].y = y;
    t->npts++;
    t->subs[t->nsubs - 1].n++;
}
static void novo_sub(tela_t *t)
{
    if (t->nsubs == t->csubs) {
        int c = t->csubs ? t->csubs * 2 : 32;
        sub_t *n = realoca(t->subs, (unsigned)c * sizeof(sub_t));
        if (!n) return;
        t->subs = n; t->csubs = c;
    }
    t->subs[t->nsubs].ini = t->npts; t->subs[t->nsubs].n = 0; t->subs[t->nsubs].fechado = 0;
    t->nsubs++;
}
static void dev(const tela_t *t, double x, double y, double *dx, double *dy)
{
    const double *m = t->st.m.m;
    *dx = m[0] * x + m[2] * y + m[4];
    *dy = m[1] * x + m[3] * y + m[5];
}
static int ok_num(double v) { return v == v && v < 1e30 && v > -1e30; }

void tela2d_caminho_novo(int id) { tela_t *t = T(id); if (t) { t->npts = t->nsubs = 0; t->aberto = 0; } }
void tela2d_mover(int id, double x, double y)
{
    tela_t *t = T(id);
    if (!t || !ok_num(x) || !ok_num(y)) return;
    double dx, dy; dev(t, x, y, &dx, &dy);
    novo_sub(t); por_pt(t, dx, dy); t->aberto = 1;
}
/* ponto em coordenadas da tela (já transformado) */
static void linha_dev(tela_t *t, double dx, double dy)
{
    if (!t->aberto) { novo_sub(t); t->aberto = 1; }
    por_pt(t, dx, dy);
}
void tela2d_linha_para(int id, double x, double y)
{
    tela_t *t = T(id);
    if (!t || !ok_num(x) || !ok_num(y)) return;
    double dx, dy; dev(t, x, y, &dx, &dy);
    linha_dev(t, dx, dy);
}
static void ultimo(tela_t *t, double *x, double *y)
{
    if (t->aberto && t->npts) { *x = t->pts[t->npts - 1].x; *y = t->pts[t->npts - 1].y; } else { *x = *y = 0; }
}
/* escala aproximada da matriz (para decidir quantos segmentos) */
static double escala(const tela_t *t) { const double *m = t->st.m.m; return c_sqrt(c_abs(m[0] * m[3] - m[1] * m[2])) + 1e-9; }

void tela2d_quad(int id, double cx, double cy, double x, double y)
{
    tela_t *t = T(id);
    if (!t || !ok_num(cx) || !ok_num(cy) || !ok_num(x) || !ok_num(y)) return;
    if (!t->aberto) { tela2d_mover(id, cx, cy); }
    double x0, y0, x1, y1, x2, y2;
    ultimo(t, &x0, &y0); dev(t, cx, cy, &x1, &y1); dev(t, x, y, &x2, &y2);
    double len = c_abs(x1 - x0) + c_abs(y1 - y0) + c_abs(x2 - x1) + c_abs(y2 - y1);
    int n = (int)(len / 3) + 2; if (n > 64) n = 64;
    for (int i = 1; i <= n; i++) {
        double s = (double)i / n, u = 1 - s;
        linha_dev(t, u * u * x0 + 2 * u * s * x1 + s * s * x2, u * u * y0 + 2 * u * s * y1 + s * s * y2);
    }
}
void tela2d_bezier(int id, double c1x, double c1y, double c2x, double c2y, double x, double y)
{
    tela_t *t = T(id);
    if (!t || !ok_num(c1x) || !ok_num(c1y) || !ok_num(c2x) || !ok_num(c2y) || !ok_num(x) || !ok_num(y)) return;
    if (!t->aberto) tela2d_mover(id, c1x, c1y);
    double x0, y0, x1, y1, x2, y2, x3, y3;
    ultimo(t, &x0, &y0); dev(t, c1x, c1y, &x1, &y1); dev(t, c2x, c2y, &x2, &y2); dev(t, x, y, &x3, &y3);
    double len = c_abs(x1 - x0) + c_abs(y1 - y0) + c_abs(x2 - x1) + c_abs(y2 - y1) + c_abs(x3 - x2) + c_abs(y3 - y2);
    int n = (int)(len / 3) + 2; if (n > 96) n = 96;
    for (int i = 1; i <= n; i++) {
        double s = (double)i / n, u = 1 - s;
        double a = u * u * u, b = 3 * u * u * s, c = 3 * u * s * s, d = s * s * s;
        linha_dev(t, a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3);
    }
}
/* elipse (centro, raios, rotação) de a0 a a1; o primeiro ponto liga por linha ao anterior */
void tela2d_elipse(int id, double x, double y, double rx, double ry, double rot, double a0, double a1, int anti)
{
    tela_t *t = T(id);
    if (!t || !ok_num(x) || !ok_num(y) || !ok_num(rx) || !ok_num(ry) || !ok_num(a0) || !ok_num(a1) || rx < 0 || ry < 0) return;
    double varre;
    double dois = 2 * PI;
    if (!anti && a1 - a0 >= dois) varre = dois;
    else if (anti && a0 - a1 >= dois) varre = -dois;
    else {
        varre = a1 - a0;
        double k = varre / dois; varre -= c_floor(k) * dois;   /* em [0, 2pi) */
        if (anti) { if (varre > 0) varre -= dois; }
        if (!anti && varre == 0 && a1 != a0) varre = 0;
    }
    double r = (rx > ry ? rx : ry) * escala(t);
    int n = (int)(c_abs(varre) / dois * (r < 4 ? 8 : r * 0.9 + 8)) + 1;
    if (n > 360) n = 360;
    double cr = c_cos(rot), sr = c_sin(rot);
    for (int i = 0; i <= n; i++) {
        double a = a0 + varre * i / n, ex = rx * c_cos(a), ey = ry * c_sin(a);
        double ux = x + ex * cr - ey * sr, uy = y + ex * sr + ey * cr, dx, dy;
        dev(t, ux, uy, &dx, &dy);
        linha_dev(t, dx, dy);
    }
}
void tela2d_arco(int id, double x, double y, double r, double a0, double a1, int anti) { tela2d_elipse(id, x, y, r, r, 0, a0, a1, anti); }
/* arcTo: arco tangente às linhas (p0,p1) e (p1,p2) com raio r */
void tela2d_arco_para(int id, double x1, double y1, double x2, double y2, double r)
{
    tela_t *t = T(id);
    if (!t || !ok_num(x1) || !ok_num(y1) || !ok_num(x2) || !ok_num(y2) || !ok_num(r) || r < 0) return;
    if (!t->aberto) { tela2d_mover(id, x1, y1); return; }
    /* ponto atual no espaço do usuário: inverso da matriz */
    const double *m = t->st.m.m;
    double det = m[0] * m[3] - m[1] * m[2];
    if (det == 0) return;
    double dx, dy; ultimo(t, &dx, &dy);
    dx -= m[4]; dy -= m[5];
    double x0 = (m[3] * dx - m[2] * dy) / det, y0 = (-m[1] * dx + m[0] * dy) / det;
    double v1x = x0 - x1, v1y = y0 - y1, v2x = x2 - x1, v2y = y2 - y1;
    double l1 = c_sqrt(v1x * v1x + v1y * v1y), l2 = c_sqrt(v2x * v2x + v2y * v2y);
    double cruz = v1x * v2y - v1y * v2x;
    if (r == 0 || l1 == 0 || l2 == 0 || c_abs(cruz) < 1e-9 * l1 * l2) { tela2d_linha_para(id, x1, y1); return; }
    double cosang = (v1x * v2x + v1y * v2y) / (l1 * l2);
    double ang = c_atan2(c_sqrt(1 - cosang * cosang), cosang);   /* ângulo entre as linhas */
    double tg = r / (c_sin(ang / 2) / c_cos(ang / 2));          /* distância do canto ao ponto tangente */
    double t1x = x1 + v1x / l1 * tg, t1y = y1 + v1y / l1 * tg, t2x = x1 + v2x / l2 * tg, t2y = y1 + v2y / l2 * tg;
    /* centro: ao longo da bissetriz */
    double bx = v1x / l1 + v2x / l2, by = v1y / l1 + v2y / l2, bl = c_sqrt(bx * bx + by * by);
    double dc = c_sqrt(tg * tg + r * r);
    double cx = x1 + bx / bl * dc, cy = y1 + by / bl * dc;
    double a0 = c_atan2(t1y - cy, t1x - cx), a1 = c_atan2(t2y - cy, t2x - cx);
    tela2d_linha_para(id, t1x, t1y);
    tela2d_elipse(id, cx, cy, r, r, 0, a0, a1, cruz > 0);
}
void tela2d_ret(int id, double x, double y, double w, double h)
{
    tela_t *t = T(id);
    if (!t || !ok_num(x) || !ok_num(y) || !ok_num(w) || !ok_num(h)) return;
    tela2d_mover(id, x, y);
    tela2d_linha_para(id, x + w, y);
    tela2d_linha_para(id, x + w, y + h);
    tela2d_linha_para(id, x, y + h);
    t->subs[t->nsubs - 1].fechado = 1;
    tela2d_mover(id, x, y);   /* rect() começa um subcaminho novo em (x, y) */
}
void tela2d_fechar(int id)
{
    tela_t *t = T(id);
    if (!t || !t->nsubs || !t->aberto) return;
    sub_t *s = &t->subs[t->nsubs - 1];
    s->fechado = 1;
    pt_t p = t->pts[s->ini];
    novo_sub(t); por_pt(t, p.x, p.y);   /* o próximo começa no primeiro ponto */
}

/* ------------------------------------------------------------ rasterização */
typedef struct { float *acc; int x0, y0, l, a; } raster_t;

static void r_linha(raster_t *R, double x0, double y0, double x1, double y1)
{
    x0 -= R->x0; x1 -= R->x0; y0 -= R->y0; y1 -= R->y0;
    if (y0 == y1) return;
    double dir = 1;
    if (y0 > y1) { double t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
    if (y1 <= 0 || y0 >= R->a) return;
    double dxdy = (x1 - x0) / (y1 - y0);
    double x = x0;
    if (y0 < 0) { x -= y0 * dxdy; y0 = 0; }
    if (y1 > R->a) y1 = R->a;
    int l = R->l;
    for (int y = (int)y0; y < R->a && y < y1; y++) {
        float *lin = R->acc + y * (l + 2);
        double dy = c_min(y + 1, y1) - c_max(y, y0);
        double xn = x + dxdy * dy, d = dy * dir;
        double a0x = x < xn ? x : xn, a1x = x < xn ? xn : x;
        /* fora da largura: à esquerda conta na coluna 0, à direita não aparece */
        if (a1x <= 0) { lin[0] += (float)d; x = xn; continue; }
        if (a0x >= l) { x = xn; continue; }
        if (a0x < 0) {   /* parte à esquerda: vertical na borda, o resto como está */
            double f = -a0x / (a1x - a0x);
            lin[0] += (float)(d * f);
            d -= d * f; a0x = 0;
        }
        if (a1x > l) a1x = l;
        double fl = c_floor(a0x); int x0i = (int)fl;
        double ce = c_ceil(a1x); int x1i = (int)ce;
        if (x1i <= x0i + 1) {
            double xm = 0.5 * (a0x + a1x) - fl;
            lin[x0i] += (float)(d - d * xm);
            lin[x0i + 1] += (float)(d * xm);
        } else {
            double s = 1 / (a1x - a0x), x0f = a0x - fl, a0 = 0.5 * s * (1 - x0f) * (1 - x0f);
            double x1f = a1x - ce + 1, am = 0.5 * s * x1f * x1f;
            lin[x0i] += (float)(d * a0);
            if (x1i == x0i + 2) lin[x0i + 1] += (float)(d * (1 - a0 - am));
            else {
                double a1 = s * (1.5 - x0f);
                lin[x0i + 1] += (float)(d * (a1 - a0));
                for (int xi = x0i + 2; xi < x1i - 1; xi++) lin[xi] += (float)(d * s);
                double a2 = a1 + (x1i - x0i - 3) * s;
                lin[x1i - 1] += (float)(d * (1 - a2 - am));
            }
            lin[x1i] += (float)(d * am);
        }
        x = xn;
    }
}

/* polígonos (lista de pontos e subcaminhos) -> cobertura 0..255 por pixel da caixa */
typedef struct { pt_t *p; int n, c; int *ini, *qtd; int ns, cs; } poli_t;
static void poli_ponto(poli_t *P, double x, double y)
{
    if (P->n == P->c) { int c = P->c ? P->c * 2 : 256; pt_t *q = realoca(P->p, (unsigned)c * sizeof(pt_t)); if (!q) return; P->p = q; P->c = c; }
    P->p[P->n].x = x; P->p[P->n].y = y; P->n++;
    P->qtd[P->ns - 1]++;
}
static void poli_novo(poli_t *P)
{
    if (P->ns == P->cs) {
        int c = P->cs ? P->cs * 2 : 64;
        int *a = realoca(P->ini, (unsigned)c * sizeof(int)), *b = a ? realoca(P->qtd, (unsigned)c * sizeof(int)) : NULL;
        if (a) P->ini = a;
        if (!b) return;
        P->qtd = b; P->cs = c;
    }
    P->ini[P->ns] = P->n; P->qtd[P->ns] = 0; P->ns++;
}
static void poli_fim(poli_t *P) { if (P->p) libera(P->p); if (P->ini) libera(P->ini); if (P->qtd) libera(P->qtd); memset(P, 0, sizeof *P); }

/* cobre: máscara l x a da caixa (bx, by); devolve a caixa (pode ser vazia) */
static uint8_t *cobrir(tela_t *t, const poli_t *P, int parimpar, int *bx, int *by, int *bl, int *ba)
{
    double mx = 1e30, my = 1e30, Mx = -1e30, My = -1e30;
    for (int i = 0; i < P->n; i++) {
        if (P->p[i].x < mx) mx = P->p[i].x;
        if (P->p[i].y < my) my = P->p[i].y;
        if (P->p[i].x > Mx) Mx = P->p[i].x;
        if (P->p[i].y > My) My = P->p[i].y;
    }
    int x0 = (int)c_floor(c_max(mx, 0)), y0 = (int)c_floor(c_max(my, 0));
    int x1 = (int)c_ceil(c_min(Mx, t->l)), y1 = (int)c_ceil(c_min(My, t->a));
    if (x1 <= x0 || y1 <= y0) return NULL;
    raster_t R = { NULL, x0, y0, x1 - x0, y1 - y0 };
    R.acc = aloca((unsigned)((R.l + 2) * R.a) * sizeof(float));
    if (!R.acc) return NULL;
    memset(R.acc, 0, (unsigned)((R.l + 2) * R.a) * sizeof(float));
    for (int s = 0; s < P->ns; s++) {
        int i0 = P->ini[s], n = P->qtd[s];
        for (int i = 0; i < n; i++) {
            pt_t a = P->p[i0 + i], b = P->p[i0 + (i + 1) % n];   /* todo polígono fecha */
            r_linha(&R, a.x, a.y, b.x, b.y);
        }
    }
    uint8_t *m = aloca((unsigned)(R.l * R.a));
    if (m) for (int y = 0; y < R.a; y++) {
        float s = 0, *lin = R.acc + y * (R.l + 2);
        for (int x = 0; x < R.l; x++) {
            s += lin[x];
            double c = s < 0 ? -s : s;
            if (parimpar) { c -= 2 * c_floor(c / 2); if (c > 1) c = 2 - c; }
            else if (c > 1) c = 1;
            m[y * R.l + x] = (uint8_t)(c * 255 + 0.5);
        }
    }
    libera(R.acc);
    *bx = x0; *by = y0; *bl = R.l; *ba = R.a;
    return m;
}

/* ------------------------------------------------------------ pintura */
static uint32_t grad_cor(const tela_t *t, const grad_t *G, int px, int py)
{
    if (!G->n) return 0;
    /* pixel (centro) para o espaço do usuário */
    const double *m = t->st.m.m;
    double det = m[0] * m[3] - m[1] * m[2];
    if (det == 0) return 0;
    double dx = px + 0.5 - m[4], dy = py + 0.5 - m[5];
    double x = (m[3] * dx - m[2] * dy) / det, y = (-m[1] * dx + m[0] * dy) / det;
    double s;
    if (!G->radial) {
        double vx = G->p[2] - G->p[0], vy = G->p[3] - G->p[1], l2 = vx * vx + vy * vy;
        if (l2 == 0) return 0;
        s = ((x - G->p[0]) * vx + (y - G->p[1]) * vy) / l2;
    } else {
        /* HTML: maior w com r(w) >= 0 tal que o ponto está no círculo w */
        double x0 = G->p[0], y0 = G->p[1], r0 = G->p[2], x1 = G->p[3], y1 = G->p[4], r1 = G->p[5];
        double cdx = x1 - x0, cdy = y1 - y0, dr = r1 - r0;
        double pdx = x - x0, pdy = y - y0;
        double a = cdx * cdx + cdy * cdy - dr * dr, b = pdx * cdx + pdy * cdy + r0 * dr, c = pdx * pdx + pdy * pdy - r0 * r0;
        if (c_abs(a) < 1e-12) {
            if (b == 0) return 0;
            s = c / (2 * b);
            if (r0 + s * dr < 0) return 0;
        } else {
            double disc = b * b - a * c;
            if (disc < 0) return 0;
            double rq = c_sqrt(disc), s1 = (b + rq) / a, s2 = (b - rq) / a;
            double hi = s1 > s2 ? s1 : s2, lo = s1 > s2 ? s2 : s1;
            if (r0 + hi * dr >= 0) s = hi;
            else if (r0 + lo * dr >= 0) s = lo;
            else return 0;
        }
    }
    if (s <= G->pos[0]) return G->cor[0];
    if (s >= G->pos[G->n - 1]) return G->cor[G->n - 1];
    int i = 1;
    while (i < G->n - 1 && G->pos[i] < s) i++;
    double span = G->pos[i] - G->pos[i - 1], f = span > 0 ? (s - G->pos[i - 1]) / span : 1;
    uint32_t c0 = G->cor[i - 1], c1 = G->cor[i];
    /* interpolação pré-multiplicada (como os navegadores) */
    double a0 = (c0 >> 24) / 255.0, a1 = (c1 >> 24) / 255.0, a = a0 + (a1 - a0) * f;
    if (a <= 0) return 0;
    uint32_t r = 0;
    for (int k = 0; k < 3; k++) {
        double v0 = ((c0 >> (16 - 8 * k)) & 255) * a0, v1 = ((c1 >> (16 - 8 * k)) & 255) * a1;
        r |= (uint32_t)faixa(arred((v0 + (v1 - v0) * f) / a)) << (16 - 8 * k);
    }
    return (uint32_t)faixa(arred(a * 255)) << 24 | r;
}

/* compõe a fonte (cor sem pré-multiplicar, alfa já com cobertura) no destino */
static uint32_t compor(uint32_t d, uint32_t s, int sa, int op)
{
    if (sa <= 0 && op != 1) return d;
    int da = (int)(d >> 24);
    int sr = (int)(s >> 16 & 255), sg = (int)(s >> 8 & 255), sb = (int)(s & 255);
    int dr = (int)(d >> 16 & 255), dg = (int)(d >> 8 & 255), db = (int)(d & 255);
    int oa, Fs, Fd;   /* frações em 0..255 (Porter-Duff) */
    switch (op) {
    case 1: Fs = 255; Fd = 0; break;                          /* copy */
    case 2: Fs = 0; Fd = 255 - sa; break;                      /* destination-out */
    case 3: {                                                  /* lighter: soma */
        int a = sa + da; if (a > 255) a = 255;
        if (!a) return 0;
        int r = (sr * sa + dr * da) / a, g = (sg * sa + dg * da) / a, b = (sb * sa + db * da) / a;
        return (uint32_t)a << 24 | (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b);
    }
    case 4: Fs = da; Fd = 255 - sa; break;                     /* source-atop */
    case 5: Fs = 255 - da; Fd = 255; break;                    /* destination-over */
    case 6: Fs = da; Fd = 0; break;                            /* source-in */
    case 7: Fs = 255 - da; Fd = 0; break;                      /* source-out */
    case 8: Fs = 0; Fd = sa; break;                            /* destination-in */
    case 9: Fs = 255 - da; Fd = 255 - sa; break;               /* xor */
    default: Fs = 255; Fd = 255 - sa; break;                   /* source-over */
    }
    /* em pré-multiplicado: co = cs*as*Fs + cd*ad*Fd */
    int ca = (sa * Fs + da * Fd + 127) / 255;
    if (ca <= 0) return 0;
    int r = (sr * sa * Fs / 255 + dr * da * Fd / 255) / ca;
    int g = (sg * sa * Fs / 255 + dg * da * Fd / 255) / ca;
    int b = (sb * sa * Fs / 255 + db * da * Fd / 255) / ca;
    oa = ca > 255 ? 255 : ca;
    return (uint32_t)oa << 24 | (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b);
}
/* operações que apagam fora da forma (copy, source-in, ...): o resto da tela também muda */
static int apaga_fora(int op) { return op == 1 || op == 6 || op == 7 || op == 8; }

/* pinta a máscara de cobertura com a cor ou o gradiente do estado */
static void pintar_mascara(tela_t *t, const uint8_t *m, int bx, int by, int bl, int ba, int traco)
{
    int op = t->st.comp, ga = (int)(t->st.alfa * 255 + 0.5);
    int g = t->st.grad[traco];
    uint32_t cor = t->st.cor[traco];
    const uint8_t *rc = t->st.recorte;
    int y0 = apaga_fora(op) ? 0 : by, y1 = apaga_fora(op) ? t->a : by + ba;
    int x0 = apaga_fora(op) ? 0 : bx, x1 = apaga_fora(op) ? t->l : bx + bl;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int cob = (x >= bx && x < bx + bl && y >= by && y < by + ba) ? m[(y - by) * bl + x - bx] : 0;
            int k = rc ? rc[y * t->l + x] : 255;
            if (!k) continue;
            uint32_t s = g >= 0 ? grad_cor(t, &grads[g], x, y) : cor;
            int sa = (int)(s >> 24) * cob / 255 * ga / 255;
            uint32_t *d = &t->px[y * t->l + x];
            uint32_t r = compor(*d, s, sa, op);
            if (k < 255) {   /* recorte com antialias: mistura com o destino */
                uint32_t o = *d; int f = k;
                uint32_t mix = 0;
                for (int c = 0; c < 4; c++) { int a = (int)(o >> (8 * c) & 255), b = (int)(r >> (8 * c) & 255); mix |= (uint32_t)((a * (255 - f) + b * f) / 255) << (8 * c); }
                r = mix;
            }
            *d = r;
        }
    versao++;
}

static void caminho_poli(tela_t *t, poli_t *P)
{
    for (int s = 0; s < t->nsubs; s++) {
        if (t->subs[s].n < 2) continue;
        poli_novo(P);
        for (int i = 0; i < t->subs[s].n; i++) poli_ponto(P, t->pts[t->subs[s].ini + i].x, t->pts[t->subs[s].ini + i].y);
    }
}

void tela2d_preencher(int id, int parimpar)
{
    tela_t *t = T(id);
    if (!t || !t->px) return;
    poli_t P; memset(&P, 0, sizeof P);
    caminho_poli(t, &P);
    int bx, by, bl, ba;
    uint8_t *m = P.n ? cobrir(t, &P, parimpar, &bx, &by, &bl, &ba) : NULL;
    if (m) { pintar_mascara(t, m, bx, by, bl, ba, 0); libera(m); }
    else if (apaga_fora(t->st.comp)) { uint8_t z = 0; pintar_mascara(t, &z, 0, 0, 0, 0, 0); }
    poli_fim(&P);
}

void tela2d_recortar(int id, int parimpar)
{
    tela_t *t = T(id);
    if (!t || !t->px) return;
    poli_t P; memset(&P, 0, sizeof P);
    caminho_poli(t, &P);
    int bx = 0, by = 0, bl = 0, ba = 0;
    uint8_t *m = P.n ? cobrir(t, &P, parimpar, &bx, &by, &bl, &ba) : NULL;
    if (!t->st.recorte) {
        t->st.recorte = aloca((unsigned)(t->l * t->a));
        if (!t->st.recorte) { if (m) libera(m); poli_fim(&P); return; }
        memset(t->st.recorte, 255, (unsigned)(t->l * t->a));
    }
    for (int y = 0; y < t->a; y++)
        for (int x = 0; x < t->l; x++) {
            int c = m && x >= bx && x < bx + bl && y >= by && y < by + ba ? m[(y - by) * bl + x - bx] : 0;
            uint8_t *r = &t->st.recorte[y * t->l + x];
            *r = (uint8_t)(*r * c / 255);
        }
    if (m) libera(m);
    poli_fim(&P);
}

/* ---- traço: polígonos no espaço do usuário, levados à tela pela matriz */
static void ponto_tela(const tela_t *t, double x, double y, pt_t *o) { dev(t, x, y, &o->x, &o->y); }
/* acrescenta um polígono convexo com orientação positiva */
static void conv(poli_t *P, const tela_t *t, const pt_t *u, int n)
{
    double area = 0;
    pt_t d[64];
    if (n > 64) n = 64;
    for (int i = 0; i < n; i++) ponto_tela(t, u[i].x, u[i].y, &d[i]);
    for (int i = 0; i < n; i++) area += d[i].x * d[(i + 1) % n].y - d[(i + 1) % n].x * d[i].y;
    if (area == 0) return;
    poli_novo(P);
    if (area > 0) for (int i = 0; i < n; i++) poli_ponto(P, d[i].x, d[i].y);
    else for (int i = n - 1; i >= 0; i--) poli_ponto(P, d[i].x, d[i].y);
}
static void disco(poli_t *P, const tela_t *t, double x, double y, double r)
{
    pt_t u[32];
    for (int i = 0; i < 32; i++) { double a = 2 * PI * i / 32; u[i].x = x + r * c_cos(a); u[i].y = y + r * c_sin(a); }
    conv(P, t, u, 32);
}

/* uma polilinha (espaço do usuário) vira polígonos de traço */
static void tracar_poli(poli_t *P, tela_t *t, const pt_t *u, int n, int fechado, double w)
{
    if (n == 1) {   /* ponto sozinho: só pontas redondas aparecem (como no Chrome) */
        if (t->st.cap == 1) disco(P, t, u[0].x, u[0].y, w);
        return;
    }
    int nseg = fechado ? n : n - 1;
    for (int i = 0; i < nseg; i++) {
        pt_t a = u[i], b = u[(i + 1) % n];
        double vx = b.x - a.x, vy = b.y - a.y, l = c_sqrt(vx * vx + vy * vy);
        if (l == 0) continue;
        double nx = -vy / l * w, ny = vx / l * w, ex = 0, ey = 0, sx = 0, sy = 0;
        if (!fechado && t->st.cap == 2) {   /* square: estende meia largura nas pontas */
            if (i == 0) { sx = -vx / l * w; sy = -vy / l * w; }
            if (i == nseg - 1) { ex = vx / l * w; ey = vy / l * w; }
        }
        pt_t q[4] = { { a.x + sx + nx, a.y + sy + ny }, { b.x + ex + nx, b.y + ey + ny }, { b.x + ex - nx, b.y + ey - ny }, { a.x + sx - nx, a.y + sy - ny } };
        conv(P, t, q, 4);
    }
    /* junções */
    for (int i = fechado ? 0 : 1; i < (fechado ? n : n - 1); i++) {
        pt_t p0 = u[(i - 1 + n) % n], p1 = u[i], p2 = u[(i + 1) % n];
        double ax = p1.x - p0.x, ay = p1.y - p0.y, bx = p2.x - p1.x, by = p2.y - p1.y;
        double la = c_sqrt(ax * ax + ay * ay), lb = c_sqrt(bx * bx + by * by);
        if (la == 0 || lb == 0) continue;
        ax /= la; ay /= la; bx /= lb; by /= lb;
        double cruz = ax * by - ay * bx;
        if (c_abs(cruz) < 1e-9 && ax * bx + ay * by > 0) continue;   /* em linha reta */
        if (t->st.join == 1) { disco(P, t, p1.x, p1.y, w); continue; }   /* round */
        double sg = cruz > 0 ? -1 : 1;   /* lado de fora da curva */
        pt_t o1 = { p1.x + sg * -ay * w, p1.y + sg * ax * w }, o2 = { p1.x + sg * -by * w, p1.y + sg * bx * w };
        pt_t tri[4] = { p1, o1, o2, p1 };
        int k = 3;
        if (t->st.join == 0) {   /* miter, se couber no limite */
            double cosv = -(ax * bx + ay * by), sen2 = (1 - cosv) / 2;   /* sen(theta/2)^2 */
            if (sen2 > 1e-12) {
                double razao = 1 / c_sqrt(sen2);
                if (razao <= t->st.miter) {
                    double mxv = (o1.x + o2.x) / 2 - p1.x, myv = (o1.y + o2.y) / 2 - p1.y, ml = c_sqrt(mxv * mxv + myv * myv);
                    if (ml > 0) {
                        double d = w * razao;
                        tri[2].x = p1.x + mxv / ml * d; tri[2].y = p1.y + myv / ml * d;
                        tri[3] = o2;
                        k = 4;
                    }
                }
            }
        }
        conv(P, t, tri, k);
    }
    if (!fechado && t->st.cap == 1) { disco(P, t, u[0].x, u[0].y, w); disco(P, t, u[n - 1].x, u[n - 1].y, w); }
}

/* tracejado: corta a polilinha nos trechos "ligados" do padrão */
static void tracar_tracejado(poli_t *P, tela_t *t, const pt_t *u, int n, int fechado, double w)
{
    const double *pat = t->st.traco; int np = t->st.ntraco;
    double tot = 0;
    for (int i = 0; i < np; i++) tot += pat[i];
    double d = t->st.traco_desl;
    d -= c_floor(d / tot) * tot;
    int k = 0;
    while (d >= pat[k]) { d -= pat[k]; k = (k + 1) % np; }
    double resta = pat[k] - d;   /* quanto falta do trecho k */
    pt_t *pc = aloca((unsigned)(n + 2) * 2 * sizeof(pt_t));
    if (!pc) return;
    int npc = 0;
    if (!(k & 1)) pc[npc++] = u[0];
    int nseg = fechado ? n : n - 1;
    for (int i = 0; i < nseg; i++) {
        pt_t a = u[i], b = u[(i + 1) % n];
        double vx = b.x - a.x, vy = b.y - a.y, l = c_sqrt(vx * vx + vy * vy), pos = 0;
        while (l - pos > resta) {
            pos += resta;
            pt_t q = { a.x + vx * pos / l, a.y + vy * pos / l };
            if (!(k & 1)) { pc[npc++] = q; tracar_poli(P, t, pc, npc, 0, w); npc = 0; }
            else pc[npc++] = q;
            k = (k + 1) % np;
            resta = pat[k];
        }
        resta -= l - pos;
        if (!(k & 1) && npc < 2 * n + 2) pc[npc++] = b;
    }
    if (!(k & 1) && npc > 1) tracar_poli(P, t, pc, npc, 0, w);
    libera(pc);
}

void tela2d_tracar(int id)
{
    tela_t *t = T(id);
    if (!t || !t->px) return;
    const double *m = t->st.m.m;
    double det = m[0] * m[3] - m[1] * m[2];
    if (det == 0) return;
    double w = t->st.lw / 2;
    poli_t P; memset(&P, 0, sizeof P);
    for (int s = 0; s < t->nsubs; s++) {
        sub_t *S = &t->subs[s];
        /* pontos de volta ao espaço do usuário, sem repetidos */
        int n = 0;
        pt_t *u = aloca((unsigned)(S->n + 1) * sizeof(pt_t));
        if (!u) continue;
        for (int i = 0; i < S->n; i++) {
            double dx = t->pts[S->ini + i].x - m[4], dy = t->pts[S->ini + i].y - m[5];
            pt_t q = { (m[3] * dx - m[2] * dy) / det, (-m[1] * dx + m[0] * dy) / det };
            if (n && c_abs(q.x - u[n - 1].x) < 1e-9 && c_abs(q.y - u[n - 1].y) < 1e-9) continue;
            u[n++] = q;
        }
        int fechado = S->fechado;
        if (fechado && n > 2 && c_abs(u[0].x - u[n - 1].x) < 1e-9 && c_abs(u[0].y - u[n - 1].y) < 1e-9) n--;
        if (n >= 1) {
            if (t->st.ntraco && n >= 2) tracar_tracejado(&P, t, u, n, fechado, w);
            else tracar_poli(&P, t, u, n, fechado, w);
        }
        libera(u);
    }
    int bx, by, bl, ba;
    uint8_t *mk = P.n ? cobrir(t, &P, 0, &bx, &by, &bl, &ba) : NULL;
    if (mk) { pintar_mascara(t, mk, bx, by, bl, ba, 1); libera(mk); }
    poli_fim(&P);
}

/* fillRect / strokeRect / clearRect não mexem no caminho atual */
static void ret_poli(tela_t *t, poli_t *P, double x, double y, double w, double h)
{
    pt_t q[4] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
    conv(P, t, q, 4);
}
void tela2d_ret_cheio(int id, double x, double y, double w, double h)
{
    tela_t *t = T(id);
    if (!t || !t->px || !ok_num(x) || !ok_num(y) || !ok_num(w) || !ok_num(h) || !w || !h) return;
    poli_t P; memset(&P, 0, sizeof P);
    ret_poli(t, &P, x, y, w, h);
    int bx, by, bl, ba;
    uint8_t *m = P.n ? cobrir(t, &P, 0, &bx, &by, &bl, &ba) : NULL;
    if (m) { pintar_mascara(t, m, bx, by, bl, ba, 0); libera(m); }
    poli_fim(&P);
}
void tela2d_ret_limpar(int id, double x, double y, double w, double h)
{
    tela_t *t = T(id);
    if (!t || !t->px || !ok_num(x) || !ok_num(y) || !ok_num(w) || !ok_num(h) || !w || !h) return;
    poli_t P; memset(&P, 0, sizeof P);
    ret_poli(t, &P, x, y, w, h);
    int bx, by, bl, ba;
    uint8_t *m = P.n ? cobrir(t, &P, 0, &bx, &by, &bl, &ba) : NULL;
    if (m) {
        const uint8_t *rc = t->st.recorte;
        for (int yy = 0; yy < ba; yy++)
            for (int xx = 0; xx < bl; xx++) {
                int c = m[yy * bl + xx];
                if (rc) c = c * rc[(by + yy) * t->l + bx + xx] / 255;
                if (!c) continue;
                uint32_t *d = &t->px[(by + yy) * t->l + bx + xx];
                int a = (int)(*d >> 24) * (255 - c) / 255;
                *d = a ? ((uint32_t)a << 24 | (*d & 0xFFFFFF)) : 0;
            }
        libera(m);
        versao++;
    }
    poli_fim(&P);
}
void tela2d_ret_traco(int id, double x, double y, double w, double h)
{
    tela_t *t = T(id);
    if (!t || !ok_num(x) || !ok_num(y) || !ok_num(w) || !ok_num(h)) return;
    /* caminho próprio: guarda e devolve o caminho atual */
    pt_t *pts = t->pts; int npts = t->npts, cpts = t->cpts, ab = t->aberto;
    sub_t *subs = t->subs; int nsubs = t->nsubs, csubs = t->csubs;
    t->pts = NULL; t->npts = t->cpts = 0; t->subs = NULL; t->nsubs = t->csubs = 0; t->aberto = 0;
    tela2d_mover(id, x, y);
    tela2d_linha_para(id, x + w, y);
    tela2d_linha_para(id, x + w, y + h);
    tela2d_linha_para(id, x, y + h);
    if (t->nsubs) t->subs[t->nsubs - 1].fechado = 1;
    tela2d_tracar(id);
    if (t->pts) libera(t->pts);
    if (t->subs) libera(t->subs);
    t->pts = pts; t->npts = npts; t->cpts = cpts; t->subs = subs; t->nsubs = nsubs; t->csubs = csubs; t->aberto = ab;
}

/* ------------------------------------------------------------ texto (fonte do sistema) */
/* largura de um texto UTF-8 em px para o tamanho dado (célula 8 x 16 escalada) */
double tela2d_medir(const char *s, double tam)
{
    int n = 0;
    for (const uint8_t *p = (const uint8_t *)s; *p; p++) if ((*p & 0xC0) != 0x80) n++;
    return n * tam / 2;
}
/* largura com uma fonte de verdade (px de fonte = tam) */
static double medir_fonte(fonte_t *f, const char *s, double tam, double ls)
{
    int px = (int)(tam + 0.5); if (px < 1) px = 1;
    double w = 0;
    for (int i = 0; s[i];) { uint32_t cp; i += utf8_ler(s + i, (int)tam_texto(s + i), &cp); w += fonte_avanco(f, cp, px) / 64.0 * (tam / px) + ls; }
    return w;
}
double tela2d_medir_fonte(fonte_t *f, const char *s, double tam, double ls) { return f ? medir_fonte(f, s, tam, ls) : tela2d_medir(s, tam); }

/* Texto com fonte de verdade: a linha é rasterizada no tamanho da tela (a escala
 * da matriz) e cada pixel da tela busca a cobertura de volta pela inversa,
 * então translação, escala, rotação e cisalhamento valem. */
void tela2d_texto_fonte(int id, fonte_t *f, const char *s, double x, double y, double tam, int alinha, int base, int traco, double maxw, double ls)
{
    tela_t *t = T(id);
    if (!t || !t->px || !ok_num(x) || !ok_num(y) || tam <= 0 || !f) return;
    const double *m = t->st.m.m;
    double det = m[0] * m[3] - m[1] * m[2];
    if (det == 0) return;
    double esc = c_sqrt(c_abs(det));
    int px = (int)(tam * esc + 0.5);
    if (px < 1) px = 1;
    if (px > 600) px = 600;
    double k = px / (tam * esc);                     /* ajuste do arredondamento do tamanho */
    double lw = medir_fonte(f, s, tam, ls);         /* largura no espaço do usuário */
    double sx = 1;
    if (maxw > 0 && lw > maxw) { sx = maxw / lw; lw = maxw; }
    int asc = fonte_ascendente(f, px), des = fonte_descendente(f, px);
    double asc_u = asc / esc / k, des_u = des / esc / k;
    double x0 = alinha == 1 ? x - lw : alinha == 2 ? x - lw / 2 : x;
    /* posição da linha de base pelo textBaseline (top/hanging ~ topo, middle = meio do em, bottom/ideographic = fundo) */
    double yb = base == 1 || base == 4 ? y + asc_u : base == 2 ? y + (asc_u - des_u) / 2 : base == 3 || base == 5 ? y - des_u : y;
    /* linha no espaço do texto, em pixels da tela */
    int W = (int)(lw * esc * k / sx) + px + 4, H = asc + des + 4;
    if (W <= 0 || H <= 0 || (long)W * H > 8L * 1024 * 1024) return;
    uint8_t *cob = aloca((unsigned)(W * H));
    if (!cob) return;
    memset(cob, 0, (unsigned)(W * H));
    int pen = 2 * 64;
    for (int i = 0; s[i];) {
        uint32_t cp; i += utf8_ler(s + i, (int)tam_texto(s + i), &cp);
        int gl, ga, ox, oy;
        const uint8_t *g = fonte_glifo(f, cp, px, &gl, &ga, &ox, &oy);
        int gx = (pen >> 6) + ox, gy = asc + 2 + oy;
        if (g) for (int yy = 0; yy < ga; yy++) {
            int py = gy + yy; if (py < 0 || py >= H) continue;
            for (int xx = 0; xx < gl; xx++) {
                int qx = gx + xx; if (qx < 0 || qx >= W) continue;
                int v = cob[py * W + qx] + g[yy * gl + xx];
                cob[py * W + qx] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
        pen += fonte_avanco(f, cp, px) + (int)(ls * esc * k * 64);
    }
    /* caixa na tela */
    double topo_u = yb - asc_u - 2 / esc / k;
    pt_t c[4]; ponto_tela(t, x0, topo_u, &c[0]); ponto_tela(t, x0 + lw, topo_u, &c[1]);
    ponto_tela(t, x0 + lw, topo_u + H / esc / k, &c[2]); ponto_tela(t, x0, topo_u + H / esc / k, &c[3]);
    double mx = 1e30, my = 1e30, Mx = -1e30, My = -1e30;
    for (int i = 0; i < 4; i++) { mx = c_min(mx, c[i].x); my = c_min(my, c[i].y); Mx = c_max(Mx, c[i].x); My = c_max(My, c[i].y); }
    int bx = (int)c_floor(c_max(mx - 2, 0)), by = (int)c_floor(c_max(my - 2, 0)), ex = (int)c_ceil(c_min(Mx + 2, t->l)), ey = (int)c_ceil(c_min(My + 2, t->a));
    if (ex > bx && ey > by) {
        int bl = ex - bx, ba = ey - by;
        uint8_t *mk = aloca((unsigned)(bl * ba));
        if (mk) {
            double fx = esc * k / sx, fy = esc * k;   /* usuário -> pixels da linha */
            for (int yy = 0; yy < ba; yy++)
                for (int xx = 0; xx < bl; xx++) {
                    double dx = bx + xx + 0.5 - m[4], dy = by + yy + 0.5 - m[5];
                    double ux = (m[3] * dx - m[2] * dy) / det - x0, uy = (-m[1] * dx + m[0] * dy) / det - topo_u;
                    double qx = ux * fx + 2 - 0.5, qy = uy * fy - 0.5;   /* centro do pixel da linha */
                    int ix = (int)c_floor(qx), iy = (int)c_floor(qy);
                    double ax = qx - ix, ay = qy - iy, v = 0;
                    for (int j = 0; j < 2; j++)
                        for (int i = 0; i < 2; i++) {
                            int px2 = ix + i, py2 = iy + j;
                            if (px2 < 0 || py2 < 0 || px2 >= W || py2 >= H) continue;
                            v += cob[py2 * W + px2] * (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
                        }
                    mk[yy * bl + xx] = (uint8_t)(v > 255 ? 255 : v + 0.5);
                }
            pintar_mascara(t, mk, bx, by, bl, ba, traco ? 1 : 0);
            libera(mk);
        }
    }
    libera(cob);
}

/* alinha: 0 start/left, 1 end/right, 2 center; base: 0 alphabetic, 1 top, 2 middle, 3 bottom, 4 hanging, 5 ideographic */
void tela2d_texto(int id, const char *s, double x, double y, double tam, int alinha, int base, int traco, double maxw)
{
    tela_t *t = T(id);
    if (!t || !t->px || !ok_num(x) || !ok_num(y) || tam <= 0) return;
    /* UTF-8 -> Latin-1 (o que a fonte do sistema tem) */
    uint8_t txt[512]; int n = 0;
    for (int i = 0; s[i] && n < 511;) {
        uint32_t cp; int k = utf8_ler(s + i, (int)tam_texto(s + i), &cp);
        i += k;
        txt[n++] = (uint8_t)(cp < 256 ? cp : '?');
    }
    double lw = n * tam / 2, esc = 1;
    if (maxw > 0 && lw > maxw) { esc = maxw / lw; lw = maxw; }
    double x0 = alinha == 1 ? x - lw : alinha == 2 ? x - lw / 2 : x;
    double asc = tam * 12 / 16, top = base == 1 || base == 4 ? y : base == 2 ? y - tam / 2 : base == 3 || base == 5 ? y - tam : y - asc;
    /* caixa do texto na tela e mapeamento inverso pixel -> célula */
    const double *m = t->st.m.m;
    double det = m[0] * m[3] - m[1] * m[2];
    if (det == 0) return;
    pt_t c[4]; ponto_tela(t, x0, top, &c[0]); ponto_tela(t, x0 + lw, top, &c[1]); ponto_tela(t, x0 + lw, top + tam, &c[2]); ponto_tela(t, x0, top + tam, &c[3]);
    double mx = 1e30, my = 1e30, Mx = -1e30, My = -1e30;
    for (int i = 0; i < 4; i++) { mx = c_min(mx, c[i].x); my = c_min(my, c[i].y); Mx = c_max(Mx, c[i].x); My = c_max(My, c[i].y); }
    int bx = (int)c_floor(c_max(mx, 0)), by = (int)c_floor(c_max(my, 0)), ex = (int)c_ceil(c_min(Mx, t->l)), ey = (int)c_ceil(c_min(My, t->a));
    if (ex <= bx || ey <= by) return;
    int bl = ex - bx, ba = ey - by;
    uint8_t *mk = aloca((unsigned)(bl * ba));
    if (!mk) return;
    double cw = tam / 2 * esc;   /* largura da célula */
    for (int yy = 0; yy < ba; yy++)
        for (int xx = 0; xx < bl; xx++) {
            int soma = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {   /* 16 amostras por pixel */
                    double dx = bx + xx + (sx + 0.5) / 4 - m[4], dy = by + yy + (sy + 0.5) / 4 - m[5];
                    double ux = (m[3] * dx - m[2] * dy) / det - x0, uy = (-m[1] * dx + m[0] * dy) / det - top;
                    if (ux < 0 || uy < 0 || uy >= tam || ux >= lw) continue;
                    int ci = (int)(ux / cw);
                    if (ci >= n) continue;
                    int gx = (int)((ux - ci * cw) / cw * 8), gy = (int)(uy / tam * 16);
                    if (gx > 7) gx = 7;
                    if (gy > 15) gy = 15;
                    if (fonte_sagui[txt[ci]][gy] & (0x80 >> gx)) soma++;
                }
            mk[yy * bl + xx] = (uint8_t)(soma * 255 / 16);
        }
    pintar_mascara(t, mk, bx, by, bl, ba, traco ? 1 : 0);
    libera(mk);
}

/* ------------------------------------------------------------ imagens */
/* drawImage de outra tela (ou de uma imagem decodificada guardada numa tela) */
void tela2d_desenhar(int id, int src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh)
{
    if (tela2d_sincronizar) tela2d_sincronizar(src);
    tela_t *t = T(id), *f = T(src);
    if (!t || !t->px || !f || !f->px || !sw || !sh || !dw || !dh) return;
    if (sw < 0) { sx += sw; sw = -sw; }
    if (sh < 0) { sy += sh; sh = -sh; }
    if (dw < 0) { dx += dw; dw = -dw; }
    if (dh < 0) { dy += dh; dh = -dh; }
    const double *m = t->st.m.m;
    double det = m[0] * m[3] - m[1] * m[2];
    if (det == 0) return;
    pt_t c[4]; ponto_tela(t, dx, dy, &c[0]); ponto_tela(t, dx + dw, dy, &c[1]); ponto_tela(t, dx + dw, dy + dh, &c[2]); ponto_tela(t, dx, dy + dh, &c[3]);
    poli_t P; memset(&P, 0, sizeof P);
    poli_novo(&P);
    double area = 0;
    for (int i = 0; i < 4; i++) area += c[i].x * c[(i + 1) % 4].y - c[(i + 1) % 4].x * c[i].y;
    for (int i = 0; i < 4; i++) { int k = area > 0 ? i : 3 - i; poli_ponto(&P, c[k].x, c[k].y); }
    int bx, by, bl, ba;
    uint8_t *mk = cobrir(t, &P, 0, &bx, &by, &bl, &ba);
    poli_fim(&P);
    if (!mk) return;
    int op = t->st.comp, ga = (int)(t->st.alfa * 255 + 0.5);
    const uint8_t *rc = t->st.recorte;
    for (int yy = 0; yy < ba; yy++)
        for (int xx = 0; xx < bl; xx++) {
            int cob = mk[yy * bl + xx];
            if (!cob) continue;
            int px = bx + xx, py = by + yy;
            int k = rc ? rc[py * t->l + px] : 255;
            if (!k) continue;
            double ddx = px + 0.5 - m[4], ddy = py + 0.5 - m[5];
            double ux = (m[3] * ddx - m[2] * ddy) / det, uy = (-m[1] * ddx + m[0] * ddy) / det;
            double fx = sx + (ux - dx) / dw * sw, fy = sy + (uy - dy) / dh * sh;
            uint32_t s;
            if (!t->st.suave) {
                int ix = (int)c_floor(fx), iy = (int)c_floor(fy);
                if (ix < 0) ix = 0;
                if (iy < 0) iy = 0;
                if (ix >= f->l) ix = f->l - 1;
                if (iy >= f->a) iy = f->a - 1;
                s = f->px[iy * f->l + ix];
            } else {   /* bilinear (pré-multiplicado) */
                double gx = fx - 0.5, gy = fy - 0.5;
                int ix = (int)c_floor(gx), iy = (int)c_floor(gy);
                double ax = gx - ix, ay = gy - iy;
                double acc[4] = { 0, 0, 0, 0 };
                for (int j = 0; j < 2; j++)
                    for (int i = 0; i < 2; i++) {
                        int qx = ix + i, qy = iy + j;
                        if (qx < 0) qx = 0;
                        if (qy < 0) qy = 0;
                        if (qx >= f->l) qx = f->l - 1;
                        if (qy >= f->a) qy = f->a - 1;
                        uint32_t p = f->px[qy * f->l + qx];
                        double wgt = (i ? ax : 1 - ax) * (j ? ay : 1 - ay), pa = (p >> 24) / 255.0;
                        acc[0] += wgt * pa;
                        acc[1] += wgt * pa * (p >> 16 & 255); acc[2] += wgt * pa * (p >> 8 & 255); acc[3] += wgt * pa * (p & 255);
                    }
                if (acc[0] <= 0) s = 0;
                else s = (uint32_t)faixa(arred(acc[0] * 255)) << 24 | (uint32_t)faixa(arred(acc[1] / acc[0])) << 16 | (uint32_t)faixa(arred(acc[2] / acc[0])) << 8 | (uint32_t)faixa(arred(acc[3] / acc[0]));
            }
            int sa = (int)(s >> 24) * cob / 255 * ga / 255 * k / 255;
            uint32_t *d = &t->px[py * t->l + px];
            *d = compor(*d, s, sa, op);
        }
    libera(mk);
    versao++;
}

/* Imagem decodificada vira uma tela (para drawImage de <img>). */
int tela2d_de_pixels(const uint32_t *px, int l, int a, int alfa)
{
    int id = tela2d_nova(l, a);
    tela_t *t = T(id);
    if (!t || !t->px) return id;
    for (int i = 0; i < l * a; i++) t->px[i] = alfa ? px[i] : (0xFF000000u | px[i]);
    return id;
}

/* getImageData: RGBA sem pré-multiplicar; fora da tela = 0 */
void tela2d_ler(int id, int x, int y, int w, int h, uint8_t *rgba)
{
    if (tela2d_sincronizar) tela2d_sincronizar(id);
    tela_t *t = T(id);
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            int px = x + xx, py = y + yy;
            uint32_t p = t && t->px && px >= 0 && py >= 0 && px < t->l && py < t->a ? t->px[py * t->l + px] : 0;
            uint8_t *o = rgba + (yy * w + xx) * 4;
            o[0] = (uint8_t)(p >> 16); o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t)p; o[3] = (uint8_t)(p >> 24);
        }
}
/* putImageData: escreve direto (sem matriz, alfa global ou composição) */
void tela2d_escrever(int id, const uint8_t *rgba, int w, int h, int dx, int dy, int rx, int ry, int rw, int rh)
{
    tela_t *t = T(id);
    if (!t || !t->px) return;
    if (rw < 0) { rx += rw; rw = -rw; }
    if (rh < 0) { ry += rh; rh = -rh; }
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > w) rw = w - rx;
    if (ry + rh > h) rh = h - ry;
    for (int yy = ry; yy < ry + rh; yy++)
        for (int xx = rx; xx < rx + rw; xx++) {
            int px = dx + xx, py = dy + yy;
            if (px < 0 || py < 0 || px >= t->l || py >= t->a) continue;
            const uint8_t *s = rgba + (yy * w + xx) * 4;
            t->px[py * t->l + px] = s[3] ? ((uint32_t)s[3] << 24 | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2]) : 0;
        }
    versao++;
}

/* ------------------------------------------------------------ arco do SVG
 * SVG 1.1, apêndice F.6.5: dos extremos e bandeiras para centro e ângulos;
 * devolve os pontos do arco (sem o inicial) para o motor, que não usa ponto
 * flutuante. Coordenadas inteiras na escala que o motor usar. */
int svg_arco_pontos(int x1, int y1, int rx_, int ry_, int rot100, int grande, int varre, int x2, int y2, int *xs, int *ys, int max)
{
    if (max < 1) return 0;
    if ((x1 == x2 && y1 == y2)) return 0;
    double rx = rx_ < 0 ? -rx_ : rx_, ry = ry_ < 0 ? -ry_ : ry_;
    if (rx == 0 || ry == 0) { xs[0] = x2; ys[0] = y2; return 1; }
    double phi = rot100 / 100.0 * PI / 180, cf = c_cos(phi), sf = c_sin(phi);
    double dx = (x1 - x2) / 2.0, dy = (y1 - y2) / 2.0;
    double xp = cf * dx + sf * dy, yp = -sf * dx + cf * dy;
    double lam = xp * xp / (rx * rx) + yp * yp / (ry * ry);
    if (lam > 1) { double s = c_sqrt(lam); rx *= s; ry *= s; }
    double num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp, den = rx * rx * yp * yp + ry * ry * xp * xp;
    double coef = den > 0 && num > 0 ? c_sqrt(num / den) : 0;
    if (grande == varre) coef = -coef;
    double cxp = coef * rx * yp / ry, cyp = -coef * ry * xp / rx;
    double cx = cf * cxp - sf * cyp + (x1 + x2) / 2.0, cy = sf * cxp + cf * cyp + (y1 + y2) / 2.0;
    double ux = (xp - cxp) / rx, uy = (yp - cyp) / ry, vx = (-xp - cxp) / rx, vy = (-yp - cyp) / ry;
    double t1 = c_atan2(uy, ux), dt = c_atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!varre && dt > 0) dt -= 2 * PI;
    else if (varre && dt < 0) dt += 2 * PI;
    /* segmentos: erro de corda abaixo de ~1/4 da unidade de tela */
    double r = rx > ry ? rx : ry;
    int n = (int)(c_abs(dt) * c_sqrt(r) / 2) + 2;
    if (n > max) n = max;
    for (int i = 1; i <= n; i++) {
        double a = t1 + dt * i / n, ca = c_cos(a), sa = c_sin(a);
        double x = cx + rx * ca * cf - ry * sa * sf, y = cy + rx * ca * sf + ry * sa * cf;
        xs[i - 1] = i == n ? x2 : (int)(x < 0 ? x - 0.5 : x + 0.5);
        ys[i - 1] = i == n ? y2 : (int)(y < 0 ? y - 0.5 : y + 0.5);
    }
    return n;
}

/* libera uma tela (as do SVG são temporárias) e um gradiente */
void tela2d_liberar(int id)
{
    tela_t *t = T(id);
    if (!t) return;
    tela_limpar_estado(t);
    if (t->px) libera(t->px);
    if (t->pts) libera(t->pts);
    if (t->subs) libera(t->subs);
    memset(t, 0, sizeof *t);
}
void tela2d_grad_liberar(int g) { if (g >= 0 && g < 256) grads[g].usado = 0; }
