/* navegador.c - a janela do Sagui Navegador (H62).
 *
 *   [<] [>] [R] [ endereço ......................... ] [Ir] [+Fav] [Favoritos]
 *   +------------------------------------------------------------------+-+
 *   |  a página (pagina.c desenha; a rolagem é só um deslocamento)    |#|
 *   +------------------------------------------------------------------+-+
 *   status: o endereço do link sob o mouse, "Carregando...", erros
 *
 * Endereços aceitos: http://, https://, um caminho de arquivo do SaguiOS
 * (/usb/pagina.html) e as páginas internas sagui:inicio e sagui:favoritos.
 * Sem "http" na frente, vai https://. A página chega inteira (http_obter),
 * vira árvore (html.c) e layout (pagina.c); depois as imagens são buscadas uma
 * a uma, e a cada imagem o layout é refeito e a tela atualizada. Se o usuário
 * clicar em outro link no meio, o resto das imagens fica para trás.
 *
 * Teclado: setas, PgUp/PgDn, Home/End rolam; Backspace volta; Ctrl+L vai
 * para o endereço. Favoritos ficam em /usb/.saguios/favoritos.txt (ou
 * /tmp/favoritos.txt sem pendrive), uma linha "endereço<TAB>título" cada. */
#include "navegador.h"

#define LARG       960
#define ALT        660
#define BARRA      34
#define STATUS     20
#define ROLAGEM_L  14
#define VISTA_L    (LARG - ROLAGEM_L)
#define VISTA_A    (ALT - BARRA - STATUS)
#define MAX_URL    1024
#define MAX_PAGINA (6 * 1024 * 1024)
#define MAX_IMAGEM (3 * 1024 * 1024)
#define MAX_HIST   64

static jan_t *j;
static pagina_t *pag;
static char *conteudo;            /* os bytes da página atual (a árvore aponta para cópias, não para eles) */
static char url_atual[MAX_URL];
static char base[MAX_URL];         /* para resolver os links (a url final ou o <base>) */
static int rolagem;
static int campo_foco = -1;
static char status[200];
static int link_sob_mouse = -1;

static struct { char url[MAX_URL]; int rolagem; } hist[MAX_HIST];
static int hist_qtd, hist_pos = -1;

/* Barra de endereço própria (a caixa_t do kit só guarda 128 letras). */
static struct { char t[MAX_URL]; int cursor, inicio; bool foco, tudo; } ende;   /* tudo: texto todo selecionado */

static struct { int x, l; const char *texto; } botoes[] = {
    { 6, 26, "<" }, { 34, 26, ">" }, { 62, 26, "R" },
    { 0, 34, "Ir" }, { 0, 50, "+Fav" }, { 0, 88, "Favoritos" },
};
#define BT_VOLTAR 0
#define BT_AVANCAR 1
#define BT_RECARREGAR 2
#define BT_IR 3
#define BT_FAV 4
#define BT_FAVORITOS 5
#define ENDE_X 94
#define ENDE_L (LARG - ENDE_X - 6 - 34 - 50 - 88 - 18)

/* ------------------------------------------------------------ desenho */

static int altura_total(void) { return pag ? pag->altura : 0; }

static void limitar_rolagem(void)
{
    int max = altura_total() - VISTA_A;
    if (rolagem > max) rolagem = max;
    if (rolagem < 0) rolagem = 0;
}

static void desenhar_botao(int i, bool apertado)
{
    int x = botoes[i].x, l = botoes[i].l;
    g_retangulo(j, x, 5, l, 24, apertado ? COR_BOTAO_APERTADO : COR_BOTAO);
    g_moldura(j, x, 5, l, 24, COR_BORDA);
    bool apagado = (i == BT_VOLTAR && hist_pos <= 0) || (i == BT_AVANCAR && hist_pos >= hist_qtd - 1);
    g_texto(j, x + (l - g_largura_texto(botoes[i].texto)) / 2, 9, botoes[i].texto, apagado ? COR_BORDA : COR_TEXTO);
}

static void desenhar_endereco(void)
{
    g_retangulo(j, ENDE_X, 5, ENDE_L, 24, COR_BRANCO);
    g_moldura(j, ENDE_X, 5, ENDE_L, 24, ende.foco ? COR_SELECAO : COR_BORDA);
    int cabe = (ENDE_L - 10) / LETRA_L, n = (int)tam_texto(ende.t);
    if (ende.cursor < ende.inicio) ende.inicio = ende.cursor;
    if (ende.cursor > ende.inicio + cabe) ende.inicio = ende.cursor - cabe;
    if (!ende.foco) ende.inicio = 0;
    int m = n - ende.inicio < cabe ? n - ende.inicio : cabe;
    if (ende.foco && ende.tudo && m > 0) g_retangulo(j, ENDE_X + 4, 8, m * LETRA_L + 2, 18, COR_SELECAO);
    if (m > 0) g_texto_n(j, ENDE_X + 5, 9, ende.t + ende.inicio, m, ende.foco && ende.tudo ? COR_BRANCO : COR_TEXTO);
    if (ende.foco) g_retangulo(j, ENDE_X + 5 + (ende.cursor - ende.inicio) * LETRA_L, 8, 1, 18, COR_TEXTO);
}

static void desenhar_barra(void)
{
    g_retangulo(j, 0, 0, LARG, BARRA, COR_FUNDO);
    g_retangulo(j, 0, BARRA - 1, LARG, 1, COR_BORDA);
    for (int i = 0; i < 6; i++) desenhar_botao(i, false);
    desenhar_endereco();
}

static void desenhar_status(void)
{
    g_retangulo(j, 0, ALT - STATUS, LARG, STATUS, COR_FUNDO);
    g_retangulo(j, 0, ALT - STATUS, LARG, 1, COR_BORDA);
    const char *t = status;
    if (link_sob_mouse >= 0 && pag && link_sob_mouse < pag->nlinks) t = pag->links[link_sob_mouse];
    int cabe = (LARG - 12) / LETRA_L;
    g_texto_n(j, 6, ALT - STATUS + 2, t, cabe, COR_TEXTO_FRACO);
}

static void desenhar_rolagem(void)
{
    int x = VISTA_L, y = BARRA;
    g_retangulo(j, x, y, ROLAGEM_L, VISTA_A, 0xE6E8EA);
    int total = altura_total();
    if (total <= VISTA_A) return;
    int a = VISTA_A * VISTA_A / total;
    if (a < 20) a = 20;
    int ya = y + (int)((int)rolagem * (VISTA_A - a) / (total - VISTA_A));
    g_retangulo(j, x + 2, ya, ROLAGEM_L - 4, a, 0xA2A9B1);
}

static void desenhar_tudo(void)
{
    if (pag) pagina_desenhar(pag, j, 0, BARRA, VISTA_L, VISTA_A, rolagem, campo_foco);
    else g_retangulo(j, 0, BARRA, VISTA_L, VISTA_A, COR_BRANCO);
    desenhar_rolagem();
    desenhar_barra();
    desenhar_status();
    jan_mostrar(j);
}

static void definir_status(const char *t)
{
    copia_texto(status, t, sizeof status);
    desenhar_status();
    jan_mostrar_area(j, 0, ALT - STATUS, LARG, STATUS);
}

static void definir_endereco(const char *u)
{
    copia_texto(ende.t, u, sizeof ende.t);
    ende.cursor = (int)tam_texto(ende.t);
    ende.inicio = 0;
}

/* ------------------------------------------------------------ busca na rede */

typedef struct { char *d; uint32_t n, cap, max; } recebido_t;

static int receber(void *ctx, const char *dados, unsigned n)
{
    recebido_t *r = ctx;
    if (r->n + n > r->max) return -1;
    if (r->n + n + 1 > r->cap) {
        uint32_t cap = r->cap ? r->cap * 2 : 65536;
        while (cap < r->n + n + 1) cap *= 2;
        if (cap > r->max + 1) cap = r->max + 1;
        char *novo = realoca(r->d, cap);
        if (!novo) return -1;
        r->d = novo;
        r->cap = cap;
    }
    memcpy(r->d + r->n, dados, n);
    r->n += n;
    r->d[r->n] = 0;
    return 0;
}

/* Lê um arquivo do SaguiOS inteiro. */
static int ler_arquivo(const char *caminho, recebido_t *r)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return fd;
    char bloco[4096];
    int n;
    while ((n = ler(fd, bloco, sizeof bloco)) > 0)
        if (receber(r, bloco, (unsigned)n) < 0) { fechar(fd); return ERRO_SEM_MEMORIA; }
    fechar(fd);
    return 0;
}

/* Busca um endereço (rede ou arquivo). tipo recebe o Content-Type. */
static int buscar(const char *url, recebido_t *r, char *tipo, int max_tipo, char *final, int max_final, char *erro, int max_erro)
{
    memset(r, 0, sizeof *r);
    r->max = MAX_PAGINA;
    tipo[0] = 0;
    if (url[0] == '/') {
        int e = ler_arquivo(url, r);
        if (e < 0) { formatar_texto(erro, (size_t)max_erro, "não consegui abrir %s (%s)", url, nome_erro(e)); return -1; }
        const char *ponto = acha_ultimo_char(url, '.');
        copia_texto(tipo, ponto && (compara_texto(ponto, ".html") == 0 || compara_texto(ponto, ".htm") == 0) ? "text/html" :
                          ponto && (compara_texto(ponto, ".png") == 0) ? "image/png" :
                          ponto && (compara_texto(ponto, ".jpg") == 0 || compara_texto(ponto, ".jpeg") == 0) ? "image/jpeg" : "text/plain",
                    (size_t)max_tipo);
        copia_texto(final, url, (size_t)max_final);
        return 0;
    }
    char rede[MAX_URL];
    endereco_para_rede(url, rede, sizeof rede);
    http_resposta_t resp;
    int ok = http_obter(rede, receber, r, &resp);
    copia_texto(tipo, resp.tipo, (size_t)max_tipo);
    copia_texto(final, resp.url_final[0] ? resp.url_final : rede, (size_t)max_final);
    if (ok < 0) { copia_texto(erro, resp.erro[0] ? resp.erro : "a conexão falhou", (size_t)max_erro); return -1; }
    if (resp.situacao >= 400) formatar_texto(erro, (size_t)max_erro, "o servidor respondeu %d", resp.situacao);
    else erro[0] = 0;
    return resp.situacao;
}

/* ------------------------------------------------------------ páginas internas */

static void pasta_config(char *out, int max)
{
    info_arquivo_t i;
    copia_texto(out, info_arquivo("/usb/.saguios", &i) >= 0 ? "/usb/.saguios" : "/tmp", (size_t)max);
}

static void html_escapar(recebido_t *r, const char *s)
{
    for (; *s; s++) {
        if (*s == '<') receber(r, "&lt;", 4);
        else if (*s == '&') receber(r, "&amp;", 5);
        else if (*s == '"') receber(r, "&quot;", 6);
        else receber(r, s, 1);
    }
}

static void pagina_interna(const char *url, recebido_t *r)
{
    memset(r, 0, sizeof *r);
    r->max = 256 * 1024;
    #define T(s) receber(r, s, (unsigned)tam_texto(s))
    T("<html><head><meta charset=\"iso-8859-1\">");
    if (compara_texto(url, "sagui:favoritos") == 0) {
        T("<title>Favoritos</title></head><body><h1>Favoritos</h1>");
        char caminho[64];
        pasta_config(caminho, sizeof caminho);
        junta_texto(caminho, "/favoritos.txt", sizeof caminho);
        recebido_t f;
        memset(&f, 0, sizeof f);
        f.max = 128 * 1024;
        int n = 0;
        if (ler_arquivo(caminho, &f) == 0 && f.d) {
            T("<ul>");
            for (char *p = f.d; *p;) {
                char *fim = acha_char(p, '\n');
                if (fim) *fim = 0;
                char *tab = acha_char(p, '\t');
                if (tab) *tab = 0;
                if (*p) {
                    T("<li><a href=\"");
                    html_escapar(r, p);
                    T("\">");
                    html_escapar(r, tab && tab[1] ? tab + 1 : p);
                    T("</a></li>");
                    n++;
                }
                if (!fim) break;
                p = fim + 1;
            }
            T("</ul>");
        }
        libera(f.d);
        if (!n) T("<p>Nenhum favorito ainda. Abra uma página e clique em <b>+Fav</b>.</p>");
        T("<p><small>Guardados em ");
        html_escapar(r, caminho);
        T("</small></p>");
    } else {
        T("<title>Sagui Navegador</title></head><body><h1>Sagui Navegador</h1>"
          "<p>Um navegador escrito do zero para o SaguiOS: HTML, imagens PNG e JPEG, HTTPS com TLS 1.3 próprio.</p>"
          "<h2>Para começar</h2><ul>"
          "<li><a href=\"https://pt.wikipedia.org/wiki/Sagui\">Wikipédia: Sagui</a></li>"
          "<li><a href=\"https://pt.wikipedia.org/wiki/Wikip%C3%A9dia:P%C3%A1gina_principal\">Wikipédia em português</a></li>"
          "<li><a href=\"https://example.com/\">example.com</a></li>"
          "<li><a href=\"https://raw.githubusercontent.com/erigobeli/sagui-os/main/README.md\">README do SaguiOS (texto)</a></li>"
          "<li><a href=\"sagui:favoritos\">Favoritos</a></li>"
          "</ul><h2>Procurar na Wikipédia</h2>"
          "<form action=\"https://pt.wikipedia.org/w/index.php\"><input type=\"hidden\" name=\"title\" value=\"Especial:Pesquisar\">"
          "<input type=\"search\" name=\"search\"> <input type=\"submit\" value=\"Procurar\"></form>"
          "<h2>Dicas</h2><ul><li>Setas, PgUp, PgDn, Home e End rolam a página.</li>"
          "<li>Backspace volta para a página anterior; Ctrl+L vai para o endereço.</li>"
          "<li>Também abre arquivos do SaguiOS, como <a href=\"/leia-me.txt\">/leia-me.txt</a>.</li></ul>");
    }
    T("</body></html>");
    #undef T
}

/* ------------------------------------------------------------ carregar */

static void guardar_rolagem(void)
{
    if (hist_pos >= 0) hist[hist_pos].rolagem = rolagem;
}

static void pagina_erro(recebido_t *r, const char *url, const char *erro)
{
    memset(r, 0, sizeof *r);
    r->max = 64 * 1024;
    const char *t = "<html><head><meta charset=\"iso-8859-1\"><title>Não deu para abrir</title></head><body><h1>Não deu para abrir a página</h1><p>";
    receber(r, t, (unsigned)tam_texto(t));
    html_escapar(r, url);
    t = "</p><p><b>Motivo:</b> ";
    receber(r, t, (unsigned)tam_texto(t));
    html_escapar(r, erro);
    t = "</p><p><a href=\"sagui:inicio\">Página inicial</a></p></body></html>";
    receber(r, t, (unsigned)tam_texto(t));
}

/* Um evento que chegou enquanto as imagens carregavam (tratado depois). */
static evento_t evento_guardado;
static bool tem_evento_guardado;

/* Busca as imagens da página, uma por vez. Para se chegar um clique. */
static void carregar_imagens(void)
{
    int prontas = 0, tentadas = 0;
    for (int i = 0; pag && i < pag->nimgs && tentadas < 80; i++) {
        img_pagina_t *im = &pag->imgs[i];
        if (im->estado != 0) continue;
        evento_t e;
        if (proximo_evento(j->id, &e, false) == 1) {
            /* Qualquer ação do usuário interrompe; o evento é tratado depois (devolvido na fila do kernel não dá,
             * então guardamos para o laço principal). */
            evento_guardado = e;
            tem_evento_guardado = true;
            registrar("navegador: imagens interrompidas (%d de %d)", prontas, pag->nimgs);
            return;
        }
        char url[MAX_URL], tipo[64], final[MAX_URL], erro[128];
        endereco_resolver(base, im->src, url, sizeof url);
        im->estado = 2;
        if (comeca_com(url, "data:") || comeca_com(url, "javascript:")) continue;
        const char *nome = acha_ultimo_char(url, '.');
        if (nome && (compara_texto_n(nome, ".svg", 4) == 0 || compara_texto_n(nome, ".gif", 4) == 0)) continue;
        tentadas++;
        char msg[200];
        formatar_texto(msg, sizeof msg, "Imagem %d de %d: %s", i + 1, pag->nimgs, url);
        definir_status(msg);
        recebido_t r;
        int s = buscar(url, &r, tipo, sizeof tipo, final, sizeof final, erro, sizeof erro);
        if (s >= 200 && s < 300 && r.d) {
            im->img = imagem_ler((const uint8_t *)r.d, r.n, erro, sizeof erro);
            if (im->img) {
                im->estado = 1;
                prontas++;
                registrar("navegador: imagem %s %ux%u", url, im->img->larg, im->img->alt);
                pagina_layout(pag, VISTA_L);
                limitar_rolagem();
                desenhar_tudo();
            } else registrar("navegador: imagem %s: %s", url, erro);
        } else registrar("navegador: imagem %s: %s", url, erro[0] ? erro : "falhou");
        libera(r.d);
    }
    if (pag) registrar("navegador: imagens prontas (%d de %d)", prontas, pag->nimgs);
}

static void carregar(const char *pedido, bool novo_no_historico, int rolagem_inicial)
{
    char url[MAX_URL];
    if (!endereco_tem_esquema(pedido) && pedido[0] != '/') formatar_texto(url, sizeof url, "https://%s", pedido);
    else copia_texto(url, pedido, sizeof url);
    /* Só o fragmento mudou: rola até a âncora, sem buscar de novo. */
    const char *frag = acha_char(url, '#');
    if (frag && pag && compara_texto_n(url, url_atual, (size_t)(frag - url)) == 0 &&
        (url_atual[frag - url] == 0 || url_atual[frag - url] == '#')) {
        int y = pagina_ancora(pag, frag + 1);
        if (y >= 0) {
            guardar_rolagem();
            rolagem = y - 4;
            limitar_rolagem();
            copia_texto(url_atual, url, sizeof url_atual);
            definir_endereco(url);
            desenhar_tudo();
            registrar("navegador: ancora %s em %d", frag + 1, y);
            return;
        }
    }
    guardar_rolagem();
    ende.foco = false;
    campo_foco = -1;
    link_sob_mouse = -1;
    definir_endereco(url);
    char msg[220];
    formatar_texto(msg, sizeof msg, "Carregando %s ...", url);
    copia_texto(status, msg, sizeof status);
    desenhar_barra();
    desenhar_status();
    jan_mostrar(j);
    unsigned inicio = tempo_ms();
    recebido_t r;
    char tipo[64] = "text/html", final[MAX_URL], erro[160] = "";
    int situacao = 200;
    if (comeca_com(url, "sagui:")) { pagina_interna(url, &r); copia_texto(final, url, sizeof final); }
    else situacao = buscar(url, &r, tipo, sizeof tipo, final, sizeof final, erro, sizeof erro);
    if (situacao < 0 || !r.d) {
        libera(r.d);
        pagina_erro(&r, url, erro[0] ? erro : "a página veio vazia");
        copia_texto(tipo, "text/html", sizeof tipo);
        registrar("navegador: erro em %s: %s", url, erro);
    }
    /* Conjunto de caracteres do Content-Type. */
    int charset = -1;
    for (const char *p = tipo; *p; p++)
        if (compara_texto_n(p, "charset=", 8) == 0) { charset = (p[8] | 32) == 'u' ? 1 : 0; break; }
    documento_t *doc;
    bool imagem = comeca_com(tipo, "image/");
    if (imagem) {
        /* Uma imagem sozinha: página com um <img> que aponta para ela. */
        char h[MAX_URL + 160];
        int n = formatar_texto(h, sizeof h, "<html><head><title>Imagem</title></head><body><img src=\"%s\"></body></html>", final);
        doc = html_ler(h, (uint32_t)n, 0);
    } else if (comeca_com(tipo, "text/html") || comeca_com(tipo, "application/xhtml")) {
        doc = html_ler(r.d, r.n, charset);
    } else {
        doc = html_texto_puro(r.d, r.n, charset);
        if (doc) {
            /* O título é o último pedaço do endereço. */
            const char *ult = acha_ultimo_char(final, '/');
            copia_texto(doc->titulo, ult && ult[1] ? ult + 1 : final, sizeof doc->titulo);
        }
    }
    if (!doc) { libera(r.d); definir_status("memória insuficiente"); return; }
    if (pag) pagina_liberar(pag);
    libera(conteudo);
    conteudo = r.d;
    pag = pagina_criar(doc);
    if (!pag) { html_liberar(doc); definir_status("memória insuficiente"); return; }
    copia_texto(url_atual, url, sizeof url_atual);
    copia_texto(base, final, sizeof base);
    if (doc->base[0]) { char b[MAX_URL]; endereco_resolver(final, doc->base, b, sizeof b); copia_texto(base, b, sizeof base); }
    pagina_layout(pag, VISTA_L);
    rolagem = rolagem_inicial;
    if (frag) { int y = pagina_ancora(pag, frag + 1); if (y >= 0) rolagem = y - 4; }
    limitar_rolagem();
    if (novo_no_historico) {
        if (hist_pos < MAX_HIST - 1) hist_pos++;
        else memmove(&hist[0], &hist[1], sizeof hist[0] * (MAX_HIST - 1));
        copia_texto(hist[hist_pos].url, url, sizeof hist[hist_pos].url);
        hist[hist_pos].rolagem = 0;
        hist_qtd = hist_pos + 1;
    }
    char titulo[160];
    /* O kernel guarda títulos curtos: o da página é cortado com "...". */
    char curto[28];
    copia_texto(curto, doc->titulo[0] ? doc->titulo : url, sizeof curto);
    if (tam_texto(doc->titulo[0] ? doc->titulo : url) >= sizeof curto) copia_texto(curto + sizeof curto - 4, "...", 4);
    formatar_texto(titulo, sizeof titulo, "%s - Sagui Navegador", curto);
    titulo_janela(j->id, titulo);
    formatar_texto(status, sizeof status, "%s%s%u KB em %u ms", erro[0] ? erro : "", erro[0] ? " | " : "", r.n / 1024, tempo_ms() - inicio);
    registrar("navegador: carregada %s (%u bytes, %s, situacao %d) em %u ms: %d nos, %d pecas, %d links, %d imagens, altura %d",
              url, r.n, tipo[0] ? tipo : "?", situacao, tempo_ms() - inicio, doc->qtd_nos, pag->qtd, pag->nlinks, pag->nimgs,
              pag->altura);
    registrar("navegador: titulo %s", doc->titulo);
    desenhar_tudo();
    carregar_imagens();
    formatar_texto(status, sizeof status, "%s%sPronto: %u KB em %u ms", erro[0] ? erro : "", erro[0] ? " | " : "", r.n / 1024, tempo_ms() - inicio);
    desenhar_tudo();
    registrar("navegador: pronto %s", url);
}

/* ------------------------------------------------------------ formulários */

static void enviar_formulario(int campo)
{
    if (!pag || campo < 0 || campo >= pag->ncampos) return;
    int f = pag->campos[campo].form;
    if (f < 0 || f >= pag->nforms) return;
    const no_t *form = pag->forms[f];
    const char *acao = html_atributo(form, "action");
    char url[MAX_URL];
    endereco_resolver(base, acao && *acao ? acao : url_atual, url, sizeof url);
    char *q = acha_char(url, '?');
    if (q) *q = 0;
    q = acha_char(url, '#');
    if (q) *q = 0;
    char consulta[MAX_URL] = "";
    /* Todos os <input> do formulário, na ordem da página. */
    const no_t *x = form->filho;
    while (x && x != form) {
        if (x->tipo == NO_ELEMENTO && x->tag == TAG_INPUT) {
            const char *nome = html_atributo(x, "name"), *tipo = html_atributo(x, "type");
            const char *valor = html_atributo(x, "value");
            bool enviar = nome && *nome;
            if (tipo && (compara_texto(tipo, "submit") == 0 || compara_texto(tipo, "button") == 0 ||
                         compara_texto(tipo, "checkbox") == 0 || compara_texto(tipo, "radio") == 0 ||
                         compara_texto(tipo, "image") == 0 || compara_texto(tipo, "file") == 0)) enviar = false;
            for (int c = 0; c < pag->ncampos; c++) if (pag->campos[c].no == x) valor = pag->campos[c].valor;
            if (enviar) {
                char a[256], b[512];
                endereco_codificar(nome, a, sizeof a);
                endereco_codificar(valor ? valor : "", b, sizeof b);
                if (consulta[0]) junta_texto(consulta, "&", sizeof consulta);
                junta_texto(consulta, a, sizeof consulta);
                junta_texto(consulta, "=", sizeof consulta);
                junta_texto(consulta, b, sizeof consulta);
            }
        }
        /* Próximo nó em profundidade, sem sair do formulário. */
        if (x->filho) x = x->filho;
        else {
            while (x && x != form && !x->prox) x = x->pai;
            if (x && x != form) x = x->prox;
        }
    }
    char final[MAX_URL];
    formatar_texto(final, sizeof final, "%s?%s", url, consulta);
    registrar("navegador: formulario -> %s", final);
    carregar(final, true, 0);
}

/* ------------------------------------------------------------ favoritos */

static void favoritar(void)
{
    if (!pag || comeca_com(url_atual, "sagui:")) return;
    char caminho[64];
    pasta_config(caminho, sizeof caminho);
    junta_texto(caminho, "/favoritos.txt", sizeof caminho);
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR);
    if (fd < 0) { definir_status("não consegui gravar os favoritos"); return; }
    char linha[MAX_URL + 160];
    int n = formatar_texto(linha, sizeof linha, "%s\t%s\n", url_atual, pag->doc->titulo);
    escrever_tudo(fd, linha, (unsigned)n);
    fechar(fd);
    char msg[200];
    formatar_texto(msg, sizeof msg, "Favorito guardado em %s", caminho);
    definir_status(msg);
    registrar("navegador: favorito %s", url_atual);
}

/* ------------------------------------------------------------ eventos */

static int botao_em(int x, int y)
{
    if (y < 5 || y >= 29) return -1;
    for (int i = 0; i < 6; i++) if (x >= botoes[i].x && x < botoes[i].x + botoes[i].l) return i;
    return -1;
}

static void voltar(int passo)
{
    int novo = hist_pos + passo;
    if (novo < 0 || novo >= hist_qtd) return;
    guardar_rolagem();
    hist_pos = novo;
    carregar(hist[hist_pos].url, false, hist[hist_pos].rolagem);
}

static void rolar(int d)
{
    int antes = rolagem;
    rolagem += d;
    limitar_rolagem();
    if (rolagem != antes) desenhar_tudo();
}

static void editar_texto(char *t, int max, int *cursor, unsigned tecla)
{
    int n = (int)tam_texto(t);
    if (tecla == TECLA_ESQUERDA) { if (*cursor > 0) (*cursor)--; }
    else if (tecla == TECLA_DIREITA) { if (*cursor < n) (*cursor)++; }
    else if (tecla == TECLA_HOME) *cursor = 0;
    else if (tecla == TECLA_END) *cursor = n;
    else if (tecla == 8) { if (*cursor > 0) { memmove(t + *cursor - 1, t + *cursor, (size_t)(n - *cursor + 1)); (*cursor)--; } }
    else if (tecla == TECLA_DELETE) { if (*cursor < n) memmove(t + *cursor, t + *cursor + 1, (size_t)(n - *cursor)); }
    else if (tecla >= 32 && tecla < 256 && tecla != 127 && n < max - 1) {
        memmove(t + *cursor + 1, t + *cursor, (size_t)(n - *cursor + 1));
        t[*cursor] = (char)tecla;
        (*cursor)++;
    }
}

static int cursor_campo;

static void tratar_tecla(const evento_t *e)
{
    unsigned t = e->tecla;
    if (t == 12 && (e->modificadores & 2)) { ende.foco = true; ende.tudo = true; campo_foco = -1; ende.cursor = (int)tam_texto(ende.t); desenhar_tudo(); return; }
    if (t == 'l' && (e->modificadores & 2)) { ende.foco = true; ende.tudo = true; campo_foco = -1; ende.cursor = (int)tam_texto(ende.t); desenhar_tudo(); return; }
    if (ende.foco) {
        if (t == '\n' || t == '\r') {
            char u[MAX_URL];
            copia_texto(u, ende.t, sizeof u);
            if (u[0]) carregar(u, true, 0);
            return;
        }
        if (t == TECLA_ESC) { ende.foco = false; definir_endereco(url_atual); desenhar_tudo(); return; }
        if (ende.tudo) {
            ende.tudo = false;
            /* Com tudo selecionado, digitar ou apagar troca o endereço inteiro; as setas só desfazem a seleção. */
            if (t == 8 || t == TECLA_DELETE || (t >= 32 && t < 256)) { ende.t[0] = 0; ende.cursor = 0; ende.inicio = 0; }
            if (t == 8 || t == TECLA_DELETE) { desenhar_barra(); jan_mostrar_area(j, 0, 0, LARG, BARRA); return; }
        }
        editar_texto(ende.t, sizeof ende.t, &ende.cursor, t);
        desenhar_barra();
        jan_mostrar_area(j, 0, 0, LARG, BARRA);
        return;
    }
    if (campo_foco >= 0 && pag && campo_foco < pag->ncampos) {
        if (t == '\n' || t == '\r') { enviar_formulario(campo_foco); return; }
        if (t == TECLA_ESC) { campo_foco = -1; desenhar_tudo(); return; }
        campo_t *c = &pag->campos[campo_foco];
        editar_texto(c->valor, sizeof c->valor, &cursor_campo, t);
        desenhar_tudo();
        return;
    }
    switch (t) {
    case TECLA_CIMA: rolar(-48); break;
    case TECLA_BAIXO: rolar(48); break;
    case TECLA_PGUP: rolar(-(VISTA_A - 40)); break;
    case TECLA_PGDN: case ' ': rolar(VISTA_A - 40); break;
    case TECLA_HOME: rolar(-altura_total()); break;
    case TECLA_END: rolar(altura_total()); break;
    case 8: voltar(-1); break;
    default: break;
    }
}

static bool arrastando_rolagem;
static int arrasto_y0, arrasto_rol0;

static void tratar_clique(const evento_t *e)
{
    if (e->y < BARRA) {
        int b = botao_em(e->x, e->y);
        if (b >= 0) {
            desenhar_botao(b, true);
            jan_mostrar_area(j, 0, 0, LARG, BARRA);
            switch (b) {
            case BT_VOLTAR: voltar(-1); break;
            case BT_AVANCAR: voltar(1); break;
            case BT_RECARREGAR: if (url_atual[0]) carregar(url_atual, false, rolagem); break;
            case BT_IR: if (ende.t[0]) { char u[MAX_URL]; copia_texto(u, ende.t, sizeof u); carregar(u, true, 0); } break;
            case BT_FAV: favoritar(); break;
            case BT_FAVORITOS: carregar("sagui:favoritos", true, 0); break;
            }
            desenhar_tudo();
            return;
        }
        if (e->x >= ENDE_X && e->x < ENDE_X + ENDE_L) {
            ende.tudo = !ende.foco;              /* o primeiro clique seleciona tudo; o segundo põe o cursor */
            ende.foco = true;
            campo_foco = -1;
            int c = ende.inicio + (e->x - ENDE_X - 5 + LETRA_L / 2) / LETRA_L;
            int n = (int)tam_texto(ende.t);
            ende.cursor = c < 0 ? 0 : c > n ? n : c;
            desenhar_tudo();
        }
        return;
    }
    if (e->y >= ALT - STATUS) return;
    if (e->x >= VISTA_L) {                       /* barra de rolagem */
        int total = altura_total();
        if (total <= VISTA_A) return;
        int a = VISTA_A * VISTA_A / total;
        if (a < 20) a = 20;
        int ya = BARRA + (int)((int)rolagem * (VISTA_A - a) / (total - VISTA_A));
        if (e->y >= ya && e->y < ya + a) { arrastando_rolagem = true; arrasto_y0 = e->y; arrasto_rol0 = rolagem; }
        else rolar(e->y < ya ? -(VISTA_A - 40) : VISTA_A - 40);
        return;
    }
    if (!pag) return;
    bool tinha_foco = ende.foco || campo_foco >= 0;
    ende.foco = false;
    int i = pagina_peca_em(pag, e->x, e->y - BARRA + rolagem);
    const peca_t *pc = i >= 0 ? &pag->pecas[i] : NULL;
    if (pc && pc->tipo == PECA_CAMPO) {
        campo_foco = pc->campo;
        cursor_campo = (int)tam_texto(pag->campos[campo_foco].valor);
        desenhar_tudo();
        return;
    }
    if (pc && pc->tipo == PECA_BOTAO) { enviar_formulario(pc->campo); return; }
    campo_foco = -1;
    if (pc && pc->link >= 0 && pc->link < pag->nlinks) {
        char u[MAX_URL];
        endereco_resolver(base, pag->links[pc->link], u, sizeof u);
        registrar("navegador: link %s", u);
        if (comeca_com(u, "javascript:") || comeca_com(u, "mailto:")) { definir_status("Esse tipo de link não funciona aqui."); return; }
        carregar(u, true, 0);
        return;
    }
    if (tinha_foco) desenhar_tudo();
}

static void tratar_movimento(const evento_t *e)
{
    if (arrastando_rolagem) {
        if (!(e->botoes & 1)) { arrastando_rolagem = false; return; }
        int total = altura_total();
        int a = VISTA_A * VISTA_A / (total ? total : 1);
        if (a < 20) a = 20;
        if (VISTA_A - a <= 0) return;
        rolagem = arrasto_rol0 + (int)((int)(e->y - arrasto_y0) * (total - VISTA_A) / (VISTA_A - a));
        limitar_rolagem();
        desenhar_tudo();
        return;
    }
    int novo = -1;
    if (pag && e->y >= BARRA && e->y < ALT - STATUS && e->x < VISTA_L) {
        int i = pagina_peca_em(pag, e->x, e->y - BARRA + rolagem);
        if (i >= 0) novo = pag->pecas[i].link;
    }
    if (novo != link_sob_mouse) {
        link_sob_mouse = novo;
        desenhar_status();
        jan_mostrar_area(j, 0, ALT - STATUS, LARG, STATUS);
    }
}

int principal(int argc, char **argv)
{
    j = jan_abrir("Sagui Navegador", LARG, ALT);
    if (!j) { imprimir_em(ERROS, "navegador: não consegui abrir a janela\n"); return 1; }
    botoes[BT_IR].x = ENDE_X + ENDE_L + 6;
    botoes[BT_FAV].x = botoes[BT_IR].x + botoes[BT_IR].l + 6;
    botoes[BT_FAVORITOS].x = botoes[BT_FAV].x + botoes[BT_FAV].l + 6;
    registrar("navegador: janela %d", j->id);
    carregar(argc > 1 ? argv[1] : "sagui:inicio", true, 0);
    for (;;) {
        evento_t e;
        if (tem_evento_guardado) { e = evento_guardado; tem_evento_guardado = false; }
        else if (proximo_evento(j->id, &e, true) != 1) continue;
        switch (e.tipo) {
        case EV_FECHAR:
            jan_fechar(j);
            return 0;
        case EV_CLIQUE:
            if (e.botoes & 1) tratar_clique(&e);
            break;
        case EV_SOLTOU:
            arrastando_rolagem = false;
            break;
        case EV_MOVER:
            tratar_movimento(&e);
            break;
        case EV_TECLA:
            tratar_tecla(&e);
            break;
        }
    }
}
