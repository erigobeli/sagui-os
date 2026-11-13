/* long long: inteiros de 64 bits (duas metades de 32). */
#include "sagui.h"

static void mostra(long long v)
{
    char d[24];
    int n = 0;
    unsigned long long u = v < 0 ? 0 - (unsigned long long)v : (unsigned long long)v;
    if (v < 0) imprimir("-");
    do { d[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
    while (n) imprimir("%c", d[--n]);
}

static long long fatorial(int n) { return n <= 1 ? 1 : n * fatorial(n - 1); }
static unsigned long long potencia2(int n) { return 1ULL << n; }

int principal(int argc, char **argv)
{
    long long a = 5000000000LL, b = -3;
    unsigned long long u = 0xFFFFFFFFFFFFFFFFULL;
    int32_t pequeno = -7;
    (void)argc; (void)argv;
    mostra(a + a); imprimir(" "); mostra(a - 6000000000LL); imprimir(" "); mostra(a * b); imprimir("\n");
    mostra(a / 7); imprimir(" "); mostra(a % 7); imprimir(" "); mostra(-a / 7); imprimir(" "); mostra(-a % 7); imprimir("\n");
    mostra(fatorial(20)); imprimir("\n");
    mostra((long long)potencia2(40)); imprimir(" "); mostra(a >> 3); imprimir(" "); mostra(b >> 1); imprimir(" "); mostra(a << 2); imprimir("\n");
    imprimir("%d %d %d %d\n", a > b, a < b, a == 5000000000LL, b != -3);
    imprimir("%d %d\n", u > (unsigned long long)a, (long long)u < a);
    imprimir("%x %x\n", (unsigned)(a >> 32), (unsigned)a);
    mostra(pequeno); imprimir(" "); mostra((long long)pequeno * 1000000000); imprimir(" "); mostra((unsigned)pequeno); imprimir("\n");
    long long acumulado = 0;
    for (int i = 1; i <= 100000; i++) acumulado += i;
    mostra(acumulado); imprimir("\n");
    a ^= 0xFFFF; a |= 1LL << 40; a &= ~1LL;
    mostra(a); imprimir(" "); mostra(~a); imprimir(" "); mostra(-a); imprimir("\n");
    uint64_t m = 1;
    for (int i = 0; i < 20; i++) m *= 7;
    mostra((long long)(m >> 8)); imprimir(" %d %d\n", (int)(m % 1000), !m);
    long long v[3] = { 1, -1, 123456789012LL };
    long long *p = v + 2;
    mostra(*p + v[1]); imprimir(" %d\n", (int)sizeof v);
    a = 10; a++; a += b; a <<= 33; a /= 3;
    mostra(a); imprimir(" %d\n", a ? 1 : 0);
    return 0;
}
