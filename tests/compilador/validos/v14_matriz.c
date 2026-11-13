/* Vetores de mais de uma dimensão. */
#include "sagui.h"

#define L 3
#define C 4

static int soma_linha(int m[][C], int l)
{
    int s = 0;
    for (int c = 0; c < C; c++) s += m[l][c];
    return s;
}

static char tabuleiro[3][3];

int principal(int argc, char **argv)
{
    int m[L][C] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } };
    int t[C][L];
    (void)argc; (void)argv;
    for (int l = 0; l < L; l++)
        for (int c = 0; c < C; c++) t[c][l] = m[l][c];
    for (int c = 0; c < C; c++) {
        for (int l = 0; l < L; l++) imprimir("%3d", t[c][l]);
        imprimir("\n");
    }
    imprimir("%d %d %d\n", soma_linha(m, 0), soma_linha(m, 2), m[1][2]);
    imprimir("%d %d\n", (int)sizeof m, (int)sizeof m[0]);
    int *plano = &m[0][0];
    imprimir("%d %d\n", plano[5], *(*(m + 2) + 1));
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) tabuleiro[i][j] = (i + j) % 2 ? 'O' : 'X';
    for (int i = 0; i < 3; i++) imprimir("%c%c%c\n", tabuleiro[i][0], tabuleiro[i][1], tabuleiro[i][2]);
    int cubo[2][3][2] = { { { 1, 2 }, { 3, 4 }, { 5, 6 } }, { { 7, 8 }, { 9, 10 }, { 11, 12 } } };
    int s = 0;
    for (int a = 0; a < 2; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 2; c++) s += cubo[a][b][c] * (a + 1);
    imprimir("%d %d\n", s, cubo[1][2][0]);
    int corrido[2][3] = { 1, 2, 3, 4, 5, 6 };
    imprimir("%d %d\n", corrido[1][0], corrido[0][2]);
    char nomes[3][8] = { "mico", "sagui", "bugio" };
    for (int i = 0; i < 3; i++) imprimir("%s(%d) ", nomes[i], (int)tam_texto(nomes[i]));
    imprimir("\n");
    return 0;
}
