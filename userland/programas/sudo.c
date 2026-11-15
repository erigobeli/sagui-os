/* sudo - executa um comando como outro usuário (padrão: root).
 *
 * Lê o usuário atual de /tmp/usuario_atual. Se for root, executa direto.
 * Caso contrário, pede a senha do usuário alvo e executa o comando.
 *
 * Uso: sudo <comando> [argumentos...]
 *      sudo -u <usuario> <comando> [argumentos...]
 *
 * Sem libc, apenas syscalls int 0x80 e libsagui. */

#include "sagui.h"

#define MAX_LINHA 256
#define MAX_USUARIO 32
#define MAX_SENHA 32

/* Remove espaços e nova-linha do final. */
static void limpar_final(char *s)
{
    int n = (int)tam_texto(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\n' || s[n-1] == '\r')) {
        s[--n] = 0;
    }
}

/* Lê o usuário atual de /tmp/usuario_atual. Devolve 0 se achou. */
static int ler_usuario_atual(char *usuario, unsigned max)
{
    int fd = abrir("/tmp/usuario_atual", ABRIR_LER);
    if (fd < 0) return -1;
    int n = ler(fd, usuario, max - 1);
    fechar(fd);
    if (n <= 0) return -1;
    usuario[n] = 0;
    limpar_final(usuario);
    return 0;
}

/* Lê uma linha de um descritor de arquivo. */
static int ler_linha_sudo(int fd, char *buf, unsigned max)
{
    unsigned n = 0;
    for (;;) {
        char c;
        int r = ler(fd, &c, 1);
        if (r <= 0) {
            if (n == 0) return -1;
            break;
        }
        if (c == '\n') break;
        if (n + 1 < max) buf[n++] = c;
    }
    buf[n] = 0;
    return (int)n;
}

/* Lê /etc/shadow e procura a senha do usuário. Devolve 0 se achou. */
static int encontrar_senha(const char *usuario, char *senha, unsigned max)
{
    int fd = abrir("/etc/shadow", ABRIR_LER);
    if (fd < 0) return -1;
    char buf[MAX_LINHA];
    while (1) {
        int n = ler_linha_sudo(fd, buf, sizeof buf);
        if (n < 0) break;
        char nome[MAX_USUARIO];
        int i;
        for (i = 0; buf[i] && buf[i] != ':' && i < MAX_USUARIO - 1; i++) {
            nome[i] = buf[i];
        }
        nome[i] = 0;
        if (compara_texto(nome, usuario) == 0) {
            int j = i + 1;
            int k = 0;
            while (buf[j] && buf[j] != ':' && k < max - 1) {
                senha[k++] = buf[j++];
            }
            senha[k] = 0;
            fechar(fd);
            return 0;
        }
    }
    fechar(fd);
    return -1;
}

/* Verifica se o usuário está na lista de sudoers (/etc/sudoers). */
static int eh_sudoer(const char *usuario)
{
    int fd = abrir("/etc/sudoers", ABRIR_LER);
    if (fd < 0) return 0;
    char buf[MAX_LINHA];
    int eh = 0;
    while (1) {
        int n = ler_linha_sudo(fd, buf, sizeof buf);
        if (n < 0) break;
        limpar_final(buf);
        if (compara_texto(buf, usuario) == 0) { eh = 1; break; }
    }
    fechar(fd);
    return eh;
}

/* Lê uma linha sem eco (para senha). */
static int ler_senha_sudo(char *buf, unsigned max)
{
    unsigned n = 0;
    for (;;) {
        char c;
        int r = ler(ENTRADA, &c, 1);
        if (r <= 0) {
            if (n == 0) return -1;
            break;
        }
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 0x7F) {
            if (n > 0) n--;
            continue;
        }
        if (n + 1 < max) buf[n++] = c;
        escrever_texto(SAIDA, "*");
    }
    buf[n] = 0;
    escrever_texto(SAIDA, "\n");
    return (int)n;
}

/* Converte inteiro para string decimal. */
static void int_para_texto(int v, char *buf, unsigned max)
{
    if (v == 0) { copia_texto(buf, "0", max); return; }
    char tmp[16];
    int n = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)(-v) : (unsigned)v;
    while (u && n < 15) { tmp[n++] = (char)('0' + u % 10); u /= 10; }
    if (neg) tmp[n++] = '-';
    size_t k = 0;
    while (n) { if (k < max - 1) buf[k++] = tmp[--n]; }
    buf[k] = 0;
}

int principal(int argc, char **argv)
{
    if (argc < 2) {
        escrever_texto(ERROS, "uso: sudo [-u <usuario>] <comando> [args...]\n");
        return 1;
    }

    int argi = 1;
    char alvo[MAX_USUARIO];
    copia_texto(alvo, "root", sizeof alvo);

    if (compara_texto(argv[1], "-u") == 0 && argc >= 4) {
        copia_texto(alvo, argv[2], sizeof alvo);
        argi = 3;
    }

    /* Lê o usuário atual. */
    char usuario_atual[MAX_USUARIO];
    if (ler_usuario_atual(usuario_atual, sizeof usuario_atual) < 0) {
        escrever_texto(ERROS, "sudo: não consigo identificar o usuário atual.\n");
        return 1;
    }

    /* Se já é o alvo, executa direto. */
    if (compara_texto(usuario_atual, alvo) == 0) {
        char *novo_argv[32];
        int k = 0;
        for (int i = argi; i < argc && k < 31; i++) {
            novo_argv[k++] = argv[i];
        }
        novo_argv[k] = NULL;
        int pid = executar(novo_argv[0], novo_argv);
        if (pid < 0) {
            escrever_texto(ERROS, "sudo: não consegui executar ");
            escrever_texto(ERROS, novo_argv[0]);
            escrever_texto(ERROS, "\n");
            return 1;
        }
        return 0;
    }

    /* Precisa de senha. Se o usuário atual não é sudoer, nega. */
    if (!eh_sudoer(usuario_atual)) {
        escrever_texto(ERROS, "sudo: ");
        escrever_texto(ERROS, usuario_atual);
        escrever_texto(ERROS, " não está na lista de sudoers.\n");
        return 1;
    }

    /* Pede a senha do usuário alvo. */
    char senha_digitada[MAX_SENHA];
    char senha_esperada[MAX_SENHA];
    int tentativas = 3;
    while (tentativas > 0) {
        escrever_texto(SAIDA, "[sudo] senha para ");
        escrever_texto(SAIDA, alvo);
        escrever_texto(SAIDA, ": ");
        int n = ler_senha_sudo(senha_digitada, sizeof senha_digitada);
        if (n < 0) {
            escrever_texto(SAIDA, "\n");
            return 1;
        }
        limpar_final(senha_digitada);

        if (encontrar_senha(alvo, senha_esperada, sizeof senha_esperada) == 0) {
            if (compara_texto(senha_digitada, senha_esperada) == 0) {
                break;
            }
        } else {
            if (tam_texto(senha_digitada) == 0) {
                break;
            }
        }

        tentativas--;
        escrever_texto(SAIDA, "Senha incorreta. Tentativas: ");
        char t[4];
        int_para_texto(tentativas, t, sizeof t);
        escrever_texto(SAIDA, t);
        escrever_texto(SAIDA, "\n");
    }
    if (tentativas == 0) {
        escrever_texto(SAIDA, "Muitas tentativas falhadas.\n");
        return 1;
    }

    /* Executa o comando como o usuário alvo. */
    char *novo_argv[32];
    int k = 0;
    for (int i = argi; i < argc && k < 31; i++) {
        novo_argv[k++] = argv[i];
    }
    novo_argv[k] = NULL;
    int pid = executar(novo_argv[0], novo_argv);
    if (pid < 0) {
        escrever_texto(ERROS, "sudo: não consegui executar ");
        escrever_texto(ERROS, novo_argv[0]);
        escrever_texto(ERROS, "\n");
        return 1;
    }
    return 0;
}
