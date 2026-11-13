/* structs por ponteiro: uma lista ligada feita com aloca e libera. */
#include "sagui.h"

typedef struct no {
    int valor;
    struct no *prox;
} no_t;

static no_t *insere_ordenado(no_t *lista, int valor)
{
    no_t *novo = aloca(sizeof *novo), **onde = &lista;
    novo->valor = valor;
    while (*onde && (*onde)->valor < valor) onde = &(*onde)->prox;
    novo->prox = *onde;
    *onde = novo;
    return lista;
}

static no_t *inverte(no_t *lista)
{
    no_t *nova = NULL;
    while (lista) {
        no_t *prox = lista->prox;
        lista->prox = nova;
        nova = lista;
        lista = prox;
    }
    return nova;
}

static void mostra(const no_t *l)
{
    for (; l; l = l->prox) imprimir("%d%s", l->valor, l->prox ? " -> " : "\n");
}

int principal(int argc, char **argv)
{
    int valores[] = { 42, 7, 19, 3, 88, 19 };
    no_t *lista = NULL;
    int soma = 0, n = 0;
    (void)argc; (void)argv;
    for (unsigned i = 0; i < sizeof valores / sizeof valores[0]; i++) lista = insere_ordenado(lista, valores[i]);
    mostra(lista);
    lista = inverte(lista);
    mostra(lista);
    for (no_t *p = lista; p; p = p->prox) { soma += p->valor; n++; }
    imprimir("%d itens, soma %d\n", n, soma);
    while (lista) { no_t *prox = lista->prox; libera(lista); lista = prox; }
    imprimir("vazia: %d\n", lista == NULL);
    return 0;
}
