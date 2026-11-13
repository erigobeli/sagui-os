/* const: tabelas que não mudam e ponteiros para coisas que não se alteram. */
#include "sagui.h"

static const int DIAS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static const char *const MESES[] = { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" };
static const char HEXA[] = "0123456789abcdef";
static const struct { const char *nome; unsigned cor; } CORES[] = {
    { "verde", 0x0B7A3E }, { "azul", 0x2A6FD8 }, { "branco", 0xFFFFFF },
};
static const unsigned char SPRITE[4][4] = { { 0, 1, 1, 0 }, { 1, 2, 2, 1 }, { 1, 2, 2, 1 }, { 0, 1, 1, 0 } };

static int dia_do_ano(int dia, int mes)
{
    int n = dia;
    for (int m = 0; m < mes - 1; m++) n += DIAS[m];
    return n;
}

static void hexa(unsigned v, char *saida)
{
    for (int i = 7; i >= 0; i--) { saida[i] = HEXA[v & 15]; v >>= 4; }
    saida[8] = 0;
}

static unsigned procura_cor(const char *nome)
{
    for (unsigned i = 0; i < sizeof CORES / sizeof CORES[0]; i++)
        if (compara_texto(CORES[i].nome, nome) == 0) return CORES[i].cor;
    return 0;
}

int principal(int argc, char **argv)
{
    char buf[9];
    const int limite = 5;
    const char *p = "leitura";
    char destino[8];
    char *const fixo = destino;
    int soma = 0;
    (void)argc; (void)argv;
    imprimir("%d %d %s %s\n", dia_do_ano(1, 3), dia_do_ano(31, 12), MESES[0], MESES[11]);
    hexa(procura_cor("azul"), buf);
    imprimir("%s %x %x\n", buf, procura_cor("verde"), procura_cor("roxo"));
    for (int l = 0; l < 4; l++) for (int c = 0; c < 4; c++) soma += SPRITE[l][c] * (l + 1);
    imprimir("%d %d\n", soma, limite * 2);
    p++;
    copia_texto(fixo, p, 8);
    fixo[0] = 'E';
    imprimir("%s %s\n", p, destino);
    imprimir("%d %d\n", (int)sizeof DIAS, (int)(sizeof MESES / sizeof MESES[0]));
    return 0;
}
