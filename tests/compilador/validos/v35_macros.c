/* #define: constantes, macros com parâmetros, # (vira texto) e ## (junta). */
#include "sagui.h"

#define TAMANHO 16
#define DOBRO(x) ((x) * 2)
#define MAIOR(a, b) ((a) > (b) ? (a) : (b))
#define QUADRADO(x) ((x) * (x))
#define TEXTO(x) #x
#define JUNTA(a, b) a ## b
#define MOSTRA(v) imprimir(#v " = %d\n", v)
#define TROCA(tipo, a, b) do { tipo t_ = a; a = b; b = t_; } while (0)
#define REPETE(n) for (int i_ = 0; i_ < (n); i_++)
#define AREA TAMANHO * TAMANHO
#define VAZIA
#define REGISTRO(fmt, ...) imprimir("[" fmt "]\n", __VA_ARGS__)
#define ELEMENTOS(v) (sizeof(v) / sizeof((v)[0]))
#define LIMITE \
    (TAMANHO + \
     4)

static int valor12 = 12;

int principal(int argc, char **argv)
{
    int a = 3, b = 8;
    int v[] = { 1, 2, 3, 4, 5 };
    (void)argc; (void)argv;
    imprimir("%d %d %d %d\n", TAMANHO, DOBRO(a + 1), MAIOR(a, b), QUADRADO(a + b));
    imprimir("%s %s\n", TEXTO(a + b), TEXTO(sagui));
    imprimir("%d %d\n", JUNTA(valor, 12), JUNTA(1, 0) + 1);
    MOSTRA(a * b);
    MOSTRA(AREA);
    TROCA(int, a, b);
    imprimir("%d %d\n", a, b);
    REPETE(3) imprimir("oi ");
    imprimir("\n") VAZIA;
    REGISTRO("%d e %s", 7, "sete");
    imprimir("%d %d\n", (int)ELEMENTOS(v), LIMITE);
#undef TAMANHO
#define TAMANHO 2
    imprimir("%d %d\n", TAMANHO, AREA);
    imprimir("%d %s\n", __LINE__, __FILE__[0] ? "arquivo" : "");
    imprimir("%d\n", MAIOR(DOBRO(a), QUADRADO(MAIOR(b, 2))));
    return 0;
}
