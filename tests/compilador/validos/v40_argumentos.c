// args: banana 42 "duas palavras" -x
/* Argumentos da linha de comando: argc e argv. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int soma = 0;
    imprimir("%d argumentos\n", argc - 1);
    for (int i = 1; i < argc; i++) {
        imprimir("%d: [%s] %d letras\n", i, argv[i], (int)tam_texto(argv[i]));
        soma += texto_para_int(argv[i]);
    }
    imprimir("soma dos numeros: %d\n", soma);
    imprimir("%d\n", argv[argc] == NULL);
    char **p = argv + 1;
    while (*p) { if ((*p)[0] == '-') imprimir("opcao %c\n", (*p)[1]); p++; }
    return argc;
}
