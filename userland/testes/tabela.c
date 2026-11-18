/* tabela - mostra a tabela de partições que o kernel leu (D066, h112).
 *
 *   tabela <disco>
 *
 * Uma linha com o estado e o problema, e uma por partição, no formato que o
 * teste confere: "tabela: hd1 p 5 tipo 83 ini 4096 n 2048 ext". A saída vai
 * para o terminal, que o teste lê pela serial. */
#include "sagui.h"

static const char *const estados[] = { "vazio", "mbr", "gpt", "vbr", "desconhecida", "erro" };

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir("uso: tabela <disco>\n"); return 1; }
    static tabela_disco_t t;
    int r = disco_tabela(argv[1], &t);
    if (r < 0) {
        imprimir("tabela: %s -> %d\n", argv[1], r);
        return 1;
    }
    const char *e = t.estado <= TABELA_ERRO ? estados[t.estado] : "?";
    imprimir("tabela: %s %s conferida %u fim %u n %u problema \"%s\"\n", argv[1], e, t.conferida, t.maior_fim, t.n, t.problema);
    for (unsigned i = 0; i < t.n; i++) {
        const particao_t *p = &t.p[i];
        imprimir("tabela: %s p %u tipo %x ini %u n %u %s\n", argv[1], p->entrada, p->tipo, p->inicio, p->setores, p->formato[0] ? p->formato : "-");
    }
    return 0;
}
