/* markdown.h - mostra um texto em Markdown numa janela, já formatado:
 * títulos, negrito, itálico, código, listas, citações, tabelas, linhas e
 * links (um clique abre o link no Cipó). Só leitura, com rolagem pelo
 * teclado, pela roda do mouse e pela barra. O texto chega em Latin-1 (como
 * os .md do sistema, que o build converte).
 *
 * Com "puro", o texto não é lido como Markdown: só as linhas longas são
 * quebradas (para licenças e outros textos simples). */
#ifndef SAGUI_MARKDOWN_H
#define SAGUI_MARKDOWN_H

#include "gui.h"

typedef struct md_peca md_peca_t;

typedef struct {
    int x, y, l, a;          /* a área na janela */
    int topo;                /* rolagem, em pixels */
    int altura;              /* altura do conteúdo formatado */
    char *fonte; int n;      /* o texto (cópia) */
    bool puro;
    md_peca_t *pecas; int qtd, cap;   /* o que desenhar, já no lugar */
    char *letras; int nl, capl;       /* os textos das peças */
    bool arrastando; int arr_y0, arr_topo0;
    char titulo[80];         /* o primeiro título do texto (para o registro) */
    int cont[8];             /* títulos, negrito, itálico, código, links, itens de lista, células, citações */
} md_t;

bool md_iniciar(md_t *m, int x, int y, int l, int a);
void md_definir(md_t *m, const char *texto, int n, bool puro);
void md_desenhar(jan_t *j, md_t *m);
bool md_tratar(jan_t *j, md_t *m, const evento_t *e);   /* true = tratou (rolou, abriu link) */
void md_fim(md_t *m);
void md_mudar_tamanho(md_t *m, int l, int a);   /* a janela mudou: refaz as linhas, perto do mesmo ponto */
/* Para os testes: quantas peças de cada tipo saíram ("títulos 3, negrito 5, ..."). */
void md_resumo(const md_t *m, char *saida, int max);

#endif
