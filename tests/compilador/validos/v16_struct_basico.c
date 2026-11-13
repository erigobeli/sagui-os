/* struct: campos, tamanho, atribuição de um struct inteiro. */
#include "sagui.h"

struct ponto { int x, y; };

struct pessoa {
    char nome[16];
    int idade;
    struct ponto casa;
    bool ativo;
};

static int distancia2(struct ponto a, struct ponto b)
{
    int dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

int principal(int argc, char **argv)
{
    struct ponto p = { 3, 4 }, q;
    struct pessoa ana;
    (void)argc; (void)argv;
    q = p;
    q.x += 10;
    imprimir("%d %d %d %d\n", p.x, p.y, q.x, q.y);
    imprimir("%d\n", distancia2(p, q));
    copia_texto(ana.nome, "Ana", sizeof ana.nome);
    ana.idade = 31;
    ana.casa = q;
    ana.ativo = true;
    struct pessoa copia = ana;
    copia.nome[0] = 'E';
    copia.casa.y = -1;
    imprimir("%s %d (%d,%d) %d\n", ana.nome, ana.idade, ana.casa.x, ana.casa.y, ana.ativo);
    imprimir("%s %d (%d,%d) %d\n", copia.nome, copia.idade, copia.casa.x, copia.casa.y, copia.ativo);
    imprimir("%d %d\n", (int)sizeof(struct ponto), (int)sizeof(struct pessoa));
    struct { char c; int i; short s; char d; } buraco;
    imprimir("%d %d\n", (int)sizeof buraco, (int)((char *)&buraco.i - (char *)&buraco));
    return 0;
}
