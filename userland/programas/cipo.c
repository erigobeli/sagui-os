/* cipo [endereço] - o Cipó, navegador do SaguiOS (D033).
 *
 * O nome: o sagui atravessa a mata pulando de cipó em cipó; o navegador
 * atravessa a web pulando de link em link.
 *
 * Baixa a página por HTTP (libsagui, http.c), lê o HTML e diagrama o texto
 * na janela: títulos, parágrafos, listas, negrito, links clicáveis, linhas
 * e formulários simples (uma caixa de texto, como numa busca). Não roda
 * JavaScript, não carrega imagens nem CSS: a página aparece como texto
 * organizado, do jeito dos primeiros navegadores. HTTPS fica de fora (não
 * há TLS no SaguiOS); os sites que ainda falam HTTP abrem.
 *
 * Como funciona:
 *   1. o corpo inteiro é baixado para a memória (até 1 MB) e convertido de
 *      UTF-8 para Latin-1, a codificação do SaguiOS;
 *   2. o leitor de HTML anda pelo texto uma vez, e cada palavra vira um
 *      "segmento" já com linha, coluna e estilo (a diagramação é feita na
 *      hora, para a largura fixa da janela);
 *   3. desenhar é só pintar os segmentos das linhas visíveis. */
#include "sagui.h"
#include "gui.h"
#include "motor.h"

static motor_t *motor;           /* motor de páginas novo: pinta e trata cliques */
static int motor_altura;
/* Rolagem em pixels (página do motor); a página simples rola por linhas (topo). */
static int rol_px;
static bool arrastando_barra; static int barra_pega;   /* arrastar o polegar da barra */

#define LARG_INICIO 860
#define ALT_INICIO  600
#define LARG      (j->larg)
#define ALT       (j->alt)
#define TOPO      40
#define RODAPE    22
#define AREA_A    (ALT - TOPO - RODAPE)
#define ROLAGEM   14
#define COLUNA_MAX 840   /* em tela cheia, o texto fica numa coluna central */
#define LARG_TEXTO (LARG - ROLAGEM - 24 > COLUNA_MAX ? COLUNA_MAX : LARG - ROLAGEM - 24)
#define MARGEM    ((LARG - ROLAGEM - LARG_TEXTO) / 2)
#define LINHA_A   18
#define VISIVEIS  ((AREA_A - 8) / LINHA_A)
#define URL_MAX   600
#define PAGINA_MAX (4 * 1024 * 1024)

/* Estilos (bits) */
#define E_NEGRITO 1
#define E_LINK    2
#define E_TITULO  4
#define E_ITALICO 8
#define E_PRE     16
#define E_CAMPO   32
#define E_LINHA   64   /* linha horizontal (<hr>) */
#define E_IMAGEM  128  /* "n" é o índice em imagens[] */
#define E_GRANDE  256  /* letra em dobro: <h1> e <h2> */
#define E_LINKCOR 512  /* link com a cor do CSS */
#define E_SEMLINHA 1024 /* link sem sublinhado */
#define MAX_IMAGENS 40
#define IMAGEM_ALT_MAX 420
#define IMAGEM_BYTES_MAX (6 * 1024 * 1024)

typedef struct {
    int     linha;
    int16_t x, n;
    uint16_t estilo;
    int16_t alvo;        /* link ou campo de formulário; -1 = nenhum */
    int     texto;       /* deslocamento em "textos" */
    uint32_t cor;        /* 0 = cor do estilo; senão cor + 1 */
} seg_t;

enum { A_LINK = 1, A_CAMPO };
typedef struct { int tipo; int url; int form; int nome; } alvo_t;
typedef struct { int acao; char fixa[300]; int campo; } form_t;   /* campo = nome do 1º campo de texto */

typedef struct { uint32_t *px; int l, a; } imagem_t;
static imagem_t imagens[MAX_IMAGENS];
static int qtd_imagens;
static bool pagina_segura, substituir;
static unsigned inicio_pagina;
static char titulo_js[96], alerta_js[120], nota_busca[80];

static seg_t  *segs;   static int qtd_segs, cap_segs;
static alvo_t *alvos;  static int qtd_alvos, cap_alvos;
static char   *textos; static int qtd_textos, cap_textos;
static form_t  forms[8];
static int     qtd_forms;
static int     linhas, topo;
static char    titulo[96];
static char    url_atual[URL_MAX];
static char    historico[16][URL_MAX];
static int     qtd_historico;

static jan_t  *j;
static caixa_t endereco;
static botao_t bt_voltar = { 8, 7, 70, 26, "Voltar", false };
static botao_t bt_inicio = { 84, 7, 70, 26, "Início", false };
static botao_t bt_ir = { LARG_INICIO - 50, 7, 42, 26, "Ir", false };
static char rodape_texto[128];

/* ------------------------------------------------------------ memória */

static bool crescer(void **p, int *cap, int precisa, int tam_item)
{
    if (precisa <= *cap) return true;
    int nova = *cap ? *cap * 2 : 1024;
    while (nova < precisa) nova *= 2;
    void *q = realoca(*p, (size_t)nova * (size_t)tam_item);
    if (!q) return false;
    *p = q;
    *cap = nova;
    return true;
}

static int guardar_texto(const char *t, int n)
{
    if (!crescer((void **)&textos, &cap_textos, qtd_textos + n + 1, 1)) return 0;
    int k = qtd_textos;
    memcpy(textos + k, t, (size_t)n);
    textos[k + n] = 0;
    qtd_textos += n + 1;
    return k;
}

/* ------------------------------------------------------------ texto */

/* Um caractere Unicode -> Latin-1 (o que não existe vira algo parecido). */
static char para_latin1(uint32_t c)
{
    if (c < 0x80 || (c >= 0xA0 && c < 0x100)) return c == 0xA0 ? ' ' : (char)c;
    switch (c) {
    /* Nunca aspas ASCII: o texto pode estar dentro de um script. */
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return (char)0xB4;
    case 0x201C: case 0x201E: case 0x2033: return (char)0xAB;
    case 0x201D: return (char)0xBB;
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2022: case 0x2026: case 0x00B7: return (char)0xB7;
    case 0x20AC: return 'E';
    default: return '?';
    }
}

/* O motor trabalha em UTF-8. Corpo em UTF-8 válido fica como está; senão é
 * Latin-1 (ou Windows-1252) e vira UTF-8 num bloco novo. */
static char *converter_corpo(char *s, int *tam)
{
    int n = *tam, cresce = 0;
    bool valido = true;
    for (int i = 0; i < n;) {
        uint32_t cp; int k = utf8_ler(s + i, n - i, &cp);
        if (k == 1 && (uint8_t)s[i] >= 0x80) { valido = false; break; }
        i += k;
    }
    if (valido) return s;
    for (int i = 0; i < n; i++) if ((uint8_t)s[i] >= 0x80) cresce += (uint8_t)s[i] >= 0x80 && (uint8_t)s[i] < 0xA0 ? 2 : 1;
    char *d = aloca((size_t)n + (size_t)cresce + 1);
    if (!d) return s;
    static const uint16_t cp1252[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F,
                                         0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 };
    int k = 0;
    for (int i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        k += utf8_por(d + k, c >= 0x80 && c < 0xA0 ? cp1252[c - 0x80] : c);
    }
    d[k] = 0;
    libera(s);
    *tam = k;
    return d;
}

/* Fronteira com a interface do SaguiOS (fonte Latin-1): UTF-8 -> Latin-1. */
static void utf8_para_sistema(char *s)
{
    int k = 0, n = (int)tam_texto(s);
    for (int i = 0; i < n;) {
        uint32_t cp; i += utf8_ler(s + i, n - i, &cp);
        s[k++] = cp < 0x100 ? (char)cp : '?';
    }
    s[k] = 0;
}


static bool letra(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static char minuscula(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool igual_sem_caixa(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) if (minuscula(a[i]) != minuscula(b[i])) return false;
    return true;
}

/* "&amp;", "&eacute;", "&#233;", "&#xE9;" -> um caractere. Devolve quantos
 * bytes da entrada usou (0 = não era uma entidade). */
static int entidade(const char *s, int n, char *saida)
{
    int fim = 1;
    while (fim < n && fim < 12 && s[fim] != ';' && s[fim] != '<' && !eh_espaco(s[fim])) fim++;
    if (fim >= n || s[fim] != ';' || fim < 2) return 0;
    const char *nome = s + 1;
    int t = fim - 1;
    if (nome[0] == '#') {
        uint32_t v = 0;
        bool hexa = t > 1 && (nome[1] == 'x' || nome[1] == 'X');
        for (int i = hexa ? 2 : 1; i < t; i++) {
            char c = minuscula(nome[i]);
            int d = c >= '0' && c <= '9' ? c - '0' : hexa && c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0) return 0;
            v = v * (hexa ? 16 : 10) + (uint32_t)d;
        }
        *saida = para_latin1(v);
        return fim + 1;
    }
    static const struct { const char *n; uint8_t c; } nomes[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' },
        { "copy", 0xA9 }, { "reg", 0xAE }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "middot", 0xB7 },
        { "bull", 0xB7 }, { "hellip", 0xB7 }, { "mdash", '-' }, { "ndash", '-' }, { "lsquo", '\'' },
        { "rsquo", '\'' }, { "ldquo", '"' }, { "rdquo", '"' }, { "deg", 0xB0 }, { "ordm", 0xBA },
        { "ordf", 0xAA }, { "szlig", 0xDF }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "euro", 'E' },
    };
    for (unsigned i = 0; i < sizeof nomes / sizeof nomes[0]; i++)
        if ((int)tam_texto(nomes[i].n) == t && memcmp(nomes[i].n, nome, (size_t)t) == 0) { *saida = (char)nomes[i].c; return fim + 1; }
    /* Letra acentuada: "a" + "acute", "C" + "cedil"... */
    static const struct { const char *suf; const char *letras; uint8_t base[6]; } acentos[] = {
        { "acute", "AEIOUY", { 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xDD } },
        { "grave", "AEIOU", { 0xC0, 0xC8, 0xCC, 0xD2, 0xD9 } },
        { "circ", "AEIOU", { 0xC2, 0xCA, 0xCE, 0xD4, 0xDB } },
        { "tilde", "ANO", { 0xC3, 0xD1, 0xD5 } },
        { "uml", "AEIOU", { 0xC4, 0xCB, 0xCF, 0xD6, 0xDC } },
        { "cedil", "C", { 0xC7 } },
    };
    for (unsigned i = 0; i < sizeof acentos / sizeof acentos[0]; i++) {
        int ts = (int)tam_texto(acentos[i].suf);
        if (t != ts + 1 || memcmp(nome + 1, acentos[i].suf, (size_t)ts) != 0) continue;
        char l = nome[0];
        bool minusc = l >= 'a' && l <= 'z';
        char L = minusc ? (char)(l - 32) : l;
        for (int k = 0; acentos[i].letras[k]; k++)
            if (acentos[i].letras[k] == L) { *saida = (char)(acentos[i].base[k] + (minusc ? 0x20 : 0)); return fim + 1; }
    }
    return 0;
}

/* Copia s[0..n) para saida, trocando as entidades. */
static int decodificar(const char *s, int n, char *saida, int max)
{
    int k = 0;
    for (int i = 0; i < n && k < max - 1;) {
        int u = s[i] == '&' ? entidade(s + i, n - i, &saida[k]) : 0;
        if (u) { k++; i += u; }
        else saida[k++] = s[i++];
    }
    saida[k] = 0;
    return k;
}

/* ------------------------------------------------------------ endereços */

/* Junta um endereço relativo ao da página. false = não dá para seguir
 * (javascript:, mailto:, só "#trecho"). */
static bool juntar_url(const char *base, const char *rel, char *saida, int max)
{
    while (*rel == ' ' || *rel == '\n' || *rel == '\t') rel++;
    if (!*rel || *rel == '#' || comeca_com(rel, "javascript:") || comeca_com(rel, "mailto:")) return false;
    if (comeca_com(rel, "http://") || comeca_com(rel, "https://") || comeca_com(rel, "sobre:") || comeca_com(rel, "sagui-js:")) {
        copia_texto(saida, rel, (size_t)max);
        return true;
    }
    if (comeca_com(rel, "//")) { formatar_texto(saida, (size_t)max, "%s:%s", url_https(base) ? "https" : "http", rel); return true; }
    if (!comeca_com(base, "http://") && !url_https(base)) return false;
    /* Raiz do site: "http://host[:porta]". */
    int raiz = url_https(base) ? 8 : 7;
    while (base[raiz] && base[raiz] != '/' && base[raiz] != '?' && base[raiz] != '#') raiz++;
    int fim = raiz, ultima = -1;   /* fim do caminho (sem consulta) e a última barra dele */
    while (base[fim] && base[fim] != '?' && base[fim] != '#') { if (base[fim] == '/') ultima = fim; fim++; }
    char caminho[URL_MAX];
    int k = 0;
    if (*rel == '/') {
        caminho[0] = 0;
    } else if (*rel == '?') {                       /* mesma página, outra consulta */
        if (fim == raiz) caminho[k++] = '/';
        for (int i = raiz; i < fim && k < (int)sizeof caminho - 1; i++) caminho[k++] = base[i];
        caminho[k] = 0;
    } else {                                         /* mesma pasta */
        if (ultima < 0) caminho[k++] = '/';
        for (int i = raiz; i <= ultima && k < (int)sizeof caminho - 1; i++) caminho[k++] = base[i];
        caminho[k] = 0;
    }
    junta_texto(caminho, rel, sizeof caminho);
    /* Tira "/./" e "pasta/../" do caminho (a consulta fica como está). */
    char limpo[URL_MAX];
    char *q = acha_char(caminho, '?');
    int fim_caminho = q ? (int)(q - caminho) : (int)tam_texto(caminho);
    k = 0;
    int i = 0;
    while (i < fim_caminho && k < (int)sizeof limpo - 1) {
        if (caminho[i] == '/' && caminho[i + 1] == '.' && (i + 2 == fim_caminho || caminho[i + 2] == '/')) { i += 2; continue; }
        if (caminho[i] == '/' && caminho[i + 1] == '.' && caminho[i + 2] == '.' && (i + 3 == fim_caminho || caminho[i + 3] == '/')) {
            while (k > 0 && limpo[k - 1] != '/') k--;
            if (k > 0) k--;
            i += 3;
            continue;
        }
        limpo[k++] = caminho[i++];
    }
    if (k == 0) limpo[k++] = '/';
    limpo[k] = 0;
    if (q) junta_texto(limpo, q, sizeof limpo);
    k = 0;
    for (int r = 0; r < raiz && k < max - 1; r++) saida[k++] = base[r];
    saida[k] = 0;
    junta_texto(saida, limpo, (size_t)max);
    return true;
}

/* ------------------------------------------------------------ diagramação */

static int x_atual, linha_atual, recuo, estilo, alvo_atual = -1;
static bool linha_vazia = true, espaco_pendente;
static int brancas;          /* linhas em branco seguidas (evita buracos) */

/* Centraliza a linha que acabou: os segmentos dela andam juntos. */
static int centro, ini_linha;
static uint32_t cor_css;
static bool linha_grande;
static int dir_atual, direita;      /* limite direito da caixa atual; linha à direita */
static uint32_t fundo_atual = 0xFFFFFF, fundo_pagina = 0xFFFFFF;

/* Fundos e bordas das caixas, desenhados antes do texto. */
typedef struct { int l0, l1; int16_t x0, x1; uint32_t fundo, borda; } ret_t;   /* cores + 1; 0 = nenhuma */
static ret_t *rets; static int qtd_rets, cap_rets;

static int luz(uint32_t c) { return (int)((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10; }

/* float: a caixa fica de lado e o texto corre ao lado dela até o fundo dela. */
static int fd_fundo, fd_lim = -1, fd_orig, fe_fundo, fe_lim = -1, fe_orig;

static void ajustar_floats(void)
{
    if (fd_fundo && linha_atual >= fd_fundo) fd_fundo = 0;
    if (fe_fundo && linha_atual >= fe_fundo) fe_fundo = 0;
    if (!fd_fundo && dir_atual == fd_lim) dir_atual = fd_orig;
    if (!fe_fundo && recuo == fe_lim) recuo = fe_orig;
}

static void quebrar(void)
{
    if (!linha_vazia && (centro || direita)) {
        int d = (dir_atual - x_atual) / (direita ? 1 : 2);
        for (int i = ini_linha; d > 0 && i < qtd_segs; i++) segs[i].x = (int16_t)(segs[i].x + d);
    }
    ini_linha = qtd_segs;
    if (!linha_vazia) { linha_atual += linha_grande ? 2 : 1; brancas = 0; }
    linha_grande = false;
    ajustar_floats();
    x_atual = recuo;
    linha_vazia = true;
    espaco_pendente = false;
}

static void paragrafo(void)
{
    quebrar();
    if (linha_atual > 0 && brancas == 0) { linha_atual++; brancas = 1; }
}

static void segmento(const char *t, int n, int est, int alvo)
{
    if (!crescer((void **)&segs, &cap_segs, qtd_segs + 1, (int)sizeof(seg_t))) return;
    seg_t *s = &segs[qtd_segs++];
    s->linha = linha_atual;
    s->x = (int16_t)x_atual;
    s->n = (int16_t)n;
    s->estilo = (uint16_t)est;
    s->cor = cor_css;
    /* Texto que sumiria no fundo vira preto ou branco; o link segue a mesma regra. */
    int lf = luz(fundo_atual), lt = cor_css ? luz(cor_css - 1) : (est & E_LINK ? 60 : 40);
    if (lt - lf < 70 && lf - lt < 70) {
        s->cor = (lf > 128 ? 0x101418u : 0xF2F4F6u) + 1;
        s->estilo |= E_LINKCOR;
    }
    if (est & E_GRANDE) linha_grande = true;
    s->alvo = (int16_t)alvo;
    s->texto = guardar_texto(t, n);
    linha_vazia = false;
    brancas = 0;
    if (linha_atual + 1 > linhas) linhas = linha_atual + 1;
}

/* Uma palavra (sem espaços), com quebra de linha automática. */
static void palavra(const char *t, int n)
{
    while (n > 0) {
        int passo = estilo & E_GRANDE ? 2 * LETRA_L : LETRA_L;
        int esp = espaco_pendente && !linha_vazia ? passo : 0;
        int cabe = (dir_atual - x_atual - esp) / passo;
        if (n > cabe && !linha_vazia) { quebrar(); continue; }
        if (cabe < 1) cabe = 1;
        int k = n < cabe ? n : cabe;
        x_atual += esp;
        segmento(t, k, estilo, alvo_atual);
        x_atual += k * passo;
        espaco_pendente = false;
        t += k;
        n -= k;
        if (n) quebrar();
    }
}

static void texto_solto(const char *t)
{
    while (*t) {
        while (*t == ' ') { espaco_pendente = true; t++; }
        int n = 0;
        while (t[n] && t[n] != ' ') n++;
        if (n) palavra(t, n);
        t += n;
    }
}

static int novo_alvo(int tipo, const char *url, int form, const char *nome)
{
    if (qtd_alvos >= 32000 || !crescer((void **)&alvos, &cap_alvos, qtd_alvos + 1, (int)sizeof(alvo_t))) return -1;
    alvo_t *a = &alvos[qtd_alvos];
    a->tipo = tipo;
    a->url = url ? guardar_texto(url, (int)tam_texto(url)) : -1;
    a->form = form;
    a->nome = nome ? guardar_texto(nome, (int)tam_texto(nome)) : -1;
    return qtd_alvos++;
}

/* Valor de um atributo da tag ("href", "alt"...), já sem entidades. */
static bool atributo(const char *a, int n, const char *nome, char *valor, int max)
{
    int t = (int)tam_texto(nome);
    for (int i = 0; i + t <= n; i++) {
        if (!igual_sem_caixa(a + i, nome, t) || (i > 0 && !eh_espaco(a[i - 1]))) continue;
        int k = i + t;
        while (k < n && eh_espaco(a[k])) k++;
        if (k >= n || a[k] != '=') continue;
        k++;
        while (k < n && eh_espaco(a[k])) k++;
        char aspas = (k < n && (a[k] == '"' || a[k] == '\'')) ? a[k++] : 0;
        int ini = k;
        while (k < n && (aspas ? a[k] != aspas : !eh_espaco(a[k]) && a[k] != '>')) k++;
        decodificar(a + ini, k - ini, valor, max);
        return true;
    }
    return false;
}

/* Tags que começam um bloco novo (quebram a linha). */
static bool bloco(const char *t)
{
    static const char *const nomes[] = { "div", "tr", "table", "form", "center", "dl", "dt", "dd", "section",
                                         "article", "header", "footer", "nav", "address", "figure", "main",
                                         "aside", "caption", "fieldset", "tbody", "thead", "option", NULL };
    for (int i = 0; nomes[i]; i++) if (compara_texto(t, nomes[i]) == 0) return true;
    return false;
}

static bool paragrafo_tag(const char *t)
{
    return compara_texto(t, "p") == 0 || compara_texto(t, "blockquote") == 0 || compara_texto(t, "pre") == 0 ||
           compara_texto(t, "ul") == 0 || compara_texto(t, "ol") == 0 ||
           (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2]);
}

static int recuo_lista[8];
static int pilha_lista[8], niveis;   /* contador de cada lista aberta (0 = com marcador) */
static int negrito_n, italico_n, pre_n, form_atual = -1;
static bool em_titulo;
static void rodape(const char *t);

/* Baixa uma imagem inteira, seguindo redirecionamentos. */
/* Última resposta HTTP (para o fetch do JavaScript): código, cabeçalhos, URL final */
static int resp_codigo; static char resp_cab[8192]; static char resp_url[URL_MAX];
/* qualquer = devolve também o corpo de 4xx/5xx (o fetch quer ver) */
static unsigned t_rede, t_js;
/* <meta http-equiv="refresh" content="N; url=..."> fora de <noscript> (o JS está ligado) */
static char refresh_url[URL_MAX]; static unsigned refresh_em; static bool refresh_ativo;
static void ler_refresh(const char *h, int n)
{
    refresh_ativo = false;
    for (int i = 0; i + 6 < n; i++) {
        if (h[i] != '<') continue;
        if (compara_texto_n(h + i + 1, "noscript", 8) == 0 || compara_texto_n(h + i + 1, "NOSCRIPT", 8) == 0) {   /* pula até o fim */
            for (; i + 10 < n; i++) if (h[i] == '<' && h[i + 1] == '/' && ((h[i + 2] | 32) == 'n') && compara_texto_n(h + i + 3, "oscript", 7) == 0) break;
            continue;
        }
        if ((h[i + 1] | 32) != 'm' || (h[i + 2] | 32) != 'e' || (h[i + 3] | 32) != 't' || (h[i + 4] | 32) != 'a') continue;
        int f = i; while (f < n && h[f] != '>') f++;
        bool eq = false; const char *ct = NULL; int ctn = 0;
        for (int k = i; k + 10 < f; k++) {
            if ((h[k] | 32) == 'r' && compara_texto_n(h + k, "refresh", 7) == 0) eq = true;
            if ((h[k] | 32) == 'c' && compara_texto_n(h + k, "content=", 8) == 0) {
                int a = k + 8; char q = h[a] == '"' || h[a] == '\'' ? h[a++] : ' ';
                int b = a; while (b < f && h[b] != q && !(q == ' ' && h[b] == '>')) b++;
                ct = h + a; ctn = b - a;
            }
        }
        if (!eq || !ct) continue;
        int seg = 0, k = 0;
        while (k < ctn && ct[k] >= '0' && ct[k] <= '9') seg = seg * 10 + (ct[k++] - '0');
        while (k < ctn && (ct[k] == ';' || ct[k] == ',' || ct[k] == ' ' || ct[k] == '.')) k++;
        if (k + 4 <= ctn && (ct[k] | 32) == 'u' && (ct[k + 1] | 32) == 'r' && (ct[k + 2] | 32) == 'l') { k += 3; while (k < ctn && (ct[k] == ' ' || ct[k] == '=')) k++; }
        char rel[URL_MAX]; int r = 0;
        while (k < ctn && r < URL_MAX - 1 && ct[k] != '\'' && ct[k] != '"') rel[r++] = ct[k++];
        rel[r] = 0;
        /* &amp; do atributo */
        for (char *a = rel; (a = (char *)acha_char(a, '&'));) { if (compara_texto_n(a, "&amp;", 5) == 0) memmove(a + 1, a + 5, tam_texto(a + 5) + 1); a++; }
        if (!juntar_url(url_atual, rel[0] ? rel : url_atual, refresh_url, sizeof refresh_url)) continue;
        refresh_em = tempo_ms() + (unsigned)seg * 1000; refresh_ativo = true;
        registrar("cipo: meta refresh em %d s para %s\n", seg, refresh_url);
        return;
    }
}
static bool canvas_vivo;
static int js_rol, rol_pedida = -1;   /* rolagem que o JS já viu; scrollTo pedido pelo JS */   /* a página anima um <canvas>: o tique vem mais rápido */   /* medição: tempo da página em rede e em scripts */
static uint8_t *baixar_resp_(const char *pedido, unsigned *n, bool qualquer);
static uint8_t *baixar_resp(const char *pedido, unsigned *n, bool qualquer)
{
    unsigned t0 = tempo_ms();
    uint8_t *d = baixar_resp_(pedido, n, qualquer);
    t_rede += tempo_ms() - t0;
    return d;
}
static uint8_t *baixar_resp_(const char *pedido, unsigned *n, bool qualquer)
{
    static char cab[16384];
    resp_codigo = 0; resp_cab[0] = 0;
    char url[URL_MAX];
    copia_texto(url, pedido, sizeof url);
    for (int r = 0; r < 4; r++) {
        char host[128], caminho[512], destino[URL_MAX];
        int porta, extra;
        if (!url_separar(url, host, sizeof host, &porta, caminho, sizeof caminho)) return NULL;
        int fd = http_pedir_seguro(host, porta, caminho, url_https(url));
        if (fd < 0) return NULL;
        int c = http_cabecalho(fd, cab, sizeof cab, &extra);
        if (c < 0) { http_fechar(fd); return NULL; }
        int codigo = http_codigo(cab);
        resp_codigo = codigo;
        { int k = c < (int)sizeof resp_cab - 1 ? c : (int)sizeof resp_cab - 1; memcpy(resp_cab, cab, (size_t)k); resp_cab[k] = 0; }
        copia_texto(resp_url, url, sizeof resp_url);
        if (codigo >= 300 && codigo < 400 && http_campo(cab, "Location", destino, sizeof destino)) {
            http_fechar(fd);
            char nova[URL_MAX];
            if (!juntar_url(url, destino, nova, sizeof nova)) return NULL;
            copia_texto(url, nova, sizeof url);
            continue;
        }
        if (codigo != 200 && !qualquer) { http_fechar(fd); return NULL; }
        unsigned cap = 262144, tam = (unsigned)extra;
        uint8_t *d = aloca(cap);
        if (!d) { http_fechar(fd); return NULL; }
        memcpy(d, cab + c, tam);
        int q;
        while ((q = http_ler(fd, d + tam, cap - tam)) > 0) {
            tam += (unsigned)q;
            if (tam == cap) {
                if (cap >= IMAGEM_BYTES_MAX) break;
                uint8_t *m = realoca(d, cap * 2);
                if (!m) break;
                d = m;
                cap *= 2;
            }
        }
        http_fechar(fd);
        char te[32];
        if (http_campo(cab, "Transfer-Encoding", te, sizeof te)) tam = (unsigned)http_juntar_pedacos((char *)d, (int)tam);
        *n = tam;
        return d;
    }
    return NULL;
}
static uint8_t *baixar_imagem(const char *pedido, unsigned *n) { return baixar_resp(pedido, n, false); }

/* <img>: baixa, reduz para caber na página e reserva as linhas. */
static bool colocar_imagem(const char *url, int larg_css)
{
    if (motor) return false;   /* o motor baixa as imagens depois da primeira pintura */
    if (qtd_imagens == MAX_IMAGENS) return false;
    if (tempo_ms() - inicio_pagina > 15000) return false;   /* página cheia de fotos não prende o navegador */
    char st[100];
    formatar_texto(st, sizeof st, "Baixando imagem %d...", qtd_imagens + 1);
    rodape(st);
    unsigned n = 0;
    uint8_t *d = baixar_imagem(url, &n);
    if (!d) { registrar("cipo: imagem nao baixou %s\n", url); return false; }
    uint32_t *px = NULL;
    int l = 0, a = 0;
    bool ok = imagem_decodificar(d, n, &px, &l, &a);
    libera(d);
    if (!ok) { registrar("cipo: imagem nao decodificou (%d bytes) %s\n", (int)n, url); return false; }
    registrar("cipo: imagem %dx%d\n", l, a);
    if (a <= 2 || l <= 2) { libera(px); return true; }     /* pixel de rastreamento */
    bool na_linha = a <= 36 && l <= 64;                     /* ícone: fica no meio do texto */
    int cabe = dir_atual - recuo, dl = l, da = a;
    if (larg_css > 0 && larg_css < cabe) cabe = larg_css;
    if (dl > cabe) { da = da * cabe / dl; dl = cabe; }
    if (na_linha && da > LINHA_A - 2) { dl = dl * (LINHA_A - 2) / da; da = LINHA_A - 2; }
    if (da > IMAGEM_ALT_MAX) { dl = dl * IMAGEM_ALT_MAX / da; da = IMAGEM_ALT_MAX; }
    if (dl < 1) dl = 1;
    if (da < 1) da = 1;
    uint32_t *r = aloca((size_t)dl * (size_t)da * 4);
    if (r)
        for (int y = 0; y < da; y++)
            for (int x = 0; x < dl; x++) r[y * dl + x] = px[(y * a / da) * l + x * l / dl];
    libera(px);
    if (!r) return false;
    imagem_t *im = &imagens[qtd_imagens];
    im->px = r; im->l = dl; im->a = da;
    if (na_linha) {
        if (x_atual + dl > dir_atual && !linha_vazia) quebrar();
        segmento("", qtd_imagens, E_IMAGEM, alvo_atual);
        x_atual += dl + 4;
        qtd_imagens++;
        return true;
    }
    quebrar();
    if (centro) x_atual = recuo + (dir_atual - recuo - dl) / 2;
    segmento("", qtd_imagens, E_IMAGEM, alvo_atual);
    ini_linha = qtd_segs;
    qtd_imagens++;
    linha_atual += (da + LINHA_A - 1) / LINHA_A;
    if (linha_atual > linhas) linhas = linha_atual;
    x_atual = recuo;
    linha_vazia = true;
    return true;
}

/* O que só atrapalha numa página sem CSS: menus, rodapés e o que o site esconde. */
static const char *achar_tag(const char *h, int n, const char *tag);

/* Os resultados do Bing passam por /ck/a, que redireciona com JavaScript. O
 * destino vem no parâmetro u=a1<base64>: abre direto. */
static void destino_bing(char *url, int max)
{
    if (!comeca_com(url, "https://www.bing.com/ck/a")) return;
    const char *p = url;
    while (*p && !((*p == '&' || *p == '?') && p[1] == 'u' && p[2] == '=' && p[3] == 'a' && p[4] == '1')) p++;
    if (!*p) return;
    p += 5;
    char d[URL_MAX];
    int k = 0, bits = 0;
    uint32_t acc = 0;
    for (; *p && *p != '&' && k < (int)sizeof d - 1; p++) {
        char c = *p;
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
                c == '-' || c == '+' ? 62 : c == '_' || c == '/' ? 63 : -1;
        if (v < 0) break;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; d[k++] = (char)(acc >> bits); }
    }
    d[k] = 0;
    if (comeca_com(d, "http")) copia_texto(url, d, (size_t)max);
}

/* ---- CSS: só o que muda uma página de texto ----
 * Lê <style>, folhas externas (<link>), @media de tela, var() e o atributo style.
 * Caixas: fundo, borda, margem, padding, largura, flex, grid, float e tabelas
 * em colunas, sobre a grade de linhas do Cipó (altura em múltiplos de LINHA_A).
 * Seletores: tag, .classe, #id, combinações deles e descendentes ("a .b", "a > b").
 * Vale a regra mais específica e, no empate, a última; !important ganha de todas.
 * Pseudoclasses, [atributo],
 * "+" e "~" ficam de fora: a regra é ignorada, para não esconder o que não deve. */
#define MAX_REGRAS 2500
#define SEL_MAX 72
#define LARGURA_CSS 1000   /* largura que as condições de @media enxergam */
/* Campos pequenos: 0 = a regra não fala nada.
 * esconde: 1 none, 2 block, 3 inline, 4 inline-block, 5 flex, 6 grid, 7 outro.
 * centra: 1 centro, 2 esquerda, 3 direita. negrito, italico: 1 liga, 2 desliga.
 * sublinha: 1 sem, 2 com. flutua: 1 esquerda, 2 direita. colunas: do grid.
 * v[]: medidas + 1 em px; acima de V_PCT é porcentagem; V_AUTO é "auto". */
enum { V_ME, V_MD, V_MC, V_MB, V_PE, V_PD, V_PC, V_PB, V_L, V_LMAX, V_FONTE, V_VAO, QTD_V };
#define V_PCT 20000
#define V_AUTO 30000
typedef struct {
    char sel[SEL_MAX];
    uint8_t esconde, disp, centra, negrito, italico, sublinha, flutua, colunas, sem_marca, vertical, forte;
    uint16_t peso;
    uint32_t cor, fundo, borda;
    int16_t v[QTD_V];
} regra_t;
static regra_t regras[MAX_REGRAS];
static int qtd_regras, qtd_folhas;
static bool css_escondeu, sem_esconder;

/* Elementos abertos: servem aos seletores de descendente e guardam o que
 * desfazer quando a tag fechar. Uma "fila" é um flex, grid ou <tr>: os filhos
 * viram colunas lado a lado. */
#define MAX_FILHOS 12
typedef struct {
    char nome[12], classe[80], id[32];
    bool bloco, celula, fila;
    int flut, flut_l;
    uint8_t disp;
    int ret, marg_b, pad_b;
    int s_recuo, s_dir, s_centro, s_direita, s_estilo, s_mascara, s_niveis, s_marca;
    uint32_t s_cor, s_fundo;
    int larg;                                     /* da célula */
    int x0, x_prox, x_fim, linha0, fundo, vao, colunas, qtd, filho;
    uint8_t peso[MAX_FILHOS]; int soma;
} aberto_t;
#define MAX_ABERTOS 64
static aberto_t abertos[MAX_ABERTOS];
static int qtd_abertos, excesso_abertos, ultimo_disp;
static bool sem_marca;

/* Variáveis do CSS (--nome: valor). */
typedef struct { char nome[32], valor[40]; } var_css_t;
static var_css_t vars_css[128];
static int qtd_vars;

static int hex_val(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

static uint32_t ler_cor(const char *v, int n)
{
    static const struct { const char *n; uint32_t c; } nomes[] = {
        { "white", 0xFFFFFF }, { "black", 0 }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF },
        { "gray", 0x808080 }, { "grey", 0x808080 }, { "yellow", 0xFFFF00 }, { "orange", 0xFFA500 },
        { "navy", 0x000080 }, { "silver", 0xC0C0C0 }, { "purple", 0x800080 }, { "maroon", 0x800000 } };
    uint32_t c = 0;
    for (unsigned i = 0; i < sizeof nomes / sizeof nomes[0]; i++) {
        int t = (int)tam_texto(nomes[i].n);
        if (n >= t && igual_sem_caixa(v, nomes[i].n, t) && (n == t || eh_espaco(v[t]) || v[t] == ';' || v[t] == '!')) return nomes[i].c + 1;
    }
    if (n >= 10 && igual_sem_caixa(v, "rgb", 3)) {
        int i = 3, partes = 0;
        while (i < n && v[i] != '(') i++;
        for (i++; i < n && partes < 3; partes++) {
            int t = 0;
            while (i < n && (eh_espaco(v[i]) || v[i] == ',')) i++;
            if (i >= n || v[i] < '0' || v[i] > '9') return 0;
            while (i < n && v[i] >= '0' && v[i] <= '9') t = t * 10 + v[i++] - '0';
            while (i < n && v[i] != ',' && !eh_espaco(v[i]) && v[i] != ')') i++;
            c = c << 8 | (uint32_t)(t > 255 ? 255 : t);
        }
        if (partes < 3) return 0;
    } else if (n < 4 || v[0] != '#') return 0;
    else if (n >= 7 && hex_val(v[6]) >= 0) {
        for (int i = 1; i <= 6; i++) { int h = hex_val(v[i]); if (h < 0) return 0; c = c << 4 | (uint32_t)h; }
    } else {
        for (int i = 1; i <= 3; i++) { int h = hex_val(v[i]); if (h < 0) return 0; c = c << 8 | (uint32_t)(h * 17); }
    }
    return c + 1;
}

/* Lê uma medida no começo de v; devolve o valor guardado (ver regra_t) e o que consumiu. */
static int ler_medida(const char *v, int n, int *usado)
{
    int i = 0, t = 0;
    bool neg = false, tem = false;
    if (n >= 4 && igual_sem_caixa(v, "auto", 4)) { *usado = 4; return V_AUTO; }
    if (i < n && v[i] == '-') { neg = true; i++; }
    while (i < n && v[i] >= '0' && v[i] <= '9') { t = t * 10 + v[i++] - '0'; tem = true; if (t > 9000) t = 9000; }
    int frac = 0;
    if (i < n && v[i] == '.') { i++; if (i < n && v[i] >= '0' && v[i] <= '9') { frac = v[i] - '0'; tem = true; } while (i < n && v[i] >= '0' && v[i] <= '9') i++; }
    int u = i;
    while (i < n && !eh_espaco(v[i]) && v[i] != ';' && v[i] != '!' && v[i] != ',' && v[i] != ')') i++;
    *usado = i;
    if (!tem) return 0;
    if (neg) return 1;
    if (u < n && v[u] == '%') return V_PCT + t;
    if (u + 1 < n && ((v[u] | 32) == 'e' || (v[u] | 32) == 'r')) return (t * 16 + frac * 16 / 10) + 1;
    if (u + 1 < n && (v[u] | 32) == 'v') return V_PCT + t;   /* vw vira porcentagem */
    return t + 1;
}

/* margin: 1 a 4 valores, na ordem cima, direita, baixo, esquerda. */
static void ler_quatro(const char *v, int n, int16_t *o, int c, int d, int b, int e)
{
    int m[4], q = 0, i = 0;
    while (i < n && q < 4) {
        while (i < n && eh_espaco(v[i])) i++;
        if (i >= n || v[i] == '!') break;
        int u = 0, x = ler_medida(v + i, n - i, &u);
        if (!x || !u) return;
        m[q++] = x;
        i += u;
    }
    if (!q) return;
    o[c] = (int16_t)m[0];
    o[d] = (int16_t)m[q > 1 ? 1 : 0];
    o[b] = (int16_t)m[q > 2 ? 2 : 0];
    o[e] = (int16_t)m[q > 3 ? 3 : q > 1 ? 1 : 0];
}

/* Primeira cor numa lista de valores, como "1px solid #ccc". */
static uint32_t cor_na_lista(const char *v, int n)
{
    for (int i = 0; i < n;) {
        while (i < n && eh_espaco(v[i])) i++;
        int k = i, prof = 0;
        while (k < n && (prof || !eh_espaco(v[k]))) { if (v[k] == '(') prof++; else if (v[k] == ')') prof--; k++; }
        uint32_t c = k > i ? ler_cor(v + i, k - i) : 0;
        if (c) return c;
        i = k;
    }
    return 0;
}

static int contar_colunas(const char *v, int n)
{
    if (n > 7 && igual_sem_caixa(v, "repeat(", 7)) { int t = texto_para_int(v + 7); return t > 0 && t <= 12 ? t : 0; }
    int q = 0;
    for (int i = 0; i < n; q++) {
        while (i < n && eh_espaco(v[i])) i++;
        if (i >= n) break;
        while (i < n && !eh_espaco(v[i])) i++;
    }
    return q > 12 ? 12 : q;
}

static void ler_declaracoes(const char *d, int n, regra_t *r)
{
    for (int i = 0; i < n;) {
        while (i < n && (eh_espaco(d[i]) || d[i] == ';')) i++;
        int ini = i;
        while (i < n && d[i] != ':' && d[i] != ';') i++;
        if (i >= n || d[i] == ';') continue;
        int fim_p = i++;
        while (i < n && eh_espaco(d[i])) i++;
        int v = i;
        while (i < n && d[i] != ';') i++;
        int nv = i - v, np = fim_p - ini;
        const char *p = d + ini, *val = d + v;
        for (int k = 0; k < nv; k++) if (val[k] == '!') { r->forte = 1; nv = k; break; }
        while (nv && eh_espaco(val[nv - 1])) nv--;
        if (np > 2 && p[0] == '-' && p[1] == '-') {
            if (qtd_vars < 128 && np < 32 && nv < 40) {
                var_css_t *w = &vars_css[qtd_vars++];
                memcpy(w->nome, p, (size_t)np); w->nome[np] = 0;
                memcpy(w->valor, val, (size_t)nv); w->valor[nv] = 0;
            }
            continue;
        }
        if (nv > 6 && igual_sem_caixa(val, "var(", 4)) {
            int t = 4;
            while (t < nv && val[t] != ')' && val[t] != ',') t++;
            bool achou = false;
            for (int w = qtd_vars - 1; w >= 0 && !achou; w--)
                if ((int)tam_texto(vars_css[w].nome) == t - 4 && memcmp(vars_css[w].nome, val + 4, (size_t)(t - 4)) == 0) {
                    val = vars_css[w].valor; nv = (int)tam_texto(val); achou = true;
                }
            if (!achou) continue;
        }
#define PROP(x) (np == (int)sizeof(x) - 1 && igual_sem_caixa(p, x, np))
#define VAL(x) (nv >= (int)sizeof(x) - 1 && igual_sem_caixa(val, x, (int)sizeof(x) - 1))
        int u;
        if (PROP("display")) {
            r->esconde = VAL("none") ? 1 : VAL("block") ? 2 : VAL("inline-block") || VAL("inline-flex") ? 4 : VAL("inline") ? 3 :
                         VAL("flex") ? 5 : VAL("grid") ? 6 : 7;
        } else if (PROP("visibility")) { if (VAL("hidden")) r->esconde = 1; }
        else if (PROP("clip") || PROP("clip-path")) { if (!VAL("auto") && !VAL("none")) r->esconde = 1; }
        else if ((PROP("left") || PROP("text-indent") || PROP("top")) && nv > 4 && val[0] == '-' && val[1] >= '1' && val[1] <= '9' && val[4] >= '0' && val[4] <= '9') r->esconde = 1;
        else if (PROP("text-align")) r->centra = VAL("center") ? 1 : VAL("right") ? 3 : 2;
        else if (PROP("color")) { uint32_t c = ler_cor(val, nv); if (c) r->cor = c; }
        else if (PROP("background-color") || PROP("background")) { uint32_t c = cor_na_lista(val, nv); if (c) r->fundo = c; }
        else if (PROP("border")) { uint32_t c = cor_na_lista(val, nv); if (c) r->borda = c; }
        else if (PROP("font-weight") && nv >= 3)
            r->negrito = val[0] == '6' || val[0] == '7' || val[0] == '8' || val[0] == '9' || VAL("bold") ? 1 : 2;
        else if (PROP("font-style")) r->italico = VAL("italic") || VAL("oblique") ? 1 : 2;
        else if (PROP("text-decoration") || PROP("text-decoration-line")) r->sublinha = VAL("none") ? 1 : VAL("underline") ? 2 : 0;
        else if (PROP("float")) r->flutua = VAL("left") ? 1 : VAL("right") ? 2 : 0;
        else if (PROP("list-style") || PROP("list-style-type")) r->sem_marca = VAL("none") ? 1 : 2;
        else if (PROP("flex-direction")) r->vertical = VAL("column") ? 1 : 2;
        else if (PROP("grid-template-columns")) r->colunas = (uint8_t)contar_colunas(val, nv);
        else if (PROP("margin")) ler_quatro(val, nv, r->v, V_MC, V_MD, V_MB, V_ME);
        else if (PROP("padding")) ler_quatro(val, nv, r->v, V_PC, V_PD, V_PB, V_PE);
        else if (PROP("margin-left")) r->v[V_ME] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("margin-right")) r->v[V_MD] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("margin-top")) r->v[V_MC] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("margin-bottom")) r->v[V_MB] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("padding-left")) r->v[V_PE] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("padding-right")) r->v[V_PD] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("padding-top")) r->v[V_PC] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("padding-bottom")) r->v[V_PB] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("width")) { r->v[V_L] = (int16_t)ler_medida(val, nv, &u); if (r->v[V_L] == 1 || r->v[V_L] == 2) r->esconde = 1; }
        else if (PROP("max-height") || PROP("height")) { if (nv >= 1 && val[0] == '0' && (nv == 1 || val[1] == 'p')) r->esconde = 1; }
        else if (PROP("max-width")) r->v[V_LMAX] = (int16_t)ler_medida(val, nv, &u);
        else if (PROP("font-size")) { r->v[V_FONTE] = (int16_t)ler_medida(val, nv, &u); if (r->v[V_FONTE] == 1) r->esconde = 1; }
        else if (PROP("gap") || PROP("column-gap")) r->v[V_VAO] = (int16_t)ler_medida(val, nv, &u);
#undef PROP
#undef VAL
    }
}

/* A condição de um @media vale para a tela do Cipó? */
static bool media_aceita(const char *m, int n)
{
    for (int i = 0; i < n; i++) {
        if (i + 5 <= n && igual_sem_caixa(m + i, "print", 5)) return false;
        if (i + 6 <= n && igual_sem_caixa(m + i, "speech", 6)) return false;
        if (i + 8 <= n && igual_sem_caixa(m + i, "prefers-", 8)) return false;
        if (i + 8 <= n && igual_sem_caixa(m + i, "portrait", 8)) return false;
        bool min = i + 9 <= n && igual_sem_caixa(m + i, "min-width", 9);
        bool max = i + 9 <= n && igual_sem_caixa(m + i, "max-width", 9);
        if (!min && !max) continue;
        int k = i + 9, v = 0;
        while (k < n && (m[k] == ':' || eh_espaco(m[k]))) k++;
        while (k < n && m[k] >= '0' && m[k] <= '9') v = v * 10 + m[k++] - '0';
        if (k < n && m[k] == '.') while (++k < n && m[k] >= '0' && m[k] <= '9') {}
        if (k < n && ((m[k] | 32) == 'e' || (m[k] | 32) == 'r')) v *= 16;
        if (min ? v > LARGURA_CSS : v < LARGURA_CSS) return false;
    }
    return true;
}

/* Copia um seletor com espaços normalizados e calcula o peso; false = não entendido. */
static bool guardar_seletor(const char *c, int t, regra_t *d)
{
    int o = 0, peso = 0;
    bool inicio = true;
    if (t >= 5 && igual_sem_caixa(c, ":root", 5)) { memcpy(d->sel, "html", 4); o = 4; c += 5; t -= 5; peso = 10; inicio = false; }
    for (int i = 0; i < t; i++) {
        char x = c[i];
        if (x == ':' || x == '[' || x == '+' || x == '~' || x == '\\') return false;
        if (eh_espaco(x) || x == '>') {
            if (o && d->sel[o - 1] != ' ') d->sel[o++] = ' ';
            inicio = true;
        } else {
            if (x == '#') peso += 100;
            else if (x == '.') peso += 10;
            else if (inicio && x != '*') peso++;
            inicio = false;
            d->sel[o++] = x;
        }
        if (o >= SEL_MAX - 1) return false;
    }
    while (o && d->sel[o - 1] == ' ') o--;
    d->sel[o] = 0;
    d->peso = (uint16_t)(peso + (d->forte ? 10000 : 0));
    return o > 0;
}

static void ler_css(const char *c, int n)
{
    int i = 0;
    while (i < n) {
        while (i < n && (eh_espaco(c[i]) || c[i] == '}')) i++;
        if (i + 1 < n && c[i] == '/' && c[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(c[i] == '*' && c[i + 1] == '/')) i++;
            i += 2;
            continue;
        }
        int ini = i;
        while (i < n && c[i] != '{' && c[i] != ';') i++;
        if (i >= n) break;
        if (c[ini] == '@') {                      /* só @media de tela entra; o resto é pulado */
            if (c[i] == ';') { i++; continue; }
            int abre = i, prof = 0;
            do { if (c[i] == '{') prof++; else if (c[i] == '}') prof--; i++; } while (i < n && prof);
            if (abre - ini > 6 && igual_sem_caixa(c + ini, "@media", 6) && media_aceita(c + ini + 6, abre - ini - 6))
                ler_css(c + abre + 1, i - abre - 2);
            continue;
        }
        if (c[i] == ';') { i++; continue; }
        int fim_s = i++, corpo = i;
        while (i < n && c[i] != '}') i++;
        regra_t r;
        memset(&r, 0, sizeof r);
        ler_declaracoes(c + corpo, i - corpo, &r);
        bool algo = false;
        for (unsigned b = SEL_MAX; b < sizeof r; b++) if (((const uint8_t *)&r)[b]) algo = true;
        if (!algo) continue;
        for (int s = ini; s < fim_s;) {           /* "a, b, c" vira três regras */
            while (s < fim_s && (eh_espaco(c[s]) || c[s] == ',')) s++;
            int k = s;
            while (k < fim_s && c[k] != ',') k++;
            if (k > s && qtd_regras < MAX_REGRAS) {
                regras[qtd_regras] = r;
                if (guardar_seletor(c + s, k - s, &regras[qtd_regras])) qtd_regras++;
            }
            s = k;
        }
    }
    registrar("cipo: css %d regras\n", qtd_regras);
}

static bool tem_palavra(const char *lista_v, const char *p, int t)
{
    for (const char *s = lista_v; *s;) {
        while (*s == ' ') s++;
        int k = 0;
        while (s[k] && s[k] != ' ') k++;
        if (k == t && memcmp(s, p, (size_t)t) == 0) return true;
        s += k;
    }
    return false;
}

/* Um pedaço do seletor, como "div.a#b", combina com este elemento? */
static bool bate_composto(const char *s, int t, const char *nome, const char *classe, const char *id)
{
    int i = 0;
    while (i < t && s[i] != '.' && s[i] != '#') i++;
    if (i && !(i == 1 && s[0] == '*') && !((int)tam_texto(nome) == i && igual_sem_caixa(nome, s, i))) return false;
    while (i < t) {
        char tipo = s[i++];
        int k = i;
        while (k < t && s[k] != '.' && s[k] != '#') k++;
        if (tipo == '.') { if (!tem_palavra(classe, s + i, k - i)) return false; }
        else if ((int)tam_texto(id) != k - i || memcmp(id, s + i, (size_t)(k - i))) return false;
        i = k;
    }
    return true;
}

static bool bate_seletor(const char *s, const char *nome, const char *classe, const char *id)
{
    int fim = (int)tam_texto(s), i = fim;
    while (i && s[i - 1] != ' ') i--;
    if (!bate_composto(s + i, fim - i, nome, classe, id)) return false;
    int p = (qtd_abertos < MAX_ABERTOS ? qtd_abertos : MAX_ABERTOS) - 1;
    while (i) {
        fim = i - 1;
        i = fim;
        while (i && s[i - 1] != ' ') i--;
        /* <main> é lido sozinho, mas html e body sempre estão por fora. */
        if ((fim - i == 4 && igual_sem_caixa(s + i, "body", 4)) || (fim - i == 4 && igual_sem_caixa(s + i, "html", 4))) continue;
        while (p >= 0 && !bate_composto(s + i, fim - i, abertos[p].nome, abertos[p].classe, abertos[p].id)) p--;
        if (p < 0) return false;
        p--;
    }
    return true;
}

/* Junta o que vale para esta tag: as regras e o atributo style. */
static void css_da_tag(const char *nome, const char *a, int n, regra_t *r)
{
    char classe[200] = "", id[64] = "", st[300];
    int pe = -1, pc = -1, pn = -1, pk = -1, pi = -1, ps = -1, pf = -1, pm = -1, pv = -1, pl = -1, po = -1, pb = -1;
    int pv_m[QTD_V];
    for (int i = 0; i < QTD_V; i++) pv_m[i] = -1;
    memset(r, 0, sizeof *r);
    atributo(a, n, "class", classe, sizeof classe);
    atributo(a, n, "id", id, sizeof id);
    for (int i = 0; i < qtd_regras; i++) {
        const regra_t *g = &regras[i];
        if (!bate_seletor(g->sel, nome, classe, id)) continue;
#define APLICA(campo, marca) if (g->campo && g->peso >= marca) { r->campo = g->campo; marca = g->peso; }
        APLICA(esconde, pe) APLICA(centra, pc) APLICA(negrito, pn) APLICA(cor, pk) APLICA(italico, pi)
        APLICA(sublinha, ps) APLICA(flutua, pf) APLICA(sem_marca, pm) APLICA(vertical, pv) APLICA(colunas, pl)
        APLICA(fundo, po) APLICA(borda, pb)
#undef APLICA
        for (int k = 0; k < QTD_V; k++)
            if (g->v[k] && g->peso >= pv_m[k]) { r->v[k] = g->v[k]; pv_m[k] = g->peso; }
    }
    /* body{display:none} é truque para o JavaScript revelar a página depois. */
    bool raiz = compara_texto(nome, "body") == 0 || compara_texto(nome, "html") == 0;
    if (r->esconde == 1 && (raiz || sem_esconder)) r->esconde = 0;
    if (r->esconde == 1) css_escondeu = true;
    if (atributo(a, n, "style", st, sizeof st)) ler_declaracoes(st, (int)tam_texto(st), r);
    if (atributo(a, n, "align", st, sizeof st) && igual_sem_caixa(st, "center", 6)) r->centra = 1;
    if (compara_texto(nome, "font") == 0 && atributo(a, n, "color", st, sizeof st)) {
        uint32_t c = ler_cor(st, (int)tam_texto(st));
        if (c) r->cor = c;
    }
    if (compara_texto(nome, "body") == 0 && r->fundo) { fundo_pagina = fundo_atual = r->fundo - 1; }
    if (raiz) { if (r->fundo && compara_texto(nome, "html") == 0) fundo_pagina = fundo_atual = r->fundo - 1; r->fundo = 0; }
    if (atributo(a, n, "bgcolor", st, sizeof st)) {
        uint32_t c = ler_cor(st, (int)tam_texto(st));
        if (c && raiz) fundo_pagina = fundo_atual = c - 1; else if (c) r->fundo = c;
    }
    if (atributo(a, n, "width", st, sizeof st) && !r->v[V_L]) { int u; r->v[V_L] = (int16_t)ler_medida(st, (int)tam_texto(st), &u); }
    r->disp = r->esconde;
    r->esconde = r->esconde == 1;
}

static bool bloco(const char *nome);
static bool paragrafo_tag(const char *nome);

static int medir(int v, int base)
{
    if (!v || v == V_AUTO) return 0;
    if (v >= V_PCT) return base * (v - V_PCT) / 100;
    return v - 1;
}

static int em_linhas(int v) { int px = medir(v, 0); return (px + LINHA_A / 2) / LINHA_A; }

/* Filhos diretos de um elemento, com o tamanho do texto de cada um. */
static int contar_filhos(const char *h, int n, uint8_t *peso, int *soma)
{
    int prof = 0, q = 0, tam[MAX_FILHOS];
    char ultimo[8] = "";
    memset(tam, 0, sizeof tam);
    if (n > 131072) n = 131072;
    for (int i = 0; i < n;) {
        if (h[i] != '<') {
            if (prof > 0 && q && q <= MAX_FILHOS && !eh_espaco(h[i])) tam[q - 1]++;
            i++;
            continue;
        }
        if (i + 3 < n && h[i + 1] == '!') { while (i < n && h[i] != '>') i++; i++; continue; }
        bool fecha = i + 1 < n && h[i + 1] == '/';
        int k = i + 1 + (fecha ? 1 : 0), t = 0;
        char nm[8];
        while (k < n && h[k] != '>' && !eh_espaco(h[k]) && h[k] != '/') { if (t < 7) nm[t++] = minuscula(h[k]); k++; }
        nm[t] = 0;
        char aspas = 0;
        while (k < n && (aspas || h[k] != '>')) { if (aspas && h[k] == aspas) aspas = 0; else if (!aspas && (h[k] == '"' || h[k] == '\'')) aspas = h[k]; k++; }
        bool auto_f = k > 0 && h[k - 1] == '/';
        i = k + 1;
        if (!compara_texto(nm, "script") || !compara_texto(nm, "style")) {
            if (fecha) continue;
            const char *f = achar_tag(h + i, n - i, nm[1] == 'c' ? "</script" : "</style");
            if (f) i = (int)(f - h);
            continue;
        }
        bool vazia = !compara_texto(nm, "img") || !compara_texto(nm, "br") || !compara_texto(nm, "hr") || !compara_texto(nm, "input") ||
                     !compara_texto(nm, "meta") || !compara_texto(nm, "link") || !compara_texto(nm, "source") || auto_f;
        if (fecha) { if (--prof < 0) break; continue; }
        bool irmao = prof == 1 && ultimo[0] && (!compara_texto(nm, "td") || !compara_texto(nm, "th") || !compara_texto(nm, "li")) &&
                     (!compara_texto(ultimo, "td") || !compara_texto(ultimo, "th") || !compara_texto(ultimo, "li"));
        if (prof == 0 || irmao) { q++; copia_texto(ultimo, nm, sizeof ultimo); if (!vazia && !irmao) prof++; }
        else if (!vazia) prof++;
        if (!compara_texto(nm, "img") && q && q <= MAX_FILHOS) tam[q - 1] += 16;
    }
    *soma = 0;
    for (int i = 0; i < MAX_FILHOS; i++) {
        int t = tam[i] > 90 ? 90 : tam[i] < 4 ? 4 : tam[i];
        peso[i] = (uint8_t)t;
        if (i < q) *soma += t;
    }
    return q;
}

static void encerrar_fila(aberto_t *f)
{
    quebrar();
    if (f->fundo > linha_atual) linha_atual = f->fundo;
    f->fila = false;
    x_atual = recuo;
}

/* Liga o CSS de um elemento que abriu. resto = o HTML logo depois da tag. */
static void abrir_elemento(const char *nome, const char *a, int n, const regra_t *r, bool de_estilo, const char *resto, int nresto)
{
    if (qtd_abertos >= MAX_ABERTOS) { excesso_abertos++; return; }
    aberto_t *pai = qtd_abertos ? &abertos[qtd_abertos - 1] : NULL;
    aberto_t *e = &abertos[qtd_abertos++];
    memset(e, 0, sizeof *e);
    copia_texto(e->nome, nome, sizeof e->nome);
    atributo(a, n, "class", e->classe, sizeof e->classe);
    atributo(a, n, "id", e->id, sizeof e->id);
    e->ret = -1;
    e->disp = r->disp;
    e->s_recuo = recuo; e->s_dir = dir_atual; e->s_centro = centro; e->s_direita = direita;
    e->s_cor = cor_css; e->s_fundo = fundo_atual; e->s_estilo = estilo; e->s_niveis = niveis; e->s_marca = sem_marca;
    bool tabela = !compara_texto(nome, "td") || !compara_texto(nome, "th") || !compara_texto(nome, "tr") || !compara_texto(nome, "table");
    if (tabela && !r->centra) centro = direita = 0;   /* <center> centra a tabela, não o texto dela */
    bool lista = !compara_texto(nome, "li") || !compara_texto(nome, "ul") || !compara_texto(nome, "ol");
    bool titulo_t = nome[0] == 'h' && nome[1] >= '1' && nome[1] <= '6' && !nome[2];
    bool em_fila = pai && pai->fila;
    bool flut = r->flutua && !em_fila && !lista;
    if (de_estilo) e->bloco = r->disp == 2 || r->disp == 5 || r->disp == 6;
    else e->bloco = r->disp != 3 && r->disp != 4 && (r->disp || tabela || lista || titulo_t || bloco(nome) || paragrafo_tag(nome));
    if (em_fila || flut) e->bloco = true;

    if (e->bloco) {
        quebrar();
        if (em_fila) {
            int area = pai->x_fim - pai->x0, w = medir(r->v[V_L], area);
            if (!w) {
                if (pai->colunas) w = area / pai->colunas;
                else if (pai->filho < MAX_FILHOS && pai->soma) w = area * pai->peso[pai->filho] / pai->soma;
                else w = area / MAX_FILHOS;
            }
            if (w < 48) w = 48;
            if (w > area) w = area;
            /* Sem largura pedida, a célula aceita o que sobrou da linha. */
            if (!r->v[V_L] && pai->x_prox + w > pai->x_fim && pai->x_fim - pai->x_prox >= 48) w = pai->x_fim - pai->x_prox;
            pai->filho++;
            if (pai->x_prox > pai->x0 && pai->x_prox + w > pai->x_fim + 1) {
                pai->x_prox = pai->x0;
                pai->linha0 = pai->fundo;
            }
            int x = r->flutua == 2 ? pai->x_fim - w : pai->x_prox;
            linha_atual = pai->linha0;
            recuo = x;
            dir_atual = x + w - pai->vao;
            if (dir_atual <= recuo + 16) dir_atual = recuo + w;
            e->celula = true;
            e->larg = r->flutua == 2 ? 0 : w;
            brancas = 1;
        } else if (flut && dir_atual - recuo > 200) {
            int area = dir_atual - recuo, w = medir(r->v[V_L], area);
            if (!w) w = area * 2 / 5;
            if (w > area - 120) w = area - 120;
            e->flut = r->flutua;
            e->flut_l = w;
            e->linha0 = linha_atual;
            if (r->flutua == 2) recuo = dir_atual - w; else dir_atual = recuo + w;
            brancas = 1;
        } else {
            int area = dir_atual - recuo;
            int ml = medir(r->v[V_ME], area), mr = medir(r->v[V_MD], area);
            int w = medir(r->v[V_L], area), wm = medir(r->v[V_LMAX], area);
            if (ml + mr >= area) ml = mr = 0;
            int livre = area - ml - mr;
            if (!w || w > livre) w = livre;
            if (wm && wm < w) w = wm;
            if (r->v[V_ME] == V_AUTO && r->v[V_MD] == V_AUTO) ml = (area - w) / 2;
            else if (r->v[V_ME] == V_AUTO) ml = area - w - mr;
            linha_atual += em_linhas(r->v[V_MC]);
            recuo += ml;
            dir_atual = recuo + w;
        }
        e->marg_b = em_linhas(r->v[V_MB]);
        int x0 = recuo, x1 = dir_atual;
        if (r->fundo || r->borda) {
            if (crescer((void **)&rets, &cap_rets, qtd_rets + 1, (int)sizeof(ret_t))) {
                ret_t *q = &rets[qtd_rets];
                q->l0 = q->l1 = linha_atual;
                q->x0 = (int16_t)x0; q->x1 = (int16_t)x1;
                q->fundo = r->fundo; q->borda = r->borda;
                e->ret = qtd_rets++;
            }
            if (r->fundo) fundo_atual = r->fundo - 1;
            brancas = 1;
        }
        int area = dir_atual - recuo;
        int pl = medir(r->v[V_PE], area), pr = medir(r->v[V_PD], area);
        if (pl + pr < area - 32) { recuo += pl; dir_atual -= pr; }
        linha_atual += em_linhas(r->v[V_PC]);
        e->pad_b = em_linhas(r->v[V_PB]);
        x_atual = recuo;
        linha_vazia = true;
    }
    if (e->bloco && r->centra) {
        centro = r->centra == 1; direita = r->centra == 3;
    }
    if (r->cor) cor_css = r->cor;
    int liga = 0, desliga = 0;
    if (r->negrito == 1) liga |= E_NEGRITO; else if (r->negrito == 2) desliga |= E_NEGRITO;
    if (r->italico == 1) liga |= E_ITALICO; else if (r->italico == 2) desliga |= E_ITALICO;
    if (r->sublinha == 1) liga |= E_SEMLINHA; else if (r->sublinha == 2) desliga |= E_SEMLINHA;
    if (r->cor) liga |= E_LINKCOR;
    int fonte = medir(r->v[V_FONTE], 16);
    if (r->v[V_FONTE] && r->v[V_FONTE] < V_PCT) { if (fonte >= 26) liga |= E_GRANDE; else desliga |= E_GRANDE; }
    e->s_mascara = liga | desliga;
    estilo = (estilo | liga) & ~desliga;
    if (r->sem_marca) sem_marca = r->sem_marca == 1;

    bool quer_fila = (r->disp == 5 && r->vertical != 1) || r->disp == 6 || !compara_texto(nome, "tr");
    if (quer_fila && e->bloco) {
        e->fila = true;
        e->colunas = r->disp == 6 ? (r->colunas ? r->colunas : 1) : 0;
        e->qtd = contar_filhos(resto, nresto, e->peso, &e->soma);
        e->x0 = e->x_prox = recuo; e->x_fim = dir_atual;
        e->linha0 = e->fundo = linha_atual;
        e->vao = r->v[V_VAO] ? medir(r->v[V_VAO], 0) : 8;
    }
}

static void desfazer(aberto_t *e, aberto_t *pai)
{
    if (e->fila) encerrar_fila(e);
    if (e->bloco) {
        quebrar();
        linha_atual += e->pad_b;
    }
    if (e->ret >= 0) rets[e->ret].l1 = linha_atual;
    if (e->bloco) linha_atual += e->marg_b;
    if (linha_atual > linhas) linhas = linha_atual;
    int extra = 0;
    for (int i = e->s_niveis; i < niveis && i < 8; i++) extra += recuo_lista[i];
    recuo = e->s_recuo + extra;
    dir_atual = e->s_dir; centro = e->s_centro; direita = e->s_direita;
    cor_css = e->s_cor; fundo_atual = e->s_fundo; sem_marca = e->s_marca;
    estilo = (estilo & ~e->s_mascara) | (e->s_estilo & e->s_mascara);
    if (e->flut) {
        int l = linha_atual;
        linha_atual = e->linha0;
        if (e->flut == 2) {
            if (!fd_fundo) fd_orig = dir_atual;
            dir_atual -= e->flut_l + 10;
            fd_lim = dir_atual;
            if (l > fd_fundo) fd_fundo = l;
        } else {
            if (!fe_fundo) fe_orig = recuo;
            recuo += e->flut_l + 10;
            fe_lim = recuo;
            if (l > fe_fundo) fe_fundo = l;
        }
        brancas = 1;
    }
    ajustar_floats();
    if (e->bloco) { x_atual = recuo; linha_vazia = true; }
    if (e->celula && pai) {
        if (linha_atual > pai->fundo) pai->fundo = linha_atual;
        pai->x_prox += e->larg;
        if (pai->fila) linha_atual = pai->linha0;
    }
}

static void fechar_elemento(const char *nome, const char *barreira)
{
    if (excesso_abertos) { excesso_abertos--; return; }
    int i = qtd_abertos - 1;
    for (; i >= 0; i--) {
        if (compara_texto(abertos[i].nome, nome) == 0) break;
        if (barreira && compara_texto(abertos[i].nome, barreira) == 0) return;
    }
    if (i < 0) return;
    while (qtd_abertos > i) {
        aberto_t *e = &abertos[--qtd_abertos];
        ultimo_disp = e->disp;
        desfazer(e, qtd_abertos ? &abertos[qtd_abertos - 1] : NULL);
    }
}

/* <li>, <td>, <tr> e <p> fecham sozinhos quando o próximo irmão abre. */
static void fechar_implicito(const char *nome)
{
    aberto_t *t = qtd_abertos ? &abertos[qtd_abertos - 1] : NULL;
    if (!compara_texto(nome, "li")) fechar_elemento("li", niveis ? "ul" : NULL);
    else if (!compara_texto(nome, "td") || !compara_texto(nome, "th")) { fechar_elemento("td", "tr"); fechar_elemento("th", "tr"); }
    else if (!compara_texto(nome, "tr")) fechar_elemento("tr", "table");
    else if (!compara_texto(nome, "p") && t && !compara_texto(t->nome, "p")) fechar_elemento("p", NULL);
}

/* <link rel=stylesheet>: baixa e lê, dentro de um limite de tempo e de arquivos. */
static void folha_externa(const char *a, int n)
{
    if (motor) return;
    char v[URL_MAX], end[URL_MAX];
    if (sem_esconder || qtd_folhas >= 6 || tempo_ms() - inicio_pagina > 10000) return;
    if (!atributo(a, n, "rel", v, sizeof v) || !igual_sem_caixa(v, "stylesheet", 10)) return;
    if (atributo(a, n, "media", v, sizeof v) && !media_aceita(v, (int)tam_texto(v))) return;
    if (!atributo(a, n, "href", v, sizeof v) || !juntar_url(url_atual, v, end, sizeof end)) return;
    qtd_folhas++;
    rodape("Baixando estilos...");
    unsigned t = 0;
    uint8_t *d = baixar_imagem(end, &t);
    if (!d) return;
    ler_css((const char *)d, (int)t);
    libera(d);
    registrar("cipo: folha %d bytes, %d regras\n", (int)t, qtd_regras);
}

static bool escondido(const char *nome, const char *a, int n)
{
    static const char *const tags[] = { "nav", "aside", "footer", "noscript", "dialog", "select", "iframe" };
    static const char *const classes[] = { "noprint", "mw-editsection", "navbox", "sr-only", "visually-hidden",
                                           "vector-menu", "vector-toc", "vector-dropdown", "vector-page-toolbar", "vector-header",
                                           "vector-sticky", "vector-column-start", "vector-pinn", "skip-", "mw-jump", "cookie" };
    char v[200];
    for (unsigned i = 0; i < sizeof tags / sizeof tags[0]; i++)
        if (compara_texto(nome, tags[i]) == 0) return true;
    if (compara_texto(nome, "img") == 0 || compara_texto(nome, "input") == 0 || compara_texto(nome, "br") == 0 ||
        compara_texto(nome, "hr") == 0 || compara_texto(nome, "meta") == 0 || compara_texto(nome, "link") == 0) return false;
    if (atributo(a, n, "style", v, sizeof v)) {
        for (char *p = v; *p; p++) {
            if (comeca_com(p, "display:none") || comeca_com(p, "display: none")) return true;
        }
    }
    if (atributo(a, n, "class", v, sizeof v))
        for (unsigned i = 0; i < sizeof classes / sizeof classes[0]; i++)
            for (char *p = v; *p; p++)
                if (comeca_com(p, classes[i])) return true;
    /* "hidden" só como classe inteira: "overflow-hidden" não esconde nada. */
    if (atributo(a, n, "class", v, sizeof v))
        for (char *p = v; *p; p++)
            if (comeca_com(p, "hidden") && (p == v || p[-1] == ' ') && (!p[6] || p[6] == ' ')) return true;
    return false;
}

/* Com <main>, só ele interessa: é onde o site diz que está o conteúdo. */
static const char *achar_tag(const char *h, int n, const char *tag)
{
    int t = (int)tam_texto(tag);
    for (int i = 0; i + t < n; i++)
        if (h[i] == '<' && igual_sem_caixa(h + i, tag, t) && (h[i + t] == ' ' || h[i + t] == '>')) return h + i;
    return NULL;
}

static void achar_titulo(const char *h, int n)
{
    const char *t = achar_tag(h, n, "<title");
    if (!t) return;
    while (t < h + n && *t != '>') t++;
    t++;
    int k = 0;
    while (t + k < h + n && t[k] != '<' && k < (int)sizeof titulo - 1) { titulo[k] = t[k]; k++; }
    titulo[k] = 0;
}

static void ler_html(const char *h, int n);

/* ---- JavaScript ----
 * O motor (libsagui/js.c) monta um DOM do HTML, roda os scripts e devolve o
 * HTML resultante. Elementos com clique viram links sagui-js:N, que voltam
 * para o motor sem passar pela rede. */
static void js_titulo(const char *t) { copia_texto(titulo_js, t, sizeof titulo_js); utf8_para_sistema(titulo_js); }

/* Sem caixa modal: um site com alert em laço travaria o Cipó. Vai para o rodapé. */
static void js_alerta(const char *t)
{
    registrar("cipo: alert %s\n", t);
    formatar_texto(alerta_js, sizeof alerta_js, "Alerta da página: %s", t);
}

/* Cache dos recursos da página (CSS e imagens): a página viva é relida a
 * cada mudança do DOM e não deve baixar tudo de novo. Limpo ao navegar. */
#define CACHE_N 48
#define CACHE_MAX (6u << 20)
static struct { char *url; uint8_t *d; unsigned n; } cache[CACHE_N];
static int qtd_cache; static unsigned cache_total;
static unsigned ult_tique, ult_redesenho, hash_viva;
static void cache_limpar(void)
{
    for (int i = 0; i < qtd_cache; i++) { libera(cache[i].url); libera(cache[i].d); }
    qtd_cache = 0; cache_total = 0;
}
static uint8_t *copia_bloco(const uint8_t *d, unsigned n)
{
    uint8_t *c = aloca(n + 1);
    if (c) { memcpy(c, d, n); c[n] = 0; }
    return c;
}
static uint8_t *baixar_recurso(const char *u, unsigned *n);
/* fetch/XHR: corpo de qualquer código, com código, cabeçalhos e URL final */
static uint8_t *js_resposta(const char *u, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax)
{
    char end[URL_MAX];
    *codigo = 0; cab[0] = 0; final[0] = 0;
    if (!juntar_url(url_atual, u, end, sizeof end)) return NULL;
    uint8_t *d = baixar_resp(end, n, true);
    *codigo = resp_codigo;
    copia_texto(cab, resp_cab, (size_t)cmax);
    copia_texto(final, resp_url, (size_t)fmax);
    return d;
}
static uint8_t *js_baixar(const char *u, unsigned *n)
{
    char end[URL_MAX];
    if (!juntar_url(url_atual, u, end, sizeof end)) return NULL;
    char antes[sizeof rodape_texto];
    copia_texto(antes, rodape_texto, sizeof antes);
    rodape("Baixando scripts...");
    uint8_t *d = baixar_imagem(end, n);
    rodape(antes);   /* depois, o rodapé volta ao que era */
    return d;
}

/* O player de uma página (YouTube) deu src a um <video>: abre o tocador. */
static void abrir_video_da_pagina(void)
{
    const char *u = js_video();
    if (!u || !(comeca_com(u, "https://") || comeca_com(u, "http://"))) return;
    char *args[] = { "video", (char *)u, NULL };
    int pid = executar("/bin/video", args);
    registrar("cipo: video da pagina (%d bytes) pid %d\n", (int)tam_texto(u), pid);
    if (pid > 0) rodape("Vídeo aberto no tocador");
}

static uint8_t *baixar_recurso(const char *u, unsigned *n)
{
    char end[URL_MAX];
    if (!juntar_url(url_atual, u, end, sizeof end)) return NULL;
    for (int i = 0; i < qtd_cache; i++)
        if (compara_texto(cache[i].url, end) == 0) { *n = cache[i].n; return copia_bloco(cache[i].d, cache[i].n); }
    uint8_t *d = js_baixar(u, n);
    if (d && qtd_cache < CACHE_N && cache_total + *n <= CACHE_MAX) {
        int k = (int)tam_texto(end);
        char *cu = aloca((unsigned)k + 1);
        uint8_t *cd = copia_bloco(d, *n);
        if (cu && cd) { memcpy(cu, end, (unsigned)k + 1); cache[qtd_cache].url = cu; cache[qtd_cache].d = cd; cache[qtd_cache].n = *n; qtd_cache++; cache_total += *n; }
        else { if (cu) libera(cu); if (cd) libera(cd); }
    }
    return d;
}

/* Layout pedido por um script (medidas): um motor à parte diagrama o DOM
 * atual com o CSS em cache e devolve as caixas. Fica vivo para o
 * getComputedStyle até o próximo pedido ou a próxima página. */
static motor_t *medidor;
static uint8_t *baixar_recurso(const char *u, unsigned *n);
static bool decodificar_registrando(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a);
static int js_layout(const char *html, int n, int *caixas, int qtd)
{
    static const motor_ganchos_t g = { baixar_recurso, decodificar_registrando, tela2d_pixels };
    unsigned t0 = tempo_ms();
    if (medidor) motor_fim(medidor);
    medidor = motor_novo(&g);
    if (!medidor) return 0;
    motor_ler(medidor, html, n, url_atual);
    motor_layout(medidor, LARG - ROLAGEM, AREA_A);
    motor_caixas(medidor, caixas, qtd);
    registrar("cipo: layout para o script (%d elementos) em %u ms\n", qtd, tempo_ms() - t0);
    return 1;
}
static bool js_estilo(int k, const char *prop, char *o, int max) { return medidor && motor_estilo_de(medidor, k, prop, o, max); }
static struct fonte *js_fonte(const char *fam, int peso, int ital, int larg) { return medidor ? motor_fonte_nomeada(medidor, fam, peso, ital, larg) : NULL; }

/* iframes: cada quadro tem o seu motor (para pintar e para achar o clique) */
#define MAX_QM 16
static struct { int tela; motor_t *m; } qm[MAX_QM];
static int js_quadro_pintar(int tela, const char *html, int n, const char *url, int l, int a)
{
    static const motor_ganchos_t g = { baixar_recurso, decodificar_registrando, tela2d_pixels };
    int k = 0;
    while (k < MAX_QM && !(qm[k].m && qm[k].tela == tela)) k++;
    if (k == MAX_QM) { k = 0; while (k < MAX_QM && qm[k].m) k++; }
    if (k == MAX_QM) return 0;
    unsigned t0 = tempo_ms();
    if (qm[k].m) motor_fim(qm[k].m);
    motor_t *m = motor_novo(&g);
    qm[k].m = m; qm[k].tela = tela;
    if (!m) return 0;
    motor_ler(m, html, n, url);
    motor_layout(m, l, a);
    while (motor_imagens(m, 8)) motor_layout(m, l, a);
    int tl, ta;
    uint32_t *px = tela2d_px_escrita(tela, &tl, &ta);
    if (!px) return 0;
    int w = l < tl ? l : tl, h = a < ta ? a : ta;
    motor_pintar(m, px, w, tl, 0, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px[y * tl + x] |= 0xFF000000u;
    registrar("cipo: quadro (tela %d) %dx%d pintado em %u ms\n", tela, w, h, tempo_ms() - t0);
    return 1;
}
static int js_quadro_alvo(int tela, int x, int y)
{
    for (int k = 0; k < MAX_QM; k++) {
        if (!qm[k].m || qm[k].tela != tela) continue;
        const motor_alvo_t *a = motor_alvo(qm[k].m, x, y);
        if (!a) return -1;
        if (a->js >= 0) return a->js;
        if (a->href && comeca_com(a->href, "sagui-js:")) return texto_para_int(a->href + 9);
        return -1;
    }
    return -1;
}
static void quadros_esquecer(void) { for (int k = 0; k < MAX_QM; k++) { if (qm[k].m) motor_fim(qm[k].m); qm[k].m = NULL; } }

static char *rodar_scripts(char *h, int *n)
{
    static const js_hooks_t ganchos = { js_baixar, js_titulo, js_alerta, js_resposta, js_layout, js_estilo, js_fonte, js_quadro_pintar, js_quadro_alvo };
    quadros_esquecer();
    if (!achar_tag(h, *n, "<script")) return h;
    rodape("Rodando scripts...");
    int t = 0;
    unsigned t0 = tempo_ms(), r0 = t_rede;
    js_janela(LARG - ROLAGEM, AREA_A, 0, 0);
    char *novo = js_pagina(h, *n, url_atual, &ganchos, &t);
    t_js += (tempo_ms() - t0) - (t_rede - r0);
    abrir_video_da_pagina();
    if (!novo) return h;
    *n = t;
    return novo;
}

static uint8_t *js_baixar(const char *u, unsigned *n);
static bool decodificar_registrando(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a)
{
    bool ok = imagem_decodificar(d, n, px, l, a);
    if (ok) registrar("cipo: imagem %dx%d\n", *l, *a);
    else registrar("cipo: imagem nao decodificou (%d bytes)\n", (int)n);
    return ok;
}
static void ler_pagina(const char *h, int n)
{
    static const motor_ganchos_t g = { baixar_recurso, decodificar_registrando, tela2d_pixels };
    if (motor) motor_fim(motor);
    motor = motor_novo(&g);
    if (motor) {
        unsigned t0 = tempo_ms();
        motor_ler(motor, h, n, url_atual);
        registrar("cipo: motor leu em %u ms\n", tempo_ms() - t0);
        copia_texto(titulo, motor_titulo(motor), sizeof titulo);
        utf8_para_sistema(titulo);
        return;   /* o motor substitui o leitor antigo */
    }
    const char *m = achar_tag(h, n, "<main");
    const char *f = m ? achar_tag(m, n - (int)(m - h), "</main") : NULL;
    if (!m) { ler_html(h, n); return; }
    achar_titulo(h, n);
    for (const char *p = achar_tag(h, (int)(m - h), "<link"); p; p = achar_tag(p + 5, (int)(m - p) - 5, "<link")) {
        const char *fim_l = p;
        while (fim_l < m && *fim_l != '>') fim_l++;
        folha_externa(p + 5, (int)(fim_l - p) - 5);
    }
    for (const char *p = achar_tag(h, (int)(m - h), "<style"); p; p = achar_tag(p + 6, (int)(m - p) - 6, "<style")) {
        const char *ini = p;
        while (ini < m && *ini != '>') ini++;
        const char *fim_c = achar_tag(ini, (int)(m - ini), "</style");
        if (fim_c) ler_css(ini + 1, (int)(fim_c - ini) - 1);
    }
    ler_html(m, (int)((f ? f : h + n) - m));
}

/* A imagem de verdade: sites guardam em data-src ou srcset e deixam um
 * marcador em src até o JavaScript trocar. */
static bool fonte_imagem(const char *a, int n, char *v, int max)
{
    static const char *const atrasadas[] = { "data-src", "data-lazy-src", "data-original", "data-lazy", "data-url" };
    static char lista[4096];
    for (unsigned i = 0; i < sizeof atrasadas / sizeof atrasadas[0]; i++)
        if (atributo(a, n, atrasadas[i], v, max) && v[0] && !comeca_com(v, "data:")) return true;
    if (atributo(a, n, "srcset", lista, sizeof lista) || atributo(a, n, "data-srcset", lista, sizeof lista)) {
        int melhor = -1;
        for (const char *p = lista; *p;) {
            while (*p == ' ' || *p == ',' || *p == '\n') p++;
            const char *u = p;
            while (*p && *p != ' ' && *p != '\n') p++;
            int t = (int)(p - u);
            if (t && u[t - 1] == ',') t--;
            while (*p == ' ') p++;
            int w = 1;
            if (*p >= '0' && *p <= '9') { w = texto_para_int(p); while (*p && *p != ',') p++; }
            /* a maior que não passe muito da página; acima disso, só se não houver outra */
            bool boa = melhor < 0 || (w <= 900 ? w > melhor || melhor > 900 : w < melhor);
            if (t && t < max && boa) { memcpy(v, u, (size_t)t); v[t] = 0; melhor = w; }
        }
        if (melhor >= 0 && !comeca_com(v, "data:")) return true;
    }
    return atributo(a, n, "src", v, max) && v[0] && !comeca_com(v, "data:");
}

static char oculto[16];
static int oculto_prof;

static void ler_html(const char *h, int n)
{
    int pular_ate = -1;   /* dentro de <script> ou <style>: tudo até o fechamento */
    char palavra_buf[256];
    int pk = 0;
#define SOLTAR() do { if (pk) { palavra_buf[pk] = 0; if (em_titulo) { junta_texto(titulo, palavra_buf, sizeof titulo); } else palavra(palavra_buf, pk); pk = 0; } } while (0)
    for (int i = 0; i < n;) {
        char c = h[i];
        if (c == '<') {
            if (i + 3 < n && h[i + 1] == '!' && h[i + 2] == '-' && h[i + 3] == '-') {   /* comentário */
                int k = i + 4;
                while (k + 2 < n && !(h[k] == '-' && h[k + 1] == '-' && h[k + 2] == '>')) k++;
                i = k + 3;
                continue;
            }
            int k = i + 1;
            bool fecha = k < n && h[k] == '/';
            if (fecha) k++;
            if (k >= n || (!letra(h[k]) && h[k] != '!')) {   /* "<" solto no texto */
                if (pular_ate < 0 && pk < (int)sizeof palavra_buf - 1) palavra_buf[pk++] = '<';
                i++;
                continue;
            }
            char nome[16];
            int nn = 0;
            while (k < n && (letra(h[k]) || (h[k] >= '0' && h[k] <= '9') || h[k] == '!') && nn < 15) nome[nn++] = minuscula(h[k++]);
            nome[nn] = 0;
            int ini_attr = k;
            char aspas = 0;
            while (k < n && (aspas || h[k] != '>')) {
                if (aspas && h[k] == aspas) aspas = 0;
                else if (!aspas && (h[k] == '"' || h[k] == '\'')) aspas = h[k];
                k++;
            }
            const char *attrs = h + ini_attr;
            int na = k - ini_attr;
            i = k + 1;
            if (pular_ate == 2) {                /* elemento escondido: conta os iguais aninhados */
                if (compara_texto(nome, oculto) == 0 && !(na > 0 && attrs[na - 1] == '/')) oculto_prof += fecha ? -1 : 1;
                if (!oculto_prof) pular_ate = -1;
                continue;
            }
            if (pular_ate >= 0) {
                if (fecha && pular_ate == 1 && (compara_texto(nome, "script") == 0 || compara_texto(nome, "style") == 0 ||
                                                compara_texto(nome, "svg") == 0 || compara_texto(nome, "template") == 0))
                    pular_ate = -1;
                continue;
            }
            if (!fecha && compara_texto(nome, "style") == 0) {
                const char *f = achar_tag(h + i, n - i, "</style");
                if (f) ler_css(h + i, (int)(f - (h + i)));
            }
            if (!fecha && (compara_texto(nome, "script") == 0 || compara_texto(nome, "style") == 0 ||
                           compara_texto(nome, "svg") == 0 || compara_texto(nome, "template") == 0)) {
                if (!(na > 0 && attrs[na - 1] == '/')) pular_ate = 1;
                continue;
            }
            if (!fecha && compara_texto(nome, "link") == 0) { folha_externa(attrs, na); continue; }
            regra_t css;
            if (!fecha) css_da_tag(nome, attrs, na, &css);
            if (!fecha && (css.esconde || escondido(nome, attrs, na))) {
                SOLTAR();
                copia_texto(oculto, nome, sizeof oculto);
                oculto_prof = 1;
                pular_ate = 2;
                continue;
            }
            SOLTAR();
            bool vazia = compara_texto(nome, "img") == 0 || compara_texto(nome, "br") == 0 || compara_texto(nome, "hr") == 0 ||
                         compara_texto(nome, "input") == 0 || compara_texto(nome, "meta") == 0 || compara_texto(nome, "link") == 0;
            /* Uma tag sempre separa palavras (menos as de estilo do texto). */
            bool de_estilo = compara_texto(nome, "b") == 0 || compara_texto(nome, "strong") == 0 ||
                             compara_texto(nome, "i") == 0 || compara_texto(nome, "em") == 0 ||
                             compara_texto(nome, "a") == 0 || compara_texto(nome, "span") == 0 ||
                             compara_texto(nome, "font") == 0 || compara_texto(nome, "small") == 0 ||
                             compara_texto(nome, "code") == 0 || compara_texto(nome, "u") == 0;
            if (!de_estilo) { SOLTAR(); espaco_pendente = true; }
            else SOLTAR();
            ultimo_disp = 0;
            if (fecha) fechar_elemento(nome, NULL);
            else if (!vazia && !(na > 0 && attrs[na - 1] == '/')) {
                fechar_implicito(nome);
                abrir_elemento(nome, attrs, na, &css, de_estilo, h + i, n - i);
            }
            {   /* bloco tornado inline pelo CSS: vira um pedaço da linha */
                int d = fecha ? ultimo_disp : css.disp;
                if ((d == 3 || d == 4) && !de_estilo && (bloco(nome) || paragrafo_tag(nome) || !compara_texto(nome, "li"))) { espaco_pendente = true; continue; }
            }
            char v[URL_MAX];
            if (compara_texto(nome, "title") == 0) { em_titulo = !fecha; continue; }
            if (compara_texto(nome, "center") == 0) {
                quebrar();
                centro += fecha ? -1 : 1;
                if (centro < 0) centro = 0;
                continue;
            }
            if (compara_texto(nome, "a") == 0 && fecha && i < n && h[i] == '<' && i + 2 < n && h[i + 1] == 'a') espaco_pendente = true;   /* links colados no HTML */
            if (compara_texto(nome, "br") == 0) {
                if (linha_vazia) linha_atual++;
                quebrar();
                continue;
            }
            if (compara_texto(nome, "img") == 0) {
                char endereco_img[URL_MAX];
                if (fonte_imagem(attrs, na, v, sizeof v) && juntar_url(url_atual, v, endereco_img, sizeof endereco_img) &&
                    colocar_imagem(endereco_img, medir(css.v[V_L], dir_atual - recuo))) continue;
                if (atributo(attrs, na, "alt", v, sizeof v) && v[0] && compara_texto(v, "noicon")) {
                    int e = estilo;
                    estilo |= E_ITALICO;
                    palavra("[", 1);
                    texto_solto(v);
                    palavra("]", 1);
                    estilo = e;
                }
                continue;
            }
            if (compara_texto(nome, "hr") == 0) {
                quebrar();
                segmento("", 0, E_LINHA, -1);
                segs[qtd_segs - 1].n = (int16_t)(dir_atual - recuo);
                quebrar();
                continue;
            }
            if (nome[0] == 'h' && nome[1] >= '1' && nome[1] <= '6' && !nome[2]) {
                paragrafo();
                if (fecha) estilo &= ~(E_TITULO | E_GRANDE);
                else {
                    estilo |= E_TITULO;
                    if (nome[1] <= '2' && !(css.v[V_FONTE] && medir(css.v[V_FONTE], 16) < 24)) {
                        abertos[qtd_abertos ? qtd_abertos - 1 : 0].s_mascara |= E_GRANDE;
                        estilo |= E_GRANDE;
                    }
                }
                continue;
            }
            if (compara_texto(nome, "ul") == 0 || compara_texto(nome, "ol") == 0) {
                if (!fecha && niveis < 8) {
                    recuo_lista[niveis] = css.sem_marca == 1 || sem_marca ? 0 : 24;
                    recuo += recuo_lista[niveis];
                    pilha_lista[niveis++] = nome[0] == 'o' ? 1 : 0;
                } else if (fecha && niveis > 0) recuo -= recuo_lista[--niveis];
                if (niveis == 0 || !fecha) paragrafo(); else quebrar();
                continue;
            }
            if (compara_texto(nome, "li") == 0) {
                quebrar();
                if (!fecha && !sem_marca) {
                    char marca[8];
                    if (niveis && pilha_lista[niveis - 1]) formatar_texto(marca, sizeof marca, "%d.", pilha_lista[niveis - 1]++);
                    else { marca[0] = (char)0xB7; marca[1] = 0; }
                    int e = estilo;
                    estilo &= ~E_LINK;
                    x_atual = recuo - 16 > 0 ? recuo - 16 : 0;
                    segmento(marca, (int)tam_texto(marca), estilo, -1);
                    x_atual = recuo;
                    estilo = e;
                }
                continue;
            }
            if (compara_texto(nome, "b") == 0 || compara_texto(nome, "strong") == 0 || compara_texto(nome, "th") == 0) {
                negrito_n += fecha ? -1 : 1;
                if (negrito_n < 0) negrito_n = 0;
                if (negrito_n) estilo |= E_NEGRITO; else estilo &= ~E_NEGRITO;
                continue;
            }
            if (compara_texto(nome, "i") == 0 || compara_texto(nome, "em") == 0) {
                italico_n += fecha ? -1 : 1;
                if (italico_n < 0) italico_n = 0;
                if (italico_n) estilo |= E_ITALICO; else estilo &= ~E_ITALICO;
                continue;
            }
            if (compara_texto(nome, "pre") == 0) {
                paragrafo();
                pre_n += fecha ? -1 : 1;
                if (pre_n < 0) pre_n = 0;
                if (pre_n) estilo |= E_PRE; else estilo &= ~E_PRE;
                continue;
            }
            if (compara_texto(nome, "a") == 0) {
                if (fecha) { alvo_atual = -1; estilo &= ~E_LINK; continue; }
                char url[URL_MAX];
                if (atributo(attrs, na, "href", v, sizeof v) && juntar_url(url_atual, v, url, sizeof url)) {
                    destino_bing(url, sizeof url);
                    alvo_atual = novo_alvo(A_LINK, url, -1, NULL);
                    if (alvo_atual >= 0) estilo |= E_LINK;
                }
                continue;
            }
            if (compara_texto(nome, "td") == 0) continue;
            if (compara_texto(nome, "img") == 0) {
                if (atributo(attrs, na, "alt", v, 80) && v[0]) {
                    char t[96];
                    formatar_texto(t, sizeof t, "[%s]", v);
                    int e = estilo;
                    estilo |= E_ITALICO;
                    texto_solto(t);
                    estilo = e;
                } else if (alvo_atual >= 0) {
                    palavra("[imagem]", 8);
                }
                continue;
            }
            if (compara_texto(nome, "form") == 0) {
                quebrar();
                if (fecha) { form_atual = -1; continue; }
                form_atual = -1;
                char metodo[8] = "get";
                atributo(attrs, na, "method", metodo, sizeof metodo);
                if (qtd_forms < 8 && minuscula(metodo[0]) == 'g') {
                    char acao[URL_MAX];
                    if (!atributo(attrs, na, "action", v, sizeof v) || !v[0] || !juntar_url(url_atual, v, acao, sizeof acao))
                        copia_texto(acao, url_atual, sizeof acao);
                    char *q = acha_char(acao, '?');
                    if (q) *q = 0;
                    form_t *f = &forms[qtd_forms];
                    f->acao = guardar_texto(acao, (int)tam_texto(acao));
                    f->fixa[0] = 0;
                    f->campo = -1;
                    form_atual = qtd_forms++;
                }
                continue;
            }
            if (compara_texto(nome, "input") == 0 && !fecha) {
                char tipo[16] = "text", nm[64] = "", valor[64] = "";
                atributo(attrs, na, "type", tipo, sizeof tipo);
                atributo(attrs, na, "name", nm, sizeof nm);
                atributo(attrs, na, "value", valor, sizeof valor);
                for (char *p = tipo; *p; p++) *p = minuscula(*p);
                form_t *f = form_atual >= 0 ? &forms[form_atual] : NULL;
                if (compara_texto(tipo, "hidden") == 0) {
                    if (f && nm[0]) {
                        char par[140];
                        formatar_texto(par, sizeof par, "%s%s=%s", f->fixa[0] ? "&" : "", nm, valor);
                        junta_texto(f->fixa, par, sizeof f->fixa);
                    }
                } else if (compara_texto(tipo, "text") == 0 || compara_texto(tipo, "search") == 0) {
                    if (f && nm[0] && f->campo < 0) f->campo = guardar_texto(nm, (int)tam_texto(nm));
                    int a = f && nm[0] ? novo_alvo(A_CAMPO, NULL, form_atual, nm) : -1;
                    int e = estilo;
                    estilo = E_CAMPO;
                    int al = alvo_atual;
                    alvo_atual = a;
                    palavra(valor[0] ? valor : "______________________", valor[0] ? (int)tam_texto(valor) : 22);
                    alvo_atual = al;
                    estilo = e;
                } else if (compara_texto(tipo, "submit") == 0) {
                    char t[80];
                    formatar_texto(t, sizeof t, "[%s]", valor[0] ? valor : "Enviar");
                    char campo[64] = "";
                    if (f && f->campo >= 0) copia_texto(campo, textos + f->campo, sizeof campo);   /* textos pode mudar de lugar */
                    int a = campo[0] ? novo_alvo(A_CAMPO, NULL, form_atual, campo) : -1;
                    int e = estilo, al = alvo_atual;
                    estilo = E_CAMPO;
                    alvo_atual = a;
                    palavra(t, (int)tam_texto(t));
                    alvo_atual = al;
                    estilo = e;
                }
                continue;
            }
            if (paragrafo_tag(nome)) { paragrafo(); continue; }
            if (bloco(nome)) { quebrar(); continue; }
            continue;
        }
        if (pular_ate >= 0) { i++; continue; }
        /* Texto. */
        if (c == '&') {
            char ch;
            int u = entidade(h + i, n - i, &ch);
            if (u) { if (pk < (int)sizeof palavra_buf - 1) palavra_buf[pk++] = ch; i += u; continue; }
        }
        if (pre_n && !em_titulo) {
            if (c == '\n') { SOLTAR(); linha_vazia = false; quebrar(); i++; continue; }
            if (c == '\r') { i++; continue; }
            if (c == '\t') c = ' ';
            if (c == ' ') { SOLTAR(); x_atual += LETRA_L; i++; continue; }
        } else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            SOLTAR();
            if (em_titulo) { if (titulo[0]) junta_texto(titulo, " ", sizeof titulo); }
            else espaco_pendente = true;
            i++;
            continue;
        }
        if (pk < (int)sizeof palavra_buf - 1) palavra_buf[pk++] = c;
        else { SOLTAR(); palavra_buf[pk++] = c; }
        i++;
    }
    SOLTAR();
#undef SOLTAR
}

static void limpar_pagina(void)
{
    if (motor) { motor_fim(motor); motor = NULL; }
    motor_altura = 0;
    qtd_segs = qtd_alvos = qtd_textos = qtd_forms = 0;
    for (int i = 0; i < qtd_imagens; i++) libera(imagens[i].px);
    qtd_imagens = 0;
    linhas = topo = 0; rol_px = 0; arrastando_barra = false;
    centro = ini_linha = 0;
    qtd_regras = qtd_folhas = qtd_abertos = excesso_abertos = qtd_rets = qtd_vars = 0;
    dir_atual = LARG_TEXTO; direita = 0;
    fd_fundo = fe_fundo = 0; fd_lim = fe_lim = -1; sem_marca = false;
    fundo_atual = fundo_pagina = 0xFFFFFF;
    css_escondeu = false;
    cor_css = 0;
    linha_grande = false;
    x_atual = linha_atual = recuo = estilo = 0;
    alvo_atual = form_atual = -1;
    linha_vazia = true;
    espaco_pendente = false;
    brancas = 1;
    niveis = negrito_n = italico_n = pre_n = 0;
    em_titulo = false;
    titulo[0] = 0;
}

/* ------------------------------------------------------------ desenho */

static void desenhar_rodape(void)
{
    g_retangulo(j, 0, ALT - RODAPE, LARG, RODAPE, 0xE6E9EC);
    g_retangulo(j, 0, ALT - RODAPE, LARG, 1, COR_BORDA);
    g_texto_n(j, 8, ALT - RODAPE + 3, rodape_texto, (LARG - 16) / LETRA_L, COR_TEXTO_FRACO);
    jan_mostrar_area(j, 0, ALT - RODAPE, LARG, RODAPE);
}

static void rodape(const char *t)
{
    if (compara_texto(t, rodape_texto) == 0) return;
    copia_texto(rodape_texto, t, sizeof rodape_texto);
    desenhar_rodape();
}

static uint32_t cor_de(int e)
{
    if (e & E_CAMPO) return 0x3A4450;
    if (e & E_LINK) return 0x1A4FBF;
    if (e & E_TITULO) return 0x0B7A3E;
    if (e & E_PRE) return 0x5A2A82;
    if (e & E_ITALICO) return 0x4A5560;
    return COR_TEXTO;
}

/* O certificado é verificado, mas sem RDRAND as chaves da conexão são fracas. */
static const char *texto_pronto(void)
{
    if (nota_busca[0]) return nota_busca;
    if (alerta_js[0]) return alerta_js;
    return pagina_segura ? "Site verificado. A segurança ainda é experimental: evite senhas." : "Pronto";
}

extern const unsigned char fonte_sagui[256][16];

/* Letra em dobro, para os títulos grandes. */
static void texto_grande(int x, int y, const char *t, int n, uint32_t cor)
{
    for (int i = 0; i < n; i++, x += 2 * LETRA_L) {
        const unsigned char *g = fonte_sagui[(uint8_t)t[i]];
        for (int l = 0; l < 16; l++)
            for (int c = 0; c < 8; c++)
                if (g[l] & (0x80 >> c)) {
                    int yy = y + 2 * l;
                    if (yy >= TOPO && yy + 2 <= ALT - RODAPE) g_retangulo(j, x + 2 * c, yy, 2, 2, cor);
                }
    }
}

/* Geometria da rolagem, igual para as duas páginas: tudo em pixels. */
static int total_px(void) { return motor ? motor_altura : linhas * LINHA_A + 8; }
static int pos_px(void) { return motor ? rol_px : topo * LINHA_A; }
static int max_px(void) { int m = total_px() - AREA_A; return m > 0 ? m : 0; }
/* Polegar da barra: posição e altura na tela. */
static bool polegar(int *y, int *a)
{
    int t = total_px();
    if (t <= AREA_A) return false;
    *a = AREA_A * AREA_A / t;
    if (*a < 20) *a = 20;
    *y = TOPO + (int)((unsigned)(AREA_A - *a) * (unsigned)pos_px() / (unsigned)max_px());
    return true;
}

static void desenhar_barra_rolagem(void)
{
    int bx = LARG - ROLAGEM, y, a;
    g_retangulo(j, bx, TOPO, ROLAGEM, AREA_A, 0xE6E9EC);
    g_retangulo(j, bx, TOPO, 1, AREA_A, COR_BORDA);
    if (polegar(&y, &a)) g_retangulo(j, bx + 3, y + 2, ROLAGEM - 5, a - 4, arrastando_barra ? 0x5F6770 : 0x8A939C);
}

static void motor_diagramar(void)
{
    if (!motor) return;
    registrar("cipo: motor diagramando\n");
    unsigned t0 = tempo_ms();
    motor_altura = motor_layout(motor, LARG - ROLAGEM, AREA_A);
    linhas = (motor_altura + LINHA_A - 1) / LINHA_A + 1;
    int nos, cmds, imgs;
    motor_numeros(motor, &nos, &cmds, &imgs);
    registrar("cipo: motor %d nos, %d comandos, %d imagens, altura %d, %u ms\n", nos, cmds, imgs, motor_altura, tempo_ms() - t0);
}

static void desenhar_pagina(void)
{
    if (motor) {
        motor_pintar(motor, j->px + TOPO * j->larg, LARG - ROLAGEM, j->larg, rol_px, AREA_A);
        desenhar_barra_rolagem();
        jan_mostrar_area(j, 0, TOPO, LARG, AREA_A);
        return;
    }
    g_retangulo(j, 0, TOPO, LARG - ROLAGEM, AREA_A, fundo_pagina);
    for (int i = 0; i < qtd_rets; i++) {
        const ret_t *q = &rets[i];
        int y0 = TOPO + 4 + (q->l0 - topo) * LINHA_A, y1 = TOPO + 4 + (q->l1 - topo) * LINHA_A;
        int x0 = MARGEM + q->x0, x1 = MARGEM + q->x1;
        if (q->x0 <= 0 && q->x1 >= LARG_TEXTO) { x0 = 0; x1 = LARG - ROLAGEM; }   /* faixa de ponta a ponta */
        if (y0 < TOPO) y0 = TOPO;
        if (y1 > ALT - RODAPE) y1 = ALT - RODAPE;
        if (y1 <= y0 || x1 <= x0) continue;
        if (q->fundo) g_retangulo(j, x0, y0, x1 - x0, y1 - y0, q->fundo - 1);
        if (q->borda) g_moldura(j, x0, y0, x1 - x0, y1 - y0, q->borda - 1);
    }
    for (int i = 0; i < qtd_segs; i++) {
        seg_t *s = &segs[i];
        if (s->linha >= topo + VISIVEIS) continue;
        int x = MARGEM + s->x, y = TOPO + 4 + (s->linha - topo) * LINHA_A;
        if (s->estilo & E_IMAGEM) {
            imagem_t *im = &imagens[s->n];
            for (int k = 0; k < im->a; k++) {
                if (y + k < TOPO || y + k >= ALT - RODAPE) continue;
                memcpy(j->px + (y + k) * j->larg + x, im->px + k * im->l, (size_t)im->l * 4);
            }
            continue;
        }
        if (s->linha < topo) continue;
        if (s->estilo & E_LINHA) { g_retangulo(j, x, y + 8, s->n, 2, 0xC4CBD2); continue; }
        uint32_t cor = s->cor && (!(s->estilo & E_LINK) || s->estilo & E_LINKCOR) ? s->cor - 1 : cor_de(s->estilo);
        const char *t = textos + s->texto;
        if (s->estilo & E_CAMPO) {
            g_retangulo(j, x - 2, y, s->n * LETRA_L + 4, LETRA_A + 1, 0xEEF2F6);
            g_moldura(j, x - 3, y - 1, s->n * LETRA_L + 6, LETRA_A + 3, COR_BORDA);
        }
        if (s->estilo & E_GRANDE) { texto_grande(x, y, t, s->n, cor); continue; }
        g_texto_n(j, x, y, t, s->n, cor);
        if (s->estilo & (E_NEGRITO | E_TITULO)) g_texto_n(j, x + 1, y, t, s->n, cor);   /* negrito: duas vezes */
        if ((s->estilo & E_LINK) && !(s->estilo & E_SEMLINHA)) g_retangulo(j, x, y + LETRA_A, s->n * LETRA_L, 1, cor);
    }
    desenhar_barra_rolagem();
    jan_mostrar_area(j, 0, TOPO, LARG, AREA_A);
}

/* Leva a rolagem ao pixel dado (a página simples arredonda para a linha). */
static void rolar_px(int para)
{
    int max = max_px();
    if (para > max) para = max;
    if (para < 0) para = 0;
    if (motor) {
        if (para == rol_px) return;
        rol_px = para;
    } else {
        int l = (para + LINHA_A / 2) / LINHA_A;
        if (l == topo) return;
        topo = l;
    }
    registrar("cipo: rolagem %d\n", pos_px());
    desenhar_pagina();
}

static void desenhar_barra(void)
{
    g_retangulo(j, 0, 0, LARG, TOPO, 0xE6E9EC);
    g_retangulo(j, 0, TOPO - 1, LARG, 1, COR_BORDA);
    botao_desenhar(j, &bt_voltar);
    botao_desenhar(j, &bt_inicio);
    botao_desenhar(j, &bt_ir);
    caixa_desenhar(j, &endereco);
    jan_mostrar_area(j, 0, 0, LARG, TOPO);
}

/* ------------------------------------------------------------ carregar */

static const char pagina_inicio[] =
    "<title>Início</title><h1>Cipó, o navegador do SaguiOS</h1>"
    "<p>Digite um endereço lá em cima e aperte Enter, ou escolha um site abaixo. O Cipó mostra texto e "
    "imagens e CSS, e roda JavaScript sem vídeo. No <b>https://</b> a conexão é cifrada, mas a "
    "identidade do site <b>não</b> é verificada: não digite senhas.</p>"
    "<h3>Para começar</h3><ul>"
    "<li><a href=\"https://pt.wikipedia.org/\">pt.wikipedia.org</a>: a Wikipédia em português</li>"
    "<li><a href=\"https://www.google.com/\">www.google.com</a>: busca</li>"
    "<li><a href=\"http://info.cern.ch/\">info.cern.ch</a>: o primeiro site da história (1991)</li>"
    "<li><a href=\"http://68k.news/\">68k.news</a>: notícias do dia em HTML simples</li>"
    "<li><a href=\"http://neverssl.com/\">neverssl.com</a>: um site feito para nunca usar https</li>"
    "<li><a href=\"http://example.com/\">example.com</a>: a página de exemplo da internet</li></ul>"
    "<h3>Dicas</h3><ul><li>Setas, Page Up e Page Down rolam a página; Espaço desce uma tela.</li>"
    "<li>Clique na barra da direita para rolar. Passe o mouse num link para ver o endereço.</li>"
    "<li>Para guardar um arquivo, use o comando <b>baixar</b> no Terminal.</li></ul>";

static void pagina_de_erro(const char *titulo_erro, const char *texto)
{
    char h[900];
    formatar_texto(h, sizeof h, "<title>%s</title><h2>%s</h2><p>%s</p><p><a href=\"sobre:inicio\">Voltar ao início</a></p>",
                   titulo_erro, titulo_erro, texto);
    limpar_pagina();
    ler_html(h, (int)tam_texto(h));
}

/* Escreve o texto de uma busca como parâmetro de endereço. */
static void codificar(const char *t, char *d, int max)
{
    static const char hexa[] = "0123456789ABCDEF";
    int k = 0;
    for (; *t && k < max - 4; t++) {
        uint8_t c = (uint8_t)*t;
        if (c == ' ') d[k++] = '+';
        else if (letra((char)c) || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') d[k++] = (char)c;
        else { d[k++] = '%'; d[k++] = hexa[c >> 4]; d[k++] = hexa[c & 15]; }
    }
    d[k] = 0;
}

/* Texto solto na barra vira busca no Google, como no Chrome. */
static void trocar_busca(char *url, int max)
{
    if (comeca_com(url, "http://") || comeca_com(url, "https://") || comeca_com(url, "sobre:")) return;
    bool ponto = false, espaco = false;
    for (const char *p = url; *p; p++) { ponto |= *p == '.' || *p == ':'; espaco |= *p == ' '; }
    if (ponto && !espaco) return;
    char q[300];
    codificar(url, q, sizeof q);
    formatar_texto(url, (size_t)max, "https://www.google.com/search?q=%s", q);
}

/* Baixa o endereço (seguindo redirecionamentos) e monta a página. */
static void carregar(const char *pedido)
{
    char url[URL_MAX];
    copia_texto(url, pedido, sizeof url);
    inicio_pagina = tempo_ms();
    t_rede = t_js = 0; canvas_vivo = false; refresh_ativo = false;
    if (medidor) { motor_fim(medidor); medidor = NULL; }
    if (motor) { motor_fim(motor); motor = NULL; }   /* o motor da página anterior solta as fontes dele antes */
    motor_fontes_limpar();   /* fontes da web são da página anterior */
    js_rol = 0; rol_pedida = -1;
    js_fim();
    cache_limpar();
    hash_viva = 0; ult_tique = tempo_ms();
    nota_busca[0] = 0;
    trocar_busca(url, sizeof url);
    /* Sem esquema, tenta HTTPS primeiro e cai para HTTP se não der. */
    bool adivinhou = false;
    if (!comeca_com(url, "http://") && !comeca_com(url, "https://") && !comeca_com(url, "sobre:")) {
        char t[URL_MAX];
        formatar_texto(t, sizeof t, "https://%s", url);
        copia_texto(url, t, sizeof url);
        adivinhou = true;
    }
    copia_texto(url_atual, url, sizeof url_atual);
    caixa_definir(&endereco, comeca_com(url, "sobre:") ? "" : url);
    endereco.foco = false;
    desenhar_barra();
    registrar("cipo: abrindo %s\n", url);

    if (comeca_com(url, "sobre:")) {
        pagina_segura = false;
        limpar_pagina();
        ler_html(pagina_inicio, (int)sizeof pagina_inicio - 1);
    } else {
        static char cab[16384];
        char *corpo = NULL;
        int tam = 0, codigo = 0;
        bool html = true;
        const char *erro = NULL;
        char detalhe[200] = "";
        unsigned t_doc = tempo_ms();
        for (int redirecoes = 0; redirecoes < 5; redirecoes++) {
            char host[128], caminho[512];
            int porta;
            if (!url_separar(url, host, sizeof host, &porta, caminho, sizeof caminho)) { erro = "Endereço inválido"; break; }
            char st[160];
            formatar_texto(st, sizeof st, "Conectando a %s...", host);
            rodape(st);
            pagina_segura = url_https(url);
            int fd = http_pedir_seguro(host, porta, caminho, pagina_segura);
            if (fd < 0 && !tls_certificado_recusado() && adivinhou) {   /* certificado recusado não cai para HTTP */
                adivinhou = false;
                char t[URL_MAX];
                formatar_texto(t, sizeof t, "http://%s", url + 8);
                copia_texto(url, t, sizeof url);
                copia_texto(url_atual, url, sizeof url_atual);
                caixa_definir(&endereco, url);
                desenhar_barra();
                registrar("cipo: sem HTTPS, tentando %s\n", url);
                continue;
            }
            if (fd < 0) {
                erro = "Não consegui abrir a página";
                formatar_texto(detalhe, sizeof detalhe, "%s: %s.", host, pagina_segura && fd == ERRO_FORMATO ? tls_erro() : nome_erro(fd));
                break;
            }
            int extra;
            int c = http_cabecalho(fd, cab, sizeof cab, &extra);
            if (c < 0) { http_fechar(fd); erro = "Resposta inválida do servidor"; break; }
            codigo = http_codigo(cab);
            char destino[URL_MAX];
            if (codigo >= 300 && codigo < 400 && http_campo(cab, "Location", destino, sizeof destino)) {
                http_fechar(fd);
                char nova[URL_MAX];
                if (!juntar_url(url, destino, nova, sizeof nova)) { erro = "Redirecionamento inválido"; break; }
                copia_texto(url, nova, sizeof url);
                copia_texto(url_atual, url, sizeof url_atual);
                caixa_definir(&endereco, url);
                desenhar_barra();
                continue;
            }
            char tipo[80] = "text/html";
            http_campo(cab, "Content-Type", tipo, sizeof tipo);
            for (char *p = tipo; *p; p++) *p = minuscula(*p);
            html = comeca_com(tipo, "text/html") || comeca_com(tipo, "application/xhtml");
            if (comeca_com(tipo, "video/mp4") || comeca_com(tipo, "audio/mp4")) {
                http_fechar(fd);
                char *args[] = { "video", url, NULL };
                int pid = executar("/bin/video", args);
                registrar("cipo: video %s pid %d\n", url, pid);
                erro = pid > 0 ? "Vídeo aberto no tocador" : "Não consegui abrir o tocador de vídeo";
                formatar_texto(detalhe, sizeof detalhe, "%s", pid > 0 ? "O vídeo toca numa janela própria, enquanto baixa." : nome_erro(pid));
                break;
            }
            if (!html && !comeca_com(tipo, "text/")) {
                http_fechar(fd);
                erro = "Tipo de arquivo que o Cipó não mostra";
                formatar_texto(detalhe, sizeof detalhe, "O servidor mandou \"%s\". Para guardar o arquivo, use o comando baixar no Terminal.", tipo);
                break;
            }
            int cap = 65536;
            corpo = aloca((size_t)cap);
            if (!corpo) { http_fechar(fd); erro = "Sem memória"; break; }
            memcpy(corpo, cab + c, (size_t)extra);
            tam = extra;
            int r, mostrado = 0;
            while ((r = http_ler(fd, corpo + tam, (unsigned)(cap - tam))) > 0) {
                tam += r;
                if (tam == cap) {
                    if (cap >= PAGINA_MAX) break;
                    char *q = realoca(corpo, (size_t)cap * 2);
                    if (!q) break;
                    corpo = q;
                    cap *= 2;
                }
                if (tam / 16384 != mostrado) {
                    mostrado = tam / 16384;
                    formatar_texto(st, sizeof st, "Carregando %s... %d KB", host, tam / 1024);
                    rodape(st);
                }
            }
            http_fechar(fd);
            char te[32];
            if (http_campo(cab, "Transfer-Encoding", te, sizeof te)) tam = http_juntar_pedacos(corpo, tam);
            break;
        }
        t_rede += tempo_ms() - t_doc;
        limpar_pagina();
        if (erro) {
            registrar("cipo: erro: %s %s\n", erro, detalhe);
            pagina_de_erro(erro, detalhe[0] ? detalhe : url);
        } else if (corpo) {
            corpo = converter_corpo(corpo, &tam);
            if (html) {
                titulo_js[0] = alerta_js[0] = 0;
                int tam_doc = tam;   /* o documento como veio (a serialização tira os scripts) */
                char *novo = rodar_scripts(corpo, &tam);
                ler_refresh(novo, tam);
                ler_pagina(novo, tam);
                if (motor) {
                    motor_diagramar();
                    if (motor_altura < 60 && tam > 60) {   /* o site revela o conteúdo por JavaScript */
                        registrar("cipo: css escondeu a pagina, lendo sem display:none\n");
                        motor_sem_esconder(motor, true);
                        motor_diagramar();
                    }
                    if (motor_altura < 40 && tam_doc > 20000) {
                        char tt[96];
                        copia_texto(tt, titulo, sizeof tt);
                        registrar("cipo: pagina so com JavaScript (%d bytes, altura %d)\n", tam_doc, motor_altura);
                        limpar_pagina();
                        pagina_de_erro("Esta página só existe com JavaScript completo",
                                       "O site monta a página com recursos de JavaScript que o motor do Cipó ainda não tem.");
                        copia_texto(titulo, tt, sizeof titulo);
                    }
                    if (novo != corpo) libera(novo);
                    if (titulo_js[0]) copia_texto(titulo, titulo_js, sizeof titulo);
                } else {
                if (linhas < 3 && css_escondeu) {   /* o site revela isso por JavaScript */
                    registrar("cipo: css escondeu a pagina, lendo sem display:none\n");
                    limpar_pagina();
                    sem_esconder = true;
                    ler_pagina(novo, tam);
                    sem_esconder = false;
                }
                if (novo != corpo) libera(novo);
                if (titulo_js[0]) copia_texto(titulo, titulo_js, sizeof titulo);
                /* Casca de aplicativo: muito HTML e script, quase nenhum texto para ler. */
                int letras = 0;
                for (int i = 0; i < qtd_segs; i++) if (!(segs[i].estilo & (E_IMAGEM | E_LINHA))) letras += segs[i].n;
                if (!linhas || (tam > 50000 && letras < 200 && !qtd_imagens)) {
                    char t[96];
                    copia_texto(t, titulo, sizeof t);
                    registrar("cipo: pagina so com JavaScript (%d bytes, %d letras)\n", tam, letras);
                    limpar_pagina();
                    pagina_de_erro("Esta página só existe com JavaScript completo",
                                   "O site monta a página com recursos de JavaScript que o motor do Cipó ainda não tem. Vídeos também não tocam aqui.");
                    copia_texto(titulo, t, sizeof titulo);
                }
                }
            } else {
                estilo = E_PRE;
                pre_n = 1;
                /* Texto puro: cada linha como veio. */
                for (int i = 0, ini = 0; i <= tam; i++)
                    if (i == tam || corpo[i] == '\n') {
                        int k = i - ini;
                        if (k && corpo[i - 1] == '\r') k--;
                        if (k) palavra(corpo + ini, k);
                        linha_vazia = false;
                        quebrar();
                        ini = i + 1;
                    }
            }
            if (codigo >= 400 && !titulo[0]) formatar_texto(titulo, sizeof titulo, "Erro %d", codigo);
        }
        libera(corpo);
    }
    if (!titulo[0]) copia_texto(titulo, url_atual, sizeof titulo);
    char t[48];   /* o kernel recusa título maior que a moldura guarda */
    formatar_texto(t, sizeof t, "Cipó - %s", titulo);
    titulo_janela(j->id, t);
    if (motor && !motor_altura) motor_diagramar();
    rodape(texto_pronto());
    desenhar_pagina();
    /* imagens chegam depois do texto, umas poucas por vez */
    while (motor && motor_imagens(motor, 3)) {
        rodape("Carregando imagens...");
        motor_diagramar();
        desenhar_pagina();
    }
    if (motor) rodape(texto_pronto());
    registrar("cipo: pronto \"%s\" (%d linhas, %d links) em %u ms: rede %u ms, scripts %u ms\n", titulo, linhas, motor ? motor_links(motor) : qtd_alvos, tempo_ms() - inicio_pagina, t_rede, t_js);
}

/* Clique num elemento com evento: o motor roda o evento e devolve a página. */
static void clique_js(int id)
{
    int n = 0;
    char *h = js_clicar(id, &n);
    abrir_video_da_pagina();
    if (!h) return;
    inicio_pagina = tempo_ms();
    int tp = rol_px, campo = motor_tem_foco(motor) ? motor_foco_campo(motor) : -1;
    limpar_pagina();
    ler_pagina(h, n);
    libera(h);
    if (motor && campo >= 0) motor_focar_campo(motor, campo);
    motor_diagramar();
    while (motor && motor_imagens(motor, 8)) motor_diagramar();
    rol_px = tp > max_px() ? max_px() : tp;   /* como nos outros navegadores, o clique não volta ao topo */
    registrar("cipo: clique js %d (%d linhas)\n", id, linhas);
    rodape(texto_pronto());
    desenhar_pagina();
}

/* Página viva: timers e animações do JS continuam depois do load; quando o
 * DOM muda, a página é relida e redesenhada no mesmo ponto da rolagem. */
static unsigned hash_texto(const char *h, int n) { unsigned x = 2166136261u; for (int i = 0; i < n; i++) x = (x ^ (uint8_t)h[i]) * 16777619u; return x; }
static void pagina_viva(void)
{
    int n = 0;
    unsigned agora = tempo_ms();
    unsigned v0 = tela2d_versao();
    char *hr = NULL, *h;
    int nr = 0;
    if (pos_px() != js_rol) {   /* a página rolou: scroll e observadores antes dos timers */
        js_rol = pos_px();
        hr = js_rolar(0, js_rol, &nr);
    }
    h = js_tique((int)(agora - ult_tique > 1000 ? 1000 : agora - ult_tique), &n);
    if (h) { if (hr) libera(hr); } else { h = hr; n = nr; }   /* vale o estado mais novo */
    int px_ped, py_ped;
    if (js_rolagem_pedida(&px_ped, &py_ped)) { (void)px_ped; rol_pedida = py_ped; }
    ult_tique = agora;
    abrir_video_da_pagina();
    if (!h) {
        /* só um <canvas> mudou: pinta de novo, sem layout */
        if (tela2d_versao() != v0 && motor) { desenhar_pagina(); ult_redesenho = tempo_ms(); canvas_vivo = true; }
        return;
    }
    unsigned x = hash_texto(h, n);
    if (x == hash_viva) { libera(h); return; }
    hash_viva = x;
    int t = topo, tp = rol_px, campo = motor_tem_foco(motor) ? motor_foco_campo(motor) : -1;
    unsigned t0 = tempo_ms();
    limpar_pagina();
    ler_pagina(h, n);
    libera(h);
    if (motor && campo >= 0) motor_focar_campo(motor, campo);
    motor_diagramar();
    while (motor && motor_imagens(motor, 8)) motor_diagramar();
    if (t > linhas) t = linhas;
    topo = t;
    rol_px = tp > max_px() ? max_px() : tp;
    desenhar_pagina();
    ult_redesenho = tempo_ms();
    registrar("cipo: viva redesenhou em %u ms\n", ult_redesenho - t0);
}

/* scrollTo pedido pelo script */
static void aplicar_rolagem_pedida(void)
{
    if (rol_pedida < 0) return;
    int y = rol_pedida;
    rol_pedida = -1;
    rolar_px(y);
}

static void navegar(const char *url);
/* Layout de novo no mesmo ponto da rolagem (o campo mudou de texto ou de foco). */
static void refazer_pagina(void)
{
    if (!motor) return;
    motor_altura = motor_layout(motor, LARG - ROLAGEM, AREA_A);
    linhas = (motor_altura + LINHA_A - 1) / LINHA_A + 1;
    if (rol_px > max_px()) rol_px = max_px();
    desenhar_pagina();
}

/* O JavaScript mudou o DOM por causa do campo: relê a página e devolve o foco. */
static void reler_com_foco(char *h, int n, int campo)
{
    int tp = rol_px;
    limpar_pagina();
    ler_pagina(h, n);
    libera(h);
    if (motor && campo >= 0) motor_focar_campo(motor, campo);
    motor_diagramar();
    while (motor && motor_imagens(motor, 8)) motor_diagramar();
    rol_px = tp > max_px() ? max_px() : tp;
    desenhar_pagina();
}

/* Envio de formulário (Enter no campo ou botão de envio): evento submit para
 * o JavaScript; se ninguém cancelar, GET para a ação com os campos. Com
 * JavaScript, o elemento é achado pelo data-sagui-campo (a página pode ter
 * sido relida pelo clique); sem, pelo alvo do motor. */
static void enviar(const motor_alvo_t *quem, int campo)
{
    char url[URL_MAX];
    if (campo >= 0) {
        char *h; int n;
        int cancelou = js_enviar(campo, &h, &n);
        abrir_video_da_pagina();
        if (cancelou) { if (h) reler_com_foco(h, n, motor_tem_foco(motor) ? motor_foco_campo(motor) : -1); return; }
        if (h) libera(h);
        if (motor_envio_campo(motor, campo, url, sizeof url)) navegar(url);
        return;
    }
    if (motor_envio(motor, quem, url, sizeof url)) navegar(url);
}

/* Tecla num campo com foco (UTF-8 por dentro; o teclado manda Latin-1). */
static void tecla_no_campo(unsigned t)
{
    if (t == TECLA_ESC) { motor_foco(motor, NULL); refazer_pagina(); return; }
    if (t == '\n' || t == '\r') { enviar(NULL, motor_foco_campo(motor)); return; }
    char v[1024];
    copia_texto(v, motor_foco_valor(motor), sizeof v);
    int k = (int)tam_texto(v);
    if (t == '\b') {
        if (!k) return;
        do k--; while (k > 0 && ((uint8_t)v[k] & 0xC0) == 0x80);
        v[k] = 0;
    } else if (t >= 32 && t < 256) {
        if (k + 4 >= (int)sizeof v) return;
        k += utf8_por(v + k, t);
        v[k] = 0;
    } else return;
    motor_foco_definir(motor, v);
    int campo = motor_foco_campo(motor);
    if (campo >= 0) {
        int n = 0;
        char *h = js_campo(campo, v, t == '\b' ? 8 : (int)t, &n);
        abrir_video_da_pagina();
        if (h) { reler_com_foco(h, n, campo); return; }
    }
    refazer_pagina();
}

static void navegar(const char *url)
{
    if (url_atual[0] && compara_texto(url, url_atual) != 0) {
        if (qtd_historico == 16) { memmove(historico[0], historico[1], sizeof historico[0] * 15); qtd_historico--; }
        copia_texto(historico[qtd_historico++], url_atual, URL_MAX);
    }
    carregar(url);
}

/* Formulário de busca: pede o texto e monta "acao?fixos&campo=texto". */
static void enviar_formulario(alvo_t *a)
{
    form_t *f = &forms[a->form];
    char texto[120] = "";
    if (!pedir_texto("Cipó", "Digite o texto e aperte Enter:", texto, sizeof texto)) return;
    char url[URL_MAX];
    int k = formatar_texto(url, sizeof url, "%s?%s%s%s=", textos + f->acao, f->fixa, f->fixa[0] ? "&" : "", textos + a->nome);
    static const char hexa[] = "0123456789ABCDEF";
    for (const char *p = texto; *p && k < (int)sizeof url - 4; p++) {
        uint8_t c = (uint8_t)*p;
        if (c == ' ') url[k++] = '+';
        else if (letra((char)c) || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') url[k++] = (char)c;
        else if (c >= 0x80) {   /* Latin-1 -> UTF-8, que é o que os sites esperam */
            uint8_t b[2] = { (uint8_t)(0xC0 | (c >> 6)), (uint8_t)(0x80 | (c & 0x3F)) };
            for (int i = 0; i < 2; i++) { url[k++] = '%'; url[k++] = hexa[b[i] >> 4]; url[k++] = hexa[b[i] & 15]; }
        } else { url[k++] = '%'; url[k++] = hexa[c >> 4]; url[k++] = hexa[c & 15]; }
    }
    url[k] = 0;
    navegar(url);
}

static int alvo_em(int x, int y)
{
    if (y < TOPO || y >= ALT - RODAPE || x >= LARG - ROLAGEM) return -1;
    int linha = topo + (y - TOPO - 4) / LINHA_A;
    for (int i = 0; i < qtd_segs; i++) {
        seg_t *s = &segs[i];
        if (s->linha == linha && s->alvo >= 0 && x >= MARGEM + s->x - 2 && x < MARGEM + s->x + s->n * LETRA_L + 6) return s->alvo;
    }
    return -1;
}

int principal(int argc, char **argv)
{
    j = jan_abrir_redimensionavel("Cipó", LARG_INICIO, ALT_INICIO);
    if (!j) return 1;
    endereco = (caixa_t){ .x = 162, .y = 8, .l = LARG - 162 - 58 };
    copia_texto(rodape_texto, "?", sizeof rodape_texto);
    desenhar_barra();
    rodape("Pronto");
    carregar(argc > 1 ? argv[1] : "sobre:inicio");
    if (argc <= 1) {   /* na página inicial, já dá para digitar */
        endereco.foco = true;
        desenhar_barra();
    }
    jan_mostrar(j);

    evento_t e;
    for (;;) {
        {   /* o script pediu outra página (location) ou o meta refresh venceu */
            char u[URL_MAX]; int subst = 0;
            if (js_navegacao(u, sizeof u, &subst)) { registrar("cipo: o script navegou para %s\n", u); if (subst) carregar(u); else navegar(u); continue; }
            if (refresh_ativo && (int)(tempo_ms() - refresh_em) >= 0) { refresh_ativo = false; copia_texto(u, refresh_url, sizeof u); carregar(u); continue; }
        }
        if (js_vivo() || refresh_ativo || (js_ativo() && motor && pos_px() != js_rol)) {
            if (proximo_evento(j->id, &e, false) != 1) {
                /* ritmo: redesenho caro espera mais */
                unsigned agora = tempo_ms(), passo = js_animando() ? 16 : canvas_vivo ? 40 : agora - ult_redesenho < 2000 ? 1000 : 250;
                if (!js_vivo() && !(js_ativo() && motor && pos_px() != js_rol)) { dormir(50); continue; }   /* só esperando o refresh */
                if (agora - ult_tique >= passo || pos_px() != js_rol) { pagina_viva(); aplicar_rolagem_pedida(); } else dormir(20);
                continue;
            }
        } else if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_TAMANHO) {
            /* A diagramação depende da largura: abre a página de novo. */
            jan_tamanho(j, &e);
            bt_ir.x = LARG - 50;
            endereco.l = LARG - 162 - 58;
            desenhar_barra();
            desenhar_rodape();
            if (motor) {
                /* como nos outros navegadores: só o layout muda, nada é baixado de novo;
                 * o script recebe resize (e pode mudar a página) */
                int nn = 0;
                char *hh = js_ativo() ? js_redimensionar(LARG - ROLAGEM, AREA_A, &nn) : NULL;
                if (hh) reler_com_foco(hh, nn, motor_tem_foco(motor) ? motor_foco_campo(motor) : -1);
                else {
                    motor_diagramar();
                    if (rol_px > max_px()) rol_px = max_px();
                    desenhar_pagina();
                }
                registrar("cipo: rediagramou em %dx%d sem baixar de novo\n", LARG - ROLAGEM, AREA_A);
            } else {
                char url[URL_MAX];
                copia_texto(url, url_atual, sizeof url);
                carregar(url);
            }
            jan_mostrar(j);
            continue;
        }
        /* Como nos outros navegadores: clicar no endereço e digitar troca o
         * endereço inteiro, em vez de escrever depois dele. */
        bool tinha_foco = endereco.foco;
        if (e.tipo == EV_CLIQUE && e.y < TOPO && motor_tem_foco(motor)) { motor_foco(motor, NULL); refazer_pagina(); }
        if (e.tipo == EV_TECLA && substituir && e.tecla >= 32 && e.tecla < 256) {
            caixa_definir(&endereco, "");
            endereco.foco = true;
        }
        if (e.tipo == EV_TECLA) substituir = false;
        if (caixa_tratar(j, &endereco, &e) && endereco.texto[0]) { navegar(endereco.texto); continue; }
        if (endereco.foco && !tinha_foco) substituir = true;
        if (botao_tratar(j, &bt_ir, &e) && endereco.texto[0]) { navegar(endereco.texto); continue; }
        if (botao_tratar(j, &bt_inicio, &e)) { navegar("sobre:inicio"); continue; }
        if (botao_tratar(j, &bt_voltar, &e)) {
            if (qtd_historico) carregar(historico[--qtd_historico]);
            continue;
        }
        if (e.tipo == EV_TECLA && !endereco.foco && motor_tem_foco(motor)) { tecla_no_campo(e.tecla); continue; }
        if (e.tipo == EV_TECLA && !endereco.foco) {
            unsigned t = e.tecla;
            /* Passos do Chrome: 40 px na seta, uma tela menos 40 px na página. */
            if (t == TECLA_BAIXO) rolar_px(pos_px() + 40);
            else if (t == TECLA_CIMA) rolar_px(pos_px() - 40);
            else if (t == TECLA_PGDN || t == ' ') rolar_px(pos_px() + AREA_A - 40);
            else if (t == TECLA_PGUP) rolar_px(pos_px() - (AREA_A - 40));
            else if (t == TECLA_HOME) rolar_px(0);
            else if (t == TECLA_END) rolar_px(max_px());
            else if (t == '\b') { if (qtd_historico) carregar(historico[--qtd_historico]); }
            continue;
        }
        if (e.tipo == EV_CLIQUE && (e.botoes & 1)) {
            int py, pa;
            if (e.x >= LARG - ROLAGEM && e.y >= TOPO && e.y < ALT - RODAPE && polegar(&py, &pa)) {
                /* No polegar: começa a arrastar. Fora dele: uma tela para aquele lado. */
                if (e.y >= py && e.y < py + pa) {
                    arrastando_barra = true;
                    barra_pega = e.y - py;
                    desenhar_barra_rolagem();
                    jan_mostrar_area(j, LARG - ROLAGEM, TOPO, ROLAGEM, AREA_A);
                } else rolar_px(pos_px() + (e.y < py ? -(AREA_A - 40) : AREA_A - 40));
                continue;
            }
            if (motor && e.y >= TOPO && e.y < ALT - RODAPE && e.x < LARG - ROLAGEM) {
                const motor_alvo_t *ma = motor_alvo(motor, e.x, e.y - TOPO + rol_px);
                registrar("cipo: clique em %d,%d -> %s\n", e.x, e.y - TOPO + rol_px, ma && ma->href ? ma->href : "nada");
                if (ma && ma->tipo == MA_LINK) {
                    char url[URL_MAX];
                    copia_texto(url, ma->href, sizeof url);
                    if (comeca_com(url, "sagui-js:")) clique_js(texto_para_int(url + 9));
                    else if (!comeca_com(url, "javascript:") && !comeca_com(url, "mailto:")) navegar(url);
                } else if (ma && ma->tipo == MA_QUADRO) {
                    /* dentro de um iframe: o quadro acha o elemento e roda o clique na janela dele */
                    js_clicar_quadro(ma->js, e.x - ma->rx, e.y - TOPO + rol_px - ma->ry);
                    desenhar_pagina();
                } else if (ma && ma->tipo == MA_CAMPO) {
                    /* foco no campo: a digitação vai para dentro da caixa */
                    if (endereco.foco) { endereco.foco = false; desenhar_barra(); }
                    motor_foco(motor, ma);
                    registrar("cipo: foco no campo %d\n", motor_foco_campo(motor));
                    refazer_pagina();
                } else if (ma && ma->tipo == MA_BOTAO) {
                    /* como no navegador: o clique vai ao script; depois, se o botão
                     * é de envio, o formulário vai (a não ser que o script cancele) */
                    /* ma deixa de valer se o clique reler a página: guarda o que precisa */
                    int js = ma->js, campo = ma->campo;
                    bool envia = ma->envia, cancelou = false;
                    if (js >= 0) { clique_js(js); cancelou = js_clique_cancelado(); }
                    if (envia && !cancelou) enviar(js >= 0 ? NULL : ma, campo);
                } else if (motor_tem_foco(motor)) {
                    motor_foco(motor, NULL);
                    refazer_pagina();
                }
                continue;
            }
            int a = alvo_em(e.x, e.y);
            if (a >= 0 && alvos[a].tipo == A_LINK) {
                char url[URL_MAX];
                copia_texto(url, textos + alvos[a].url, sizeof url);
                if (comeca_com(url, "sagui-js:")) clique_js(texto_para_int(url + 9));
                else navegar(url);
            } else if (a >= 0 && alvos[a].tipo == A_CAMPO) {
                enviar_formulario(&alvos[a]);
            }
            continue;
        }
        if (arrastando_barra && (e.tipo == EV_SOLTOU || (e.tipo == EV_MOVER && !(e.botoes & 1)))) {
            arrastando_barra = false;
            desenhar_barra_rolagem();
            jan_mostrar_area(j, LARG - ROLAGEM, TOPO, ROLAGEM, AREA_A);
            continue;
        }
        if (arrastando_barra && e.tipo == EV_MOVER) {
            int py, pa;
            if (polegar(&py, &pa) && AREA_A > pa)
                rolar_px((e.y - barra_pega - TOPO <= 0 ? 0 : (int)((unsigned)(e.y - barra_pega - TOPO) * (unsigned)max_px() / (unsigned)(AREA_A - pa))));
            continue;
        }
        if (e.tipo == EV_ROLAR) {   /* roda: 100 px por degrau, como o Chrome */
            rolar_px(pos_px() + 100 * (int)e.tecla);
            continue;
        }
        if (e.tipo == EV_MOVER && motor) {
            const motor_alvo_t *ma = e.y >= TOPO && e.y < ALT - RODAPE ? motor_alvo(motor, e.x, e.y - TOPO + rol_px) : NULL;
            if (ma && ma->tipo == MA_LINK && !comeca_com(ma->href, "sagui-js:")) rodape(ma->href);   /* clique só de script: sem endereço, como no Chrome */
            else if (ma && ma->tipo == MA_LINK) rodape(texto_pronto());
            else if (ma) rodape(ma->tipo == MA_CAMPO ? "Campo de texto" : "Enviar formulário");
            else rodape(texto_pronto());
            continue;
        }
        if (e.tipo == EV_MOVER) {
            int a = alvo_em(e.x, e.y);
            if (a >= 0 && alvos[a].tipo == A_LINK) rodape(textos + alvos[a].url);
            else if (a >= 0) rodape("Formulário: clique para digitar");
            else rodape(texto_pronto());
        }
    }
    jan_fechar(j);
    return 0;
}
