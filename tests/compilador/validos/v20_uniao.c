/* union: os campos dividem a mesma memória. */
#include "sagui.h"

typedef union {
    unsigned inteiro;
    unsigned char bytes[4];
    struct { unsigned short baixo, alto; } metades;
} palavra_t;

enum { T_INT, T_TEXTO, T_PONTO };

typedef struct {
    int tipo;
    union {
        int numero;
        const char *texto;
        struct { short x, y; } ponto;
    } v;
} valor_t;

static void mostra(const valor_t *v)
{
    switch (v->tipo) {
    case T_INT: imprimir("inteiro %d\n", v->v.numero); break;
    case T_TEXTO: imprimir("texto %s\n", v->v.texto); break;
    case T_PONTO: imprimir("ponto %d,%d\n", v->v.ponto.x, v->v.ponto.y); break;
    }
}

int principal(int argc, char **argv)
{
    palavra_t p;
    valor_t lista[3];
    (void)argc; (void)argv;
    p.inteiro = 0x11223344;
    imprimir("%x %x %x %x\n", p.bytes[0], p.bytes[1], p.bytes[2], p.bytes[3]);
    imprimir("%x %x\n", p.metades.baixo, p.metades.alto);
    p.bytes[3] = 0xFF;
    p.metades.baixo = 0;
    imprimir("%x %d\n", p.inteiro, (int)sizeof p);
    lista[0].tipo = T_INT; lista[0].v.numero = -9;
    lista[1].tipo = T_TEXTO; lista[1].v.texto = "banana";
    lista[2].tipo = T_PONTO; lista[2].v.ponto.x = 3; lista[2].v.ponto.y = -4;
    for (int i = 0; i < 3; i++) mostra(&lista[i]);
    imprimir("%d\n", (int)sizeof(valor_t));
    palavra_t q = { 258 };
    imprimir("%d %d\n", q.bytes[0], q.bytes[1]);
    return 0;
}
