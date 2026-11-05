/* http.c - o pedaço de HTTP que o baixar e o Navegador usam (D032). */
#include "sagui.h"

bool url_separar(const char *url, char *host, int max_host, int *porta, char *caminho, int max_caminho)
{
    if (comeca_com(url, "http://")) url += 7;
    int i = 0;
    while (*url && *url != '/' && *url != ':' && *url != '?' && *url != '#' && i < max_host - 1) host[i++] = *url++;
    host[i] = 0;
    *porta = 80;
    if (*url == ':') { url++; *porta = texto_para_int(url); while (*url >= '0' && *url <= '9') url++; }
    if (*url == '?') { caminho[0] = '/'; copia_texto(caminho + 1, url, (size_t)max_caminho - 1); }
    else copia_texto(caminho, *url ? url : "/", (size_t)max_caminho);
    char *frag = acha_char(caminho, '#');   /* o "#trecho" não vai para o servidor */
    if (frag) *frag = 0;
    return host[0] && *porta > 0 && *porta <= 65535;
}

int http_pedir(const char *host, int porta, const char *caminho)
{
    uint32_t ip;
    int r;
    /* Logo depois do boot o endereço IP ainda está chegando (DHCP): espera até 10 s. */
    int fd = ERRO_SEM_REDE;
    for (int t = 0; fd == ERRO_SEM_REDE && t < 20; t++) {
        if (t) dormir(500);
        r = resolver(host, &ip);
        fd = r < 0 ? r : conectar(ip, porta);
    }
    if (fd < 0) return fd;
    char pedido[900];
    int n = formatar_texto(pedido, sizeof pedido,
                           "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: SaguiOS\r\nConnection: close\r\n\r\n", caminho, host);
    r = escrever_tudo(fd, pedido, (unsigned)n);
    if (r < 0) { fechar(fd); return r; }
    return fd;
}

int http_cabecalho(int fd, char *buf, int max, int *extra)
{
    int n = 0;
    for (;;) {
        if (n >= max - 1) return ERRO_FORMATO;
        int r = ler(fd, buf + n, (unsigned)(max - 1 - n));
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
