/* (tipo){ ... }: um valor montado na hora, sem dar nome a uma variável. */
#include "sagui.h"

typedef struct { int x, y; } ponto_t;
typedef struct { ponto_t canto; int l, a; const char *nome; } ret_t;

static int area(ret_t r) { return r.l * r.a; }
static int soma(const int *v, int n) { int s = 0; while (n--) s += *v++; return s; }
static void anda(ponto_t *p, ponto_t passo) { p->x += passo.x; p->y += passo.y; }

static ret_t lista[4];
static int qtd;
static void novo(const char *nome, int x, int y, int l, int a)
{
    lista[qtd++] = (ret_t){ { x, y }, l, a, nome };
}

int principal(int argc, char **argv)
{
    ponto_t p = { 0, 0 };
    (void)argc; (void)argv;
    imprimir("%d\n", area((ret_t){ { 1, 2 }, 10, 20, "sala" }));
    imprimir("%d\n", soma((int[]){ 1, 2, 3, 4 }, 4));
    anda(&p, (ponto_t){ 3, -1 });
    anda(&p, (ponto_t){ .y = 5 });
    imprimir("%d %d\n", p.x, p.y);
    p = (ponto_t){ 7, 8 };
    imprimir("%d %d %d\n", p.x, p.y, ((ponto_t){ 40, 2 }).x + (ponto_t){ 40, 2 }.y);
    novo("porta", 0, 0, 2, 7);
    novo("janela", 5, 3, 4, 4);
    for (int i = 0; i < qtd; i++) imprimir("%s %d,%d %dx%d = %d\n", lista[i].nome, lista[i].canto.x, lista[i].canto.y, lista[i].l, lista[i].a, area(lista[i]));
    int *v = (int[3]){ 10, 20 };
    v[2] = v[0] + v[1];
    imprimir("%d\n", v[2]);
    const char *nome = (char[]){ 'm', 'i', 'c', 'o', 0 };
    imprimir("%s\n", nome);
    return 0;
}
