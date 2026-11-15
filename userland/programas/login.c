/* login - programa de login do SaguiOS.
 *
 * Pede o nome de usuário e a senha, verifica em /etc/passwd e /etc/shadow,
 * grava o usuário autenticado em /tmp/usuario_atual e inicia o sagsh.
 *
 * Se não houver /etc/shadow, aceita qualquer usuário (modo sem senha).
 * O usuário "live" não precisa de senha (enter direto).
 *
 * Uso: login
 *      login <usuario>   (pula a pergunta de usuário, só pede senha)
 *
 * Sem libc, apenas syscalls int 0x80 e libsagui. */

#include "sagui.h"

#define MAX_LINHA 256
#define MAX_USUARIO 32
#define MAX_SENHA 32

/* Lê uma linha do stdin (fd 0), sem eco de senha quando 'senha' é true. */
static int ler_linha_sem_eco(char *buf, unsigned max, bool senha)
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
            if (n > 0) {
                n--;
                if (!senha) {
                    char apaga[3] = "\b \b";
                    escrever_texto(SAIDA, apaga);
                }
            }
            continue;
        }
        if (n + 1 < max) {
            buf[n++] = c;
            if (!senha) {
                char cx[2] = { c, 0 };
                escrever_texto(SAIDA, cx);
            } else {
                escrever_texto(SAIDA, "*");
            }
        }
    }
    buf[n] = 0;
    if (!senha) escrever_texto(SAIDA, "\n");
    return (int)n;
}

/* Remove espaços e nova-linha do final. */
static void limpar_final(char *s)
{
    int n = (int)tam_texto(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\n' || s[n-1] == '\r')) {
        s[--n] = 0;
    }
}

/* Converte inteiro para string decimal (sem sprintf). */
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

/* Lê /etc/passwd e procura o usuário. Devolve 0 se achou. */
static int encontrar_usuario(const char *usuario, char *linha, unsigned max)
{
    int fd = abrir("/etc/passwd", ABRIR_LER);
    if (fd < 0) return -1;
    char buf[MAX_LINHA];
    int achou = -1;
    while (1) {
        int n = ler_linha(fd, buf, sizeof buf);
        if (n < 0) break;
        char nome[MAX_USUARIO];
        int i;
        for (i = 0; buf[i] && buf[i] != ':' && i < MAX_USUARIO - 1; i++) {
            nome[i] = buf[i];
        }
        nome[i] = 0;
        if (compara_texto(nome, usuario) == 0) {
            achou = 0;
            copia_texto(linha, buf, max);
            break;
        }
    }
    fechar(fd);
    return achou;
}

/* Lê uma linha de um descritor de arquivo (nome diferente da ler_linha da libsagui). */
static int ler_linha_login(int fd, char *buf, unsigned max)
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
    if (fd < 0) return -1;   /* sem shadow: modo sem senha */
    char buf[MAX_LINHA];
    while (1) {
        int n = ler_linha_login(fd, buf, sizeof buf);
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

/* Verifica se o usuário é "live" (não precisa de senha). */
static bool usuario_nao_precisa_senha(const char *usuario)
{
    return compara_texto(usuario, "live") == 0;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    char usuario[MAX_USUARIO];

    /* Se passou usuário como argumento, usa direto. */
    if (argc >= 2) {
        copia_texto(usuario, argv[1], sizeof usuario);
    } else {
        escrever_texto(SAIDA, "SaguiOS - Login\n");
        escrever_texto(SAIDA, "===============\n\n");
        for (;;) {
            escrever_texto(SAIDA, "Usuario: ");
            int n = ler_linha_sem_eco(usuario, sizeof usuario, false);
            if (n < 0) {
                escrever_texto(SAIDA, "\nSaindo...\n");
                return 1;
            }
            limpar_final(usuario);
            if (tam_texto(usuario) == 0) {
                escrever_texto(SAIDA, "Digite um usuario.\n");
                continue;
            }
            break;
        }
    }

    /* Verifica se o usuário existe. */
    char linha_passwd[MAX_LINHA];
    if (encontrar_usuario(usuario, linha_passwd, sizeof linha_passwd) < 0) {
        escrever_texto(SAIDA, "Usuario nao encontrado: ");
        escrever_texto(SAIDA, usuario);
        escrever_texto(SAIDA, "\n");
        return 1;
    }

    /* Pede a senha (se não for "live"). */
    if (!usuario_nao_precisa_senha(usuario)) {
        char senha_digitada[MAX_SENHA];
        char senha_esperada[MAX_SENHA];
        int tentativas = 3;
        while (tentativas > 0) {
            escrever_texto(SAIDA, "Senha: ");
            int n = ler_linha_sem_eco(senha_digitada, sizeof senha_digitada, true);
            if (n < 0) {
                escrever_texto(SAIDA, "\nSaindo...\n");
                return 1;
            }
            limpar_final(senha_digitada);

            if (encontrar_senha(usuario, senha_esperada, sizeof senha_esperada) == 0) {
                if (compara_texto(senha_digitada, senha_esperada) == 0) {
                    break;   /* senha correta */
                }
            } else {
                if (tam_texto(senha_digitada) == 0) {
                    break;
                }
            }

            tentativas--;
            escrever_texto(SAIDA, "\nSenha incorreta. Tentativas restantes: ");
            char t[4];
            int_para_texto(tentativas, t, sizeof t);
            escrever_texto(SAIDA, t);
            escrever_texto(SAIDA, "\n");
        }
        if (tentativas == 0) {
            escrever_texto(SAIDA, "\nMuitas tentativas falhadas.\n");
            return 1;
        }
    }

    /* Login bem-sucedido. */
    escrever_texto(SAIDA, "\nLogin bem-sucedido. Bem-vindo, ");
    escrever_texto(SAIDA, usuario);
    escrever_texto(SAIDA, "!\n");

    /* Grava o usuário atual para o sagsh saber quem está logado. */
    int fd = abrir("/tmp/usuario_atual", ABRIR_CRIAR | ABRIR_TRUNCAR | ABRIR_ESCREVER);
    if (fd >= 0) {
        escrever_texto(fd, usuario);
        fechar(fd);
    }

    /* Inicia o shell passando o usuário como argumento e sai. */
    char *argv_sagsh[] = { "sagsh", usuario, NULL };
    executar("/bin/sagsh", argv_sagsh);
    sair(0);
    return 0;
}
