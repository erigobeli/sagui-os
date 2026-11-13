/* Vetores: percorrer, procurar, copiar, passar para funções. */
#include "sagui.h"

#define N 8

static int maior(const int *v, int n)
{
    int m = v[0];
    for (int i = 1; i < n; i++) if (v[i] > m) m = v[i];
    return m;
}

static int procura(int v[], int n, int x)
{
    for (int i = 0; i < n; i++) if (v[i] == x) return i;
    return -1;
}

static void inverte(int *v, int n)
{
    for (int i = 0, j = n - 1; i < j; i++, j--) { int t = v[i]; v[i] = v[j]; v[j] = t; }
}

static int global[N];

int principal(int argc, char **argv)
{
    int v[N] = { 3, 9, -2, 7, 7, 0, 15, 4 };
    int copia[N];
    (void)argc; (void)argv;
    imprimir("%d %d %d\n", maior(v, N), procura(v, N, 7), procura(v, N, 100));
    memcpy(copia, v, sizeof v);
    inverte(copia, N);
    for (int i = 0; i < N; i++) imprimir("%d ", copia[i]);
    imprimir("\n");
    for (int i = 0; i < N; i++) global[i] = v[i] * v[i];
    imprimir("%d %d\n", global[2], global[6]);
    imprimir("%d %d\n", (int)sizeof v, (int)(sizeof v / sizeof v[0]));
    int zeros[5] = { 0 };
    int parcial[5] = { 1, 2 };
    imprimir("%d %d %d %d\n", zeros[4], parcial[1], parcial[2], parcial[4]);
    char letras[26];
    for (int i = 0; i < 26; i++) letras[i] = (char)('a' + i);
    imprimir("%c%c%c\n", letras[18], letras[0], letras[6]);
    int sem_tamanho[] = { 5, 4, 3, 2, 1 };
    imprimir("%d\n", (int)(sizeof sem_tamanho / sizeof(int)));
    return 0;
}
