/* ls (listar) - lista arquivos e pastas. Uma entrada por linha, para que
 * "ls bin | conta" conte os programas. Com -l, cada linha ganha a data de
 * modificação ("-" quando não se sabe: os arquivos da imagem do sistema). */
#include "sagui.h"

static bool longo;

static void escrever_data(const char *pasta, const char *nome)
{
    if (!longo) { imprimir("\n"); return; }
    char c[300];
    if (nome) {
        copia_texto(c, pasta, sizeof c);
        if (compara_texto(pasta, "/") != 0) junta_texto(c, "/", sizeof c);
        junta_texto(c, nome, sizeof c);
    } else copia_texto(c, pasta, sizeof c);
    data_t d;
    if (data_arquivo(c, &d) == 0 && d.ano) imprimir("  %02d/%02d/%04d %02d:%02d\n", d.dia, d.mes, d.ano, d.hora, d.minuto);
    else imprimir("  -\n");
}

static int listar_pasta(const char *pasta)
{
    entrada_pasta_t e;
    int i = 0, r;
    while ((r = listar(pasta, i, &e)) == 1) {
        if (e.tipo == TIPO_PASTA) {
            if (longo) imprimir("%-28s %7s       ", e.nome, "pasta");
            else imprimir("%s/", e.nome);
        } else if (e.tipo == TIPO_PROGRAMA) imprimir("%-28s %7u bytes  programa", e.nome, e.tamanho);
        else imprimir("%-28s %7u bytes", e.nome, e.tamanho);
        escrever_data(pasta, e.nome);
        i++;
    }
    if (r < 0) {
        info_arquivo_t inf;
        if (r == ERRO_NAO_E_PASTA && info_arquivo(pasta, &inf) == 0) {   /* ls de um arquivo */
            imprimir("%-28s %7u bytes", pasta, inf.tamanho);
            escrever_data(pasta, NULL);
            return 0;
        }
        imprimir_em(ERROS, "ls: %s: %s\n", pasta, nome_erro(r));
        return 1;
    }
    return 0;
}

int principal(int argc, char **argv)
{
    int primeiro = 1;
    if (argc > 1 && compara_texto(argv[1], "-l") == 0) { longo = true; primeiro = 2; }
    if (argc <= primeiro) return listar_pasta(".");
    int erros = 0;
    for (int i = primeiro; i < argc; i++) {
        if (argc - primeiro > 1) imprimir("%s:\n", argv[i]);
        erros += listar_pasta(argv[i]);
    }
    return erros ? 1 : 0;
}
