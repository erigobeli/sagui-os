/* teste_tls.c - programa de teste do TLS e dos certificados (H52/H53).
 *
 * Roda no computador de desenvolvimento, ligado à biblioteca compilada com
 * as opções do SaguiOS. Só este arquivo usa a libc e os soquetes do Linux:
 * ele é o "ambiente" (TlsAmbiente) que no SaguiOS será o TCP do sistema.
 * Quem manda nele é o tests/h52_h53_tls.py.
 *
 * Modos:
 *   conectar <host> <porta> <nome> <data|-> <raiz.der>...
 *       Faz o aperto de mão e depois obedece a comandos pela entrada:
 *         e <hex>      escreve esses bytes                 -> "ok N"
 *         g <n> <s>    escreve n bytes de um padrão        -> "ok N"
 *         l <n>        lê exatamente n bytes               -> "dados <hex>"
 *         t            lê até o servidor fechar            -> "fim <código> <total> <sha256>"
 *         q            fecha e sai
 *   validar <nome|-> <data> <nraizes> <raiz.der>... <cert.der>...
 *   nome <cert.der> <nome>...
 *   analisar <cert.der>...
 *
 * <data> é AAAAMMDDhhmmss (o "agora" de mentira para os testes de validade)
 * ou "-" para usar o relógio da máquina. Variáveis de ambiente: TLS_PICADO=1
 * faz a rede entregar e aceitar poucos bytes por vez, para exercitar as
 * leituras e os envios parciais; TLS_ALPN=protocolo anuncia o protocolo;
 * TLS_LIMITE=ms muda o limite de tempo do aperto de mão. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "tls.h"

#define RAIZES_MAX 256

typedef struct {
    int soquete;
    int picado;
    const char *data;           /* AAAAMMDDhhmmss ou 0 */
} Mundo;

static Tls tls;                 /* uns 72 KB: fora da pilha, como no SaguiOS */

/* ------------------------------------------------------------ o ambiente */

static int amb_enviar(void *ctx, const u8 *dados, u32 n)
{
    Mundo *m = ctx;
    if (m->picado && n > 100) n = 100;
    ssize_t r = send(m->soquete, dados, n, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (r < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    return (int)r;
}

static int amb_receber(void *ctx, u8 *dados, u32 max)
{
    Mundo *m = ctx;
    if (m->picado && max > 3) max = 3;
    ssize_t r = recv(m->soquete, dados, max, MSG_DONTWAIT);
    if (r < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    if (r == 0) return -1;      /* o outro lado fechou */
    return (int)r;
}

static void amb_esperar(void *ctx, u32 ms)
{
    /* Dorme até chegar alguma coisa ou passar o tempo. */
    Mundo *m = ctx;
    struct pollfd p = { m->soquete, POLLIN, 0 };
    poll(&p, 1, (int)ms);
}

static u32 amb_agora_ms(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u32)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}

static void amb_aleatorio(void *ctx, u8 *dados, u32 n)
{
    (void)ctx;
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(dados, 1, n, f) != n) abort();
    fclose(f);
}

static void amb_data_atual(void *ctx, int *ano, int *mes, int *dia, int *hora, int *min, int *seg)
{
    Mundo *m = ctx;
    if (m->data) {
        sscanf(m->data, "%4d%2d%2d%2d%2d%2d", ano, mes, dia, hora, min, seg);
        return;
    }
    time_t agora = time(0);
    struct tm *g = gmtime(&agora);
    *ano = g->tm_year + 1900; *mes = g->tm_mon + 1; *dia = g->tm_mday;
    *hora = g->tm_hour; *min = g->tm_min; *seg = g->tm_sec;
}

/* ------------------------------------------------------------ utilidades */

static int ler_arquivo(const char *caminho, X509Der *d)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) { fprintf(stderr, "não abre %s\n", caminho); return -1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *p = malloc(n ? n : 1);
    if (fread(p, 1, n, f) != (size_t)n) { fclose(f); return -1; }
    fclose(f);
    d->der = p;
    d->n = (u32)n;
    return 0;
}

static void data_de_agora(const char *arg, char data[14])
{
    Mundo m = { -1, 0, strcmp(arg, "-") ? arg : 0 };
    int a, me, d, h, mi, s;
    amb_data_atual(&m, &a, &me, &d, &h, &mi, &s);
    x509_montar_data(data, a, me, d, h, mi, s);
}

static void imprimir_hex(const u8 *p, u32 n)
{
    for (u32 i = 0; i < n; i++) printf("%02x", p[i]);
}

/* ------------------------------------------------------------ modos sem rede */

static int modo_validar(int argc, char **argv)
{
    if (argc < 5) return 2;
    const char *nome = strcmp(argv[2], "-") ? argv[2] : 0;
    char data[14], erro[200];
    int nraizes = atoi(argv[4]), ncadeia = argc - 5 - nraizes;
    static X509Der raizes[RAIZES_MAX], cadeia[X509_CADEIA_MAX + 4];
    if (nraizes < 0 || nraizes > RAIZES_MAX || ncadeia < 0 || ncadeia > X509_CADEIA_MAX + 4) return 2;
    data_de_agora(argv[3], data);
    for (int i = 0; i < nraizes; i++) if (ler_arquivo(argv[5 + i], &raizes[i])) return 2;
    for (int i = 0; i < ncadeia; i++) if (ler_arquivo(argv[5 + nraizes + i], &cadeia[i])) return 2;
    if (x509_validar_cadeia(cadeia, ncadeia, raizes, nraizes, nome, data, erro, sizeof erro) == 0)
        printf("ok\n");
    else
        printf("erro: %s\n", erro);
    return 0;
}

static int modo_nome(int argc, char **argv)
{
    X509Der d;
    X509 c;
    if (argc < 3 || ler_arquivo(argv[2], &d)) return 2;
    if (x509_ler(&c, d.der, d.n)) { printf("falha\n"); return 0; }
    for (int i = 3; i < argc; i++) printf("%d", x509_nome_confere(&c, argv[i]));
    printf("\n");
    return 0;
}

static int modo_analisar(int argc, char **argv)
{
    static const char *chaves[] = { "chave?", "rsa", "p256", "p384", "ed25519" };
    static const char *assinaturas[] = { "assinatura?", "rsa-pkcs1", "rsa-pss", "ecdsa", "ed25519" };
    for (int i = 2; i < argc; i++) {
        X509Der d;
        X509 c;
        if (ler_arquivo(argv[i], &d)) return 2;
        if (x509_ler(&c, d.der, d.n)) { printf("falha\n"); continue; }
        /* Se o emissor é o próprio dono, confere a assinatura com a chave dele. */
        int propria = -9;
        if (c.nemissor == c.nsujeito && !memcmp(c.emissor, c.sujeito, c.nsujeito))
            propria = x509_conferir(&c.chave, c.assin_tipo, c.assin_resumo, c.tbs, c.ntbs, c.assin, c.nassin);
        printf("ok %s %s resumo=%d ca=%d caminho=%d critica=%d propria=%d %.14s %.14s cn=%s\n",
               chaves[c.chave.tipo], assinaturas[c.assin_tipo], c.assin_resumo, c.autoridade,
               c.caminho_max, c.critica_desconhecida, propria, c.inicio, c.fim, c.cn);
    }
    return 0;
}

/* ------------------------------------------------------------ modo com rede */

static int abrir_soquete(const char *host, const char *porta)
{
    struct addrinfo dica = { 0 }, *lista, *a;
    int s = -1;
    dica.ai_family = AF_INET;
    dica.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, porta, &dica, &lista)) return -1;
    for (a = lista; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s < 0) continue;
        if (connect(s, a->ai_addr, a->ai_addrlen) == 0) break;
        close(s);
        s = -1;
    }
    freeaddrinfo(lista);
    if (s >= 0) fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    return s;
}

/* Lê até juntar "quero" bytes, o servidor fechar ou passar o tempo. Devolve
 * o código final (0 = juntou tudo). */
static int ler_ate(Mundo *m, u8 *destino, u32 quero, u32 *veio, int so_resumir, Sha256 *resumo)
{
    u8 pedaco[4096];
    u32 inicio = amb_agora_ms(0);
    *veio = 0;
    for (;;) {
        u32 max = sizeof pedaco;
        if (!so_resumir) {
            if (*veio == quero) return 0;
            if (max > quero - *veio) max = quero - *veio;
        }
        int r = tls_ler(&tls, so_resumir ? pedaco : destino + *veio, max);
        if (r < 0) return r;
        if (r == 0) {
            if (amb_agora_ms(0) - inicio > 20000) return -9;
            amb_esperar(m, 20);
            continue;
        }
        if (so_resumir) sha256_atualizar(resumo, pedaco, r);
        *veio += r;
    }
}

/* Fechar um soquete com dados ainda por ler (os bilhetes de sessão que o
 * servidor manda e ninguém leu, ou o resto do aperto de mão recusado) faz o
 * Linux mandar um RST, e o servidor perde o que ainda não tinha lido do que
 * mandamos (inclusive o nosso alerta). Então: avisamos que não escrevemos
 * mais e esperamos o servidor fechar o lado dele. */
static void encerrar_soquete(Mundo *m)
{
    shutdown(m->soquete, SHUT_WR);
    for (u32 inicio = amb_agora_ms(0); amb_agora_ms(0) - inicio < 3000; ) {
        u8 resto[4096];
        ssize_t r = recv(m->soquete, resto, sizeof resto, MSG_DONTWAIT);
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) break;
        if (r < 0) amb_esperar(m, 50);
    }
    close(m->soquete);
}

static int modo_conectar(int argc, char **argv)
{
    static X509Der raizes[RAIZES_MAX];
    static u8 dados[1 << 20];
    static char linha[(1 << 21) + 64];
    Mundo m = { -1, getenv("TLS_PICADO") != 0, 0 };
    u32 nraizes = 0;

    if (argc < 6) return 2;
    if (strcmp(argv[5], "-")) m.data = argv[5];
    for (int i = 6; i < argc && nraizes < RAIZES_MAX; i++)
        if (ler_arquivo(argv[i], &raizes[nraizes++])) return 2;

    m.soquete = abrir_soquete(argv[2], argv[3]);
    if (m.soquete < 0) { printf("erro: sem conexão TCP\n"); return 0; }

    TlsAmbiente amb = { &m, amb_enviar, amb_receber, amb_esperar, amb_agora_ms, amb_aleatorio, amb_data_atual };
    TlsOpcoes op = { argv[4], raizes, nraizes, 20000, getenv("TLS_ALPN") };
    if (getenv("TLS_LIMITE")) op.limite_ms = (u32)atoi(getenv("TLS_LIMITE"));
    if (tls_conectar(&tls, &amb, &op) != TLS_OK) {
        printf("erro: %s\n", tls_erro(&tls));
        fflush(stdout);
        encerrar_soquete(&m);
        return 0;
    }
    printf("conectado %s\n", tls_cifra(&tls));
    fflush(stdout);

    while (fgets(linha, sizeof linha, stdin)) {
        u32 n = 0, veio;
        int r;
        if (linha[0] == 'e') {
            for (char *p = linha + 2; p[0] && p[1] && p[0] != '\n'; p += 2) {
                unsigned b;
                sscanf(p, "%2x", &b);
                dados[n++] = (u8)b;
            }
            r = tls_escrever(&tls, dados, n);
            if (r < 0) printf("erro: %s\n", tls_erro(&tls)); else printf("ok %d\n", r);
        } else if (linha[0] == 'g') {
            unsigned semente = 0;
            sscanf(linha + 2, "%u %u", &n, &semente);
            if (n > sizeof dados) n = sizeof dados;
            for (u32 i = 0; i < n; i++) dados[i] = (u8)(i * 31 + semente + (i >> 8));
            r = tls_escrever(&tls, dados, n);
            if (r < 0) printf("erro: %s\n", tls_erro(&tls)); else printf("ok %d\n", r);
        } else if (linha[0] == 'l') {
            sscanf(linha + 2, "%u", &n);
            if (n > sizeof dados) n = sizeof dados;
            r = ler_ate(&m, dados, n, &veio, 0, 0);
            if (r == 0) {
                printf("dados ");
                imprimir_hex(dados, veio);
                printf("\n");
            } else {
                printf("fim %d %u %s\n", r, veio, tls_erro(&tls));
            }
        } else if (linha[0] == 't') {
            Sha256 s;
            u8 h[32];
            sha256_iniciar(&s);
            r = ler_ate(&m, 0, 0, &veio, 1, &s);
            sha256_finalizar(&s, h);
            printf("fim %d %u ", r, veio);
            imprimir_hex(h, 32);
            printf(" %s\n", tls_erro(&tls));
        } else if (linha[0] == 'q') {
            break;
        }
        fflush(stdout);
    }
    tls_fechar(&tls);
    encerrar_soquete(&m);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "conectar")) return modo_conectar(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "validar")) return modo_validar(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "nome")) return modo_nome(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "analisar")) return modo_analisar(argc, argv);
    fprintf(stderr, "uso: teste_tls conectar|validar|nome|analisar ...\n");
    return 2;
}
