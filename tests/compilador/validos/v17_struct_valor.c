/* structs passados e devolvidos por valor (cópias inteiras). */
#include "sagui.h"

typedef struct { int x, y; } ponto_t;
typedef struct { ponto_t a, b; char nome[12]; } caixa_t;
typedef struct { long long grande; char c; } misto_t;

static ponto_t ponto(int x, int y) { ponto_t p; p.x = x; p.y = y; return p; }
static ponto_t soma(ponto_t a, ponto_t b) { return ponto(a.x + b.x, a.y + b.y); }

static caixa_t caixa(const char *nome, ponto_t a, ponto_t b)
{
    caixa_t c;
    c.a = a;
    c.b = b;
    copia_texto(c.nome, nome, sizeof c.nome);
    return c;
}

static int area(caixa_t c) { c.a.x = 0; return (c.b.x - 0) * (c.b.y - c.a.y); }   /* mexe só na cópia */

static misto_t misto(int n) { misto_t m; m.grande = n; m.c = 'm'; return m; }

int principal(int argc, char **argv)
{
    ponto_t p = soma(ponto(1, 2), ponto(10, 20));
    (void)argc; (void)argv;
    imprimir("%d %d\n", p.x, p.y);
    imprimir("%d\n", soma(p, p).y);
    caixa_t c = caixa("sala", ponto(2, 3), soma(p, ponto(1, 1)));
    imprimir("%s %d %d %d %d\n", c.nome, c.a.x, c.a.y, c.b.x, c.b.y);
    imprimir("%d %d\n", area(c), c.a.x);
    caixa_t d = c;
    d.nome[0] = 'b';
    imprimir("%s %s\n", c.nome, d.nome);
    misto_t m = misto(77);
    imprimir("%d %c %d\n", (int)m.grande, m.c, (int)sizeof(misto_t));
    ponto_t v[3];
    for (int i = 0; i < 3; i++) v[i] = ponto(i, i * i);
    v[0] = v[2];
    imprimir("%d %d\n", v[0].x, v[0].y);
    return 0;
}
