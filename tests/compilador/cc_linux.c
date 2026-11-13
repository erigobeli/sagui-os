/* cc_linux.c - o compilador do SaguiOS rodando no computador de
 * desenvolvimento, para os testes (tests/h64_h66_compilador.py).
 *
 * É só o "ambiente" (arquivos e memória do Linux) em volta do mesmo núcleo
 * de userland/compilador que roda dentro do SaguiOS.
 *
 * uso: cc_linux arquivo.c [-o programa] [-I pasta] [-L libsagui.o] [--ri] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../userland/compilador/compilador.h"

static int ler_arquivo(void *ctx, const char *caminho, unsigned char **dados, unsigned *tam)
{
    FILE *f = fopen(caminho, "rb");
    long n;
    (void)ctx;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *dados = malloc((size_t)n + 1);
    if (fread(*dados, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*dados); return -1; }
    fclose(f);
    *tam = (unsigned)n;
    return 0;
}

static int gravar_arquivo(void *ctx, const char *caminho, const unsigned char *dados, unsigned tam)
{
    FILE *f = fopen(caminho, "wb");
    (void)ctx;
    if (!f) return -1;
    fwrite(dados, 1, tam, f);
    fclose(f);
    return 0;
}

static void *reservar(void *ctx, unsigned n) { (void)ctx; return malloc(n); }
static void liberar(void *ctx, void *p) { (void)ctx; free(p); }
static void mensagem(void *ctx, const char *texto) { (void)ctx; fputs(texto, stderr); }
static void listagem(void *ctx, const char *texto) { (void)ctx; fputs(texto, stdout); }

int main(int argc, char **argv)
{
    CcAmbiente amb = { NULL, ler_arquivo, gravar_arquivo, reservar, liberar, mensagem, listagem };
    CcOpcoes op;
    int i;
    memset(&op, 0, sizeof op);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) op.saida = argv[++i];
        else if (!strcmp(argv[i], "-I") && i + 1 < argc && op.qtd_pastas < CC_MAX_PASTAS) op.pastas[op.qtd_pastas++] = argv[++i];
        else if (!strcmp(argv[i], "-L") && i + 1 < argc) op.biblioteca = argv[++i];
        else if (!strcmp(argv[i], "--ri")) op.mostrar_ri = 1;
        else if (!strcmp(argv[i], "--latin1")) op.latin1 = 1;
        else if (op.qtd_fontes < CC_MAX_FONTES) op.fontes[op.qtd_fontes++] = argv[i];
    }
    return cc_compilar(&amb, &op);
}
