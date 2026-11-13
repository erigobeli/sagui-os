/* #include de um cabeçalho próprio (na mesma pasta), incluído duas vezes. */
#include "v37_cabecalho.h"
#include "v37_cabecalho.h"
#include <sagui.h>

int total(const item_t *itens, int n)
{
    int t = 0;
    for (int i = 0; i < n; i++) t += itens[i].preco;
    return t;
}

int principal(int argc, char **argv)
{
    item_t lista[MAX_ITENS] = { { "banana", 350 }, { "mamao", 799 }, { "caju", 1250 }, { "coco", 601 } };
    int t = total(lista, MAX_ITENS);
    (void)argc; (void)argv;
    imprimir("%d itens, R$ %d,%02d\n", MAX_ITENS, reais(t), t % 100);
    return 0;
}
