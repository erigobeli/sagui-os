/* longo64.c - contas com inteiros de 64 bits para os programas feitos pelo
 * compilador do SaguiOS (userland/compilador).
 *
 * Num x86 de 32 bits um "long long" ocupa dois registradores. Somar,
 * subtrair e comparar o compilador faz direto; multiplicar, dividir e
 * deslocar ele deixa para estas rotinas, para o gerador de código ficar
 * pequeno. A divisão é a "longa", um bit do quociente por vez, igual à da
 * Calculadora: o SaguiOS não usa a biblioteca do GCC que faria isso. */
#include "sagui.h"

long long __sagui_mul64(long long a, long long b) { return a * b; }
long long __sagui_desle64(long long a, int n) { return (long long)((unsigned long long)a << (n & 63)); }
long long __sagui_desld64(long long a, int n) { return a >> (n & 63); }
unsigned long long __sagui_deslu64(unsigned long long a, int n) { return a >> (n & 63); }

static unsigned long long dividir(unsigned long long a, unsigned long long b, unsigned long long *resto)
{
    unsigned long long q = 0, r = 0;
    if (b == 0) { volatile int zero = 0; *resto = 0; return (unsigned)(1 / zero); }   /* a mesma falha de uma divisão comum por zero */
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { r -= b; q |= 1ULL << i; }
    }
    *resto = r;
    return q;
}

unsigned long long __sagui_divu64(unsigned long long a, unsigned long long b)
{
    unsigned long long r;
    return dividir(a, b, &r);
}

unsigned long long __sagui_restou64(unsigned long long a, unsigned long long b)
{
    unsigned long long r;
    dividir(a, b, &r);
    return r;
}

long long __sagui_div64(long long a, long long b)
{
    unsigned long long r, q = dividir(a < 0 ? 0 - (unsigned long long)a : (unsigned long long)a,
                                      b < 0 ? 0 - (unsigned long long)b : (unsigned long long)b, &r);
    return (a < 0) != (b < 0) ? -(long long)q : (long long)q;
}

long long __sagui_resto64(long long a, long long b)
{
    unsigned long long r;
    dividir(a < 0 ? 0 - (unsigned long long)a : (unsigned long long)a,
            b < 0 ? 0 - (unsigned long long)b : (unsigned long long)b, &r);
    return a < 0 ? -(long long)r : (long long)r;     /* o resto acompanha o sinal do dividendo */
}

/* Nomes que o GCC chama ao usar "long long" sem libgcc. */
long long __muldi3(long long a, long long b) { return __sagui_mul64(a, b); }
long long __divdi3(long long a, long long b) { return __sagui_div64(a, b); }
