/* baixar <url> [arquivo] - baixa um arquivo da web (H49). Sem o nome do
 * arquivo, usa o último pedaço do endereço (ou "pagina.html"). Mostra o
 * progresso e a velocidade. */
#include "sagui.h"
#include "http.h"

typedef struct { int fd; unsigned ultimo; unsigned inicio; unsigned total; } destino_t;

static int gravar(void *ctx, const char *dados, unsigned n)
{
    destino_t *d = ctx;
    if (escrever(d->fd, dados, n) != (int)n) return -1;
    d->total += n;
    if (tempo_ms() - d->ultimo >= 1000) {
        d->ultimo = tempo_ms();
        imprimir("  %u KB...\n", d->total / 1024);
    }
    return 0;
}

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: baixar <url> [arquivo]\n"); return 1; }
    char nome[128];
    if (argc > 2) copia_texto(nome, argv[2], sizeof nome);
    else {
        const char *ultimo = argv[1];
        for (const char *p = argv[1]; *p; p++) if (*p == '/') ultimo = p + 1;
        size_t k = 0;
        while (ultimo[k] && ultimo[k] != '?' && k < sizeof nome - 1) { nome[k] = ultimo[k]; k++; }
        nome[k] = 0;
        if (!nome[0] || compara_texto_n(argv[1] + (argv[1][4] == 's' ? 8 : 7), nome, tam_texto(nome)) == 0) copia_texto(nome, "pagina.html", sizeof nome);
    }
    destino_t d = { abrir(nome, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR), tempo_ms(), tempo_ms(), 0 };
    if (d.fd < 0) { imprimir_em(ERROS, "baixar: não consegui criar %s (%s)\n", nome, nome_erro(d.fd)); return 1; }
    imprimir("Baixando %s\n", argv[1]);
    http_resposta_t r;
    int ok = http_obter(argv[1], gravar, &d, &r);
    fechar(d.fd);
    if (ok < 0 || r.situacao != 200) {
        if (ok < 0) imprimir_em(ERROS, "baixar: %s\n", r.erro);
        else imprimir_em(ERROS, "baixar: o servidor respondeu %d\n", r.situacao);
        if (ok < 0 || r.situacao >= 400) apagar(nome);
        return 1;
    }
    unsigned ms = tempo_ms() - d.inicio;
    imprimir("Gravado %s: %u bytes em %u ms (%u KB/s)\n", nome, d.total, ms, ms ? d.total / ms : 0);
    registrar("baixar: %s -> %s, %u bytes, situacao %d", argv[1], nome, d.total, r.situacao);
    return 0;
}
