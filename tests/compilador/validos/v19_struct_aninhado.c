/* structs dentro de structs, vetores de structs e inicializadores. */
#include "sagui.h"

typedef struct { int dia, mes, ano; } data_nasc_t;

typedef struct {
    const char *nome;
    data_nasc_t nasc;
    int notas[3];
    struct { char letra; int numero; } sala;
} aluno_t;

static aluno_t turma[] = {
    { "Bia",  { 12, 3, 2010 }, { 9, 8, 10 }, { 'A', 12 } },
    { "Caio", { 30, 11, 2009 }, { 6, 7, 5 }, { 'B', 7 } },
    { "Duda", { 1, 1, 2011 }, { 10, 10, 9 }, { 'A', 12 } },
};

static int media(const aluno_t *a)
{
    int s = 0;
    for (int i = 0; i < 3; i++) s += a->notas[i];
    return s / 3;
}

static const aluno_t *mais_velho(const aluno_t *v, int n)
{
    const aluno_t *m = v;
    for (int i = 1; i < n; i++) {
        const data_nasc_t *a = &v[i].nasc, *b = &m->nasc;
        if (a->ano < b->ano || (a->ano == b->ano && (a->mes < b->mes || (a->mes == b->mes && a->dia < b->dia)))) m = &v[i];
    }
    return m;
}

int principal(int argc, char **argv)
{
    int n = sizeof turma / sizeof turma[0];
    (void)argc; (void)argv;
    for (int i = 0; i < n; i++)
        imprimir("%-5s %02d/%02d/%d media %d sala %c%d\n", turma[i].nome, turma[i].nasc.dia, turma[i].nasc.mes,
                 turma[i].nasc.ano, media(&turma[i]), turma[i].sala.letra, turma[i].sala.numero);
    imprimir("mais velho: %s\n", mais_velho(turma, n)->nome);
    aluno_t novo = turma[0];
    novo.nome = "Edu";
    novo.notas[1] = 0;
    novo.sala.numero++;
    imprimir("%s %d %d | %s %d %d\n", novo.nome, media(&novo), novo.sala.numero, turma[0].nome, media(&turma[0]), turma[0].sala.numero);
    aluno_t *p = &turma[1];
    p->nasc.ano -= 1;
    (*p).notas[2] = 10;
    imprimir("mais velho: %s, media %d\n", mais_velho(turma, n)->nome, media(p));
    imprimir("%d\n", (int)sizeof(aluno_t));
    return 0;
}
