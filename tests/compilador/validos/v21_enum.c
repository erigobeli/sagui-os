/* enum: nomes para números. */
#include "sagui.h"

enum cor { VERMELHO, VERDE, AZUL, QTD_CORES };
enum { PEQUENO = 1, MEDIO = 10, GRANDE = MEDIO * 10, ENORME };
typedef enum { SEG, TER, QUA, QUI, SEX, SAB, DOM } dia_t;

static const char *nome_cor(enum cor c)
{
    static const char *nomes[QTD_CORES] = { "vermelho", "verde", "azul" };
    return c < QTD_CORES ? nomes[c] : "?";
}

static bool fim_de_semana(dia_t d) { return d == SAB || d == DOM; }

int principal(int argc, char **argv)
{
    enum cor c = VERDE;
    int contagem[QTD_CORES] = { 0 };
    (void)argc; (void)argv;
    imprimir("%d %d %d %d\n", VERMELHO, VERDE, AZUL, QTD_CORES);
    imprimir("%d %d %d %d\n", PEQUENO, MEDIO, GRANDE, ENORME);
    imprimir("%s %s\n", nome_cor(c), nome_cor(AZUL));
    c = (enum cor)(c + 1);
    imprimir("%s %d\n", nome_cor(c), c == AZUL);
    for (dia_t d = SEG; d <= DOM; d++) imprimir("%d", fim_de_semana(d));
    imprimir("\n");
    enum cor pinta[] = { AZUL, AZUL, VERMELHO, AZUL, VERDE };
    for (unsigned i = 0; i < sizeof pinta / sizeof pinta[0]; i++) contagem[pinta[i]]++;
    for (int i = 0; i < QTD_CORES; i++) imprimir("%s=%d ", nome_cor((enum cor)i), contagem[i]);
    imprimir("\n%d\n", (int)sizeof(enum cor));
    return 0;
}
