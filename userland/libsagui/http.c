/* http.c - o pedaço de HTTP que o baixar e o Navegador usam (D032). */
#include "sagui.h"

bool url_https(const char *url) { return comeca_com(url, "https://"); }

bool url_separar(const char *url, char *host, int max_host, int *porta, char *caminho, int max_caminho)
{
    bool seguro = url_https(url);
    if (comeca_com(url, "http://")) url += 7;
    else if (seguro) url += 8;
    int i = 0;
    while (*url && *url != '/' && *url != ':' && *url != '?' && *url != '#' && i < max_host - 1) host[i++] = *url++;
    host[i] = 0;
    *porta = seguro ? 443 : 80;
    if (*url == ':') { url++; *porta = texto_para_int(url); while (*url >= '0' && *url <= '9') url++; }
    if (*url == '?') { caminho[0] = '/'; copia_texto(caminho + 1, url, (size_t)max_caminho - 1); }
    else copia_texto(caminho, *url ? url : "/", (size_t)max_caminho);
    char *frag = acha_char(caminho, '#');   /* o "#trecho" não vai para o servidor */
    if (frag) *frag = 0;
    return host[0] && *porta > 0 && *porta <= 65535;
}

static const char *versao = "1.1";
void cookie_guardar(const char *url, const char *sc, bool de_js);
int  cookie_texto(const char *url, char *o, int max, bool de_js);

/* ---- Conexão persistente (HTTP/1.1 keep-alive) ----
 * Um handshake TLS custa centenas de milissegundos; uma página pede dezenas
 * de recursos ao mesmo servidor. Quando a resposta termina onde o servidor
 * disse (Content-Length ou o último pedaço do chunked) e ele não pediu para
 * fechar, a conexão fica guardada e o próximo pedido ao mesmo host:porta
 * vai por ela. O estado do TLS é um só por processo, então só uma conexão
 * fica guardada, e abrir outra a fecha.
 *
 * Quem usa vê sempre o mesmo número (o fd da primeira conexão do pedido):
 * se a conexão guardada tinha sido fechada pelo servidor, o pedido é refeito
 * numa nova por baixo (fd real diferente), sem o chamador saber. */
static struct { int fd; char host[128]; int porta; bool seguro; } guardada = { -1, "", 0, false };

/* corpo da resposta em andamento */
enum { M_NADA, M_FIM_CONEXAO, M_TAMANHO, M_PEDACOS };
enum { P_TAM, P_EXT, P_DADOS, P_FIM_DADOS, P_TRAILER, P_PRONTO };
static struct {
    int virt, real;           /* número que o chamador vê / conexão de verdade */
    bool reusada;             /* veio da conexão guardada */
    bool manter;              /* pode guardar no fim */
    int modo;
    unsigned resta;           /* M_TAMANHO */
    int p_est; unsigned p_tam, p_resto; int p_linha;   /* M_PEDACOS */
    char host[128]; int porta; bool seguro;            /* para refazer o pedido */
    char caminho[512];                                  /* para os cookies da resposta */
    const uint8_t *corpo; int nc; int n_pedido;
} at = { .virt = -1, .real = -1, .modo = M_NADA };
static char pedido[8192];   /* URLs de vídeo têm 1,3 KB */

static int real_de(int fd) { return fd == at.virt ? at.real : fd; }

static int ler_cru(int fd, void *buf, unsigned n)
{
    fd = real_de(fd);
    return tls_ativo(fd) ? tls_ler(buf, n) : ler(fd, buf, n);
}

static void fechar_de_verdade(int real)
{
    if (real < 0) return;
    if (tls_ativo(real)) tls_fechar();
    fechar(real);
    if (guardada.fd == real) guardada.fd = -1;
}

/* Acompanha os bytes crus do chunked até o fim (tamanho 0 e trailers). */
static void pedacos(const uint8_t *p, int n)
{
    for (int i = 0; i < n && at.p_est != P_PRONTO; i++) {
        uint8_t c = p[i];
        switch (at.p_est) {
        case P_TAM: case P_EXT:
            if (c == '\n') {
                if (at.p_tam) { at.p_est = P_DADOS; at.p_resto = at.p_tam; }
                else { at.p_est = P_TRAILER; at.p_linha = 0; }
            } else if (at.p_est == P_TAM) {
                int v = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
                if (v >= 0) at.p_tam = at.p_tam * 16 + (unsigned)v; else at.p_est = P_EXT;
            }
            break;
        case P_DADOS: {
            unsigned k = (unsigned)(n - i);
            if (k > at.p_resto) k = at.p_resto;
            at.p_resto -= k;
            i += (int)k - 1;
            if (!at.p_resto) at.p_est = P_FIM_DADOS;
            break;
        }
        case P_FIM_DADOS:
            if (c == '\n') { at.p_est = P_TAM; at.p_tam = 0; }
            break;
        case P_TRAILER:
            if (c == '\r') break;
            if (c == '\n') { if (!at.p_linha) at.p_est = P_PRONTO; at.p_linha = 0; }
            else at.p_linha++;
            break;
        }
    }
}

static bool corpo_acabou(void)
{
    return (at.modo == M_TAMANHO && !at.resta) || (at.modo == M_PEDACOS && at.p_est == P_PRONTO) || at.modo == M_NADA;
}

int http_ler(int fd, void *buf, unsigned n)
{
    if (fd != at.virt || at.modo == M_FIM_CONEXAO) return ler_cru(fd, buf, n);
    if (corpo_acabou()) return 0;
    if (at.modo == M_TAMANHO && n > at.resta) n = at.resta;
    int r = ler_cru(fd, buf, n);
    if (r <= 0) { at.manter = false; return r; }
    if (at.modo == M_TAMANHO) at.resta -= (unsigned)r;
    else pedacos(buf, r);
    return r;
}

void http_fechar(int fd)
{
    int real = real_de(fd);
    if (fd == at.virt) {
        bool guarda = at.manter && at.modo != M_FIM_CONEXAO && corpo_acabou();
        at.virt = at.real = -1;
        if (guarda) {
            guardada.fd = real;
            copia_texto(guardada.host, at.host, sizeof guardada.host);
            guardada.porta = at.porta; guardada.seguro = at.seguro;
            return;
        }
    }
    fechar_de_verdade(real);
}

int http_pedir(const char *host, int porta, const char *caminho)
{
    versao = "1.0";   /* o baixar grava o corpo como veio, sem pedaços */
    int fd = http_pedir_seguro(host, porta, caminho, false);
    versao = "1.1";
    return fd;
}

/* HTTP/1.1 manda o corpo em pedaços: "tamanho em hexa", dados, e assim por
 * diante. Junta tudo no lugar e devolve o tamanho novo. */
int http_juntar_pedacos(char *d, int n)
{
    int i = 0, o = 0;
    while (i < n) {
        int t = 0, h;
        for (; i < n; i++) {
            char c = d[i];
            h = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
            if (h < 0) break;
            t = t * 16 + h;
        }
        while (i < n && d[i] != '\n') i++;
        i++;
        if (t == 0 || i >= n) break;
        if (t > n - i) t = n - i;      /* cortado pelo limite de tamanho */
        memmove(d + o, d + i, (size_t)t);
        o += t;
        i += t + 2;
    }
    return o;
}

/* Próximo pedido com outro método, cabeçalhos ("Nome: valor\r\n...") e corpo. */
static const char *px_metodo, *px_cab;
static const uint8_t *px_corpo;
static int px_n;
void http_preparar(const char *metodo, const char *cabecalhos, const void *corpo, int n)
{
    px_metodo = metodo; px_cab = cabecalhos; px_corpo = corpo; px_n = n;
}

static int escrever_pedido(int real, bool seguro)
{
    int r;
    if (seguro) {
        r = tls_escrever(pedido, (unsigned)at.n_pedido);
        if (r >= 0 && at.nc > 0) r = tls_escrever(at.corpo, (unsigned)at.nc);
    } else {
        r = escrever_tudo(real, pedido, (unsigned)at.n_pedido);
        if (r >= 0 && at.nc > 0) r = escrever_tudo(real, at.corpo, (unsigned)at.nc);
    }
    return r;
}

/* Conexão nova (DNS, TCP e, se for o caso, TLS). */
static int conectar_novo(const char *host, int porta, bool seguro)
{
    if (guardada.fd >= 0) fechar_de_verdade(guardada.fd);   /* o TLS é um só: a guardada não sobrevive */
    uint32_t ip;
    int r;
    /* Logo depois do boot o endereço IP ainda está chegando (DHCP): espera até 10 s. */
    int fd = ERRO_SEM_REDE;
    unsigned t0 = tempo_ms(), t_dns = 0;
    for (int t = 0; fd == ERRO_SEM_REDE && t < 20; t++) {
        if (t) dormir(500);
        r = resolver(host, &ip);
        t_dns = tempo_ms();
        fd = r < 0 ? r : conectar(ip, porta);
    }
    if (fd < 0) return fd;
    if (seguro) {
        unsigned t1 = tempo_ms();
        r = tls_abrir(fd, host);
        registrar("http: %s dns %u ms, tcp %u ms, tls %u ms\n", host, t_dns - t0, t1 - t_dns, tempo_ms() - t1);
        if (r < 0) { fechar(fd); return r; }
    }
    return fd;
}

int http_pedir_seguro(const char *host, int porta, const char *caminho, bool seguro)
{
    if (at.virt >= 0) { fechar_de_verdade(at.real); at.virt = at.real = -1; }   /* resposta largada no meio */
    bool persistente = versao[2] == '1';
    const char *met = px_metodo ? px_metodo : "GET";
    /* cookies do pote para este endereço */
    static char ck[4096], urlc[700], linha_ck[4200];
    formatar_texto(urlc, sizeof urlc, "%s://%s%s", seguro ? "https" : "http", host, caminho);
    linha_ck[0] = 0;
    if (cookie_texto(urlc, ck, sizeof ck, false) > 0) formatar_texto(linha_ck, sizeof linha_ck, "Cookie: %s\r\n", ck);
    int n = formatar_texto(pedido, sizeof pedido,
                           "%s %s HTTP/%s\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (compatible; SaguiOS)\r\nAccept: */*\r\nConnection: %s\r\n%s%s",
                           met, caminho, versao, host, persistente ? "keep-alive" : "close", linha_ck, px_cab ? px_cab : "");
    if (px_metodo && n < (int)sizeof pedido - 64) n += formatar_texto(pedido + n, sizeof pedido - (size_t)n, "Content-Length: %d\r\n", px_n);
    if (n < (int)sizeof pedido - 3) n += formatar_texto(pedido + n, sizeof pedido - (size_t)n, "\r\n");
    at.n_pedido = n;
    at.corpo = px_corpo; at.nc = px_metodo ? px_n : 0;
    px_metodo = NULL; px_cab = NULL; px_corpo = NULL; px_n = 0;   /* vale só para um pedido */
    copia_texto(at.host, host, sizeof at.host); at.porta = porta; at.seguro = seguro;
    copia_texto(at.caminho, caminho, sizeof at.caminho);
    at.modo = M_FIM_CONEXAO; at.manter = false;

    int fd = -1;
    at.reusada = false;
    if (guardada.fd >= 0 && persistente && guardada.porta == porta && guardada.seguro == seguro && compara_texto(guardada.host, host) == 0) {
        fd = guardada.fd;
        guardada.fd = -1;
        if (escrever_pedido(fd, seguro) >= 0) at.reusada = true;
        else { fechar_de_verdade(fd); fd = -1; }
    }
    if (fd < 0) {
        fd = conectar_novo(host, porta, seguro);
        if (fd < 0) return fd;
        int r = escrever_pedido(fd, seguro);
        if (r < 0) { fechar_de_verdade(fd); return r; }
    }
    at.virt = at.real = fd;
    return fd;
}

/* Lê o cabeçalho e prepara o enquadramento do corpo (RFC 9112, seção 6.3). */
int http_cabecalho(int fd, char *buf, int max, int *extra)
{
    int n = 0;
    for (;;) {
        if (n >= max - 1) return ERRO_FORMATO;
        int r = ler_cru(fd, buf + n, (unsigned)(max - 1 - n));
        if (r <= 0 && n == 0 && fd == at.virt && at.reusada) {
            /* o servidor tinha fechado a conexão guardada: refaz numa nova */
            at.reusada = false;
            fechar_de_verdade(at.real);
            int novo = conectar_novo(at.host, at.porta, at.seguro);
            if (novo < 0) return novo;
            if (escrever_pedido(novo, at.seguro) < 0) { fechar_de_verdade(novo); return ERRO_FORMATO; }
            at.real = novo;
            continue;
        }
        if (r < 0) return r;
        if (r == 0) return ERRO_FORMATO;
        n += r;
        buf[n] = 0;
        for (int i = 0; i + 3 < n; i++)
            if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                *extra = n - (i + 4);
                if (fd == at.virt) {
                    /* Set-Cookie (podem ser vários) */
                    static char urlr[700], sc[4096];
                    formatar_texto(urlr, sizeof urlr, "%s://%s%s", at.seguro ? "https" : "http", at.host, at.caminho);
                    for (const char *l = buf; l && l < buf + i; l = acha_char(l, '\n') ? acha_char(l, '\n') + 1 : NULL) {
                        if (!((l[0] | 32) == 's' && (l[1] | 32) == 'e' && (l[2] | 32) == 't' && l[3] == '-' && (l[4] | 32) == 'c' && (l[5] | 32) == 'o' &&
                              (l[6] | 32) == 'o' && (l[7] | 32) == 'k' && (l[8] | 32) == 'i' && (l[9] | 32) == 'e' && l[10] == ':')) continue;
                        const char *v = l + 11; while (*v == ' ') v++;
                        int k = 0; while (v[k] && v[k] != '\r' && v[k] != '\n' && k < (int)sizeof sc - 1) { sc[k] = v[k]; k++; }
                        sc[k] = 0;
                        cookie_guardar(urlr, sc, false);
                    }
                    char v[64];
                    int codigo = http_codigo(buf);
                    bool v11 = comeca_com(buf, "HTTP/1.1");
                    bool fecha = http_campo(buf, "Connection", v, sizeof v) && (v[0] | 32) == 'c';
                    at.manter = v11 && !fecha && versao[2] == '1';
                    if ((codigo >= 100 && codigo < 200) || codigo == 204 || codigo == 304) at.modo = M_NADA;
                    else if (http_campo(buf, "Transfer-Encoding", v, sizeof v)) {
                        at.modo = M_PEDACOS; at.p_est = P_TAM; at.p_tam = 0;
                        pedacos((const uint8_t *)buf + i + 4, *extra);
                    } else if (http_campo(buf, "Content-Length", v, sizeof v)) {
                        at.modo = M_TAMANHO;
                        unsigned len = (unsigned)texto_para_int(v);
                        at.resta = len > (unsigned)*extra ? len - (unsigned)*extra : 0;
                    } else { at.modo = M_FIM_CONEXAO; at.manter = false; }
                }
                return i + 4;
            }
    }
}

int http_codigo(const char *cab)
{
    const char *esp = acha_char(cab, ' ');
    return esp ? texto_para_int(esp) : 0;
}

bool http_campo(const char *cab, const char *nome, char *valor, int max)
{
    int t = (int)tam_texto(nome);
    for (const char *l = cab; l && *l; l = acha_char(l, '\n') ? acha_char(l, '\n') + 1 : NULL) {
        int i = 0;
        while (i < t && l[i] && (l[i] | 32) == (nome[i] | 32)) i++;
        if (i < t || l[i] != ':') continue;
        l += t + 1;
        while (*l == ' ') l++;
        int k = 0;
        while (*l && *l != '\r' && *l != '\n' && k < max - 1) valor[k++] = *l++;
        valor[k] = 0;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------ cookies (RFC 6265)
 * Um pote por processo: Set-Cookie das respostas e document.cookie gravam,
 * cada pedido leva os que casam com host, caminho e esquema. Vale enquanto o
 * navegador estiver aberto (expiração respeitada pelo relógio do sistema). */
typedef struct { char nome[64]; char *valor; char dominio[128]; char cam[128]; bool so_host, seguro, so_http; uint32_t expira; } cookie_t;
static cookie_t *pote; static int n_pote, cap_pote;

static uint32_t dias_desde_1970(int a, int m, int d)
{
    a -= m <= 2;
    int era = (a >= 0 ? a : a - 399) / 400;
    unsigned yoe = (unsigned)(a - era * 400), doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1), doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + (int)doe - 719468);
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
    int porta;
    if (!url_separar(url, host, mh, &porta, cam, mc)) { host[0] = 0; cam[0] = '/'; cam[1] = 0; }
    char *q = acha_char(cam, '?'); if (q) *q = 0;
    *seguro = url_https(url);
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
