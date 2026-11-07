/* motor.c - motor de páginas do Cipó.
 *
 * 1. HTML vira árvore (regras de fechamento implícito do HTML comum).
 * 2. CSS: folha padrão + <style> + <link> + style="", seletores compostos com
 *    descendente/filho/irmão, especificidade, !important, var(), @media.
 * 3. Layout: bloco (margens que colapsam entre irmãos), inline com quebra de
 *    linha e alinhamento, inline-block, float, flex (linha/coluna, wrap, grow,
 *    shrink, justify, align, gap), grid (colunas px/fr/%/repeat, span), tabela
 *    (colunas por conteúdo, colspan), position relative/absolute/fixed.
 * 4. Pintura: lista de comandos (fundo, borda, texto, imagem) com recorte.
 *
 * Fonte: a 8x16 do sistema, escalada pelo font-size (largura = tamanho / 2).
 * Só inteiros. */
#include "sagui.h"
#include "motor.h"

extern const unsigned char fonte_sagui[256][16];

/* ================================================================ memória */

typedef struct bloco_m { struct bloco_m *prox; int usado, cap; } bloco_m;

struct motor;
static void *m_novo(struct motor *m, int n);

/* ================================================================ tipos */

enum { D_NONE, D_BLOCK, D_INLINE, D_IBLOCK, D_FLEX, D_IFLEX, D_GRID, D_TABLE, D_ROW, D_CELL, D_LIST, D_CONTENTS, D_ROWGROUP, D_CAPTION };
enum { P_STATIC, P_RELATIVE, P_ABSOLUTE, P_FIXED, P_STICKY };
enum { U_AUTO, U_PX, U_PCT, U_EM, U_REM, U_VW, U_VH, U_NONE, U_FR, U_MAXC, U_MINC, U_FIT };

typedef struct { int32_t v; uint8_t u; } medida_t;   /* v em 1/100 para EM/REM/PCT/VW/VH/FR */

#define MAX_TRILHAS 32

typedef struct {
    uint8_t display, position, flutua, limpa, branco, alinha, peso, italico, sublinha, risca, maiusc;
    uint8_t lista, visivel, corta, caixa_borda, dir, quebra, justifica, alinha_itens, alinha_si, alinha_conteudo;
    uint8_t vert;
    medida_t larg, alt, min_l, max_l, min_a, max_a, base;
    medida_t marg[4], pad[4], pos[4];   /* top right bottom left */
    int16_t borda[4]; uint32_t cor_borda[4];
    int16_t raio;
    int16_t tam;                        /* font-size em px */
    medida_t linha;                     /* line-height: U_AUTO = normal, U_NONE = número x100 */
    uint32_t cor, fundo;                /* fundo: 0 = transparente, senão cor | 0xFF000000 */
    int16_t cresce, encolhe, ordem;     /* x100 */
    medida_t vao_l, vao_c;              /* row-gap, column-gap */
    uint8_t n_col; medida_t col[MAX_TRILHAS]; uint8_t col_rep_auto;
    int8_t g_ini, g_span, g_lin, g_lspan;
    const char *areas;      /* grid-template-areas (texto cru) */
    const char *area;       /* grid-area: nome */
    uint8_t opaco0;         /* opacity: 0 */
    const char *bg_url;     /* background-image: url() */
    const char *conteudo;   /* content */
    uint8_t bg_tam;         /* 0 natural, 1 cobre/estica, 2 contém */
    uint8_t mascara;        /* mask-image: o fundo é a cor de um ícone que não desenhamos */
    int16_t z;
} estilo_t;

typedef struct atr { char *nome, *valor; struct atr *prox; } atr_t;

typedef struct var_m { char *nome, *valor; struct var_m *prox; } var_m_t;

typedef struct no_m {
    uint8_t texto;                  /* 1 = nó de texto */
    char tag[12];
    char *t; int nt;                /* texto */
    atr_t *atr;
    const char *id, *classe;
    struct no_m *pai, *filho, *ult, *prox, *ant;
    estilo_t e;
    var_m_t *vars;
    int img;                        /* índice em imagens, -1 */
    int alvo;                       /* alvo de clique herdado (link) */
    /* layout: caixa de borda, em coordenadas da página */
    int x, y, l, a;
    int ml, mr, mt, mb;             /* margens resolvidas */
    int bl[4], pl[4];               /* borda e padding resolvidos */
    int max_c, min_c;               /* medidas intrínsecas (cache), -1 = não medido */
    struct frag *frags, *frags_fim;  /* linhas de texto deste bloco */
    int ordem_doc;
    int colapso;                    /* margem de cima do primeiro filho que vazou para fora */
    uint8_t pseudo;                 /* nó gerado por ::before/::after */
    struct no_m *ps[2];
    int bg_img;                     /* índice da imagem de fundo, -1 */
} no_m;

/* ================================================================ CSS */

#define MAX_COMP 8
typedef struct {
    char tag[12];
    const char *id;
    const char *classes[6]; int nc;
    const char *atr_nome[3], *atr_valor[3]; char atr_op[3]; int na;
    uint8_t primeiro, ultimo, nunca, raiz, vazio;
    int nth_a, nth_b; uint8_t tem_nth;
    char comb;          /* combinador À ESQUERDA deste composto: ' ', '>', '+', '~', 0 */
    const char *nao_tag, *nao_classe;
    uint8_t pseudo;     /* 1 ::before, 2 ::after */
} composto_t;

typedef struct {
    composto_t c[MAX_COMP]; int n;
    int espec;
} seletor_t;

typedef struct { int prop; const char *valor; uint8_t importante; } decl_t;

typedef struct {
    seletor_t sel;
    decl_t *d; int nd;
    int ordem;
} regra_m;

typedef struct { const char *chave; int *regras; int n, cap; } balde_t;

/* ================================================================ pintura */

enum { C_RET, C_BORDA, C_TEXTO, C_IMG, C_LINHA };
typedef struct {
    uint8_t tipo, peso, italico, sublinha;
    int16_t tam, raio;
    int x, y, l, a;
    uint32_t cor;
    const char *t; int nt;
    int img;
    int cx0, cy0, cx1, cy1;     /* recorte */
    int16_t bw[4]; uint32_t bc[4];
    int z;
} cmd_t;

typedef struct { int x, y, l, a; int alvo; } regiao_t;

typedef struct { char *url; uint32_t *px; int l, a; uint8_t tentou, alfa; } imagem_m;

struct motor {
    motor_ganchos_t g;
    bloco_m *blocos;
    no_m *raiz, *corpo;
    int qtd_nos;
    char url[600];
    char titulo[128];
    regra_m *regras; int n_regras, cap_regras;
    balde_t *baldes; int cap_baldes;
    int *univ; int n_univ, cap_univ;
    cmd_t *cmds; int n_cmds, cap_cmds;
    regiao_t *regs; int n_regs, cap_regs;
    motor_alvo_t *alvos; int n_alvos, cap_alvos;
    imagem_m imgs[96]; int n_imgs;
    int vw, vh;
    int altura;
    uint32_t fundo;
    int clip[4];
    int folhas;
    bool sem_esconder;      /* ignora display:none do autor (páginas que só aparecem por JS) */
};

/* ================================================================ arena */

static void *m_novo(struct motor *m, int n)
{
    n = (n + 7) & ~7;
    bloco_m *b = m->blocos;
    if (!b || b->usado + n > b->cap) {
        int cap = n > 60000 ? n : 65536 - (int)sizeof(bloco_m);
        b = aloca(sizeof(bloco_m) + (size_t)cap);
        if (!b) return NULL;
        b->prox = m->blocos; b->usado = 0; b->cap = cap;
        m->blocos = b;
    }
    void *p = (char *)(b + 1) + b->usado;
    b->usado += n;
    memset(p, 0, (size_t)n);
    return p;
}

static char *m_dup(motor_t *m, const char *s, int n)
{
    char *d = m_novo(m, n + 1);
    if (!d) return (char *)"";
    memcpy(d, s, (size_t)n);
    d[n] = 0;
    return d;
}

static bool crescer_v(motor_t *m, void **p, int *cap, int n, int item)
{
    if (n <= *cap) return true;
    int nc = *cap ? *cap * 2 : 64;
    while (nc < n) nc *= 2;
    void *q = m_novo(m, nc * item);
    if (!q) return false;
    if (*cap) memcpy(q, *p, (size_t)(*cap) * (size_t)item);
    *p = q; *cap = nc;
    return true;
}

/* ================================================================ texto */

static char minusc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static bool eh_letra(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool eh_dig(char c) { return c >= '0' && c <= '9'; }
static bool eh_esp(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static bool igual_n(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) if (minusc(a[i]) != minusc(b[i])) return false;
    return true;
}
static bool igual(const char *a, const char *b) { return compara_texto(a, b) == 0; }
static bool strstr_m(const char *s, const char *t)
{
    int n = (int)tam_texto(t);
    for (; *s; s++) if (igual_n(s, t, n)) return true;
    return false;
}

static char para_l1(uint32_t c)
{
    if (c < 0x100) return c == 0xA0 ? (char)0xA0 : (char)c;
    switch (c) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return (char)0xB4;
    case 0x201C: case 0x201E: case 0x2033: case 0x201D: return '"';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2022: case 0x2026: case 0x00B7: case 0x25CF: return (char)0xB7;
    case 0x20AC: return 'E';
    case 0x2192: return '>';
    case 0x2190: return '<';
    case 0x00D7: case 0x2715: return 'x';
    default: return 0;
    }
}

static const struct { const char *n; uint16_t c; } ents[] = {
    {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",0xA0},{"copy",0xA9},{"reg",0xAE},
    {"middot",0xB7},{"bull",0x2022},{"hellip",0x2026},{"ndash",0x2013},{"mdash",0x2014},{"rsquo",0x2019},
    {"lsquo",0x2018},{"ldquo",0x201C},{"rdquo",0x201D},{"laquo",0xAB},{"raquo",0xBB},{"euro",0x20AC},
    {"aacute",0xE1},{"eacute",0xE9},{"iacute",0xED},{"oacute",0xF3},{"uacute",0xFA},{"atilde",0xE3},
    {"otilde",0xF5},{"ccedil",0xE7},{"acirc",0xE2},{"ecirc",0xEA},{"ocirc",0xF4},{"agrave",0xE0},
    {"Aacute",0xC1},{"Eacute",0xC9},{"Iacute",0xCD},{"Oacute",0xD3},{"Uacute",0xDA},{"Atilde",0xC3},
    {"Ccedil",0xC7},{"times",0xD7},{"deg",0xB0},{"uuml",0xFC},{"ouml",0xF6},{"auml",0xE4},{"szlig",0xDF},
    {"rarr",0x2192},{"larr",0x2190},{"trade",'T'},{"shy",0},{"zwj",0},{"zwnj",0},{"thinsp",' '},{"ensp",' '},{"emsp",' '},
};

/* Decodifica entidades em d (no lugar); devolve o novo tamanho. */
static int entidades(char *d, int n)
{
    int k = 0;
    for (int i = 0; i < n;) {
        if (d[i] != '&') { d[k++] = d[i++]; continue; }
        int j = i + 1;
        uint32_t c = 0;
        bool ok = false;
        if (j < n && d[j] == '#') {
            j++;
            bool hx = j < n && (d[j] == 'x' || d[j] == 'X');
            if (hx) j++;
            int ini = j;
            while (j < n && j - ini < 8 && (eh_dig(d[j]) || (hx && ((d[j] | 32) >= 'a' && (d[j] | 32) <= 'f')))) {
                c = c * (hx ? 16 : 10) + (uint32_t)(eh_dig(d[j]) ? d[j] - '0' : (d[j] | 32) - 'a' + 10);
                j++;
            }
            ok = j > ini;
        } else {
            int ini = j;
            while (j < n && j - ini < 10 && (eh_letra(d[j]) || eh_dig(d[j]))) j++;
            for (unsigned e = 0; e < sizeof ents / sizeof ents[0]; e++)
                if ((int)tam_texto(ents[e].n) == j - ini && memcmp(ents[e].n, d + ini, (size_t)(j - ini)) == 0) { c = ents[e].c; ok = true; break; }
        }
        if (!ok) { d[k++] = d[i++]; continue; }
        if (j < n && d[j] == ';') j++;
        char l = para_l1(c);
        if (c == 0 || (!l && c > 0xFF)) { if (c > 0xFF && !l) d[k++] = '?'; }
        else d[k++] = l;
        i = j;
    }
    return k;
}

/* ================================================================ URL */

static void juntar(const char *base, const char *rel, char *s, int max)
{
    while (*rel == ' ') rel++;
    if (comeca_com(rel, "http://") || comeca_com(rel, "https://") || comeca_com(rel, "data:") ||
        comeca_com(rel, "javascript:") || comeca_com(rel, "mailto:") || comeca_com(rel, "sagui-js:")) { copia_texto(s, rel, (size_t)max); return; }
    const char *esq = acha_char(base, ':');
    if (rel[0] == '/' && rel[1] == '/') {
        int k = esq ? (int)(esq - base) + 1 : 0;
        if (k > max - 1) k = max - 1;
        memcpy(s, base, (size_t)k); s[k] = 0;
        junta_texto(s, rel, (size_t)max);
        return;
    }
    const char *host = esq && esq[1] == '/' && esq[2] == '/' ? esq + 3 : base;
    const char *barra = acha_char(host, '/');
    int fim_host = barra ? (int)(barra - base) : (int)tam_texto(base);
    if (rel[0] == '/') {
        if (fim_host > max - 1) fim_host = max - 1;
        memcpy(s, base, (size_t)fim_host); s[fim_host] = 0;
        junta_texto(s, rel, (size_t)max);
        return;
    }
    if (rel[0] == '?' || rel[0] == '#') {
        int k = 0;
        while (base[k] && base[k] != (rel[0] == '?' ? '?' : '#') && !(rel[0] == '?' && base[k] == '#')) k++;
        if (k > max - 1) k = max - 1;
        memcpy(s, base, (size_t)k); s[k] = 0;
        junta_texto(s, rel, (size_t)max);
        return;
    }
    /* relativo à pasta */
    int k = (int)tam_texto(base);
    for (int i = fim_host; base[i]; i++) if (base[i] == '?' || base[i] == '#') { k = i; break; }
    int ult = fim_host;
    for (int i = fim_host; i < k; i++) if (base[i] == '/') ult = i + 1;
    if (ult == fim_host) { if (ult > max - 2) ult = max - 2; memcpy(s, base, (size_t)ult); s[ult++] = '/'; s[ult] = 0; }
    else { if (ult > max - 1) ult = max - 1; memcpy(s, base, (size_t)ult); s[ult] = 0; }
    junta_texto(s, rel, (size_t)max);
    /* normaliza "/./" e "/pasta/../" no caminho (antes de ? e #) */
    int fim = (int)tam_texto(s);
    for (int i = fim_host; s[i]; i++) if (s[i] == '?' || s[i] == '#') { fim = i; break; }
    char *c = s + fim_host;
    int n = fim - fim_host, kk = 0;
    char tmp[600];
    for (int i = 0; i < n && kk < (int)sizeof tmp - 1;) {
        if (c[i] == '/' && i + 2 <= n && c[i + 1] == '.' && (i + 2 == n || c[i + 2] == '/')) { i += 2; continue; }
        if (c[i] == '/' && i + 3 <= n && c[i + 1] == '.' && c[i + 2] == '.' && (i + 3 == n || c[i + 3] == '/')) {
            while (kk > 0 && tmp[kk - 1] != '/') kk--;
            if (kk > 0) kk--;
            i += 3;
            continue;
        }
        tmp[kk++] = c[i++];
    }
    if (kk == 0) tmp[kk++] = '/';
    tmp[kk] = 0;
    char resto[600];
    copia_texto(resto, s + fim, sizeof resto);
    s[fim_host] = 0;
    junta_texto(s, tmp, (size_t)max);
    junta_texto(s, resto, (size_t)max);
}

/* ================================================================ HTML */

static const char *const vazios[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr", "param", "keygen", 0 };
static const char *const fecham_p[] = { "p", "div", "ul", "ol", "h1", "h2", "h3", "h4", "h5", "h6", "table", "section", "article", "header", "footer", "nav", "form", "pre", "blockquote", "dl", "figure", "main", "aside", "hr", "address", "fieldset", "menu", "details", 0 };

static bool na_lista(const char *t, const char *const *l) { for (; *l; l++) if (igual(t, *l)) return true; return false; }

static no_m *novo_no(motor_t *m, no_m *pai)
{
    no_m *n = m_novo(m, sizeof(no_m));
    if (!n) return NULL;
    n->img = -1; n->alvo = -1; n->max_c = n->min_c = -1;
    n->ordem_doc = m->qtd_nos++;
    n->pai = pai;
    if (pai) {
        if (pai->ult) { pai->ult->prox = n; n->ant = pai->ult; } else pai->filho = n;
        pai->ult = n;
    }
    return n;
}

const char *atributo_m(const no_m *n, const char *nome)
{
    for (atr_t *a = n->atr; a; a = a->prox) if (igual(a->nome, nome)) return a->valor;
    return NULL;
}

static void ler_html_m(motor_t *m, const char *h, int n)
{
    no_m *raiz = novo_no(m, NULL);
    copia_texto(raiz->tag, "#doc", sizeof raiz->tag);
    m->raiz = raiz;
    no_m *pilha[256]; int np = 0;
    pilha[np++] = raiz;
#define ATUAL pilha[np - 1]
    for (int i = 0; i < n;) {
        if (h[i] != '<') {
            int k = i;
            while (k < n && h[k] != '<') k++;
            /* texto */
            bool so_esp = true;
            for (int q = i; q < k; q++) if (!eh_esp(h[q])) { so_esp = false; break; }
            if (!so_esp || igual(ATUAL->tag, "pre") || igual(ATUAL->tag, "textarea")) {
                no_m *t = novo_no(m, ATUAL);
                if (!t) return;
                t->texto = 1;
                t->t = m_dup(m, h + i, k - i);
                t->nt = entidades(t->t, k - i);
                t->t[t->nt] = 0;
            } else if (ATUAL->ult && !ATUAL->ult->texto) {
                /* espaço entre elementos inline importa: guarda um espaço */
                no_m *t = novo_no(m, ATUAL);
                if (!t) return;
                t->texto = 1; t->t = (char *)" "; t->nt = 1;
            }
            i = k;
            continue;
        }
        if (i + 3 < n && h[i + 1] == '!' && h[i + 2] == '-' && h[i + 3] == '-') {
            int k = i + 4;
            while (k + 2 < n && !(h[k] == '-' && h[k + 1] == '-' && h[k + 2] == '>')) k++;
            i = k + 3;
            continue;
        }
        int k = i + 1;
        bool fecha = k < n && h[k] == '/';
        if (fecha) k++;
        if (k >= n || !(eh_letra(h[k]) || h[k] == '!' || h[k] == '?')) {
            no_m *t = novo_no(m, ATUAL);
            if (!t) return;
            t->texto = 1; t->t = (char *)"<"; t->nt = 1;
            i++;
            continue;
        }
        if (h[k] == '!' || h[k] == '?') {   /* doctype, <?xml */
            while (k < n && h[k] != '>') k++;
            i = k + 1;
            continue;
        }
        char nome[12]; int nn = 0;
        while (k < n && (eh_letra(h[k]) || eh_dig(h[k]) || h[k] == '-' || h[k] == ':')) { if (nn < 11) nome[nn++] = minusc(h[k]); k++; }
        nome[nn] = 0;
        /* atributos */
        atr_t *atrs = NULL, **fim_a = &atrs;
        bool auto_fecha = false;
        while (k < n && h[k] != '>') {
            while (k < n && (eh_esp(h[k]) || h[k] == '/')) { if (h[k] == '/') auto_fecha = true; k++; }
            if (k >= n || h[k] == '>') break;
            auto_fecha = false;
            int a0 = k;
            while (k < n && !eh_esp(h[k]) && h[k] != '=' && h[k] != '>' && h[k] != '/') k++;
            int a1 = k;
            while (k < n && eh_esp(h[k])) k++;
            const char *v = ""; int nv = 0;
            if (k < n && h[k] == '=') {
                k++;
                while (k < n && eh_esp(h[k])) k++;
                if (k < n && (h[k] == '"' || h[k] == '\'')) {
                    char q = h[k++]; int v0 = k;
                    while (k < n && h[k] != q) k++;
                    v = h + v0; nv = k - v0;
                    if (k < n) k++;
                } else {
                    int v0 = k;
                    while (k < n && !eh_esp(h[k]) && h[k] != '>') k++;
                    v = h + v0; nv = k - v0;
                }
            }
            if (fecha || a1 == a0) continue;
            atr_t *a = m_novo(m, sizeof(atr_t));
            if (!a) return;
            a->nome = m_dup(m, h + a0, a1 - a0);
            for (char *p = a->nome; *p; p++) *p = minusc(*p);
            a->valor = m_dup(m, v, nv);
            int nn2 = entidades(a->valor, nv); a->valor[nn2] = 0;
            *fim_a = a; fim_a = &a->prox;
        }
        i = k < n ? k + 1 : n;
        if (fecha) {
            int alvo = -1;
            for (int q = np - 1; q > 0 && q >= np - 40; q--) if (igual(pilha[q]->tag, nome)) { alvo = q; break; }
            if (alvo > 0) np = alvo;
            else if (igual(nome, "p")) { /* </p> sem <p>: parágrafo vazio */ }
            continue;
        }
        /* fechamentos implícitos */
        if (na_lista(nome, fecham_p)) for (int q = np - 1; q > 0 && q >= np - 6; q--) {
            if (igual(pilha[q]->tag, "p")) { np = q; break; }
            if (!igual(pilha[q]->tag, "span") && !igual(pilha[q]->tag, "b") && !igual(pilha[q]->tag, "i") && !igual(pilha[q]->tag, "a")) break;
        }
        if (igual(nome, "li")) for (int q = np - 1; q > 0 && q >= np - 6; q--) { if (igual(pilha[q]->tag, "li")) { np = q; break; } if (igual(pilha[q]->tag, "ul") || igual(pilha[q]->tag, "ol")) break; }
        if (igual(nome, "option")) for (int q = np - 1; q > 0 && q >= np - 3; q--) if (igual(pilha[q]->tag, "option")) { np = q; break; }
        if (igual(nome, "dt") || igual(nome, "dd")) for (int q = np - 1; q > 0 && q >= np - 6; q--) { if (igual(pilha[q]->tag, "dt") || igual(pilha[q]->tag, "dd")) { np = q; break; } if (igual(pilha[q]->tag, "dl")) break; }
        if (igual(nome, "tr")) for (int q = np - 1; q > 0 && q >= np - 6; q--) { if (igual(pilha[q]->tag, "tr")) { np = q; break; } if (igual(pilha[q]->tag, "table") || igual(pilha[q]->tag, "tbody") || igual(pilha[q]->tag, "thead")) break; }
        if (igual(nome, "td") || igual(nome, "th")) for (int q = np - 1; q > 0 && q >= np - 6; q--) { if (igual(pilha[q]->tag, "td") || igual(pilha[q]->tag, "th")) { np = q; break; } if (igual(pilha[q]->tag, "tr") || igual(pilha[q]->tag, "table")) break; }
        if (igual(nome, "html") || igual(nome, "head") || igual(nome, "body")) {
            no_m *ja = NULL;
            for (int q = 0; q < np; q++) if (igual(pilha[q]->tag, nome)) ja = pilha[q];
            if (ja) { if (!ja->atr) ja->atr = atrs; continue; }
        }
        no_m *e = novo_no(m, ATUAL);
        if (!e) return;
        copia_texto(e->tag, nome, sizeof e->tag);
        e->atr = atrs;
        e->id = atributo_m(e, "id");
        e->classe = atributo_m(e, "class");
        if (igual(nome, "body")) m->corpo = e;
        bool vazio = na_lista(nome, vazios) || auto_fecha;
        if (igual(nome, "script") || igual(nome, "style") || igual(nome, "textarea") || igual(nome, "title") || igual(nome, "xmp") || igual(nome, "noscript")) {
            /* texto cru até </nome> */
            int q = i;
            int ln = (int)tam_texto(nome);
            while (q + ln + 2 < n && !(h[q] == '<' && h[q + 1] == '/' && igual_n(h + q + 2, nome, ln))) q++;
            if (q + ln + 2 >= n) q = n;
            no_m *t = novo_no(m, e);
            if (!t) return;
            t->texto = 1;
            t->t = m_dup(m, h + i, q - i);
            t->nt = q - i;
            if (igual(nome, "textarea") || igual(nome, "title")) { t->nt = entidades(t->t, t->nt); t->t[t->nt] = 0; }
            while (q < n && h[q] != '>') q++;
            i = q < n ? q + 1 : n;
            continue;
        }
        if (igual(nome, "svg") || igual(nome, "math")) {   /* guarda o conteúdo cru (o SVG é desenhado depois) */
            int prof = 1, q = i, ini_svg = i;
            if (!auto_fecha)
                while (q < n && prof) {
                    if (h[q] == '<') {
                        if (q + 4 < n && h[q + 1] == '/' && igual_n(h + q + 2, nome, 3)) prof--;
                        else if (igual_n(h + q + 1, nome, 3)) prof++;
                    }
                    q++;
                }
            int fim_svg = q > ini_svg ? q - 1 : ini_svg;
            while (q < n && h[q - 1] != '>') q++;
            if (igual(nome, "svg") && !auto_fecha && fim_svg > ini_svg) { e->t = m_dup(m, h + ini_svg, fim_svg - ini_svg); e->nt = fim_svg - ini_svg; }
            i = q;
            continue;
        }
        if (!vazio && np < 255) pilha[np++] = e;
    }
#undef ATUAL
    if (!m->corpo) m->corpo = raiz;
}

/* ================================================================ valores CSS */

enum {
    PR_NADA, PR_DISPLAY, PR_POSITION, PR_FLOAT, PR_CLEAR, PR_WIDTH, PR_HEIGHT, PR_MINW, PR_MAXW, PR_MINH, PR_MAXH,
    PR_MARGIN, PR_MT, PR_MR, PR_MB, PR_ML, PR_PADDING, PR_PT, PR_PR, PR_PB, PR_PL,
    PR_BORDER, PR_BT, PR_BR, PR_BB, PR_BL, PR_BWIDTH, PR_BCOLOR, PR_BSTYLE, PR_RADIUS,
    PR_COLOR, PR_BG, PR_BGCOLOR, PR_FONT, PR_FSIZE, PR_FWEIGHT, PR_FSTYLE, PR_TALIGN, PR_TDECO, PR_LHEIGHT,
    PR_WSPACE, PR_LIST, PR_LISTTYPE, PR_VISIBILITY, PR_OVERFLOW, PR_BOXSIZING,
    PR_FLEX, PR_FDIR, PR_FWRAP, PR_FFLOW, PR_JUSTIFY, PR_AITEMS, PR_ASELF, PR_GROW, PR_SHRINK, PR_BASIS, PR_GAP, PR_RGAP, PR_CGAP, PR_ORDER,
    PR_GTC, PR_GCOL, PR_GCS, PR_GCE, PR_GROW_L, PR_TOP, PR_RIGHT, PR_BOTTOM, PR_LEFT, PR_INSET, PR_TTRANSFORM, PR_VALIGN, PR_ZINDEX,
    PR_ACONTENT, PR_PITEMS, PR_OVX, PR_OVY, PR_BTW, PR_BRW, PR_BBW, PR_BLW, PR_BTC, PR_BRC, PR_BBC, PR_BLC, PR_PCONTENT, PR_TDLINE,
    PR_GTA, PR_GAREA, PR_OPACITY, PR_MINLINE, PR_MBLOCK, PR_PINLINE, PR_PBLOCK, PR_BGIMAGE, PR_BGSIZE, PR_MASK, PR_CONTENT,
    PR_CUSTOM, PR_QTD
};
static const char *const nomes_prop[] = {
    "", "display", "position", "float", "clear", "width", "height", "min-width", "max-width", "min-height", "max-height",
    "margin", "margin-top", "margin-right", "margin-bottom", "margin-left", "padding", "padding-top", "padding-right", "padding-bottom", "padding-left",
    "border", "border-top", "border-right", "border-bottom", "border-left", "border-width", "border-color", "border-style", "border-radius",
    "color", "background", "background-color", "font", "font-size", "font-weight", "font-style", "text-align", "text-decoration", "line-height",
    "white-space", "list-style", "list-style-type", "visibility", "overflow", "box-sizing",
    "flex", "flex-direction", "flex-wrap", "flex-flow", "justify-content", "align-items", "align-self", "flex-grow", "flex-shrink", "flex-basis", "gap", "row-gap", "column-gap", "order",
    "grid-template-columns", "grid-column", "grid-column-start", "grid-column-end", "grid-row", "top", "right", "bottom", "left", "inset", "text-transform", "vertical-align", "z-index",
    "align-content", "place-items", "overflow-x", "overflow-y", "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
    "border-top-color", "border-right-color", "border-bottom-color", "border-left-color", "place-content", "text-decoration-line",
    "grid-template-areas", "grid-area", "opacity", "-margin-inline", "-margin-block", "-padding-inline", "-padding-block", "background-image", "background-size", "mask-image", "content",
};

static int achar_prop(const char *p, int n)
{
    if (n > 2 && p[0] == '-' && p[1] == '-') return PR_CUSTOM;
    if (n > 8 && igual_n(p, "-webkit-", 8)) { p += 8; n -= 8; }
    static const struct { const char *n; int p; } logicas[] = {
        {"margin-inline-start", PR_ML}, {"margin-inline-end", PR_MR}, {"margin-block-start", PR_MT}, {"margin-block-end", PR_MB},
        {"padding-inline-start", PR_PL}, {"padding-inline-end", PR_PR}, {"padding-block-start", PR_PT}, {"padding-block-end", PR_PB},
        {"margin-inline", PR_MINLINE}, {"margin-block", PR_MBLOCK}, {"padding-inline", PR_PINLINE}, {"padding-block", PR_PBLOCK},
        {"inset-inline-start", PR_LEFT}, {"inset-inline-end", PR_RIGHT}, {"inset-block-start", PR_TOP}, {"inset-block-end", PR_BOTTOM},
        {"inline-size", PR_WIDTH}, {"block-size", PR_HEIGHT}, {"max-inline-size", PR_MAXW}, {"min-inline-size", PR_MINW},
        {"max-block-size", PR_MAXH}, {"min-block-size", PR_MINH}, {"grid-row-start", PR_GROW_L},
    };
    for (unsigned i = 0; i < sizeof logicas / sizeof logicas[0]; i++) if ((int)tam_texto(logicas[i].n) == n && igual_n(p, logicas[i].n, n)) return logicas[i].p;
    for (int i = 1; i < PR_CUSTOM; i++) if ((int)tam_texto(nomes_prop[i]) == n && igual_n(p, nomes_prop[i], n)) return i;
    if (n == 10 && igual_n(p, "grid-gap", 8)) return PR_GAP;
    return PR_NADA;
}

/* Lê um número decimal: devolve x100. */
static int num100(const char *s, int n, int *usado)
{
    int i = 0, neg = 0, v = 0, frac = 0, dig = 0;
    if (i < n && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
    while (i < n && eh_dig(s[i])) { if (v < 10000000) v = v * 10 + (s[i] - '0'); i++; dig++; }
    v *= 100;
    if (i < n && s[i] == '.') {
        i++;
        int m = 10;
        while (i < n && eh_dig(s[i])) { if (m) { frac += (s[i] - '0') * m; m /= 10; } i++; dig++; }
    }
    if (!dig) { *usado = 0; return 0; }
    *usado = i;
    return neg ? -(v + frac) : v + frac;
}

static bool ler_medida_m(const char *s, int n, medida_t *m)
{
    while (n && eh_esp(*s)) { s++; n--; }
    while (n && eh_esp(s[n - 1])) n--;
    if (!n) return false;
    if (n == 4 && igual_n(s, "auto", 4)) { m->u = U_AUTO; m->v = 0; return true; }
    if (n == 4 && igual_n(s, "none", 4)) { m->u = U_NONE; m->v = 0; return true; }
    if (n >= 11 && igual_n(s, "max-content", 11)) { m->u = U_MAXC; return true; }
    if (n >= 11 && igual_n(s, "min-content", 11)) { m->u = U_MINC; return true; }
    if (n >= 11 && igual_n(s, "fit-content", 11)) { m->u = U_FIT; return true; }
    if (n > 5 && igual_n(s, "calc(", 5)) {
        /* calc(A op B): fica com a parte percentual, ou com a soma de px */
        int pct = 0, px = 0, sinal = 1, tem_pct = 0;
        for (int i = 5; i < n;) {
            if (s[i] == '-' && i + 1 < n && eh_esp(s[i + 1])) { sinal = -1; i++; continue; }
            if (s[i] == '+') { sinal = 1; i++; continue; }
            int u; int v = num100(s + i, n - i, &u);
            if (!u) { i++; continue; }
            i += u;
            if (i < n && s[i] == '%') { pct += sinal * v; tem_pct = 1; i++; }
            else if (i + 1 < n && s[i] == 'p' && s[i + 1] == 'x') { px += sinal * v / 100; i += 2; }
            else if (i + 2 < n && s[i] == 'r' && s[i + 1] == 'e' && s[i + 2] == 'm') { px += sinal * v * 16 / 100; i += 3; }
            else if (i + 1 < n && s[i] == 'e' && s[i + 1] == 'm') { px += sinal * v * 16 / 100; i += 2; }
            else if (i + 1 < n && s[i] == 'v' && s[i + 1] == 'w') { pct += sinal * v; tem_pct = 1; i += 2; }
            sinal = 1;
        }
        if (tem_pct) { m->u = U_PCT; m->v = pct + (px > 0 ? 0 : 0); m->v = pct; if (px < 0) m->v = pct - 1; return true; }
        m->u = U_PX; m->v = px;
        return true;
    }
    if (n > 4 && (igual_n(s, "min(", 4) || igual_n(s, "max(", 4) || igual_n(s, "clamp(", 6))) {
        /* fica com o primeiro valor em px, ou o percentual */
        int i = igual_n(s, "clamp(", 6) ? 6 : 4;
        int fim = i;
        while (fim < n && s[fim] != ',' && s[fim] != ')') fim++;
        if (igual_n(s, "clamp(", 6)) {   /* clamp(min, ideal, max): usa o ideal */
            i = fim + 1; fim = i;
            while (fim < n && s[fim] != ',' && s[fim] != ')') fim++;
        }
        return ler_medida_m(s + i, fim - i, m);
    }
    int u; int v = num100(s, n, &u);
    if (!u) return false;
    const char *un = s + u; int nu = n - u;
    if (nu == 0) { m->u = v == 0 ? U_PX : U_PX; m->v = v / 100; return true; }
    if (nu == 2 && igual_n(un, "px", 2)) { m->u = U_PX; m->v = v / 100; return true; }
    if (nu == 1 && un[0] == '%') { m->u = U_PCT; m->v = v; return true; }
    if (nu == 2 && igual_n(un, "em", 2)) { m->u = U_EM; m->v = v; return true; }
    if (nu == 3 && igual_n(un, "rem", 3)) { m->u = U_REM; m->v = v; return true; }
    if (nu == 2 && igual_n(un, "vw", 2)) { m->u = U_VW; m->v = v; return true; }
    if (nu == 2 && igual_n(un, "vh", 2)) { m->u = U_VH; m->v = v; return true; }
    if (nu == 4 && igual_n(un, "dvh", 3)) { m->u = U_VH; m->v = v; return true; }
    if (nu == 3 && (igual_n(un, "dvh", 3) || igual_n(un, "svh", 3) || igual_n(un, "lvh", 3))) { m->u = U_VH; m->v = v; return true; }
    if (nu == 2 && igual_n(un, "pt", 2)) { m->u = U_PX; m->v = v * 4 / 300; return true; }
    if (nu == 2 && igual_n(un, "fr", 2)) { m->u = U_FR; m->v = v; return true; }
    if (nu == 2 && igual_n(un, "ch", 2)) { m->u = U_EM; m->v = v / 2; return true; }
    if (nu == 2 && igual_n(un, "ex", 2)) { m->u = U_EM; m->v = v / 2; return true; }
    return false;
}

static const struct { const char *n; uint32_t c; } cores_nome[] = {
    {"black",0x000000},{"white",0xFFFFFF},{"red",0xFF0000},{"green",0x008000},{"blue",0x0000FF},{"gray",0x808080},{"grey",0x808080},
    {"silver",0xC0C0C0},{"yellow",0xFFFF00},{"orange",0xFFA500},{"purple",0x800080},{"navy",0x000080},{"teal",0x008080},
    {"maroon",0x800000},{"olive",0x808000},{"lime",0x00FF00},{"aqua",0x00FFFF},{"cyan",0x00FFFF},{"fuchsia",0xFF00FF},
    {"magenta",0xFF00FF},{"pink",0xFFC0CB},{"brown",0xA52A2A},{"lightgray",0xD3D3D3},{"lightgrey",0xD3D3D3},{"darkgray",0xA9A9A9},
    {"darkgrey",0xA9A9A9},{"whitesmoke",0xF5F5F5},{"gainsboro",0xDCDCDC},{"dimgray",0x696969},{"darkblue",0x00008B},
    {"darkgreen",0x006400},{"darkred",0x8B0000},{"gold",0xFFD700},{"beige",0xF5F5DC},{"ivory",0xFFFFF0},{"lightblue",0xADD8E6},
    {"skyblue",0x87CEEB},{"steelblue",0x4682B4},{"royalblue",0x4169E1},{"dodgerblue",0x1E90FF},{"crimson",0xDC143C},{"tomato",0xFF6347},
    {"coral",0xFF7F50},{"salmon",0xFA8072},{"khaki",0xF0E68C},{"indigo",0x4B0082},{"violet",0xEE82EE},{"tan",0xD2B48C},
    {"lightyellow",0xFFFFE0},{"lightgreen",0x90EE90},{"aliceblue",0xF0F8FF},{"ghostwhite",0xF8F8FF},{"snow",0xFFFAFA},
    {"linen",0xFAF0E6},{"seagreen",0x2E8B57},{"slategray",0x708090},{"darkslategray",0x2F4F4F},{"midnightblue",0x191970},
    {"forestgreen",0x228B22},{"firebrick",0xB22222},{"chocolate",0xD2691E},{"orangered",0xFF4500},{"hotpink",0xFF69B4},
    {"lightcoral",0xF08080},{"honeydew",0xF0FFF0},{"mintcream",0xF5FFFA},{"lavender",0xE6E6FA},{"wheat",0xF5DEB3},
};

/* Cor: devolve true e *c = 0xAARRGGBB (AA = 0 transparente). */
static bool ler_cor(const char *s, int n, uint32_t *c, uint32_t atual)
{
    while (n && eh_esp(*s)) { s++; n--; }
    while (n && eh_esp(s[n - 1])) n--;
    if (!n) return false;
    if (s[0] == '#') {
        uint32_t v = 0; int d = 0;
        for (int i = 1; i < n; i++) {
            char h = (char)(s[i] | 32);
            int x = eh_dig(s[i]) ? s[i] - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : -1;
            if (x < 0) return false;
            v = v * 16 + (uint32_t)x; d++;
        }
        if (d == 3 || d == 4) {
            uint32_t r = (v >> (d == 4 ? 12 : 8)) & 15, g = (v >> (d == 4 ? 8 : 4)) & 15, b = (v >> (d == 4 ? 4 : 0)) & 15;
            uint32_t a = d == 4 ? (v & 15) * 17 : 255;
            *c = (a << 24) | (r * 17 << 16) | (g * 17 << 8) | (b * 17);
            return true;
        }
        if (d == 6) { *c = 0xFF000000u | v; return true; }
        if (d == 8) { *c = ((v & 255) << 24) | (v >> 8); return true; }
        return false;
    }
    if ((n > 4 && igual_n(s, "rgb(", 4)) || (n > 5 && igual_n(s, "rgba(", 5))) {
        int i = s[3] == 'a' ? 5 : 4, comp[4] = { 0, 0, 0, 100 }, k = 0;
        while (i < n && k < 4) {
            while (i < n && (eh_esp(s[i]) || s[i] == ',' || s[i] == '/')) i++;
            int u; int v = num100(s + i, n - i, &u);
            if (!u) break;
            i += u;
            if (i < n && s[i] == '%') { v = k < 3 ? v * 255 / 10000 * 100 : v / 100; i++; }
            if (k < 3) comp[k] = v / 100; else comp[k] = v;
            k++;
        }
        if (k < 3) return false;
        int a = k == 4 ? comp[3] : 100;
        if (a > 100) a = 100;
        if (a < 0) a = 0;
        uint32_t A = (uint32_t)(a * 255 / 100);
        for (int q = 0; q < 3; q++) comp[q] = comp[q] < 0 ? 0 : comp[q] > 255 ? 255 : comp[q];
        *c = (A << 24) | ((uint32_t)comp[0] << 16) | ((uint32_t)comp[1] << 8) | (uint32_t)comp[2];
        return true;
    }
    if ((n > 4 && igual_n(s, "hsl(", 4)) || (n > 5 && igual_n(s, "hsla(", 5))) {
        int i = s[3] == 'a' ? 5 : 4, comp[4] = { 0, 0, 0, 100 }, k = 0;
        while (i < n && k < 4) {
            while (i < n && (eh_esp(s[i]) || s[i] == ',' || s[i] == '/')) i++;
            int u; int v = num100(s + i, n - i, &u);
            if (!u) break;
            i += u;
            while (i < n && eh_letra(s[i])) i++;
            if (i < n && s[i] == '%') i++;
            comp[k++] = v / 100;
        }
        if (k < 3) return false;
        int h = ((comp[0] % 360) + 360) % 360, sat = comp[1], l = comp[2];
        int cc = (100 - (2 * l - 100 < 0 ? 100 - 2 * l : 2 * l - 100)) * sat / 100;   /* croma x100 */
        int hp = h * 100 / 60, x = cc * (100 - (hp % 200 - 100 < 0 ? 100 - hp % 200 : hp % 200 - 100)) / 100;
        int r = 0, g = 0, b = 0;
        if (hp < 100) { r = cc; g = x; } else if (hp < 200) { r = x; g = cc; } else if (hp < 300) { g = cc; b = x; }
        else if (hp < 400) { g = x; b = cc; } else if (hp < 500) { r = x; b = cc; } else { r = cc; b = x; }
        int mm = l - cc / 2;
        r = (r + mm) * 255 / 100; g = (g + mm) * 255 / 100; b = (b + mm) * 255 / 100;
        r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
        int a = k == 4 ? (comp[3] <= 1 ? comp[3] * 100 : comp[3]) : 100;
        *c = ((uint32_t)(a * 255 / 100) << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        return true;
    }
    if (n == 11 && igual_n(s, "transparent", 11)) { *c = 0; return true; }
    if (n == 12 && igual_n(s, "currentcolor", 12)) { *c = atual | 0xFF000000u; return true; }
    for (unsigned i = 0; i < sizeof cores_nome / sizeof cores_nome[0]; i++)
        if ((int)tam_texto(cores_nome[i].n) == n && igual_n(s, cores_nome[i].n, n)) { *c = 0xFF000000u | cores_nome[i].c; return true; }
    return false;
}

/* Divide valor em palavras (respeitando parênteses). */
static int palavras(const char *s, int n, const char **p, int *t, int max)
{
    int k = 0, i = 0;
    while (i < n && k < max) {
        while (i < n && (eh_esp(s[i]) || s[i] == ',')) i++;
        if (i >= n) break;
        int ini = i, prof = 0;
        while (i < n && (prof || (!eh_esp(s[i]) && s[i] != ','))) {
            if (s[i] == '(') prof++;
            else if (s[i] == ')') prof--;
            i++;
        }
        p[k] = s + ini; t[k] = i - ini; k++;
    }
    return k;
}

/* ================================================================ seletores */

static const char *pular_esp(const char *s, const char *f) { while (s < f && eh_esp(*s)) s++; return s; }

static bool ler_composto(motor_t *m, const char **ps, const char *f, composto_t *c)
{
    const char *s = *ps;
    bool algo = false;
    memset(c, 0, sizeof *c);
    while (s < f) {
        if (*s == '*') { s++; algo = true; continue; }
        if (eh_letra(*s) || *s == '-' || *s == '_') {
            int k = 0;
            while (s < f && (eh_letra(*s) || eh_dig(*s) || *s == '-' || *s == '_')) { if (k < 11) c->tag[k++] = minusc(*s); s++; }
            c->tag[k] = 0; algo = true; continue;
        }
        if (*s == '#' || *s == '.') {
            char t = *s++;
            const char *i0 = s;
            while (s < f && (eh_letra(*s) || eh_dig(*s) || *s == '-' || *s == '_' || *s == '\\' || (uint8_t)*s >= 0x80)) { if (*s == '\\' && s + 1 < f) s++; s++; }
            char *nm = m_dup(m, i0, (int)(s - i0));
            if (t == '#') c->id = nm; else if (c->nc < 6) c->classes[c->nc++] = nm; else c->nunca = 1;
            algo = true; continue;
        }
        if (*s == '[') {
            s++;
            const char *a0 = pular_esp(s, f); s = a0;
            while (s < f && *s != ']' && *s != '=' && *s != '~' && *s != '^' && *s != '$' && *s != '*' && *s != '|' && !eh_esp(*s)) s++;
            char *nome = m_dup(m, a0, (int)(s - a0));
            for (char *p = nome; *p; p++) *p = minusc(*p);
            s = pular_esp(s, f);
            char op = 0; const char *val = NULL;
            if (s < f && *s != ']') {
                op = *s == '=' ? '=' : *s;
                if (*s != '=') s++;
                if (s < f && *s == '=') s++;
                s = pular_esp(s, f);
                if (s < f && (*s == '"' || *s == '\'')) {
                    char q = *s++; const char *v0 = s;
                    while (s < f && *s != q) s++;
                    val = m_dup(m, v0, (int)(s - v0));
                    if (s < f) s++;
                } else {
                    const char *v0 = s;
                    while (s < f && *s != ']' && !eh_esp(*s)) s++;
                    val = m_dup(m, v0, (int)(s - v0));
                }
            }
            while (s < f && *s != ']') s++;
            if (s < f) s++;
            if (c->na < 3) { c->atr_nome[c->na] = nome; c->atr_valor[c->na] = val; c->atr_op[c->na] = op; c->na++; }
            algo = true; continue;
        }
        if (*s == ':') {
            s++;
            bool elem = s < f && *s == ':';
            if (elem) s++;
            const char *p0 = s;
            while (s < f && (eh_letra(*s) || *s == '-')) s++;
            int np = (int)(s - p0);
            const char *arg = NULL; int na = 0;
            if (s < f && *s == '(') {
                int prof = 1; arg = ++s;
                while (s < f && prof) { if (*s == '(') prof++; else if (*s == ')') prof--; s++; }
                na = (int)(s - arg) - 1;
            }
            if (np == 6 && igual_n(p0, "before", 6)) { c->pseudo = 1; algo = true; continue; }
            if (np == 5 && igual_n(p0, "after", 5)) { c->pseudo = 2; algo = true; continue; }
            if (elem) { c->nunca = 1; algo = true; continue; }
            if (np == 11 && igual_n(p0, "first-child", 11)) c->primeiro = 1;
            else if (np == 10 && igual_n(p0, "last-child", 10)) c->ultimo = 1;
            else if (np == 4 && igual_n(p0, "root", 4)) c->raiz = 1;
            else if (np == 5 && igual_n(p0, "empty", 5)) c->vazio = 1;
            else if (np == 4 && igual_n(p0, "link", 4)) { if (c->na < 3) { c->atr_nome[c->na] = "href"; c->atr_op[c->na] = 0; c->na++; } }
            else if (np == 9 && igual_n(p0, "nth-child", 9) && arg) {
                c->tem_nth = 1;
                if (na >= 3 && igual_n(arg, "odd", 3)) { c->nth_a = 2; c->nth_b = 1; }
                else if (na >= 4 && igual_n(arg, "even", 4)) { c->nth_a = 2; c->nth_b = 0; }
                else {
                    int u, a = 0, b = 0;
                    const char *q = arg, *qf = arg + na;
                    int v = num100(q, (int)(qf - q), &u);
                    if (u && q + u < qf && q[u] == 'n') { a = v / 100; q += u + 1; }
                    else if (q < qf && *q == 'n') { a = 1; q++; }
                    else if (q + 1 < qf && q[0] == '-' && q[1] == 'n') { a = -1; q += 2; }
                    else if (u) { b = v / 100; q = qf; }
                    q = pular_esp(q, qf);
                    if (q < qf && (*q == '+' || *q == '-')) { bool neg = *q == '-'; q = pular_esp(q + 1, qf); v = num100(q, (int)(qf - q), &u); b = neg ? -v / 100 : v / 100; }
                    c->nth_a = a; c->nth_b = b;
                }
            } else if (np == 3 && igual_n(p0, "not", 3) && arg) {
                const char *q = pular_esp(arg, arg + na);
                if (*q == '.') c->nao_classe = m_dup(m, q + 1, na - (int)(q + 1 - arg));
                else if (eh_letra(*q)) c->nao_tag = m_dup(m, q, na - (int)(q - arg));
            } else if ((np == 2 && igual_n(p0, "is", 2)) || (np == 5 && igual_n(p0, "where", 5))) {
                /* :is(a, b): aceita se o primeiro for uma classe ou tag simples */
                const char *q = pular_esp(arg, arg + na);
                const char *fim = q;
                while (fim < arg + na && *fim != ',' && *fim != ')') fim++;
                composto_t sub;
                const char *qq = q;
                if (ler_composto(m, &qq, fim, &sub) && qq == fim) {
                    if (sub.tag[0] && !c->tag[0]) copia_texto(c->tag, sub.tag, sizeof c->tag);
                    for (int z = 0; z < sub.nc && c->nc < 6; z++) c->classes[c->nc++] = sub.classes[z];
                } else c->nunca = 1;
            } else if ((np == 7 && igual_n(p0, "visited", 7)) || (np == 4 && igual_n(p0, "lang", 4)) || (np == 3 && igual_n(p0, "dir", 3))) {
            } else c->nunca = 1;   /* :hover, :focus, :checked... */
            algo = true;
            continue;
        }
        break;
    }
    *ps = s;
    return algo;
}

static bool ler_seletor(motor_t *m, const char *s, const char *f, seletor_t *sel)
{
    memset(sel, 0, sizeof *sel);
    composto_t tmp[MAX_COMP]; int n = 0;
    char comb = 0;
    s = pular_esp(s, f);
    while (s < f) {
        if (n == MAX_COMP) return false;
        if (!ler_composto(m, &s, f, &tmp[n])) return false;
        tmp[n].comb = comb;
        n++;
        const char *a = s;
        s = pular_esp(s, f);
        if (s >= f) break;
        if (*s == '>' || *s == '+' || *s == '~') { comb = *s; s = pular_esp(s + 1, f); }
        else if (s > a) comb = ' ';
        else return false;
    }
    if (!n) return false;
    /* guarda da direita para a esquerda */
    for (int i = 0; i < n; i++) {
        sel->c[i] = tmp[n - 1 - i];
        sel->c[i].comb = tmp[n - 1 - i].comb;
        composto_t *c = &sel->c[i];
        sel->espec += (c->id ? 10000 : 0) + (c->nc + c->na + c->primeiro + c->ultimo + c->tem_nth + c->raiz + (c->nao_tag || c->nao_classe)) * 100 + (c->tag[0] ? 1 : 0);
    }
    sel->n = n;
    return true;
}

static bool tem_classe(const char *lista, const char *c)
{
    if (!lista) return false;
    int n = (int)tam_texto(c);
    for (const char *p = lista; *p;) {
        while (*p && eh_esp(*p)) p++;
        const char *i = p;
        while (*p && !eh_esp(*p)) p++;
        if (p - i == n && memcmp(i, c, (size_t)n) == 0) return true;
    }
    return false;
}

static no_m *irmao_ant(no_m *n) { for (n = n->ant; n; n = n->ant) if (!n->texto) return n; return NULL; }
static no_m *irmao_prox(no_m *n) { for (n = n->prox; n; n = n->prox) if (!n->texto) return n; return NULL; }

static bool bate_composto_m(const composto_t *c, no_m *n)
{
    if (n->texto || c->nunca) return false;
    if (c->tag[0] && !igual(c->tag, n->tag)) return false;
    if (c->id && (!n->id || !igual(c->id, n->id))) return false;
    for (int i = 0; i < c->nc; i++) if (!tem_classe(n->classe, c->classes[i])) return false;
    for (int i = 0; i < c->na; i++) {
        const char *v = atributo_m(n, c->atr_nome[i]);
        if (!v) return false;
        if (!c->atr_op[i]) continue;
        const char *w = c->atr_valor[i];
        int lv = (int)tam_texto(v), lw = (int)tam_texto(w);
        switch (c->atr_op[i]) {
        case '=': if (!igual(v, w)) return false; break;
        case '~': if (!tem_classe(v, w)) return false; break;
        case '^': if (lv < lw || memcmp(v, w, (size_t)lw)) return false; break;
        case '$': if (lv < lw || memcmp(v + lv - lw, w, (size_t)lw)) return false; break;
        case '*': { bool ok = false; for (int k = 0; k + lw <= lv; k++) if (!memcmp(v + k, w, (size_t)lw)) { ok = true; break; } if (!ok) return false; break; }
        case '|': if (!(igual(v, w) || (lv > lw && !memcmp(v, w, (size_t)lw) && v[lw] == '-'))) return false; break;
        default: return false;
        }
    }
    if (c->primeiro && irmao_ant(n)) return false;
    if (c->ultimo && irmao_prox(n)) return false;
    if (c->raiz && !(igual(n->tag, "html"))) return false;
    if (c->vazio && n->filho) return false;
    if (c->tem_nth) {
        int pos = 1;
        for (no_m *q = irmao_ant(n); q; q = irmao_ant(q)) pos++;
        int d = pos - c->nth_b;
        if (c->nth_a == 0) { if (d) return false; }
        else if (d % c->nth_a || d / c->nth_a < 0) return false;
    }
    if (c->nao_tag && igual(c->nao_tag, n->tag)) return false;
    if (c->nao_classe && tem_classe(n->classe, c->nao_classe)) return false;
    return true;
}

static bool bate_de(const seletor_t *s, int i, no_m *n)
{
    if (!bate_composto_m(&s->c[i], n)) return false;
    if (i + 1 >= s->n) return true;
    char comb = s->c[i].comb;
    if (comb == '>') return n->pai && bate_de(s, i + 1, n->pai);
    if (comb == '+') { no_m *a = irmao_ant(n); return a && bate_de(s, i + 1, a); }
    if (comb == '~') { for (no_m *a = irmao_ant(n); a; a = irmao_ant(a)) if (bate_de(s, i + 1, a)) return true; return false; }
    for (no_m *a = n->pai; a; a = a->pai) if (bate_de(s, i + 1, a)) return true;
    return false;
}

/* ================================================================ folhas */

static unsigned hash_s(const char *s) { unsigned h = 2166136261u; while (*s) h = (h ^ (uint8_t)*s++) * 16777619u; return h; }

static balde_t *balde(motor_t *m, const char *chave, bool criar)
{
    if (!m->cap_baldes) {
        if (!criar) return NULL;
        m->cap_baldes = 4096;
        m->baldes = m_novo(m, m->cap_baldes * (int)sizeof(balde_t));
        if (!m->baldes) { m->cap_baldes = 0; return NULL; }
    }
    unsigned h = hash_s(chave) & (unsigned)(m->cap_baldes - 1);
    for (int i = 0; i < m->cap_baldes; i++) {
        balde_t *b = &m->baldes[(h + (unsigned)i) & (unsigned)(m->cap_baldes - 1)];
        if (!b->chave) {
            if (!criar) return NULL;
            b->chave = m_dup(m, chave, (int)tam_texto(chave));
            return b;
        }
        if (igual(b->chave, chave)) return b;
    }
    return NULL;
}

static void indexar(motor_t *m, int r)
{
    composto_t *c = &m->regras[r].sel.c[0];
    char ch[80];
    if (c->id) formatar_texto(ch, sizeof ch, "#%s", c->id);
    else if (c->nc) formatar_texto(ch, sizeof ch, ".%s", c->classes[0]);
    else if (c->tag[0]) copia_texto(ch, c->tag, sizeof ch);
    else {
        crescer_v(m, (void **)&m->univ, &m->cap_univ, m->n_univ + 1, sizeof(int));
        m->univ[m->n_univ++] = r;
        return;
    }
    balde_t *b = balde(m, ch, true);
    if (!b) return;
    if (!crescer_v(m, (void **)&b->regras, &b->cap, b->n + 1, sizeof(int))) return;
    b->regras[b->n++] = r;
}

static int ler_decls(motor_t *m, const char *s, int n, decl_t *out, int max)
{
    int k = 0, i = 0;
    while (i < n && k < max) {
        while (i < n && (eh_esp(s[i]) || s[i] == ';')) i++;
        int p0 = i;
        while (i < n && s[i] != ':' && s[i] != ';') i++;
        if (i >= n || s[i] != ':') { while (i < n && s[i] != ';') i++; continue; }
        int p1 = i;
        while (p1 > p0 && eh_esp(s[p1 - 1])) p1--;
        i++;
        int v0 = i, prof = 0;
        while (i < n && (prof || s[i] != ';')) {
            if (s[i] == '(') prof++;
            else if (s[i] == ')') prof--;
            else if (s[i] == '"' || s[i] == '\'') { char q = s[i++]; while (i < n && s[i] != q) i++; }
            i++;
        }
        int v1 = i < n ? i : n;
        while (v1 > v0 && eh_esp(s[v1 - 1])) v1--;
        while (v0 < v1 && eh_esp(s[v0])) v0++;
        bool imp = false;
        if (v1 - v0 >= 10 && igual_n(s + v1 - 10, "!important", 10)) { imp = true; v1 -= 10; while (v1 > v0 && eh_esp(s[v1 - 1])) v1--; }
        int prop = achar_prop(s + p0, p1 - p0);
        if (!prop) continue;
        if (prop == PR_CUSTOM) {
            /* "--nome:valor" num valor só */
            int tn = p1 - p0, tv = v1 - v0;
            char *x = m_novo(m, tn + tv + 2);
            if (!x) continue;
            memcpy(x, s + p0, (size_t)tn); x[tn] = 0;
            memcpy(x + tn + 1, s + v0, (size_t)tv); x[tn + 1 + tv] = 0;
            out[k].valor = x;
        } else out[k].valor = m_dup(m, s + v0, v1 - v0);
        out[k].prop = prop;
        out[k].importante = imp;
        k++;
    }
    return k;
}

static bool media_ok(motor_t *m, const char *s, int n);

static void ler_folha(motor_t *m, const char *s, int n, int prof);

static void ler_regra(motor_t *m, const char *sel, int ns, const char *d, int nd)
{
    static decl_t tmp[256];
    int k = ler_decls(m, d, nd, tmp, 256);
    if (!k) return;
    decl_t *dd = m_novo(m, k * (int)sizeof(decl_t));
    if (!dd) return;
    memcpy(dd, tmp, (size_t)k * sizeof(decl_t));
    /* lista de seletores separados por vírgula */
    const char *f = sel + ns;
    for (const char *a = sel; a < f;) {
        const char *b = a; int prof = 0;
        while (b < f && (prof || *b != ',')) { if (*b == '(') prof++; else if (*b == ')') prof--; b++; }
        if (!crescer_v(m, (void **)&m->regras, &m->cap_regras, m->n_regras + 1, sizeof(regra_m))) return;
        regra_m *r = &m->regras[m->n_regras];
        if (ler_seletor(m, a, b, &r->sel)) {
            r->d = dd; r->nd = k; r->ordem = m->n_regras;
            indexar(m, m->n_regras);
            m->n_regras++;
        }
        a = b + 1;
    }
}

/* Folha de estilo. As regras dentro de @media só entram se a mídia vale
 * para a largura atual (o motor relê as folhas quando a largura muda). */
static void ler_folha(motor_t *m, const char *s, int n, int prof)
{
    int i = 0;
    while (i < n) {
        while (i < n && (eh_esp(s[i]) || s[i] == ';')) i++;
        if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i += 2;
            continue;
        }
        if (i >= n) break;
        if (s[i] == '<' || s[i] == '-') { if (i + 3 < n && (igual_n(s + i, "<!--", 4) || igual_n(s + i, "-->", 3))) { i += s[i] == '<' ? 4 : 3; continue; } }
        int ini = i;
        while (i < n && s[i] != '{' && s[i] != ';') {
            if (s[i] == '"' || s[i] == '\'') { char q = s[i++]; while (i < n && s[i] != q) i++; }
            i++;
        }
        if (i >= n) break;
        if (s[i] == ';') { i++; continue; }   /* @import, @charset: ignorados */
        int cab_fim = i;
        /* acha o fim do bloco */
        int b0 = ++i, nivel = 1;
        while (i < n && nivel) {
            if (s[i] == '{') nivel++;
            else if (s[i] == '}') nivel--;
            else if (s[i] == '"' || s[i] == '\'') { char q = s[i++]; while (i < n && s[i] != q) i++; }
            else if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++; i++; }
            i++;
        }
        int b1 = nivel ? n : i - 1;
        const char *cab = s + ini; int nc = cab_fim - ini;
        while (nc && eh_esp(cab[nc - 1])) nc--;
        if (nc && cab[0] == '@') {
            if (prof < 4 && nc >= 6 && igual_n(cab, "@media", 6)) { if (media_ok(m, cab + 6, nc - 6)) ler_folha(m, s + b0, b1 - b0, prof + 1); }
            else if (prof < 4 && ((nc >= 9 && igual_n(cab, "@supports", 9)) || (nc >= 6 && igual_n(cab, "@layer", 6)) || (nc >= 10 && igual_n(cab, "@container", 10))))
                ler_folha(m, s + b0, b1 - b0, prof + 1);
            continue;
        }
        if (nc) ler_regra(m, cab, nc, s + b0, b1 - b0);
    }
}

static bool media_ok(motor_t *m, const char *s, int n)
{
    /* vírgula = "ou" */
    for (int a = 0; a < n;) {
        int b = a, prof = 0;
        while (b < n && (prof || s[b] != ',')) { if (s[b] == '(') prof++; else if (s[b] == ')') prof--; b++; }
        bool ok = true, nao = false;
        int i = a;
        while (i < b) {
            while (i < b && eh_esp(s[i])) i++;
            if (i >= b) break;
            if (s[i] == '(') {
                int f = i + 1, p = 1;
                while (f < b && p) { if (s[f] == '(') p++; else if (s[f] == ')') p--; f++; }
                const char *q = s + i + 1; int nq = f - i - 2;
                int dp = 0;
                while (dp < nq && q[dp] != ':' && q[dp] != '<' && q[dp] != '>') dp++;
                medida_t md = { 0, U_AUTO };
                int v = 0;
                if (dp < nq && q[dp] == ':' && ler_medida_m(q + dp + 1, nq - dp - 1, &md)) v = md.u == U_EM || md.u == U_REM ? md.v * 16 / 100 : md.v;
                const char *nm = q; int nn = dp;
                while (nn && eh_esp(nm[nn - 1])) nn--;
                bool r = true;
                if (nn == 9 && igual_n(nm, "min-width", 9)) r = m->vw >= v;
                else if (nn == 9 && igual_n(nm, "max-width", 9)) r = m->vw <= v;
                else if (nn == 10 && igual_n(nm, "min-height", 10)) r = m->vh >= v;
                else if (nn == 10 && igual_n(nm, "max-height", 10)) r = m->vh <= v;
                else if (nn == 11 && igual_n(nm, "orientation", 11)) r = dp + 1 < nq && (q[dp + 1 + (q[dp + 1] == ' ')] == 'l') == (m->vw >= m->vh);
                else if (nn == 20 && igual_n(nm, "prefers-color-scheme", 20)) { const char *w = q + dp + 1; while (eh_esp(*w)) w++; r = *w == 'l'; }
                else if (nn == 22 && igual_n(nm, "prefers-reduced-motion", 22)) r = false;
                else if ((nn == 5 && igual_n(nm, "hover", 5)) || (nn == 7 && igual_n(nm, "pointer", 7))) r = true;
                else if (dp < nq && (q[dp] == '<' || q[dp] == '>')) {   /* (width >= 600px) */
                    bool maior = q[dp] == '>';
                    int e = dp + 1; if (e < nq && q[e] == '=') e++;
                    if (ler_medida_m(q + e, nq - e, &md)) { v = md.v; r = maior ? m->vw >= v : m->vw <= v; }
                } else r = false;
                ok = ok && r;
                i = f;
                continue;
            }
            int w0 = i;
            while (i < b && !eh_esp(s[i]) && s[i] != '(') i++;
            int nw = i - w0;
            if (nw == 3 && igual_n(s + w0, "not", 3)) nao = true;
            else if (nw == 5 && igual_n(s + w0, "print", 5)) ok = false;
            else if (nw == 6 && igual_n(s + w0, "speech", 6)) ok = false;
        }
        if (nao) ok = !ok;
        if (ok) return true;
        a = b + 1;
    }
    return false;
}

static const char folha_padrao[] =
    "html,body,div,p,h1,h2,h3,h4,h5,h6,ul,ol,dl,dt,dd,form,section,article,header,footer,nav,main,aside,figure,figcaption,"
    "blockquote,pre,address,hr,fieldset,legend,details,summary,center,menu,dialog,hgroup,search,optgroup{display:block}"
    "head,script,style,link,meta,title,template,noscript,base,datalist,param,source,track,area,map,[hidden],input[type=hidden],option{display:none}"
    "li{display:list-item}table{display:table}thead,tbody,tfoot{display:table-row-group}tr{display:table-row}"
    "td,th{display:table-cell;padding:2px 4px}th{font-weight:bold;text-align:center}caption{display:table-caption;text-align:center}"
    "body{margin:8px;color:#000}p,dl,ul,ol,menu,pre,fieldset{margin:1em 0}blockquote,figure{margin:1em 40px}"
    "h1{font-size:2em;margin:.67em 0;font-weight:bold}h2{font-size:1.5em;margin:.83em 0;font-weight:bold}"
    "h3{font-size:1.17em;margin:1em 0;font-weight:bold}h4{margin:1.33em 0;font-weight:bold}"
    "h5{font-size:.83em;margin:1.67em 0;font-weight:bold}h6{font-size:.67em;margin:2.33em 0;font-weight:bold}"
    "ul,ol,menu{padding-left:40px}ul{list-style-type:disc}ol{list-style-type:decimal}li ul,li ol{margin:0}dd{margin-left:40px}"
    "b,strong{font-weight:bold}i,em,cite,var,dfn,address{font-style:italic}u,ins{text-decoration:underline}s,strike,del{text-decoration:line-through}"
    "table{text-align:left}center table{margin-left:auto;margin-right:auto}a:link{color:#0b57d0;text-decoration:underline}small,sub,sup{font-size:.83em}big{font-size:1.17em}pre{white-space:pre}"
    "hr{border:1px solid #c4cbd2;margin:.5em 0}center{text-align:center}mark{background-color:#ff0}"
    "img,video,canvas,iframe,input,button,select,textarea,object,embed,meter,progress{display:inline-block}"
    "input,select,textarea{border:1px solid #8a939c;padding:2px 4px;background-color:#fff;color:#000}"
    "button,input[type=submit],input[type=button],input[type=reset]{border:1px solid #8a939c;padding:2px 8px;background-color:#e2e6ea;text-align:center}"
    "input[type=checkbox],input[type=radio]{padding:0}textarea{white-space:pre}summary{font-weight:bold}legend{padding:0 2px}fieldset{border:1px solid #c4cbd2;padding:4px 8px}";

/* ================================================================ cascata */

static void aplicar(motor_t *m, no_m *n, int prop, const char *v, const estilo_t *pai);

static const char *subst_vars(motor_t *m, no_m *n, const char *v)
{
    const char *p = v;
    bool tem = false;
    for (; *p; p++) if (p[0] == 'v' && p[1] == 'a' && p[2] == 'r' && p[3] == '(') { tem = true; break; }
    if (!tem) return v;
    char buf[512]; int k = 0;
    for (int volta = 0; volta < 4; volta++) {
        k = 0;
        for (p = v; *p && k < 500;) {
            if (p[0] == 'v' && p[1] == 'a' && p[2] == 'r' && p[3] == '(') {
                const char *a = p + 4;
                while (eh_esp(*a)) a++;
                const char *b = a;
                while (*b && *b != ',' && *b != ')' && !eh_esp(*b)) b++;
                int prof = 1; const char *f = p + 4;
                while (*f && prof) { if (*f == '(') prof++; else if (*f == ')') prof--; f++; }
                const char *val = NULL;
                for (no_m *q = n; q && !val; q = q->pai)
                    for (var_m_t *x = q->vars; x; x = x->prox)
                        if ((int)tam_texto(x->nome) == b - a && !memcmp(x->nome, a, (size_t)(b - a))) { val = x->valor; break; }
                if (!val) {   /* reserva: var(--x, reserva) */
                    const char *c = b;
                    while (eh_esp(*c)) c++;
                    if (*c == ',') {
                        c++;
                        int nn = (int)(f - 1 - c);
                        if (nn > 0 && k + nn < 500) { memcpy(buf + k, c, (size_t)nn); k += nn; }
                    }
                } else {
                    int nn = (int)tam_texto(val);
                    if (k + nn < 500) { memcpy(buf + k, val, (size_t)nn); k += nn; }
                }
                p = f;
                continue;
            }
            buf[k++] = *p++;
        }
        buf[k] = 0;
        v = m_dup(m, buf, k);
        bool mais = false;
        for (p = v; *p; p++) if (p[0] == 'v' && p[1] == 'a' && p[2] == 'r' && p[3] == '(') { mais = true; break; }
        if (!mais) break;
    }
    return v;
}

typedef struct { const decl_t *d; int espec, ordem; } achado_t;

static int comparar_achado(const achado_t *a, const achado_t *b)
{
    int ia = a->d->importante, ib = b->d->importante;
    if (ia != ib) return ia - ib;
    if (a->espec != b->espec) return a->espec - b->espec;
    return a->ordem - b->ordem;
}

static void estilo_inicial(estilo_t *e, const estilo_t *pai)
{
    memset(e, 0, sizeof *e);
    e->display = D_INLINE;
    e->larg.u = e->alt.u = U_AUTO; e->min_l.u = e->min_a.u = U_PX; e->max_l.u = e->max_a.u = U_NONE; e->base.u = U_AUTO;
    for (int i = 0; i < 4; i++) { e->marg[i].u = U_PX; e->pad[i].u = U_PX; e->pos[i].u = U_AUTO; }
    e->encolhe = 100;
    e->vao_l.u = e->vao_c.u = U_PX;
    e->g_span = 1; e->g_lspan = 1;
    e->dir = 0;
    if (pai) {
        e->cor = pai->cor; e->tam = pai->tam; e->peso = pai->peso; e->italico = pai->italico; e->alinha = pai->alinha;
        e->branco = pai->branco; e->lista = pai->lista; e->visivel = pai->visivel; e->linha = pai->linha; e->maiusc = pai->maiusc;
        e->sublinha = pai->sublinha; e->risca = pai->risca;
    } else { e->cor = 0x000000; e->tam = 16; e->linha.u = U_AUTO; }
}

static int px_de(const medida_t *m, int tam)
{
    switch (m->u) {
    case U_PX: return m->v;
    case U_EM: return m->v * tam / 100;
    case U_REM: return m->v * 16 / 100;
    default: return 0;
    }
}

static void quatro(const char *v, medida_t *o, int tam)
{
    const char *p[4]; int t[4];
    int k = palavras(v, (int)tam_texto(v), p, t, 4);
    medida_t x[4];
    for (int i = 0; i < k; i++) if (!ler_medida_m(p[i], t[i], &x[i])) return;
    if (!k) return;
    if (k == 1) x[1] = x[2] = x[3] = x[0];
    else if (k == 2) { x[2] = x[0]; x[3] = x[1]; }
    else if (k == 3) x[3] = x[1];
    for (int i = 0; i < 4; i++) {
        o[i] = x[i];
        if (o[i].u == U_EM || o[i].u == U_REM) { o[i].v = px_de(&o[i], tam); o[i].u = U_PX; }
    }
}

static int largura_borda(const char *p, int t, int tam)
{
    if (t == 4 && igual_n(p, "thin", 4)) return 1;
    if (t == 6 && igual_n(p, "medium", 6)) return 3;
    if (t == 5 && igual_n(p, "thick", 5)) return 5;
    medida_t md;
    if (ler_medida_m(p, t, &md) && (md.u == U_PX || md.u == U_EM || md.u == U_REM)) return px_de(&md, tam);
    return -1;
}

static void borda_lado(estilo_t *e, int lado, const char *v)
{
    const char *p[4]; int t[4];
    int k = palavras(v, (int)tam_texto(v), p, t, 4);
    int w = -1; uint32_t c = 0xFF000000u | e->cor; bool nenhuma = false, tem_estilo = false;
    for (int i = 0; i < k; i++) {
        int x = largura_borda(p[i], t[i], e->tam);
        uint32_t cc;
        if (x >= 0) w = x;
        else if ((t[i] == 4 && igual_n(p[i], "none", 4)) || (t[i] == 6 && igual_n(p[i], "hidden", 6))) nenhuma = true;
        else if (ler_cor(p[i], t[i], &cc, e->cor)) c = cc ? cc : 1;   /* 1 = transparente */
        else tem_estilo = true;
    }
    if (k == 1 && t[0] == 1 && p[0][0] == '0') w = 0;
    for (int l = 0; l < 4; l++) {
        if (lado >= 0 && l != lado) continue;
        if (nenhuma) { e->borda[l] = 0; continue; }
        e->borda[l] = (int16_t)(w >= 0 ? w : (tem_estilo ? 3 : e->borda[l]));
        e->cor_borda[l] = c;
    }
}

static uint8_t ler_display(const char *v)
{
    static const struct { const char *n; uint8_t d; } t[] = {
        {"none",D_NONE},{"block",D_BLOCK},{"inline",D_INLINE},{"inline-block",D_IBLOCK},{"flex",D_FLEX},{"inline-flex",D_IFLEX},
        {"grid",D_GRID},{"inline-grid",D_GRID},{"table",D_TABLE},{"inline-table",D_TABLE},{"table-row",D_ROW},{"table-cell",D_CELL},
        {"list-item",D_LIST},{"contents",D_CONTENTS},{"table-row-group",D_ROWGROUP},{"table-header-group",D_ROWGROUP},
        {"table-footer-group",D_ROWGROUP},{"table-caption",D_CAPTION},{"flow-root",D_BLOCK},{"-webkit-box",D_BLOCK},{"-webkit-inline-box",D_IBLOCK},
        {"block flex",D_FLEX},{"inline flex",D_IFLEX},{"block grid",D_GRID},{"inline-list-item",D_INLINE},{"ruby",D_INLINE},{"table-column",D_NONE},{"table-column-group",D_NONE},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (igual(v, t[i].n)) return t[i].d;
    return 255;
}

static int ler_peso(const char *v)
{
    if (igual(v, "bold") || igual(v, "bolder")) return 1;
    if (igual(v, "normal") || igual(v, "lighter")) return 0;
    int u; int x = num100(v, (int)tam_texto(v), &u);
    if (u) return x >= 60000 ? 1 : 0;
    return -1;
}

static void tamanho_fonte(estilo_t *e, const char *v, const estilo_t *pai)
{
    int pt = pai ? pai->tam : 16;
    static const struct { const char *n; int t; } kw[] = { {"xx-small",9},{"x-small",10},{"small",13},{"medium",16},{"large",18},{"x-large",24},{"xx-large",32},{"xxx-large",48} };
    for (unsigned i = 0; i < sizeof kw / sizeof kw[0]; i++) if (igual(v, kw[i].n)) { e->tam = (int16_t)kw[i].t; return; }
    if (igual(v, "smaller")) { e->tam = (int16_t)(pt * 5 / 6); return; }
    if (igual(v, "larger")) { e->tam = (int16_t)(pt * 6 / 5); return; }
    medida_t md;
    if (!ler_medida_m(v, (int)tam_texto(v), &md)) return;
    int t = md.u == U_PX ? md.v : md.u == U_EM ? md.v * pt / 100 : md.u == U_REM ? md.v * 16 / 100 : md.u == U_PCT ? md.v * pt / 10000 :
            md.u == U_VW ? md.v * 1024 / 10000 : (md.u == U_FR ? 16 : pt);
    if (t < 6) t = 6;
    if (t > 96) t = 96;
    e->tam = (int16_t)t;
}

static void trilhas(estilo_t *e, const char *v)
{
    e->n_col = 0;
    e->col_rep_auto = 0;
    const char *p[24]; int t[24];
    int k = palavras(v, (int)tam_texto(v), p, t, 24);
    for (int i = 0; i < k && e->n_col < MAX_TRILHAS; i++) {
        if (t[i] > 7 && igual_n(p[i], "repeat(", 7)) {
            const char *q = p[i] + 7; int nq = t[i] - 8;
            const char *r[8]; int tr[8];
            int nr = palavras(q, nq, r, tr, 8);
            if (nr < 2) continue;
            int vezes = 0;
            if (tr[0] >= 8 && (igual_n(r[0], "auto-fill", 9) || igual_n(r[0], "auto-fit", 8))) { vezes = -1; }
            else { int u; vezes = num100(r[0], tr[0], &u) / 100; }
            medida_t md;
            const char *alvo = r[1]; int ta = tr[1];
            if (ta > 7 && igual_n(alvo, "minmax(", 7)) {
                /* auto-fill: o mínimo dá a contagem; repetição fixa: o máximo (fr) dá o tamanho */
                const char *z[2]; int tz[2]; int nz = palavras(alvo + 7, ta - 8, z, tz, 2);
                if (nz == 2) {
                    medida_t mx;
                    bool fr_max = ler_medida_m(z[1], tz[1], &mx) && mx.u == U_FR;
                    if (vezes < 0 || !fr_max) { alvo = z[0]; ta = tz[0]; } else { alvo = z[1]; ta = tz[1]; }
                }
            }
            if (!ler_medida_m(alvo, ta, &md)) md.u = U_FR, md.v = 100;
            if (vezes < 0) { e->col_rep_auto = 1; e->col[e->n_col++] = md; continue; }
            for (int z = 0; z < vezes && e->n_col < MAX_TRILHAS; z++) e->col[e->n_col++] = md;
            continue;
        }
        medida_t md;
        const char *alvo = p[i]; int ta = t[i];
        if (ta > 7 && igual_n(alvo, "minmax(", 7)) { const char *z[2]; int tz[2]; int nz = palavras(alvo + 7, ta - 8, z, tz, 2); if (nz == 2) { alvo = z[1]; ta = tz[1]; } }
        if (alvo[0] == '[') continue;   /* nomes de linha */
        if (ler_medida_m(alvo, ta, &md)) e->col[e->n_col++] = md;
    }
}

static void coluna_grid(estilo_t *e, const char *v0)
{
    /* "span 2", "1 / 3", "1 / -1", "2", "1/span 18" */
    char v[96]; int kk = 0;
    for (const char *q = v0; *q && kk < 90; q++) { if (*q == '/') { v[kk++] = ' '; v[kk++] = '/'; v[kk++] = ' '; } else v[kk++] = *q; }
    v[kk] = 0;
    const char *p[6]; int t[6];
    int k = palavras(v, (int)tam_texto(v), p, t, 6);
    int ini = 0, fim = 0, span = 1, u;
    int i = 0;
    if (i < k && t[i] == 4 && igual_n(p[i], "span", 4) && i + 1 < k) { span = num100(p[i + 1], t[i + 1], &u) / 100; i += 2; }
    else if (i < k) { ini = num100(p[i], t[i], &u) / 100; if (!u) ini = 0; i++; }
    if (i < k && p[i][0] == '/') {
        i++;
        if (i < k && t[i] == 4 && igual_n(p[i], "span", 4) && i + 1 < k) span = num100(p[i + 1], t[i + 1], &u) / 100;
        else if (i < k) { fim = num100(p[i], t[i], &u) / 100; if (!u) fim = 0; }
    }
    e->g_ini = (int8_t)ini;
    if (fim == -1) e->g_span = -1;
    else if (fim > ini && ini > 0) e->g_span = (int8_t)(fim - ini);
    else e->g_span = (int8_t)(span < 1 ? 1 : span > MAX_TRILHAS ? MAX_TRILHAS : span);
}

static void aplicar(motor_t *m, no_m *n, int prop, const char *v0, const estilo_t *pai)
{
    estilo_t *e = &n->e;
    const char *v = subst_vars(m, n, v0);
    int nv = (int)tam_texto(v);
    if (nv == 7 && igual_n(v, "inherit", 7)) {
        if (!pai) return;
        switch (prop) {
        case PR_COLOR: e->cor = pai->cor; break;
        case PR_DISPLAY: e->display = pai->display; break;
        case PR_BGCOLOR: case PR_BG: e->fundo = pai->fundo; break;
        case PR_FSIZE: e->tam = pai->tam; break;
        case PR_WIDTH: e->larg = pai->larg; break;
        case PR_HEIGHT: e->alt = pai->alt; break;
        default: break;
        }
        return;
    }
    if ((nv == 7 && igual_n(v, "initial", 7)) || (nv == 5 && igual_n(v, "unset", 5)) || (nv == 6 && igual_n(v, "revert", 6))) {
        if (prop == PR_COLOR && pai) e->cor = pai->cor;
        return;
    }
    medida_t md;
    uint32_t c;
    const char *p[6]; int t[6];
    switch (prop) {
    case PR_DISPLAY: { uint8_t d = ler_display(v); if (d != 255) e->display = d; break; }
    case PR_POSITION:
        e->position = igual(v, "relative") ? P_RELATIVE : igual(v, "absolute") ? P_ABSOLUTE : igual(v, "fixed") ? P_FIXED : igual(v, "sticky") ? P_STICKY : P_STATIC;
        break;
    case PR_FLOAT: e->flutua = igual(v, "left") ? 1 : igual(v, "right") ? 2 : 0; break;
    case PR_CLEAR: e->limpa = igual(v, "left") ? 1 : igual(v, "right") ? 2 : igual(v, "both") ? 3 : 0; break;
    case PR_WIDTH: if (ler_medida_m(v, nv, &md)) e->larg = md; break;
    case PR_HEIGHT: if (ler_medida_m(v, nv, &md)) e->alt = md; break;
    case PR_MINW: if (ler_medida_m(v, nv, &md)) e->min_l = md; break;
    case PR_MAXW: if (ler_medida_m(v, nv, &md)) e->max_l = md; break;
    case PR_MINH: if (ler_medida_m(v, nv, &md)) e->min_a = md; break;
    case PR_MAXH: if (ler_medida_m(v, nv, &md)) e->max_a = md; break;
    case PR_MARGIN: quatro(v, e->marg, e->tam); break;
    case PR_MT: case PR_MR: case PR_MB: case PR_ML:
        if (ler_medida_m(v, nv, &md)) { if (md.u == U_EM || md.u == U_REM) { md.v = px_de(&md, e->tam); md.u = U_PX; } e->marg[prop - PR_MT] = md; }
        break;
    case PR_PADDING: quatro(v, e->pad, e->tam); break;
    case PR_PT: case PR_PR: case PR_PB: case PR_PL:
        if (ler_medida_m(v, nv, &md)) { if (md.u == U_EM || md.u == U_REM) { md.v = px_de(&md, e->tam); md.u = U_PX; } e->pad[prop - PR_PT] = md; }
        break;
    case PR_INSET: quatro(v, e->pos, e->tam); break;
    case PR_TOP: case PR_RIGHT: case PR_BOTTOM: case PR_LEFT:
        if (ler_medida_m(v, nv, &md)) { if (md.u == U_EM || md.u == U_REM) { md.v = px_de(&md, e->tam); md.u = U_PX; } e->pos[prop - PR_TOP] = md; }
        break;
    case PR_BORDER: borda_lado(e, -1, v); break;
    case PR_BT: case PR_BR: case PR_BB: case PR_BL: borda_lado(e, prop - PR_BT, v); break;
    case PR_BWIDTH: {
        int k = palavras(v, nv, p, t, 4), w[4];
        for (int i = 0; i < k; i++) { w[i] = largura_borda(p[i], t[i], e->tam); if (w[i] < 0) return; }
        if (!k) break;
        if (k == 1) w[1] = w[2] = w[3] = w[0]; else if (k == 2) { w[2] = w[0]; w[3] = w[1]; } else if (k == 3) w[3] = w[1];
        for (int i = 0; i < 4; i++) e->borda[i] = (int16_t)w[i];
        break;
    }
    case PR_BTW: case PR_BRW: case PR_BBW: case PR_BLW: { int w = largura_borda(v, nv, e->tam); if (w >= 0) e->borda[prop - PR_BTW] = (int16_t)w; break; }
    case PR_BCOLOR: {
        int k = palavras(v, nv, p, t, 4); uint32_t cc[4];
        for (int i = 0; i < k; i++) { if (!ler_cor(p[i], t[i], &cc[i], e->cor)) return; if (!cc[i]) cc[i] = 1; }
        if (!k) break;
        if (k == 1) cc[1] = cc[2] = cc[3] = cc[0]; else if (k == 2) { cc[2] = cc[0]; cc[3] = cc[1]; } else if (k == 3) cc[3] = cc[1];
        for (int i = 0; i < 4; i++) e->cor_borda[i] = cc[i];
        break;
    }
    case PR_BTC: case PR_BRC: case PR_BBC: case PR_BLC: if (ler_cor(v, nv, &c, e->cor)) e->cor_borda[prop - PR_BTC] = c ? c : 1; break;
    case PR_BSTYLE: if (igual(v, "none") || igual(v, "hidden")) for (int i = 0; i < 4; i++) e->borda[i] = 0;
        else for (int i = 0; i < 4; i++) if (!e->borda[i] && !e->cor_borda[i]) { e->borda[i] = 3; e->cor_borda[i] = 0xFF000000u | e->cor; }
        break;
    case PR_RADIUS: if (ler_medida_m(v, palavras(v, nv, p, t, 1) ? t[0] : nv, &md)) e->raio = (int16_t)(md.u == U_PCT ? (md.v >= 5000 ? 999 : md.v / 10) : px_de(&md, e->tam)); break;
    case PR_COLOR: if (ler_cor(v, nv, &c, e->cor)) e->cor = c & 0xFFFFFF; break;
    case PR_BGCOLOR: if (ler_cor(v, nv, &c, e->cor)) e->fundo = (c >> 24) >= 0x40 ? c : 0; break;
    case PR_BGIMAGE: case PR_BGSIZE:
        if (prop == PR_BGSIZE) { e->bg_tam = strstr_m(v, "contain") ? 2 : (strstr_m(v, "cover") || strstr_m(v, "100%")) ? 1 : 0; break; }
        /* fallthrough */
    case PR_BG: {
        {
            const char *u = v;
            for (; *u; u++) if (igual_n(u, "url(", 4)) break;
            if (*u) {
                u += 4;
                while (*u == ' ' || *u == '"' || *u == '\'') u++;
                const char *f = u;
                while (*f && *f != ')' && *f != '"' && *f != '\'') f++;
                if (f > u && !igual_n(u, "data:", 5)) e->bg_url = m_dup(m, u, (int)(f - u));
            } else if (prop == PR_BGIMAGE && strstr_m(v, "none")) e->bg_url = NULL;
            if (prop == PR_BG) { if (strstr_m(v, "cover")) e->bg_tam = 1; else if (strstr_m(v, "contain")) e->bg_tam = 2; }
            if (prop == PR_BGIMAGE) break;
        }
        if (nv == 4 && igual_n(v, "none", 4)) { e->fundo = 0; break; }
        int k = palavras(v, nv, p, t, 6);
        for (int i = 0; i < k; i++) if (ler_cor(p[i], t[i], &c, e->cor)) { e->fundo = (c >> 24) >= 0x40 ? c : 0; break; }
        /* gradiente: usa a primeira cor */
        for (int i = 0; i < k; i++)
            if (t[i] > 16 && (igual_n(p[i], "linear-gradient(", 16) || igual_n(p[i], "radial-gradient(", 16))) {
                const char *q[6]; int tq[6];
                int nq = palavras(p[i] + 16, t[i] - 17, q, tq, 6);
                for (int z = 0; z < nq; z++) if (ler_cor(q[z], tq[z], &c, e->cor)) { e->fundo = (c >> 24) >= 0x40 ? c : 0; break; }
            }
        break;
    }
    case PR_FSIZE: tamanho_fonte(e, v, pai); break;
    case PR_FWEIGHT: { int w = ler_peso(v); if (w >= 0) e->peso = (uint8_t)w; break; }
    case PR_FSTYLE: e->italico = igual(v, "italic") || igual(v, "oblique"); break;
    case PR_FONT: {
        int k = palavras(v, nv, p, t, 6);
        for (int i = 0; i < k; i++) {
            char w[24]; int tw = t[i] < 23 ? t[i] : 23;
            memcpy(w, p[i], (size_t)tw); w[tw] = 0;
            char *barra = acha_char(w, '/');
            if (barra) *barra = 0;
            if (ler_peso(w) >= 0 && !eh_dig(w[0])) e->peso = (uint8_t)ler_peso(w);
            else if (eh_dig(w[0]) && ler_peso(w) >= 0 && !barra && tw == 3 && w[1] == '0' && w[2] == '0') e->peso = (uint8_t)ler_peso(w);
            else if (igual(w, "italic")) e->italico = 1;
            else if (ler_medida_m(w, (int)tam_texto(w), &md) && md.u != U_PX) tamanho_fonte(e, w, pai);
            else if (ler_medida_m(w, (int)tam_texto(w), &md) && md.u == U_PX && md.v > 0) tamanho_fonte(e, w, pai);
        }
        break;
    }
    case PR_TALIGN:
        e->alinha = igual(v, "center") || igual(v, "-webkit-center") ? 1 : (igual(v, "right") || igual(v, "end") || igual(v, "-webkit-right")) ? 2 : igual(v, "justify") ? 0 : 0;
        break;
    case PR_TDECO: case PR_TDLINE:
        e->sublinha = strstr_m(v, "underline"); e->risca = strstr_m(v, "line-through");
        break;
    case PR_LHEIGHT:
        if (igual(v, "normal")) e->linha.u = U_AUTO;
        else if (ler_medida_m(v, nv, &md)) {
            int u; int x = num100(v, nv, &u);
            if (u == nv) { e->linha.u = U_NONE; e->linha.v = x; }               /* número: fator x100 */
            else if (md.u == U_PCT) { e->linha.u = U_NONE; e->linha.v = md.v / 100; }
            else { e->linha.u = U_PX; e->linha.v = px_de(&md, e->tam); }
        }
        break;
    case PR_WSPACE: e->branco = igual(v, "pre") || igual(v, "pre-wrap") || igual(v, "break-spaces") ? 1 : igual(v, "nowrap") ? 2 : igual(v, "pre-line") ? 0 : 0; break;
    case PR_LIST: case PR_LISTTYPE:
        if (strstr_m(v, "none")) e->lista = 0;
        else if (strstr_m(v, "decimal")) e->lista = 2;
        else if (strstr_m(v, "disc") || strstr_m(v, "circle") || strstr_m(v, "square")) e->lista = 1;
        else if (prop == PR_LISTTYPE) e->lista = 1;
        break;
    case PR_VISIBILITY: e->visivel = igual(v, "hidden") || igual(v, "collapse") ? 1 : 0; break;
    case PR_OVERFLOW: case PR_OVX: case PR_OVY: if (strstr_m(v, "hidden") || strstr_m(v, "clip") || strstr_m(v, "auto") || strstr_m(v, "scroll")) e->corta = 1; else if (igual(v, "visible")) e->corta = 0; break;
    case PR_BOXSIZING: e->caixa_borda = igual(v, "border-box"); break;
    case PR_FDIR: e->dir = strstr_m(v, "column") ? 1 : 0; if (strstr_m(v, "reverse")) e->dir |= 2; break;
    case PR_FWRAP: e->quebra = strstr_m(v, "wrap") && !igual(v, "nowrap"); break;
    case PR_FFLOW:
        e->dir = strstr_m(v, "column") ? 1 : 0;
        e->quebra = strstr_m(v, "wrap") && !strstr_m(v, "nowrap");
        break;
    case PR_JUSTIFY: case PR_PCONTENT:
        e->justifica = strstr_m(v, "space-between") ? 4 : strstr_m(v, "space-around") ? 5 : strstr_m(v, "space-evenly") ? 6 : strstr_m(v, "center") ? 1 :
                       (strstr_m(v, "end") || strstr_m(v, "right")) ? 2 : 0;
        if (prop == PR_PCONTENT) e->alinha_conteudo = e->justifica;
        break;
    case PR_ACONTENT: e->alinha_conteudo = strstr_m(v, "center") ? 1 : strstr_m(v, "end") ? 2 : strstr_m(v, "space-between") ? 4 : 0; break;
    case PR_AITEMS: case PR_PITEMS:
        e->alinha_itens = strstr_m(v, "center") ? 1 : strstr_m(v, "end") ? 2 : (strstr_m(v, "start") || strstr_m(v, "baseline")) ? 3 : 0;
        break;
    case PR_ASELF: e->alinha_si = strstr_m(v, "center") ? 2 : strstr_m(v, "end") ? 3 : strstr_m(v, "start") ? 4 : strstr_m(v, "stretch") ? 1 : 0; break;
    case PR_GROW: { int u; int x = num100(v, nv, &u); if (u) e->cresce = (int16_t)(x > 30000 ? 30000 : x); break; }
    case PR_SHRINK: { int u; int x = num100(v, nv, &u); if (u) e->encolhe = (int16_t)(x > 30000 ? 30000 : x); break; }
    case PR_BASIS: if (ler_medida_m(v, nv, &md)) e->base = md; if (igual(v, "content")) e->base.u = U_AUTO; break;
    case PR_FLEX: {
        if (igual(v, "none")) { e->cresce = 0; e->encolhe = 0; e->base.u = U_AUTO; break; }
        if (igual(v, "auto")) { e->cresce = 100; e->encolhe = 100; e->base.u = U_AUTO; break; }
        int k = palavras(v, nv, p, t, 3), u;
        int x = k ? num100(p[0], t[0], &u) : 0;
        if (k && u == t[0]) {
            e->cresce = (int16_t)x; e->encolhe = 100; e->base.u = U_PX; e->base.v = 0;
            if (k > 1) { int y = num100(p[1], t[1], &u); if (u == t[1]) e->encolhe = (int16_t)y; else if (ler_medida_m(p[1], t[1], &md)) e->base = md; }
            if (k > 2 && ler_medida_m(p[2], t[2], &md)) e->base = md;
        } else if (k && ler_medida_m(p[0], t[0], &md)) { e->base = md; e->cresce = 100; }
        break;
    }
    case PR_GAP: {
        int k = palavras(v, nv, p, t, 2);
        if (k && ler_medida_m(p[0], t[0], &md)) { if (md.u == U_EM || md.u == U_REM) { md.v = px_de(&md, e->tam); md.u = U_PX; } e->vao_l = md; e->vao_c = md; }
        if (k > 1 && ler_medida_m(p[1], t[1], &md)) { if (md.u == U_EM || md.u == U_REM) { md.v = px_de(&md, e->tam); md.u = U_PX; } e->vao_c = md; }
        break;
    }
    case PR_RGAP: if (ler_medida_m(v, nv, &md)) { if (md.u == U_EM || md.u == U_REM) { md.v = px_de(&md, e->tam); md.u = U_PX; } e->vao_l = md; } break;
    case PR_CGAP: if (ler_medida_m(v, nv, &md)) { if (md.u == U_EM || md.u == U_REM) { md.v = px_de(&md, e->tam); md.u = U_PX; } e->vao_c = md; } break;
    case PR_ORDER: { int u; int x = num100(v, nv, &u); if (u) e->ordem = (int16_t)(x / 100); break; }
    case PR_GTC: trilhas(e, v); break;
    case PR_GCOL: coluna_grid(e, v); break;
    case PR_GCS: { int u; int x = num100(v, nv, &u); if (u) e->g_ini = (int8_t)(x / 100); else if (nv > 5 && igual_n(v, "span", 4)) coluna_grid(e, v); break; }
    case PR_GCE: { int u; int x = num100(v, nv, &u) / 100; if (u && x == -1) e->g_span = -1; else if (u && e->g_ini && x > e->g_ini) e->g_span = (int8_t)(x - e->g_ini); else if (nv > 5 && igual_n(v, "span", 4)) coluna_grid(e, v); break; }
    case PR_GROW_L: {
        estilo_t tmp = *e;
        coluna_grid(&tmp, v);
        e->g_lin = tmp.g_ini; e->g_lspan = tmp.g_span < 1 ? 1 : tmp.g_span;
        break;
    }
    case PR_GTA: e->areas = igual(v, "none") ? NULL : v; break;
    case PR_GAREA: {
        int k = palavras(v, nv, p, t, 6);
        if (k == 1 && !eh_dig(p[0][0]) && p[0][0] != '-') { e->area = m_dup(m, p[0], t[0]); break; }
        /* "linha-ini / col-ini / linha-fim / col-fim" */
        int nums[4] = { 0, 0, 0, 0 }, q = 0, u;
        for (int i = 0; i < k && q < 4; i++) { if (p[i][0] == '/') continue; nums[q++] = num100(p[i], t[i], &u) / 100; }
        if (nums[0] > 0) { e->g_lin = (int8_t)nums[0]; e->g_lspan = (int8_t)(nums[2] > nums[0] ? nums[2] - nums[0] : 1); }
        if (nums[1] > 0) { e->g_ini = (int8_t)nums[1]; e->g_span = (int8_t)(nums[3] > nums[1] ? nums[3] - nums[1] : nums[3] == -1 ? -1 : 1); }
        break;
    }
    case PR_MINLINE: case PR_MBLOCK: case PR_PINLINE: case PR_PBLOCK: {
        medida_t q[4]; int k = palavras(v, nv, p, t, 2);
        if (!k || !ler_medida_m(p[0], t[0], &q[0])) break;
        q[1] = q[0];
        if (k > 1) ler_medida_m(p[1], t[1], &q[1]);
        for (int i = 0; i < 2; i++) if (q[i].u == U_EM || q[i].u == U_REM) { q[i].v = px_de(&q[i], e->tam); q[i].u = U_PX; }
        medida_t *alvo = prop == PR_MINLINE || prop == PR_MBLOCK ? e->marg : e->pad;
        if (prop == PR_MINLINE || prop == PR_PINLINE) { alvo[3] = q[0]; alvo[1] = q[1]; } else { alvo[0] = q[0]; alvo[2] = q[1]; }
        break;
    }
    case PR_CONTENT: e->conteudo = v; break;
    case PR_MASK: e->mascara = !strstr_m(v, "none"); break;
    case PR_OPACITY: { int u; int x = num100(v, nv, &u); if (u) e->opaco0 = x <= 0; break; }
    case PR_TTRANSFORM: e->maiusc = igual(v, "uppercase") ? 1 : igual(v, "lowercase") ? 2 : igual(v, "capitalize") ? 3 : 0; break;
    case PR_VALIGN: e->vert = igual(v, "middle") ? 1 : igual(v, "top") ? 2 : igual(v, "bottom") ? 3 : 0; break;
    case PR_ZINDEX: { int u; int x = num100(v, nv, &u); if (u) e->z = (int16_t)(x / 100); break; }
    default: break;
    }
}

/* ---------------------------------------------------------------- cascata */

static int ua_fim;   /* regras [0, ua_fim) são da folha padrão */

static void presentacionais(motor_t *m, no_m *n)
{
    estilo_t *e = &n->e;
    const char *v;
    medida_t md;
    if ((v = atributo_m(n, "bgcolor"))) { uint32_t c; if (ler_cor(v, (int)tam_texto(v), &c, 0)) e->fundo = c | 0xFF000000u; }
    if (igual(n->tag, "font") && (v = atributo_m(n, "color"))) { uint32_t c; if (ler_cor(v, (int)tam_texto(v), &c, 0)) e->cor = c & 0xFFFFFF; }
    if ((v = atributo_m(n, "width")) && ler_medida_m(v, (int)tam_texto(v), &md) && !igual(n->tag, "input")) e->larg = md;
    if ((v = atributo_m(n, "height")) && ler_medida_m(v, (int)tam_texto(v), &md) && !igual(n->tag, "input")) e->alt = md;
    if ((v = atributo_m(n, "align"))) {
        if (igual(n->tag, "table") || igual(n->tag, "img")) {
            if (igual(v, "center")) { e->marg[1].u = e->marg[3].u = U_AUTO; }
            else if (igual(v, "left")) e->flutua = 1; else if (igual(v, "right")) e->flutua = 2;
        } else e->alinha = igual(v, "center") || igual(v, "middle") ? 1 : igual(v, "right") ? 2 : 0;
    }
    if (igual(n->tag, "table") && (v = atributo_m(n, "border")) && v[0] && v[0] != '0') for (int i = 0; i < 4; i++) { e->borda[i] = 1; e->cor_borda[i] = 0xFF808080u; }
    if ((igual(n->tag, "td") || igual(n->tag, "th"))) {
        no_m *t = n->pai; while (t && !igual(t->tag, "table")) t = t->pai;
        if (t && (v = atributo_m(t, "border")) && v[0] && v[0] != '0') for (int i = 0; i < 4; i++) { e->borda[i] = 1; e->cor_borda[i] = 0xFF808080u; }
        if (t && (v = atributo_m(t, "cellpadding"))) { int u; int x = num100(v, (int)tam_texto(v), &u) / 100; if (u) for (int i = 0; i < 4; i++) { e->pad[i].u = U_PX; e->pad[i].v = x; } }
        if ((v = atributo_m(n, "valign"))) e->vert = igual(v, "top") ? 2 : igual(v, "bottom") ? 3 : 1;
        if ((v = atributo_m(n, "nowrap"))) e->branco = 2;
    }
    if (igual(n->tag, "font") && (v = atributo_m(n, "size"))) {
        int u; int x = num100(v, (int)tam_texto(v), &u) / 100;
        static const int t[] = { 10, 13, 16, 18, 24, 32, 48 };
        if (v[0] == '+' || v[0] == '-') x += 3;
        if (u) e->tam = (int16_t)t[x < 1 ? 0 : x > 7 ? 6 : x - 1];
    }
    (void)m;
}

static void estilizar(motor_t *m, no_m *n)
{
    no_m *pn = n->pai;
    const estilo_t *pe = pn ? &pn->e : NULL;
    estilo_inicial(&n->e, pe);
    if (n->texto) { n->e.display = n->texto == 2 ? D_NONE : D_INLINE; return; }
    if (!pn || igual(n->tag, "#anon")) { n->e.display = D_BLOCK; return; }
    static achado_t ach[512];
    static achado_t achps[2][64]; int nps[2] = { 0, 0 };
    int na = 0;
    /* regras candidatas */
    int *listas[16]; int tams[16]; int nl = 0;
    char ch[80];
    balde_t *b;
    if (n->id) { formatar_texto(ch, sizeof ch, "#%s", n->id); if ((b = balde(m, ch, false))) { listas[nl] = b->regras; tams[nl++] = b->n; } }
    if (n->classe) {
        for (const char *p = n->classe; *p && nl < 13;) {
            while (*p && eh_esp(*p)) p++;
            const char *i = p;
            while (*p && !eh_esp(*p)) p++;
            if (p > i && p - i < 78) {
                ch[0] = '.'; memcpy(ch + 1, i, (size_t)(p - i)); ch[p - i + 1] = 0;
                if ((b = balde(m, ch, false))) { listas[nl] = b->regras; tams[nl++] = b->n; }
            }
        }
    }
    if ((b = balde(m, n->tag, false))) { listas[nl] = b->regras; tams[nl++] = b->n; }
    listas[nl] = m->univ; tams[nl++] = m->n_univ;
    for (int l = 0; l < nl; l++)
        for (int k = 0; k < tams[l]; k++) {
            regra_m *r = &m->regras[listas[l][k]];
            uint8_t ps = r->sel.c[0].pseudo;
            if (ps) r->sel.c[0].pseudo = 0;
            bool bate = bate_de(&r->sel, 0, n);
            r->sel.c[0].pseudo = ps;
            if (!bate) continue;
            if (ps) {
                for (int d = 0; d < r->nd && nps[ps - 1] < 64; d++) {
                    achado_t *a = &achps[ps - 1][nps[ps - 1]++];
                    a->d = &r->d[d]; a->espec = (r->ordem < ua_fim ? 0 : 2000000) + r->sel.espec; a->ordem = r->ordem;
                }
                continue;
            }
            int origem = r->ordem < ua_fim ? 0 : 1;
            for (int d = 0; d < r->nd && na < 512; d++) {
                ach[na].d = &r->d[d];
                ach[na].espec = origem * 2000000 + r->sel.espec;
                ach[na].ordem = r->ordem;
                na++;
            }
        }
    const char *st = atributo_m(n, "style");
    static decl_t inl[64];
    if (st) {
        int k = ler_decls(m, st, (int)tam_texto(st), inl, 64);
        for (int d = 0; d < k && na < 512; d++) { ach[na].d = &inl[d]; ach[na].espec = 3000000; ach[na].ordem = 1 << 30; na++; }
    }
    /* ordena (estável) */
    for (int i = 1; i < na; i++) {
        achado_t x = ach[i]; int j = i - 1;
        while (j >= 0 && comparar_achado(&ach[j], &x) > 0) { ach[j + 1] = ach[j]; j--; }
        ach[j + 1] = x;
    }
    /* UA primeiro, depois atributos de apresentação, depois autor */
    for (int i = 0; i < na; i++) if (ach[i].d->prop == PR_CUSTOM) {
        var_m_t *v = m_novo(m, sizeof(var_m_t));
        if (!v) break;
        v->nome = (char *)ach[i].d->valor;
        v->valor = (char *)ach[i].d->valor + tam_texto(ach[i].d->valor) + 1;
        v->prox = n->vars; n->vars = v;
    }
    int i = 0;
    for (; i < na && ach[i].espec < 2000000 && !ach[i].d->importante; i++) {
        if (ach[i].d->prop == PR_FSIZE || ach[i].d->prop == PR_FONT) aplicar(m, n, ach[i].d->prop, ach[i].d->valor, pe);
    }
    presentacionais(m, n);
    for (int q = i; q < na; q++) if (ach[q].d->prop == PR_FSIZE || ach[q].d->prop == PR_FONT) aplicar(m, n, ach[q].d->prop, ach[q].d->valor, pe);
    for (int q = 0; q < na; q++) {
        if (q == i) presentacionais(m, n);
        int p = ach[q].d->prop;
        if (m->sem_esconder && ach[q].espec >= 2000000 && ((p == PR_DISPLAY && strstr_m(ach[q].d->valor, "none")) ||
            (p == PR_VISIBILITY && strstr_m(ach[q].d->valor, "hidden")) || (p == PR_OPACITY && ach[q].d->valor[0] == '0'))) continue;
        if (p != PR_FSIZE && p != PR_CUSTOM && p != PR_FONT) aplicar(m, n, p, ach[q].d->valor, pe);
    }
    if (i == na) presentacionais(m, n);
    estilo_t *e = &n->e;
    if (e->flutua || e->position == P_ABSOLUTE || e->position == P_FIXED) {
        if (e->display == D_INLINE || e->display == D_IBLOCK || e->display == D_ROW || e->display == D_CELL) e->display = D_BLOCK;
        else if (e->display == D_IFLEX) e->display = D_FLEX;
    }
    if (igual(n->tag, "html") || igual(n->tag, "body")) { if (e->display != D_NONE && e->display != D_FLEX && e->display != D_GRID) e->display = D_BLOCK; }
    if (e->display == D_LIST && !e->lista && !atributo_m(n, "style") && pe && pe->lista) e->lista = pe->lista;
    if (pn && igual(pn->tag, "details") && !atributo_m(pn, "open") && !igual(n->tag, "summary")) e->display = D_NONE;
    /* ::before e ::after viram filhos gerados */
    for (int k = 0; k < 2; k++) {
        if (!nps[k] && !n->ps[k]) continue;
        no_m *g = n->ps[k];
        if (!g) {
            g = m_novo(m, sizeof(no_m));
            if (!g) continue;
            copia_texto(g->tag, k ? "::after" : "::before", sizeof g->tag);
            g->pseudo = 1; g->img = -1; g->alvo = -1; g->max_c = g->min_c = -1; g->bg_img = -1;
            g->pai = n;
            if (k == 0) { g->prox = n->filho; if (n->filho) n->filho->ant = g; else n->ult = g; n->filho = g; }
            else { g->ant = n->ult; if (n->ult) n->ult->prox = g; else n->filho = g; n->ult = g; }
            n->ps[k] = g;
        }
        estilo_inicial(&g->e, e);
        for (int i = 1; i < nps[k]; i++) { achado_t x = achps[k][i]; int j = i - 1; while (j >= 0 && comparar_achado(&achps[k][j], &x) > 0) { achps[k][j + 1] = achps[k][j]; j--; } achps[k][j + 1] = x; }
        for (int i = 0; i < nps[k]; i++) if (achps[k][i].d->prop == PR_FSIZE) aplicar(m, g, PR_FSIZE, achps[k][i].d->valor, e);
        for (int i = 0; i < nps[k]; i++) if (achps[k][i].d->prop != PR_FSIZE && achps[k][i].d->prop != PR_CUSTOM) aplicar(m, g, achps[k][i].d->prop, achps[k][i].d->valor, e);
        const char *ct = g->e.conteudo;
        if (!nps[k] || !ct || igual(ct, "none") || igual(ct, "normal")) { g->e.display = D_NONE; continue; }
        if (g->e.flutua || g->e.position == P_ABSOLUTE || g->e.position == P_FIXED) if (g->e.display == D_INLINE) g->e.display = D_BLOCK;
        /* texto: 'abc', "abc", attr(x); o resto (counter, url, ícones de fonte) é ignorado */
        char buf[128]; int bk = 0;
        for (const char *q = ct; *q && bk < 120;) {
            if (*q == '"' || *q == '\'') {
                char aspa = *q++;
                while (*q && *q != aspa && bk < 120) {
                    if (*q == '\\' && q[1]) {   /* \201C, \a etc.: só ASCII/Latin-1 */
                        q++;
                        unsigned cp = 0; int dg = 0;
                        while (dg < 6 && ((*q >= '0' && *q <= '9') || ((*q | 32) >= 'a' && (*q | 32) <= 'f'))) { cp = cp * 16 + (unsigned)(eh_dig(*q) ? *q - '0' : (*q | 32) - 'a' + 10); q++; dg++; }
                        if (*q == ' ' && dg) q++;
                        char l1 = dg ? para_l1(cp) : *q++;
                        if (l1 && (uint8_t)l1 >= 32) buf[bk++] = l1;
                        continue;
                    }
                    buf[bk++] = *q++;
                }
                if (*q) q++;
            } else if (igual_n(q, "attr(", 5)) {
                q += 5; const char *a0 = q; while (*q && *q != ')') q++;
                char nm[32]; int tn = (int)(q - a0) < 31 ? (int)(q - a0) : 31; memcpy(nm, a0, (size_t)tn); nm[tn] = 0;
                const char *av = atributo_m(n, nm);
                while (av && *av && bk < 120) buf[bk++] = *av++;
                if (*q) q++;
            } else q++;
        }
        buf[bk] = 0;
        if (!g->filho && bk) {
            no_m *t = m_novo(m, sizeof(no_m));
            if (t) { t->texto = 1; t->pai = g; t->img = -1; t->alvo = -1; t->bg_img = -1; g->filho = g->ult = t; }
        }
        if (g->filho) { g->filho->t = m_dup(m, buf, bk); g->filho->nt = bk; g->filho->e.display = bk ? D_INLINE : D_NONE; }
    }
}

static int imagem_idx(motor_t *m, const char *url)
{
    for (int i = 0; i < m->n_imgs; i++) if (igual(m->imgs[i].url, url)) return i;
    if (m->n_imgs >= 96) return -1;
    m->imgs[m->n_imgs].url = m_dup(m, url, (int)tam_texto(url));
    return m->n_imgs++;
}

static void estilizar_tudo(motor_t *m, no_m *n)
{
    for (; n; n = n->prox) {
        if (n->pseudo) { n->bg_img = -1; if (n->e.bg_url) { char u[600]; juntar(m->url, n->e.bg_url, u, sizeof u); n->bg_img = imagem_idx(m, u); } continue; }
        estilizar(m, n);
        n->bg_img = -1;
        if (!n->texto && n->e.bg_url) { char u[600]; juntar(m->url, n->e.bg_url, u, sizeof u); n->bg_img = imagem_idx(m, u); }
        if (n->e.display == D_NONE) continue;
        if (n->filho) estilizar_tudo(m, n->filho);
    }
}

/* Textos soltos dentro de flex/grid/tabela viram um bloco anônimo; células
 * soltas ganham uma linha. */
static void normalizar(motor_t *m, no_m *n)
{
    for (no_m *c = n->filho; c; c = c->prox) if (!c->texto && c->e.display != D_NONE) normalizar(m, c);
    uint8_t d = n->e.display;
    if (d != D_FLEX && d != D_IFLEX && d != D_GRID) return;
    for (no_m *c = n->filho; c;) {
        if (!(c->texto || c->e.display == D_INLINE)) { c = c->prox; continue; }
        no_m *ini = c, *fim = c;
        bool so_esp = true;
        while (fim && (fim->texto || fim->e.display == D_INLINE)) {
            if (!fim->texto) so_esp = false;
            else for (int i = 0; i < fim->nt; i++) if (!eh_esp(fim->t[i])) { so_esp = false; break; }
            fim = fim->prox;
        }
        if (so_esp) {   /* tira o espaço */
            for (no_m *q = ini; q != fim; q = q->prox) q->e.display = D_NONE, q->texto = q->texto ? 2 : 0;
            c = fim;
            continue;
        }
        if (!ini->texto && ini->prox == fim && ini->e.display == D_INLINE) { ini->e.display = D_BLOCK; c = fim; continue; }
        no_m *a = m_novo(m, sizeof(no_m));
        if (!a) return;
        copia_texto(a->tag, "#anon", sizeof a->tag);
        a->img = -1; a->alvo = -1; a->max_c = a->min_c = -1;
        estilo_inicial(&a->e, &n->e);
        a->e.display = D_BLOCK;
        a->pai = n;
        a->ant = ini->ant; a->prox = fim;
        if (ini->ant) ini->ant->prox = a; else n->filho = a;
        if (fim) fim->ant = a; else n->ult = a;
        a->filho = ini; ini->ant = NULL;
        no_m *u = ini;
        while (u->prox != fim) { u->pai = a; u = u->prox; }
        u->pai = a; u->prox = NULL;
        a->ult = u;
        c = fim;
    }
}

/* ================================================================ layout */

#define INF 1000000
static motor_t *mo;    /* motor em uso (layout não é reentrante) */

/* a * b / c sem divisão de 64 bits (não há libgcc) */
static int muldiv(int a, int b, int c)
{
    if (!c) return 0;
    int neg = (a < 0) ^ (b < 0) ^ (c < 0);
    uint32_t ua = (uint32_t)(a < 0 ? -a : a), ub = (uint32_t)(b < 0 ? -b : b), uc = (uint32_t)(c < 0 ? -c : c);
    uint64_t p = (uint64_t)ua * ub, q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((p >> i) & 1);
        if (r >= uc) { r -= uc; q |= (uint64_t)1 << i; }
    }
    if (q > 0x7FFFFFFF) q = 0x7FFFFFFF;
    return neg ? -(int)q : (int)q;
}

static int res_med(const medida_t *v, int base, const estilo_t *e)
{
    switch (v->u) {
    case U_PX: return v->v;
    case U_PCT: return base >= 0 && base < INF ? muldiv(v->v, base, 10000) : -1;
    case U_EM: return v->v * e->tam / 100;
    case U_REM: return v->v * 16 / 100;
    case U_VW: return v->v * mo->vw / 10000;
    case U_VH: return v->v * mo->vh / 10000;
    default: return -1;
    }
}

static int altura_linha(const estilo_t *e)
{
    if (e->linha.u == U_AUTO) return e->tam * 5 / 4 + 1;
    if (e->linha.u == U_NONE) { int a = e->linha.v * e->tam / 100; return a < e->tam ? e->tam : a; }
    return e->linha.v > 0 ? e->linha.v : e->tam;
}
static inline int larg_letra(const estilo_t *e) { return (e->tam + 1) / 2; }

static bool eh_bloco(uint8_t d) { return d == D_BLOCK || d == D_FLEX || d == D_GRID || d == D_TABLE || d == D_LIST || d == D_ROW || d == D_CELL || d == D_ROWGROUP || d == D_CAPTION; }
static bool eh_atomico(no_m *n)
{
    uint8_t d = n->e.display;
    if (d == D_IBLOCK || d == D_IFLEX) return true;
    if (d == D_INLINE && (igual(n->tag, "img") || igual(n->tag, "input") || igual(n->tag, "button") || igual(n->tag, "select") ||
                          igual(n->tag, "textarea") || igual(n->tag, "video") || igual(n->tag, "iframe") || igual(n->tag, "canvas") || igual(n->tag, "svg"))) return true;
    if (d == D_INLINE && (n->e.fundo || n->e.borda[0] || n->e.borda[3] || n->e.pad[3].v > 0) && !igual(n->tag, "a") ? false : false) return true;
    return false;
}
static bool fora_do_fluxo(no_m *n) { return n->e.position == P_ABSOLUTE || n->e.position == P_FIXED; }

/* fragmentos de linha: texto ou marcador, relativos ao bloco dono */
typedef struct frag {
    no_m *dono, *estilo_de;
    int x, y, l, a;
    const char *t; int nt;
    int alvo;
    struct frag *prox;
} frag_t;

typedef struct { int y0, y1, x0, x1, lado; } flut_t;
typedef struct {
    flut_t f[64]; int n;
} contexto_t;

static void por_frag(no_m *dono, frag_t *f);

static int layout_no(no_m *n, int cw, int forca_l, int forca_a);
static void layout_conteudo(no_m *n, int cw, int ch);
static int medir_max(no_m *n);
static int medir_min(no_m *n);

static void caixa_modelo(no_m *n, int cw)
{
    estilo_t *e = &n->e;
    for (int i = 0; i < 4; i++) {
        n->bl[i] = e->borda[i];
        int p = res_med(&e->pad[i], cw, e);
        n->pl[i] = p < 0 ? 0 : p;
    }
    int ml = res_med(&e->marg[3], cw, e), mr = res_med(&e->marg[1], cw, e);
    int mt = res_med(&e->marg[0], cw, e), mb = res_med(&e->marg[2], cw, e);
    n->ml = ml < -INF ? 0 : (e->marg[3].u == U_AUTO ? 0 : ml);
    n->mr = e->marg[1].u == U_AUTO ? 0 : mr;
    n->mt = e->marg[0].u == U_AUTO ? 0 : mt;
    n->mb = e->marg[2].u == U_AUTO ? 0 : mb;
}

static int extra_h(no_m *n) { return n->bl[1] + n->bl[3] + n->pl[1] + n->pl[3]; }
static int extra_v(no_m *n) { return n->bl[0] + n->bl[2] + n->pl[0] + n->pl[2]; }

/* imagem natural de um <img> */
static void tamanho_img(no_m *n, int *l, int *a)
{
    *l = *a = 0;
    if (n->img >= 0 && mo->imgs[n->img].px) { *l = mo->imgs[n->img].l; *a = mo->imgs[n->img].a; return; }
    const char *v;
    int u;
    if ((v = atributo_m(n, "width"))) *l = num100(v, (int)tam_texto(v), &u) / 100;
    if ((v = atributo_m(n, "height"))) *a = num100(v, (int)tam_texto(v), &u) / 100;
}

/* tamanho natural de controles e mídia */
static bool tamanho_natural(no_m *n, int *l, int *a)
{
    estilo_t *e = &n->e;
    if (igual(n->tag, "img")) {
        tamanho_img(n, l, a);
        if (!*l && !*a) { const char *alt = atributo_m(n, "alt"); if (alt && alt[0] && n->img < 0) { *l = (int)tam_texto(alt) * larg_letra(e); *a = altura_linha(e); } }
        return true;
    }
    if (igual(n->tag, "svg")) {
        int u; const char *v;
        *l = *a = e->tam;
        if ((v = atributo_m(n, "width"))) { int x = num100(v, (int)tam_texto(v), &u) / 100; if (u && !strstr_m(v, "%")) *l = x; }
        if ((v = atributo_m(n, "height"))) { int x = num100(v, (int)tam_texto(v), &u) / 100; if (u && !strstr_m(v, "%")) *a = x; }
        return true;
    }
    if (igual(n->tag, "video") || igual(n->tag, "iframe") || igual(n->tag, "canvas") || igual(n->tag, "embed") || igual(n->tag, "object")) {
        int u; const char *v;
        *l = 300; *a = 150;
        if ((v = atributo_m(n, "width"))) { int x = num100(v, (int)tam_texto(v), &u) / 100; if (u) *l = x; }
        if ((v = atributo_m(n, "height"))) { int x = num100(v, (int)tam_texto(v), &u) / 100; if (u) *a = x; }
        return true;
    }
    if (igual(n->tag, "input")) {
        const char *t = atributo_m(n, "type");
        if (t && (igual(t, "checkbox") || igual(t, "radio"))) { *l = 13; *a = 13; return true; }
        if (t && (igual(t, "submit") || igual(t, "button") || igual(t, "reset"))) {
            const char *v = atributo_m(n, "value");
            *l = (int)tam_texto(v ? v : (igual(t, "reset") ? "Limpar" : "Enviar")) * larg_letra(e);
            *a = altura_linha(e);
            return true;
        }
        int sz = 20; const char *v = atributo_m(n, "size"); int u;
        if (v) { int x = num100(v, (int)tam_texto(v), &u) / 100; if (u && x > 0 && x < 200) sz = x; }
        *l = sz * larg_letra(e); *a = altura_linha(e);
        return true;
    }
    if (igual(n->tag, "textarea")) {
        int cols = 20, rows = 2, u; const char *v;
        if ((v = atributo_m(n, "cols"))) { int x = num100(v, (int)tam_texto(v), &u) / 100; if (u && x > 0) cols = x; }
        if ((v = atributo_m(n, "rows"))) { int x = num100(v, (int)tam_texto(v), &u) / 100; if (u && x > 0) rows = x; }
        *l = cols * larg_letra(e); *a = rows * altura_linha(e);
        return true;
    }
    if (igual(n->tag, "select")) {
        int mx = 4;
        for (no_m *o = n->filho; o; o = o->prox) if (igual(o->tag, "option") && o->filho && o->filho->texto && o->filho->nt > mx) mx = o->filho->nt;
        if (mx > 40) mx = 40;
        *l = (mx + 2) * larg_letra(e); *a = altura_linha(e);
        return true;
    }
    return false;
}

/* ------------------------------------------------- medidas intrínsecas */

/* Largura de uma palavra (com text-transform não muda a largura). */
static int larg_texto(const estilo_t *e, int n) { return n * larg_letra(e); }

static void medir_inline(no_m *n, int *mx, int *mn, int *linha)
{
    /* percorre filhos inline acumulando a linha corrente (max) e a maior palavra (min) */
    for (no_m *c = n->filho; c; c = c->prox) {
        if (c->texto) {
            if (c->texto == 2) continue;
            const estilo_t *e = &c->pai->e;
            int pal = 0;
            for (int i = 0; i < c->nt; i++) {
                char ch = c->t[i];
                if (e->branco == 1 && ch == '\n') { if (*linha > *mx) *mx = *linha; *linha = 0; pal = 0; continue; }
                if (eh_esp(ch) && e->branco != 1) {
                    if (pal * larg_letra(e) > *mn) *mn = pal * larg_letra(e);
                    pal = 0;
                    *linha += larg_letra(e);
                    continue;
                }
                pal++;
                *linha += larg_letra(e);
            }
            if (pal * larg_letra(e) > *mn) *mn = pal * larg_letra(e);
            continue;
        }
        estilo_t *e = &c->e;
        if (e->display == D_NONE || fora_do_fluxo(c)) continue;
        if (igual(c->tag, "br")) { if (*linha > *mx) *mx = *linha; *linha = 0; continue; }
        if (eh_bloco(e->display) && !e->flutua) {
            if (*linha > *mx) *mx = *linha;
            *linha = 0;
            int a = medir_max(c), b = medir_min(c);
            if (a > *mx) *mx = a;
            if (b > *mn) *mn = b;
            continue;
        }
        if (e->flutua || eh_atomico(c)) {
            int a = medir_max(c), b = medir_min(c);
            *linha += a;
            if (b > *mn) *mn = b;
            continue;
        }
        caixa_modelo(c, 0);
        *linha += c->ml + c->mr + extra_h(c);
        medir_inline(c, mx, mn, linha);
    }
}

static void medir(no_m *n)
{
    if (n->max_c >= 0) return;
    estilo_t *e = &n->e;
    caixa_modelo(n, 0);
    int ex = extra_h(n) + n->ml + n->mr;
    int w = res_med(&e->larg, -1, e);
    if (w >= 0) {
        if (!e->caixa_borda) w += extra_h(n); else if (w < extra_h(n)) w = extra_h(n);
        n->max_c = n->min_c = w + n->ml + n->mr;
        return;
    }
    int l, a;
    if (tamanho_natural(n, &l, &a)) {
        int h = res_med(&e->alt, -1, e);
        if (h >= 0 && a > 0 && igual(n->tag, "img")) l = l * h / a;
        n->max_c = n->min_c = l + ex;
        return;
    }
    int mx = 0, mn = 0, linha = 0;
    if (e->display == D_FLEX || e->display == D_IFLEX) {
        int vao = res_med(&e->vao_c, 0, e); if (vao < 0) vao = 0;
        int k = 0;
        for (no_m *c = n->filho; c; c = c->prox) {
            if (c->texto || c->e.display == D_NONE || fora_do_fluxo(c)) continue;
            int a2 = medir_max(c), b2 = medir_min(c);
            if (e->dir & 1) { if (a2 > mx) mx = a2; }
            else mx += a2 + (k ? vao : 0);
            if (b2 > mn) mn = b2;
            if (!(e->dir & 1) && !e->quebra) { /* sem quebra: o mínimo é a soma dos mínimos encolhíveis */ }
            k++;
        }
    } else if (e->display == D_GRID) {
        int k = 0, maior = 0, vao = res_med(&e->vao_c, 0, e); if (vao < 0) vao = 0;
        for (no_m *c = n->filho; c; c = c->prox) {
            if (c->texto || c->e.display == D_NONE || fora_do_fluxo(c)) continue;
            int a2 = medir_max(c), b2 = medir_min(c);
            if (a2 > maior) maior = a2;
            if (b2 > mn) mn = b2;
            k++;
        }
        int cols = e->n_col && !e->col_rep_auto ? e->n_col : 1;
        if (cols > k && k) cols = k;
        int fixo = 0;
        for (int i = 0; i < e->n_col && !e->col_rep_auto; i++) if (e->col[i].u == U_PX) fixo += e->col[i].v;
        mx = fixo > maior * cols ? fixo : maior * cols + vao * (cols - 1);
    } else if (e->display == D_TABLE || e->display == D_ROWGROUP || e->display == D_ROW) {
        /* soma das células por linha */
        for (no_m *c = n->filho; c; c = c->prox) {
            if (c->texto || c->e.display == D_NONE) continue;
            if (c->e.display == D_CELL) { mx += medir_max(c); if (medir_min(c) > mn) mn = medir_min(c); }
            else { int a2 = medir_max(c), b2 = medir_min(c); if (a2 > mx) mx = a2; if (b2 > mn) mn = b2; }
        }
        if (e->display == D_ROW) { int s = 0; for (no_m *c = n->filho; c; c = c->prox) if (!c->texto && c->e.display == D_CELL) s += medir_min(c); if (s > mn) mn = s; }
    } else {
        medir_inline(n, &mx, &mn, &linha);
        if (linha > mx) mx = linha;
    }
    int mw = res_med(&e->max_l, -1, e);
    if (mw >= 0 && mx > mw) mx = mw;
    if (mn > mx) mn = mx;
    n->max_c = mx + ex;
    n->min_c = mn + ex;
}
static int medir_max(no_m *n) { medir(n); return n->max_c; }
static int medir_min(no_m *n) { medir(n); return n->min_c; }

/* ------------------------------------------------------ inline e linhas */

typedef struct {
    no_m *no;           /* texto: nó de texto; átomo: o elemento */
    const estilo_t *e;
    const char *t; int nt;
    int l, a;
    uint8_t tipo;       /* 0 palavra, 1 espaço, 2 átomo, 3 quebra */
    int alvo;
} item_t;

static item_t *itens; static int n_itens, cap_itens;

static void por_item(item_t *it)
{
    if (n_itens == cap_itens) {
        int nc = cap_itens ? cap_itens * 2 : 1024;
        item_t *q = m_novo(mo, nc * (int)sizeof(item_t));
        if (!q) return;
        if (n_itens) memcpy(q, itens, (size_t)n_itens * sizeof(item_t));
        itens = q; cap_itens = nc;
    }
    itens[n_itens++] = *it;
}

static int alvo_link(no_m *n);

static void coletar(no_m *n, int alvo, contexto_t *ctx, no_m *bloco_dono, int cw, int *y_flut);
static void coletar_no(no_m *c, no_m *n, int alvo, contexto_t *ctx, no_m *bloco_dono, int cw, int *y_flut);

/* Coloca um float no contexto, na altura y (relativa ao bloco). */
static void colocar_float(no_m *f, no_m *bloco, contexto_t *ctx, int cw, int y)
{
    estilo_t *e = &f->e;
    caixa_modelo(f, cw);
    int w = res_med(&e->larg, cw, e);
    if (w < 0) { int mx = medir_max(f) - f->ml - f->mr, mn = medir_min(f) - f->ml - f->mr; w = mx < cw ? mx : (mn > cw ? mn : cw); w -= extra_h(f); if (w < 0) w = 0; }
    else if (e->caixa_borda) w -= extra_h(f);
    layout_no(f, cw, w + extra_h(f), -1);
    int ow = f->l + f->ml + f->mr, oh = f->a + f->mt + f->mb;
    int x0 = bloco->bl[3] + bloco->pl[3], x1 = x0 + cw;
    /* desce até caber */
    for (int volta = 0; volta < 64; volta++) {
        int l = x0, r = x1, prox_y = INF;
        for (int i = 0; i < ctx->n; i++) {
            flut_t *q = &ctx->f[i];
            if (q->y1 <= y || q->y0 >= y + oh) continue;
            if (q->lado == 1 && q->x1 > l) l = q->x1;
            if (q->lado == 2 && q->x0 < r) r = q->x0;
            if (q->y1 < prox_y) prox_y = q->y1;
        }
        if (r - l >= ow || prox_y == INF) {
            f->x = (e->flutua == 1 ? l : r - ow) + f->ml;
            f->y = y + f->mt;
            if (ctx->n < 64) { flut_t *q = &ctx->f[ctx->n++]; q->y0 = y; q->y1 = y + oh; q->x0 = f->x - f->ml; q->x1 = f->x - f->ml + ow; q->lado = e->flutua; }
            return;
        }
        y = prox_y;
    }
    f->x = x0 + f->ml; f->y = y + f->mt;
}

static void coletar_no(no_m *c, no_m *n, int alvo, contexto_t *ctx, no_m *bloco_dono, int cw, int *y_flut)
{
    do {
        if (c->texto) {
            if (c->texto == 2 || !c->nt) return;
            const estilo_t *e = &n->e;
            if (e->visivel) { item_t it = { c, e, NULL, 0, larg_texto(e, c->nt), altura_linha(e), 1, -1 }; it.l = 0; (void)it; }
            int i = 0;
            while (i < c->nt) {
                char ch = c->t[i];
                if (e->branco == 1) {
                    if (ch == '\n') { item_t it = { c, e, NULL, 0, 0, altura_linha(e), 3, alvo }; por_item(&it); i++; continue; }
                    int k = i;
                    while (k < c->nt && c->t[k] != '\n') k++;
                    item_t it = { c, e, c->t + i, k - i, larg_texto(e, k - i), altura_linha(e), 0, alvo };
                    por_item(&it);
                    i = k;
                    continue;
                }
                if (eh_esp(ch)) {
                    while (i < c->nt && eh_esp(c->t[i])) i++;
                    item_t it = { c, e, " ", 1, larg_letra(e), altura_linha(e), 1, alvo };
                    if (e->branco == 2) it.tipo = 0;
                    por_item(&it);
                    continue;
                }
                int k = i;
                while (k < c->nt && !eh_esp(c->t[k])) k++;
                item_t it = { c, e, c->t + i, k - i, larg_texto(e, k - i), altura_linha(e), 0, alvo };
                por_item(&it);
                i = k;
            }
            return;
        }
        estilo_t *e = &c->e;
        if (e->display == D_NONE) return;
        c->x = c->y = 0;
        if (fora_do_fluxo(c)) { c->l = c->a = 0; item_t it = { c, e, NULL, 0, 0, 0, 4, alvo }; por_item(&it); return; }
        int al = alvo_link(c);
        if (al < 0) al = alvo;
        if (e->flutua) {
            item_t it = { c, e, NULL, 0, 0, 0, 5, al }; por_item(&it);
            return;
        }
        if (igual(c->tag, "br")) { item_t it = { c, &n->e, NULL, 0, 0, altura_linha(&n->e), 3, al }; por_item(&it); return; }
        if (eh_atomico(c) || eh_bloco(e->display)) {
            item_t it = { c, e, NULL, 0, 0, 0, 2, al };
            por_item(&it);
            return;
        }
        /* inline comum: margem/padding horizontais viram espaço */
        caixa_modelo(c, cw);
        int esq = c->ml + c->bl[3] + c->pl[3], dir = c->mr + c->bl[1] + c->pl[1];
        if (esq > 0) { item_t it = { c, e, NULL, 0, esq, 0, 6, al }; por_item(&it); }
        c->alvo = al;
        coletar(c, al, ctx, bloco_dono, cw, y_flut);
        if (dir > 0) { item_t it = { c, e, NULL, 0, dir, 0, 6, al }; por_item(&it); }
        } while (0);
}

static void coletar(no_m *n, int alvo, contexto_t *ctx, no_m *bloco_dono, int cw, int *y_flut)
{
    for (no_m *c = n->filho; c; c = c->prox) coletar_no(c, n, alvo, ctx, bloco_dono, cw, y_flut);
    (void)y_flut;
}

static void bordas_flut(contexto_t *ctx, int y, int h, int x0, int x1, int *l, int *r)
{
    *l = x0; *r = x1;
    for (int i = 0; i < ctx->n; i++) {
        flut_t *q = &ctx->f[i];
        if (q->y1 <= y || q->y0 >= y + h) continue;
        if (q->lado == 1 && q->x1 > *l) *l = q->x1;
        if (q->lado == 2 && q->x0 < *r) *r = q->x0;
    }
}

static frag_t *novo_frag(void) { return m_novo(mo, sizeof(frag_t)); }

/* Monta as linhas dos itens [i0, i1) no bloco; y relativo ao bloco. Devolve o novo y. */
static int montar_linhas(no_m *bloco, int i0, int i1, int y, int cw, contexto_t *ctx, int *larg_usada)
{
    estilo_t *be = &bloco->e;
    int x0 = bloco->bl[3] + bloco->pl[3];
    /* atômicos: layout antes, para saber o tamanho */
    for (int i = i0; i < i1; i++) {
        item_t *it = &itens[i];
        if (it->tipo != 2) continue;
        no_m *c = it->no;
        estilo_t *e = &c->e;
        caixa_modelo(c, cw);
        int w = res_med(&e->larg, cw, e);
        int nl, na;
        bool nat = tamanho_natural(c, &nl, &na);
        int h = res_med(&e->alt, -1, e);
        if (w >= 0) { if (!e->caixa_borda) w += extra_h(c); }
        else if (nat) {
            if (igual(c->tag, "img") && h >= 0 && na > 0) w = nl * (e->caixa_borda ? h - extra_v(c) : h) / na + extra_h(c);
            else w = nl + extra_h(c);
        } else if (eh_bloco(e->display) && !(e->display == D_TABLE)) w = cw - c->ml - c->mr;
        else {
            int mx = medir_max(c) - c->ml - c->mr, mn = medir_min(c) - c->ml - c->mr;
            int disp = cw - c->ml - c->mr;
            w = mx < disp ? mx : (mn > disp ? mn : disp);
        }
        int mw = res_med(&e->max_l, cw, e);
        if (mw >= 0 && w > mw + (e->caixa_borda ? 0 : extra_h(c))) w = mw + (e->caixa_borda ? 0 : extra_h(c));
        if (nat && h < 0 && igual(c->tag, "img") && nl > 0) { int ww = w - extra_h(c); na = na * ww / nl; }
        layout_no(c, cw, w, nat && h < 0 ? na + extra_v(c) : -1);
        it = &itens[i];   /* o layout aninhado pode ter realocado a lista */
        it->l = c->l + c->ml + c->mr;
        it->a = c->a + c->mt + c->mb;
        if (eh_bloco(e->display)) it->tipo = 7;   /* bloco no meio do inline (dentro de <a> ou <span>) */
    }
    int i = i0;
    int max_w = 0;
    bool primeira = true;
    while (i < i1) {
        /* pula espaços no início da linha */
        while (i < i1 && itens[i].tipo == 1) i++;
        if (i >= i1) break;
        if (itens[i].tipo == 7) {   /* bloco interno: linha própria */
            item_t *it = &itens[i];
            no_m *c = it->no;
            int l, r;
            bordas_flut(ctx, y, it->a, x0, x0 + cw, &l, &r);
            c->x = l + c->ml; c->y = y + c->mt;
            no_m *p = c->pai;
            while (p && p != bloco) { c->x -= p->x; c->y -= p->y; p = p->pai; }
            y += it->a;
            if (it->l > max_w) max_w = it->l;
            i++;
            continue;
        }
        int lh = 0;
        int l, r;
        int h_prev = altura_linha(be);
        bordas_flut(ctx, y, h_prev, x0, x0 + cw, &l, &r);
        int disp = r - l;
        /* junta itens até encher */
        int j = i, w = 0, ult_quebra = -1, w_quebra = 0;
        bool nowrap = be->branco == 2;
        while (j < i1) {
            item_t *it = &itens[j];
            if (it->tipo == 3) break;
            if (it->tipo == 7) break;
            if (it->tipo == 4 || it->tipo == 5) { j++; continue; }
            if (it->tipo == 1) { ult_quebra = j; w_quebra = w; }
            if (w + it->l > disp && j > i && !nowrap && it->tipo != 1) {
                if (ult_quebra > i) { j = ult_quebra; w = w_quebra; }
                else if (ctx->n && disp < cw && w == 0) { /* desce abaixo do float */ }
                break;
            }
            w += it->l;
            j++;
            if (it->tipo == 2 && it->no->e.display != D_INLINE && j < i1 && itens[j].tipo == 2) { ult_quebra = j; w_quebra = w; }
        }
        if (j == i && i < i1 && itens[i].tipo != 3 && itens[i].tipo != 7) {
            if (ctx->n && disp < cw) {   /* não coube ao lado do float: desce */
                int prox = INF;
                for (int q = 0; q < ctx->n; q++) if (ctx->f[q].y1 > y && ctx->f[q].y1 < prox) prox = ctx->f[q].y1;
                if (prox < INF) { y = prox; continue; }
            }
            w = itens[i].l; j = i + 1;
        }
        /* tira espaços do fim */
        int fim = j;
        while (fim > i && itens[fim - 1].tipo == 1) { w -= itens[fim - 1].l; fim--; }
        /* altura da linha */
        int sobe = 0, desce = 0;   /* em relação à linha de base */
        for (int q = i; q < fim; q++) {
            item_t *it = &itens[q];
            if (it->tipo == 0 || it->tipo == 1 || it->tipo == 6) {
                int a = altura_linha(it->e), t = it->e->tam;
                int s = (a - t) / 2 + t * 4 / 5, d = a - s;
                if (s > sobe) sobe = s;
                if (d > desce) desce = d;
            } else if (it->tipo == 2) {
                uint8_t va = it->no->e.vert;
                if (va == 1) { int s = it->a / 2 + be->tam / 4, d = it->a - s; if (s > sobe) sobe = s; if (d > desce) desce = d; }
                else if (it->a > sobe) sobe = it->a;
            }
        }
        if (fim == i) { int a = altura_linha(be); sobe = a * 4 / 5; desce = a - sobe; }
        lh = sobe + desce;
        if (primeira && lh == 0) lh = 0;
        /* posição horizontal */
        int x = l;
        if (be->alinha == 1) x += (disp - w) / 2;
        else if (be->alinha == 2) x += disp - w;
        if (x < l && be->alinha) x = l;
        int base = y + sobe;
        for (int q = i; q < fim; q++) {
            item_t *it = &itens[q];
            if (it->tipo == 0 || it->tipo == 1) {
                if (it->tipo == 0 && !it->e->visivel) {
                    frag_t *f = novo_frag();
                    if (!f) return y;
                    int t = it->e->tam;
                    f->dono = bloco; f->estilo_de = it->no;
                    f->x = x; f->y = base - t * 4 / 5; f->l = it->l; f->a = t;
                    f->t = it->t; f->nt = it->nt; f->alvo = it->alvo;
                    por_frag(bloco, f);
                } else if (it->tipo == 1 && it->alvo >= 0 && it->e->sublinha) {
                    frag_t *f = novo_frag();
                    if (!f) return y;
                    int t = it->e->tam;
                    f->dono = bloco; f->estilo_de = it->no;
                    f->x = x; f->y = base - t * 4 / 5; f->l = it->l; f->a = t;
                    f->t = " "; f->nt = 1; f->alvo = it->alvo;
                    por_frag(bloco, f);
                }
                x += it->l;
            } else if (it->tipo == 6) x += it->l;
            else if (it->tipo == 2) {
                no_m *c = it->no;
                int ay = it->no->e.vert == 1 ? base - (it->a / 2 + be->tam / 4) : it->no->e.vert == 2 ? y : base - it->a;
                c->x = x + c->ml; c->y = ay + c->mt;
                no_m *p = c->pai;
                while (p && p != bloco) { c->x -= p->x; c->y -= p->y; p = p->pai; }
                if (it->alvo >= 0) c->alvo = it->alvo;
                x += it->l;
            }
        }
        if (x - l > max_w) max_w = x - l;
        /* floats e posicionados que estavam nesta linha */
        for (int q = i; q < j; q++) {
            item_t *it = &itens[q];
            if (it->tipo == 5) {
                no_m *fn = it->no;
                colocar_float(fn, bloco, ctx, cw, y);
                no_m *p = fn->pai;
                while (p && p != bloco) { fn->x -= p->x; fn->y -= p->y; p = p->pai; }
            } else if (it->tipo == 4) {
                it->no->x = x; it->no->y = y;
                no_m *p = it->no->pai;
                while (p && p != bloco) { it->no->x -= p->x; it->no->y -= p->y; p = p->pai; }
            }
        }
        y += lh;
        i = j;
        if (i < i1 && itens[i].tipo == 3) {
            if (fim == j && j > 0 && (j == i0 || itens[j - 1].tipo == 3)) y += itens[i].a;   /* <br> numa linha vazia */
            i++;
        }
        primeira = false;
    }
    /* floats e posicionados soltos depois da última linha */
    for (int q = i0; q < i1; q++) {
        item_t *it = &itens[q];
        if (it->tipo == 5 && it->no->l == 0 && it->no->a == 0 && it->no->y == 0) {
            no_m *fn = it->no;
            colocar_float(fn, bloco, ctx, cw, y);
            no_m *p = fn->pai;
            while (p && p != bloco) { fn->x -= p->x; fn->y -= p->y; p = p->pai; }
        }
    }
    if (larg_usada && max_w > *larg_usada) *larg_usada = max_w;
    return y;
}

static int alvo_link(no_m *n)
{
    if (!igual(n->tag, "a")) return -1;
    const char *h = atributo_m(n, "href");
    if (!h || !h[0] || (h[0] == '#' && !h[1])) return -1;
    if (n->alvo >= 0) return n->alvo;
    if (!crescer_v(mo, (void **)&mo->alvos, &mo->cap_alvos, mo->n_alvos + 1, sizeof(motor_alvo_t))) return -1;
    motor_alvo_t *a = &mo->alvos[mo->n_alvos];
    memset(a, 0, sizeof *a);
    a->tipo = MA_LINK;
    char u[600];
    juntar(mo->url, h, u, sizeof u);
    a->href = m_dup(mo, u, (int)tam_texto(u));
    n->alvo = mo->n_alvos;
    return mo->n_alvos++;
}

/* ------------------------------------------------------------ bloco */

static int fechar_inline(no_m *n, int ini, int y, int cw, contexto_t *ctx, int *usado)
{
    if (n_itens > ini) y = montar_linhas(n, ini, n_itens, y, cw, ctx, usado);
    n_itens = ini;
    return y;
}


/* Floats valem no contexto de formatação inteiro: um bloco filho comum
 * herda os floats do pai (transladados) e devolve os que criar. */
static contexto_t *ctx_herdado; static int her_dx, her_dy;

static bool novo_contexto(no_m *c)
{
    estilo_t *e = &c->e;
    return e->corta || e->flutua || fora_do_fluxo(c) || e->display == D_FLEX || e->display == D_IFLEX || e->display == D_GRID ||
           e->display == D_TABLE || e->display == D_IBLOCK || e->display == D_CELL;
}

/* Layout de fluxo (bloco com filhos bloco e inline). y começa no topo do conteúdo. */
static int fluxo(no_m *n, int cw, int *usado)
{
    contexto_t *ctx = m_novo(mo, sizeof(contexto_t));
    if (!ctx) return 0;
    contexto_t *pai_ctx = ctx_herdado;
    int pdx = her_dx, pdy = her_dy, herdados = 0;
    ctx_herdado = NULL;
    if (pai_ctx) {
        for (int i = 0; i < pai_ctx->n && ctx->n < 64; i++) {
            flut_t q = pai_ctx->f[i];
            q.x0 -= pdx; q.x1 -= pdx; q.y0 -= pdy; q.y1 -= pdy;
            if (q.y1 <= 0) continue;
            ctx->f[ctx->n++] = q;
        }
        herdados = ctx->n;
    }
    int x0 = n->bl[3] + n->pl[3];
    int y = n->bl[0] + n->pl[0];
    int ini = n_itens;
    int marg_ant = 0;   /* margem de baixo do bloco anterior, para colapsar */
    bool primeiro = true;
    /* filhos efetivos: display:contents é achatado */
    no_m *pilha[16]; int np = 0;
    no_m *c = n->filho;
    while (c || np) {
        if (!c) { c = pilha[--np]->prox; continue; }
        if (!c->texto && c->e.display == D_CONTENTS && np < 16) { pilha[np++] = c; c->x = c->y = 0; c->l = c->a = 0; c = c->filho; continue; }
        bool blk = !c->texto && c->e.display != D_NONE && eh_bloco(c->e.display) && !c->e.flutua && !fora_do_fluxo(c) && c->e.display != D_IBLOCK;
        if (!blk) {
            /* inline: guarda para montar as linhas */
            if (c->texto || c->e.display != D_NONE) coletar_no(c, c->pai, -1, ctx, n, cw, NULL);
            c = c->prox;
            continue;
        }
        int y_antes = y;
        y = fechar_inline(n, ini, y, cw, ctx, usado);
        if (y != y_antes) marg_ant = 0;
        estilo_t *e = &c->e;
        if (e->limpa) {
            int maxy = y;
            for (int i = 0; i < ctx->n; i++) if ((e->limpa & ctx->f[i].lado) && ctx->f[i].y1 > maxy) maxy = ctx->f[i].y1;
            if (maxy > y) { y = maxy; marg_ant = 0; }
        }
        caixa_modelo(c, cw);
        int mt = c->mt;
        /* primeiro filho sem nada antes: a margem dele vaza para o pai (se o pai não tiver borda/padding em cima) */
        bool vaza = primeiro && y == n->bl[0] + n->pl[0] && !n->bl[0] && !n->pl[0] && !novo_contexto(n) && n->pai && n->pai->pai &&
                    n->e.display == D_BLOCK && mt > 0;
        int col = primeiro && (n->bl[0] + n->pl[0]) == 0 && n->pai && n->e.display != D_FLEX ? 0 : 1;
        (void)col;
        int dy = mt > marg_ant ? mt - marg_ant : 0;
        if (mt < 0) dy = mt;
        if (vaza) { n->colapso = mt; dy = 0; }
        int bw = cw - c->ml - c->mr;
        int w = -1;
        if (!novo_contexto(c)) { ctx_herdado = ctx; her_dx = x0 + c->ml; her_dy = y + dy; }
        layout_no(c, cw, -1, -1);
        ctx_herdado = NULL;
        if (c->colapso > 0 && !vaza) {   /* o filho devolveu margem: soma à dele, colapsando */
            int ef = c->mt > c->colapso ? c->mt : c->colapso;
            int dy2 = ef > marg_ant ? ef - marg_ant : 0;
            if (dy2 > dy) dy = dy2;
        }
        w = c->l;
        (void)bw;
        /* margens automáticas: centraliza */
        int x = x0 + c->ml;
        if (e->marg[3].u == U_AUTO && e->marg[1].u == U_AUTO) x = x0 + (cw - w) / 2;
        else if (e->marg[3].u == U_AUTO) x = x0 + cw - w - c->mr;
        /* blocos que formam contexto próprio desviam dos floats */
        if (ctx->n && (e->corta || e->display == D_FLEX || e->display == D_GRID || e->display == D_TABLE)) {
            int l, r;
            bordas_flut(ctx, y + dy, c->a, x0, x0 + cw, &l, &r);
            if (l > x0 && x < l) x = l + c->ml;
        }
        c->x = x;
        c->y = y + dy;
        y = c->y + c->a;
        marg_ant = c->mb > 0 ? c->mb : 0;
        y += c->mb;
        if (c->ml + w + c->mr > *usado) *usado = c->ml + w + c->mr;
        primeiro = false;
        c = c->prox;
    }
    y = fechar_inline(n, ini, y, cw, ctx, usado);
    /* floats novos voltam para o contexto do pai */
    if (pai_ctx)
        for (int i = herdados; i < ctx->n && pai_ctx->n < 64; i++) {
            flut_t q = ctx->f[i];
            q.x0 += pdx; q.x1 += pdx; q.y0 += pdy; q.y1 += pdy;
            pai_ctx->f[pai_ctx->n++] = q;
        }
    /* a caixa cresce para conter os floats */
    if (n->e.corta || n->e.display == D_IBLOCK || n->e.flutua || n->e.display == D_CELL || fora_do_fluxo(n) || n->e.display == D_FLEX || !n->pai || !n->pai->pai)
        for (int i = 0; i < ctx->n; i++) if (ctx->f[i].y1 > y) y = ctx->f[i].y1;
    return y - (n->bl[0] + n->pl[0]);
}

static void por_frag(no_m *dono, frag_t *f)
{
    f->prox = NULL;
    if (dono->frags_fim) dono->frags_fim->prox = f; else dono->frags = f;
    dono->frags_fim = f;
}

static void deslocar_conteudo(no_m *n, int dy)
{
    if (!dy) return;
    for (no_m *c = n->filho; c; c = c->prox) if (!c->texto && c->e.display != D_NONE && !fora_do_fluxo(c)) c->y += dy;
    for (frag_t *f = n->frags; f; f = f->prox) f->y += dy;
}

/* ------------------------------------------------------------ flex */

typedef struct { no_m *c; int base, hip, mn, mx, fin, cruz; int linha; } fitem_t;

static int flex(no_m *n, int cw, int ch)
{
    estilo_t *e = &n->e;
    bool col = e->dir & 1;
    int gm = res_med(col ? &e->vao_l : &e->vao_c, cw, e), gc = res_med(col ? &e->vao_c : &e->vao_l, cw, e);
    if (gm < 0) gm = 0;
    if (gc < 0) gc = 0;
    int qtd = 0;
    for (no_m *c = n->filho; c; c = c->prox) if (!c->texto && c->e.display != D_NONE && !fora_do_fluxo(c)) qtd++;
    if (!qtd) return 0;
    fitem_t *it = m_novo(mo, qtd * (int)sizeof(fitem_t));
    if (!it) return 0;
    int k = 0;
    for (no_m *c = n->filho; c; c = c->prox) if (!c->texto && c->e.display != D_NONE && !fora_do_fluxo(c)) it[k++].c = c;
    for (int i = 1; i < k; i++) { fitem_t x = it[i]; int j = i - 1; while (j >= 0 && it[j].c->e.ordem > x.c->e.ordem) { it[j + 1] = it[j]; j--; } it[j + 1] = x; }
    if (e->dir & 2) for (int i = 0; i < k / 2; i++) { fitem_t x = it[i]; it[i] = it[k - 1 - i]; it[k - 1 - i] = x; }
    int x0 = n->bl[3] + n->pl[3], y0 = n->bl[0] + n->pl[0];
    /* tamanhos base */
    for (int i = 0; i < k; i++) {
        no_m *c = it[i].c; estilo_t *ce = &c->e;
        caixa_modelo(c, cw);
        int mg = col ? c->mt + c->mb : c->ml + c->mr;
        int ex = col ? extra_v(c) : extra_h(c);
        int b = res_med(&ce->base, col ? ch : cw, ce);
        if (ce->base.u == U_AUTO || b < 0) {
            int w = res_med(col ? &ce->alt : &ce->larg, col ? ch : cw, ce);
            if (w >= 0) b = ce->caixa_borda ? w : w + ex;
            else if (!col) {
                b = medir_max(c) - mg;
                if (b > cw && cw < INF) { int mn = medir_min(c) - mg; b = mn > cw ? mn : cw; }
            } else {
                int ww = res_med(&ce->larg, cw, ce);
                int lw = ww >= 0 ? (ce->caixa_borda ? ww : ww + extra_h(c)) : -1;
                if (lw < 0) { uint8_t al = ce->alinha_si ? (ce->alinha_si == 1 ? 0 : ce->alinha_si - 1) : e->alinha_itens; if (al == 0) lw = cw - c->ml - c->mr; else { int mx = medir_max(c) - c->ml - c->mr; lw = mx < cw - c->ml - c->mr ? mx : cw - c->ml - c->mr; } }
                layout_no(c, cw, lw, -1);
                b = c->a;
            }
        } else if (!ce->caixa_borda) b += ex;
        int mn = res_med(col ? &ce->min_a : &ce->min_l, col ? ch : cw, ce);
        if (mn < 0 || (col ? ce->min_a.u : ce->min_l.u) == U_AUTO) mn = 0;
        if ((col ? ce->min_a.u : ce->min_l.u) == U_PX && (col ? ce->min_a.v : ce->min_l.v) == 0 && !col && !ce->corta) {
            /* min-width automático: não encolhe abaixo do conteúdo mínimo */
            int w = res_med(&ce->larg, cw, ce);
            int mc = medir_min(c) - mg;
            mn = w >= 0 && w < mc ? (ce->caixa_borda ? w : w + ex) : mc;
        }
        int mx = res_med(col ? &ce->max_a : &ce->max_l, col ? ch : cw, ce);
        if (mx < 0) mx = INF; else if (!ce->caixa_borda) mx += ex;
        if (mn > mx) mn = mx;
        it[i].base = b < 0 ? 0 : b;
        it[i].mn = mn; it[i].mx = mx;
        it[i].hip = it[i].base < mn ? mn : it[i].base > mx ? mx : it[i].base;
    }
    int principal = col ? (ch >= 0 ? ch : INF) : cw;
    /* linhas */
    int nlin = 0;
    {
        int usado = 0, cnt = 0;
        for (int i = 0; i < k; i++) {
            no_m *c = it[i].c;
            int o = it[i].hip + (col ? c->mt + c->mb : c->ml + c->mr);
            if (e->quebra && cnt && usado + gm + o > principal) { nlin++; usado = 0; cnt = 0; }
            usado += (cnt ? gm : 0) + o; cnt++;
            it[i].linha = nlin;
        }
        nlin++;
    }
    int cruz_total = 0;
    int pos_cruz = col ? x0 : y0;
    int conteudo_principal = 0;
    for (int L = 0, i0 = 0; L < nlin; L++) {
        int i1 = i0;
        while (i1 < k && it[i1].linha == L) i1++;
        int usado = 0, cresce = 0, encolhe = 0;
        for (int i = i0; i < i1; i++) {
            no_m *c = it[i].c;
            usado += it[i].hip + (col ? c->mt + c->mb : c->ml + c->mr) + (i > i0 ? gm : 0);
            cresce += c->e.cresce;
            encolhe += c->e.encolhe * it[i].base / 100;
        }
        int livre = (principal >= INF ? usado : principal) - usado;
        for (int i = i0; i < i1; i++) it[i].fin = it[i].hip;
        for (int volta = 0; volta < 3; volta++) {
            if (livre > 0 && cresce > 0) {
                int dist = 0, cr2 = 0;
                for (int i = i0; i < i1; i++) {
                    int g = it[i].c->e.cresce;
                    if (!g) continue;
                    int add = muldiv(livre, g, cresce);
                    int nv = it[i].fin + add;
                    if (nv > it[i].mx) nv = it[i].mx; else cr2 += g;
                    dist += nv - it[i].fin;
                    it[i].fin = nv;
                }
                livre -= dist; cresce = cr2;
                if (livre <= 1 || !dist) break;
            } else if (livre < 0 && encolhe > 0) {
                int tira = 0, en2 = 0;
                for (int i = i0; i < i1; i++) {
                    int s = it[i].c->e.encolhe * it[i].base / 100;
                    if (!s || it[i].fin <= it[i].mn) continue;
                    int sub = muldiv(-livre, s, encolhe);
                    int nv = it[i].fin - sub;
                    if (nv < it[i].mn) nv = it[i].mn; else en2 += s;
                    tira += it[i].fin - nv;
                    it[i].fin = nv;
                }
                livre += tira; encolhe = en2;
                if (livre >= -1 || !tira) break;
            } else break;
        }
        /* layout de cada item no tamanho final */
        int cruz_linha = 0;
        int cruz_def = col ? cw : (ch >= 0 && !e->quebra ? ch : -1);
        for (int i = i0; i < i1; i++) {
            no_m *c = it[i].c; estilo_t *ce = &c->e;
            if (col) {
                int ww = res_med(&ce->larg, cw, ce), lw;
                uint8_t al = ce->alinha_si ? (ce->alinha_si == 1 ? 0 : ce->alinha_si - 1) : e->alinha_itens;
                if (ww >= 0) lw = ce->caixa_borda ? ww : ww + extra_h(c);
                else if (al == 0) lw = cw - c->ml - c->mr;
                else { int mx = medir_max(c) - c->ml - c->mr; lw = mx < cw - c->ml - c->mr ? mx : cw - c->ml - c->mr; }
                layout_no(c, cw, lw, it[i].fin != it[i].base || ce->alt.u != U_AUTO ? it[i].fin : -1);
                it[i].cruz = c->l + c->ml + c->mr;
            } else {
                layout_no(c, cw, it[i].fin, -1);
                it[i].cruz = c->a + c->mt + c->mb;
            }
            if (it[i].cruz > cruz_linha) cruz_linha = it[i].cruz;
        }
        if (cruz_def >= 0 && nlin == 1) cruz_linha = cruz_def;
        /* alinhamento no eixo principal */
        int soma = 0, autos = 0;
        for (int i = i0; i < i1; i++) {
            no_m *c = it[i].c;
            soma += (col ? c->a + c->mt + c->mb : c->l + c->ml + c->mr) + (i > i0 ? gm : 0);
            if (!col) autos += (c->e.marg[3].u == U_AUTO) + (c->e.marg[1].u == U_AUTO);
            else autos += (c->e.marg[0].u == U_AUTO) + (c->e.marg[2].u == U_AUTO);
        }
        int resta = (principal >= INF ? soma : principal) - soma;
        int pos = 0, entre = 0;
        int nitens = i1 - i0;
        if (resta > 0 && !autos) {
            switch (e->justifica) {
            case 1: pos = resta / 2; break;
            case 2: pos = resta; break;
            case 4: entre = nitens > 1 ? resta / (nitens - 1) : 0; break;
            case 5: entre = resta / nitens; pos = entre / 2; break;
            case 6: entre = resta / (nitens + 1); pos = entre; break;
            default: break;
            }
        }
        int cursor = (col ? y0 : x0) + pos;
        for (int i = i0; i < i1; i++) {
            no_m *c = it[i].c; estilo_t *ce = &c->e;
            int ma = 0, mb2 = 0;
            if (resta > 0 && autos) {
                int fat = resta / autos;
                if (!col) { if (ce->marg[3].u == U_AUTO) ma = fat; if (ce->marg[1].u == U_AUTO) mb2 = fat; }
                else { if (ce->marg[0].u == U_AUTO) ma = fat; if (ce->marg[2].u == U_AUTO) mb2 = fat; }
            }
            uint8_t al = ce->alinha_si ? (ce->alinha_si == 1 ? 0 : ce->alinha_si - 1) : e->alinha_itens;
            if (col) {
                c->y = cursor + c->mt + ma;
                int livre_c = cruz_linha - (c->l + c->ml + c->mr);
                int dx = al == 1 ? livre_c / 2 : al == 2 ? livre_c : 0;
                if (ce->marg[3].u == U_AUTO && ce->marg[1].u == U_AUTO) dx = livre_c / 2;
                c->x = pos_cruz + c->ml + (dx > 0 ? dx : 0);
                cursor = c->y + c->a + c->mb + mb2 + gm + entre;
            } else {
                c->x = cursor + c->ml + ma;
                int livre_c = cruz_linha - (c->a + c->mt + c->mb);
                if (al == 0 && ce->alt.u == U_AUTO && livre_c > 0) {
                    layout_no(c, cw, c->l, cruz_linha - c->mt - c->mb);
                    livre_c = 0;
                }
                int dy = al == 1 ? livre_c / 2 : al == 2 ? livre_c : 0;
                if (ce->marg[0].u == U_AUTO && ce->marg[2].u == U_AUTO) dy = livre_c / 2;
                c->y = pos_cruz + c->mt + (dy > 0 ? dy : 0);
                cursor = c->x + c->l + c->mr + mb2 + gm + entre;
            }
        }
        int fim_principal = cursor - (col ? y0 : x0) - gm - entre;
        if (fim_principal > conteudo_principal) conteudo_principal = fim_principal;
        pos_cruz += cruz_linha + gc;
        cruz_total += cruz_linha + (L ? gc : 0);
        i0 = i1;
    }
    return col ? (ch >= 0 ? ch : conteudo_principal) : cruz_total;
}

/* ------------------------------------------------------------ grid */

static int grid(no_m *n, int cw)
{
    estilo_t *e = &n->e;
    int gcol = res_med(&e->vao_c, cw, e), glin = res_med(&e->vao_l, cw, e);
    if (gcol < 0) gcol = 0;
    if (glin < 0) glin = 0;
    medida_t tr[MAX_TRILHAS]; int nt = 0;
    if (e->n_col && e->col_rep_auto) {
        int mn = res_med(&e->col[0], cw, e);
        if (mn <= 0) mn = 100;
        nt = (cw + gcol) / (mn + gcol);
        if (nt < 1) nt = 1;
        if (nt > MAX_TRILHAS) nt = MAX_TRILHAS;
        for (int i = 0; i < nt; i++) { tr[i].u = U_FR; tr[i].v = 100; }
    } else if (e->n_col) { nt = e->n_col; memcpy(tr, e->col, sizeof(medida_t) * (size_t)nt); }
    else { nt = 1; tr[0].u = U_FR; tr[0].v = 100; }
    int qtd = 0;
    for (no_m *c = n->filho; c; c = c->prox) if (!c->texto && c->e.display != D_NONE && !fora_do_fluxo(c)) qtd++;
    if (!qtd) return 0;
    typedef struct { no_m *c; int lin, col, span, lspan; } gi_t;
    /* áreas nomeadas: "a a b" "c c b" */
    char *anome[24]; int ar0[24], ac0[24], ar1[24], ac1[24], nareas = 0, acols = 0;
    if (e->areas) {
        int lin = 0;
        for (const char *q = e->areas; *q;) {
            while (*q && *q != '"' && *q != '\'') q++;
            if (!*q) break;
            char aspa = *q++;
            int col = 0;
            while (*q && *q != aspa) {
                while (*q == ' ') q++;
                const char *w0 = q;
                while (*q && *q != ' ' && *q != aspa) q++;
                int tw = (int)(q - w0);
                if (!tw) continue;
                if (!(tw == 1 && w0[0] == '.')) {
                    int f = -1;
                    for (int z = 0; z < nareas; z++) if ((int)tam_texto(anome[z]) == tw && !memcmp(anome[z], w0, (size_t)tw)) f = z;
                    if (f < 0 && nareas < 24) { f = nareas++; anome[f] = m_dup(mo, w0, tw); ar0[f] = lin; ac0[f] = col; ar1[f] = lin; ac1[f] = col; }
                    if (f >= 0) { if (lin > ar1[f]) ar1[f] = lin; if (col > ac1[f]) ac1[f] = col; }
                }
                col++;
            }
            if (*q) q++;
            if (col > acols) acols = col;
            lin++;
        }
        if (acols > nt) {
            for (int i = nt; i < acols && i < MAX_TRILHAS; i++) { tr[i].u = U_FR; tr[i].v = 100; }
            nt = acols > MAX_TRILHAS ? MAX_TRILHAS : acols;
        }
    }
    gi_t *it = m_novo(mo, qtd * (int)sizeof(gi_t));
    int max_lin = qtd * 2 + 8;
    uint32_t *ocup = m_novo(mo, max_lin * 4);
    if (!it || !ocup) return 0;
    int k = 0, cl = 0, cc = 0;
    for (no_m *c = n->filho; c; c = c->prox) {
        if (c->texto || c->e.display == D_NONE || fora_do_fluxo(c)) continue;
        int span = c->e.g_span < 0 ? nt : c->e.g_span;
        int ini = c->e.g_ini > 0 ? c->e.g_ini - 1 : -1;
        int lin_fixa = c->e.g_lin > 0 ? c->e.g_lin - 1 : -1, lspan = c->e.g_lspan > 0 ? c->e.g_lspan : 1;
        if (c->e.area) for (int z = 0; z < nareas; z++) if (igual(anome[z], c->e.area)) {
            ini = ac0[z]; span = ac1[z] - ac0[z] + 1; lin_fixa = ar0[z]; lspan = ar1[z] - ar0[z] + 1;
        }
        if (span > nt) span = nt;
        if (ini >= nt) ini = nt - 1;
        if (ini >= 0 && ini + span > nt) span = nt - ini;
        if (c->e.g_span < 0 && ini > 0) span = nt - ini;
        if (span < 1) span = 1;
        uint32_t mask = ((span >= 32 ? 0xFFFFFFFFu : (1u << span) - 1));
        int lin = -1, col = -1;
        if (lin_fixa >= 0 && lin_fixa < max_lin) {
            lin = lin_fixa;
            col = ini >= 0 ? ini : 0;
            if (ini < 0) while (col + span <= nt && (ocup[lin] & (mask << col))) col++;
            if (col + span > nt) col = 0;
        } else {
            for (;;) {
                if (cl >= max_lin) { cl = max_lin - 1; break; }
                int cc2 = ini >= 0 ? ini : cc;
                if (ini >= 0 && cc2 < cc) { cl++; cc = 0; continue; }
                if (cc2 + span > nt) { cl++; cc = 0; continue; }
                if (ocup[cl] & (mask << cc2)) { if (ini >= 0) { cl++; cc = 0; } else cc++; continue; }
                lin = cl; col = cc2;
                cc = cc2 + span;
                break;
            }
            if (lin < 0) { lin = cl; col = 0; }
        }
        for (int z = 0; z < lspan && lin + z < max_lin; z++) ocup[lin + z] |= mask << col;
        it[k].c = c; it[k].lin = lin; it[k].col = col; it[k].span = span; it[k].lspan = lspan;
        k++;
    }
    /* larguras das trilhas */
    int w[MAX_TRILHAS], fixo = gcol * (nt - 1), fr = 0;
    for (int i = 0; i < nt; i++) {
        w[i] = 0;
        if (tr[i].u == U_FR) { fr += tr[i].v; continue; }
        if (tr[i].u == U_AUTO || tr[i].u == U_MAXC || tr[i].u == U_MINC || tr[i].u == U_FIT) {
            for (int q = 0; q < k; q++) if (it[q].col == i && it[q].span == 1) { int x = tr[i].u == U_MINC ? medir_min(it[q].c) : medir_max(it[q].c); if (x > w[i]) w[i] = x; }
        } else { int x = res_med(&tr[i], cw, e); w[i] = x < 0 ? 0 : x; }
        fixo += w[i];
    }
    int sobra = cw - fixo;
    if (sobra < 0 && fr == 0) {   /* encolhe as auto */
        int autos = 0;
        for (int i = 0; i < nt; i++) if (tr[i].u == U_AUTO) autos += w[i];
        if (autos > 0) for (int i = 0; i < nt; i++) if (tr[i].u == U_AUTO) w[i] += muldiv(sobra, w[i], autos);
    }
    if (fr) for (int i = 0; i < nt; i++) if (tr[i].u == U_FR) { w[i] = sobra > 0 ? muldiv(sobra, tr[i].v, fr) : 0; }
    int xs[MAX_TRILHAS + 1];
    xs[0] = n->bl[3] + n->pl[3];
    for (int i = 0; i < nt; i++) xs[i + 1] = xs[i] + w[i] + gcol;
    /* layout e alturas das linhas */
    int nlin = 0;
    for (int q = 0; q < k; q++) if (it[q].lin + it[q].lspan > nlin) nlin = it[q].lin + it[q].lspan;
    int *alt = m_novo(mo, (nlin + 1) * 4);
    if (!alt) return 0;
    for (int q = 0; q < k; q++) {
        no_m *c = it[q].c;
        int ww = xs[it[q].col + it[q].span] - xs[it[q].col] - gcol;
        caixa_modelo(c, ww);
        int lw = res_med(&c->e.larg, ww, &c->e);
        int lar = lw >= 0 ? (c->e.caixa_borda ? lw : lw + extra_h(c)) : ww - c->ml - c->mr;
        if (lar > ww - c->ml - c->mr && lw < 0) lar = ww - c->ml - c->mr;
        layout_no(c, ww, lar, -1);
        int h = c->a + c->mt + c->mb;
        if (it[q].lspan <= 1 && h > alt[it[q].lin]) alt[it[q].lin] = h;
    }
    for (int q = 0; q < k; q++) {   /* quem ocupa várias linhas empurra a última */
        if (it[q].lspan <= 1) continue;
        int h = it[q].c->a + it[q].c->mt + it[q].c->mb, soma = 0, ult = it[q].lin + it[q].lspan - 1;
        if (ult >= nlin) ult = nlin - 1;
        for (int L = it[q].lin; L <= ult; L++) soma += alt[L] + (L < ult ? glin : 0);
        if (h > soma) alt[ult] += h - soma;
    }
    int y = n->bl[0] + n->pl[0];
    int *ys = m_novo(mo, (nlin + 1) * 4);
    if (!ys) return 0;
    for (int L = 0; L < nlin; L++) { ys[L] = y; y += alt[L] + (L + 1 < nlin ? glin : 0); }
    for (int q = 0; q < k; q++) {
        no_m *c = it[q].c;
        int ww = xs[it[q].col + it[q].span] - xs[it[q].col] - gcol;
        int alt_area = ys[it[q].lin + it[q].lspan - 1 < nlin ? it[q].lin + it[q].lspan - 1 : nlin - 1] + alt[it[q].lin + it[q].lspan - 1 < nlin ? it[q].lin + it[q].lspan - 1 : nlin - 1] - ys[it[q].lin];
        int livre = alt_area - (c->a + c->mt + c->mb);
        uint8_t al = c->e.alinha_si ? (c->e.alinha_si == 1 ? 0 : c->e.alinha_si - 1) : e->alinha_itens;
        if (livre > 0 && al == 0 && c->e.alt.u == U_AUTO) { layout_no(c, ww, c->l, alt_area - c->mt - c->mb); livre = 0; }
        int dx = 0;
        if (e->justifica == 1 || (c->e.marg[3].u == U_AUTO && c->e.marg[1].u == U_AUTO)) dx = (ww - c->l - c->ml - c->mr) / 2;
        c->x = xs[it[q].col] + c->ml + (dx > 0 ? dx : 0);
        c->y = ys[it[q].lin] + c->mt + (al == 1 ? livre / 2 : al == 2 ? livre : 0);
    }
    return y - (n->bl[0] + n->pl[0]);
}

/* ------------------------------------------------------------ tabela */

#define MAX_COLS 32
static int tabela(no_m *n, int cw)
{
    /* linhas */
    no_m **linhas = m_novo(mo, 1024 * (int)sizeof(no_m *)), **grupo_de = m_novo(mo, 1024 * (int)sizeof(no_m *)); int nl = 0;
    if (!linhas || !grupo_de) return 0;
    no_m *legenda = NULL;
    for (no_m *c = n->filho; c && nl < 1024; c = c->prox) {
        if (c->texto || c->e.display == D_NONE) continue;
        if (c->e.display == D_CAPTION) { legenda = c; continue; }
        if (c->e.display == D_ROWGROUP) {
            for (no_m *r = c->filho; r && nl < 1024; r = r->prox) if (!r->texto && r->e.display == D_ROW) { grupo_de[nl] = c; linhas[nl++] = r; }
            continue;
        }
        if (c->e.display == D_ROW) { grupo_de[nl] = NULL; linhas[nl++] = c; continue; }
        /* outra coisa: vira uma linha de uma célula */
        grupo_de[nl] = NULL; linhas[nl++] = c;
    }
    int esp = 2;
    int cmax[MAX_COLS], cmin[MAX_COLS], ncol = 0;
    memset(cmax, 0, sizeof cmax); memset(cmin, 0, sizeof cmin);
    for (int r = 0; r < nl; r++) {
        int col = 0;
        if (linhas[r]->e.display != D_ROW) { int a = medir_max(linhas[r]), b = medir_min(linhas[r]); if (a > cmax[0]) cmax[0] = a; if (b > cmin[0]) cmin[0] = b; if (ncol < 1) ncol = 1; continue; }
        for (no_m *c = linhas[r]->filho; c && col < MAX_COLS; c = c->prox) {
            if (c->texto || c->e.display == D_NONE) continue;
            const char *cs = atributo_m(c, "colspan"); int u, span = cs ? num100(cs, (int)tam_texto(cs), &u) / 100 : 1;
            if (span < 1) span = 1;
            if (col + span > MAX_COLS) span = MAX_COLS - col;
            int a = medir_max(c), b = medir_min(c);
            if (span == 1) { if (a > cmax[col]) cmax[col] = a; if (b > cmin[col]) cmin[col] = b; }
            col += span;
        }
        if (col > ncol) ncol = col;
    }
    if (!ncol) ncol = 1;
    int smax = 0, smin = 0;
    for (int i = 0; i < ncol; i++) { if (cmin[i] > cmax[i]) cmax[i] = cmin[i]; smax += cmax[i]; smin += cmin[i]; }
    int disp = cw - esp * (ncol + 1);
    int largura_def = res_med(&n->e.larg, -1, &n->e) >= 0 || n->e.larg.u == U_PCT;
    int w[MAX_COLS];
    if (smax <= disp) {
        int extra = largura_def ? disp - smax : 0;
        for (int i = 0; i < ncol; i++) w[i] = cmax[i] + (smax ? muldiv(extra, cmax[i], smax) : extra / ncol);
    } else if (smin >= disp) {
        for (int i = 0; i < ncol; i++) w[i] = cmin[i];
    } else {
        int d = smax - smin;
        for (int i = 0; i < ncol; i++) w[i] = cmin[i] + (d ? muldiv(disp - smin, (cmax[i] - cmin[i]), d) : 0);
    }
    int xs[MAX_COLS + 1];
    xs[0] = n->bl[3] + n->pl[3] + esp;
    for (int i = 0; i < ncol; i++) xs[i + 1] = xs[i] + w[i] + esp;
    int y = n->bl[0] + n->pl[0];
    int largura_tab = xs[ncol] - (n->bl[3] + n->pl[3]);
    if (legenda) {
        layout_no(legenda, largura_tab, largura_tab - legenda->ml - legenda->mr, -1);
        legenda->x = n->bl[3] + n->pl[3] + legenda->ml; legenda->y = y + legenda->mt;
        y += legenda->a + legenda->mt + legenda->mb;
    }
    y += esp;
    no_m *grupo_atual = NULL; int y_grupo = 0;
    for (int r = 0; r < nl; r++) {
        no_m *L = linhas[r];
        if (grupo_de[r] != grupo_atual) {
            if (grupo_atual) grupo_atual->a = y - y_grupo;
            grupo_atual = grupo_de[r];
            if (grupo_atual) { caixa_modelo(grupo_atual, cw); grupo_atual->x = 0; grupo_atual->y = y; grupo_atual->l = largura_tab; y_grupo = y; }
        }
        int oy = grupo_atual ? grupo_atual->y : 0;
        caixa_modelo(L, cw);
        L->x = 0; L->y = y - oy; L->l = largura_tab;
        int ha = 0;
        if (L->e.display != D_ROW) {
            layout_no(L, largura_tab, largura_tab - 2 * esp, -1);
            L->x = xs[0]; L->y = y - oy;
            ha = L->a;
        } else {
            int col = 0;
            int hmin = res_med(&L->e.alt, -1, &L->e);
            for (no_m *c = L->filho; c && col < ncol; c = c->prox) {
                if (c->texto || c->e.display == D_NONE) continue;
                const char *cs = atributo_m(c, "colspan"); int u, span = cs ? num100(cs, (int)tam_texto(cs), &u) / 100 : 1;
                if (span < 1) span = 1;
                if (col + span > ncol) span = ncol - col;
                int cwid = xs[col + span] - xs[col] - esp;
                layout_no(c, cwid, cwid, -1);
                c->x = xs[col]; c->y = 0;
                if (c->a > ha) ha = c->a;
                col += span;
            }
            if (hmin > ha) ha = hmin;
            for (no_m *c = L->filho; c; c = c->prox) {
                if (c->texto || c->e.display == D_NONE || c->e.display != D_CELL) continue;
                int dy = ha - c->a;
                int va = c->e.vert ? c->e.vert : 1;
                c->a = ha;
                if (va == 1) deslocar_conteudo(c, dy / 2);
                else if (va == 3) deslocar_conteudo(c, dy);
            }
        }
        L->a = ha;
        y += ha + esp;
    }
    if (grupo_atual) grupo_atual->a = y - y_grupo;
    n->l = largura_tab + n->bl[3] + n->pl[3] + n->bl[1] + n->pl[1];
    return y - (n->bl[0] + n->pl[0]);
}

/* ------------------------------------------------------------ nó */

static void layout_conteudo(no_m *n, int cw, int ch)
{
    estilo_t *e = &n->e;
    n->frags = n->frags_fim = NULL;
    n->colapso = 0;
    int usado = 0, h;
    int nl, na;
    if (tamanho_natural(n, &nl, &na)) {
        if (igual(n->tag, "img") && nl > 0) h = muldiv(na, cw, nl);
        else h = na;
        if (!igual(n->tag, "select") && !igual(n->tag, "input") && !igual(n->tag, "textarea") && !igual(n->tag, "video") && !igual(n->tag, "iframe")) {}
        n->a = h;
        return;
    }
    switch (e->display) {
    case D_FLEX: case D_IFLEX: h = flex(n, cw, ch); break;
    case D_GRID: h = grid(n, cw); break;
    case D_TABLE: h = tabela(n, cw); break;
    default: h = fluxo(n, cw, &usado); break;
    }
    n->a = h;
}

static int layout_no(no_m *n, int cw, int forca_l, int forca_a)
{
    estilo_t *e = &n->e;
    caixa_modelo(n, cw);
    int w;
    int ex = extra_h(n);
    if (forca_l >= 0) w = forca_l;
    else {
        int cwid = res_med(&e->larg, cw, e);
        if (cwid >= 0) w = e->caixa_borda ? cwid : cwid + ex;
        else if (eh_bloco(e->display) && e->display != D_TABLE && e->display != D_IBLOCK && !e->flutua && !fora_do_fluxo(n)) w = cw - n->ml - n->mr;
        else {
            int mx = medir_max(n) - n->ml - n->mr, mn = medir_min(n) - n->ml - n->mr, d = cw - n->ml - n->mr;
            w = mx < d ? mx : (mn > d ? mn : d);
            if (e->display == D_TABLE && (e->larg.u == U_PCT)) w = d;
        }
        int mw = res_med(&e->max_l, cw, e);
        if (mw >= 0) { if (!e->caixa_borda) mw += ex; if (w > mw) w = mw; }
        int nw = res_med(&e->min_l, cw, e);
        if (nw > 0) { if (!e->caixa_borda) nw += ex; if (w < nw) w = nw; }
    }
    if (w < ex) w = ex;
    n->l = w;
    int ccw = w - ex;
    int h = forca_a >= 0 ? forca_a : -1;
    if (h < 0) {
        int hh = res_med(&e->alt, -1, e);
        if (e->alt.u == U_PCT && n->pai && n->pai->e.alt.u == U_PX) hh = res_med(&e->alt, n->pai->e.alt.v, e);
        if (e->alt.u == U_PCT && (igual(n->tag, "html") || igual(n->tag, "body"))) hh = res_med(&e->alt, mo->vh, e);
        if (hh >= 0) h = e->caixa_borda ? hh : hh + extra_v(n);
    }
    int ch = h >= 0 ? h - extra_v(n) : -1;
    layout_conteudo(n, ccw, ch);
    int ah = n->a + extra_v(n);
    if (e->display == D_TABLE) w = n->l;
    if (h >= 0) ah = h;
    int mxh = res_med(&e->max_a, -1, e);
    if (mxh >= 0) { if (!e->caixa_borda) mxh += extra_v(n); if (ah > mxh) ah = mxh; }
    int mnh = res_med(&e->min_a, -1, e);
    if (e->min_a.u == U_VH || e->min_a.u == U_PX) { if (mnh > 0) { if (!e->caixa_borda) mnh += extra_v(n); if (ah < mnh) ah = mnh; } }
    n->a = ah;
    n->l = w;
    return ah;
}

/* ================================================================ achatar */

static void cmd_novo(cmd_t *c)
{
    if (!crescer_v(mo, (void **)&mo->cmds, &mo->cap_cmds, mo->n_cmds + 1, sizeof(cmd_t))) return;
    c->cx0 = mo->clip[0]; c->cy0 = mo->clip[1]; c->cx1 = mo->clip[2]; c->cy1 = mo->clip[3];
    mo->cmds[mo->n_cmds++] = *c;
}
static void regiao(int x, int y, int l, int a, int alvo)
{
    if (alvo < 0 || l <= 0 || a <= 0) return;
    if (!crescer_v(mo, (void **)&mo->regs, &mo->cap_regs, mo->n_regs + 1, sizeof(regiao_t))) return;
    regiao_t *r = &mo->regs[mo->n_regs++];
    r->x = x; r->y = y; r->l = l; r->a = a; r->alvo = alvo;
}

static void texto_cmd(int x, int y, int l, int a, const char *t, int nt, const estilo_t *e, uint32_t cor)
{
    cmd_t c; memset(&c, 0, sizeof c);
    c.tipo = C_TEXTO; c.x = x; c.y = y; c.l = l; c.a = a; c.t = t; c.nt = nt;
    c.tam = e->tam; c.cor = cor; c.peso = e->peso; c.italico = e->italico;
    c.sublinha = (uint8_t)(e->sublinha | (e->risca << 1));
    c.raio = e->maiusc;
    cmd_novo(&c);
}

static int alvo_form(no_m *n, int tipo)
{
    no_m *f = n->pai;
    while (f && !igual(f->tag, "form")) f = f->pai;
    if (!crescer_v(mo, (void **)&mo->alvos, &mo->cap_alvos, mo->n_alvos + 1, sizeof(motor_alvo_t))) return -1;
    motor_alvo_t *a = &mo->alvos[mo->n_alvos];
    memset(a, 0, sizeof *a);
    a->tipo = tipo;
    const char *ac = f ? atributo_m(f, "action") : NULL;
    char u[600];
    juntar(mo->url, ac && ac[0] ? ac : mo->url, u, sizeof u);
    a->href = m_dup(mo, u, (int)tam_texto(u));
    a->metodo = f ? atributo_m(f, "method") : NULL;
    const char *nm = atributo_m(n, "name");
    if (tipo == MA_BOTAO && f) {   /* o nome vem do primeiro campo de texto */
        nm = NULL;
        no_m *pilha[64]; int np = 0; no_m *q = f->filho;
        while ((q || np) && !nm) {
            if (!q) { q = pilha[--np]->prox; continue; }
            if (!q->texto && igual(q->tag, "input")) { const char *t = atributo_m(q, "type"); if (!t || igual(t, "text") || igual(t, "search")) nm = atributo_m(q, "name"); }
            if (!q->texto && igual(q->tag, "textarea")) nm = atributo_m(q, "name");
            if (!q->texto && q->filho && np < 64) { pilha[np++] = q; q = q->filho; continue; }
            q = q->prox;
        }
    }
    a->nome = nm ? nm : "q";
    /* escondidos */
    if (f) {
        char fx[600]; fx[0] = 0;
        no_m *pilha[64]; int np = 0; no_m *q = f->filho;
        while (q || np) {
            if (!q) { q = pilha[--np]->prox; continue; }
            if (!q->texto && igual(q->tag, "input")) {
                const char *t = atributo_m(q, "type"), *nn = atributo_m(q, "name"), *vv = atributo_m(q, "value");
                if (t && igual(t, "hidden") && nn && (int)(tam_texto(fx) + tam_texto(nn) + (vv ? tam_texto(vv) : 0)) < 560) {
                    if (fx[0]) junta_texto(fx, "&", sizeof fx);
                    junta_texto(fx, nn, sizeof fx); junta_texto(fx, "=", sizeof fx); if (vv) junta_texto(fx, vv, sizeof fx);
                }
            }
            if (!q->texto && q->filho && np < 64) { pilha[np++] = q; q = q->filho; continue; }
            q = q->prox;
        }
        a->fixos = m_dup(mo, fx, (int)tam_texto(fx));
    } else a->fixos = "";
    return mo->n_alvos++;
}


/* ================================================================ SVG
 * Desenha os <svg> embutidos: path (M L H V C S Q T A Z), rect, circle,
 * ellipse, polygon, polyline e line, com fill (e stroke simples em polyline/
 * line), grupos com fill herdado e transform translate/scale. Preenchimento
 * com regra não-zero e cobertura 4x16 por pixel. Ponto fixo: 1/64 px. */

#define SVG_F 64
/* a / b com a de 64 bits, sem __divdi3 */
static int64_t div64(int64_t a, int b)
{
    if (!b) return 0;
    int neg = (a < 0) ^ (b < 0);
    uint64_t ua = (uint64_t)(a < 0 ? -a : a), q = 0, r = 0;
    uint32_t ub = (uint32_t)(b < 0 ? -b : b);
    for (int i = 63; i >= 0; i--) { r = (r << 1) | ((ua >> i) & 1); if (r >= ub) { r -= ub; q |= (uint64_t)1 << i; } }
    return neg ? -(int64_t)q : (int64_t)q;
}
typedef struct { int x0, y0, x1, y1; } aresta_t;
static aresta_t *svg_ar; static int svg_n, svg_cap;
static int svg_mx[2], svg_tx, svg_ty;   /* escala x256 e translação (1/64 px) */

static void svg_aresta(int x0, int y0, int x1, int y1)
{
    if (y0 == y1) return;
    if (svg_n == svg_cap) {
        int nc = svg_cap ? svg_cap * 2 : 1024;
        aresta_t *q = m_novo(mo, nc * (int)sizeof(aresta_t));
        if (!q) return;
        if (svg_n) memcpy(q, svg_ar, (size_t)svg_n * sizeof(aresta_t));
        svg_ar = q; svg_cap = nc;
    }
    svg_ar[svg_n].x0 = x0; svg_ar[svg_n].y0 = y0; svg_ar[svg_n].x1 = x1; svg_ar[svg_n].y1 = y1;
    svg_n++;
}

/* número SVG em x256 */
static const char *svg_num(const char *s, int *v)
{
    while (*s == ' ' || *s == ',' || *s == '\n' || *s == '\t' || *s == '\r') s++;
    int neg = 0, i = 0, f = 0, d = 0, esc = 256;
    if (*s == '-' || *s == '+') { neg = *s == '-'; s++; }
    while (eh_dig(*s)) { if (i < 1000000) i = i * 10 + (*s - '0'); s++; d++; }
    if (*s == '.') { s++; while (eh_dig(*s)) { if (esc > 1) { esc /= 10; f += (*s - '0') * esc; } s++; d++; } }
    if (*s == 'e' || *s == 'E') { s++; int en = 0, eneg = 0; if (*s == '-' || *s == '+') { eneg = *s == '-'; s++; } while (eh_dig(*s)) en = en * 10 + (*s++ - '0'); int x = i * 256 + f * 256 / 256; for (int k = 0; k < en; k++) x = eneg ? x / 10 : x * 10; *v = neg ? -x : x; return s; }
    *v = d ? (neg ? -(i * 256 + f) : i * 256 + f) : 0;
    if (f && esc < 256) { int frac = f * 256 / 256; (void)frac; }
    return d ? s : NULL;
}

/* x256 do usuário -> 1/64 px do dispositivo */
static int svg_dx(int x) { return (int)(((int64_t)x * svg_mx[0]) >> 10) + svg_tx; }
static int svg_dy(int y) { return (int)(((int64_t)y * svg_mx[1]) >> 10) + svg_ty; }

static int px_ant, py_ant, mov_x, mov_y;
static void svg_linha(int x, int y) { svg_aresta(svg_dx(px_ant), svg_dy(py_ant), svg_dx(x), svg_dy(y)); px_ant = x; py_ant = y; }

static void svg_cubica(int x1, int y1, int x2, int y2, int x3, int y3)
{
    int x0 = px_ant, y0 = py_ant;
    int n = 12;
    for (int i = 1; i <= n; i++) {
        int t = i * 256 / n, u = 256 - t;
        int64_t a = (int64_t)u * u * u, b = 3LL * u * u * t, c = 3LL * u * t * t, d = (int64_t)t * t * t;
        int x = (int)((a * x0 + b * x1 + c * x2 + d * x3) >> 24);
        int y = (int)((a * y0 + b * y1 + c * y2 + d * y3) >> 24);
        svg_linha(x, y);
    }
}
static void svg_quad(int x1, int y1, int x2, int y2)
{
    int x0 = px_ant, y0 = py_ant;
    int n = 10;
    for (int i = 1; i <= n; i++) {
        int t = i * 256 / n, u = 256 - t;
        int x = (int)(((int64_t)u * u * x0 + 2LL * u * t * x1 + (int64_t)t * t * x2) >> 16);
        int y = (int)(((int64_t)u * u * y0 + 2LL * u * t * y1 + (int64_t)t * t * y2) >> 16);
        svg_linha(x, y);
    }
}

/* arco: aproxima com uma curva que passa pelo meio do arco (sem trigonometria) */
static void svg_arco(int rx, int ry, int grande, int varre, int x, int y)
{
    int x0 = px_ant, y0 = py_ant;
    int mx = (x0 + x) / 2, my = (y0 + y) / 2;
    int dx = x - x0, dy = y - y0;
    /* normal à corda, com tamanho de "flecha" ~ r - sqrt(r^2 - (c/2)^2) */
    int64_t c2 = ((int64_t)dx * dx + (int64_t)dy * dy) / 4;
    int r = rx > ry ? rx : ry;
    int64_t r2 = (int64_t)r * r;
    int64_t h2 = r2 > c2 ? r2 - c2 : 0;
    int h = 0; while ((int64_t)(h + 1) * (h + 1) <= h2 && h < 4000000) h += (h2 > (int64_t)(h + 64) * (h + 64)) ? 64 : 1;
    int flecha = grande ? r + h : r - h;
    int len = 0; int64_t l2 = 4 * c2; while ((int64_t)(len + 1) * (len + 1) <= l2 && len < 8000000) len += (l2 > (int64_t)(len + 64) * (len + 64)) ? 64 : 1;
    if (!len) return;
    int nx = -dy, ny = dx;
    int sinal = (grande ^ varre) ? 1 : -1;
    int cx = mx + muldiv(nx, flecha, len) * sinal, cy = my + muldiv(ny, flecha, len) * sinal;
    /* curva quadrática cujo ponto do meio é (cx, cy): controle = 2*meio - (ini+fim)/2 */
    svg_quad(2 * cx - mx, 2 * cy - my, x, y);
}

static void svg_path(const char *d)
{
    char cmd = 0;
    int qx = 0, qy = 0;   /* último controle (para S/T) */
    char ult = 0;
    const char *p = d;
    while (p && *p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t' || *p == '\r') p++;
        if (!*p) break;
        if (eh_letra(*p)) { cmd = *p++; }
        else if (!cmd) break;
        int rel = cmd >= 'a';
        char C = (char)(rel ? cmd - 32 : cmd);
        int bx = rel ? px_ant : 0, by = rel ? py_ant : 0;
        int v[7];
        const char *q = p;
        switch (C) {
        case 'M':
            if (!(q = svg_num(q, &v[0])) || !(q = svg_num(q, &v[1]))) return;
            px_ant = mov_x = bx + v[0]; py_ant = mov_y = by + v[1];
            cmd = rel ? 'l' : 'L';
            break;
        case 'L':
            if (!(q = svg_num(q, &v[0])) || !(q = svg_num(q, &v[1]))) return;
            svg_linha(bx + v[0], by + v[1]);
            break;
        case 'H': if (!(q = svg_num(q, &v[0]))) return; svg_linha((rel ? px_ant : 0) + v[0], py_ant); break;
        case 'V': if (!(q = svg_num(q, &v[0]))) return; svg_linha(px_ant, (rel ? py_ant : 0) + v[0]); break;
        case 'C':
            for (int i = 0; i < 6; i++) if (!(q = svg_num(q, &v[i]))) return;
            qx = bx + v[2]; qy = by + v[3];
            svg_cubica(bx + v[0], by + v[1], bx + v[2], by + v[3], bx + v[4], by + v[5]);
            break;
        case 'S': {
            for (int i = 0; i < 4; i++) if (!(q = svg_num(q, &v[i]))) return;
            int c1x = (ult == 'C' || ult == 'S') ? 2 * px_ant - qx : px_ant, c1y = (ult == 'C' || ult == 'S') ? 2 * py_ant - qy : py_ant;
            qx = bx + v[0]; qy = by + v[1];
            svg_cubica(c1x, c1y, bx + v[0], by + v[1], bx + v[2], by + v[3]);
            break;
        }
        case 'Q':
            for (int i = 0; i < 4; i++) if (!(q = svg_num(q, &v[i]))) return;
            qx = bx + v[0]; qy = by + v[1];
            svg_quad(bx + v[0], by + v[1], bx + v[2], by + v[3]);
            break;
        case 'T': {
            for (int i = 0; i < 2; i++) if (!(q = svg_num(q, &v[i]))) return;
            int cx = (ult == 'Q' || ult == 'T') ? 2 * px_ant - qx : px_ant, cy = (ult == 'Q' || ult == 'T') ? 2 * py_ant - qy : py_ant;
            qx = cx; qy = cy;
            svg_quad(cx, cy, bx + v[0], by + v[1]);
            break;
        }
        case 'A': {
            for (int i = 0; i < 7; i++) {
                if (i == 3 || i == 4) {   /* bandeiras podem vir coladas: "0 01 5 5" ou "011" */
                    while (*q == ' ' || *q == ',') q++;
                    if (*q != '0' && *q != '1') return;
                    v[i] = *q++ - '0';
                    continue;
                }
                if (!(q = svg_num(q, &v[i]))) return;
            }
            svg_arco(v[0] < 0 ? -v[0] : v[0], v[1] < 0 ? -v[1] : v[1], v[3], v[4], bx + v[5], by + v[6]);
            break;
        }
        case 'Z': svg_linha(mov_x, mov_y); cmd = 0; q = p; break;
        default: return;
        }
        ult = C;
        if (q == p && C != 'Z') return;
        p = q;
    }
}

static void svg_fechar_sub(void) { if (px_ant != mov_x || py_ant != mov_y) svg_linha(mov_x, mov_y); }

/* rasteriza as arestas em px (l x a) com a cor (regra não-zero) */
static void svg_preencher(uint32_t *px, int l, int a, uint32_t cor)
{
    if (!svg_n) return;
    int *cob = m_novo(mo, (l + 2) * (int)sizeof(int));
    int *xs = m_novo(mo, svg_n * 2 * (int)sizeof(int));
    if (!cob || !xs) return;
    for (int y = 0; y < a; y++) {
        memset(cob, 0, (size_t)(l + 2) * sizeof(int));
        bool algo = false;
        for (int sub = 0; sub < 4; sub++) {
            int sy = y * SVG_F + sub * (SVG_F / 4) + SVG_F / 8;
            int k = 0;
            for (int i = 0; i < svg_n; i++) {
                aresta_t *e = &svg_ar[i];
                int ya = e->y0, yb = e->y1, dir = 1;
                if (ya > yb) { int t = ya; ya = yb; yb = t; dir = -1; }
                if (sy < ya || sy >= yb) continue;
                int x = e->x0 + muldiv(sy - e->y0, e->x1 - e->x0, e->y1 - e->y0);
                xs[k++] = x * 2 + (dir > 0 ? 1 : 0);   /* bit 0 = direção */
            }
            if (k < 2) continue;
            for (int i = 1; i < k; i++) { int v = xs[i], j = i - 1; while (j >= 0 && (xs[j] >> 1) > (v >> 1)) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
            int w = 0;
            for (int i = 0; i + 1 < k; i++) {
                w += (xs[i] & 1) ? 1 : -1;
                if (!w) continue;
                int xa = (xs[i] >> 1), xb = (xs[i + 1] >> 1);
                if (xa < 0) xa = 0;
                if (xb > l * SVG_F) xb = l * SVG_F;
                if (xb <= xa) continue;
                algo = true;
                int pa = xa / SVG_F, pb = xb / SVG_F;
                if (pa == pb) { cob[pa] += xb - xa; continue; }
                cob[pa] += SVG_F - xa % SVG_F;
                for (int q = pa + 1; q < pb; q++) cob[q] += SVG_F;
                if (pb < l) cob[pb] += xb % SVG_F;
            }
        }
        if (!algo) continue;
        uint32_t *lin = px + y * l;
        uint32_t ca = cor >> 24;
        for (int x = 0; x < l; x++) {
            if (!cob[x]) continue;
            int al = cob[x] * 255 / (4 * SVG_F);
            if (al > 255) al = 255;
            al = (int)(al * ca / 255);
            uint32_t ant = lin[x], aa = ant >> 24;
            /* "sobre" com alfa (fundo transparente começa com 0) */
            uint32_t na = (uint32_t)al + aa * (255 - (uint32_t)al) / 255;
            if (!na) continue;
            uint32_t r = (((cor >> 16) & 255) * (uint32_t)al + ((ant >> 16) & 255) * aa * (255 - (uint32_t)al) / 255) / na;
            uint32_t g = (((cor >> 8) & 255) * (uint32_t)al + ((ant >> 8) & 255) * aa * (255 - (uint32_t)al) / 255) / na;
            uint32_t b = ((cor & 255) * (uint32_t)al + (ant & 255) * aa * (255 - (uint32_t)al) / 255) / na;
            lin[x] = (na << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

static bool svg_attr(const char *tag, int n, const char *nome, char *v, int max)
{
    int ln = (int)tam_texto(nome);
    for (int i = 0; i + ln < n; i++) {
        if ((i == 0 || tag[i - 1] == ' ' || tag[i - 1] == '\n' || tag[i - 1] == '\t') && igual_n(tag + i, nome, ln) && tag[i + ln] == '=') {
            int k = i + ln + 1;
            char q = tag[k];
            if (q != '"' && q != '\'') return false;
            k++;
            int j = 0;
            while (k < n && tag[k] != q && j < max - 1) v[j++] = tag[k++];
            v[j] = 0;
            return true;
        }
    }
    /* style="fill:..." */
    if (igual(nome, "fill") || igual(nome, "stroke")) {
        char st[256];
        if (svg_attr(tag, n, "style", st, sizeof st)) {
            const char *f = st;
            for (; *f; f++) if (igual_n(f, nome, ln) && f[ln] == ':') {
                f += ln + 1;
                while (*f == ' ') f++;
                int j = 0;
                while (*f && *f != ';' && j < max - 1) v[j++] = *f++;
                v[j] = 0;
                return true;
            }
        }
    }
    return false;
}

static int svg_val(const char *tag, int n, const char *nome, int padrao)
{
    char v[32];
    if (!svg_attr(tag, n, nome, v, sizeof v)) return padrao;
    int x;
    return svg_num(v, &x) ? x : padrao;
}

/* Desenha o SVG do nó num quadro l x a (pixels com alfa). */
static uint32_t *svg_desenhar(no_m *n, int l, int a)
{
    if (l <= 0 || a <= 0 || l > 2000 || a > 2000) return NULL;
    uint32_t *px = aloca((size_t)l * (size_t)a * 4);
    if (!px) return NULL;
    memset(px, 0, (size_t)l * (size_t)a * 4);
    /* viewBox */
    int vb[4] = { 0, 0, 0, 0 };
    const char *vbs = atributo_m(n, "viewbox");
    if (!vbs) vbs = atributo_m(n, "viewBox");
    if (vbs) { const char *q = vbs; for (int i = 0; i < 4 && q; i++) q = svg_num(q, &vb[i]); }
    if (vb[2] <= 0 || vb[3] <= 0) { vb[0] = vb[1] = 0; vb[2] = l * 256; vb[3] = a * 256; }
    /* escala uniforme (xMidYMid meet): x256 -> 1/64 px, guardada x1024 */
    int64_t sx = div64((int64_t)l * SVG_F * 1024 * 256, vb[2]), sy = div64((int64_t)a * SVG_F * 1024 * 256, vb[3]);
    int64_t es = sx < sy ? sx : sy;
    int base_tx = (int)((l * SVG_F - (es * vb[2] >> 18)) / 2) - (int)((es * vb[0]) >> 18);
    int base_ty = (int)((a * SVG_F - (es * vb[3] >> 18)) / 2) - (int)((es * vb[1]) >> 18);
    const char *s = n->t; int ns = n->nt;
    uint32_t cor_atual = 0xFF000000u | n->e.cor;
    /* pilha de grupos: fill e translação */
    uint32_t fill_g[16]; int tx_g[16], ty_g[16], sc_g[16], ng = 0, pula = 0;
    fill_g[0] = cor_atual; tx_g[0] = 0; ty_g[0] = 0; sc_g[0] = 1024;
    { char v[48]; const char *sf = NULL;
      for (atr_t *at = n->atr; at; at = at->prox) if (igual(at->nome, "fill")) sf = at->valor;
      if (sf) { uint32_t c; if (igual(sf, "none")) fill_g[0] = 0; else if (ler_cor(sf, (int)tam_texto(sf), &c, n->e.cor)) fill_g[0] = c; }
      (void)v; }
    for (int i = 0; i < ns;) {
        if (s[i] != '<') { i++; continue; }
        int k = i + 1;
        bool fecha = s[k] == '/';
        if (fecha) k++;
        int t0 = k;
        while (k < ns && (eh_letra(s[k]) || eh_dig(s[k]) || s[k] == '-' || s[k] == ':')) k++;
        int tn = k - t0;
        const char *tag = s + t0;
        int fim = k;
        while (fim < ns && s[fim] != '>') { if (s[fim] == '"' || s[fim] == '\'') { char q = s[fim++]; while (fim < ns && s[fim] != q) fim++; } fim++; }
        bool auto_f = fim > 0 && s[fim - 1] == '/';
        const char *at = s + k; int na = fim - k;
        i = fim + 1;
        if (tn == 0 || s[t0] == '!' ) continue;
        bool eh_g = tn == 1 && tag[0] == 'g';
        bool esconde = (tn == 4 && igual_n(tag, "defs", 4)) || (tn == 8 && igual_n(tag, "clipPath", 8)) || (tn == 4 && igual_n(tag, "mask", 4)) ||
                       (tn == 6 && igual_n(tag, "symbol", 6)) || (tn == 5 && igual_n(tag, "title", 5)) || (tn == 5 && igual_n(tag, "style", 5)) ||
                       (tn == 14 && igual_n(tag, "linearGradient", 14)) || (tn == 14 && igual_n(tag, "radialGradient", 14)) || (tn == 7 && igual_n(tag, "pattern", 7)) || (tn == 6 && igual_n(tag, "filter", 6));
        if (fecha) {
            if (esconde && pula) pula--;
            else if (eh_g && ng) ng--;
            continue;
        }
        if (esconde) { if (!auto_f) pula++; continue; }
        if (pula) continue;
        uint32_t fill = fill_g[ng];
        char v[1024];
        if (svg_attr(at, na, "fill", v, sizeof v)) {
            uint32_t c;
            if (igual(v, "none")) fill = 0;
            else if (comeca_com(v, "url(")) fill = cor_atual;
            else if (ler_cor(v, (int)tam_texto(v), &c, n->e.cor)) fill = c;
        }
        int op = svg_val(at, na, "opacity", 256);
        int fop = svg_val(at, na, "fill-opacity", 256);
        if (op < 256 || fop < 256) { uint32_t al = (fill >> 24) * (uint32_t)(op < 256 ? op : 256) / 256 * (uint32_t)(fop < 256 ? fop : 256) / 256; fill = (al << 24) | (fill & 0xFFFFFF); }
        int gtx = tx_g[ng], gty = ty_g[ng], gsc = sc_g[ng];
        if (svg_attr(at, na, "transform", v, sizeof v)) {
            const char *tt = v;
            for (; *tt; tt++) {
                if (igual_n(tt, "translate(", 10)) { int a2 = 0, b2 = 0; const char *q = svg_num(tt + 10, &a2); if (q) svg_num(q, &b2); gtx += a2; gty += b2; }
                if (igual_n(tt, "scale(", 6)) { int a2 = 256; svg_num(tt + 6, &a2); gsc = gsc * a2 / 256; }
                if (igual_n(tt, "matrix(", 7)) { int m6[6] = { 256, 0, 0, 256, 0, 0 }; const char *q = tt + 7; for (int z = 0; z < 6 && q; z++) q = svg_num(q, &m6[z]); gsc = gsc * m6[0] / 256; gtx += m6[4]; gty += m6[5]; }
            }
        }
        if (eh_g) {
            if (!auto_f && ng < 15) { ng++; fill_g[ng] = fill; tx_g[ng] = gtx; ty_g[ng] = gty; sc_g[ng] = gsc; }
            continue;
        }
        if (!(fill >> 24)) {
            /* sem preenchimento: linhas com stroke viram contorno fino */
            if (!svg_attr(at, na, "stroke", v, sizeof v) || igual(v, "none")) continue;
            uint32_t c;
            if (!ler_cor(v, (int)tam_texto(v), &c, n->e.cor)) c = cor_atual;
            fill = c;
            if (!((tn == 4 && igual_n(tag, "line", 4)) || (tn == 8 && igual_n(tag, "polyline", 8)) || (tn == 4 && igual_n(tag, "path", 4)) ||
                  (tn == 6 && igual_n(tag, "circle", 6)) || (tn == 4 && igual_n(tag, "rect", 4)))) continue;
            /* contorno: desenha o caminho como linhas de 1,5 px (aproximado como preenchimento de faixa) */
        }
        svg_mx[0] = svg_mx[1] = (int)((es * gsc) >> 18);
        svg_tx = base_tx + (int)((es * gtx) >> 18); svg_ty = base_ty + (int)((es * gty) >> 18);
        svg_n = 0;
        px_ant = py_ant = mov_x = mov_y = 0;
        if (tn == 4 && igual_n(tag, "path", 4)) {
            if (svg_attr(at, na, "d", v, sizeof v)) {
                /* subcaminhos: fecha cada um */
                svg_path(v);
                svg_fechar_sub();
            }
        } else if (tn == 4 && igual_n(tag, "rect", 4)) {
            int x = svg_val(at, na, "x", 0), y = svg_val(at, na, "y", 0), w = svg_val(at, na, "width", 0), h = svg_val(at, na, "height", 0);
            px_ant = mov_x = x; py_ant = mov_y = y;
            svg_linha(x + w, y); svg_linha(x + w, y + h); svg_linha(x, y + h); svg_linha(x, y);
        } else if ((tn == 6 && igual_n(tag, "circle", 6)) || (tn == 7 && igual_n(tag, "ellipse", 7))) {
            int cx = svg_val(at, na, "cx", 0), cy = svg_val(at, na, "cy", 0);
            int rx = tn == 6 ? svg_val(at, na, "r", 0) : svg_val(at, na, "rx", 0), ry = tn == 6 ? rx : svg_val(at, na, "ry", 0);
            /* 4 cúbicas (k = 0,5523) */
            int kx = rx * 141 / 256, ky = ry * 141 / 256;
            px_ant = mov_x = cx + rx; py_ant = mov_y = cy;
            svg_cubica(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
            svg_cubica(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
            svg_cubica(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
            svg_cubica(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
        } else if ((tn == 7 && igual_n(tag, "polygon", 7)) || (tn == 8 && igual_n(tag, "polyline", 8))) {
            if (svg_attr(at, na, "points", v, sizeof v)) {
                const char *q = v; int x, y, pri = 1;
                while (q && (q = svg_num(q, &x)) && (q = svg_num(q, &y))) {
                    if (pri) { px_ant = mov_x = x; py_ant = mov_y = y; pri = 0; } else svg_linha(x, y);
                }
                svg_fechar_sub();
            }
        } else if (tn == 4 && igual_n(tag, "line", 4)) {
            int x1 = svg_val(at, na, "x1", 0), y1 = svg_val(at, na, "y1", 0), x2 = svg_val(at, na, "x2", 0), y2 = svg_val(at, na, "y2", 0);
            int w = svg_val(at, na, "stroke-width", 256) / 2 + 64;
            px_ant = mov_x = x1 - w; py_ant = mov_y = y1 - w;
            svg_linha(x2 + w, y2 - w); svg_linha(x2 + w, y2 + w); svg_linha(x1 - w, y1 + w); svg_linha(x1 - w, y1 - w);
        } else continue;
        svg_preencher(px, l, a, fill);
    }
    return px;
}

typedef struct { no_m *n; int sx, sy; int clip[4]; int cb[4]; } adiado_t;
static adiado_t adiados[512]; static int n_adiados;

static void achatar(no_m *n, int ox, int oy, int cb_x, int cb_y, int cb_l, int cb_a);

static void posicionar_abs(no_m *c, int cb_x, int cb_y, int cb_l, int cb_a, int static_x, int static_y)
{
    estilo_t *e = &c->e;
    int top = res_med(&e->pos[0], cb_a, e), dir = res_med(&e->pos[1], cb_l, e), bai = res_med(&e->pos[2], cb_a, e), esq = res_med(&e->pos[3], cb_l, e);
    if (e->pos[0].u == U_AUTO) top = -INF;
    if (e->pos[1].u == U_AUTO) dir = -INF;
    if (e->pos[2].u == U_AUTO) bai = -INF;
    if (e->pos[3].u == U_AUTO) esq = -INF;
    caixa_modelo(c, cb_l);
    int forca_l = -1;
    if (e->larg.u == U_AUTO && esq > -INF && dir > -INF) { forca_l = cb_l - esq - dir - c->ml - c->mr; if (forca_l < 0) forca_l = 0; }
    int forca_a = -1;
    if (e->alt.u == U_AUTO && top > -INF && bai > -INF) { forca_a = cb_a - top - bai - c->mt - c->mb; if (forca_a < 0) forca_a = -1; }
    layout_no(c, cb_l, forca_l, forca_a);
    int x, y;
    if (esq > -INF) x = cb_x + esq + c->ml;
    else if (dir > -INF) x = cb_x + cb_l - dir - c->l - c->mr;
    else x = static_x + c->ml;
    if (e->marg[3].u == U_AUTO && e->marg[1].u == U_AUTO && esq > -INF && dir > -INF) x = cb_x + esq + (cb_l - esq - dir - c->l) / 2;
    if (top > -INF) y = cb_y + top + c->mt;
    else if (bai > -INF) y = cb_y + cb_a - bai - c->a - c->mb;
    else y = static_y + c->mt;
    c->x = x; c->y = y;   /* absolutos: coordenadas da página */
}

static void achatar_filhos(no_m *n, int ox, int oy, int cb_x, int cb_y, int cb_l, int cb_a)
{
    for (no_m *c = n->filho; c; c = c->prox) {
        if (c->texto || c->e.display == D_NONE) continue;
        if (fora_do_fluxo(c)) {
            if (n_adiados < 512) {
                adiado_t *a = &adiados[n_adiados++];
                a->n = c;
                a->sx = ox + c->x; a->sy = oy + c->y;   /* posição estática */
                memcpy(a->clip, mo->clip, sizeof a->clip);
                if (c->e.position == P_FIXED) { a->clip[0] = -INF; a->clip[1] = -INF; a->clip[2] = INF; a->clip[3] = INF; }
                a->cb[0] = cb_x; a->cb[1] = cb_y; a->cb[2] = cb_l; a->cb[3] = cb_a;
            }
            continue;
        }
        achatar(c, ox, oy, cb_x, cb_y, cb_l, cb_a);
    }
}

static void desenhar_caixa(no_m *n, int x, int y)
{
    estilo_t *e = &n->e;
    if (e->visivel || e->opaco0) return;
    if (e->fundo && !e->mascara) {
        cmd_t c; memset(&c, 0, sizeof c);
        c.tipo = C_RET; c.x = x; c.y = y; c.l = n->l; c.a = n->a; c.cor = e->fundo; c.raio = e->raio > n->a / 2 ? (int16_t)(n->a / 2) : e->raio;
        cmd_novo(&c);
    }
    if (n->bg_img >= 0 && mo->imgs[n->bg_img].px && n->l > 0 && n->a > 0) {
        imagem_m *im = &mo->imgs[n->bg_img];
        cmd_t c; memset(&c, 0, sizeof c);
        c.tipo = C_IMG; c.img = n->bg_img;
        int l = im->l, a = im->a;
        if (e->bg_tam == 1) { l = n->l; a = n->a; }
        else if (e->bg_tam == 2 || l > n->l || a > n->a) {
            if (l * n->a > a * n->l) { a = a * n->l / (l ? l : 1); l = n->l; } else { l = l * n->a / (a ? a : 1); a = n->a; }
        }
        c.x = x + (n->l - l) / 2; c.y = y + (n->a - a) / 2; c.l = l; c.a = a; c.raio = e->raio;
        int salvo[4]; memcpy(salvo, mo->clip, sizeof salvo);
        if (x > mo->clip[0]) mo->clip[0] = x;
        if (y > mo->clip[1]) mo->clip[1] = y;
        if (x + n->l < mo->clip[2]) mo->clip[2] = x + n->l;
        if (y + n->a < mo->clip[3]) mo->clip[3] = y + n->a;
        cmd_novo(&c);
        memcpy(mo->clip, salvo, sizeof salvo);
    }
    if (n->bl[0] || n->bl[1] || n->bl[2] || n->bl[3]) {
        cmd_t c; memset(&c, 0, sizeof c);
        c.tipo = C_BORDA; c.x = x; c.y = y; c.l = n->l; c.a = n->a;
        for (int i = 0; i < 4; i++) { c.bw[i] = (int16_t)n->bl[i]; c.bc[i] = e->cor_borda[i] ? e->cor_borda[i] : 0xFF000000u | e->cor; }
        c.raio = e->raio > n->a / 2 ? (int16_t)(n->a / 2) : e->raio;
        cmd_novo(&c);
    }
}

static void controle(no_m *n, int x, int y)
{
    estilo_t *e = &n->e;
    int cx = x + n->bl[3] + n->pl[3], cy = y + n->bl[0] + n->pl[0];
    int cl = n->l - extra_h(n), ca = n->a - extra_v(n);
    if (igual(n->tag, "img")) {
        if (n->img >= 0 && mo->imgs[n->img].px) {
            cmd_t c; memset(&c, 0, sizeof c);
            c.tipo = C_IMG; c.x = cx; c.y = cy; c.l = cl; c.a = ca; c.img = n->img; c.raio = e->raio;
            cmd_novo(&c);
        } else {
            const char *alt = atributo_m(n, "alt");
            if (alt && alt[0] && cl > 8) texto_cmd(cx, cy, cl, ca, alt, (int)tam_texto(alt) < cl / larg_letra(e) ? (int)tam_texto(alt) : cl / larg_letra(e), e, 0x6A737C);
        }
        regiao(x, y, n->l, n->a, n->alvo);
        return;
    }
    if (igual(n->tag, "svg")) {
        if (!n->t || cl <= 0 || ca <= 0) return;
        if (n->img < 0 || mo->imgs[n->img].l != cl || mo->imgs[n->img].a != ca) {
            if (n->img < 0) { if (mo->n_imgs >= 96) return; n->img = mo->n_imgs++; mo->imgs[n->img].url = (char *)"svg"; mo->imgs[n->img].tentou = 1; mo->imgs[n->img].alfa = 1; }
            imagem_m *im = &mo->imgs[n->img];
            if (im->px) libera(im->px);
            im->px = svg_desenhar(n, cl, ca);
            im->l = cl; im->a = ca;
        }
        cmd_t c; memset(&c, 0, sizeof c);
        c.tipo = C_IMG; c.x = cx; c.y = cy; c.l = cl; c.a = ca; c.img = n->img;
        cmd_novo(&c);
        regiao(x, y, n->l, n->a, n->alvo);
        return;
    }
    if (igual(n->tag, "video") || igual(n->tag, "iframe") || igual(n->tag, "embed") || igual(n->tag, "object") || igual(n->tag, "canvas")) {
        cmd_t c; memset(&c, 0, sizeof c);
        c.tipo = C_RET; c.x = cx; c.y = cy; c.l = cl; c.a = ca; c.cor = igual(n->tag, "canvas") ? 0 : 0xFF202428;
        if (c.cor) cmd_novo(&c);
        if (!igual(n->tag, "canvas")) {
            const char *rot = igual(n->tag, "video") ? "[ video ]" : "[ quadro ]";
            texto_cmd(cx + 8, cy + 8, cl, 16, rot, (int)tam_texto(rot), e, 0xC4CBD2);
        }
        regiao(x, y, n->l, n->a, n->alvo);
        return;
    }
    if (igual(n->tag, "input") || igual(n->tag, "select") || igual(n->tag, "textarea")) {
        const char *t = atributo_m(n, "type");
        const char *txt = NULL;
        int tipo_alvo = MA_CAMPO;
        if (igual(n->tag, "input")) {
            if (t && (igual(t, "checkbox") || igual(t, "radio"))) {
                if (atributo_m(n, "checked")) { cmd_t c; memset(&c, 0, sizeof c); c.tipo = C_RET; c.x = cx + 3; c.y = cy + 3; c.l = cl - 6; c.a = ca - 6; c.cor = 0xFF0B7A3E; c.raio = igual(t, "radio") ? 4 : 0; cmd_novo(&c); }
                return;
            }
            if (t && (igual(t, "submit") || igual(t, "button") || igual(t, "reset") || igual(t, "image"))) { txt = atributo_m(n, "value"); if (!txt) txt = "Enviar"; tipo_alvo = MA_BOTAO; }
            else { txt = atributo_m(n, "value"); if (!txt || !txt[0]) txt = atributo_m(n, "placeholder"); }
        } else if (igual(n->tag, "select")) {
            no_m *o = NULL;
            for (no_m *q = n->filho; q; q = q->prox) if (!q->texto && igual(q->tag, "option")) { if (!o || atributo_m(q, "selected")) o = q; }
            if (o && o->filho && o->filho->texto) txt = o->filho->t;
            tipo_alvo = 0;
        } else if (n->filho && n->filho->texto) txt = n->filho->t;
        if (txt) {
            int max = cl / larg_letra(e), nt = (int)tam_texto(txt);
            while (nt && eh_esp(*txt)) { txt++; nt--; }
            if (nt > max) nt = max;
            int tx = tipo_alvo == MA_BOTAO ? cx + (cl - nt * larg_letra(e)) / 2 : cx;
            uint32_t cor = (igual(n->tag, "input") && !atributo_m(n, "value") && tipo_alvo == MA_CAMPO) ? 0x8A939C : e->cor;
            for (const char *p = txt; p < txt + nt; p++) if (*p == '\n') { nt = (int)(p - txt); break; }
            texto_cmd(tx, cy + (ca - e->tam) / 2, nt * larg_letra(e), e->tam, txt, nt, e, cor);
        }
        if (tipo_alvo && !atributo_m(n, "disabled")) regiao(x, y, n->l, n->a, alvo_form(n, tipo_alvo));
        return;
    }
}

static void achatar(no_m *n, int ox, int oy, int cb_x, int cb_y, int cb_l, int cb_a)
{
    estilo_t *e = &n->e;
    if (e->display == D_NONE || e->opaco0) return;
    int x = ox + n->x, y = oy + n->y;
    if (e->position == P_RELATIVE || e->position == P_STICKY) {
        int t = res_med(&e->pos[0], cb_a, e), l = res_med(&e->pos[3], cb_l, e), b = res_med(&e->pos[2], cb_a, e), r = res_med(&e->pos[1], cb_l, e);
        if (t >= 0 || e->pos[0].u == U_PX) y += e->pos[0].u == U_AUTO ? 0 : t; else if (b > 0) y -= b;
        if (l >= 0 || e->pos[3].u == U_PX) x += e->pos[3].u == U_AUTO ? 0 : l; else if (r > 0) x -= r;
        if (e->pos[0].u == U_PX && e->pos[0].v < 0) y += 0;
    }
    if (e->display == D_CONTENTS) { achatar_filhos(n, x, y, cb_x, cb_y, cb_l, cb_a); return; }
    bool tem_caixa = !(e->display == D_INLINE && !eh_atomico(n));
    int salvo[4];
    memcpy(salvo, mo->clip, sizeof salvo);
    if (tem_caixa) {
        /* botão (ou outro átomo) cujo conteúdo é um link: a caixa inteira é clicável */
        if (n->alvo < 0 && eh_atomico(n))
            for (no_m *c = n->filho; c; c = c->prox) if (!c->texto && igual(c->tag, "a")) { int al = alvo_link(c); if (al >= 0) { n->alvo = al; break; } }
        desenhar_caixa(n, x, y);
        if (n->alvo >= 0 && (eh_atomico(n) || eh_bloco(e->display))) regiao(x, y, n->l, n->a, n->alvo);
        controle(n, x, y);
        /* marcador de lista */
        if (e->display == D_LIST && e->lista && !e->visivel) {
            int lh = altura_linha(e);
            int my = y + n->bl[0] + n->pl[0] + (lh - e->tam) / 2;
            if (n->frags) my = y + n->frags->y;
            if (e->lista == 1) {
                cmd_t c; memset(&c, 0, sizeof c);
                int r = e->tam / 5 + 1;
                c.tipo = C_RET; c.x = x - e->tam + 2; c.y = my + e->tam / 2 - r / 2 - 1; c.l = r; c.a = r; c.cor = 0xFF000000u | e->cor; c.raio = (int16_t)(r / 2);
                cmd_novo(&c);
            } else {
                int k = 1;
                for (no_m *q = n->ant; q; q = q->ant) if (!q->texto && q->e.display == D_LIST) k++;
                char num[12]; formatar_texto(num, sizeof num, "%d.", k);
                int nn = (int)tam_texto(num);
                char *p = m_dup(mo, num, nn);
                texto_cmd(x - nn * larg_letra(e) - 4, my, nn * larg_letra(e), e->tam, p, nn, e, e->cor);
            }
        }
        if (e->corta) {
            int c0 = x + n->bl[3], c1 = y + n->bl[0], c2 = x + n->l - n->bl[1], c3 = y + n->a - n->bl[2];
            if (c0 > mo->clip[0]) mo->clip[0] = c0;
            if (c1 > mo->clip[1]) mo->clip[1] = c1;
            if (c2 < mo->clip[2]) mo->clip[2] = c2;
            if (c3 < mo->clip[3]) mo->clip[3] = c3;
        }
    }
    /* linhas de texto */
    for (frag_t *f = n->frags; f; f = f->prox) {
        no_m *tn = f->estilo_de;
        const estilo_t *te = tn->texto ? &tn->pai->e : &tn->e;
        uint32_t cor = te->cor;
        texto_cmd(x + f->x, y + f->y, f->l, f->a, f->t, f->nt, te, cor);
        if (f->alvo >= 0) regiao(x + f->x, y + f->y - 2, f->l, f->a + 4, f->alvo);
    }
    int ncb_x = cb_x, ncb_y = cb_y, ncb_l = cb_l, ncb_a = cb_a;
    if (e->position != P_STATIC) { ncb_x = x + n->bl[3]; ncb_y = y + n->bl[0]; ncb_l = n->l - n->bl[1] - n->bl[3]; ncb_a = n->a - n->bl[0] - n->bl[2]; }
    achatar_filhos(n, x, y, ncb_x, ncb_y, ncb_l, ncb_a);
    memcpy(mo->clip, salvo, sizeof salvo);
}

static void achatar_adiados(int ini)
{
    for (int i = ini; i < n_adiados; i++) {
        adiado_t a = adiados[i];
        no_m *c = a.n;
        int cb_x = a.cb[0], cb_y = a.cb[1], cb_l = a.cb[2], cb_a = a.cb[3];
        if (c->e.position == P_FIXED) { cb_x = 0; cb_y = 0; cb_l = mo->vw; cb_a = mo->vh; }
        posicionar_abs(c, cb_x, cb_y, cb_l, cb_a, a.sx, a.sy);
        int salvo[4];
        memcpy(salvo, mo->clip, sizeof salvo);
        memcpy(mo->clip, a.clip, sizeof salvo);
        int ax = c->x, ay = c->y;
        c->x = 0; c->y = 0;
        bool era = c->e.position == P_ABSOLUTE || c->e.position == P_FIXED;
        uint8_t pos = c->e.position;
        medida_t pos_salva[4];
        memcpy(pos_salva, c->e.pos, sizeof pos_salva);
        c->e.position = P_RELATIVE;     /* para achatar não tratar de novo; vira bloco de contenção */
        c->e.pos[0].u = c->e.pos[1].u = c->e.pos[2].u = c->e.pos[3].u = U_AUTO;
        achatar(c, ax, ay, cb_x, cb_y, cb_l, cb_a);
        c->e.position = pos;
        memcpy(c->e.pos, pos_salva, sizeof pos_salva);
        (void)era;
        c->x = ax; c->y = ay;
        memcpy(mo->clip, salvo, sizeof salvo);
    }
}

/* ================================================================ API */

static char *folhas_txt[64]; static int folhas_n[64]; static int n_folhas;

static int largura_regras = -1;

motor_t *motor_novo(const motor_ganchos_t *g)
{
    /* estado estático vinha da arena do motor anterior: zera */
    itens = NULL; n_itens = cap_itens = 0;
    svg_ar = NULL; svg_n = svg_cap = 0;
    largura_regras = -1; n_folhas = 0; n_adiados = 0;
    motor_t *m = aloca(sizeof(motor_t));
    if (!m) return NULL;
    memset(m, 0, sizeof *m);
    if (g) m->g = *g;
    return m;
}

void motor_fim(motor_t *m)
{
    if (!m) return;
    for (int i = 0; i < m->n_imgs; i++) if (m->imgs[i].px) libera(m->imgs[i].px);
    while (m->blocos) { bloco_m *b = m->blocos; m->blocos = b->prox; libera(b); }
    libera(m);
    n_folhas = 0;
}

static no_m *proximo_no(no_m *n)
{
    if (n->filho) return n->filho;
    while (n && !n->prox) n = n->pai;
    return n ? n->prox : NULL;
}

static void guardar_folha(motor_t *m, const char *t, int n)
{
    if (n_folhas >= 64 || n <= 0) return;
    folhas_txt[n_folhas] = m_dup(m, t, n);
    folhas_n[n_folhas++] = n;
}

void motor_ler(motor_t *m, const char *html, int n, const char *url)
{
    mo = m;
    n_folhas = 0;
    copia_texto(m->url, url ? url : "", sizeof m->url);
    ler_html_m(m, html, n);
    char u[600];
    for (no_m *q = m->raiz; q; q = proximo_no(q)) {
        if (q->texto) continue;
        if (igual(q->tag, "base")) { const char *h = atributo_m(q, "href"); if (h) { juntar(m->url, h, u, sizeof u); copia_texto(m->url, u, sizeof m->url); } }
        if (igual(q->tag, "title") && q->filho && q->filho->texto && !m->titulo[0]) {
            int k = 0;
            for (int i = 0; i < q->filho->nt && k < 126; i++) { char c = q->filho->t[i]; if (eh_esp(c)) { if (k && m->titulo[k - 1] != ' ') m->titulo[k++] = ' '; } else m->titulo[k++] = c; }
            while (k && m->titulo[k - 1] == ' ') k--;
            m->titulo[k] = 0;
        }
        if (igual(q->tag, "style") && q->filho && q->filho->texto) {
            const char *md = atributo_m(q, "media");
            if (!md || media_ok(m, md, (int)tam_texto(md)) || 1) guardar_folha(m, q->filho->t, q->filho->nt);
        }
        if (igual(q->tag, "link")) {
            const char *rel = atributo_m(q, "rel"), *h = atributo_m(q, "href");
            const char *lm = atributo_m(q, "media");
            bool so_impressao = lm && (strstr_m(lm, "print") || strstr_m(lm, "speech")) && !strstr_m(lm, "screen") && !strstr_m(lm, "all");
            if (rel && h && strstr_m(rel, "stylesheet") && !strstr_m(rel, "alternate") && !so_impressao && m->g.baixar && m->folhas < 12) {
                juntar(m->url, h, u, sizeof u);
                unsigned tam = 0;
                uint8_t *d = m->g.baixar(u, &tam);
                m->folhas++;
                if (d) { guardar_folha(m, (const char *)d, (int)tam); libera(d); }
            }
        }
        if (igual(q->tag, "img") || (igual(q->tag, "input") && atributo_m(q, "src"))) {
            const char *src = atributo_m(q, "src");
            if (!src || !src[0] || comeca_com(src, "data:")) { const char *ds = atributo_m(q, "data-src"); if (ds) src = ds; }
            if (!src || !src[0] || comeca_com(src, "data:")) { const char *ss = atributo_m(q, "srcset"); if (ss && ss[0]) { int k = 0; while (ss[k] && !eh_esp(ss[k]) && ss[k] != ',') k++; src = m_dup(m, ss, k); } }
            if (!src || !src[0] || comeca_com(src, "data:")) continue;
            juntar(m->url, src, u, sizeof u);
            int idx = -1;
            for (int i = 0; i < m->n_imgs; i++) if (igual(m->imgs[i].url, u)) { idx = i; break; }
            if (idx < 0 && m->n_imgs < 96) { idx = m->n_imgs++; m->imgs[idx].url = m_dup(m, u, (int)tam_texto(u)); }
            q->img = idx;
        }
    }
}

int motor_imagens(motor_t *m, int max)
{
    int k = 0;
    for (int i = 0; i < m->n_imgs && k < max; i++) {
        imagem_m *im = &m->imgs[i];
        if (im->tentou) continue;
        im->tentou = 1;
        k++;
        if (!m->g.baixar || !m->g.imagem) continue;
        unsigned n = 0;
        uint8_t *d = m->g.baixar(im->url, &n);
        if (!d) continue;
        uint32_t *px; int l, a;
        if (m->g.imagem(d, n, &px, &l, &a)) { im->px = px; im->l = l; im->a = a; }
        libera(d);
    }
    return k;
}

static void limpar_layout(no_m *n)
{
    for (no_m *q = n; q; q = proximo_no(q)) {
        q->max_c = q->min_c = -1;
        q->colapso = 0;
        q->alvo = -1;
        q->x = q->y = q->l = q->a = 0;
        q->frags = q->frags_fim = NULL;
    }
}


static int layout_interno(motor_t *m, int largura, int altura_tela);
static motor_t *arg_m; static int arg_l, arg_a, arg_r;
static void layout_na_pilha(void) { arg_r = layout_interno(arg_m, arg_l, arg_a); }

/* O layout é recursivo e a pilha dos programas é pequena: roda numa própria. */
int motor_layout(motor_t *m, int largura, int altura_tela)
{
#ifdef MOTOR_HOST
    return layout_interno(m, largura, altura_tela);
#else
    static char *pilha;
    enum { PILHA_MOTOR = 2 * 1024 * 1024 };
    if (!pilha) pilha = aloca(PILHA_MOTOR);
    if (!pilha) return layout_interno(m, largura, altura_tela);
    arg_m = m; arg_l = largura; arg_a = altura_tela;
    char *topo = pilha + PILHA_MOTOR - 16;
    void (*f)(void) = layout_na_pilha;
    __asm__ volatile("movl %%esp, %%ebx\n\tmovl %0, %%esp\n\tcall *%1\n\tmovl %%ebx, %%esp"
                     :: "r"(topo), "r"(f) : "ebx", "eax", "ecx", "edx", "memory", "cc");
    return arg_r;
#endif
}

static int layout_interno(motor_t *m, int largura, int altura_tela)
{
    mo = m;
    if (!m->raiz) return 0;
    m->vw = largura; m->vh = altura_tela;
    if (largura_regras != largura || !m->n_regras) {
        m->n_regras = 0; m->n_univ = 0;
        if (m->cap_baldes) memset(m->baldes, 0, sizeof(balde_t) * (size_t)m->cap_baldes);
        ler_folha(m, folha_padrao, (int)sizeof folha_padrao - 1, 0);
        ua_fim = m->n_regras;
        for (int i = 0; i < n_folhas; i++) {
            int antes = m->n_regras;
            ler_folha(m, folhas_txt[i], folhas_n[i], 0);
            if (largura_regras < 0) registrar("cipo: css %d regras\n", m->n_regras - antes);
        }
        largura_regras = largura;
        estilizar_tudo(m, m->raiz);
        normalizar(m, m->raiz);
        estilizar_tudo(m, m->raiz);
    }
    limpar_layout(m->raiz);
    m->n_alvos = 0;
    n_itens = 0;
    m->raiz->e.display = D_BLOCK;
    layout_no(m->raiz, largura, largura, -1);
    /* fundo da página: o do html, ou o do body */
    m->fundo = 0xFFFFFF;
    for (no_m *c = m->raiz->filho; c; c = c->prox) {
        if (c->texto || !igual(c->tag, "html")) continue;
        if (c->e.fundo) m->fundo = c->e.fundo & 0xFFFFFF;
        else for (no_m *b = c->filho; b; b = b->prox) if (!b->texto && igual(b->tag, "body") && b->e.fundo) m->fundo = b->e.fundo & 0xFFFFFF;
    }
    m->n_cmds = 0; m->n_regs = 0; n_adiados = 0;
    m->clip[0] = -INF; m->clip[1] = -INF; m->clip[2] = INF; m->clip[3] = INF;
    achatar(m->raiz, 0, 0, 0, 0, largura, altura_tela);
    achatar_adiados(0);
    int h = m->raiz->a;
    for (int i = 0; i < m->n_cmds; i++) {
        cmd_t *c = &m->cmds[i];
        int fim = c->y + c->a;
        if (c->cy1 < fim) fim = c->cy1;
        if (fim > h && fim < INF / 2) h = fim;
    }
    m->altura = h;
    return h;
}

/* ---------------------------------------------------------------- pintura */

static uint32_t misturar(uint32_t fundo, uint32_t cor)
{
    uint32_t a = cor >> 24;
    if (a >= 250) return cor & 0xFFFFFF;
    uint32_t r = ((cor >> 16 & 255) * a + (fundo >> 16 & 255) * (255 - a)) / 255;
    uint32_t g = ((cor >> 8 & 255) * a + (fundo >> 8 & 255) * (255 - a)) / 255;
    uint32_t b = ((cor & 255) * a + (fundo & 255) * (255 - a)) / 255;
    return (r << 16) | (g << 8) | b;
}

typedef struct { uint32_t *px; int larg, passo, y0, a; int c0, c1, c2, c3; } alvo_px_t;

static inline void ponto(alvo_px_t *t, int x, int y, uint32_t cor)
{
    if (x < t->c0 || x >= t->c2 || y < t->c1 || y >= t->c3) return;
    t->px[(y - t->y0) * t->passo + x] = (cor >> 24) >= 250 ? cor & 0xFFFFFF : misturar(t->px[(y - t->y0) * t->passo + x], cor);
}

/* recuo de um canto arredondado na linha dy (0 = topo) para raio r */
/* recuo horizontal de um canto arredondado na linha d (0 = borda) para raio r */
static int recuo(int d, int r)
{
    int k = r - d, s = 0;
    while ((s + 1) * (s + 1) + k * k <= r * r) s++;
    return r - s;
}

static void ret(alvo_px_t *t, int x, int y, int l, int a, uint32_t cor, int raio)
{
    if (raio * 2 > l) raio = l / 2;
    if (raio * 2 > a) raio = a / 2;
    int ya = y < t->c1 ? t->c1 : y, yb = y + a > t->c3 ? t->c3 : y + a;
    for (int yy = ya; yy < yb; yy++) {
        int r = 0;
        if (raio > 0) { int dy = yy - y, db = y + a - 1 - yy, d = dy < db ? dy : db; if (d < raio) r = recuo(d, raio); }
        int xa = x + r, xb = x + l - r;
        if (xa < t->c0) xa = t->c0;
        if (xb > t->c2) xb = t->c2;
        uint32_t *lin = t->px + (yy - t->y0) * t->passo;
        if ((cor >> 24) >= 250) { uint32_t c = cor & 0xFFFFFF; for (int xx = xa; xx < xb; xx++) lin[xx] = c; }
        else for (int xx = xa; xx < xb; xx++) lin[xx] = misturar(lin[xx], cor);
    }
}

static char maiuscula(char c)
{
    uint8_t u = (uint8_t)c;
    if (u >= 'a' && u <= 'z') return (char)(u - 32);
    if (u >= 0xE0 && u <= 0xFE && u != 0xF7) return (char)(u - 32);
    return c;
}

static void texto_px(alvo_px_t *t, cmd_t *c)
{
    int tam = c->tam < 6 ? 6 : c->tam;
    int lw = (tam + 1) / 2, lh = tam;
    int x = c->x;
    bool negrito = c->peso && tam >= 10;
    for (int i = 0; i < c->nt; i++, x += lw) {
        uint8_t ch = (uint8_t)c->t[i];
        if (c->raio == 1) ch = (uint8_t)maiuscula((char)ch);
        else if (c->raio == 2 && ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch + 32);
        else if (c->raio == 3 && (i == 0 || c->t[i - 1] == ' ')) ch = (uint8_t)maiuscula((char)ch);
        if (ch == ' ' || ch == 0xA0) continue;
        if (x + lw < t->c0 || x >= t->c2) continue;
        const unsigned char *g = fonte_sagui[ch];
        if (lw < 8 || lh < 16) {   /* redução: cobertura do retângulo de origem vira transparência */
            for (int yy = 0; yy < lh; yy++) {
                int py = c->y + yy;
                if (py < t->c1 || py >= t->c3) continue;
                int sy0 = yy * 16 / lh, sy1 = (yy + 1) * 16 / lh;
                if (sy1 <= sy0) sy1 = sy0 + 1;
                int inc = c->italico ? (lh - yy) / 4 - lh / 8 : 0;
                for (int xx = 0; xx < lw; xx++) {
                    int sx0 = xx * 8 / lw, sx1 = (xx + 1) * 8 / lw;
                    if (sx1 <= sx0) sx1 = sx0 + 1;
                    int on = 0, tot = 0;
                    for (int sy = sy0; sy < sy1; sy++) for (int sx = sx0; sx < sx1; sx++) { tot++; if (g[sy] & (0x80 >> sx)) on++; }
                    if (!on) continue;
                    int al = on * 255 / tot;
                    if (negrito) al = al * 3 / 2 > 255 ? 255 : al * 3 / 2;
                    if (al < 40) continue;
                    ponto(t, x + xx + inc, py, ((uint32_t)al << 24) | c->cor);
                }
            }
            continue;
        }
        for (int yy = 0; yy < lh; yy++) {
            int py = c->y + yy;
            if (py < t->c1 || py >= t->c3) continue;
            uint8_t lin = g[yy * 16 / lh];
            if (!lin) continue;
            int inc = c->italico ? (lh - yy) / 4 - lh / 8 : 0;
            for (int xx = 0; xx < lw; xx++) {
                int bx = xx * 8 / lw;
                if (lin & (0x80 >> bx)) {
                    ponto(t, x + xx + inc, py, 0xFF000000u | c->cor);
                    if (negrito) ponto(t, x + xx + inc + 1, py, 0xFF000000u | c->cor);
                }
            }
        }
    }
    if (c->sublinha & 1) ret(t, c->x, c->y + tam * 7 / 8, c->nt * lw, tam >= 24 ? 2 : 1, 0xFF000000u | c->cor, 0);
    if (c->sublinha & 2) ret(t, c->x, c->y + tam / 2, c->nt * lw, 1, 0xFF000000u | c->cor, 0);
}

void motor_pintar(motor_t *m, uint32_t *px, int larg, int passo, int y, int a)
{
    alvo_px_t t = { px, larg, passo, y, a, 0, y, larg, y + a };
    for (int yy = 0; yy < a; yy++) { uint32_t *l = px + yy * passo; for (int xx = 0; xx < larg; xx++) l[xx] = m->fundo; }
    for (int i = 0; i < m->n_cmds; i++) {
        cmd_t *c = &m->cmds[i];
        if (c->y >= y + a || c->y + c->a + 4 <= y) continue;
        t.c0 = c->cx0 > 0 ? c->cx0 : 0; t.c1 = c->cy0 > y ? c->cy0 : y;
        t.c2 = c->cx1 < larg ? c->cx1 : larg; t.c3 = c->cy1 < y + a ? c->cy1 : y + a;
        if (t.c0 >= t.c2 || t.c1 >= t.c3) continue;
        switch (c->tipo) {
        case C_RET: ret(&t, c->x, c->y, c->l, c->a, c->cor, c->raio); break;
        case C_BORDA:
            if (c->bw[0]) ret(&t, c->x + (c->raio ? c->raio : 0), c->y, c->l - 2 * (c->raio ? c->raio : 0), c->bw[0], c->bc[0], 0);
            if (c->bw[2]) ret(&t, c->x + (c->raio ? c->raio : 0), c->y + c->a - c->bw[2], c->l - 2 * (c->raio ? c->raio : 0), c->bw[2], c->bc[2], 0);
            if (c->bw[3]) ret(&t, c->x, c->y + (c->raio ? c->raio : 0), c->bw[3], c->a - 2 * (c->raio ? c->raio : 0), c->bc[3], 0);
            if (c->bw[1]) ret(&t, c->x + c->l - c->bw[1], c->y + (c->raio ? c->raio : 0), c->bw[1], c->a - 2 * (c->raio ? c->raio : 0), c->bc[1], 0);
            break;
        case C_TEXTO: texto_px(&t, c); break;
        case C_IMG: {
            imagem_m *im = &m->imgs[c->img];
            if (!im->px || c->l <= 0 || c->a <= 0) break;
            int ya = c->y < t.c1 ? t.c1 : c->y, yb = c->y + c->a > t.c3 ? t.c3 : c->y + c->a;
            int xa = c->x < t.c0 ? t.c0 : c->x, xb = c->x + c->l > t.c2 ? t.c2 : c->x + c->l;
            for (int yy = ya; yy < yb; yy++) {
                const uint32_t *src = im->px + ((yy - c->y) * im->a / c->a) * im->l;
                uint32_t *d = px + (yy - y) * passo;
                for (int xx = xa; xx < xb; xx++) {
                    uint32_t p = src[(xx - c->x) * im->l / c->l];
                    if (!im->alfa) { d[xx] = p & 0xFFFFFF; continue; }   /* JPEG/PNG já vêm compostos sobre branco */
                    uint32_t al = p >> 24;
                    if (al) d[xx] = al >= 250 ? p & 0xFFFFFF : misturar(d[xx], p);
                }
            }
            break;
        }
        default: break;
        }
    }
}

const motor_alvo_t *motor_alvo(motor_t *m, int x, int y)
{
    for (int i = m->n_regs - 1; i >= 0; i--) {
        regiao_t *r = &m->regs[i];
        if (x >= r->x && x < r->x + r->l && y >= r->y && y < r->y + r->a && r->alvo < m->n_alvos) return &m->alvos[r->alvo];
    }
    return NULL;
}

const char *motor_titulo(motor_t *m) { return m->titulo; }
uint32_t motor_fundo(motor_t *m) { return m->fundo; }
void motor_numeros(motor_t *m, int *nos, int *cmds, int *imgs)
{
    *nos = m->qtd_nos; *cmds = m->n_cmds;
    int k = 0;
    for (int i = 0; i < m->n_imgs; i++) if (m->imgs[i].px) k++;
    *imgs = k;
}

#ifdef MOTOR_HOST
static void dump(no_m *n, int prof, int ox, int oy, int max_prof)
{
    for (; n; n = n->prox) {
        if (n->texto || n->e.display == D_NONE) continue;
        int x = ox + n->x, y = oy + n->y;
        if (fora_do_fluxo(n)) { x = n->x; y = n->y; }
        if (n->l || n->a || n->filho)
            fprintf(stderr, "%*s%s.%.30s d%d p%d f%d [%d,%d %dx%d]\n", prof * 2, "", n->tag, n->classe ? n->classe : "", n->e.display, n->e.position, n->e.flutua, x, y, n->l, n->a);
        if (getenv("FRAGS") && n->frags) for (frag_t *f = n->frags; f; f = f->prox) fprintf(stderr, "%*s  ~ [%d,%d] %.*s\n", prof * 2, "", x + f->x, y + f->y, f->nt > 20 ? 20 : f->nt, f->t);
        if (prof < max_prof) dump(n->filho, prof + 1, x, y, max_prof);
    }
}
void motor_dump(motor_t *m, int max_prof) { dump(m->raiz, 0, 0, 0, max_prof); }
#endif
#ifdef MOTOR_HOST
void motor_dump_cmds(motor_t *m, int y0, int y1)
{
    for (int i = 0; i < m->n_cmds; i++) {
        cmd_t *c = &m->cmds[i];
        if (c->y + c->a < y0 || c->y > y1) continue;
        fprintf(stderr, "cmd %d t%d [%d,%d %dx%d] cor %08x clip[%d,%d,%d,%d] %.*s\n", i, c->tipo, c->x, c->y, c->l, c->a, c->cor, c->cx0, c->cy0, c->cx1, c->cy1, c->tipo == C_TEXTO ? (c->nt > 30 ? 30 : c->nt) : 0, c->t ? c->t : "");
    }
}
#endif

int motor_links(motor_t *m)
{
    int k = 0;
    for (int i = 0; i < m->n_alvos; i++) if (m->alvos[i].tipo == MA_LINK) k++;
    return k;
}

void motor_sem_esconder(motor_t *m, bool sim) { m->sem_esconder = sim; largura_regras = -1; m->n_regras = 0; }
