/* switch: casos, default no meio, e a "queda" de um caso para o próximo. */
#include "sagui.h"

static const char *dias(int mes)
{
    switch (mes) {
    case 2: return "28 ou 29";
    case 4: case 6: case 9: case 11: return "30";
    default: return "31";
    }
}

static int pontos(char c)
{
    int p = 0;
    switch (c) {
    case 'a': p += 1;       /* cai */
    case 'b': p += 10;      /* cai */
    case 'c': p += 100; break;
    case 'z': p = -1; break;
    default: p = 0;
    }
    return p;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    imprimir("%s %s %s %s\n", dias(1), dias(2), dias(9), dias(12));
    imprimir("%d %d %d %d %d\n", pontos('a'), pontos('b'), pontos('c'), pontos('z'), pontos('k'));
    int vogais = 0, espacos = 0, outros = 0;
    for (const char *s = "o rato roeu a roupa"; *s; s++) {
        switch (*s) {
        case 'a': case 'e': case 'i': case 'o': case 'u': vogais++; break;
        case ' ': espacos++; break;
        default: outros++;
        }
    }
    imprimir("%d %d %d\n", vogais, espacos, outros);
    for (int i = 0; i < 6; i++) {
        switch (i) {
        case 0: continue;               /* continue dentro de switch vale para o for */
        case 3: imprimir("tres "); break;
        case -1: imprimir("nunca ");
        default:
            if (i == 5) break;
            imprimir("%d ", i);
        }
    }
    imprimir("\n");
    int x = 1000000;
    switch (x * 3) { case 3000000: imprimir("grande\n"); break; case 0: imprimir("zero\n"); }
    switch (x) { }
    switch (7) { default: imprimir("so default\n"); }
    return 0;
}
