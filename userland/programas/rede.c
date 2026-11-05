/* rede - mostra a placa de rede, o endereço IP e os contadores (D032). */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    static char texto[2048];
    int n = info_sistema(INFO_REDE, texto, sizeof texto - 1);
    if (n < 0) { imprimir_em(ERROS, "rede: %s\n", nome_erro(n)); return 1; }
    texto[n] = 0;
    escrever_texto(SAIDA, texto);
    return 0;
}
