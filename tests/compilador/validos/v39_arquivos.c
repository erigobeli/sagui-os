/* Arquivos: criar, escrever, ler de volta, posicionar, acrescentar e apagar. */
#include "sagui.h"

#define CAMINHO "/tmp/cc-teste.txt"

int principal(int argc, char **argv)
{
    char buf[64];
    int fd, n;
    (void)argc; (void)argv;
    fd = abrir(CAMINHO, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) { imprimir("nao criou\n"); return 1; }
    for (int i = 1; i <= 3; i++) imprimir_em(fd, "linha %d\n", i);
    escrever(fd, "fim", 3);
    fechar(fd);

    fd = abrir(CAMINHO, ABRIR_LER);
    n = ler(fd, buf, sizeof buf - 1);
    buf[n] = 0;
    imprimir("%d bytes:\n%s\n", n, buf);
    posicionar(fd, 6, POS_INICIO);
    n = ler(fd, buf, 1);
    imprimir("%d %c\n", n, buf[0]);
    imprimir("%d\n", posicionar(fd, 0, POS_FIM));
    imprimir("%d\n", ler(fd, buf, 10));          /* no fim: 0 */
    fechar(fd);

    fd = abrir(CAMINHO, ABRIR_ESCREVER | ABRIR_ACRESCENTAR);
    escrever_texto(fd, "!\n");
    fechar(fd);
    fd = abrir(CAMINHO, ABRIR_LER);
    int linhas = 0, total = 0;
    while ((n = ler(fd, buf, 7)) > 0) {
        total += n;
        for (int i = 0; i < n; i++) if (buf[i] == '\n') linhas++;
    }
    fechar(fd);
    imprimir("%d linhas, %d bytes\n", linhas, total);
    imprimir("%d\n", apagar(CAMINHO));
    imprimir("%d\n", abrir(CAMINHO, ABRIR_LER) < 0);
    return 0;
}
