/* rede.c - os pedidos do Cipó: a página, os recursos (CSS, imagens, fontes,
 * scripts) e o fetch/XHR do JavaScript, por cima do HTTP e do TLS da 2.0
 * (libsagui/http.c, que é núcleo e continua escrito aqui).
 *
 * Aqui fica o que o navegador acrescenta a cada pedido:
 *   - cookies (RFC 6265): o pote de cookies veio do pull request #6 (Robson
 *     de Lima Barbosa), onde estava no http.c da libsagui;
 *   - método, cabeçalhos e corpo pedidos pelo script (http_preparar);
 *   - arquivos do próprio SaguiOS (/usb/pagina.html).
 * O HTTP mostra cada linha de cabeçalho de cada resposta (http_pedir_completo):
 * Set-Cookie vai para o pote, e o resto volta para o fetch. */
#include "navegador.h"

/* ------------------------------------------------------------ cookies (RFC 6265)
 * Um pote por processo: Set-Cookie das respostas e document.cookie gravam,
 * cada pedido leva os que casam com host, caminho e esquema. Vale enquanto o
 * navegador estiver aberto (expiração respeitada pelo relógio do sistema). */
typedef struct { char nome[64]; char *valor; char dominio[128]; char cam[128]; bool so_host, seguro, so_http; uint32_t expira; } cookie_t;
static cookie_t *pote; static int n_pote, cap_pote;

/* Dias de 1/1/1970 até a data (calendário gregoriano: ano bissexto se
 * divisível por 4, menos os de 100, mais os de 400). Escrito na 2.1 de
 * propósito simples: um laço pelos anos, que aqui vão de 1601 a ~2100. */
static bool bissexto(int a) { return (a % 4 == 0 && a % 100 != 0) || a % 400 == 0; }
static uint32_t dias_desde_1970(int a, int m, int d)
{
    static const int antes[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    long dias = 0;
    for (int k = 1970; k < a; k++) dias += bissexto(k) ? 366 : 365;
    for (int k = a; k < 1970; k++) dias -= bissexto(k) ? 366 : 365;
    if (m < 1) m = 1;
    if (m > 12) m = 12;
    dias += antes[m - 1] + (m > 2 && bissexto(a)) + d - 1;
    return dias < 0 ? 0 : (uint32_t)dias;
}
static uint32_t agora_s(void)
{
    data_t d; memset(&d, 0, sizeof d);
    ler_data(&d);
    return dias_desde_1970(d.ano, d.mes, d.dia) * 86400u + d.hora * 3600u + d.minuto * 60u + d.segundo;
}
static int minusc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static bool igual_ci(const char *a, const char *b, int n) { for (int i = 0; i < n; i++) if (minusc((uint8_t)a[i]) != minusc((uint8_t)b[i])) return false; return true; }
/* "Wdy, DD Mon YYYY HH:MM:SS GMT" (também com '-' e ano de 2 dígitos) */
static uint32_t ler_data_http(const char *s, int n)
{
    static const char meses[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    int dia = -1, mes = -1, ano = -1, h = -1, mi = 0, se = 0;
    for (int i = 0; i < n;) {
        while (i < n && !((s[i] >= '0' && s[i] <= '9') || (minusc(s[i]) >= 'a' && minusc(s[i]) <= 'z'))) i++;
        int j = i;
        while (j < n && ((s[j] >= '0' && s[j] <= '9') || (minusc(s[j]) >= 'a' && minusc(s[j]) <= 'z') || s[j] == ':')) j++;
        if (j == i) break;
        int t = j - i;
        if (h < 0 && t == 8 && s[i + 2] == ':' && s[i + 5] == ':') { h = (s[i] - '0') * 10 + s[i + 1] - '0'; mi = (s[i + 3] - '0') * 10 + s[i + 4] - '0'; se = (s[i + 6] - '0') * 10 + s[i + 7] - '0'; }
        else if (mes < 0 && t >= 3 && minusc(s[i]) >= 'a') { for (int k = 0; k < 12; k++) if (igual_ci(s + i, meses + k * 3, 3)) mes = k + 1; }
        else if (s[i] >= '0' && s[i] <= '9') {
            int v = 0; for (int k = i; k < j && s[k] >= '0' && s[k] <= '9'; k++) v = v * 10 + s[k] - '0';
            if (dia < 0 && t <= 2) dia = v;
            else if (ano < 0 && (t == 2 || t == 4)) ano = t == 2 ? (v >= 70 ? 1900 + v : 2000 + v) : v;
        }
        i = j;
    }
    if (dia < 1 || mes < 1 || ano < 1601 || h < 0) return 0;
    return dias_desde_1970(ano, mes, dia) * 86400u + (uint32_t)(h * 3600 + mi * 60 + se);
}
static bool dominio_casa(const char *host, const char *dom)
{
    int lh = (int)tam_texto(host), ld = (int)tam_texto(dom);
    if (lh == ld) return igual_ci(host, dom, lh);
    return lh > ld && host[lh - ld - 1] == '.' && igual_ci(host + lh - ld, dom, ld);
}
static bool caminho_casa(const char *cam, const char *ck)
{
    int lc = (int)tam_texto(ck);
    if (compara_texto_n(cam, ck, (size_t)lc)) return false;
    return (int)tam_texto(cam) == lc || ck[lc - 1] == '/' || cam[lc] == '/';
}
static void separar_url(const char *url, char *host, int mh, char *cam, int mc, bool *seguro)
{
    url_t u;
    if (url_separar(url, &u) < 0) { host[0] = 0; copia_texto(cam, "/", (size_t)mc); *seguro = false; return; }
    copia_texto(host, u.servidor, (size_t)mh);
    copia_texto(cam, u.caminho, (size_t)mc);
    char *q = acha_char(cam, '?'); if (q) *q = 0;
    *seguro = u.seguro;
}
void cookie_guardar(const char *url, const char *sc, bool de_js)
{
    char host[128], cam[512]; bool seg;
    separar_url(url, host, sizeof host, cam, sizeof cam, &seg);
    if (!host[0]) return;
    cookie_t c; memset(&c, 0, sizeof c);
    const char *p = sc;
    while (*p == ' ') p++;
    const char *eq = p; while (*eq && *eq != '=' && *eq != ';') eq++;
    if (*eq != '=') return;
    int nn = (int)(eq - p); while (nn && p[nn - 1] == ' ') nn--;
    if (nn <= 0 || nn > 63) return;
    memcpy(c.nome, p, (size_t)nn); c.nome[nn] = 0;
    const char *v = eq + 1; while (*v == ' ') v++;
    const char *vf = v; while (*vf && *vf != ';') vf++;
    int nv = (int)(vf - v); while (nv && v[nv - 1] == ' ') nv--;
    copia_texto(c.dominio, host, sizeof c.dominio); c.so_host = true;
    /* caminho padrão: diretório do pedido */
    int k = (int)tam_texto(cam); while (k > 1 && cam[k - 1] != '/') k--;
    if (k > 1) k--;
    memcpy(c.cam, cam, (size_t)(k < 127 ? k : 127)); c.cam[k < 127 ? k : 127] = 0;
    if (!c.cam[0] || c.cam[0] != '/') copia_texto(c.cam, "/", sizeof c.cam);
    long max_age = -1; uint32_t expira = 0; bool tem_expira = false;
    for (const char *a = vf; *a;) {
        if (*a == ';') a++;
        while (*a == ' ') a++;
        const char *af = a; while (*af && *af != ';') af++;
        const char *ae = a; while (ae < af && *ae != '=') ae++;
        int an = (int)(ae - a); while (an && a[an - 1] == ' ') an--;
        const char *av = ae < af ? ae + 1 : af; while (av < af && *av == ' ') av++;
        int avn = (int)(af - av); while (avn && av[avn - 1] == ' ') avn--;
        if (an == 6 && igual_ci(a, "domain", 6) && avn > 0) {
            if (*av == '.') { av++; avn--; }
            char d[128]; int t = avn < 127 ? avn : 127; memcpy(d, av, (size_t)t); d[t] = 0;
            if (!dominio_casa(host, d) || !acha_char(d, '.')) return;   /* domínio de outro site: recusa */
            copia_texto(c.dominio, d, sizeof c.dominio); c.so_host = false;
        } else if (an == 4 && igual_ci(a, "path", 4) && avn > 0 && *av == '/') {
            int t = avn < 127 ? avn : 127; memcpy(c.cam, av, (size_t)t); c.cam[t] = 0;
        } else if (an == 6 && igual_ci(a, "secure", 6)) c.seguro = true;
        else if (an == 8 && igual_ci(a, "httponly", 8)) c.so_http = true;
        else if (an == 7 && igual_ci(a, "max-age", 7) && avn > 0) { long x = 0; int neg = *av == '-'; for (int i = neg; i < avn && av[i] >= '0' && av[i] <= '9'; i++) x = x * 10 + (av[i] - '0'); max_age = neg ? 0 : x; }
        else if (an == 7 && igual_ci(a, "expires", 7) && avn > 0) { expira = ler_data_http(av, avn); tem_expira = expira != 0; }
        a = af;
    }
    if (de_js && c.so_http) return;
    if (c.seguro && !seg) return;
    uint32_t agora = agora_s();
    if (max_age >= 0) c.expira = max_age == 0 ? 1 : agora + (uint32_t)max_age;
    else if (tem_expira) c.expira = expira;
    /* substitui o de mesmo nome, domínio e caminho; vencido = apagar */
    for (int i = 0; i < n_pote; i++) {
        cookie_t *o = &pote[i];
        if (!compara_texto(o->nome, c.nome) && igual_ci(o->dominio, c.dominio, (int)tam_texto(c.dominio) + 1) && !compara_texto(o->cam, c.cam)) {
            if (de_js && o->so_http) return;   /* script não troca cookie HttpOnly */
            libera(o->valor);
            pote[i] = pote[--n_pote];
            break;
        }
    }
    if (c.expira && c.expira <= agora) return;
    if (n_pote == cap_pote) {
        int nc = cap_pote ? cap_pote * 2 : 64;
        cookie_t *np = aloca((unsigned)nc * sizeof(cookie_t));
        if (!np) return;
        if (n_pote) memcpy(np, pote, (size_t)n_pote * sizeof(cookie_t));
        if (pote) libera(pote);
        pote = np; cap_pote = nc;
    }
    c.valor = aloca((unsigned)nv + 1);
    if (!c.valor) return;
    memcpy(c.valor, v, (size_t)nv); c.valor[nv] = 0;
    pote[n_pote++] = c;
}
/* "a=1; b=2" para o endereço (de_js: sem os HttpOnly) */
int cookie_texto(const char *url, char *o, int max, bool de_js)
{
    char host[128], cam[512]; bool seg;
    separar_url(url, host, sizeof host, cam, sizeof cam, &seg);
    int k = 0; o[0] = 0;
    uint32_t agora = agora_s();
    /* caminhos mais longos primeiro (RFC 6265, 5.4) */
    for (int passo = 127; passo >= 0; passo--)
        for (int i = 0; i < n_pote; i++) {
            cookie_t *c = &pote[i];
            if ((int)tam_texto(c->cam) != passo) continue;
            if (c->expira && c->expira <= agora) continue;
            if (c->so_host ? !igual_ci(host, c->dominio, (int)tam_texto(host) + 1) : !dominio_casa(host, c->dominio)) continue;
            if (!caminho_casa(cam, c->cam) || (c->seguro && !seg) || (de_js && c->so_http)) continue;
            int precisa = (int)(tam_texto(c->nome) + tam_texto(c->valor)) + 3;
            if (k + precisa >= max) return k;
            k += formatar_texto(o + k, (size_t)(max - k), "%s%s=%s", k ? "; " : "", c->nome, c->valor);
        }
    return k;
}

/* ------------------------------------------------------------ pedidos */

/* Próximo pedido com outro método, cabeçalhos ("Nome: valor\r\n...") e corpo. */
static const char *px_metodo, *px_cab;
static const void *px_corpo;
static int px_n;
void http_preparar(const char *metodo, const char *cabecalhos, const void *corpo, int n)
{
    px_metodo = metodo; px_cab = cabecalhos; px_corpo = corpo; px_n = n;
}

typedef struct {
    rede_resposta_t *r;
    char *cab;
    int max_cab, n_cab;
} pedido_t;

static int guardar(rede_resposta_t *r, const char *dados, unsigned n)
{
    if (r->n + n > r->max) return -1;
    if (r->n + n + 1 > r->cap) {
        uint32_t cap = r->cap ? r->cap * 2 : 65536;
        while (cap < r->n + n + 1) cap *= 2;
        if (cap > r->max + 1) cap = r->max + 1;
        uint8_t *novo = realoca(r->d, cap);
        if (!novo) return -1;
        r->d = novo;
        r->cap = cap;
    }
    memcpy(r->d + r->n, dados, n);
    r->n += n;
    r->d[r->n] = 0;
    return 0;
}

static int receber(void *ctx, const char *dados, unsigned n) { return guardar(((pedido_t *)ctx)->r, dados, n); }

static void cabecalho(void *ctx, const char *url, const char *linha)
{
    pedido_t *p = ctx;
    if (comeca_com(linha, "HTTP/")) { p->n_cab = 0; if (p->cab && p->max_cab) p->cab[0] = 0; return; }   /* resposta nova */
    if (tam_texto(linha) > 11 && igual_ci(linha, "set-cookie:", 11)) cookie_guardar(url, linha + 11, false);
    if (p->cab && p->n_cab + (int)tam_texto(linha) + 3 < p->max_cab)
        p->n_cab += formatar_texto(p->cab + p->n_cab, (size_t)(p->max_cab - p->n_cab), "%s\r\n", linha);
}

static const char *tipo_do_arquivo(const char *url)
{
    static const char *const tipos[][2] = {
        { ".html", "text/html" }, { ".htm", "text/html" }, { ".png", "image/png" }, { ".gif", "image/gif" },
        { ".webp", "image/webp" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".css", "text/css" },
        { ".js", "text/javascript" }, { ".svg", "image/svg+xml" }, { ".mp4", "video/mp4" },
    };
    const char *ponto = acha_ultimo_char(url, '.');
    for (unsigned i = 0; ponto && i < sizeof tipos / sizeof tipos[0]; i++)
        if (compara_texto(ponto, tipos[i][0]) == 0) return tipos[i][1];
    return "text/plain";
}

int rede_buscar(const char *url, rede_resposta_t *r, unsigned max, char *cab, int max_cab)
{
    memset(r, 0, sizeof *r);
    r->max = max;
    if (cab && max_cab) cab[0] = 0;
    if (url[0] == '/') {
        /* arquivo do SaguiOS */
        int fd = abrir(url, ABRIR_LER);
        if (fd < 0) { formatar_texto(r->erro, sizeof r->erro, "não consegui abrir %s (%s)", url, nome_erro(fd)); return -1; }
        char bloco[4096];
        int n;
        while ((n = ler(fd, bloco, sizeof bloco)) > 0)
            if (guardar(r, bloco, (unsigned)n) < 0) { fechar(fd); copia_texto(r->erro, "memória insuficiente", sizeof r->erro); return -1; }
        fechar(fd);
        copia_texto(r->tipo, tipo_do_arquivo(url), sizeof r->tipo);
        copia_texto(r->final, url, sizeof r->final);
        r->situacao = 200;
        return 200;
    }
    char rede[MAX_URL_NAV];
    endereco_para_rede(url, rede, sizeof rede);
    /* cabeçalhos a mais: os cookies do endereço e os do script */
    static char extra[4096];
    extra[0] = 0;
    static char ck[3000];
    if (cookie_texto(rede, ck, sizeof ck, false) > 0) formatar_texto(extra, sizeof extra, "Cookie: %s\r\n", ck);
    if (px_cab && px_cab[0]) {
        junta_texto(extra, px_cab, sizeof extra);
        int k = (int)tam_texto(extra);
        if (k >= 2 && !(extra[k - 2] == '\r' && extra[k - 1] == '\n')) junta_texto(extra, "\r\n", sizeof extra);
    }
    pedido_t p = { r, cab, max_cab, 0 };
    http_resposta_t resp;
    const char *metodo = px_metodo && px_metodo[0] ? px_metodo : "GET";
    bool com_corpo = compara_texto(metodo, "GET") != 0 && compara_texto(metodo, "HEAD") != 0;
    int ok = http_pedir_completo(metodo, rede, extra[0] ? extra : NULL, com_corpo ? (px_corpo ? px_corpo : "") : NULL,
                                 com_corpo ? (unsigned)px_n : 0, receber, &p, &resp, cabecalho);
    copia_texto(r->tipo, resp.tipo, sizeof r->tipo);
    copia_texto(r->final, resp.url_final[0] ? resp.url_final : rede, sizeof r->final);
    r->situacao = resp.situacao;
    if (ok < 0) { copia_texto(r->erro, resp.erro[0] ? resp.erro : "a conexão falhou", sizeof r->erro); return -1; }
    if (resp.situacao >= 400) formatar_texto(r->erro, sizeof r->erro, "o servidor respondeu %d", resp.situacao);
    return resp.situacao;
}
