/* Funções que chamam a si mesmas. */
#include "sagui.h"

static unsigned fatorial(unsigned n) { return n <= 1 ? 1 : n * fatorial(n - 1); }
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static int potencia(int b, int e) { if (e == 0) return 1; int m = potencia(b, e / 2); return e % 2 ? m * m * b : m * m; }
static int ackermann(int m, int n) { return m == 0 ? n + 1 : n == 0 ? ackermann(m - 1, 1) : ackermann(m - 1, ackermann(m, n - 1)); }

static int movimentos;
static void hanoi(int n, char de, char para, char apoio)
{
    if (n == 0) return;
    hanoi(n - 1, de, apoio, para);
    movimentos++;
    if (n == 3) imprimir("disco 3: %c -> %c\n", de, para);
    hanoi(n - 1, apoio, para, de);
}

static bool par(int n);
static bool impar(int n) { return n == 0 ? false : par(n - 1); }
static bool par(int n) { return n == 0 ? true : impar(n - 1); }

static void binario(unsigned n) { if (n > 1) binario(n / 2); imprimir("%d", n % 2); }

static int soma_digitos(int n) { return n < 10 ? n : n % 10 + soma_digitos(n / 10); }

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    imprimir("%u %u\n", fatorial(5), fatorial(12));
    for (int i = 0; i < 15; i++) imprimir("%d ", fib(i));
    imprimir("\n%d %d\n", fib(24), potencia(3, 13));
    imprimir("%d %d\n", ackermann(2, 3), ackermann(3, 3));
    hanoi(5, 'A', 'C', 'B');
    imprimir("%d movimentos\n", movimentos);
    imprimir("%d %d\n", par(10), par(7));
    binario(1234567); imprimir("\n");
    imprimir("%d\n", soma_digitos(987654321));
    return 0;
}
