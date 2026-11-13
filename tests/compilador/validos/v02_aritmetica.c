/* Contas com inteiros com sinal e a precedência dos operadores. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int a = 17, b = 5, c = -3;
    (void)argc; (void)argv;
    imprimir("%d %d %d %d %d\n", a + b, a - b, a * b, a / b, a % b);
    imprimir("%d %d %d\n", a + b * c, (a + b) * c, a - b - c);
    imprimir("%d %d %d\n", -a, -(-a), +a);
    imprimir("%d %d\n", a * b + c * b - a / b, 2 + 3 * 4 - 5 * (6 - 7));
    imprimir("%d\n", 2147483647 + c);
    imprimir("%d %d\n", c * c * c, a * a * a * a);
    int soma = 0;
    for (int i = 1; i <= 100; i++) soma += i;
    imprimir("soma de 1 a 100: %d\n", soma);
    int x = 1000000;
    imprimir("%d %d\n", x * 2000, x * x);      /* estoura e dá a volta, como no processador */
    return 0;
}
