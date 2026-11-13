/* Uma pilha e uma fila em vetores, e uma calculadora de notação polonesa reversa. */
#include "sagui.h"

#define CAP 16

typedef struct { int itens[CAP]; int topo; } pilha_t;
typedef struct { int itens[CAP]; int ini, qtd; } fila_t;

static bool empilha(pilha_t *p, int v) { if (p->topo == CAP) return false; p->itens[p->topo++] = v; return true; }
static bool desempilha(pilha_t *p, int *v) { if (!p->topo) return false; *v = p->itens[--p->topo]; return true; }

static bool entra(fila_t *f, int v)
{
    if (f->qtd == CAP) return false;
    f->itens[(f->ini + f->qtd++) % CAP] = v;
    return true;
}

static bool sai(fila_t *f, int *v)
{
    if (!f->qtd) return false;
    *v = f->itens[f->ini];
    f->ini = (f->ini + 1) % CAP;
    f->qtd--;
    return true;
}

/* "3 4 + 2 *" -> 14 */
static bool rpn(const char *s, int *resultado)
{
    pilha_t p = { { 0 }, 0 };
    while (*s) {
        if (*s == ' ') { s++; continue; }
        if (*s >= '0' && *s <= '9') {
            int n = 0;
            while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
            if (!empilha(&p, n)) return false;
            continue;
        }
        int a, b;
        if (!desempilha(&p, &b) || !desempilha(&p, &a)) return false;
        switch (*s++) {
        case '+': empilha(&p, a + b); break;
        case '-': empilha(&p, a - b); break;
        case '*': empilha(&p, a * b); break;
        case '/': if (!b) return false; empilha(&p, a / b); break;
        default: return false;
        }
    }
    return desempilha(&p, resultado) && p.topo == 0;
}

int principal(int argc, char **argv)
{
    pilha_t p = { .topo = 0 };
    fila_t f = { { 0 }, 0, 0 };
    int v, r;
    const char *contas[] = { "3 4 + 2 *", "100 7 / 3 -", "1 2 3 4 5 + + + +", "2 +", "5 0 /", "12 34" };
    (void)argc; (void)argv;
    for (int i = 1; i <= 5; i++) empilha(&p, i * i);
    while (desempilha(&p, &v)) imprimir("%d ", v);
    imprimir("\n");
    for (int i = 0; i < 20; i++) if (!entra(&f, i)) { imprimir("cheia em %d\n", i); break; }
    for (int i = 0; i < 10; i++) sai(&f, &v);
    for (int i = 100; i < 105; i++) entra(&f, i);
    while (sai(&f, &v)) imprimir("%d ", v);
    imprimir("\n");
    for (unsigned i = 0; i < sizeof contas / sizeof *contas; i++) {
        if (rpn(contas[i], &r)) imprimir("%s = %d\n", contas[i], r);
        else imprimir("%s: conta errada\n", contas[i]);
    }
    return 0;
}
