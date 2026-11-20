/* markdown.c - Markdown formatado numa janela (veja markdown.h).
 *
 * O texto é lido em dois passos, como descreve o CommonMark (a especificação
 * do Markdown, commonmark.org): primeiro os blocos, linha a linha (títulos
 * com # ou sublinhados com === e ---, parágrafos, listas com - * + ou 1.,
 * citações com >, blocos de código entre ```, linhas ---, tabelas com |);
 * depois, dentro de cada bloco, os trechos (**negrito**, *itálico*,
 * `código`, ~~riscado~~, [links](endereço), <endereços> e \ para escapar).
 * Não é o CommonMark inteiro: é o que os textos do sistema usam.
 *
 * O resultado é uma lista de "peças" já no lugar (texto com estilo,
 * retângulo, linha), desenhada com a fonte 8x16 do sistema: negrito é a
 * letra desenhada duas vezes, um pixel ao lado; itálico, as linhas de cima
 * deslocadas para a direita; o título principal sai no dobro do tamanho. */
#include "sagui.h"
#include "markdown.h"

extern const unsigned char fonte_sagui[256][16];

enum { P_TEXTO, P_RET };
struct md_peca {
    uint8_t tipo, escala, negrito, italico, sublinha, risca;
    int x, y, l, a;      /* relativos ao começo do conteúdo */
    uint32_t cor;
    int t, n;            /* o texto, em m->letras */
    int link;            /* o endereço, em m->letras; -1 = não é link */
};

#define MARGEM 12
#define ROLAGEM 12
#define LINHA 18                 /* altura de uma linha de texto (16 + 2) */
#define C_TEXTO 0x1E2328
#define C_FRACO 0x5C6670
#define C_TITULO 0x0B6B37
#define C_LINK 0x1A5FB4
#define C_CODIGO_FUNDO 0xE9EDF0
#define C_CODIGO 0x7A2E0E
#define C_LINHA 0xD0D5DA
#define C_CITACAO 0xB9C2CA

/* ------------------------------------------------------------ peças e letras */

static int guardar(md_t *m, const char *t, int n)
{
    if (m->nl + n + 1 > m->capl) {
        int cap = m->capl ? m->capl * 2 : 8192;
        while (cap < m->nl + n + 1) cap *= 2;
        char *novo = realoca(m->letras, (size_t)cap);
        if (!novo) return -1;
        m->letras = novo; m->capl = cap;
    }
    memcpy(m->letras + m->nl, t, (size_t)n);
    int i = m->nl;
    m->nl += n;
    m->letras[m->nl++] = 0;
    return i;
}

static md_peca_t *nova_peca(md_t *m, int tipo)
{
    if (m->qtd == m->cap) {
        int cap = m->cap ? m->cap * 2 : 256;
        md_peca_t *novo = realoca(m->pecas, (size_t)cap * sizeof *novo);
        if (!novo) return NULL;
        m->pecas = novo; m->cap = cap;
    }
    md_peca_t *p = &m->pecas[m->qtd++];
    memset(p, 0, sizeof *p);
    p->tipo = (uint8_t)tipo; p->link = -1; p->escala = 1;
    return p;
}

static void retangulo(md_t *m, int x, int y, int l, int a, uint32_t cor)
{
    md_peca_t *p = nova_peca(m, P_RET);
    if (p) { p->x = x; p->y = y; p->l = l; p->a = a; p->cor = cor; }
}

/* ------------------------------------------------------------ trechos (o que vai dentro de um bloco) */

typedef struct { uint8_t negrito, italico, codigo, risca; int link; } estilo_t;
#define MAX_LETRAS 6000
static char car[MAX_LETRAS];
static uint8_t est[MAX_LETRAS];
static estilo_t estilos[256];
static int qtd_car, qtd_estilos;

static int estilo_id(estilo_t e)
{
    for (int i = 0; i < qtd_estilos; i++)
        if (!memcmp(&estilos[i], &e, sizeof e)) return i;
    if (qtd_estilos == 256) return 0;
    estilos[qtd_estilos] = e;
    return qtd_estilos++;
}

static void por(char c, estilo_t e)
{
    if (qtd_car >= MAX_LETRAS) return;
    car[qtd_car] = c;
    est[qtd_car++] = (uint8_t)estilo_id(e);
}

static bool pontuacao(char c) { return c && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && (uint8_t)c < 0x80 && c != ' '; }
static bool letra(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (uint8_t)c >= 0xC0; }

/* Acha "marca" em s[i..n) (para saber se um ** ou * tem par antes de abrir). */
static int achar(const char *s, int i, int n, const char *marca)
{
    int k = (int)tam_texto(marca);
    for (; i + k <= n; i++) {
        if (s[i] == '\\') { i++; continue; }
        if (!memcmp(s + i, marca, (size_t)k)) return i;
    }
    return -1;
}

/* Lê os trechos de s[0..n) para car/est, a partir do estilo "base". */
static void trechos(md_t *m, const char *s, int n, estilo_t base)
{
    estilo_t e = base;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n && pontuacao(s[i + 1])) { por(s[++i], e); continue; }
        if (c == '`') {
            int k = 1;
            while (i + k < n && s[i + k] == '`') k++;
            char marca[8]; int nk = k < 7 ? k : 7;
            memset(marca, '`', (size_t)nk); marca[nk] = 0;
            int f = achar(s, i + k, n, marca);
            if (f < 0) { for (int q = 0; q < k; q++) por('`', e); i += k - 1; continue; }
            estilo_t ec = e; ec.codigo = 1;
            int a = i + k, b = f;
            if (b - a >= 2 && s[a] == ' ' && s[b - 1] == ' ') { a++; b--; }
            for (int q = a; q < b; q++) por(s[q], ec);
            i = f + k - 1;
            m->cont[3]++;
            continue;
        }
        if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c) {
            char marca[3] = { c, c, 0 };
            bool abre = !e.negrito, ok = true;
            if (c == '_' && ((abre && i > 0 && letra(s[i - 1])) || (!abre && i + 2 < n && letra(s[i + 2])))) ok = false;   /* snake_case não é negrito */
            if (ok && abre && achar(s, i + 2, n, marca) < 0) ok = false;
            if (ok && abre && (i + 2 >= n || s[i + 2] == ' ')) ok = false;
            if (ok) { e.negrito = abre; if (abre) m->cont[1]++; i++; continue; }
        }
        if (c == '*' || c == '_') {
            char marca[2] = { c, 0 };
            bool abre = !e.italico, ok = true;
            if (c == '_' && ((abre && i > 0 && letra(s[i - 1])) || (!abre && i + 1 < n && letra(s[i + 1])))) ok = false;
            if (ok && abre && (i + 1 >= n || s[i + 1] == ' ' || achar(s, i + 1, n, marca) < 0)) ok = false;
            if (ok) { e.italico = abre; if (abre) m->cont[2]++; continue; }
        }
        if (c == '~' && i + 1 < n && s[i + 1] == '~') {
            if (e.risca || achar(s, i + 2, n, "~~") >= 0) { e.risca = !e.risca; i++; continue; }
        }
        if (c == '!' && i + 1 < n && s[i + 1] == '[') {
            /* imagem: fica o texto alternativo, em itálico */
            int f = achar(s, i + 2, n, "](");
            int g = f >= 0 ? achar(s, f + 2, n, ")") : -1;
            if (g >= 0) {
                estilo_t ei = e; ei.italico = 1;
                const char *pre = "[imagem: ";
                for (const char *q = pre; *q; q++) por(*q, ei);
                for (int q = i + 2; q < f; q++) por(s[q], ei);
                por(']', ei);
                i = g;
                continue;
            }
        }
        if (c == '[') {
            int f = achar(s, i + 1, n, "](");
            int g = f >= 0 ? achar(s, f + 2, n, ")") : -1;
            if (g >= 0) {
                int fim_url = f + 2;
                while (fim_url < g && s[fim_url] != ' ') fim_url++;   /* [t](url "título") */
                estilo_t el = e; el.link = guardar(m, s + f + 2, fim_url - f - 2);
                trechos(m, s + i + 1, f - i - 1, el);
                i = g;
                m->cont[4]++;
                continue;
            }
        }
        if (c == '<') {
            int f = achar(s, i + 1, n, ">");
            if (f > i + 1) {
                if (comeca_com(s + i + 1, "http://") || comeca_com(s + i + 1, "https://")) {
                    estilo_t el = e; el.link = guardar(m, s + i + 1, f - i - 1);
                    for (int q = i + 1; q < f; q++) por(s[q], el);
                    i = f;
                    m->cont[4]++;
                    continue;
                }
                if (letra(s[i + 1]) || s[i + 1] == '/') { i = f; continue; }   /* uma marca de HTML (<br>, <details>...) some */
            }
        }
        por(c, e);
    }
}

/* ------------------------------------------------------------ quebra de linhas */

/* Distribui car/est em linhas de "largura" pixels, a partir de (x, *y), e
 * cria as peças. As linhas de baixo começam em x_volta. */
static void assentar(md_t *m, int x, int x_volta, int *y, int largura, int escala, int negrito_base, uint32_t cor, int altura_linha)
{
    int lw = LETRA_L * escala;
    int limite = x_volta + largura;
    int cx = x, i = 0;
    if (qtd_car == 0) return;
    while (i < qtd_car) {
        /* uma palavra (com os espaços antes dela) */
        int a = i;
        while (i < qtd_car && car[i] == ' ') i++;
        int ini_palavra = i;
        while (i < qtd_car && car[i] != ' ') i++;
        int w = (i - a) * lw;
        if (cx + w > limite && cx > x_volta && ini_palavra < i) {
            /* não cabe: próxima linha, sem os espaços */
            *y += altura_linha;
            cx = x_volta;
            a = ini_palavra;
            w = (i - a) * lw;
        }
        /* palavra maior que a linha inteira: corta onde der */
        while (cx + w > limite && i - a > 1) {
            int cabe = (limite - cx) / lw;
            if (cabe < 1) { *y += altura_linha; cx = x_volta; continue; }
            int corte = a + cabe;
            for (int k = a; k < corte;) {
                int s = est[k], f = k;
                while (f < corte && est[f] == s) f++;
                md_peca_t *p = nova_peca(m, P_TEXTO);
                if (!p) return;
                estilo_t e = estilos[s];
                p->x = cx + (k - a) * lw; p->y = *y; p->l = (f - k) * lw; p->a = altura_linha;
                p->t = guardar(m, car + k, f - k); p->n = f - k; p->escala = (uint8_t)escala;
                p->negrito = (uint8_t)(negrito_base || e.negrito); p->italico = e.italico; p->risca = e.risca;
                p->cor = e.link >= 0 ? C_LINK : e.codigo ? C_CODIGO : cor; p->link = e.link; p->sublinha = e.link >= 0;
                k = f;
            }
            *y += altura_linha;
            cx = x_volta;
            a = corte;
            w = (i - a) * lw;
        }
        /* as peças da palavra: um pedaço por estilo */
        for (int k = a; k < i;) {
            int s = est[k], f = k;
            while (f < i && est[f] == s) f++;
            estilo_t e = estilos[s];
            int px = cx + (k - a) * lw, pl = (f - k) * lw;
            if (e.codigo) retangulo(m, px, *y - 1, pl, 18 * escala, C_CODIGO_FUNDO);
            md_peca_t *p = nova_peca(m, P_TEXTO);
            if (!p) return;
            p->x = px; p->y = *y; p->l = pl; p->a = altura_linha;
            p->t = guardar(m, car + k, f - k); p->n = f - k; p->escala = (uint8_t)escala;
            p->negrito = (uint8_t)(negrito_base || e.negrito); p->italico = e.italico; p->risca = e.risca;
            p->cor = e.link >= 0 ? C_LINK : e.codigo ? C_CODIGO : cor; p->link = e.link; p->sublinha = e.link >= 0;
            k = f;
        }
        cx += w;
    }
    *y += altura_linha;
}

static void escrever_trecho(md_t *m, const char *s, int n, int x, int x_volta, int *y, int largura, int escala, bool negrito, uint32_t cor)
{
    qtd_car = 0; qtd_estilos = 0;
    estilo_t e = { 0, 0, 0, 0, -1 };
    trechos(m, s, n, e);
    assentar(m, x, x_volta, y, largura, escala, negrito, cor, LINHA * escala);
}

/* ------------------------------------------------------------ blocos */

typedef struct { const char *s; int n; } linha_t;
#define MAX_LINHAS_DOC 6000
static linha_t linhas[MAX_LINHAS_DOC];

static int recuo(linha_t l) { int k = 0; while (k < l.n && l.s[k] == ' ') k++; return k; }
static bool vazia(linha_t l) { return recuo(l) == l.n; }

/* "- ", "* ", "+ " ou "12. " / "12) ": devolve onde começa o texto (0 = não é item). */
static int item_de_lista(linha_t l, bool *numerado, int *numero)
{
    int k = recuo(l);
    if (k + 1 < l.n && (l.s[k] == '-' || l.s[k] == '*' || l.s[k] == '+') && l.s[k + 1] == ' ') { *numerado = false; return k + 2; }
    int v = 0, d = 0;
    while (k < l.n && l.s[k] >= '0' && l.s[k] <= '9' && d < 9) { v = v * 10 + l.s[k] - '0'; k++; d++; }
    if (d && k + 1 < l.n && (l.s[k] == '.' || l.s[k] == ')') && l.s[k + 1] == ' ') { *numerado = true; *numero = v; return k + 2; }
    return 0;
}

static bool linha_horizontal(linha_t l)
{
    int k = recuo(l), q = 0;
    if (k > 3 || k >= l.n) return false;
    char c = l.s[k];
    if (c != '-' && c != '*' && c != '_') return false;
    for (; k < l.n; k++) { if (l.s[k] == c) q++; else if (l.s[k] != ' ') return false; }
    return q >= 3;
}

static bool sublinhado(linha_t l, char c)   /* "====" ou "----" de um título */
{
    int k = recuo(l);
    if (k > 3 || k >= l.n) return false;
    int q = 0;
    for (; k < l.n && l.s[k] == c; k++) q++;
    while (k < l.n && l.s[k] == ' ') k++;
    return q >= 2 && k == l.n;
}

static bool cerca(linha_t l) { int k = recuo(l); return k <= 3 && k + 2 < l.n && !memcmp(l.s + k, "```", 3); }

static bool tem_barra(linha_t l) { for (int k = 0; k < l.n; k++) if (l.s[k] == '|') return true; return false; }

static bool separador_tabela(linha_t l)
{
    int barras = 0, tracos = 0;
    for (int k = 0; k < l.n; k++) {
        char c = l.s[k];
        if (c == '|') barras++;
        else if (c == '-') tracos++;
        else if (c != ':' && c != ' ') return false;
    }
    return barras && tracos >= 3;
}

/* As células de uma linha de tabela ("| a | b |"). */
static int celulas(linha_t l, linha_t *c, int max)
{
    int i = 0, n = l.n, k = 0;
    while (i < n && l.s[i] == ' ') i++;
    if (i < n && l.s[i] == '|') i++;
    while (n > i && l.s[n - 1] == ' ') n--;
    if (n > i && l.s[n - 1] == '|' && (n < 2 || l.s[n - 2] != '\\')) n--;
    while (i <= n && k < max) {
        int f = i;
        while (f < n && !(l.s[f] == '|' && (f == 0 || l.s[f - 1] != '\\'))) f++;
        int a = i, b = f;
        while (a < b && l.s[a] == ' ') a++;
        while (b > a && l.s[b - 1] == ' ') b--;
        c[k].s = l.s + a; c[k].n = b - a; k++;
        i = f + 1;
        if (f >= n) break;
    }
    return k;
}

/* Junta as linhas [a, b) de um parágrafo num texto só (quebra dura com
 * dois espaços no fim vira um espaço também: as linhas são curtas). */
static char junto[MAX_LETRAS];
static int juntar(int a, int b, int tira)
{
    int k = 0;
    for (int i = a; i < b; i++) {
        linha_t l = linhas[i];
        int ini = i == a ? 0 : recuo(l);
        if (i == a) ini = tira;
        else if (ini > l.n) ini = l.n;
        int n = l.n;
        while (n > ini && l.s[n - 1] == ' ') n--;
        if (k && k < MAX_LETRAS - 1) junto[k++] = ' ';
        for (int q = ini; q < n && k < MAX_LETRAS - 1; q++) junto[k++] = l.s[q];
    }
    junto[k] = 0;
    return k;
}

static void tabela(md_t *m, int a, int b, int x0, int *y, int largura)
{
    linha_t cab[16], cel[16];
    int nc = celulas(linhas[a], cab, 16);
    if (nc < 1) return;
    /* largura natural de cada coluna (o texto mais longo, até um limite) */
    int nat[16];
    for (int c = 0; c < nc; c++) nat[c] = cab[c].n;
    for (int r = a + 2; r < b; r++) {
        int k = celulas(linhas[r], cel, 16);
        for (int c = 0; c < nc && c < k; c++) if (cel[c].n > nat[c]) nat[c] = cel[c].n;
    }
    int col[16], soma = 0;
    for (int c = 0; c < nc; c++) { col[c] = (nat[c] < 3 ? 3 : nat[c] > 60 ? 60 : nat[c]) * LETRA_L + 16; soma += col[c]; }
    if (soma > largura) {   /* não cabe: cada coluna encolhe na proporção (mínimo 5 letras) */
        int resto = largura;
        for (int c = 0; c < nc; c++) {
            int w = (int)(col[c] * largura / soma);
            if (w < 5 * LETRA_L + 16) w = 5 * LETRA_L + 16;
            col[c] = w;
            resto -= w;
        }
        if (resto < 0) col[nc - 1] += resto;
    }
    int total = 0;
    for (int c = 0; c < nc; c++) total += col[c];
    for (int r = a; r < b; r++) {
        if (r == a + 1) continue;   /* o separador */
        int k = r == a ? nc : celulas(linhas[r], cel, 16);
        linha_t *cs = r == a ? cab : cel;
        int y0 = *y, ymax = *y, x = x0;
        if (r == a) retangulo(m, x0, y0, total, LINHA + 8, 0xF0F3F5);   /* o fundo do cabeçalho; a altura é corrigida abaixo */
        int ret_cab = r == a ? m->qtd - 1 : -1;
        for (int c = 0; c < nc; c++) {
            int yy = y0 + 4;
            if (c < k) {
                /* numa célula, \| é a barra (até dentro de `código`) */
                static char cel_txt[1024];
                int q = 0;
                for (int z = 0; z < cs[c].n && q < 1023; z++) {
                    if (cs[c].s[z] == '\\' && z + 1 < cs[c].n && cs[c].s[z + 1] == '|') continue;
                    cel_txt[q++] = cs[c].s[z];
                }
                escrever_trecho(m, cel_txt, q, x + 8, x + 8, &yy, col[c] - 16, 1, r == a, C_TEXTO);
            }
            else yy += LINHA;
            if (yy + 4 > ymax) ymax = yy + 4;
            x += col[c];
            m->cont[6]++;
        }
        if (ret_cab >= 0) m->pecas[ret_cab].a = ymax - y0;
        /* as linhas da grade */
        retangulo(m, x0, y0, total, 1, C_LINHA);
        x = x0;
        for (int c = 0; c <= nc; c++) { retangulo(m, x, y0, 1, ymax - y0, C_LINHA); if (c < nc) x += col[c]; }
        *y = ymax;
    }
    retangulo(m, x0, *y, total, 1, C_LINHA);
    *y += 1;
}

static void montar_pecas(md_t *m)
{
    m->qtd = 0; m->nl = 0; m->titulo[0] = 0;
    memset(m->cont, 0, sizeof m->cont);
    int largura = m->l - ROLAGEM - 2 * MARGEM;
    if (largura < 80) largura = 80;
    /* as linhas do texto */
    int nlin = 0;
    for (int i = 0; i < m->n && nlin < MAX_LINHAS_DOC;) {
        int f = i;
        while (f < m->n && m->fonte[f] != '\n') f++;
        int n = f - i;
        if (n && m->fonte[i + n - 1] == '\r') n--;
        linhas[nlin].s = m->fonte + i; linhas[nlin].n = n; nlin++;
        i = f + 1;
    }
    int y = MARGEM;
    if (m->puro) {
        /* texto puro: cada linha como está, quebrada se for larga */
        for (int i = 0; i < nlin; i++) {
            qtd_car = 0; qtd_estilos = 0;
            estilo_t e = { 0, 0, 0, 0, -1 };
            for (int k = 0; k < linhas[i].n; k++) por(linhas[i].s[k] == '\t' ? ' ' : linhas[i].s[k], e);
            if (!qtd_car) { y += LINHA; continue; }
            int r = recuo(linhas[i]) * LETRA_L;
            assentar(m, MARGEM, MARGEM + (r < largura / 2 ? r : 0), &y, largura, 1, 0, C_TEXTO, LINHA);
        }
        m->altura = y + MARGEM;
        return;
    }
    bool primeiro = true;
    for (int i = 0; i < nlin;) {
        linha_t l = linhas[i];
        if (vazia(l)) { i++; continue; }
        int k = recuo(l);
        /* bloco de código entre ``` */
        if (cerca(l)) {
            int f = i + 1;
            while (f < nlin && !cerca(linhas[f])) f++;
            int y0 = y;
            retangulo(m, MARGEM, y0, largura, 1, C_CODIGO_FUNDO);   /* altura corrigida abaixo */
            int fundo = m->qtd - 1;
            y += 8;
            for (int q = i + 1; q < f; q++) {
                qtd_car = 0; qtd_estilos = 0;
                estilo_t e = { 0, 0, 0, 0, -1 };
                for (int c = 0; c < linhas[q].n; c++) por(linhas[q].s[c] == '\t' ? ' ' : linhas[q].s[c], e);
                if (!qtd_car) { y += LINHA; continue; }
                assentar(m, MARGEM + 10, MARGEM + 10, &y, largura - 20, 1, 0, C_TEXTO, LINHA);
            }
            y += 8;
            m->pecas[fundo].a = y - y0;
            y += 10;
            m->cont[3]++;
            i = f + 1;
            primeiro = false;
            continue;
        }
        /* título com # */
        if (k <= 3 && l.s[k] == '#') {
            int nivel = 0;
            while (k + nivel < l.n && l.s[k + nivel] == '#') nivel++;
            if (nivel <= 6 && (k + nivel == l.n || l.s[k + nivel] == ' ')) {
                int a = k + nivel, b = l.n;
                while (a < b && l.s[a] == ' ') a++;
                while (b > a && (l.s[b - 1] == '#' || l.s[b - 1] == ' ')) b--;
                if (!primeiro) y += nivel <= 2 ? 14 : 10;
                if (!m->titulo[0]) { int c = b - a < 79 ? b - a : 79; memcpy(m->titulo, l.s + a, (size_t)c); m->titulo[c] = 0; }
                escrever_trecho(m, l.s + a, b - a, MARGEM, MARGEM, &y, largura, nivel == 1 ? 2 : 1, true, nivel <= 2 ? C_TITULO : nivel <= 3 ? C_TEXTO : C_FRACO);
                if (nivel <= 2) { y += 2; retangulo(m, MARGEM, y, largura, 1, C_LINHA); y += 6; }
                else y += 4;
                m->cont[0]++;
                i++;
                primeiro = false;
                continue;
            }
        }
        /* linha horizontal */
        if (linha_horizontal(l)) {
            y += 6; retangulo(m, MARGEM, y, largura, 1, C_LINHA); y += 10;
            i++;
            continue;
        }
        /* citação */
        if (k <= 3 && l.s[k] == '>') {
            int f = i, y0 = y;
            static char cit[MAX_LETRAS];
            int nc = 0;
            while (f < nlin && !vazia(linhas[f])) {
                linha_t q = linhas[f];
                int r = recuo(q), ini = r < q.n && q.s[r] == '>' ? r + 1 : r;
                if (ini < q.n && q.s[ini] == ' ') ini++;
                if (nc && nc < MAX_LETRAS - 1) cit[nc++] = ' ';
                for (int c = ini; c < q.n && nc < MAX_LETRAS - 1; c++) cit[nc++] = q.s[c];
                f++;
            }
            escrever_trecho(m, cit, nc, MARGEM + 16, MARGEM + 16, &y, largura - 16, 1, false, C_FRACO);
            retangulo(m, MARGEM + 2, y0 - 2, 3, y - y0 + 2, C_CITACAO);
            y += 8;
            m->cont[7]++;
            i = f;
            primeiro = false;
            continue;
        }
        /* tabela: uma linha com | seguida do separador */
        if (i + 1 < nlin && tem_barra(l) && separador_tabela(linhas[i + 1])) {
            int f = i + 2;
            while (f < nlin && !vazia(linhas[f]) && tem_barra(linhas[f])) f++;
            tabela(m, i, f, MARGEM, &y, largura);
            y += 10;
            i = f;
            primeiro = false;
            continue;
        }
        /* lista */
        bool num; int numero = 0;
        int ini = item_de_lista(l, &num, &numero);
        if (ini) {
            int nivel = k / 2;
            if (nivel > 6) nivel = 6;
            int f = i + 1;
            /* as linhas seguintes mais recuadas que o marcador são a continuação do item */
            while (f < nlin && !vazia(linhas[f])) {
                linha_t q = linhas[f];
                bool n2; int v2;
                int r = recuo(q);
                if (item_de_lista(q, &n2, &v2) || cerca(q) || linha_horizontal(q)) break;
                if (r <= 3 && (q.s[r] == '#' || q.s[r] == '>')) break;
                f++;   /* continuação (recuada ou não: a "preguiçosa" do CommonMark) */
            }
            int x = MARGEM + 4 + nivel * 22;
            if (num) {
                char t[12];
                int nt = formatar_texto(t, sizeof t, "%d.", numero);
                md_peca_t *p = nova_peca(m, P_TEXTO);
                if (p) { p->x = x; p->y = y; p->l = nt * LETRA_L; p->a = LINHA; p->t = guardar(m, t, nt); p->n = nt; p->cor = C_FRACO; }
            } else retangulo(m, x + 4, y + 7, 5, 5, nivel ? C_FRACO : C_TEXTO);
            int n = juntar(i, f, ini);
            escrever_trecho(m, junto, n, x + 22, x + 22, &y, largura - (x + 22 - MARGEM), 1, false, C_TEXTO);
            y += 3;
            /* espaço depois da lista inteira, não de cada item */
            bool n2; int v2;
            if (!(f < nlin && item_de_lista(linhas[f], &n2, &v2))) y += 6;
            m->cont[5]++;
            i = f;
            primeiro = false;
            continue;
        }
        /* parágrafo (ou título sublinhado com === / ---) */
        int f = i + 1;
        while (f < nlin && !vazia(linhas[f])) {
            linha_t q = linhas[f];
            bool n2; int v2;
            if (sublinhado(q, '=') || sublinhado(q, '-') || cerca(q) || linha_horizontal(q) || item_de_lista(q, &n2, &v2)) break;
            int r = recuo(q);
            if (r <= 3 && (q.s[r] == '#' || q.s[r] == '>')) break;
            f++;
        }
        if (f < nlin && (sublinhado(linhas[f], '=') || (sublinhado(linhas[f], '-') && f == i + 1))) {
            int nivel = sublinhado(linhas[f], '=') ? 1 : 2;
            int n = juntar(i, f, k);
            if (!primeiro) y += 14;
            if (!m->titulo[0]) { int c = n < 79 ? n : 79; memcpy(m->titulo, junto, (size_t)c); m->titulo[c] = 0; }
            escrever_trecho(m, junto, n, MARGEM, MARGEM, &y, largura, nivel == 1 ? 2 : 1, true, C_TITULO);
            y += 2; retangulo(m, MARGEM, y, largura, 1, C_LINHA); y += 6;
            m->cont[0]++;
            i = f + 1;
            primeiro = false;
            continue;
        }
        int n = juntar(i, f, k);
        escrever_trecho(m, junto, n, MARGEM, MARGEM, &y, largura, 1, false, C_TEXTO);
        y += 8;
        i = f;
        primeiro = false;
    }
    m->altura = y + MARGEM;
}

/* ------------------------------------------------------------ desenho */

static void letra_px(jan_t *j, int x, int y, unsigned char ch, const md_peca_t *p, int c0, int c1, int c2, int c3)
{
    const unsigned char *g = fonte_sagui[ch];
    int e = p->escala;
    for (int lin = 0; lin < 16; lin++) {
        uint8_t b = g[lin];
        if (!b) continue;
        int inc = p->italico ? (15 - lin) / 4 - 1 : 0;
        for (int col = 0; col < 8; col++) {
            if (!(b & (0x80 >> col))) continue;
            for (int dy = 0; dy < e; dy++) {
                int py = y + lin * e + dy;
                if (py < c1 || py >= c3) continue;
                for (int dx = 0; dx < e + (p->negrito ? 1 : 0); dx++) {
                    int px = x + col * e + dx + inc * e;
                    if (px < c0 || px >= c2) continue;
                    j->px[py * j->larg + px] = p->cor;
                }
            }
        }
    }
}

void md_desenhar(jan_t *j, md_t *m)
{
    int c0 = m->x, c1 = m->y, c2 = m->x + m->l - ROLAGEM, c3 = m->y + m->a;
    if (c2 > j->larg) c2 = j->larg;
    if (c3 > j->alt) c3 = j->alt;
    g_retangulo(j, m->x, m->y, m->l, m->a, COR_BRANCO);
    for (int i = 0; i < m->qtd; i++) {
        md_peca_t *p = &m->pecas[i];
        int y = m->y + p->y - m->topo, x = m->x + p->x;
        if (y >= c3 || y + p->a <= c1) continue;
        if (p->tipo == P_RET) {
            int ya = y < c1 ? c1 : y, yb = y + p->a > c3 ? c3 : y + p->a;
            int xa = x < c0 ? c0 : x, xb = x + p->l > c2 ? c2 : x + p->l;
            for (int yy = ya; yy < yb; yy++) for (int xx = xa; xx < xb; xx++) j->px[yy * j->larg + xx] = p->cor;
            continue;
        }
        const char *t = m->letras + p->t;
        int lw = LETRA_L * p->escala;
        for (int k = 0; k < p->n; k++) letra_px(j, x + k * lw, y, (unsigned char)t[k], p, c0, c1, c2, c3);
        int base = y + 15 * p->escala;
        if (p->sublinha && base >= c1 && base < c3)
            for (int xx = x; xx < x + p->l && xx < c2; xx++) if (xx >= c0) j->px[base * j->larg + xx] = p->cor;
        int meio = y + 8 * p->escala;
        if (p->risca && meio >= c1 && meio < c3)
            for (int xx = x; xx < x + p->l && xx < c2; xx++) if (xx >= c0) j->px[meio * j->larg + xx] = p->cor;
    }
    /* a barra de rolagem */
    int bx = m->x + m->l - ROLAGEM;
    g_retangulo(j, bx, m->y, ROLAGEM, m->a, 0xF2F4F6);
    if (m->altura > m->a) {
        int h = (int)(m->a * m->a / m->altura);
        if (h < 24) h = 24;
        int ya = m->y + (int)(m->topo * (m->a - h) / (m->altura - m->a));
        g_ret_redondo(j, bx + 3, ya + 2, ROLAGEM - 6, h - 4, 3, 0xB8C0C8, 0xB8C0C8);
    }
    g_moldura(j, m->x - 1, m->y - 1, m->l + 2, m->a + 2, 0xC8CED4);
}

/* ------------------------------------------------------------ uso */

bool md_iniciar(md_t *m, int x, int y, int l, int a)
{
    memset(m, 0, sizeof *m);
    m->x = x; m->y = y; m->l = l; m->a = a;
    return true;
}

void md_definir(md_t *m, const char *texto, int n, bool puro)
{
    libera(m->fonte);
    m->fonte = aloca((size_t)n + 1);
    if (!m->fonte) { m->n = 0; return; }
    memcpy(m->fonte, texto, (size_t)n);
    m->fonte[n] = 0;
    m->n = n;
    m->puro = puro;
    m->topo = 0;
    montar_pecas(m);
}

void md_mudar_tamanho(md_t *m, int l, int a)
{
    int alt_antes = m->altura;
    m->l = l; m->a = a;
    montar_pecas(m);
    if (alt_antes > 0) m->topo = (int)((long)m->topo * m->altura / alt_antes);
    int max = m->altura - m->a;
    if (m->topo > max) m->topo = max;
    if (m->topo < 0) m->topo = 0;
}

void md_fim(md_t *m)
{
    libera(m->fonte); libera(m->pecas); libera(m->letras);
    m->fonte = NULL; m->pecas = NULL; m->letras = NULL;
}

static bool rolar(md_t *m, int d)
{
    int antes = m->topo, max = m->altura - m->a;
    m->topo += d;
    if (m->topo > max) m->topo = max;
    if (m->topo < 0) m->topo = 0;
    return m->topo != antes;
}

bool md_tratar(jan_t *j, md_t *m, const evento_t *e)
{
    bool mudou = false, tratou = false;
    if (e->tipo == EV_TECLA) {
        int pagina = m->a - 40;
        switch (e->tecla) {
        case TECLA_CIMA: mudou = rolar(m, -LINHA * 2); tratou = true; break;
        case TECLA_BAIXO: mudou = rolar(m, LINHA * 2); tratou = true; break;
        case TECLA_PGUP: mudou = rolar(m, -pagina); tratou = true; break;
        case TECLA_PGDN: case ' ': mudou = rolar(m, pagina); tratou = true; break;
        case TECLA_HOME: mudou = rolar(m, -m->altura); tratou = true; break;
        case TECLA_END: mudou = rolar(m, m->altura); tratou = true; break;
        default:
            if ((e->tecla == 3 || e->tecla == 'c') && (e->modificadores & 2)) {   /* Ctrl+C: o texto todo */
                area_copia_gravar(m->fonte, (unsigned)m->n);
                tratou = true;
            }
        }
    } else if (e->tipo == EV_CLIQUE && (e->botoes & 1) && e->x >= m->x && e->x < m->x + m->l && e->y >= m->y && e->y < m->y + m->a) {
        tratou = true;
        if (e->x >= m->x + m->l - ROLAGEM) {
            if (m->altura > m->a) {
                int h = (int)(m->a * m->a / m->altura);
                if (h < 24) h = 24;
                int ya = m->y + (int)(m->topo * (m->a - h) / (m->altura - m->a));
                if (e->y >= ya && e->y < ya + h) { m->arrastando = true; m->arr_y0 = e->y; m->arr_topo0 = m->topo; }
                else mudou = rolar(m, e->y < ya ? -(m->a - 40) : m->a - 40);
            }
        } else {
            /* um link: abre no Cipó */
            int px = e->x - m->x, py = e->y - m->y + m->topo;
            for (int i = 0; i < m->qtd; i++) {
                md_peca_t *p = &m->pecas[i];
                if (p->link < 0 || px < p->x || px >= p->x + p->l || py < p->y || py >= p->y + p->a) continue;
                char *url = m->letras + p->link;
                registrar("markdown: link %s", url);
                if (comeca_com(url, "http://") || comeca_com(url, "https://")) {
                    char *argv[] = { "navegador", url, NULL };
                    executar("/bin/navegador", argv);
                }
                break;
            }
        }
    } else if (e->tipo == EV_MOVER && m->arrastando) {
        if (!(e->botoes & 1)) m->arrastando = false;
        else {
            int h = (int)(m->a * m->a / (m->altura ? m->altura : 1));
            if (h < 24) h = 24;
            if (m->a - h > 0) {
                int alvo = m->arr_topo0 + (int)((e->y - m->arr_y0) * (m->altura - m->a) / (m->a - h));
                mudou = rolar(m, alvo - m->topo);
            }
            tratou = true;
        }
    } else if (e->tipo == EV_SOLTOU) m->arrastando = false;
    if (mudou) { md_desenhar(j, m); jan_mostrar_area(j, m->x - 1, m->y - 1, m->l + 2, m->a + 2); }
    return tratou;
}

void md_resumo(const md_t *m, char *saida, int max)
{
    formatar_texto(saida, (size_t)max, "titulos %d, negrito %d, italico %d, codigo %d, links %d, itens %d, celulas %d, citacoes %d, altura %d",
                   m->cont[0], m->cont[1], m->cont[2], m->cont[3], m->cont[4], m->cont[5], m->cont[6], m->cont[7], m->altura);
}
