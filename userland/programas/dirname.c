/* dirname (pasta-de) - mostra o diretório de um caminho, tirando o último
 * componente (o nome do arquivo ou da pasta).
 *
 *   pasta-de /a/b/c.txt      /a/b
 *   pasta-de /a/b/           /a
 *   pasta-de c.txt           .
 *   pasta-de /               /
 *   pasta-de a/b             a
 *
 * É o inverso de basename: dado um caminho, mostra a pasta que o contém.
 * Se não houver barra, o diretório é "." (a pasta atual).
 *
 * Não há chamada de sistema para isso: é só manipulação de texto, do zero. */
#include "sagui.h"

static int uso(void)
{
    imprimir_em(ERROS, "uso: pasta-de CAMINHO\n");
    return 1;
}

int principal(int argc, char **argv)
{
    if (argc != 2) return uso();

    const char *c = argv[1];
    size_t n = tam_texto(c);

    /* Tira barras no fim, exceto se o caminho for só "/". */
    while (n > 1 && c[n - 1] == '/') n--;

    /* Procura a última barra nos primeiros n caracteres. */
    size_t ultima_pos = (size_t)-1;
    for (size_t i = 0; i < n; i++)
        if (c[i] == '/') ultima_pos = i;

    if (ultima_pos == (size_t)-1) {
        /* Não tem barra: o diretório é "." (pasta atual). */
        escrever_texto(SAIDA, ".\n");
        return 0;
    }

    if (ultima_pos == 0) {
        /* A única barra está no começo: raiz "/". */
        escrever_texto(SAIDA, "/\n");
        return 0;
    }

    /* Escreve do início até antes da última barra. */
    escrever_tudo(SAIDA, c, (unsigned)ultima_pos);
    escrever_texto(SAIDA, "\n");
    return 0;
}
