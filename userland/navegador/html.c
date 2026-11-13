/* html.c - leitura do HTML numa árvore de elementos (H61).
 *
 * O HTML da web real é cheio de erros: tags que não fecham, que fecham fora
 * de ordem, parágrafos dentro de parágrafos, '&' soltos. Este leitor nunca
 * desiste; ele segue regras simples parecidas com as dos navegadores:
 *   - elementos "vazios" (br, img, hr, input, meta...) nunca têm filhos;
 *   - um <p> aberto é fechado por qualquer bloco novo (div, h1, ul, table...);
 *   - um título (h1-h6) começando logo dentro de outro fecha o anterior;
 *   - <li> fecha o <li> anterior da mesma lista; <dt>/<dd> idem; <tr>, <td>,
 *     <th> e <option> também se fecham sozinhos;
 *   - uma tag de fechamento sobe até o elemento de mesmo nome e fecha tudo
 *     no caminho; se não houver um aberto (ou se para chegar nele tivesse de
 *     sair de uma tabela), ela é ignorada;
 *   - <script>, <style>, <textarea> e <title> guardam o texto cru até a tag
 *     de fechamento deles;
 *   - comentários, <!DOCTYPE> e <?...?> são pulados.
 * O texto é guardado em Latin-1 (o conjunto de caracteres da fonte do
 * SaguiOS): a página em UTF-8 (o padrão) ou Latin-1/Windows-1252 é
 * convertida, e as entidades (&amp;, &eacute;, &#233;, &#xE9;) também. O que
 * não existe em Latin-1 vira o parecido mais próximo (aspas curvas viram
 * retas, travessão vira hífen, reticências viram três pontos) ou '?'.
 *
 * Todos os nós e textos ficam numa "arena": blocos grandes de memória
 * liberados de uma vez quando a página sai da tela. */
#include "navegador.h"

/* ------------------------------------------------------------ arena */

#define BLOCO_ARENA (64 * 1024)

typedef struct bloco_arena {
    struct bloco_arena *ant;
    uint32_t usado, tam;
} bloco_arena_t;

void *arena_aloca(documento_t *doc, uint32_t n)
{
    n = (n + 3) & ~3u;
    bloco_arena_t *b = doc->arena;
    if (!b || b->usado + n > b->tam) {
        uint32_t tam = n > BLOCO_ARENA ? n : BLOCO_ARENA;
        bloco_arena_t *novo = aloca(sizeof(bloco_arena_t) + tam);
        if (!novo) return NULL;
        novo->ant = b;
        novo->usado = 0;
        novo->tam = tam;
        doc->arena = b = novo;
    }
    void *p = (uint8_t *)(b + 1) + b->usado;
    b->usado += n;
    memset(p, 0, n);
    return p;
}

static char *arena_texto(documento_t *doc, const char *t, uint32_t n)
{
    char *p = arena_aloca(doc, n + 1);
    if (p) { memcpy(p, t, n); p[n] = 0; }
    return p;
}

void html_liberar(documento_t *doc)
{
    if (!doc) return;
    bloco_arena_t *b = doc->arena;
    while (b) { bloco_arena_t *a = b->ant; libera(b); b = a; }
    libera(doc);
}

/* ------------------------------------------------------------ nomes das tags */

static const char *const nomes_tags[TAG_QTD] = {
    [TAG_OUTRA] = "?", [TAG_HTML] = "html", [TAG_HEAD] = "head", [TAG_BODY] = "body", [TAG_TITLE] = "title",
    [TAG_META] = "meta", [TAG_LINK] = "link", [TAG_STYLE] = "style", [TAG_SCRIPT] = "script",
    [TAG_NOSCRIPT] = "noscript", [TAG_BASE] = "base", [TAG_P] = "p", [TAG_DIV] = "div", [TAG_SPAN] = "span",
    [TAG_A] = "a", [TAG_B] = "b", [TAG_STRONG] = "strong", [TAG_I] = "i", [TAG_EM] = "em", [TAG_U] = "u",
    [TAG_BR] = "br", [TAG_HR] = "hr", [TAG_IMG] = "img", [TAG_H1] = "h1", [TAG_H2] = "h2", [TAG_H3] = "h3",
    [TAG_H4] = "h4", [TAG_H5] = "h5", [TAG_H6] = "h6", [TAG_UL] = "ul", [TAG_OL] = "ol", [TAG_LI] = "li",
    [TAG_DL] = "dl", [TAG_DT] = "dt", [TAG_DD] = "dd", [TAG_BLOCKQUOTE] = "blockquote", [TAG_PRE] = "pre",
    [TAG_CODE] = "code", [TAG_TABLE] = "table", [TAG_CAPTION] = "caption", [TAG_THEAD] = "thead",
    [TAG_TBODY] = "tbody", [TAG_TFOOT] = "tfoot", [TAG_TR] = "tr", [TAG_TD] = "td", [TAG_TH] = "th",
    [TAG_FORM] = "form", [TAG_INPUT] = "input", [TAG_BUTTON] = "button", [TAG_SELECT] = "select",
    [TAG_OPTION] = "option", [TAG_TEXTAREA] = "textarea", [TAG_LABEL] = "label", [TAG_NAV] = "nav",
    [TAG_HEADER] = "header", [TAG_FOOTER] = "footer", [TAG_SECTION] = "section", [TAG_ARTICLE] = "article",
    [TAG_ASIDE] = "aside", [TAG_MAIN] = "main", [TAG_FIGURE] = "figure", [TAG_FIGCAPTION] = "figcaption",
    [TAG_SUP] = "sup", [TAG_SUB] = "sub", [TAG_SMALL] = "small", [TAG_CENTER] = "center", [TAG_FONT] = "font",
    [TAG_TT] = "tt", [TAG_KBD] = "kbd", [TAG_SAMP] = "samp", [TAG_CITE] = "cite", [TAG_ABBR] = "abbr",
    [TAG_IFRAME] = "iframe", [TAG_SVG] = "svg", [TAG_MATH] = "math", [TAG_TEMPLATE] = "template",
    [TAG_WBR] = "wbr", [TAG_AREA] = "area", [TAG_COL] = "col", [TAG_EMBED] = "embed", [TAG_SOURCE] = "source",
    [TAG_PARAM] = "param", [TAG_TRACK] = "track", [TAG_DETAILS] = "details", [TAG_SUMMARY] = "summary",
    [TAG_ADDRESS] = "address", [TAG_VIDEO] = "video", [TAG_AUDIO] = "audio", [TAG_CANVAS] = "canvas",
    [TAG_OBJECT] = "object", [TAG_S] = "s", [TAG_STRIKE] = "strike", [TAG_DEL] = "del", [TAG_INS] = "ins",
    [TAG_MARK] = "mark", [TAG_Q] = "q", [TAG_VAR] = "var", [TAG_BIG] = "big", [TAG_COLGROUP] = "colgroup",
    [TAG_FIELDSET] = "fieldset", [TAG_LEGEND] = "legend", [TAG_PICTURE] = "picture", [TAG_TIME] = "time",
};

const char *html_nome_tag(int tag) { return tag > 0 && tag < TAG_QTD && nomes_tags[tag] ? nomes_tags[tag] : "?"; }

static int achar_tag(const char *nome)
{
    for (int i = 1; i < TAG_QTD; i++)
        if (nomes_tags[i] && compara_texto(nomes_tags[i], nome) == 0) return i;
    return TAG_OUTRA;
}

static bool vazio(int t)
{
    return t == TAG_BR || t == TAG_HR || t == TAG_IMG || t == TAG_META || t == TAG_LINK || t == TAG_INPUT ||
           t == TAG_BASE || t == TAG_WBR || t == TAG_AREA || t == TAG_COL || t == TAG_EMBED || t == TAG_SOURCE ||
           t == TAG_PARAM || t == TAG_TRACK;
}

/* Blocos que fecham um <p> aberto. */
static bool fecha_p(int t)
{
    return t == TAG_P || t == TAG_DIV || (t >= TAG_H1 && t <= TAG_H6) || t == TAG_UL || t == TAG_OL || t == TAG_DL ||
           t == TAG_BLOCKQUOTE || t == TAG_PRE || t == TAG_TABLE || t == TAG_FORM || t == TAG_HR || t == TAG_NAV ||
           t == TAG_HEADER || t == TAG_FOOTER || t == TAG_SECTION || t == TAG_ARTICLE || t == TAG_ASIDE ||
           t == TAG_MAIN || t == TAG_FIGURE || t == TAG_CENTER || t == TAG_ADDRESS || t == TAG_DETAILS ||
           t == TAG_FIELDSET || t == TAG_LI || t == TAG_DT || t == TAG_DD;
}

/* Elementos de onde uma busca para fechar algo não passa. */
static bool barreira(int t, int procurado)
{
    if (t == TAG_TABLE) return procurado != TAG_TABLE;
    if (procurado == TAG_P || procurado == TAG_LI || procurado == TAG_DT || procurado == TAG_DD)
        return t == TAG_TD || t == TAG_TH || t == TAG_BUTTON || t == TAG_OBJECT ||
               (procurado == TAG_LI && (t == TAG_UL || t == TAG_OL)) ||
               (procurado != TAG_P && t == TAG_DL) || (procurado == TAG_P && (t == TAG_BLOCKQUOTE || t == TAG_DIV ||
               t == TAG_LI || t == TAG_DD || t == TAG_SECTION || t == TAG_ARTICLE || t == TAG_ASIDE || t == TAG_NAV ||
               t == TAG_HEADER || t == TAG_FOOTER || t == TAG_MAIN || t == TAG_FIGURE || t == TAG_FORM));
    if (procurado == TAG_TD || procurado == TAG_TH || procurado == TAG_TR) return false;
    return false;
}

/* ------------------------------------------------------------ caracteres */

/* Bytes 0x80-0x9F do Windows-1252 (que muitas páginas "Latin-1" usam). */
static const uint16_t win1252[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

/* Um ponto de código Unicode vira 0 a 3 caracteres Latin-1. */
static int para_latin1(uint32_t c, char *s)
{
    if (c == 0xA0) { s[0] = ' '; return 1; }        /* espaço que não quebra: a fonte não tem, vira espaço */
    if (c >= 0x80 && c < 0xA0) c = win1252[c - 0x80];
    if (c == 0) return 0;
    if (c < 0x100) { s[0] = (char)c; return 1; }
    switch (c) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: case 0x2039: case 0x203A: case 0x02BC: s[0] = '\''; return 1;
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: s[0] = '"'; return 1;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: s[0] = '-'; return 1;
    case 0x2026: s[0] = s[1] = s[2] = '.'; return 3;
    case 0x2022: case 0x2027: case 0x2219: case 0x25CF: case 0x30FB: s[0] = (char)0xB7; return 1;
    case 0x20AC: s[0] = 'E'; s[1] = 'U'; s[2] = 'R'; return 3;
    case 0x2122: s[0] = 'T'; s[1] = 'M'; return 2;
    case 0x2190: s[0] = '<'; s[1] = '-'; return 2;
    case 0x2192: s[0] = '-'; s[1] = '>'; return 2;
    case 0x2191: s[0] = '^'; return 1;
    case 0x2193: s[0] = 'v'; return 1;
    case 0x2002: case 0x2003: case 0x2009: case 0x200A: case 0x202F: case 0x2007: case 0x2008: case 0x3000:
        s[0] = ' '; return 1;
    case 0x200B: case 0x200C: case 0x200D: case 0x200E: case 0x200F: case 0xFEFF: case 0x00AD: case 0x2060:
        return 0;
    case 0x0152: s[0] = 'O'; s[1] = 'E'; return 2;
    case 0x0153: s[0] = 'o'; s[1] = 'e'; return 2;
    case 0x0160: s[0] = 'S'; return 1;
    case 0x0161: s[0] = 's'; return 1;
    case 0x0178: s[0] = 'Y'; return 1;
    case 0x017D: s[0] = 'Z'; return 1;
    case 0x017E: s[0] = 'z'; return 1;
    case 0x0192: s[0] = 'f'; return 1;
    case 0x02C6: s[0] = '^'; return 1;
    case 0x02DC: s[0] = '~'; return 1;
    case 0x2020: s[0] = '+'; return 1;
    case 0x2248: s[0] = '~'; return 1;
    case 0x2260: s[0] = '!'; s[1] = '='; return 2;
    case 0x2264: s[0] = '<'; s[1] = '='; return 2;
    case 0x2265: s[0] = '>'; s[1] = '='; return 2;
    case 0x2116: s[0] = 'N'; s[1] = (char)0xBA; return 2;
    }
    s[0] = '?';
    return 1;
}

/* Letras acentuadas: o nome da entidade é a letra + o acento; a maiúscula e a
 * minúscula ficam a 0x20 de distância em Latin-1. */
static const struct { const char *nome; uint16_t cp; } entidades[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
    { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 }, { "hellip", 0x2026 }, { "mdash", 0x2014 },
    { "ndash", 0x2013 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
    { "sbquo", 0x201A }, { "bdquo", 0x201E }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "bull", 0x2022 },
    { "middot", 0xB7 }, { "deg", 0xB0 }, { "plusmn", 0xB1 }, { "times", 0xD7 }, { "divide", 0xF7 },
    { "euro", 0x20AC }, { "pound", 0xA3 }, { "yen", 0xA5 }, { "cent", 0xA2 }, { "sect", 0xA7 }, { "para", 0xB6 },
    { "shy", 0xAD }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "acute", 0xB4 }, { "cedil", 0xB8 }, { "uml", 0xA8 },
    { "macr", 0xAF }, { "ordf", 0xAA }, { "ordm", 0xBA }, { "sup1", 0xB9 }, { "sup2", 0xB2 }, { "sup3", 0xB3 },
    { "frac14", 0xBC }, { "frac12", 0xBD }, { "frac34", 0xBE }, { "micro", 0xB5 }, { "not", 0xAC },
    { "curren", 0xA4 }, { "brvbar", 0xA6 }, { "szlig", 0xDF }, { "yuml", 0xFF }, { "minus", 0x2212 },
    { "rarr", 0x2192 }, { "larr", 0x2190 }, { "uarr", 0x2191 }, { "darr", 0x2193 }, { "thinsp", 0x2009 },
    { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "zwj", 0x200D }, { "zwnj", 0x200C }, { "lrm", 0x200E },
    { "rlm", 0x200F }, { "prime", 0x2032 }, { "le", 0x2264 }, { "ge", 0x2265 }, { "ne", 0x2260 },
    { "asymp", 0x2248 }, { "dagger", 0x2020 }, { "oelig", 0x153 }, { "OElig", 0x152 }, { "lsaquo", 0x2039 },
    { "rsaquo", 0x203A }, { "Aring", 0xC5 }, { "aring", 0xE5 }, { "AElig", 0xC6 }, { "aelig", 0xE6 },
    { "Oslash", 0xD8 }, { "oslash", 0xF8 }, { "ETH", 0xD0 }, { "eth", 0xF0 }, { "THORN", 0xDE }, { "thorn", 0xFE },
    { "Ccedil", 0xC7 }, { "ccedil", 0xE7 }, { "Ntilde", 0xD1 }, { "ntilde", 0xF1 }, { "Yacute", 0xDD },
    { "yacute", 0xFD },
};

static uint32_t entidade_letra(const char *n, int tam)
{
    /* Vogal + grave/acute/circ/tilde/uml. */
    static const char *const acentos[5] = { "grave", "acute", "circ", "tilde", "uml" };
    static const uint8_t base[5][5] = {     /* A E I O U, por acento (0 = não existe) */
        { 0xC0, 0xC8, 0xCC, 0xD2, 0xD9 }, { 0xC1, 0xC9, 0xCD, 0xD3, 0xDA }, { 0xC2, 0xCA, 0xCE, 0xD4, 0xDB },
        { 0xC3, 0, 0, 0xD5, 0 }, { 0xC4, 0xCB, 0xCF, 0xD6, 0xDC },
    };
    if (tam < 4) return 0;
    const char *vogais = "AEIOU", *p;
    char v = n[0];
    bool minuscula = v >= 'a' && v <= 'z';
    if (minuscula) v = (char)(v - 32);
    p = acha_char(vogais, v);
    if (!p || !v) return 0;
    for (int a = 0; a < 5; a++)
        if ((int)tam_texto(acentos[a]) == tam - 1 && compara_texto_n(n + 1, acentos[a], (size_t)tam - 1) == 0) {
            uint8_t c = base[a][p - vogais];
            return c ? (uint32_t)c + (minuscula ? 0x20u : 0u) : 0;
        }
    return 0;
}

/* Lê uma entidade começando depois do '&'. Devolve quantos bytes consumiu
 * (0 = não é entidade: o '&' fica como está) e o ponto de código. */
static int ler_entidade(const char *s, uint32_t resto, uint32_t *cp)
{
    if (resto >= 2 && s[0] == '#') {
        uint32_t v = 0, i = 1;
        bool hex = s[1] == 'x' || s[1] == 'X';
        if (hex) i++;
        uint32_t inicio = i;
        for (; i < resto && i < 10; i++) {
            char c = s[i];
            if (c >= '0' && c <= '9') v = v * (hex ? 16 : 10) + (uint32_t)(c - '0');
            else if (hex && ((c | 32) >= 'a' && (c | 32) <= 'f')) v = v * 16 + (uint32_t)((c | 32) - 'a' + 10);
            else break;
        }
        if (i == inicio) return 0;
        if (i < resto && s[i] == ';') i++;
        *cp = v > 0x10FFFF ? '?' : v;
        return (int)i;
    }
    int n = 0;
    while ((uint32_t)n < resto && n < 10 && ((s[n] | 32) >= 'a' && (s[n] | 32) <= 'z')) n++;
    while ((uint32_t)n < resto && n < 10 && s[n] >= '0' && s[n] <= '9') n++;
    if (n == 0) return 0;
    /* Procura o nome mais longo que casa (alguns aparecem sem ';', como &copy). */
    for (int t = n; t >= 2; t--) {
        uint32_t c = 0;
        for (unsigned k = 0; k < sizeof entidades / sizeof entidades[0]; k++)
            if ((int)tam_texto(entidades[k].nome) == t && compara_texto_n(s, entidades[k].nome, (size_t)t) == 0) c = entidades[k].cp;
        if (!c) c = entidade_letra(s, t);
        if (c) {
            bool ponto_virgula = (uint32_t)t < resto && s[t] == ';';
            if (!ponto_virgula && t != n) continue;     /* &copyright não é &copy + "right" */
            *cp = c;
            return t + (ponto_virgula ? 1 : 0);
        }
    }
    return 0;
}

/* Converte um pedaço do documento para Latin-1, resolvendo as entidades.
 * Devolve o tamanho escrito em "saida" (que precisa de 3x o tamanho). */
static uint32_t converter(const documento_t *doc, const char *s, uint32_t n, char *saida, bool entidades_sim)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n;) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp;
        if (c == '&' && entidades_sim) {
            int t = ler_entidade(s + i + 1, n - i - 1, &cp);
            if (t > 0) { k += (uint32_t)para_latin1(cp, saida + k); i += 1 + (uint32_t)t; continue; }
            saida[k++] = '&';
            i++;
            continue;
        }
        if (c < 0x80) {
            saida[k++] = c == '\r' ? '\n' : (char)c;
            i++;
            if (c == '\r' && i < n && s[i] == '\n') i++;
            continue;
        }
        if (doc->utf8) {
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            cp = extra == 3 ? c & 7u : extra == 2 ? c & 15u : c & 31u;
            bool ok = extra > 0 && i + (uint32_t)extra < n + 0;
            for (int e = 1; ok && e <= extra; e++) {
                if (i + (uint32_t)e >= n || ((uint8_t)s[i + (uint32_t)e] & 0xC0) != 0x80) ok = false;
                else cp = (cp << 6) | ((uint8_t)s[i + (uint32_t)e] & 63u);
            }
            if (ok) { k += (uint32_t)para_latin1(cp, saida + k); i += 1 + (uint32_t)extra; continue; }
            saida[k++] = '?';                           /* UTF-8 quebrado */
            i++;
            continue;
        }
        k += (uint32_t)para_latin1(c, saida + k);
        i++;
    }
    return k;
}

/* ------------------------------------------------------------ montagem da árvore */

typedef struct {
    documento_t *doc;
    const char  *d;
    uint32_t     n, pos;
    no_t        *atual;
    char        *tmp;            /* área para converter textos (3x o maior pedaço) */
    uint32_t     tam_tmp;
} leitor_t;

static no_t *novo_no(leitor_t *l, int tipo, int tag)
{
    no_t *x = arena_aloca(l->doc, sizeof(no_t));
    if (!x) return NULL;
    x->tipo = (uint8_t)tipo;
    x->tag = (uint8_t)tag;
    x->pai = l->atual;
    if (l->atual) {
        if (l->atual->ultimo) l->atual->ultimo->prox = x;
        else l->atual->filho = x;
        l->atual->ultimo = x;
    }
    l->doc->qtd_nos++;
    return x;
}

static char *texto_convertido(leitor_t *l, const char *s, uint32_t n, uint32_t *tam, bool entidades_sim)
{
    if (3 * n + 1 > l->tam_tmp) {
        libera(l->tmp);
        l->tam_tmp = 3 * n + 1024;
        l->tmp = aloca(l->tam_tmp);
        if (!l->tmp) { l->tam_tmp = 0; return NULL; }
    }
    uint32_t k = converter(l->doc, s, n, l->tmp, entidades_sim);
    if (tam) *tam = k;
    return arena_texto(l->doc, l->tmp, k);
}

static void acrescentar_texto(leitor_t *l, const char *s, uint32_t n, bool entidades_sim)
{
    if (n == 0 || n > l->n || !l->atual) return;
    uint32_t tam;
    char *t = texto_convertido(l, s, n, &tam, entidades_sim);
    if (!t || tam == 0) return;
    no_t *ult = l->atual->ultimo;
    if (ult && ult->tipo == NO_TEXTO) {              /* junta com o texto anterior */
        char *junto = arena_aloca(l->doc, ult->tam + tam + 1);
        if (!junto) return;
        memcpy(junto, ult->texto, ult->tam);
        memcpy(junto + ult->tam, t, tam);
        ult->texto = junto;
        ult->tam += tam;
        return;
    }
    no_t *x = novo_no(l, NO_TEXTO, 0);
    if (!x) return;
    x->texto = t;
    x->tam = tam;
}

/* Fecha o elemento "tag" mais próximo, se houver um aberto sem barreira no caminho. */
static bool fechar_ate(leitor_t *l, int tag)
{
    for (no_t *x = l->atual; x && x != l->doc->raiz; x = x->pai) {
        if (x->tag == tag) { l->atual = x->pai; return true; }
        if (barreira(x->tag, tag)) return false;
    }
    return false;
}

static bool aberto(leitor_t *l, int tag)
{
    for (no_t *x = l->atual; x && x != l->doc->raiz; x = x->pai) {
        if (x->tag == tag) return true;
        if (barreira(x->tag, tag)) return false;
    }
    return false;
}

static bool eh_letra(char c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
static char minuscula(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* Procura (sem diferenciar maiúsculas) "</nome" a partir de pos. */
static uint32_t achar_fechamento(leitor_t *l, const char *nome)
{
    uint32_t t = (uint32_t)tam_texto(nome);
    for (uint32_t i = l->pos; i + 2 + t <= l->n; i++) {
        if (l->d[i] != '<' || l->d[i + 1] != '/') continue;
        uint32_t k = 0;
        while (k < t && minuscula(l->d[i + 2 + k]) == nome[k]) k++;
        if (k == t) return i;
    }
    return l->n;
}

static void ler_atributos(leitor_t *l, no_t *x, bool *auto_fechou)
{
    char buf[1024];
    uint32_t k = 0;               /* "nome\0valor\0nome\0valor\0" */
    *auto_fechou = false;
    for (;;) {
        while (l->pos < l->n && (eh_espaco(l->d[l->pos]) || l->d[l->pos] == '/')) {
            if (l->d[l->pos] == '/' && l->pos + 1 < l->n && l->d[l->pos + 1] == '>') *auto_fechou = true;
            l->pos++;
        }
        if (l->pos >= l->n || l->d[l->pos] == '>') { l->pos++; break; }
        uint32_t ini = l->pos;
        while (l->pos < l->n && !eh_espaco(l->d[l->pos]) && l->d[l->pos] != '=' && l->d[l->pos] != '>' &&
               !(l->d[l->pos] == '/' && l->pos + 1 < l->n && l->d[l->pos + 1] == '>'))
            l->pos++;
        uint32_t tam_nome = l->pos - ini;
        while (l->pos < l->n && eh_espaco(l->d[l->pos])) l->pos++;
        const char *valor = "";
        uint32_t tam_valor = 0;
        if (l->pos < l->n && l->d[l->pos] == '=') {
            l->pos++;
            while (l->pos < l->n && eh_espaco(l->d[l->pos])) l->pos++;
            char aspas = l->pos < l->n ? l->d[l->pos] : 0;
            if (aspas == '"' || aspas == '\'') {
                l->pos++;
                valor = l->d + l->pos;
                while (l->pos < l->n && l->d[l->pos] != aspas) l->pos++;
                tam_valor = (uint32_t)(l->d + l->pos - valor);
                if (l->pos < l->n) l->pos++;
            } else {
                valor = l->d + l->pos;
                while (l->pos < l->n && !eh_espaco(l->d[l->pos]) && l->d[l->pos] != '>') l->pos++;
                tam_valor = (uint32_t)(l->d + l->pos - valor);
            }
        }
        if (tam_nome == 0) { l->pos++; continue; }
        if (tam_nome > 40 || tam_valor > 600 || k + tam_nome + 3 * tam_valor + 3 > sizeof buf) continue;
        for (uint32_t i = 0; i < tam_nome; i++) buf[k++] = minuscula(l->d[ini + i]);
        buf[k++] = 0;
        k += converter(l->doc, valor, tam_valor, buf + k, true);
        buf[k++] = 0;
    }
    if (k) {
        buf[k++] = 0;
        x->atributos = arena_aloca(l->doc, k);
        if (x->atributos) memcpy(x->atributos, buf, k);
    }
}

const char *html_atributo(const no_t *x, const char *nome)
{
    if (!x || !x->atributos) return NULL;
    for (const char *p = x->atributos; *p;) {
        const char *v = p + tam_texto(p) + 1;
        if (compara_texto(p, nome) == 0) return v;
        p = v + tam_texto(v) + 1;
    }
    return NULL;
}

/* Procura charset=... num texto (meta ou Content-Type). */
static int charset_em(const char *s)
{
    const char *p = s;
    for (; *p; p++) {
        if (compara_texto_n(p, "charset", 7) == 0 || compara_texto_n(p, "CHARSET", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '=' || *p == '"' || *p == '\'') p++;
            if ((p[0] | 32) == 'u' && (p[1] | 32) == 't' && (p[2] | 32) == 'f') return 1;
            return 0;
        }
    }
    return -1;
}

static void abrir_tag(leitor_t *l)
{
    char nome[16];
    uint32_t k = 0;
    while (l->pos < l->n && (eh_letra(l->d[l->pos]) || (k > 0 && l->d[l->pos] >= '0' && l->d[l->pos] <= '9') ||
                             l->d[l->pos] == '-' || l->d[l->pos] == ':')) {
        if (k < sizeof nome - 1) nome[k++] = minuscula(l->d[l->pos]);
        l->pos++;
    }
    nome[k] = 0;
    int tag = achar_tag(nome);
    /* Fechamentos automáticos. */
    if (fecha_p(tag) && aberto(l, TAG_P)) fechar_ate(l, TAG_P);
    if (tag == TAG_LI) fechar_ate(l, TAG_LI);
    if (tag >= TAG_H1 && tag <= TAG_H6 && l->atual && l->atual->tag >= TAG_H1 && l->atual->tag <= TAG_H6) l->atual = l->atual->pai;
    if (tag == TAG_DT || tag == TAG_DD) { if (!fechar_ate(l, TAG_DT)) fechar_ate(l, TAG_DD); }
    if (tag == TAG_OPTION && l->atual && l->atual->tag == TAG_OPTION) l->atual = l->atual->pai;
    if (tag == TAG_TR) {
        for (no_t *x = l->atual; x && x->tag != TAG_TABLE; x = x->pai)
            if (x->tag == TAG_TR) { l->atual = x->pai; break; }
    }
    if (tag == TAG_TD || tag == TAG_TH) {
        for (no_t *x = l->atual; x && x->tag != TAG_TABLE && x->tag != TAG_TR; x = x->pai)
            if (x->tag == TAG_TD || x->tag == TAG_TH) { l->atual = x->pai; break; }
    }
    if ((tag == TAG_THEAD || tag == TAG_TBODY || tag == TAG_TFOOT)) {
        for (no_t *x = l->atual; x && x->tag != TAG_TABLE; x = x->pai)
            if (x->tag == TAG_THEAD || x->tag == TAG_TBODY || x->tag == TAG_TFOOT) { l->atual = x->pai; break; }
    }
    if (tag == TAG_A && aberto(l, TAG_A)) fechar_ate(l, TAG_A);
    if (tag == TAG_BODY) {                                      /* <body> fecha um <head> que ficou aberto */
        for (no_t *x = l->atual; x && x != l->doc->raiz; x = x->pai)
            if (x->tag == TAG_HEAD) { l->atual = x->pai; break; }
    }   /* link dentro de link não existe */
    no_t *x = novo_no(l, NO_ELEMENTO, tag);
    if (!x) { l->pos = l->n; return; }
    if (tag == TAG_OUTRA) x->texto = arena_texto(l->doc, nome, k);
    bool auto_fechou;
    ler_atributos(l, x, &auto_fechou);
    if (tag == TAG_META && !l->doc->charset_definido) {
        const char *c = html_atributo(x, "charset");
        int r = c ? (((c[0] | 32) == 'u') ? 1 : 0) : -1;
        const char *ct = html_atributo(x, "content");
        if (r < 0 && ct) r = charset_em(ct);
        if (r >= 0) { l->doc->utf8 = r == 1; l->doc->charset_definido = true; }
    }
    if (tag == TAG_BASE && !l->doc->base[0]) {
        const char *h = html_atributo(x, "href");
        if (h) copia_texto(l->doc->base, h, sizeof l->doc->base);
    }
    if (vazio(tag) || auto_fechou) return;
    /* Elementos de texto cru. */
    if (tag == TAG_SCRIPT || tag == TAG_STYLE || tag == TAG_TEXTAREA || tag == TAG_TITLE) {
        if (l->pos > l->n) l->pos = l->n;
        uint32_t fim = achar_fechamento(l, nome);
        l->atual = x;
        acrescentar_texto(l, l->d + l->pos, fim - l->pos, tag == TAG_TEXTAREA || tag == TAG_TITLE);
        l->atual = x->pai;
        l->pos = fim;
        while (l->pos < l->n && l->d[l->pos] != '>') l->pos++;
        l->pos++;
        if (tag == TAG_TITLE && !l->doc->titulo[0] && x->filho) {
            /* Título sem espaços repetidos. */
            uint32_t j = 0;
            bool espaco = true;
            for (uint32_t i = 0; i < x->filho->tam && j < sizeof l->doc->titulo - 1; i++) {
                char c = x->filho->texto[i];
                if (eh_espaco(c)) { if (!espaco) l->doc->titulo[j++] = ' '; espaco = true; }
                else { l->doc->titulo[j++] = c; espaco = false; }
            }
            while (j > 0 && l->doc->titulo[j - 1] == ' ') j--;
            l->doc->titulo[j] = 0;
        }
        return;
    }
    l->atual = x;
}

static void fechar_tag(leitor_t *l)
{
    char nome[16];
    uint32_t k = 0;
    while (l->pos < l->n && l->d[l->pos] != '>' && !eh_espaco(l->d[l->pos])) {
        if (k < sizeof nome - 1) nome[k++] = minuscula(l->d[l->pos]);
        l->pos++;
    }
    nome[k] = 0;
    while (l->pos < l->n && l->d[l->pos] != '>') l->pos++;
    l->pos++;
    int tag = achar_tag(nome);
    if (tag == TAG_OUTRA) {
        for (no_t *x = l->atual; x && x != l->doc->raiz; x = x->pai) {
            if (x->tag == TAG_OUTRA && x->texto && compara_texto(x->texto, nome) == 0) { l->atual = x->pai; return; }
            if (x->tag == TAG_TABLE || x->tag == TAG_TD || x->tag == TAG_TH) return;
        }
        return;
    }
    if (tag == TAG_HTML || tag == TAG_BODY) return;   /* conteúdo depois de </body> continua valendo */
    if (tag == TAG_BR) { novo_no(l, NO_ELEMENTO, TAG_BR); return; }     /* </br> funciona como <br> */
    fechar_ate(l, tag);
}

documento_t *html_ler(const char *d, uint32_t n, int charset)
{
    documento_t *doc = aloca_zerado(sizeof(documento_t));
    if (!doc) return NULL;
    leitor_t l;
    memset(&l, 0, sizeof l);
    l.doc = doc;
    l.d = d;
    l.n = n;
    /* Conjunto de caracteres: o do servidor (Content-Type), senão o <meta>,
     * senão UTF-8 se o começo for UTF-8 válido. */
    if (charset >= 0) { doc->utf8 = charset == 1; doc->charset_definido = true; }
    else doc->utf8 = true;
    if (n >= 3 && (uint8_t)d[0] == 0xEF && (uint8_t)d[1] == 0xBB && (uint8_t)d[2] == 0xBF) { l.pos = 3; doc->utf8 = true; doc->charset_definido = true; }
    if (!doc->charset_definido) {
        /* Olha o <meta charset> nos primeiros 2 KB antes de converter qualquer texto. */
        for (uint32_t i = 0; i + 8 < n && i < 2048; i++)
            if (compara_texto_n(d + i, "charset", 7) == 0 || compara_texto_n(d + i, "CHARSET", 7) == 0) {
                char amostra[24];
                uint32_t t = n - i < sizeof amostra - 1 ? n - i : sizeof amostra - 1;
                memcpy(amostra, d + i, t);
                amostra[t] = 0;
                int r = charset_em(amostra);
                if (r >= 0) { doc->utf8 = r == 1; doc->charset_definido = true; }
                break;
            }
    }
    doc->raiz = arena_aloca(doc, sizeof(no_t));
    if (!doc->raiz) { html_liberar(doc); return NULL; }
    doc->raiz->tipo = NO_ELEMENTO;
    doc->raiz->tag = TAG_OUTRA;
    doc->raiz->texto = "#documento";
    l.atual = doc->raiz;
    while (l.pos < n) {
        uint32_t ini = l.pos;
        while (l.pos < n && d[l.pos] != '<') l.pos++;
        acrescentar_texto(&l, d + ini, l.pos - ini, true);
        if (l.pos >= n) break;
        l.pos++;                                          /* o '<' */
        if (l.pos + 2 < n && d[l.pos] == '!' && d[l.pos + 1] == '-' && d[l.pos + 2] == '-') {
            l.pos += 3;
            while (l.pos + 2 < n && !(d[l.pos] == '-' && d[l.pos + 1] == '-' && d[l.pos + 2] == '>')) l.pos++;
            l.pos += 3;
        } else if (l.pos < n && (d[l.pos] == '!' || d[l.pos] == '?')) {
            while (l.pos < n && d[l.pos] != '>') l.pos++;
            l.pos++;
        } else if (l.pos < n && d[l.pos] == '/') {
            l.pos++;
            if (l.pos < n && eh_letra(d[l.pos])) fechar_tag(&l);
            else { while (l.pos < n && d[l.pos] != '>') l.pos++; l.pos++; }
        } else if (l.pos < n && eh_letra(d[l.pos])) {
            abrir_tag(&l);
        } else {
            acrescentar_texto(&l, "<", 1, false);          /* um '<' solto é texto */
        }
    }
    libera(l.tmp);
    return doc;
}

/* Texto puro (text/plain): um <pre> com tudo dentro. */
documento_t *html_texto_puro(const char *d, uint32_t n, int charset)
{
    documento_t *doc = aloca_zerado(sizeof(documento_t));
    if (!doc) return NULL;
    leitor_t l;
    memset(&l, 0, sizeof l);
    l.doc = doc;
    l.d = d;
    l.n = n;
    doc->utf8 = charset != 0;
    doc->raiz = arena_aloca(doc, sizeof(no_t));
    if (!doc->raiz) { html_liberar(doc); return NULL; }
    doc->raiz->tipo = NO_ELEMENTO;
    doc->raiz->texto = "#documento";
    l.atual = doc->raiz;
    l.atual = novo_no(&l, NO_ELEMENTO, TAG_PRE);
    if (l.atual) acrescentar_texto(&l, d, n, false);
    libera(l.tmp);
    return doc;
}
