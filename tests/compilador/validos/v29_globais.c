/* Variáveis globais, static de arquivo e static de dentro de função. */
#include "sagui.h"

int contador = 100;
static int privado;
const int LIMITE = 3;
char recado[] = "global";
const char *ponteiro = "apontado";
int tabela[4] = { 1, 2, 3 };
int *meio = &tabela[2];
static unsigned char zerado[1000];
extern int depois;

static int proximo(void)
{
    static int n = 10;      /* guarda o valor entre as chamadas */
    return n++;
}

static const char *vez(void)
{
    static int chamadas;
    static const char *nomes[] = { "primeira", "segunda", "terceira" };
    return nomes[chamadas++ % 3];
}

static void mexe(void) { contador++; privado += 2; *meio = 30; }

int depois = 77;

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    mexe(); mexe();
    imprimir("%d %d %d %d\n", contador, privado, LIMITE, depois);
    imprimir("%s %s %d\n", recado, ponteiro, (int)sizeof recado);
    imprimir("%d %d %d %d\n", tabela[0], tabela[2], tabela[3], *meio);
    int a = proximo(), b = proximo(), c = proximo();
    imprimir("%d %d %d\n", a, b, c);
    for (int i = 0; i < 4; i++) imprimir("%s ", vez());
    imprimir("\n");
    int soma = 0;
    for (int i = 0; i < 1000; i++) soma += zerado[i];
    zerado[999] = 9;
    imprimir("%d %d\n", soma, zerado[999]);
    recado[0] = 'G';
    imprimir("%s\n", recado);
    int contador = 5;       /* a local esconde a global */
    { int contador = 6; imprimir("%d ", contador); }
    imprimir("%d\n", contador);
    return 0;
}
