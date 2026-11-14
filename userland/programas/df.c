/* df (espaco) - mostra o espaço dos discos: total, usado, livre e quanto
 * por cento está ocupado (H83).
 *
 * "/" é a árvore de arquivos na memória: o ramdisk que veio no pendrive de
 * boot mais tudo o que foi gravado depois, que fica na RAM até desligar.
 * Por isso o "livre" de "/" é a memória livre do computador.
 * "/usb" é o pendrive de dados (FAT32), quando está montado; o kernel conta
 * os clusters livres na própria FAT (ver D046).
 *
 * Os números vêm do kernel por info_sistema(INFO_DISCOS_ESPACO). Com -k,
 * tudo sai em KB; sem, cada número escolhe KB, MB ou GB para ficar curto. */
#include "sagui.h"

/* Um tamanho em KB, no formato mais legível: "812 KB", "37,5 MB", "1,9 GB". */
static void tamanho(char *s, size_t max, unsigned kb, bool so_kb)
{
    if (so_kb || kb < 1024) formatar_texto(s, max, "%u KB", kb);
    else if (kb < 1024 * 1024) formatar_texto(s, max, "%u,%u MB", kb / 1024, (kb % 1024) * 10 / 1024);
    else formatar_texto(s, max, "%u,%u GB", kb / (1024 * 1024), (kb % (1024 * 1024)) / 1024 * 10 / 1024);
}

int principal(int argc, char **argv)
{
    bool so_kb = false;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-k") == 0) so_kb = true;
        else {
            imprimir_em(ERROS, "uso: df [-k]\n");
            return 1;
        }
    }
    info_espaco_t v[2];
    int n = info_sistema(INFO_DISCOS_ESPACO, v, 2);
    if (n < 0) { imprimir_em(ERROS, "df: %s\n", nome_erro(n)); return 1; }
    imprimir("%-6s %-14s %11s %11s %11s %4s\n", "LOCAL", "TIPO", "TOTAL", "USADO", "LIVRE", "USO");
    for (int i = 0; i < n; i++) {
        char t[16], u[16], l[16];
        tamanho(t, sizeof t, v[i].total_kb, so_kb);
        tamanho(u, sizeof u, v[i].usado_kb, so_kb);
        tamanho(l, sizeof l, v[i].livre_kb, so_kb);
        /* Porcentagem arredondada para cima, como de costume: um disco
         * quase vazio mostra 1%, e não 0%, se tiver qualquer coisa. Para
         * "usado * 100" caber em 32 bits num pendrive grande, os dois
         * números são divididos por 2 juntos (a proporção não muda). */
        unsigned pct = 0, us = v[i].usado_kb, to = v[i].total_kb;
        while (us > 40000000u || to > 40000000u) { us >>= 1; to >>= 1; }
        if (to) pct = (us * 100 + to - 1) / to;
        imprimir("%-6s %-14s %11s %11s %11s %3u%%", v[i].ponto, v[i].tipo, t, u, l, pct);
        if (v[i].rotulo[0]) imprimir("  \"%s\"", v[i].rotulo);
        imprimir("\n");
    }
    if (n < 2) imprimir("(nenhum pendrive montado em /usb: use montar)\n");
    return 0;
}
