/* Inicializadores: com nome de campo, com índice, parciais (o resto é zero). */
#include "sagui.h"

typedef struct { int x, y, z; } v3_t;
typedef struct { char nome[8]; v3_t pos; int vida; unsigned char cor[3]; } bicho_t;

static const v3_t origem;
static v3_t unidade = { .z = 1, .x = 1, .y = 1 };
static int esparso[10] = { [2] = 20, [7] = 70, 80 };
static bicho_t bichos[] = {
    { "sagui", { 1, 2, 3 }, 100, { 255, 200, 0 } },
    { .nome = "mico", .vida = 50 },
    { "bugio", .pos = { .y = 9 }, .cor = { [2] = 7 } },
};
static const char *dias[] = { [0] = "dom", [6] = "sab", [3] = "qua" };

int principal(int argc, char **argv)
{
    v3_t a = { 1 }, b = { 0 }, c = unidade;
    int m[2][3] = { { 1 }, { 4, 5 } };
    char texto[10] = "oi";
    char letras[] = { 'a', 'b', 'c', 0 };
    int n = 3;
    int dinamico[3] = { n, n * 2, n * n };
    bicho_t local = { .pos.x = 0 ? 1 : 2 };
    (void)argc; (void)argv; (void)b;
    imprimir("%d %d %d | %d %d %d\n", origem.x, origem.y, origem.z, unidade.x, unidade.y, unidade.z);
    imprimir("%d %d %d | %d %d %d\n", a.x, a.y, a.z, c.x, c.y, c.z);
    for (int i = 0; i < 10; i++) imprimir("%d ", esparso[i]);
    imprimir("\n");
    for (int i = 0; i < 3; i++)
        imprimir("%s %d,%d,%d vida %d cor %d,%d,%d\n", bichos[i].nome, bichos[i].pos.x, bichos[i].pos.y, bichos[i].pos.z,
                 bichos[i].vida, bichos[i].cor[0], bichos[i].cor[1], bichos[i].cor[2]);
    imprimir("%s %s %d\n", dias[0], dias[6], dias[1] == NULL);
    imprimir("%d %d %d %d %d %d\n", m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2]);
    imprimir("%s %d %d %s\n", texto, texto[5], (int)sizeof texto, letras);
    imprimir("%d %d %d\n", dinamico[0], dinamico[1], dinamico[2]);
    imprimir("%d %d\n", local.pos.x, local.vida);
    return 0;
}
