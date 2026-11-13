/* Ordenação: bolha, inserção e quicksort com função de comparação. */
#include "sagui.h"

static void bolha(int *v, int n)
{
    for (int i = 0; i < n - 1; i++)
        for (int j = 0; j < n - 1 - i; j++)
            if (v[j] > v[j + 1]) { int t = v[j]; v[j] = v[j + 1]; v[j + 1] = t; }
}

static void insercao(char *s)
{
    for (int i = 1; s[i]; i++) {
        char c = s[i];
        int j = i - 1;
        while (j >= 0 && s[j] > c) { s[j + 1] = s[j]; j--; }
        s[j + 1] = c;
    }
}

typedef int (*compara_t)(const void *, const void *);

static void troca(char *a, char *b, unsigned tam)
{
    while (tam--) { char t = *a; *a++ = *b; *b++ = t; }
}

static void rapido(void *base, int n, unsigned tam, compara_t cmp)
{
    char *v = base;
    int i = 0, j = n - 1;
    if (n < 2) return;
    troca(v, v + (n / 2) * tam, tam);          /* pivô no começo */
    i = 1;
    while (i <= j) {
        if (cmp(v + i * tam, v) <= 0) i++;
        else { troca(v + i * tam, v + j * tam, tam); j--; }
    }
    troca(v, v + j * tam, tam);
    rapido(v, j, tam, cmp);
    rapido(v + (j + 1) * tam, n - j - 1, tam, cmp);
}

typedef struct { const char *nome; int idade; } pessoa_t;

static int por_idade(const void *a, const void *b) { return ((const pessoa_t *)a)->idade - ((const pessoa_t *)b)->idade; }
static int por_nome(const void *a, const void *b) { return compara_texto(((const pessoa_t *)a)->nome, ((const pessoa_t *)b)->nome); }
static int decrescente(const void *a, const void *b) { return *(const int *)b - *(const int *)a; }

int principal(int argc, char **argv)
{
    int v[] = { 34, -2, 99, 0, 15, 15, 7, -40, 61, 3 };
    int n = sizeof v / sizeof v[0];
    char letras[] = "compilador";
    pessoa_t gente[] = { { "Rui", 40 }, { "Ana", 25 }, { "Leo", 33 }, { "Bia", 19 }, { "Edu", 52 } };
    unsigned semente = 12345;
    int muitos[200], ok = 1;
    (void)argc; (void)argv;
    bolha(v, n);
    for (int i = 0; i < n; i++) imprimir("%d ", v[i]);
    imprimir("\n");
    insercao(letras);
    imprimir("%s\n", letras);
    rapido(gente, 5, sizeof(pessoa_t), por_idade);
    for (int i = 0; i < 5; i++) imprimir("%s(%d) ", gente[i].nome, gente[i].idade);
    imprimir("\n");
    rapido(gente, 5, sizeof(pessoa_t), por_nome);
    for (int i = 0; i < 5; i++) imprimir("%s ", gente[i].nome);
    imprimir("\n");
    for (int i = 0; i < 200; i++) { semente = semente * 1103515245u + 12345u; muitos[i] = (int)((semente >> 16) % 1000); }
    rapido(muitos, 200, sizeof(int), decrescente);
    for (int i = 1; i < 200; i++) if (muitos[i - 1] < muitos[i]) ok = 0;
    imprimir("%d %d %d\n", ok, muitos[0], muitos[199]);
    return 0;
}
