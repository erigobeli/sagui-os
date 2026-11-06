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
#define PAGINA_MAX (1024 * 1024)

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
#define MAX_IMAGENS 12
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
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2022: case 0x2026: case 0x00B7: return (char)0xB7;
    case 0x20AC: return 'E';
    default: return '?';
    }
}

/* Converte o corpo para Latin-1, se for UTF-8 válido; senão já é Latin-1. */
static int converter(char *s, int n)
{
    for (int i = 0; i < n;) {          /* confere primeiro */
        uint8_t c = (uint8_t)s[i];
        int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0 || (extra && i + extra >= n)) return n;   /* não é UTF-8: já é Latin-1 */
        for (int k = 1; k <= extra; k++) if (((uint8_t)s[i + k] & 0xC0) != 0x80) return n;
        i += 1 + extra;
    }
    int k = 0;
    for (int i = 0; i < n;) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp;
        int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
        cp = extra == 0 ? c : extra == 1 ? (c & 0x1Fu) : extra == 2 ? (c & 0x0Fu) : (c & 0x07u);
        for (int e = 1; e <= extra; e++) cp = cp << 6 | ((uint8_t)s[i + e] & 0x3Fu);
        s[k++] = para_latin1(cp);
        i += 1 + extra;
    }
    return k;
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
    if (comeca_com(rel, "http://") || comeca_com(rel, "https://") || comeca_com(rel, "sobre:")) {
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

static void quebrar(void)
{
    if (!linha_vazia && centro) {
        int d = (LARG_TEXTO - x_atual) / 2;
        for (int i = ini_linha; d > 0 && i < qtd_segs; i++) segs[i].x = (int16_t)(segs[i].x + d);
    }
    ini_linha = qtd_segs;
    if (!linha_vazia) { linha_atual += linha_grande ? 2 : 1; brancas = 0; }
    linha_grande = false;
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
        int cabe = (LARG_TEXTO - x_atual - esp) / passo;
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

static int pilha_lista[8], niveis;   /* contador de cada lista aberta (0 = com marcador) */
static int negrito_n, italico_n, pre_n, form_atual = -1;
static bool em_titulo;
static void rodape(const char *t);

/* Baixa uma imagem inteira, seguindo redirecionamentos. */
static uint8_t *baixar_imagem(const char *pedido, unsigned *n)
{
    static char cab[8192];
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
        if (codigo >= 300 && codigo < 400 && http_campo(cab, "Location", destino, sizeof destino)) {
            http_fechar(fd);
            char nova[URL_MAX];
            if (!juntar_url(url, destino, nova, sizeof nova)) return NULL;
            copia_texto(url, nova, sizeof url);
            continue;
        }
        if (codigo != 200) { http_fechar(fd); return NULL; }
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
        *n = tam;
        return d;
    }
    return NULL;
}

/* <img>: baixa, reduz para caber na página e reserva as linhas. */
static bool colocar_imagem(const char *url)
{
    if (qtd_imagens == MAX_IMAGENS) return false;
    char st[100];
    formatar_texto(st, sizeof st, "Baixando imagem %d...", qtd_imagens + 1);
    rodape(st);
    unsigned n = 0;
    uint8_t *d = baixar_imagem(url, &n);
    if (!d) return false;
    uint32_t *px = NULL;
    int l = 0, a = 0;
    bool ok = imagem_decodificar(d, n, &px, &l, &a);
    libera(d);
    if (!ok) return false;
    registrar("cipo: imagem %dx%d\n", l, a);
    if (a <= 24 && l <= 32) { libera(px); return true; }   /* ícones de enfeite ocupam uma linha à toa */
    int cabe = LARG_TEXTO - recuo, dl = l, da = a;
    if (dl > cabe) { da = da * cabe / dl; dl = cabe; }
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
    quebrar();
    if (centro) x_atual = recuo + (LARG_TEXTO - recuo - dl) / 2;
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

/* ---- CSS: só o que muda uma página de texto ----
 * Regras de <style> com seletor simples (tag, .classe, #id, tag.classe) e o
 * atributo style. Entendem display:none, text-align:center, color e
 * font-weight:bold. Folhas externas, seletores compostos e @media ficam de fora. */
#define MAX_REGRAS 192
typedef struct { char sel[36]; uint8_t esconde, centra, negrito; uint32_t cor; } regra_t;
static regra_t regras[MAX_REGRAS];
static int qtd_regras;

static int hex_val(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

/* Claro demais some no fundo branco, porque o fundo do site não é usado. */
static uint32_t ler_cor(const char *v, int n)
{
    uint32_t c = 0;
    if (n < 4 || v[0] != '#') return 0;
    if (n >= 7 && hex_val(v[6]) >= 0) {
        for (int i = 1; i <= 6; i++) { int h = hex_val(v[i]); if (h < 0) return 0; c = c << 4 | (uint32_t)h; }
    } else {
        for (int i = 1; i <= 3; i++) { int h = hex_val(v[i]); if (h < 0) return 0; c = c << 8 | (uint32_t)(h * 17); }
    }
    int luz = (int)((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10;
    return luz > 190 ? 0 : c + 1;
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
        if (np == 7 && igual_sem_caixa(p, "display", 7) && nv >= 4 && igual_sem_caixa(val, "none", 4)) r->esconde = 1;
        else if (np == 10 && igual_sem_caixa(p, "text-align", 10) && nv >= 6 && igual_sem_caixa(val, "center", 6)) r->centra = 1;
        else if (np == 5 && igual_sem_caixa(p, "color", 5)) { uint32_t c = ler_cor(val, nv); if (c) r->cor = c; }
        else if (np == 11 && igual_sem_caixa(p, "font-weight", 11) && nv >= 3 && (val[0] == '7' || val[0] == '8' || val[0] == '9' || igual_sem_caixa(val, "bold", 4))) r->negrito = 1;
    }
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
        if (c[ini] == '@') {                      /* pula o bloco inteiro */
            if (c[i] == ';') { i++; continue; }
            int prof = 0;
            do { if (c[i] == '{') prof++; else if (c[i] == '}') prof--; i++; } while (i < n && prof);
            continue;
        }
        if (c[i] == ';') { i++; continue; }
        int fim_s = i++, corpo = i;
        while (i < n && c[i] != '}') i++;
        regra_t r;
        memset(&r, 0, sizeof r);
        ler_declaracoes(c + corpo, i - corpo, &r);
        if (!r.esconde && !r.centra && !r.negrito && !r.cor) continue;
        for (int s = ini; s < fim_s;) {           /* "a, b, c" vira três regras */
            while (s < fim_s && (eh_espaco(c[s]) || c[s] == ',')) s++;
            int k = s;
            bool simples = true;
            while (k < fim_s && c[k] != ',') {
                if (eh_espaco(c[k]) && k + 1 < fim_s && c[k + 1] != ',' && !eh_espaco(c[k + 1])) simples = false;
                if (c[k] == ':' || c[k] == '[' || c[k] == '>' || c[k] == '+' || c[k] == '~' || c[k] == '*') simples = false;
                k++;
            }
            int t = k - s;
            while (t && eh_espaco(c[s + t - 1])) t--;
            if (simples && t > 0 && t < 36 && qtd_regras < MAX_REGRAS) {
                regra_t *d = &regras[qtd_regras++];
                *d = r;
                memcpy(d->sel, c + s, (size_t)t);
                d->sel[t] = 0;
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

/* Junta o que vale para esta tag: regras de <style> e o atributo style. */
static void css_da_tag(const char *nome, const char *a, int n, regra_t *r)
{
    char classe[200] = "", id[64] = "", st[300];
    memset(r, 0, sizeof *r);
    atributo(a, n, "class", classe, sizeof classe);
    atributo(a, n, "id", id, sizeof id);
    for (int i = 0; i < qtd_regras; i++) {
        const char *s = regras[i].sel, *ponto = acha_char(s, '.');
        bool bate;
        if (s[0] == '#') bate = compara_texto(s + 1, id) == 0;
        else if (ponto) {
            int t = (int)(ponto - s);
            bate = (t == 0 || ((int)tam_texto(nome) == t && memcmp(nome, s, (size_t)t) == 0)) &&
                   tem_palavra(classe, ponto + 1, (int)tam_texto(ponto + 1));
        } else bate = compara_texto(s, nome) == 0;
        if (!bate) continue;
        r->esconde |= regras[i].esconde;
        r->centra |= regras[i].centra;
        r->negrito |= regras[i].negrito;
        if (regras[i].cor) r->cor = regras[i].cor;
    }
    if (atributo(a, n, "style", st, sizeof st)) ler_declaracoes(st, (int)tam_texto(st), r);
    if (atributo(a, n, "align", st, sizeof st) && igual_sem_caixa(st, "center", 6)) r->centra = 1;
    if (compara_texto(nome, "font") == 0 && atributo(a, n, "color", st, sizeof st)) {
        uint32_t c = ler_cor(st, (int)tam_texto(st));
        if (c) r->cor = c;
    }
    if (compara_texto(nome, "body") == 0 || compara_texto(nome, "html") == 0) r->cor = 0;
}

/* Efeitos abertos, para desfazer quando a tag fechar. */
typedef struct { char nome[16]; int prof; bool centra, negrito; uint32_t cor; } efeito_t;
static efeito_t efeitos[24];
static int qtd_efeitos;

static void efeito_tag(const char *nome, bool fecha, const regra_t *r)
{
    efeito_t *e = qtd_efeitos ? &efeitos[qtd_efeitos - 1] : NULL;
    if (e && compara_texto(e->nome, nome) == 0) {
        e->prof += fecha ? -1 : 1;
        if (e->prof) return;
        if (e->centra) { quebrar(); centro--; }
        if (e->negrito) estilo &= ~E_NEGRITO;
        cor_css = e->cor;
        qtd_efeitos--;
        return;
    }
    if (fecha || !r || (!r->centra && !r->negrito && !r->cor) || qtd_efeitos == 24) return;
    e = &efeitos[qtd_efeitos++];
    copia_texto(e->nome, nome, sizeof e->nome);
    e->prof = 1;
    e->centra = r->centra;
    e->negrito = r->negrito && !(estilo & E_NEGRITO);
    e->cor = cor_css;
    if (r->centra) { quebrar(); centro++; }
    if (e->negrito) estilo |= E_NEGRITO;
    if (r->cor) cor_css = r->cor;
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

static void ler_pagina(const char *h, int n)
{
    const char *m = achar_tag(h, n, "<main");
    const char *f = m ? achar_tag(m, n - (int)(m - h), "</main") : NULL;
    if (!m) { ler_html(h, n); return; }
    achar_titulo(h, n);
    for (const char *p = achar_tag(h, (int)(m - h), "<style"); p; p = achar_tag(p + 6, (int)(m - p) - 6, "<style")) {
        const char *ini = p;
        while (ini < m && *ini != '>') ini++;
        const char *fim_c = achar_tag(ini, (int)(m - ini), "</style");
        if (fim_c) ler_css(ini + 1, (int)(fim_c - ini) - 1);
    }
    ler_html(m, (int)((f ? f : h + n) - m));
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
            if (!vazia) efeito_tag(nome, fecha, fecha ? NULL : &css);
            /* Uma tag sempre separa palavras (menos as de estilo do texto). */
            bool de_estilo = compara_texto(nome, "b") == 0 || compara_texto(nome, "strong") == 0 ||
                             compara_texto(nome, "i") == 0 || compara_texto(nome, "em") == 0 ||
                             compara_texto(nome, "a") == 0 || compara_texto(nome, "span") == 0 ||
                             compara_texto(nome, "font") == 0 || compara_texto(nome, "small") == 0 ||
                             compara_texto(nome, "code") == 0 || compara_texto(nome, "u") == 0;
            if (!de_estilo) { SOLTAR(); espaco_pendente = true; }
            else SOLTAR();
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
                if (atributo(attrs, na, "src", v, sizeof v) && !comeca_com(v, "data:") &&
                    juntar_url(url_atual, v, endereco_img, sizeof endereco_img) && colocar_imagem(endereco_img)) continue;
                if (atributo(attrs, na, "alt", v, sizeof v) && v[0]) {
                    int e = estilo;
                    estilo |= E_ITALICO;
                    palavra("[", 1);
                    texto_solto(v);
                    palavra("]", 1);
                    estilo = e;
                }
                continue;
            }
            if (compara_texto(nome, "hr") == 0) { quebrar(); segmento("", 0, E_LINHA, -1); quebrar(); continue; }
            if (nome[0] == 'h' && nome[1] >= '1' && nome[1] <= '6' && !nome[2]) {
                paragrafo();
                if (fecha) estilo &= ~(E_TITULO | E_GRANDE);
                else estilo |= E_TITULO | (nome[1] <= '2' ? E_GRANDE : 0);
                continue;
            }
            if (compara_texto(nome, "ul") == 0 || compara_texto(nome, "ol") == 0) {
                if (!fecha && niveis < 8) { pilha_lista[niveis++] = nome[0] == 'o' ? 1 : 0; recuo += 24; }
                else if (fecha && niveis > 0) { niveis--; recuo -= 24; }
                if (niveis == 0 || !fecha) paragrafo(); else quebrar();
                continue;
            }
            if (compara_texto(nome, "li") == 0) {
                quebrar();
                if (!fecha) {
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
                if (compara_texto(nome, "th") == 0) x_atual += fecha ? 0 : 16;
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
                    alvo_atual = novo_alvo(A_LINK, url, -1, NULL);
                    if (alvo_atual >= 0) estilo |= E_LINK;
                }
                continue;
            }
            if (compara_texto(nome, "td") == 0) { if (!fecha && !linha_vazia) x_atual += 16; continue; }
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
    qtd_segs = qtd_alvos = qtd_textos = qtd_forms = 0;
    for (int i = 0; i < qtd_imagens; i++) libera(imagens[i].px);
    qtd_imagens = 0;
    linhas = topo = 0;
    centro = ini_linha = 0;
    qtd_regras = qtd_efeitos = 0;
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

/* O cadeado não promete o que não existe: o certificado não é verificado. */
static const char *texto_pronto(void)
{
    return pagina_segura ? "Cifrado, mas a identidade do site NÃO foi verificada. Não digite senhas." : "Pronto";
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

static void desenhar_pagina(void)
{
    g_retangulo(j, 0, TOPO, LARG - ROLAGEM, AREA_A, COR_BRANCO);
    for (int i = 0; i < qtd_segs; i++) {
        seg_t *s = &segs[i];
        if (s->linha >= topo + VISIVEIS) break;
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
        if (s->estilo & E_LINHA) { g_retangulo(j, MARGEM, y + 8, LARG_TEXTO, 2, 0xC4CBD2); continue; }
        uint32_t cor = s->cor && !(s->estilo & E_LINK) ? s->cor - 1 : cor_de(s->estilo);
        const char *t = textos + s->texto;
        if (s->estilo & E_CAMPO) {
            g_retangulo(j, x - 2, y, s->n * LETRA_L + 4, LETRA_A + 1, 0xEEF2F6);
            g_moldura(j, x - 3, y - 1, s->n * LETRA_L + 6, LETRA_A + 3, COR_BORDA);
        }
        if (s->estilo & E_GRANDE) { texto_grande(x, y, t, s->n, cor); continue; }
        g_texto_n(j, x, y, t, s->n, cor);
        if (s->estilo & (E_NEGRITO | E_TITULO)) g_texto_n(j, x + 1, y, t, s->n, cor);   /* negrito: duas vezes */
        if (s->estilo & E_LINK) g_retangulo(j, x, y + LETRA_A, s->n * LETRA_L, 1, cor);
    }
    /* Barra de rolagem. */
    int bx = LARG - ROLAGEM;
    g_retangulo(j, bx, TOPO, ROLAGEM, AREA_A, 0xE6E9EC);
    g_retangulo(j, bx, TOPO, 1, AREA_A, COR_BORDA);
    if (linhas > VISIVEIS) {
        int a = AREA_A * VISIVEIS / linhas;
        if (a < 20) a = 20;
        int y = TOPO + (AREA_A - a) * topo / (linhas - VISIVEIS);
        g_retangulo(j, bx + 3, y + 2, ROLAGEM - 5, a - 4, 0x8A939C);
    }
    jan_mostrar_area(j, 0, TOPO, LARG, AREA_A);
}

static void rolar(int para)
{
    int max = linhas > VISIVEIS ? linhas - VISIVEIS : 0;
    if (para > max) para = max;
    if (para < 0) para = 0;
    if (para == topo) return;
    topo = para;
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
    "imagens PNG e JPEG; não roda JavaScript nem CSS. No <b>https://</b> a conexão é cifrada, mas a "
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

/* Baixa o endereço (seguindo redirecionamentos) e monta a página. */
static void carregar(const char *pedido)
{
    char url[URL_MAX];
    copia_texto(url, pedido, sizeof url);
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
        static char cab[8192];
        char *corpo = NULL;
        int tam = 0, codigo = 0;
        bool html = true;
        const char *erro = NULL;
        char detalhe[200] = "";
        for (int redirecoes = 0; redirecoes < 5; redirecoes++) {
            char host[128], caminho[512];
            int porta;
            if (!url_separar(url, host, sizeof host, &porta, caminho, sizeof caminho)) { erro = "Endereço inválido"; break; }
            char st[160];
            formatar_texto(st, sizeof st, "Conectando a %s...", host);
            rodape(st);
            pagina_segura = url_https(url);
            int fd = http_pedir_seguro(host, porta, caminho, pagina_segura);
            if (fd < 0 && adivinhou) {
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
            break;
        }
        limpar_pagina();
        if (erro) {
            registrar("cipo: erro: %s %s\n", erro, detalhe);
            pagina_de_erro(erro, detalhe[0] ? detalhe : url);
        } else if (corpo) {
            tam = converter(corpo, tam);
            if (html) {
                ler_pagina(corpo, tam);
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
    registrar("cipo: pronto \"%s\" (%d linhas, %d links)\n", titulo, linhas, qtd_alvos);
    rodape(texto_pronto());
    desenhar_pagina();
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
        if (s->linha > linha) break;
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
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_TAMANHO) {
            /* A diagramação depende da largura: abre a página de novo. */
            jan_tamanho(j, &e);
            bt_ir.x = LARG - 50;
            endereco.l = LARG - 162 - 58;
            char url[URL_MAX];
            copia_texto(url, url_atual, sizeof url);
            carregar(url);
            jan_mostrar(j);
            continue;
        }
        /* Como nos outros navegadores: clicar no endereço e digitar troca o
         * endereço inteiro, em vez de escrever depois dele. */
        bool tinha_foco = endereco.foco;
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
        if (e.tipo == EV_TECLA && !endereco.foco) {
            unsigned t = e.tecla;
            if (t == TECLA_BAIXO) rolar(topo + 1);
            else if (t == TECLA_CIMA) rolar(topo - 1);
            else if (t == TECLA_PGDN || t == ' ') rolar(topo + VISIVEIS - 1);
            else if (t == TECLA_PGUP) rolar(topo - (VISIVEIS - 1));
            else if (t == TECLA_HOME) rolar(0);
            else if (t == TECLA_END) rolar(linhas);
            else if (t == '\b') { if (qtd_historico) carregar(historico[--qtd_historico]); }
            continue;
        }
        if (e.tipo == EV_CLIQUE && (e.botoes & 1)) {
            if (e.x >= LARG - ROLAGEM && e.y >= TOPO && e.y < ALT - RODAPE && linhas > VISIVEIS) {
                /* Clique na barra: leva aquele ponto da página para a tela. */
                rolar((e.y - TOPO) * (linhas - VISIVEIS) / AREA_A);
                continue;
            }
            int a = alvo_em(e.x, e.y);
            if (a >= 0 && alvos[a].tipo == A_LINK) {
                char url[URL_MAX];
                copia_texto(url, textos + alvos[a].url, sizeof url);
                navegar(url);
            } else if (a >= 0 && alvos[a].tipo == A_CAMPO) {
                enviar_formulario(&alvos[a]);
            }
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
