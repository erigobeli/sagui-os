/* http.c - cliente HTTP/1.1 para os programas (H49).
 *
 * Uma página na web é pedida com um texto simples por uma conexão TCP:
 *
 *     GET /caminho HTTP/1.1
 *     Host: exemplo.com
 *     Connection: close
 *
 * A resposta começa com a linha de situação ("HTTP/1.1 200 OK"), cabeçalhos
 * ("Nome: valor") e uma linha vazia; depois vem o conteúdo. O tamanho do
 * conteúdo vem em Content-Length, ou o conteúdo vem em pedaços
 * ("Transfer-Encoding: chunked": cada pedaço começa com o tamanho em
 * hexadecimal), ou simplesmente vai até a conexão fechar. Respostas 301, 302,
 * 303, 307 e 308 mandam para outro endereço (Location), que é seguido até 5
 * vezes.
 *
 * O transporte é abstrato (um "canal"): TCP puro para http:// e, a partir da
 * H53, TLS por cima do TCP para https://.
 *
 * Conexões guardadas (H129): um programa que pede muitas coisas ao mesmo
 * servidor (o Cipó, com as imagens e o CSS de uma página) liga
 * http_guardar_conexoes(true). O pedido sai com "Connection: keep-alive" e,
 * se a resposta veio inteira e delimitada (Content-Length ou pedaços), a
 * conexão fica guardada para o próximo pedido ao mesmo servidor, sem outro
 * DNS, TCP e TLS. Se o servidor já fechou a conexão guardada, um GET é
 * repetido uma vez numa conexão nova. */
#include "sagui.h"
#include "http.h"
#include "compactar.h"

/* ------------------------------------------------------------ endereço */

int url_separar(const char *url, url_t *u)
{
    memset(u, 0, sizeof *u);
    const char *p = url;
    if (compara_texto_n(p, "http://", 7) == 0) { p += 7; u->seguro = false; u->porta = 80; }
    else if (compara_texto_n(p, "https://", 8) == 0) { p += 8; u->seguro = true; u->porta = 443; }
    else return ERRO_INVALIDO;
    int k = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && k < (int)sizeof u->servidor - 1) u->servidor[k++] = *p++;
    u->servidor[k] = 0;
    if (!k) return ERRO_INVALIDO;
    if (*p == ':') {
        p++;
        int porta = 0;
        while (*p >= '0' && *p <= '9') porta = porta * 10 + (*p++ - '0');
        if (porta <= 0 || porta > 65535) return ERRO_INVALIDO;
        u->porta = porta;
    }
    if (*p != '/') { u->caminho[0] = '/'; copia_texto(u->caminho + 1, p, sizeof u->caminho - 1); }
    else copia_texto(u->caminho, p, sizeof u->caminho);
    return 0;
}

/* ------------------------------------------------------------ canal */

static int canal_ler(canal_t *c, void *buf, unsigned max)
{
    if (c->tls) return ponte_tls_ler(c->tls, buf, max);
    return rede_receber(c->socket, buf, max, NULL);
}

static int canal_escrever(canal_t *c, const void *dados, unsigned n)
{
    if (c->tls) return ponte_tls_escrever(c->tls, dados, n);
    return tcp_enviar_tudo(c->socket, dados, n);
}

int canal_abrir(canal_t *c, const url_t *u, char *erro, int max_erro)
{
    memset(c, 0, sizeof *c);
    c->socket = -1;
    unsigned ip;
    int r = dns_resolver(u->servidor, &ip);
    if (r < 0) { formatar_texto(erro, (size_t)max_erro, "%s: %s", u->servidor, r == ERRO_NAO_EXISTE ? "nome não encontrado" : nome_erro(r)); return r; }
    c->socket = tcp_conectar(ip, u->porta, 15000);
    if (c->socket < 0) { formatar_texto(erro, (size_t)max_erro, "%s: não conectou", u->servidor); return c->socket; }
    if (u->seguro) {
        c->tls = ponte_tls_conectar(c->socket, u->servidor, erro, max_erro);
        if (!c->tls) { rede_fechar(c->socket); c->socket = -1; return ERRO_REDE; }
    }
    return 0;
}

void canal_fechar(canal_t *c)
{
    if (c->tls) ponte_tls_fechar(c->tls);
    if (c->socket >= 0) rede_fechar(c->socket);
    c->tls = NULL;
    c->socket = -1;
}

/* Lê um byte, esperando o que for preciso (dentro de um limite de tempo).
 * Usa um buffer próprio para não chamar o kernel byte a byte. */
static int ler_byte(canal_t *c)
{
    if (c->pos < c->tam) return (unsigned char)c->buf[c->pos++];
    unsigned inicio = tempo_ms();
    for (;;) {
        int n = canal_ler(c, c->buf, sizeof c->buf);
        if (n > 0) { c->tam = (unsigned)n; c->pos = 1; return (unsigned char)c->buf[0]; }
        if (n < 0) return -1;                           /* fim ou erro */
        if (tempo_ms() - inicio > 30000 || rede_esperar(5)) return -1;
    }
}

static int linha_http(canal_t *c, char *linha, int max)
{
    int k = 0;
    for (;;) {
        int b = ler_byte(c);
        if (b < 0) { linha[k] = 0; return k ? k : -1; }
        if (b == '\n') break;
        if (b != '\r' && k < max - 1) linha[k++] = (char)b;
    }
    linha[k] = 0;
    return k;
}

static bool cabecalho(const char *linha, const char *nome, const char **valor)
{
    size_t n = tam_texto(nome);
    for (size_t i = 0; i < n; i++) {
        char a = linha[i], b = nome[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
    }
    if (linha[n] != ':') return false;
    const char *v = linha + n + 1;
    while (*v == ' ') v++;
    *valor = v;
    return true;
}

/* ------------------------------------------------------------ conexões guardadas (H129) */

#define GUARDADAS 4
#define GUARDADA_MAX_MS 20000   /* parada há mais tempo: o servidor já deve ter fechado */

typedef struct {
    bool     usada;
    bool     seguro;
    int      porta;
    char     servidor[128];
    unsigned desde;
    canal_t  c;
} guardada_t;

static bool guardar_ligado;
static guardada_t guardadas[GUARDADAS];

void http_guardar_conexoes(bool sim)
{
    guardar_ligado = sim;
    if (!sim)
        for (int i = 0; i < GUARDADAS; i++)
            if (guardadas[i].usada) { canal_fechar(&guardadas[i].c); guardadas[i].usada = false; }
}

/* Tira da guarda uma conexão para o servidor (true) ou não acha (false). */
static bool pegar_guardada(const url_t *u, canal_t *c)
{
    for (int i = 0; i < GUARDADAS; i++) {
        guardada_t *g = &guardadas[i];
        if (!g->usada || g->seguro != u->seguro || g->porta != u->porta || compara_texto(g->servidor, u->servidor) != 0) continue;
        g->usada = false;
        if (tempo_ms() - g->desde > GUARDADA_MAX_MS) { canal_fechar(&g->c); continue; }
        *c = g->c;
        return true;
    }
    return false;
}

/* A ponte do TLS tem só 2 sessões (tls_ponte.c): antes de abrir outra
 * conexão segura, fica guardada no máximo uma, a mais nova. */
static void abrir_vaga_tls(void)
{
    for (;;) {
        int n = 0, velha = -1;
        for (int i = 0; i < GUARDADAS; i++)
            if (guardadas[i].usada && guardadas[i].seguro) {
                n++;
                if (velha < 0 || guardadas[i].desde < guardadas[velha].desde) velha = i;
            }
        if (n <= 1) return;
        canal_fechar(&guardadas[velha].c);
        guardadas[velha].usada = false;
    }
}

/* Guarda a conexão, no lugar vago ou no da mais antiga. */
static void guardar_conexao(const url_t *u, canal_t *c)
{
    int k = 0;
    for (int i = 0; i < GUARDADAS; i++) {
        if (!guardadas[i].usada) { k = i; break; }
        if (guardadas[i].desde < guardadas[k].desde) k = i;
    }
    guardada_t *g = &guardadas[k];
    if (g->usada) canal_fechar(&g->c);
    g->usada = true;
    g->seguro = u->seguro;
    g->porta = u->porta;
    copia_texto(g->servidor, u->servidor, sizeof g->servidor);
    g->desde = tempo_ms();
    g->c = *c;
    g->c.pos = g->c.tam = 0;
}

/* O corpo da resposta: em pedaços, com Content-Length ou até fechar. */
static int ler_corpo(canal_t *c, long tamanho, bool pedacos, http_receptor_t receptor, void *ctx, http_resposta_t *resp)
{
    char bloco[4096];
    char linha[512];
    int ok = 0;
    if (pedacos) {
        for (;;) {
            if (linha_http(c, linha, sizeof linha) < 0) { ok = ERRO_REDE; break; }
            unsigned t = 0;
            for (const char *p = linha; *p; p++) {
                char h = *p;
                if (h >= '0' && h <= '9') t = t * 16 + (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') t = t * 16 + (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') t = t * 16 + (unsigned)(h - 'A' + 10);
                else break;
            }
            if (t == 0) {
                /* os cabeçalhos finais, se houver, até a linha vazia */
                while (linha_http(c, linha, sizeof linha) > 0) {}
                break;
            }
            while (t > 0) {
                unsigned k = 0;
                while (k < t && k < sizeof bloco) {
                    int b = ler_byte(c);
                    if (b < 0) { ok = ERRO_REDE; break; }
                    bloco[k++] = (char)b;
                }
                if (ok < 0) break;
                if (receptor(ctx, bloco, k) < 0) { ok = ERRO_INVALIDO; break; }
                resp->recebidos += k;
                t -= k;
            }
            if (ok < 0) break;
            linha_http(c, linha, sizeof linha);   /* o \r\n depois do pedaço */
        }
    } else {
        for (;;) {
            if (tamanho >= 0 && (long)resp->recebidos >= tamanho) break;
            unsigned k = 0;
            /* O que já está no buffer da leitura de linhas vem primeiro. */
            while (c->pos < c->tam && k < sizeof bloco && (tamanho < 0 || (long)(resp->recebidos + k) < tamanho)) bloco[k++] = c->buf[c->pos++];
            if (k == 0) {
                int b = ler_byte(c);
                if (b < 0) { if (tamanho >= 0) ok = ERRO_REDE; break; }
                bloco[k++] = (char)b;
            }
            if (receptor(ctx, bloco, k) < 0) { ok = ERRO_INVALIDO; break; }
            resp->recebidos += k;
        }
    }
    return ok;
}

/* ------------------------------------------------------------ pedido */

int http_obter(const char *url_inicial, http_receptor_t receptor, void *ctx, http_resposta_t *resp)
{
    return http_pedir("GET", url_inicial, NULL, NULL, 0, receptor, ctx, resp);
}

/* Um pedido qualquer (GET ou POST, H69). "extra" são cabeçalhos a mais, já
 * no formato "Nome: valor\r\n" (pode ser NULL); "corpo" vai depois deles,
 * com Content-Length. Pedidos com corpo não seguem redirecionamentos. */
int http_pedir(const char *metodo, const char *url_inicial, const char *extra, const void *corpo, unsigned n_corpo,
               http_receptor_t receptor, void *ctx, http_resposta_t *resp)
{
    return http_pedir_completo(metodo, url_inicial, extra, corpo, n_corpo, receptor, ctx, resp, NULL);
}

/* Conteúdo compactado (Content-Encoding: gzip): o texto de uma página chega
 * com um quinto do tamanho. O corpo compactado é juntado aqui e, inteiro,
 * descompactado e passado ao receptor de quem pediu, em blocos. */
typedef struct { uint8_t *d; uint32_t n, cap; } juntado_t;
#define MAX_COMPACTADO (32u * 1024 * 1024)
static int juntar(void *p, const char *dados, unsigned n)
{
    juntado_t *j = p;
    if (j->n + n > MAX_COMPACTADO) return -1;
    if (j->n + n > j->cap) {
        uint32_t cap = j->cap ? j->cap * 2 : 64 * 1024;
        while (cap < j->n + n) cap *= 2;
        uint8_t *novo = aloca(cap);
        if (!novo) return -1;
        if (j->d) { memcpy(novo, j->d, j->n); libera(j->d); }
        j->d = novo;
        j->cap = cap;
    }
    memcpy(j->d + j->n, dados, n);
    j->n += n;
    return 0;
}

static int entregar_descompactado(juntado_t *j, http_receptor_t receptor, void *ctx, http_resposta_t *resp)
{
    uint8_t *saida = NULL;
    int n = gzip_descompactar(j->d, j->n, &saida, NULL, 0);
    if (n < 0) { copia_texto(resp->erro, "conteúdo compactado inválido", sizeof resp->erro); return ERRO_REDE; }
    int ok = 0;
    for (int i = 0; i < n && ok == 0; i += 16384)
        if (receptor(ctx, (const char *)saida + i, (unsigned)(n - i < 16384 ? n - i : 16384)) < 0) ok = ERRO_INVALIDO;
    libera(saida);
    return ok;
}

/* O mesmo, mostrando os cabeçalhos de cada resposta (H82: os cookies e o
 * fetch do Cipó precisam deles). "cabecalhos" recebe primeiro a linha de
 * situação ("HTTP/1.1 200 OK") e depois cada linha "Nome: valor", com o
 * endereço que respondeu; também nas respostas de redirecionamento. */
int http_pedir_completo(const char *metodo, const char *url_inicial, const char *extra, const void *corpo, unsigned n_corpo,
                        http_receptor_t receptor, void *ctx, http_resposta_t *resp, http_cabecalhos_t cabecalhos)
{
    char url[MAX_URL_HTTP];
    copia_texto(url, url_inicial, sizeof url);
    memset(resp, 0, sizeof *resp);
    for (int redirecionamentos = 0; redirecionamentos < 6; redirecionamentos++) {
        url_t u;
        if (url_separar(url, &u) < 0) { copia_texto(resp->erro, "endereço inválido (use http:// ou https://)", sizeof resp->erro); return ERRO_INVALIDO; }
        canal_t c;
        bool reusada = guardar_ligado && pegar_guardada(&u, &c);
        if (!reusada && guardar_ligado && u.seguro) abrir_vaga_tls();
        if (!reusada && canal_abrir(&c, &u, resp->erro, sizeof resp->erro) < 0) return ERRO_REDE;
        bool repetir = reusada && !corpo;   /* o servidor pode ter fechado a guardada: um GET vai de novo */
    de_novo:
        char pedido[6144];   /* com os cookies do Cipó (H82) */
        bool porta_padrao = u.porta == (u.seguro ? 443 : 80);
        char host[80];
        if (porta_padrao) copia_texto(host, u.servidor, sizeof host);
        else formatar_texto(host, sizeof host, "%s:%d", u.servidor, u.porta);
        char tamanho_corpo[48] = "";
        if (corpo) formatar_texto(tamanho_corpo, sizeof tamanho_corpo, "Content-Length: %u\r\n", n_corpo);
        int n = formatar_texto(pedido, sizeof pedido,
                               "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: SaguiOS/3.0.0 (+https://github.com/erigobeli/sagui-os)\r\nAccept: */*\r\n"
                               "Accept-Encoding: gzip\r\nConnection: %s\r\n%s%s\r\n", metodo, u.caminho, host,
                               guardar_ligado ? "keep-alive" : "close", extra ? extra : "", tamanho_corpo);
        if (n >= (int)sizeof pedido - 1) { canal_fechar(&c); copia_texto(resp->erro, "cabeçalhos grandes demais", sizeof resp->erro); return ERRO_INVALIDO; }
        char linha[2048];   /* Set-Cookie e Content-Security-Policy passam fácil de 512 */
        bool mandou = canal_escrever(&c, pedido, (unsigned)n) >= 0 && !(corpo && n_corpo && canal_escrever(&c, corpo, n_corpo) < 0);
        bool respondeu = mandou && linha_http(&c, linha, sizeof linha) >= 0;
        if (!respondeu && repetir) {
            canal_fechar(&c);
            repetir = false;
            registrar("http: %s fechou a conexão guardada; outra", u.servidor);
            if (canal_abrir(&c, &u, resp->erro, sizeof resp->erro) < 0) return ERRO_REDE;
            goto de_novo;
        }
        if (!mandou) { canal_fechar(&c); copia_texto(resp->erro, "a conexão caiu ao mandar o pedido", sizeof resp->erro); return ERRO_REDE; }
        if (!respondeu || compara_texto_n(linha, "HTTP/1.", 7) != 0) {
            canal_fechar(&c);
            copia_texto(resp->erro, "resposta inválida do servidor", sizeof resp->erro);
            return ERRO_REDE;
        }
        resp->situacao = texto_para_int(linha + 9);
        if (cabecalhos) cabecalhos(ctx, url, linha);
        long tamanho = -1;
        bool pedacos = false, compactado = false;
        bool fica = guardar_ligado && compara_texto_n(linha, "HTTP/1.1", 8) == 0;   /* a conexão pode ser guardada */
        char destino[MAX_URL_HTTP] = "";
        resp->tipo[0] = 0;
        while (linha_http(&c, linha, sizeof linha) > 0) {
            const char *v;
            if (cabecalhos) cabecalhos(ctx, url, linha);
            if (cabecalho(linha, "Content-Length", &v)) tamanho = texto_para_int(v);
            else if (cabecalho(linha, "Transfer-Encoding", &v) && compara_texto_n(v, "chunked", 7) == 0) pedacos = true;
            else if (cabecalho(linha, "Content-Encoding", &v) && compara_texto_n(v, "gzip", 4) == 0) compactado = true;
            else if (cabecalho(linha, "Location", &v)) copia_texto(destino, v, sizeof destino);
            else if (cabecalho(linha, "Content-Type", &v)) copia_texto(resp->tipo, v, sizeof resp->tipo);
            else if (cabecalho(linha, "Connection", &v) && compara_texto_n(v, "close", 5) == 0) fica = false;
        }
        /* HEAD, 1xx, 204 e 304 não têm corpo, mesmo com Content-Length. */
        bool sem_corpo = compara_texto(metodo, "HEAD") == 0 || resp->situacao < 200 || resp->situacao == 204 || resp->situacao == 304;
        if (sem_corpo) { tamanho = 0; pedacos = false; }
        if (!pedacos && tamanho < 0) fica = false;   /* vai até fechar: não dá para reaproveitar */
        if (resp->situacao >= 300 && resp->situacao < 400 && destino[0] && !corpo) {
            canal_fechar(&c);
            if (destino[0] == '/')
                formatar_texto(url, sizeof url, "%s://%s%s", u.seguro ? "https" : "http", host, destino);
            else copia_texto(url, destino, sizeof url);
            registrar("http: %d -> %s", resp->situacao, url);
            continue;
        }
        copia_texto(resp->url_final, url, sizeof resp->url_final);
        int ok;
        if (compactado && !sem_corpo) {
            juntado_t j = { NULL, 0, 0 };
            ok = ler_corpo(&c, tamanho, pedacos, juntar, &j, resp);
            if (ok == 0 && j.n) ok = entregar_descompactado(&j, receptor, ctx, resp);
            libera(j.d);
        } else ok = ler_corpo(&c, tamanho, pedacos, receptor, ctx, resp);
        if (fica && ok == 0 && c.pos == c.tam) guardar_conexao(&u, &c);
        else canal_fechar(&c);
        if (ok < 0 && !resp->erro[0]) copia_texto(resp->erro, ok == ERRO_INVALIDO ? "parado por quem pediu" : "a conexão caiu no meio", sizeof resp->erro);
        return ok;
    }
    copia_texto(resp->erro, "redirecionamentos demais", sizeof resp->erro);
    return ERRO_REDE;
}

/* ------------------------------------------------------------ lote (H129) */

typedef struct { http_lote_receptor_t r; void *ctx; int i; } ponte_lote_t;
static int receber_lote(void *p, const char *dados, unsigned n)
{
    ponte_lote_t *pl = p;
    return pl->r(pl->ctx, pl->i, dados, n);
}

/* Vários GET ao mesmo servidor de uma vez, pela mesma conexão: os pedidos
 * saem todos juntos e as respostas chegam na mesma ordem (pipelining do
 * HTTP/1.1). Para n imagens pequenas, isso troca n idas e voltas pela rede
 * por uma. Só entram os endereços do mesmo servidor do primeiro; cada
 * resposta vai para fim(ctx, i, resp, ok) depois do corpo, que chega por
 * receptor(ctx, i, ...). Devolve quantas respostas chegaram inteiras: as que
 * faltarem (o servidor fechou no meio, um redirecionamento) o chamador pede
 * de novo, uma a uma. */
int http_lote(const char *const *urls, const char *const *extras, int n,
              http_lote_receptor_t receptor, http_lote_fim_t fim, void *ctx)
{
    if (n <= 0) return 0;
    url_t u0;
    if (url_separar(urls[0], &u0) < 0) return 0;
    canal_t c;
    bool reusada = guardar_ligado && pegar_guardada(&u0, &c);
    if (!reusada) {
        if (guardar_ligado && u0.seguro) abrir_vaga_tls();
        char erro[128];
        if (canal_abrir(&c, &u0, erro, sizeof erro) < 0) return 0;
    }
    bool porta_padrao = u0.porta == (u0.seguro ? 443 : 80);
    char host[160];
    if (porta_padrao) copia_texto(host, u0.servidor, sizeof host);
    else formatar_texto(host, sizeof host, "%s:%d", u0.servidor, u0.porta);
    /* Os pedidos: só os do mesmo servidor, em ordem, até o primeiro de outro. */
    int m = 0;
    for (; m < n; m++) {
        url_t u;
        if (url_separar(urls[m], &u) < 0 || u.seguro != u0.seguro || u.porta != u0.porta || compara_texto(u.servidor, u0.servidor) != 0) break;
        char pedido[6144];
        int k = formatar_texto(pedido, sizeof pedido,
                               "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: SaguiOS/3.0.0 (+https://github.com/erigobeli/sagui-os)\r\nAccept: */*\r\n"
                               "Accept-Encoding: identity\r\nConnection: keep-alive\r\n%s\r\n", u.caminho, host,
                               extras && extras[m] ? extras[m] : "");
        if (k >= (int)sizeof pedido - 1) break;
        if (canal_escrever(&c, pedido, (unsigned)k) < 0) { canal_fechar(&c); return 0; }
    }
    /* As respostas, na ordem dos pedidos. */
    int prontos = 0;
    bool fica = true;
    for (int i = 0; i < m && fica; i++) {
        http_resposta_t resp;
        memset(&resp, 0, sizeof resp);
        char linha[2048];
        if (linha_http(&c, linha, sizeof linha) < 0 || compara_texto_n(linha, "HTTP/1.", 7) != 0) { fica = false; break; }
        resp.situacao = texto_para_int(linha + 9);
        if (compara_texto_n(linha, "HTTP/1.1", 8) != 0) fica = false;
        long tamanho = -1;
        bool pedacos = false;
        while (linha_http(&c, linha, sizeof linha) > 0) {
            const char *v;
            if (cabecalho(linha, "Content-Length", &v)) tamanho = texto_para_int(v);
            else if (cabecalho(linha, "Transfer-Encoding", &v) && compara_texto_n(v, "chunked", 7) == 0) pedacos = true;
            else if (cabecalho(linha, "Content-Type", &v)) copia_texto(resp.tipo, v, sizeof resp.tipo);
            else if (cabecalho(linha, "Connection", &v) && compara_texto_n(v, "close", 5) == 0) fica = false;
        }
        if (resp.situacao < 200 || resp.situacao == 204 || resp.situacao == 304) { tamanho = 0; pedacos = false; }
        if (!pedacos && tamanho < 0) fica = false;
        copia_texto(resp.url_final, urls[i], sizeof resp.url_final);
        ponte_lote_t pl = { receptor, ctx, i };
        int ok = ler_corpo(&c, tamanho, pedacos, receber_lote, &pl, &resp);
        if (ok < 0) { fica = false; break; }
        fim(ctx, i, &resp);
        prontos++;
    }
    if (fica && prontos == m && c.pos == c.tam) guardar_conexao(&u0, &c);
    else canal_fechar(&c);
    return prontos;
}
