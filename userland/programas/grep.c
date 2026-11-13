/* grep - busca um padrão em arquivos ou na entrada padrão (SaguiOS).
 *
 * Uso:
 *   grep <padrao>           lê da entrada padrão (stdin)
 *   grep <padrao> <arquivo> lê do arquivo
 *
 * Imprime todas as linhas que contêm o padrão.
 * Usa apenas syscalls int 0x80 + libsagui. Sem libc.
 *
 * Exemplo:
 *   sagui:/$ grep Sagui leia-me.txt
 *   sagui:/$ cat leia-me.txt | grep versao
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

/* Processa um descritor de arquivo, linha por linha. */
static void processar(int fd, const char *padrao)
{
    char linha[LINHA_MAX];
    while (1) {
        int n = ler_linha(fd, linha, sizeof linha);
        if (n < 0) break;   /* fim do arquivo */
        if (contem(linha, padrao)) {
            escrever_texto(SAIDA, linha);
            escrever_texto(SAIDA, "\n");
        }
    }
}

int principal(int argc, char **argv)
{
    if (argc < 2) {
        escrever_texto(ERROS, "uso: grep <padrao> [arquivo]\n");
        return 1;
    }

    const char *padrao = argv[1];

    if (argc >= 3) {
        const char *caminho = argv[2];
        int fd = abrir(caminho, ABRIR_LER);
        if (fd < 0) {
            escrever_texto(ERROS, "grep: nao consigo abrir ");
            escrever_texto(ERROS, caminho);
            escrever_texto(ERROS, "\n");
            return 1;
        }
        processar(fd, padrao);
        fechar(fd);
    } else {
        processar(ENTRADA, padrao);
    }

    return 0;
}
