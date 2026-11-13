/* O crivo de Eratóstenes, com um bit por número. */
#include "sagui.h"

#define MAX 10000

static unsigned char riscado[MAX / 8 + 1];

static void risca(unsigned n) { riscado[n >> 3] |= (unsigned char)(1 << (n & 7)); }
static int esta(unsigned n) { return (riscado[n >> 3] >> (n & 7)) & 1; }

static bool primo_devagar(unsigned n)
{
    if (n < 2) return false;
    for (unsigned d = 2; d * d <= n; d++) if (n % d == 0) return false;
    return true;
}

int principal(int argc, char **argv)
{
    unsigned qtd = 0, ultimo = 0, soma = 0;
    int iguais = 1;
    (void)argc; (void)argv;
    risca(0); risca(1);
    for (unsigned i = 2; i * i <= MAX; i++)
        if (!esta(i))
            for (unsigned j = i * i; j <= MAX; j += i) risca(j);
    for (unsigned i = 0; i <= MAX; i++) {
        if (!esta(i)) { qtd++; ultimo = i; soma += i; if (qtd <= 15) imprimir("%u ", i); }
        if (i < 2000 && (bool)!esta(i) != primo_devagar(i)) iguais = 0;
    }
    imprimir("\n%u primos ate %d; o maior e %u; soma %u; confere %d\n", qtd, MAX, ultimo, soma, iguais);
    unsigned gemeos = 0;
    for (unsigned i = 3; i + 2 <= MAX; i += 2) if (!esta(i) && !esta(i + 2)) gemeos++;
    imprimir("%u pares de gemeos\n", gemeos);
    return 0;
}
