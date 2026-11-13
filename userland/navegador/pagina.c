/* pagina.c - layout e desenho de uma página (H62).
 *
 * O layout percorre a árvore do HTML e produz uma lista de "peças" com
 * posição fixa na página: pedaços de texto, imagens, linhas, fundos, bordas
 * de tabela e campos de formulário. Desenhar é só pintar as peças que caem
 * na parte visível; rolar é mudar o deslocamento.
 *
 * Modelo (bem menor que o de um navegador completo, de propósito):
 *   - blocos (p, div, h1-h6, listas, citações, pre, tabelas...) começam numa
 *     linha nova, com margem em cima e embaixo (margens seguidas não somam:
 *     vale a maior);
 *   - dentro de um bloco, o texto corre em linhas: as palavras vão sendo
 *     postas da esquerda para a direita e, quando a próxima não cabe, a linha
 *     acaba. Espaços em sequência valem um só (menos em <pre>);
 *   - imagens ficam na linha como uma palavra grande; a linha cresce para
 *     caber a mais alta e tudo se alinha pela base;
 *   - tabelas: a largura de cada coluna sai da largura "preferida" (o texto
 *     sem quebrar) e da "mínima" (a maior palavra) das células; se a soma
 *     das preferidas couber, é ela; se não, divide-se o espaço entre o mínimo
 *     e o preferido; cada célula é um bloco com o seu próprio fluxo;
 *   - a fonte é a do sistema (8x16); h1 é desenhado em tamanho dobrado.
 *
 * CSS (tudo o que é suportado; o resto é ignorado):
 *   - no atributo style e em regras de <style> com seletores simples
 *     (tag, .classe, #id, tag.classe, listas com vírgula):
 *       display: none | visibility: hidden | color | background(-color) |
 *       text-align: center/right | font-weight: bold | text-decoration:
 *       underline/line-through | font-style (ignorado: não há itálico);
 *   - atributos antigos: bgcolor, color (em <font>), align=center, hidden;
 *   - cores por nome (as 16 básicas e mais algumas), #rgb, #rrggbb, rgb().
 * Há uma folha de estilo própria do navegador que esconde o que só faz
 * sentido com JavaScript ou com o CSS do site (ver FOLHA_PADRAO). */
#include "navegador.h"

#define MAX_REGRAS ((int)(sizeof ((pagina_t *)0)->regras / sizeof ((pagina_t *)0)->regras[0]))

/* ------------------------------------------------------------ cores */

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)(c | 32);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static bool igual_sem_caixa(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
        if (!x) return true;
    }
    return true;
}

/* Lê uma cor; devolve false se não entender. */
static bool ler_cor(const char *s, int n, uint32_t *cor)
{
    static const struct { const char *nome; uint32_t cor; } nomes[] = {
        { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 },
        { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "gray", 0x808080 }, { "grey", 0x808080 },
        { "silver", 0xC0C0C0 }, { "maroon", 0x800000 }, { "purple", 0x800080 }, { "fuchsia", 0xFF00FF },
        { "lime", 0x00FF00 }, { "olive", 0x808000 }, { "navy", 0x000080 }, { "teal", 0x008080 },
        { "aqua", 0x00FFFF }, { "orange", 0xFFA500 }, { "brown", 0xA52A2A }, { "pink", 0xFFC0CB },
        { "darkgreen", 0x006400 }, { "darkred", 0x8B0000 }, { "darkblue", 0x00008B }, { "lightgray", 0xD3D3D3 },
        { "lightgrey", 0xD3D3D3 }, { "darkgray", 0xA9A9A9 }, { "whitesmoke", 0xF5F5F5 }, { "gold", 0xFFD700 },
        { "lightblue", 0xADD8E6 }, { "lightyellow", 0xFFFFE0 }, { "beige", 0xF5F5DC }, { "ivory", 0xFFFFF0 },
    };
    while (n > 0 && eh_espaco(*s)) { s++; n--; }
    while (n > 0 && eh_espaco(s[n - 1])) n--;
    if (n > 0 && s[0] == '#') {
        int v[6], k = n - 1;
        if (k != 3 && k != 6) return false;
        for (int i = 0; i < k; i++) if ((v[i] = hex(s[1 + i])) < 0) return false;
        if (k == 3) *cor = (uint32_t)((v[0] * 17) << 16 | (v[1] * 17) << 8 | v[2] * 17);
        else *cor = (uint32_t)((v[0] * 16 + v[1]) << 16 | (v[2] * 16 + v[3]) << 8 | (v[4] * 16 + v[5]));
        return true;
    }
    if (n > 4 && igual_sem_caixa(s, "rgb", 3)) {
        int c[3] = { 0, 0, 0 }, k = 0;
        for (int i = 3; i < n && k < 3; i++) {
            if (s[i] >= '0' && s[i] <= '9') {
                int v = 0;
                while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
                c[k++] = v > 255 ? 255 : v;
            }
        }
        if (k < 3) return false;
        *cor = (uint32_t)(c[0] << 16 | c[1] << 8 | c[2]);
        return true;
    }
    for (unsigned i = 0; i < sizeof nomes / sizeof nomes[0]; i++)
        if ((int)tam_texto(nomes[i].nome) == n && igual_sem_caixa(s, nomes[i].nome, n)) { *cor = nomes[i].cor; return true; }
    return false;
}

/* ------------------------------------------------------------ estilo */

typedef struct {
    uint32_t cor;
    uint32_t fundo;            /* COR_NENHUMA = transparente */
    uint8_t  marcas;           /* PECA_NEGRITO etc. */
    uint8_t  alinhar;          /* 0 esquerda, 1 centro, 2 direita */
    bool     pre;
    bool     escondido;
    int16_t  link;
    int16_t  form;             /* formulário em que está (índice), -1 nenhum */
    uint8_t  escala;           /* 2 em h1 */
} estilo_t;

#define COR_NENHUMA 0xFF000000u
#define COR_LINK    0x1A4FB0u

/* Aplica declarações CSS ("a: b; c: d") ao estilo. */
static void aplicar_css(estilo_t *e, const char *s, int n)
{
    int i = 0;
    while (i < n) {
        while (i < n && (eh_espaco(s[i]) || s[i] == ';')) i++;
        int ini_p = i;
        while (i < n && s[i] != ':' && s[i] != ';') i++;
        int fim_p = i;
        while (fim_p > ini_p && eh_espaco(s[fim_p - 1])) fim_p--;
        if (i >= n || s[i] != ':') continue;
        i++;
        while (i < n && eh_espaco(s[i])) i++;
        int ini_v = i;
        while (i < n && s[i] != ';') i++;
        int fim_v = i;
        while (fim_v > ini_v && eh_espaco(s[fim_v - 1])) fim_v--;
        const char *p = s + ini_p, *v = s + ini_v;
        int np = fim_p - ini_p, nv = fim_v - ini_v;
        /* Tira o "!important". */
        for (int k = 0; k < nv; k++) if (v[k] == '!') { nv = k; while (nv > 0 && eh_espaco(v[nv - 1])) nv--; break; }
        #define PROP(x) (np == (int)sizeof(x) - 1 && igual_sem_caixa(p, x, np))
        #define VAL(x) (nv >= (int)sizeof(x) - 1 && igual_sem_caixa(v, x, (int)sizeof(x) - 1))
        if (PROP("display") && VAL("none")) e->escondido = true;
        else if (PROP("visibility") && (VAL("hidden") || VAL("collapse"))) e->escondido = true;
        else if (PROP("color")) ler_cor(v, nv, &e->cor);
        else if (PROP("background-color") || PROP("background")) {
            uint32_t c;
            if (ler_cor(v, nv, &c)) e->fundo = c;
            else if (nv > 0) {          /* "background: #fff url(...)" : a primeira palavra */
                int k = 0;
                while (k < nv && !eh_espaco(v[k])) k++;
                if (ler_cor(v, k, &c)) e->fundo = c;
            }
        } else if (PROP("text-align")) e->alinhar = VAL("center") ? 1 : VAL("right") ? 2 : 0;
        else if (PROP("font-weight")) {
            if (VAL("bold") || VAL("bolder") || VAL("6") || VAL("7") || VAL("8") || VAL("9")) e->marcas |= PECA_NEGRITO;
            else if (VAL("normal") || VAL("4") || VAL("lighter")) e->marcas &= (uint8_t)~PECA_NEGRITO;
        } else if (PROP("text-decoration") || PROP("text-decoration-line")) {
            if (VAL("none")) e->marcas &= (uint8_t)~(PECA_SUBLINHADO | PECA_RISCADO);
            else if (VAL("underline")) e->marcas |= PECA_SUBLINHADO;
            else if (VAL("line-through")) e->marcas |= PECA_RISCADO;
        }
        #undef PROP
        #undef VAL
    }
}

/* Regras de folha de estilo: seletor simples -> declarações. */
/* Uma regra: tag (0 = qualquer), classe e id (NULL = qualquer) e o texto
 * das declarações. Fica em pagina_t.regras. */
typedef __typeof__(((pagina_t *)0)->regras[0]) regra_t;

/* O que o navegador esconde por conta própria: partes de páginas que só
 * funcionam com JavaScript ou que o CSS dos sites esconde no papel. */
static const char FOLHA_PADRAO[] =
    ".mw-jump-link, .mw-editsection, .noprint, .navbox, .vertical-navbox, .mw-empty-elt, .catlinks, "
    ".vector-main-menu-landmark, .vector-user-links, .vector-appearance-landmark, .vector-page-tools-landmark, "
    ".vector-pinnable-header, .vector-pinned-container, .vector-main-menu-container, .vector-page-toolbar, .vector-column-start, "
    ".vector-sticky-header, .vector-dropdown, .vector-menu, .mw-portlet, .vector-page-titlebar-toc, "
    ".vector-toc, .vector-settings, #siteSub, .mw-indicators, .mw-footer, #p-lang-btn, .mw-authority-control, "
    ".sistersitebox, .metadata, .mw-hidden-catlinks, .printfooter, .mw-cite-backlink, .reference-accessdate, "
    ".hatnote-icon, .mw-ui-icon, .skin-theme-clientpref-night, #mw-head, #mw-panel, .vector-sticky-header, "
    ".mw-references-columns:empty, .mwe-popups, .client-js-only, .nomobile, .vector-body-before-content, "
    ".mw-logo, .mw-file-description:empty, .ext-discussiontools-init-section-bar, .mw-kartographer-maplink, "
    "template, svg, math, iframe, video, audio, canvas, object, embed, select { display: none }"
    "sup, sub { color: #445566 }"
    "th { font-weight: bold; text-align: center }"
    "caption { text-align: center; font-weight: bold }"
    "center { text-align: center }"
    "mark { background: yellow }"
    "pre, code, tt, kbd, samp { background: #f0f0ee }"
    "del, s, strike { text-decoration: line-through }"
    "ins, u { text-decoration: underline }";

static int ler_folha(pagina_t *p, documento_t *doc, const char *s, int n)
{
    int qtd = 0;
    int i = 0;
    while (i < n && p->nregras < MAX_REGRAS) {
        /* Pula comentários e @regras (blocos inteiros). */
        while (i < n && eh_espaco(s[i])) i++;
        if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i += 2;
            continue;
        }
        int ini = i;
        while (i < n && s[i] != '{') i++;
        int fim_sel = i;
        if (i >= n) break;
        int nivel = 1, ini_d = ++i;
        while (i < n && nivel > 0) { if (s[i] == '{') nivel++; else if (s[i] == '}') nivel--; i++; }
        int fim_d = i - 1;
        if (s[ini] == '@') continue;                     /* @media etc.: ignorado inteiro */
        /* Cada seletor da lista. */
        for (int a = ini; a < fim_sel;) {
            int b = a;
            while (b < fim_sel && s[b] != ',') b++;
            int x = a, y = b;
            while (x < y && eh_espaco(s[x])) x++;
            while (y > x && eh_espaco(s[y - 1])) y--;
            a = b + 1;
            bool simples = y > x;
            for (int k = x; k < y; k++)
                if (eh_espaco(s[k]) || s[k] == '>' || s[k] == '+' || s[k] == '~' || s[k] == '[' || s[k] == '*') simples = false;
            if (!simples || p->nregras >= MAX_REGRAS) continue;
            regra_t *r = &p->regras[p->nregras];
            memset(r, 0, sizeof *r);
            int k = x;
            char nome[16];
            int t = 0;
            while (k < y && s[k] != '.' && s[k] != '#' && s[k] != ':') { if (t < 15) nome[t++] = (char)(s[k] | 32); k++; }
            nome[t] = 0;
            if (t) {
                int tag = 0;
                for (int q = 1; q < TAG_QTD; q++) if (compara_texto(html_nome_tag(q), nome) == 0) tag = q;
                if (!tag) continue;
                r->tag = (uint8_t)tag;
            }
            bool ok = true;
            while (k < y && ok) {
                char tipo = s[k++];
                int c0 = k;
                while (k < y && s[k] != '.' && s[k] != '#' && s[k] != ':') k++;
                char *v = arena_aloca(doc, (uint32_t)(k - c0 + 1));
                if (!v) return qtd;
                memcpy(v, s + c0, (size_t)(k - c0));
                v[k - c0] = 0;
                if (tipo == '.' && !r->classe) r->classe = v;
                else if (tipo == '#' && !r->id) r->id = v;
                else ok = false;                          /* pseudo-classes (:hover...): a regra fica de fora */
            }
            if (!ok) continue;
            r->decl = s + ini_d;
            r->ndecl = fim_d - ini_d;
            p->nregras++;
            qtd++;
        }
    }
    return qtd;
}

static bool tem_classe(const char *lista, const char *c)
{
    size_t t = tam_texto(c);
    for (const char *p = lista; *p;) {
        while (*p && eh_espaco(*p)) p++;
        const char *ini = p;
        while (*p && !eh_espaco(*p)) p++;
        if ((size_t)(p - ini) == t && compara_texto_n(ini, c, t) == 0) return true;
    }
    return false;
}

static void estilo_do_elemento(pagina_t *p, const no_t *x, estilo_t *e)
{
    switch (x->tag) {
    case TAG_H1:
        e->marcas |= PECA_NEGRITO;
        e->escala = 2;
        break;
    case TAG_B: case TAG_STRONG: case TAG_TH: case TAG_DT: case TAG_SUMMARY: case TAG_LEGEND:
    case TAG_H2: case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6:
        e->marcas |= PECA_NEGRITO;
        break;
    case TAG_HEAD: case TAG_SCRIPT: case TAG_STYLE: case TAG_TITLE: case TAG_META: case TAG_LINK: case TAG_BASE:
    case TAG_NOSCRIPT:
        e->escondido = true;
        return;
    case TAG_A: {
        const char *h = html_atributo(x, "href");
        if (h && *h && p->nlinks < MAX_LINKS) {
            e->link = (int16_t)p->nlinks;
            p->links[p->nlinks++] = h;
            e->cor = COR_LINK;
            e->marcas |= PECA_SUBLINHADO;
        }
        break;
    }
    case TAG_FONT: {
        const char *c = html_atributo(x, "color");
        if (c) ler_cor(c, (int)tam_texto(c), &e->cor);
        break;
    }
    default: break;
    }
    if (html_atributo(x, "hidden")) e->escondido = true;
    const char *a = html_atributo(x, "align");
    if (a) e->alinhar = igual_sem_caixa(a, "center", 6) ? 1 : igual_sem_caixa(a, "right", 5) ? 2 : e->alinhar;
    const char *bg = html_atributo(x, "bgcolor");
    if (bg) ler_cor(bg, (int)tam_texto(bg), &e->fundo);
    const char *classe = html_atributo(x, "class"), *id = html_atributo(x, "id");
    for (int i = 0; i < p->nregras; i++) {
        const regra_t *r = &p->regras[i];
        if (r->tag && r->tag != x->tag) continue;
        if (r->classe && (!classe || !tem_classe(classe, r->classe))) continue;
        if (r->id && (!id || compara_texto(id, r->id) != 0)) continue;
        if (!r->tag && !r->classe && !r->id) continue;
        aplicar_css(e, r->decl, r->ndecl);
    }
    const char *st = html_atributo(x, "style");
    if (st) aplicar_css(e, st, (int)tam_texto(st));
}

/* ------------------------------------------------------------ peças */

static peca_t *nova_peca(pagina_t *p, int tipo)
{
    if (p->qtd == p->cap) {
        int cap = p->cap ? p->cap * 2 : 1024;
        peca_t *n = realoca(p->pecas, (size_t)cap * sizeof(peca_t));
        if (!n) return NULL;
        p->pecas = n;
        p->cap = cap;
    }
    peca_t *x = &p->pecas[p->qtd++];
    memset(x, 0, sizeof *x);
    x->tipo = (uint8_t)tipo;
    x->link = -1;
    x->img = -1;
    x->campo = -1;
    return x;
}

/* ------------------------------------------------------------ fluxo */

typedef struct {
    pagina_t *p;
    int x0, larg;              /* a caixa do bloco atual */
    int x, y;                  /* cursor */
    int alt_linha;
    int inicio_linha;          /* primeira peça da linha atual */
    int margem;                /* margem pendente (aplicada antes do próximo conteúdo) */
    bool espaco;               /* um espaço espera a próxima palavra */
    uint8_t alinhar;
    int max_x;                 /* para medir: até onde o conteúdo chegou */
    int flut_x, flut_y;        /* caixa flutuando à direita: começa em flut_x e vai até flut_y */
} fluxo_t;

/* Largura útil na altura atual: até a caixa flutuante, se houver uma ao lado. */
static int util(fluxo_t *f)
{
    int d = f->x0 + f->larg;
    if (f->y < f->flut_y && f->flut_x < d) d = f->flut_x;
    return d - f->x0;
}

/* Se sobrou pouco espaço ao lado da caixa flutuante, o texto desce para baixo dela. */
static void abrir_espaco(fluxo_t *f)
{
    if (f->y < f->flut_y && util(f) < 160 && f->x == f->x0) f->y = f->flut_y;
}

static void fechar_linha(fluxo_t *f)
{
    if (f->inicio_linha < f->p->qtd) {
        int ult = f->x;
        int dir = f->x0 + util(f);
        int desloc = f->alinhar == 1 ? (dir - ult) / 2 : f->alinhar == 2 ? dir - ult : 0;
        if (desloc < 0) desloc = 0;
        for (int i = f->inicio_linha; i < f->p->qtd; i++) {
            peca_t *x = &f->p->pecas[i];
            if (x->tipo == PECA_FUNDO || x->tipo == PECA_BORDA) continue;
            x->x += desloc;
            x->y = f->y + f->alt_linha - x->a;     /* alinhado pela base */
            if (x->x + x->l > f->max_x) f->max_x = x->x + x->l;
        }
        f->y += f->alt_linha;
    }
    f->x = f->x0;
    f->alt_linha = 0;
    f->inicio_linha = f->p->qtd;
    f->espaco = false;
}

static void quebra(fluxo_t *f, int margem)
{
    if (f->inicio_linha < f->p->qtd || f->x != f->x0) fechar_linha(f);
    f->espaco = false;
    if (margem > f->margem) f->margem = margem;
}

static void aplicar_margem(fluxo_t *f)
{
    if (f->margem) { f->y += f->margem; f->margem = 0; }
    abrir_espaco(f);
}

/* Põe uma caixa de l x a na linha (quebrando antes se não couber). */
static int colocar(fluxo_t *f, int l, int a, bool espaco_antes)
{
    aplicar_margem(f);
    int esp = espaco_antes && f->x > f->x0 ? LETRA_L : 0;
    /* Um pedaço curto grudado no anterior (a vírgula depois de um link) não
     * quebra a linha: passa um pouco da margem. */
    bool grudado = !espaco_antes && l <= 2 * LETRA_L && f->inicio_linha < f->p->qtd;
    if (f->x + esp + l > f->x0 + util(f) && f->x > f->x0 && !grudado) {
        fechar_linha(f);
        abrir_espaco(f);
        esp = 0;
    }
    int x = f->x + esp;
    f->x = x + l;
    if (a > f->alt_linha) f->alt_linha = a;
    return x;
}

static void texto_peca(fluxo_t *f, const estilo_t *e, const char *t, int n, int escala)
{
    int l = n * LETRA_L * escala, a = LETRA_A * escala + (escala > 1 ? 4 : 2);
    bool esp = f->espaco;
    /* Junta com a peça anterior se for da mesma linha e do mesmo estilo. */
    peca_t *ant = f->p->qtd > f->inicio_linha ? &f->p->pecas[f->p->qtd - 1] : NULL;
    int x = colocar(f, l, a, esp);
    if (ant && f->p->qtd > f->inicio_linha && ant->tipo == PECA_TEXTO && ant->cor == e->cor &&
        ant->marcas == (e->marcas | (escala > 1 ? PECA_DOBRO : 0)) && ant->link == e->link && ant->fundo == e->fundo &&
        ant->texto + ant->n + (esp ? 1 : 0) == t - 0 && ant->x + ant->l + (esp ? LETRA_L * escala : 0) == x) {
        ant->n = (int)(t + n - ant->texto);
        ant->l = x + l - ant->x;
        f->espaco = false;
        return;
    }
    peca_t *pc = nova_peca(f->p, PECA_TEXTO);
    if (!pc) return;
    pc->x = x;
    pc->l = l;
    pc->a = a;
    pc->cor = e->cor;
    pc->fundo = e->fundo;
    pc->marcas = (uint8_t)(e->marcas | (escala > 1 ? PECA_DOBRO : 0));
    pc->link = e->link;
    pc->texto = t;
    pc->n = n;
    f->espaco = false;
}

/* Texto normal: palavras separadas por espaços. O texto é copiado para a
 * arena já com os espaços simplificados, para que palavras vizinhas de uma
 * mesma peça fiquem contíguas na memória. */
static void fluxo_texto(fluxo_t *f, const no_t *x, const estilo_t *e, int escala)
{
    if (e->pre) {
        const char *t = x->texto;
        int n = (int)x->tam, por_linha = f->larg / (LETRA_L * escala);
        if (por_linha < 1) por_linha = 1;
        int ini = 0;
        for (int i = 0; i <= n; i++) {
            if (i == n || t[i] == '\n') {
                for (int k = ini; k < i; k += por_linha) {
                    int m = i - k < por_linha ? i - k : por_linha;
                    /* Tabulações viram espaço (a fonte não tem tabulação). */
                    char *c = arena_aloca(f->p->doc, (uint32_t)m + 1);
                    if (!c) return;
                    for (int q = 0; q < m; q++) c[q] = t[k + q] == '\t' ? ' ' : t[k + q];
                    f->espaco = false;
                    texto_peca(f, e, c, m, escala);
                    if (k + m < i) fechar_linha(f);
                }
                if (i < n) {
                    if (f->alt_linha == 0) f->alt_linha = LETRA_A * escala + 2;
                    fechar_linha(f);
                }
                ini = i + 1;
            }
        }
        return;
    }
    char *c = arena_aloca(f->p->doc, x->tam + 1);
    if (!c) return;
    int k = 0;
    bool esp_ant = false;
    for (uint32_t i = 0; i < x->tam; i++) {
        char ch = x->texto[i];
        if (eh_espaco(ch)) { if (!esp_ant) c[k++] = ' '; esp_ant = true; }
        else { c[k++] = ch; esp_ant = false; }
    }
    int max_letras = util(f) / (LETRA_L * escala);
    if (max_letras < 1) max_letras = 1;
    for (int i = 0; i < k;) {
        if (c[i] == ' ') { if (f->x > f->x0 || f->inicio_linha < f->p->qtd) f->espaco = true; i++; continue; }
        int ini = i;
        while (i < k && c[i] != ' ' && i - ini < max_letras) i++;
        texto_peca(f, e, c + ini, i - ini, escala);
    }
}

/* ------------------------------------------------------------ medidas (para tabelas) */

typedef struct { int minimo, preferido, linha; } medida_t;

static void medir(pagina_t *p, const no_t *x, medida_t *m, const estilo_t *pai);

static void medir_filhos(pagina_t *p, const no_t *x, medida_t *m, const estilo_t *e)
{
    for (const no_t *c = x->filho; c; c = c->prox) medir(p, c, m, e);
}

static bool eh_bloco(int t)
{
    return t == TAG_P || t == TAG_DIV || (t >= TAG_H1 && t <= TAG_H6) || t == TAG_UL || t == TAG_OL || t == TAG_LI ||
           t == TAG_DL || t == TAG_DT || t == TAG_DD || t == TAG_BLOCKQUOTE || t == TAG_PRE || t == TAG_TABLE ||
           t == TAG_FORM || t == TAG_HR || t == TAG_NAV || t == TAG_HEADER || t == TAG_FOOTER || t == TAG_SECTION ||
           t == TAG_ARTICLE || t == TAG_ASIDE || t == TAG_MAIN || t == TAG_FIGURE || t == TAG_FIGCAPTION ||
           t == TAG_CENTER || t == TAG_ADDRESS || t == TAG_DETAILS || t == TAG_SUMMARY || t == TAG_FIELDSET ||
           t == TAG_LEGEND || t == TAG_CAPTION || t == TAG_BODY || t == TAG_HTML || t == TAG_TR || t == TAG_TBODY ||
           t == TAG_THEAD || t == TAG_TFOOT;
}

static void tamanho_imagem(pagina_t *p, const no_t *x, int max, int *l, int *a);

static void medir(pagina_t *p, const no_t *x, medida_t *m, const estilo_t *pai)
{
    if (x->tipo == NO_TEXTO) {
        int palavra = 0;
        for (uint32_t i = 0; i < x->tam; i++) {
            if (eh_espaco(x->texto[i]) && !pai->pre) {
                if (palavra) { m->linha += LETRA_L; palavra = 0; }
                continue;
            }
            if (pai->pre && x->texto[i] == '\n') { m->linha = 0; palavra = 0; continue; }
            palavra += LETRA_L;
            m->linha += LETRA_L;
            if (palavra > m->minimo) m->minimo = palavra;
            if (m->linha > m->preferido) m->preferido = m->linha;
        }
        return;
    }
    estilo_t e = *pai;
    int nlinks = p->nlinks;
    estilo_do_elemento(p, x, &e);
    p->nlinks = nlinks;                      /* medir não registra links */
    if (e.escondido) return;
    if (x->tag == TAG_PRE) e.pre = true;
    if (x->tag == TAG_IMG) {
        int l, a;
        tamanho_imagem(p, x, 4000, &l, &a);
        m->linha += l;
        if (l > m->minimo) m->minimo = l;
        if (m->linha > m->preferido) m->preferido = m->linha;
        return;
    }
    if (x->tag == TAG_INPUT) { m->linha += 160; if (160 > m->minimo) m->minimo = 160; if (m->linha > m->preferido) m->preferido = m->linha; return; }
    if (x->tag == TAG_BR) { m->linha = 0; return; }
    if (x->tag == TAG_TABLE) {
        /* Aproximação: a tabela inteira como uma palavra larga. */
        medida_t t = { 0, 0, 0 };
        medir_filhos(p, x, &t, &e);
        m->linha = 0;
        if (t.minimo > m->minimo) m->minimo = t.minimo;
        if (t.preferido > m->preferido) m->preferido = t.preferido;
        return;
    }
    if (x->tag == TAG_TD || x->tag == TAG_TH) {
        medida_t t = { 0, 0, 0 };
        medir_filhos(p, x, &t, &e);
        m->linha += t.preferido + 8;
        m->minimo += t.minimo + 8;          /* células lado a lado somam */
        if (m->linha > m->preferido) m->preferido = m->linha;
        return;
    }
    bool bloco = eh_bloco(x->tag);
    int recuo = (x->tag == TAG_UL || x->tag == TAG_OL || x->tag == TAG_BLOCKQUOTE || x->tag == TAG_DD) ? 24 : 0;
    if (bloco) m->linha = 0;
    if (x->tag == TAG_TR) {
        medida_t t = { 0, 0, 0 };
        medir_filhos(p, x, &t, &e);
        if (t.minimo > m->minimo) m->minimo = t.minimo;
        if (t.preferido > m->preferido) m->preferido = t.preferido;
        m->linha = 0;
        return;
    }
    medida_t t = { 0, 0, 0 };
    medir_filhos(p, x, recuo ? &t : m, &e);
    if (recuo) {
        if (t.minimo + recuo > m->minimo) m->minimo = t.minimo + recuo;
        if (t.preferido + recuo > m->preferido) m->preferido = t.preferido + recuo;
    }
    if (bloco) m->linha = 0;
    if (m->preferido > 3000) m->preferido = 3000;
}

/* ------------------------------------------------------------ imagens */

static int numero(const char *s)
{
    if (!s) return 0;
    int v = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

static int achar_imagem(pagina_t *p, const no_t *x)
{
    for (int i = 0; i < p->nimgs; i++) if (p->imgs[i].no == x) return i;
    return -1;
}

static void tamanho_imagem(pagina_t *p, const no_t *x, int max, int *l, int *a)
{
    int dl = numero(html_atributo(x, "width")), da = numero(html_atributo(x, "height"));
    int i = achar_imagem(p, x);
    imagem_t *img = i >= 0 ? p->imgs[i].img : NULL;
    if (img) {
        if (dl && da) { *l = dl; *a = da; }
        else if (dl) { *l = dl; *a = (int)((uint32_t)dl * img->alt / img->larg); }
        else if (da) { *a = da; *l = (int)((uint32_t)da * img->larg / img->alt); }
        else { *l = (int)img->larg; *a = (int)img->alt; }
    } else if (dl && da) { *l = dl; *a = da; }
    else { *l = dl ? dl : 24; *a = da ? da : 24; }
    if (*l > max && *l > 0) { *a = *a * max / *l; *l = max; }
    if (*l < 1) *l = 1;
    if (*a < 1) *a = 1;
    if (*a > 4000) *a = 4000;
}

/* ------------------------------------------------------------ layout */

static void layout(fluxo_t *f, const no_t *x, const estilo_t *pai, int profundidade);

static void layout_filhos(fluxo_t *f, const no_t *x, const estilo_t *e, int prof)
{
    for (const no_t *c = x->filho; c; c = c->prox) layout(f, c, e, prof + 1);
}

/* Um bloco com recuo à esquerda (listas, citações). */
static void layout_recuado(fluxo_t *f, const no_t *x, const estilo_t *e, int recuo, int prof)
{
    int x0 = f->x0, larg = f->larg;
    f->x0 += recuo;
    f->larg -= recuo;
    if (f->larg < 40) { f->x0 = x0; f->larg = larg; }
    f->x = f->x0;
    layout_filhos(f, x, e, prof);
    quebra(f, 0);
    f->x0 = x0;
    f->larg = larg;
    f->x = f->x0;
}

#define MAX_COLUNAS 40

static void layout_tabela(fluxo_t *f, const no_t *tabela, const estilo_t *e, int prof, int max_largura)
{
    /* Junta as linhas (dentro ou fora de thead/tbody/tfoot) e a legenda. */
    const no_t *linhas[512];
    int nlinhas = 0;
    const no_t *legenda = NULL;
    for (const no_t *c = tabela->filho; c; c = c->prox) {
        if (c->tipo != NO_ELEMENTO) continue;
        if (c->tag == TAG_CAPTION) legenda = c;
        else if (c->tag == TAG_TR) { if (nlinhas < 512) linhas[nlinhas++] = c; }
        else if (c->tag == TAG_THEAD || c->tag == TAG_TBODY || c->tag == TAG_TFOOT)
            for (const no_t *r = c->filho; r; r = r->prox)
                if (r->tipo == NO_ELEMENTO && r->tag == TAG_TR && nlinhas < 512) linhas[nlinhas++] = r;
    }
    quebra(f, 6);
    aplicar_margem(f);
    if (legenda) {
        estilo_t el = *e;
        estilo_do_elemento(f->p, legenda, &el);
        if (!el.escondido) {
            uint8_t al = f->alinhar;
            f->alinhar = 1;
            layout_filhos(f, legenda, &el, prof);
            quebra(f, 0);
            f->alinhar = al;
            aplicar_margem(f);
        }
    }
    /* Larguras das colunas. */
    int minimo[MAX_COLUNAS], preferido[MAX_COLUNAS], ncol = 0;
    memset(minimo, 0, sizeof minimo);
    memset(preferido, 0, sizeof preferido);
    for (int r = 0; r < nlinhas; r++) {
        int col = 0;
        estilo_t er = *e;
        estilo_do_elemento(f->p, linhas[r], &er);
        int nl = f->p->nlinks;
        (void)nl;
        for (const no_t *c = linhas[r]->filho; c && col < MAX_COLUNAS; c = c->prox) {
            if (c->tipo != NO_ELEMENTO || (c->tag != TAG_TD && c->tag != TAG_TH)) continue;
            int span = numero(html_atributo(c, "colspan"));
            if (span < 1) span = 1;
            if (col + span > MAX_COLUNAS) span = MAX_COLUNAS - col;
            medida_t m = { 0, 0, 0 };
            estilo_t ec = er;
            int nlinks = f->p->nlinks;
            estilo_do_elemento(f->p, c, &ec);
            f->p->nlinks = nlinks;
            if (!ec.escondido) medir_filhos(f->p, c, &m, &ec);
            m.minimo += 8;
            m.preferido += 8;
            if (span == 1) {
                if (m.minimo > minimo[col]) minimo[col] = m.minimo;
                if (m.preferido > preferido[col]) preferido[col] = m.preferido;
            }
            col += span;
        }
        if (col > ncol) ncol = col;
    }
    if (ncol == 0) return;
    int soma_min = 0, soma_pref = 0, disponivel = max_largura > 0 ? max_largura : util(f);
    for (int c = 0; c < ncol; c++) {
        if (minimo[c] < 16) minimo[c] = 16;
        if (preferido[c] < minimo[c]) preferido[c] = minimo[c];
        soma_min += minimo[c];
        soma_pref += preferido[c];
    }
    int larg[MAX_COLUNAS];
    const char *w = html_atributo(tabela, "width");
    bool cheia = w && acha_char(w, '%') && numero(w) >= 90;
    if (soma_pref <= disponivel) {
        int sobra = cheia ? disponivel - soma_pref : 0;
        for (int c = 0; c < ncol; c++) larg[c] = preferido[c] + (soma_pref ? sobra * preferido[c] / soma_pref : 0);
    } else if (soma_min <= disponivel) {
        int extra = disponivel - soma_min, falta = soma_pref - soma_min;
        for (int c = 0; c < ncol; c++)
            larg[c] = minimo[c] + (falta ? (int)((long long)extra * (preferido[c] - minimo[c]) / falta) : 0);
    } else {
        for (int c = 0; c < ncol; c++) larg[c] = minimo[c] * disponivel / soma_min;
    }
    int total = 0;
    for (int c = 0; c < ncol; c++) total += larg[c];
    const char *borda = html_atributo(tabela, "border"), *classe = html_atributo(tabela, "class");
    bool com_borda = (borda && numero(borda) > 0) ||
                     (classe && (tem_classe(classe, "wikitable") || tem_classe(classe, "infobox") ||
                                 tem_classe(classe, "infobox_v2") || tem_classe(classe, "infobox_v3")));
    int x_tab = f->x0;
    if (e->alinhar == 1 || (html_atributo(tabela, "align") && igual_sem_caixa(html_atributo(tabela, "align"), "center", 6)))
        x_tab += (util(f) - total) / 2;
    if (x_tab < f->x0) x_tab = f->x0;
    int y = f->y;
    for (int r = 0; r < nlinhas; r++) {
        estilo_t er = *e;
        estilo_do_elemento(f->p, linhas[r], &er);
        if (er.escondido) continue;
        int col = 0, x = x_tab, altura = 0;
        int inicio = f->p->qtd;
        int celulas[MAX_COLUNAS][3];      /* x, largura, índice do fundo */
        int ncel = 0;
        for (const no_t *c = linhas[r]->filho; c && col < ncol; c = c->prox) {
            if (c->tipo != NO_ELEMENTO || (c->tag != TAG_TD && c->tag != TAG_TH)) continue;
            int span = numero(html_atributo(c, "colspan"));
            if (span < 1) span = 1;
            if (col + span > ncol) span = ncol - col;
            int l = 0;
            for (int k = 0; k < span; k++) l += larg[col + k];
            estilo_t ec = er;
            estilo_do_elemento(f->p, c, &ec);
            int fundo = -1;
            if (ec.fundo != COR_NENHUMA && ec.fundo != e->fundo) {
                peca_t *pf = nova_peca(f->p, PECA_FUNDO);
                if (pf) { pf->x = x; pf->y = y; pf->l = l; pf->cor = ec.fundo; fundo = f->p->qtd - 1; }
            }
            if (!ec.escondido) {
                fluxo_t sub = *f;
                sub.x0 = sub.x = x + 4;
                sub.larg = l - 8;
                if (sub.larg < 8) sub.larg = 8;
                sub.y = y + 3;
                sub.alt_linha = 0;
                sub.margem = 0;
                sub.inicio_linha = f->p->qtd;
                sub.espaco = false;
                sub.alinhar = ec.alinhar;
                sub.max_x = 0;
                sub.flut_y = 0;
                layout_filhos(&sub, c, &ec, prof);
                quebra(&sub, 0);
                int a = sub.y - y + 3;
                if (a > altura) altura = a;
            }
            if (ncel < MAX_COLUNAS) { celulas[ncel][0] = x; celulas[ncel][1] = l; celulas[ncel][2] = fundo; ncel++; }
            x += l;
            col += span;
        }
        if (altura < LETRA_A + 6) altura = LETRA_A + 6;
        for (int k = 0; k < ncel; k++) {
            if (celulas[k][2] >= 0) f->p->pecas[celulas[k][2]].a = altura;
            if (com_borda) {
                peca_t *b = nova_peca(f->p, PECA_BORDA);
                if (b) { b->x = celulas[k][0]; b->y = y; b->l = celulas[k][1] + 1; b->a = altura + 1; b->cor = 0xA2A9B1; }
            }
        }
        (void)inicio;
        y += altura;
    }
    f->y = y;
    f->x = f->x0;
    f->inicio_linha = f->p->qtd;
    f->alt_linha = 0;
    if (x_tab + total > f->max_x) f->max_x = x_tab + total;
    quebra(f, 6);
}

/* Caixas que flutuam à direita: tabelas de informações (infobox), figuras
 * "thumb" da Wikipédia e quem pede float: right no style ou align=right. */
static bool flutua(const no_t *x)
{
    const char *c = html_atributo(x, "class"), *st = html_atributo(x, "style"), *al = html_atributo(x, "align");
    if (al && igual_sem_caixa(al, "right", 5) && (x->tag == TAG_TABLE || x->tag == TAG_FIGURE || x->tag == TAG_DIV)) return true;
    if (st) for (const char *q = st; *q; q++) if (igual_sem_caixa(q, "float", 5)) {
        q += 5;
        while (*q == ' ' || *q == ':') q++;
        return igual_sem_caixa(q, "right", 5);
    }
    if (c && (tem_classe(c, "infobox") || tem_classe(c, "infobox_v2") || tem_classe(c, "infobox_v3") ||
              tem_classe(c, "tright") || tem_classe(c, "floatright") || tem_classe(c, "sidebar"))) return true;
    if (x->tag == TAG_FIGURE) {
        const char *t = html_atributo(x, "typeof");
        if (c && (tem_classe(c, "mw-halign-left") || tem_classe(c, "mw-halign-center") || tem_classe(c, "mw-halign-none"))) return false;
        if (t && (comeca_com(t, "mw:File/Thumb") || comeca_com(t, "mw:File/Frame"))) return true;
    }
    return false;
}

static void layout(fluxo_t *f, const no_t *x, const estilo_t *pai, int profundidade);

static void layout_flutuante(fluxo_t *f, const no_t *x, const estilo_t *e, int prof)
{
    pagina_t *p = f->p;
    if (f->inicio_linha < p->qtd || f->x != f->x0) fechar_linha(f);
    /* Largura: a preferida, até 45% do espaço (mínimo 200). */
    medida_t m = { 0, 0, 0 };
    int nlinks = p->nlinks;
    medir(p, x, &m, e);
    p->nlinks = nlinks;
    int maximo = f->larg * 45 / 100;
    if (maximo < 200) maximo = f->larg < 200 ? f->larg : 200;
    int l = m.preferido + 12;
    if (l > maximo) l = maximo;
    if (l < m.minimo + 12 && m.minimo + 12 <= f->larg / 2) l = m.minimo + 12;
    int y = f->y + f->margem;
    if (f->flut_y > y) y = f->flut_y + 6;        /* uma embaixo da outra */
    fluxo_t sub = *f;
    sub.x0 = sub.x = f->x0 + f->larg - l;
    sub.larg = l;
    sub.y = y;
    sub.alt_linha = 0;
    sub.margem = 0;
    sub.inicio_linha = p->qtd;
    sub.espaco = false;
    sub.flut_y = 0;
    sub.max_x = 0;
    if (x->tag == TAG_TABLE) layout_tabela(&sub, x, e, prof, l);
    else {
        estilo_t ef = *e;
        int fundo = -1;
        peca_t *pf = nova_peca(p, PECA_BORDA);
        if (pf) { pf->x = sub.x0 - 4; pf->y = y - 4; pf->l = l + 4; pf->cor = 0xC8CCD1; fundo = p->qtd - 1; }
        sub.inicio_linha = p->qtd;
        sub.x0 += 4; sub.x = sub.x0; sub.larg -= 8;
        for (const no_t *c = x->filho; c; c = c->prox) layout(&sub, c, &ef, prof + 1);
        quebra(&sub, 0);
        if (fundo >= 0) p->pecas[fundo].a = sub.y - y + 8;
        sub.y += 4;
    }
    f->flut_x = f->x0 + f->larg - l - 10;
    f->flut_y = sub.y + 6;
    f->inicio_linha = p->qtd;
    f->x = f->x0;
}

static void layout_campo(fluxo_t *f, const no_t *x, const estilo_t *e)
{
    const char *tipo = html_atributo(x, "type");
    if (!tipo) tipo = "text";
    if (igual_sem_caixa(tipo, "hidden", 7)) return;
    pagina_t *p = f->p;
    if (p->ncampos >= MAX_CAMPOS) return;
    campo_t *c = &p->campos[p->ncampos];
    memset(c, 0, sizeof *c);
    c->no = x;
    c->form = e->form;
    const char *v = html_atributo(x, "value");
    bool botao = igual_sem_caixa(tipo, "submit", 7) || igual_sem_caixa(tipo, "button", 7) || x->tag == TAG_BUTTON;
    if (igual_sem_caixa(tipo, "checkbox", 9) || igual_sem_caixa(tipo, "radio", 6) || igual_sem_caixa(tipo, "image", 6) ||
        igual_sem_caixa(tipo, "file", 5)) return;
    c->botao = botao;
    if (v) copia_texto(c->valor, v, sizeof c->valor);
    if (botao && !c->valor[0] && x->tag == TAG_BUTTON) {
        /* O rótulo de <button> é o texto de dentro. */
        int k = 0;
        for (const no_t *t = x->filho; t; t = t->filho ? t->filho : t->prox) {
            if (t->tipo != NO_TEXTO) continue;
            for (uint32_t i = 0; i < t->tam && k < (int)sizeof c->valor - 1; i++)
                if (!eh_espaco(t->texto[i]) || (k > 0 && c->valor[k - 1] != ' ')) c->valor[k++] = eh_espaco(t->texto[i]) ? ' ' : t->texto[i];
        }
        while (k > 0 && c->valor[k - 1] == ' ') k--;
        c->valor[k] = 0;
    }
    if (botao && !c->valor[0]) copia_texto(c->valor, "Enviar", sizeof c->valor);
    int l = botao ? (int)tam_texto(c->valor) * LETRA_L + 16 : 200, a = 22;
    if (l > util(f)) l = util(f);
    int px = colocar(f, l, a, f->espaco);
    f->espaco = false;
    peca_t *pc = nova_peca(p, botao ? PECA_BOTAO : PECA_CAMPO);
    if (!pc) return;
    pc->x = px;
    pc->l = l;
    pc->a = a;
    pc->campo = p->ncampos;
    c->peca = p->qtd - 1;
    p->ncampos++;
}

static void layout(fluxo_t *f, const no_t *x, const estilo_t *pai, int prof)
{
    if (prof > 200) return;                      /* página com aninhamento absurdo */
    if (x->tipo == NO_TEXTO) {
        fluxo_texto(f, x, pai, pai->escala);
        return;
    }
    estilo_t e = *pai;
    estilo_do_elemento(f->p, x, &e);
    if (e.escondido) return;
    pagina_t *p = f->p;
    const char *id = html_atributo(x, "id");
    if (id && p->nancoras < MAX_ANCORAS) {
        p->ancoras[p->nancoras].nome = id;
        p->ancoras[p->nancoras].y = f->y + f->margem;
        p->nancoras++;
    }
    const char *nome = x->tag == TAG_A ? html_atributo(x, "name") : NULL;
    if (nome && p->nancoras < MAX_ANCORAS) { p->ancoras[p->nancoras].nome = nome; p->ancoras[p->nancoras].y = f->y; p->nancoras++; }
    switch (x->tag) {
    case TAG_BR:
        if (f->alt_linha == 0) f->alt_linha = LETRA_A + 2;
        fechar_linha(f);
        return;
    case TAG_WBR:
        return;
    case TAG_HR: {
        quebra(f, 8);
        aplicar_margem(f);
        peca_t *l = nova_peca(p, PECA_LINHA);
        if (l) { l->x = f->x0; l->y = f->y; l->l = util(f); l->a = 1; l->cor = 0xA2A9B1; }
        f->y += 1;
        f->inicio_linha = p->qtd;
        quebra(f, 8);
        return;
    }
    case TAG_IMG: {
        int l, a;
        tamanho_imagem(p, x, util(f), &l, &a);
        int i = achar_imagem(p, x);
        if (i < 0 && p->nimgs < MAX_IMAGENS) {
            const char *src = html_atributo(x, "src");
            if (src && *src) {
                i = p->nimgs++;
                memset(&p->imgs[i], 0, sizeof p->imgs[i]);
                p->imgs[i].no = x;
                p->imgs[i].src = src;
            }
        }
        int px = colocar(f, l, a, f->espaco);
        f->espaco = false;
        peca_t *pc = nova_peca(p, PECA_IMAGEM);
        if (!pc) return;
        pc->x = px;
        pc->l = l;
        pc->a = a;
        pc->img = i;
        pc->link = e.link;
        const char *alt = html_atributo(x, "alt");
        pc->texto = alt ? alt : "";
        pc->n = (int)tam_texto(pc->texto);
        return;
    }
    case TAG_INPUT: case TAG_BUTTON:
        layout_campo(f, x, &e);
        return;
    case TAG_TEXTAREA:
        layout_campo(f, x, &e);
        return;
    case TAG_TABLE:
        if (flutua(x)) { layout_flutuante(f, x, &e, prof); return; }
        layout_tabela(f, x, &e, prof, 0);
        return;
    case TAG_FORM:
        if (p->nforms < MAX_FORMS) { p->forms[p->nforms] = x; e.form = (int16_t)p->nforms++; }
        break;
    default: break;
    }
    if (x->tag == TAG_PRE) e.pre = true;
    if ((x->tag == TAG_FIGURE || x->tag == TAG_DIV) && flutua(x)) { layout_flutuante(f, x, &e, prof); return; }
    if (!eh_bloco(x->tag)) {
        /* Elementos em linha: só mudam o estilo (o fundo só em <mark> e <code>). */
        layout_filhos(f, x, &e, prof);
        return;
    }
    int margem = 0;
    switch (x->tag) {
    case TAG_P: case TAG_UL: case TAG_OL: case TAG_DL: case TAG_BLOCKQUOTE: case TAG_PRE: case TAG_FIGURE:
    case TAG_FORM: case TAG_FIELDSET:
        margem = prof > 0 && (pai->pre) ? 0 : 10; break;
    case TAG_H1: margem = 16; break;
    case TAG_H2: margem = 14; break;
    case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6: margem = 10; break;
    default: margem = 0;
    }
    if ((x->tag == TAG_UL || x->tag == TAG_OL) && x->pai && (x->pai->tag == TAG_LI)) margem = 0;
    quebra(f, margem);
    uint8_t alinhar = f->alinhar;
    f->alinhar = e.alinhar;
    /* Fundo do bloco: a altura só se sabe no fim. */
    int fundo = -1;
    if (e.fundo != pai->fundo && e.fundo != COR_NENHUMA && x->tag != TAG_BODY && x->tag != TAG_HTML) {
        aplicar_margem(f);
        peca_t *pf = nova_peca(p, PECA_FUNDO);
        if (pf) { pf->x = f->x0; pf->y = f->y; pf->l = util(f); pf->cor = e.fundo; fundo = p->qtd - 1; }
        f->inicio_linha = p->qtd;
    }
    int y0 = f->y;
    if (x->tag == TAG_BODY && e.fundo != COR_NENHUMA) p->fundo = e.fundo;
    switch (x->tag) {
    case TAG_H1: {
        aplicar_margem(f);
        layout_filhos(f, x, &e, prof);
        quebra(f, 0);
        aplicar_margem(f);
        peca_t *l = nova_peca(p, PECA_LINHA);
        if (l) { l->x = f->x0; l->y = f->y + 2; l->l = util(f); l->a = 1; l->cor = 0xA2A9B1; }
        f->y += 3;
        f->inicio_linha = p->qtd;
        break;
    }
    case TAG_H2:
        layout_filhos(f, x, &e, prof);
        quebra(f, 0);
        aplicar_margem(f);
        {
            peca_t *l = nova_peca(p, PECA_LINHA);
            if (l) { l->x = f->x0; l->y = f->y + 1; l->l = util(f); l->a = 1; l->cor = 0xC8CCD1; }
            f->y += 2;
            f->inicio_linha = p->qtd;
        }
        break;
    case TAG_UL: case TAG_OL: {
        int numero_item = numero(html_atributo(x, "start"));
        if (numero_item < 1) numero_item = 1;
        int x0 = f->x0, larg = f->larg;
        f->x0 += 24;
        f->larg -= 24;
        if (f->larg < 40) { f->x0 = x0; f->larg = larg; }
        f->x = f->x0;
        for (const no_t *c = x->filho; c; c = c->prox) {
            if (c->tipo == NO_ELEMENTO && c->tag == TAG_LI) {
                estilo_t el = e;
                estilo_do_elemento(p, c, &el);
                if (el.escondido) continue;
                quebra(f, 2);
                aplicar_margem(f);
                /* O marcador fica à esquerda da caixa, na altura da primeira linha. */
                char *m = arena_aloca(p->doc, 8);
                if (m) {
                    int n;
                    if (x->tag == TAG_OL) n = formatar_texto(m, 8, "%d.", numero_item++);
                    else { m[0] = (char)0xB7; m[1] = 0; n = 1; }
                    peca_t *pm = nova_peca(p, PECA_TEXTO);
                    if (pm) {
                        pm->x = f->x0 - (n + 1) * LETRA_L;
                        pm->y = f->y;
                        pm->l = n * LETRA_L;
                        pm->a = LETRA_A + 2;
                        pm->cor = e.cor;
                        pm->fundo = COR_NENHUMA;
                        pm->texto = m;
                        pm->n = n;
                        pm->marcas = PECA_FIXA;
                    }
                    f->inicio_linha = p->qtd;
                }
                layout_filhos(f, c, &el, prof + 1);
                quebra(f, 2);
            } else layout(f, c, &e, prof + 1);
        }
        quebra(f, 0);
        f->x0 = x0;
        f->larg = larg;
        f->x = f->x0;
        break;
    }
    case TAG_BLOCKQUOTE: case TAG_DD:
        layout_recuado(f, x, &e, x->tag == TAG_DD ? 32 : 28, prof);
        break;
    default:
        layout_filhos(f, x, &e, prof);
    }
    quebra(f, margem);
    if (fundo >= 0) { p->pecas[fundo].a = f->y - y0; p->pecas[fundo].y = y0; }
    f->alinhar = alinhar;
}

/* ------------------------------------------------------------ montagem */

static void achar_folhas(pagina_t *p, const no_t *x)
{
    for (; x; x = x->prox) {
        if (x->tipo != NO_ELEMENTO) continue;
        if (x->tag == TAG_STYLE && x->filho && x->filho->tipo == NO_TEXTO) ler_folha(p, p->doc, x->filho->texto, (int)x->filho->tam);
        achar_folhas(p, x->filho);
    }
}

pagina_t *pagina_criar(documento_t *doc)
{
    pagina_t *p = aloca_zerado(sizeof(pagina_t));
    if (!p) return NULL;
    p->doc = doc;
    p->fundo = COR_BRANCO;
    ler_folha(p, doc, FOLHA_PADRAO, (int)sizeof FOLHA_PADRAO - 1);
    achar_folhas(p, doc->raiz->filho);
    return p;
}

void pagina_layout(pagina_t *p, int largura)
{
    p->qtd = 0;
    p->nlinks = 0;
    p->nancoras = 0;
    p->nforms = 0;
    /* Os valores digitados nos campos sobrevivem a um novo layout. */
    static campo_t antigos[MAX_CAMPOS];
    int nantigos = p->ncampos;
    memcpy(antigos, p->campos, sizeof(campo_t) * (size_t)nantigos);
    p->ncampos = 0;
    p->largura = largura;
    fluxo_t f;
    memset(&f, 0, sizeof f);
    f.p = p;
    f.x0 = f.x = MARGEM_PAGINA;
    f.larg = largura - 2 * MARGEM_PAGINA;
    f.y = MARGEM_PAGINA;
    estilo_t e = { COR_TEXTO, COR_NENHUMA, 0, 0, false, false, -1, -1, 1 };
    layout_filhos(&f, p->doc->raiz, &e, 0);
    quebra(&f, 0);
    p->altura = (f.y > f.flut_y ? f.y : f.flut_y) + MARGEM_PAGINA;
    for (int i = 0; i < p->ncampos; i++)
        for (int k = 0; k < nantigos; k++)
            if (antigos[k].no == p->campos[i].no) { copia_texto(p->campos[i].valor, antigos[k].valor, sizeof p->campos[i].valor); break; }
}

void pagina_liberar(pagina_t *p)
{
    if (!p) return;
    for (int i = 0; i < p->nimgs; i++) if (p->imgs[i].img) libera(p->imgs[i].img);
    libera(p->pecas);
    html_liberar(p->doc);
    libera(p);
}

/* ------------------------------------------------------------ desenho */

extern const unsigned char fonte_sagui[256][16];

typedef struct { jan_t *j; int x0, y0, x1, y1; } recorte_t;

static void ponto(const recorte_t *r, int x, int y, uint32_t cor)
{
    if (x < r->x0 || y < r->y0 || x >= r->x1 || y >= r->y1) return;
    r->j->px[y * r->j->larg + x] = cor;
}

static void retangulo(const recorte_t *r, int x, int y, int l, int a, uint32_t cor)
{
    int xa = x < r->x0 ? r->x0 : x, ya = y < r->y0 ? r->y0 : y;
    int xb = x + l > r->x1 ? r->x1 : x + l, yb = y + a > r->y1 ? r->y1 : y + a;
    for (int yy = ya; yy < yb; yy++)
        for (int xx = xa; xx < xb; xx++) r->j->px[yy * r->j->larg + xx] = cor;
}

static void letras(const recorte_t *r, int x, int y, const char *t, int n, uint32_t cor, int escala, bool negrito)
{
    for (int k = 0; k < n; k++, x += LETRA_L * escala) {
        if (x >= r->x1) break;
        if (x + LETRA_L * escala < r->x0) continue;
        const unsigned char *g = fonte_sagui[(uint8_t)t[k]];
        for (int lin = 0; lin < 16; lin++)
            for (int col = 0; col < 8; col++)
                if (g[lin] & (0x80 >> col)) {
                    if (escala == 1) {
                        ponto(r, x + col, y + lin, cor);
                        if (negrito) ponto(r, x + col + 1, y + lin, cor);
                    } else {
                        retangulo(r, x + col * escala, y + lin * escala, escala + (negrito ? 1 : 0), escala, cor);
                    }
                }
    }
}

static uint32_t misturar(uint32_t fundo, uint32_t px)
{
    uint32_t a = px >> 24;
    if (a == 255) return px & 0xFFFFFF;
    if (a == 0) return fundo;
    uint32_t r = (((px >> 16) & 255) * a + ((fundo >> 16) & 255) * (255 - a)) / 255;
    uint32_t g = (((px >> 8) & 255) * a + ((fundo >> 8) & 255) * (255 - a)) / 255;
    uint32_t b = ((px & 255) * a + (fundo & 255) * (255 - a)) / 255;
    return r << 16 | g << 8 | b;
}

void pagina_desenhar(pagina_t *p, jan_t *j, int x0, int y0, int l, int a, int rolagem, int campo_foco)
{
    recorte_t r = { j, x0, y0, x0 + l, y0 + a };
    retangulo(&r, x0, y0, l, a, p->fundo);
    int topo = rolagem, fim = rolagem + a;
    for (int i = 0; i < p->qtd; i++) {
        const peca_t *pc = &p->pecas[i];
        if (pc->y + pc->a < topo || pc->y > fim) continue;
        int x = x0 + pc->x, y = y0 + pc->y - rolagem;
        switch (pc->tipo) {
        case PECA_FUNDO:
            retangulo(&r, x, y, pc->l, pc->a, pc->cor);
            break;
        case PECA_LINHA:
            retangulo(&r, x, y, pc->l, pc->a, pc->cor);
            break;
        case PECA_BORDA:
            retangulo(&r, x, y, pc->l, 1, pc->cor);
            retangulo(&r, x, y + pc->a - 1, pc->l, 1, pc->cor);
            retangulo(&r, x, y, 1, pc->a, pc->cor);
            retangulo(&r, x + pc->l - 1, y, 1, pc->a, pc->cor);
            break;
        case PECA_TEXTO: {
            int esc = pc->marcas & PECA_DOBRO ? 2 : 1;
            if (pc->fundo != COR_NENHUMA) retangulo(&r, x, y, pc->l, pc->a, pc->fundo);
            letras(&r, x, y + pc->a - LETRA_A * esc - (esc > 1 ? 2 : 1), pc->texto, pc->n, pc->cor, esc, pc->marcas & PECA_NEGRITO);
            if (pc->marcas & PECA_SUBLINHADO) retangulo(&r, x, y + pc->a - 2, pc->l, 1, pc->cor);
            if (pc->marcas & PECA_RISCADO) retangulo(&r, x, y + pc->a / 2, pc->l, 1, pc->cor);
            break;
        }
        case PECA_IMAGEM: {
            imagem_t *img = pc->img >= 0 ? p->imgs[pc->img].img : NULL;
            if (!img) {
                retangulo(&r, x, y, pc->l, 1, 0xC8CCD1);
                retangulo(&r, x, y + pc->a - 1, pc->l, 1, 0xC8CCD1);
                retangulo(&r, x, y, 1, pc->a, 0xC8CCD1);
                retangulo(&r, x + pc->l - 1, y, 1, pc->a, 0xC8CCD1);
                if (pc->a >= LETRA_A + 4 && pc->l > 3 * LETRA_L) {
                    int n = (pc->l - 8) / LETRA_L;
                    letras(&r, x + 4, y + 2, pc->texto, pc->n < n ? pc->n : n, COR_TEXTO_FRACO, 1, false);
                }
                break;
            }
            /* Reduz ou amplia pelo vizinho mais próximo, já recortando. */
            int ya = y < r.y0 ? r.y0 - y : 0, yb = y + pc->a > r.y1 ? r.y1 - y : pc->a;
            int xa = x < r.x0 ? r.x0 - x : 0, xb = x + pc->l > r.x1 ? r.x1 - x : pc->l;
            for (int yy = ya; yy < yb; yy++) {
                uint32_t sy = (uint32_t)yy * img->alt / (uint32_t)pc->a;
                uint32_t *linha = j->px + (y + yy) * j->larg + x;
                const uint32_t *orig = img->px + sy * img->larg;
                for (int xx = xa; xx < xb; xx++) {
                    uint32_t sx = (uint32_t)xx * img->larg / (uint32_t)pc->l;
                    linha[xx] = misturar(linha[xx], orig[sx]);
                }
            }
            break;
        }
        case PECA_CAMPO: case PECA_BOTAO: {
            const campo_t *c = &p->campos[pc->campo];
            bool botao = pc->tipo == PECA_BOTAO;
            retangulo(&r, x, y, pc->l, pc->a, botao ? COR_BOTAO : COR_BRANCO);
            uint32_t borda = pc->campo == campo_foco ? COR_SELECAO : COR_BORDA;
            retangulo(&r, x, y, pc->l, 1, borda);
            retangulo(&r, x, y + pc->a - 1, pc->l, 1, borda);
            retangulo(&r, x, y, 1, pc->a, borda);
            retangulo(&r, x + pc->l - 1, y, 1, pc->a, borda);
            int n = (int)tam_texto(c->valor), cabe = (pc->l - 8) / LETRA_L;
            const char *t = c->valor;
            if (n > cabe) { t += n - cabe; n = cabe; }
            int tx = botao ? x + (pc->l - n * LETRA_L) / 2 : x + 4;
            recorte_t rc = { j, x + 1 > r.x0 ? x + 1 : r.x0, r.y0, x + pc->l - 1 < r.x1 ? x + pc->l - 1 : r.x1, r.y1 };
            letras(&rc, tx, y + 3, t, n, COR_TEXTO, 1, false);
            if (pc->campo == campo_foco && !botao) retangulo(&rc, tx + n * LETRA_L, y + 3, 1, LETRA_A, COR_TEXTO);
            break;
        }
        }
    }
}

/* Qual peça está no ponto (coordenadas da página)? -1 = nenhuma. */
int pagina_peca_em(pagina_t *p, int x, int y)
{
    for (int i = p->qtd - 1; i >= 0; i--) {
        const peca_t *pc = &p->pecas[i];
        if (pc->tipo == PECA_FUNDO || pc->tipo == PECA_LINHA || pc->tipo == PECA_BORDA) continue;
        if (x >= pc->x && x < pc->x + pc->l && y >= pc->y && y < pc->y + pc->a) return i;
    }
    return -1;
}

int pagina_ancora(pagina_t *p, const char *nome)
{
    for (int i = 0; i < p->nancoras; i++) if (compara_texto(p->ancoras[i].nome, nome) == 0) return p->ancoras[i].y;
    return -1;
}
