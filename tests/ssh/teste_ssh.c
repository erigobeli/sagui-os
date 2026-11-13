/* teste_ssh.c - programa de teste do cliente SSH (H56 a H58).
 *
 * Roda no computador de desenvolvimento, ligado à biblioteca compilada com
 * as opções do SaguiOS. Só este arquivo usa a libc e os soquetes do Linux:
 * ele é o "ambiente" (SshAmbiente) que no SaguiOS será o TCP do sistema.
 * Quem manda nele é o tests/h56_h58_ssh.py.
 *
 * Modos:
 *   gerar <arquivo privado> <arquivo público> <comentário>
 *       Sorteia uma semente e grava o par de chaves com as funções da
 *       biblioteca.
 *   digital <arquivo de chave pública>
 *       Lê a linha "tipo base64 comentário" e mostra a impressão digital.
 *   publica <arquivo privado> <comentário>
 *       Lê a chave privada e mostra a linha pública (ou "erro").
 *   tamanho
 *       Mostra o tamanho do contexto de uma conexão (sizeof(Ssh)).
 *   b64 <hex> | b64d <texto>
 *       Codifica (com e sem enchimento) / decodifica.
 *   conectar <host> <porta> <usuário>
 *       Conecta, autentica, abre a sessão e depois obedece a comandos pela
 *       entrada:
 *         e <hex>          escreve esses bytes                 -> "ok N"
 *         g <n>            escreve n bytes de um padrão        -> "ok N"
 *         u <ms> <hex>     lê até aparecer essa marca          -> "dados <1|0|código> <hex>"
 *         c <ms>           lê até o canal fechar               -> "fim <código> <total> <sha256> <saída>"
 *         j <col> <lin>    muda o tamanho do terminal          -> "ok"
 *         f                manda o fim da entrada (EOF)        -> "ok"
 *         i                                                    -> "info trocas=N ..."
 *         q                fecha e sai
 *
 * Variáveis de ambiente do "conectar":
 *   SSH_CHAVE=arquivo      chave privada (formato do SaguiOS) para autenticar
 *   SSH_CHAVE_VEZES=n      tenta a chave n vezes (para provocar o servidor)
 *   SSH_SENHA=senha        tenta senha (depois da chave, se houver)
 *   SSH_SENHA_ANTES=senha  tenta senha antes da chave
 *   SSH_ACEITAR=sim|nao|SHA256:...   resposta da conferência da chave do servidor
 *   SSH_CIFRAS, SSH_CHAVES_SERVIDOR  listas para as opções
 *   SSH_TERM, SSH_COLUNAS, SSH_LINHAS   terminal (sem SSH_TERM não pede terminal)
 *   SSH_COMANDO=texto      executa o comando em vez do shell
 *   SSH_TROCAR=bytes       pede chaves novas depois de tantos bytes
 *   SSH_LIMITE=ms          limite de espera
 *   SSH_PICADO=1           a rede entrega e aceita poucos bytes por vez
 *   SSH_ESTRAGAR=k         troca um bit do k-ésimo "corpo de pacote" recebido */
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

#include "ssh.h"

typedef struct {
    int soquete;
    int picado;
    int estragar;               /* quantos corpos de pacote faltam até o estrago */
} Mundo;

static Ssh ssh;                 /* uns 55 KB: fora da pilha, como no SaguiOS */

#define ACUMULADO_MAX (8 * 1024 * 1024)
static u8 acumulado[ACUMULADO_MAX];     /* o que chegou e ainda não foi mostrado */
static u32 nacumulado;

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
    /* A biblioteca pede primeiro um bloco (8 ou 16 bytes) e depois o resto
     * do pacote: um pedido maior que 16 é o corpo de um pacote. O estrago
     * vai no primeiro byte dele (por isso, nesse caso, não picamos). */
    int corpo = max > 16;
    int alvo = corpo && m->estragar == 1;
    if (m->picado && max > 3 && !alvo) max = 3;
    ssize_t r = recv(m->soquete, dados, max, MSG_DONTWAIT);
    if (r < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    if (r == 0) return -1;      /* o outro lado fechou */
    if (corpo && m->estragar > 0 && !m->picado) {
        if (--m->estragar == 0) dados[0] ^= 0x10;
    }
    return (int)r;
}

static void amb_esperar(void *ctx, u32 ms)
{
    Mundo *m = ctx;
    struct pollfd p = { m->soquete, POLLIN, 0 };
    poll(&p, 1, (int)ms);
}

static u32 relogio_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u32)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static u32 amb_agora_ms(void *ctx)
{
    (void)ctx;
    return relogio_ms();
}

static void sortear(u8 *dados, u32 n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(dados, 1, n, f) != n) { fprintf(stderr, "sem /dev/urandom\n"); exit(2); }
    fclose(f);
}

static void amb_aleatorio(void *ctx, u8 *dados, u32 n)
{
    (void)ctx;
    sortear(dados, n);
}

/* ------------------------------------------------------------ utilidades */

static int ler_arquivo(const char *caminho, char *buf, u32 max)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, max - 1, f);
    fclose(f);
    buf[n] = 0;
    return (int)n;
}

static void mostrar_hex(const u8 *p, u32 n)
{
    for (u32 i = 0; i < n; i++) printf("%02x", p[i]);
}

static u32 ler_hex(const char *t, u8 *saida, u32 max)
{
    u32 n = 0;
    unsigned v;
    while (n < max && sscanf(t, "%2x", &v) == 1) { saida[n++] = (u8)v; t += 2; }
    return n;
}

/* ------------------------------------------------------------ modos sem conexão */

static int modo_gerar(const char *priv, const char *pub, const char *comentario)
{
    u8 semente[32];
    char texto[SSH_ARQUIVO_CHAVE_MAX], linha[256];
    sortear(semente, 32);
    int n = ssh_chave_privada_escrever(semente, comentario, texto, sizeof texto);
    int m = ssh_chave_publica_linha(semente, comentario, linha, sizeof linha);
    if (n < 0 || m < 0) { printf("erro\n"); return 1; }
    FILE *f = fopen(priv, "wb");
    fwrite(texto, 1, (size_t)n, f);
    fclose(f);
    f = fopen(pub, "wb");
    fwrite(linha, 1, (size_t)m, f);
    fclose(f);
    printf("ok\n");
    return 0;
}

static int modo_digital(const char *arquivo)
{
    char texto[4096], digital[SSH_DIGITAL_MAX];
    u8 blob[2048];
    int n = ler_arquivo(arquivo, texto, sizeof texto);
    if (n < 0) { printf("erro\n"); return 1; }
    int r = ssh_chave_publica_ler(texto, (u32)n, blob, sizeof blob);
    if (r < 0) { printf("erro\n"); return 1; }
    ssh_digital(blob, (u32)r, digital);
    printf("%s\n", digital);
    return 0;
}

static int modo_publica(const char *arquivo, const char *comentario)
{
    char texto[4096], linha[256];
    u8 semente[32];
    int n = ler_arquivo(arquivo, texto, sizeof texto);
    if (n < 0 || ssh_chave_privada_ler(texto, (u32)n, semente) != 0) { printf("erro\n"); return 1; }
    if (ssh_chave_publica_linha(semente, comentario, linha, sizeof linha) < 0) { printf("erro\n"); return 1; }
    printf("%s", linha);
    return 0;
}

static int modo_b64(const char *hex)
{
    static u8 dados[4096];
    static char texto[8192];
    u32 n = ler_hex(hex, dados, sizeof dados);
    if (ssh_base64_codificar(dados, n, texto, sizeof texto, 1) < 0) { printf("erro\n"); return 1; }
    printf("%s ", texto);
    if (ssh_base64_codificar(dados, n, texto, sizeof texto, 0) < 0) { printf("erro\n"); return 1; }
    printf("%s\n", texto);
    return 0;
}

static int modo_b64d(const char *texto)
{
    static u8 dados[4096];
    int n = ssh_base64_decodificar(texto, (u32)strlen(texto), dados, sizeof dados);
    if (n < 0) { printf("erro\n"); return 1; }
    printf("ok ");
    mostrar_hex(dados, (u32)n);
    printf("\n");
    return 0;
}

/* ------------------------------------------------------------ conexão */

static int conferir_chave(void *ctx, const char *tipo, const u8 *blob, u32 nblob, const char *digital)
{
    const char *aceitar = ctx;
    static char b64[4096];
    ssh_base64_codificar(blob, nblob, b64, sizeof b64, 1);
    printf("chave %s %s %s\n", tipo, digital, b64);
    fflush(stdout);
    if (!aceitar || !strcmp(aceitar, "sim")) return 1;
    return !strcmp(aceitar, digital);
}

/* Lê o que houver para o acumulado. Devolve 1 se leu, 0 se não havia nada,
 * negativo se a sessão acabou. */
static int bombear(void)
{
    static u8 pedaco[40000];
    int r = ssh_ler(&ssh, pedaco, sizeof pedaco);
    if (r <= 0) return r;
    if (nacumulado + (u32)r > ACUMULADO_MAX) { fprintf(stderr, "acumulado cheio\n"); exit(2); }
    memcpy(acumulado + nacumulado, pedaco, (size_t)r);
    nacumulado += (u32)r;
    return 1;
}

/* Escreve tudo, lendo nos intervalos (é assim que o programa do SaguiOS
 * terá de fazer: o ssh_escrever pode aceitar só uma parte). */
static int escrever_tudo(Mundo *m, const u8 *dados, u32 n)
{
    u32 feito = 0, desde = relogio_ms();
    while (feito < n) {
        int r = ssh_escrever(&ssh, dados + feito, n - feito);
        if (r < 0) return r;
        feito += (u32)r;
        if (r > 0) desde = relogio_ms();
        int b = bombear();
        if (b < 0 && b != SSH_FECHADO) return b;
        if (r == 0 && b == 0) {
            if (relogio_ms() - desde > 20000) return -100;
            amb_esperar(m, 5);
        }
    }
    return (int)feito;
}

static void mostrar_saida(void)
{
    u32 status = 0;
    const char *sinal = "";
    int r = ssh_saida(&ssh, &status, &sinal);
    if (r == 1) printf("status=%u", status);
    else if (r == 2) printf("sinal=%s", sinal);
    else printf("sem-status");
}

static int modo_conectar(const char *host, const char *porta, const char *usuario)
{
    Mundo m = { -1, getenv("SSH_PICADO") != 0, getenv("SSH_ESTRAGAR") ? atoi(getenv("SSH_ESTRAGAR")) : 0 };
    struct addrinfo dica = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *lista;
    if (getaddrinfo(host, porta, &dica, &lista) != 0) { printf("erro endereço\n"); return 1; }
    m.soquete = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(m.soquete, lista->ai_addr, lista->ai_addrlen) != 0) { printf("erro conexão TCP\n"); return 1; }
    freeaddrinfo(lista);
    fcntl(m.soquete, F_SETFL, fcntl(m.soquete, F_GETFL) | O_NONBLOCK);

    SshAmbiente amb = { &m, amb_enviar, amb_receber, amb_esperar, amb_agora_ms, amb_aleatorio };
    SshOpcoes op;
    memset(&op, 0, sizeof op);
    op.conferir_chave = conferir_chave;
    op.ctx_chave = getenv("SSH_ACEITAR");
    op.chaves_servidor = getenv("SSH_CHAVES_SERVIDOR");
    op.cifras = getenv("SSH_CIFRAS");
    if (getenv("SSH_TROCAR")) op.trocar_apos_bytes = (u32)atoi(getenv("SSH_TROCAR"));
    if (getenv("SSH_LIMITE")) op.limite_ms = (u32)atoi(getenv("SSH_LIMITE"));

    int r = ssh_conectar(&ssh, &amb, &op);
    if (r != SSH_OK) { printf("erro %d %s\n", r, ssh_erro(&ssh)); return 1; }
    printf("transporte %s | %s\n", ssh_versao_servidor(&ssh), ssh_algoritmos(&ssh));

    r = ssh_autenticar_nenhum(&ssh, usuario);
    printf("metodos %d %s\n", r, ssh_metodos(&ssh));
    if (r != SSH_OK && r != SSH_RECUSADO) { printf("erro %d %s\n", r, ssh_erro(&ssh)); return 1; }

    if (r != SSH_OK && getenv("SSH_SENHA_ANTES")) {
        r = ssh_autenticar_senha(&ssh, usuario, getenv("SSH_SENHA_ANTES"));
        printf("auth senha %d %s\n", r, r == SSH_OK ? "" : ssh_erro(&ssh));
        if (r != SSH_OK && r != SSH_RECUSADO) { printf("erro %d %s\n", r, ssh_erro(&ssh)); return 1; }
    }
    if (r != SSH_OK && getenv("SSH_CHAVE")) {
        char texto[4096];
        u8 semente[32];
        int n = ler_arquivo(getenv("SSH_CHAVE"), texto, sizeof texto);
        if (n < 0 || ssh_chave_privada_ler(texto, (u32)n, semente) != 0) { printf("erro chave privada inválida\n"); return 1; }
        int vezes = getenv("SSH_CHAVE_VEZES") ? atoi(getenv("SSH_CHAVE_VEZES")) : 1;
        while (vezes-- > 0 && r != SSH_OK) {
            r = ssh_autenticar_chave(&ssh, usuario, semente);
            printf("auth chave %d %s\n", r, r == SSH_OK ? "" : ssh_erro(&ssh));
            if (r != SSH_OK && r != SSH_RECUSADO) { printf("erro %d %s\n", r, ssh_erro(&ssh)); return 1; }
        }
    }
    if (r != SSH_OK && getenv("SSH_SENHA")) {
        r = ssh_autenticar_senha(&ssh, usuario, getenv("SSH_SENHA"));
        printf("auth senha %d %s\n", r, r == SSH_OK ? "" : ssh_erro(&ssh));
        if (r != SSH_OK && r != SSH_RECUSADO) { printf("erro %d %s\n", r, ssh_erro(&ssh)); return 1; }
    }
    if (r != SSH_OK) {
        printf("erro %d não autenticado; métodos: %s\n", r, ssh_metodos(&ssh));
        ssh_fechar(&ssh);
        return 1;
    }

    const char *term = getenv("SSH_TERM");
    u32 colunas = getenv("SSH_COLUNAS") ? (u32)atoi(getenv("SSH_COLUNAS")) : 80;
    u32 linhas = getenv("SSH_LINHAS") ? (u32)atoi(getenv("SSH_LINHAS")) : 24;
    r = ssh_abrir_sessao(&ssh, term, colunas, linhas, getenv("SSH_COMANDO"));
    if (r != SSH_OK) { printf("erro %d %s\n", r, ssh_erro(&ssh)); return 1; }
    printf("conectado\n");
    fflush(stdout);

    static char linha[2 * 1024 * 1024];
    static u8 dados[1024 * 1024];
    while (fgets(linha, sizeof linha, stdin)) {
        if (linha[0] == 'q') break;
        if (linha[0] == 'e' || linha[0] == 'g') {
            u32 n;
            if (linha[0] == 'e') n = ler_hex(linha + 2, dados, sizeof dados);
            else {
                n = (u32)atoi(linha + 2);
                if (n > sizeof dados) n = sizeof dados;
                for (u32 i = 0; i < n; i++) dados[i] = i % 100 == 99 ? '\n' : (u8)('a' + i % 23);
            }
            r = escrever_tudo(&m, dados, n);
            if (r < 0) printf("erro %d %s\n", r, ssh_erro(&ssh));
            else printf("ok %d\n", r);
        } else if (linha[0] == 'u') {
            unsigned ms = 0;
            int pos = 0;
            static u8 marca[256];
            sscanf(linha + 2, "%u %n", &ms, &pos);
            u32 nmarca = ler_hex(linha + 2 + pos, marca, sizeof marca);
            u32 inicio = relogio_ms();
            int achou = 0;
            for (;;) {
                if (nmarca && nacumulado >= nmarca && memmem(acumulado, nacumulado, marca, nmarca)) { achou = 1; break; }
                r = bombear();
                if (r < 0) { achou = r; break; }
                if (r == 0) {
                    if (relogio_ms() - inicio > ms) break;
                    amb_esperar(&m, 10);
                }
            }
            printf("dados %d ", achou);
            mostrar_hex(acumulado, nacumulado);
            printf("\n");
            nacumulado = 0;
        } else if (linha[0] == 'c') {
            unsigned ms = (unsigned)atoi(linha + 2);
            u32 inicio = relogio_ms();
            for (;;) {
                r = bombear();
                if (r < 0) break;
                if (r == 0) {
                    if (relogio_ms() - inicio > ms) { r = 0; break; }
                    amb_esperar(&m, 10);
                }
            }
            u8 resumo[32];
            sha256(acumulado, nacumulado, resumo);
            printf("fim %d %u ", r, nacumulado);
            mostrar_hex(resumo, 32);
            printf(" ");
            mostrar_saida();
            if (r != SSH_FECHADO && r != 0) printf(" %s", ssh_erro(&ssh));
            printf("\n");
            nacumulado = 0;
        } else if (linha[0] == 'j') {
            unsigned c = 0, l = 0;
            sscanf(linha + 2, "%u %u", &c, &l);
            r = ssh_mudar_tamanho(&ssh, c, l);
            if (r < 0) printf("erro %d %s\n", r, ssh_erro(&ssh));
            else printf("ok\n");
        } else if (linha[0] == 'f') {
            r = ssh_enviar_fim(&ssh);
            if (r < 0) printf("erro %d %s\n", r, ssh_erro(&ssh));
            else printf("ok\n");
        } else if (linha[0] == 'i') {
            printf("info trocas=%u algoritmos=%s\n", ssh_trocas(&ssh), ssh_algoritmos(&ssh));
        }
        fflush(stdout);
    }
    ssh_fechar(&ssh);
    close(m.soquete);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IOLBF, 0);
    if (argc == 2 && !strcmp(argv[1], "tamanho")) { printf("%u\n", (unsigned)sizeof(Ssh)); return 0; }
    if (argc == 5 && !strcmp(argv[1], "gerar")) return modo_gerar(argv[2], argv[3], argv[4]);
    if (argc == 3 && !strcmp(argv[1], "digital")) return modo_digital(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "publica")) return modo_publica(argv[2], argv[3]);
    if (argc == 3 && !strcmp(argv[1], "b64")) return modo_b64(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "b64d")) return modo_b64d(argv[2]);
    if (argc == 5 && !strcmp(argv[1], "conectar")) return modo_conectar(argv[2], argv[3], argv[4]);
    fprintf(stderr, "uso: ver o comentário no começo de tests/ssh/teste_ssh.c\n");
    return 2;
}
