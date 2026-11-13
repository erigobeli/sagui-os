/* Multiplicação de matrizes, potência por quadrados e o jogo da vida. */
#include "sagui.h"

#define N 3
typedef int matriz_t[N][N];

static void multiplica(matriz_t a, matriz_t b, matriz_t r)
{
    matriz_t t;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            t[i][j] = 0;
            for (int k = 0; k < N; k++) t[i][j] += a[i][k] * b[k][j];
        }
    memcpy(r, t, sizeof t);
}

static int determinante(matriz_t m)
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

static void mostra(matriz_t m)
{
    for (int i = 0; i < N; i++) imprimir("%6d %6d %6d\n", m[i][0], m[i][1], m[i][2]);
}

#define LADO 8
static char mundo[LADO][LADO], prox[LADO][LADO];

static int vizinhos(int l, int c)
{
    int n = 0;
    for (int dl = -1; dl <= 1; dl++)
        for (int dc = -1; dc <= 1; dc++) {
            int a = l + dl, b = c + dc;
            if ((dl || dc) && a >= 0 && a < LADO && b >= 0 && b < LADO) n += mundo[a][b];
        }
    return n;
}

int principal(int argc, char **argv)
{
    matriz_t a = { { 1, 2, 0 }, { 0, 1, 3 }, { 4, 0, 1 } }, id = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, r;
    (void)argc; (void)argv;
    multiplica(a, a, r);
    mostra(r);
    imprimir("det %d %d\n", determinante(a), determinante(r));
    memcpy(r, id, sizeof r);
    for (int e = 0; e < 5; e++) multiplica(r, a, r);
    mostra(r);
    mundo[1][2] = mundo[2][3] = mundo[3][1] = mundo[3][2] = mundo[3][3] = 1;     /* um planador */
    for (int geracao = 0; geracao < 8; geracao++) {
        for (int l = 0; l < LADO; l++)
            for (int c = 0; c < LADO; c++) {
                int v = vizinhos(l, c);
                prox[l][c] = (char)(v == 3 || (mundo[l][c] && v == 2));
            }
        memcpy(mundo, prox, sizeof mundo);
    }
    for (int l = 0; l < LADO; l++) {
        for (int c = 0; c < LADO; c++) imprimir("%c", mundo[l][c] ? '#' : '.');
        imprimir("\n");
    }
    return 0;
}
