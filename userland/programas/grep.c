/* grep - busca um padrão em arquivos ou na entrada padrão (SaguiOS).
 *
 * Uso:
 *   grep [-n] <padrao>           lê da entrada padrão (stdin)
 *   grep [-n] <padrao> <arquivo> lê do arquivo
 *
 * -n : mostra o número de cada linha encontrada
 *
 * Imprime todas as linhas que contêm o padrão (case-sensitive).
 * Usa apenas syscalls int 0x80 + libsagui. Sem libc.
 *
 * Exemplos:
 *   sagui:/$ grep Sagui leia-me.txt
 *   sagui:/$ cat leia-me.txt | grep versao
 *   sagui:/$ grep -n erro docs/erros.md
 */

#include "sagui.h"

#define LINHA_MAX 512

/* Retorna 1 se 'palha' contém 'agulha', 0 caso contrário. Case-sensitive. */
static int contem(const char *palha, const char *agulha)
{
    if (!*agulha) return 1;
    for (; *palha; palha++) {
        const char *a = palha, *b = agulha;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

/* Converte inteiro para string decimal (sem sprintf). */
static void texto_int_dec(int v, char *buf, unsigned max)
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

/* Escreve uma string completa no fd (garante que todos os bytes saem). */
static void escrever_tudo_fd(int fd, const char *s)
{
    unsigned n = tam_texto(s);
    escrever_tudo(fd, s, n);
}

/* Processa um descritor de arquivo, linha por linha. */
static void processar(int fd, const char *padrao, bool numerar)
{
    char linha[LINHA_MAX];
    int num = 0;
    while (1) {
        int n = ler_linha(fd, linha, sizeof linha);
        if (n < 0) break;   /* fim do arquivo */
        num++;
        if (contem(linha, padrao)) {
            if (numerar) {
                char buf[16];
                texto_int_dec(num, buf, sizeof buf);
                escrever_tudo_fd(SAIDA, buf);
                escrever_tudo_fd(SAIDA, ": ");
            }
            escrever_tudo_fd(SAIDA, linha);
            escrever_tudo_fd(SAIDA, "\n");
        }
    }
}

int principal(int argc, char **argv)
{
    if (argc < 2) {
        escrever_tudo_fd(ERROS, "uso: grep [-n] <padrao> [arquivo]\n");
        return 1;
    }

    bool numerar = false;
    int arg_padrao = 1;

    if (compara_texto(argv[1], "-n") == 0) {
        numerar = true;
        arg_padrao = 2;
        if (argc < 3) {
            escrever_tudo_fd(ERROS, "uso: grep [-n] <padrao> [arquivo]\n");
            return 1;
        }
    }

    const char *padrao = argv[arg_padrao];

    if (argc > arg_padrao + 1) {
        const char *caminho = argv[arg_padrao + 1];
        int fd = abrir(caminho, ABRIR_LER);
        if (fd < 0) {
            escrever_tudo_fd(ERROS, "grep: nao consigo abrir ");
            escrever_tudo_fd(ERROS, caminho);
            escrever_tudo_fd(ERROS, "\n");
            return 1;
        }
        processar(fd, padrao, numerar);
        fechar(fd);
    } else {
        /* Sem arquivo: lê da entrada padrão. */
        processar(ENTRADA, padrao, numerar);
    }

    return 0;
}
