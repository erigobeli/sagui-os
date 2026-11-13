/* imprimir e formatar_texto: os formatos %d %u %x %c %s %p, largura, - 0 e *. */
#include "sagui.h"

static void tabela(const char *nome, int qtd, int preco)
{
    imprimir("|%-8s|%4d|%6d,%02d|\n", nome, qtd, preco / 100, preco % 100);
}

int principal(int argc, char **argv)
{
    char buf[32], pequeno[8];
    int n;
    (void)argc; (void)argv;
    imprimir("[%d] [%5d] [%-5d] [%05d] [%d]\n", 42, 42, 42, 42, -42);
    imprimir("[%u] [%x] [%8x] [%08x]\n", 4000000000u, 255, 255, 255);
    imprimir("[%c] [%s] [%10s] [%-10s]\n", 'z', "sagui", "sagui", "sagui");
    imprimir("[%*d] [%-*d] [%%]\n", 6, 7, 6, 7);
    tabela("banana", 12, 350);
    tabela("melancia", 1, 1299);
    n = formatar_texto(buf, sizeof buf, "%s-%03d", "peca", 7);
    imprimir("%s %d\n", buf, n);
    n = formatar_texto(pequeno, sizeof pequeno, "%s", "texto comprido demais");
    imprimir("%s %d\n", pequeno, (int)tam_texto(pequeno));
    formatar_texto(buf, sizeof buf, "%d/%d/%d", 13, 11, 2026);
    imprimir("%s\n", buf);
    imprimir_em(SAIDA, "saida %d\n", SAIDA);
    escrever_texto(SAIDA, "direto\n");
    escrever(SAIDA, "abcdef\n", 4);
    imprimir("\n%s\n", nome_erro(ERRO_NAO_EXISTE)[0] ? "tem nome" : "sem nome");
    for (int i = 1; i <= 3; i++) imprimir("%d x %d = %2d\n", i, i, i * i);
    return 0;
}
