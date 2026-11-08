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
    const uint8_t *corpo; int nc; int n_pedido;
} at = { -1, -1, false, false, M_NADA, 0, 0, 0, 0, 0, "", 0, false, NULL, 0, 0 };
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
    int n = formatar_texto(pedido, sizeof pedido,
                           "%s %s HTTP/%s\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (compatible; SaguiOS)\r\nAccept: */*\r\nConnection: %s\r\n%s",
                           met, caminho, versao, host, persistente ? "keep-alive" : "close", px_cab ? px_cab : "");
    if (px_metodo && n < (int)sizeof pedido - 64) n += formatar_texto(pedido + n, sizeof pedido - (size_t)n, "Content-Length: %d\r\n", px_n);
    if (n < (int)sizeof pedido - 3) n += formatar_texto(pedido + n, sizeof pedido - (size_t)n, "\r\n");
    at.n_pedido = n;
    at.corpo = px_corpo; at.nc = px_metodo ? px_n : 0;
    px_metodo = NULL; px_cab = NULL; px_corpo = NULL; px_n = 0;   /* vale só para um pedido */
    copia_texto(at.host, host, sizeof at.host); at.porta = porta; at.seguro = seguro;
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
