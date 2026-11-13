/* Mexer com texto: contar palavras, inverter, cifra de César, palíndromos. */
#include "sagui.h"

static bool letra(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static char minuscula(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static int conta_palavras(const char *s)
{
    int n = 0;
    bool dentro = false;
    for (; *s; s++) {
        if (eh_espaco(*s)) dentro = false;
        else if (!dentro) { dentro = true; n++; }
    }
    return n;
}

static void inverte(char *ini, char *fim)
{
    while (ini < fim) { char t = *ini; *ini++ = *fim; *fim-- = t; }
}

static void inverte_palavras(char *s)
{
    char *ini = s;
    for (char *p = s; ; p++) {
        if (*p == ' ' || *p == 0) {
            inverte(ini, p - 1);
            ini = p + 1;
            if (!*p) break;
        }
    }
}

static void cesar(char *s, int chave)
{
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s = (char)('a' + (*s - 'a' + chave + 26) % 26);
        else if (*s >= 'A' && *s <= 'Z') *s = (char)('A' + (*s - 'A' + chave + 26) % 26);
    }
}

static bool palindromo(const char *s)
{
    const char *f = s + tam_texto(s);
    while (s < f) {
        if (!letra(*s)) { s++; continue; }
        if (!letra(f[-1])) { f--; continue; }
        if (minuscula(*s) != minuscula(f[-1])) return false;
        s++; f--;
    }
    return true;
}

int principal(int argc, char **argv)
{
    char frase[] = "o sagui come banana no galho";
    char segredo[] = "Sagui OS, versao Dois!";
    int vezes[26] = { 0 };
    int mais = 0;
    (void)argc; (void)argv;
    imprimir("%d palavras\n", conta_palavras(frase));
    imprimir("%d %d\n", conta_palavras("   "), conta_palavras(" um  dois\ttres\n"));
    inverte_palavras(frase);
    imprimir("%s\n", frase);
    inverte(frase, frase + tam_texto(frase) - 1);
    imprimir("%s\n", frase);
    cesar(segredo, 3);
    imprimir("%s\n", segredo);
    cesar(segredo, -3);
    imprimir("%s\n", segredo);
    imprimir("%d %d %d\n", palindromo("A base do teto desaba"), palindromo("Socorram-me, subi no onibus em Marrocos"), palindromo("sagui"));
    for (const char *p = frase; *p; p++) if (letra(*p)) vezes[minuscula(*p) - 'a']++;
    for (int i = 1; i < 26; i++) if (vezes[i] > vezes[mais]) mais = i;
    imprimir("letra mais comum: %c (%d vezes)\n", 'a' + mais, vezes[mais]);
    return 0;
}
