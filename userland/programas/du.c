/* du (disco-usado) - mostra o espaço usado por arquivos e pastas.
 *
 *   du                 o tamanho da pasta atual, em bytes
 *   du CAMINHO...      o tamanho de cada caminho dado
 *
 * Percorre pastas recursivamente, somando o tamanho de cada arquivo. Pastas
 * vazias contam como 0. O tamanho de uma pasta é a soma dos arquivos dentro
 * dela (não conta a própria entrada da pasta, que ocupa quase nada no
 * sistema de arquivos do SaguiOS).
 *
 * Cada linha sai como "TAMANHO  CAMINHO", igual ao du dos outros sistemas.
 *
 * Não há chamada de sistema para isso: usamos info_arquivo e listar da
 * libsagui, do zero. */
#include "sagui.h"

/* Soma o tamanho de "caminho". Se for pasta, entra nela e soma tudo dentro.
 * Devolve o total em bytes, ou -1 se não existir. */
static int64_t tamanho_usado(const char *caminho)
{
    info_arquivo_t inf;
    if (info_arquivo(caminho, &inf) < 0) return -1;

    if (inf.tipo != TIPO_PASTA) return (int64_t)inf.tamanho;

    /* É pasta: lista o conteúdo e soma cada entrada. */
    int64_t total = 0;
    entrada_pasta_t e;
    int i = 0;
    char dentro[NOME_MAX + 64];
    for (;;) {
        int r = listar(caminho, i, &e);
        if (r == 0) break;      /* acabou */
        if (r < 0) return -1;   /* erro */

        /* Monta o caminho completo da entrada. */
        size_t nc = tam_texto(caminho);
        copia_texto(dentro, caminho, sizeof dentro);
        if (nc > 0 && dentro[nc - 1] != '/') dentro[nc++] = '/';
        copia_texto(dentro + nc, e.nome, sizeof dentro - nc);

        int64_t t = tamanho_usado(dentro);
        if (t < 0) return -1;
        total += t;
        i++;
    }
    return total;
}

int principal(int argc, char **argv)
{
    if (argc < 2) {
        int64_t t = tamanho_usado(".");
        if (t < 0) { imprimir_em(ERROS, "du: não consigo ler a pasta atual\n"); return 1; }
        imprimir("%llu\t.\n", (unsigned long long)t);
        return 0;
    }

    int erros = 0;
    for (int i = 1; i < argc; i++) {
        int64_t t = tamanho_usado(argv[i]);
        if (t < 0) {
            imprimir_em(ERROS, "du: %s: não consigo ler\n", argv[i]);
            erros = 1;
        } else {
            imprimir("%llu\t%s\n", (unsigned long long)t, argv[i]);
        }
    }
    return erros;
}
