/* numero_base.c - Number.prototype.toString(base) do JavaScript.
 *
 * CÓDIGO DE TERCEIROS (D041, D043). Este é o algoritmo DoubleToRadixCString
 * do V8 (Copyright 2006-2011, the V8 project authors; licença BSD de 3
 * cláusulas, no LICENSE desta pasta), reproduzido de memória em C pela IA que
 * ajudou Robson de Lima Barbosa no pull request #6 do SaguiOS. Por isso é
 * tratado como derivado do V8. Veja ORIGEM.md para o que mudou.
 *
 * A ideia: a parte fracionária é escrita dígito a dígito só até onde o double
 * distingue o número do vizinho mais próximo ("delta" é metade dessa
 * distância), arredondando o último dígito; a parte inteira, de baixo para
 * cima, com zeros onde o double já não guarda os dígitos de baixo. */
#include "numero_base.h"

typedef unsigned long long u64;
typedef union { double d; u64 b; } dbits;
static u64 d_bits(double d) { dbits x; x.d = d; return x.b; }
static double d_de(u64 b) { dbits x; x.b = b; return x.d; }

int v8_numero_em_base(double v, int b, char *buf, double (*piso)(double), double (*resto)(double, double))
{
    static const char cs[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    int pos = 1100, ini = pos;
    int neg = v < 0; if (neg) v = -v;
    double inteiro = piso(v), fr = v - inteiro;
    double delta = 0.5 * (d_de(d_bits(v) + 1) - v);
    double minimo = d_de(1);
    if (delta < minimo) delta = minimo;
    int fp = pos;
    if (fr >= delta) {
        buf[fp++] = '.';
        do {
            fr *= b; delta *= b;
            int dg = (int)fr;
            buf[fp++] = cs[dg];
            fr -= dg;
            if (fr > 0.5 || (fr == 0.5 && (dg & 1))) {
                if (fr + delta > 1) {
                    /* arredonda para cima e propaga */
                    for (;;) {
                        fp--;
                        if (fp == pos) { inteiro += 1; break; }
                        char c = buf[fp];
                        int d = c > '9' ? c - 'a' + 10 : c - '0';
                        if (d + 1 < b) { buf[fp++] = cs[d + 1]; break; }
                    }
                    break;
                }
            }
        } while (fr >= delta && fp < 2190);
    }
    /* parte inteira: dígitos de baixo para cima, exatos enquanto o expoente permite */
    while (((d_bits(inteiro / b) >> 52) & 0x7FF) > 1075) { inteiro /= b; buf[--ini] = '0'; }
    do {
        double r = resto(inteiro, b);
        buf[--ini] = cs[(int)r];
        inteiro = (inteiro - r) / b;
    } while (inteiro > 0 && ini > 1);
    if (neg) buf[--ini] = '-';
    /* o texto vai para o começo do buffer */
    int n = fp - ini;
    for (int i = 0; i < n; i++) buf[i] = buf[ini + i];
    return n;
}
