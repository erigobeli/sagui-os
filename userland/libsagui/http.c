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

int http_ler(int fd, void *buf, unsigned n)
{
    return tls_ativo(fd) ? tls_ler(buf, n) : ler(fd, buf, n);
}

void http_fechar(int fd)
{
    if (tls_ativo(fd)) tls_fechar();
    fechar(fd);
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

int http_pedir_seguro(const char *host, int porta, const char *caminho, bool seguro)
{
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
    static char pedido[8192];   /* URLs de vídeo têm 1,3 KB */
    const char *met = px_metodo ? px_metodo : "GET";
    int n = formatar_texto(pedido, sizeof pedido,
                           "%s %s HTTP/%s\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (compatible; SaguiOS)\r\nAccept: */*\r\nConnection: close\r\n%s",
                           met, caminho, versao, host, px_cab ? px_cab : "");
    if (px_metodo && n < (int)sizeof pedido - 64) n += formatar_texto(pedido + n, sizeof pedido - (size_t)n, "Content-Length: %d\r\n", px_n);
    if (n < (int)sizeof pedido - 3) n += formatar_texto(pedido + n, sizeof pedido - (size_t)n, "\r\n");
    const uint8_t *corpo = px_corpo; int nc = px_metodo ? px_n : 0;
    px_metodo = NULL; px_cab = NULL; px_corpo = NULL; px_n = 0;   /* vale só para um pedido */
    if (seguro) {
        unsigned t1 = tempo_ms();
        r = tls_abrir(fd, host);
        registrar("http: %s dns %u ms, tcp %u ms, tls %u ms\n", host, t_dns - t0, t1 - t_dns, tempo_ms() - t1);
        if (r < 0) { fechar(fd); return r; }
        r = tls_escrever(pedido, (unsigned)n);
        if (r >= 0 && nc > 0) r = tls_escrever(corpo, (unsigned)nc);
    } else {
        r = escrever_tudo(fd, pedido, (unsigned)n);
        if (r >= 0 && nc > 0) r = escrever_tudo(fd, corpo, (unsigned)nc);
    }
    if (r < 0) { http_fechar(fd); return r; }
    return fd;
}

int http_cabecalho(int fd, char *buf, int max, int *extra)
{
    int n = 0;
    for (;;) {
        if (n >= max - 1) return ERRO_FORMATO;
        int r = http_ler(fd, buf + n, (unsigned)(max - 1 - n));
        if (r < 0) return r;
        if (r == 0) return ERRO_FORMATO;
        n += r;
        buf[n] = 0;
        for (int i = 0; i + 3 < n; i++)
            if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                *extra = n - (i + 4);
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
