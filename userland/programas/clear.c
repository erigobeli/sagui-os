/* clear (limpar) - limpa a tela do terminal.
 *
 *   clear           limpa a tela e coloca o cursor no canto superior esquerdo
 *   limpar          mesmo que clear (apelido em portugues)
 *
 * Escreve a sequencia de escape ANSI que apaga a tela inteira e reposiciona
 * o cursor no começo (linha 0, coluna 0). Nao precisa de chamada de sistema
 * especial: so escreve no stdout. Tudo do zero, sem libc. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    /* Sequencia de escape ANSI:
     *   \033[2J  apaga a tela toda
     *   \033[H   move o cursor para a posicao 0,0 (canto superior esquerdo) */
    escrever_texto(SAIDA, "\033[2J\033[H");
    return 0;
}
