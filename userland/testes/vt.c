/* vt - teste da emulação do Terminal (H55): limpa a tela, escreve com cores
 * (16, 256 e 24 bits), posiciona texto, desenha uma caixa com molduras UTF-8 e
 * com o conjunto gráfico antigo, usa uma região de rolagem e pergunta ao
 * terminal onde está o cursor (o Terminal responde pela entrada). */
#include "sagui.h"

static void s(const char *t) { escrever_texto(SAIDA, t); }

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    s("\x1b[2J\x1b[H");
    s("\x1b[1;32mverde negrito\x1b[0m \x1b[41m fundo vermelho \x1b[0m \x1b[38;5;208mlaranja 256\x1b[0m \x1b[38;2;42;111;216mazul 24 bits\x1b[0m\n");
    s("\x1b[5;10H\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90");     /* ┌───┐ */
    s("\x1b[6;10H\xe2\x94\x82 S \xe2\x94\x82");                                     /* │ S │ */
    s("\x1b[7;10H\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98");     /* └───┘ */
    s("\x1b[5;20H\x1b(0lqqqk\x1b(B\x1b[6;20H\x1b(0x\x1b(B G \x1b(0x\x1b(B\x1b[7;20H\x1b(0mqqqj\x1b(B");
    s("\x1b[9;1H\xe2\x96\x88\xe2\x96\x93\xe2\x96\x92\xe2\x96\x91 blocos  acentos: ação é você\n");
    /* Região de rolagem nas linhas 12 a 14: escreve 5 linhas, ficam as 3 últimas. */
    s("\x1b[12;14r\x1b[12;1H");
    for (int i = 1; i <= 5; i++) { char t[32]; formatar_texto(t, sizeof t, "rolagem %d\n", i); s(t); }
    s("\x1b[r");
    s("\x1b[20;5H\x1b[6n");
    /* A resposta vem pela entrada: ESC [ linha ; coluna R */
    modo_terminal(TERM_BRUTO);
    char r[32];
    int k = 0;
    unsigned inicio = tempo_ms();
    while (k < 31 && tempo_ms() - inicio < 3000) {
        if (disponivel(ENTRADA) <= 0) { dormir(10); continue; }
        char c;
        if (ler(ENTRADA, &c, 1) == 1) { r[k++] = c; if (c == 'R') break; }
    }
    r[k] = 0;
    modo_terminal(TERM_LINHA);
    s("\x1b[22;1H");
    imprimir("resposta da posicao: %s\n", r[0] == 27 ? r + 1 : r);
    return 0;
}
