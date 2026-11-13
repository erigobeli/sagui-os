/* teste_html.c - lê um HTML e imprime a árvore, um nó por linha, com recuo:
 *   <tag atributo="valor">   para elementos
 *   "texto"                  para textos (espaços em sequência viram um só)
 * Usado pelo teste da H61 no computador de desenvolvimento. */
#include "../../userland/navegador/navegador.h"

static void mostrar(const no_t *x, int nivel)
{
    for (; x; x = x->prox) {
        if (x->tipo == NO_TEXTO) {
            char buf[4096];
            uint32_t k = 0;
            bool espaco = false;
            for (uint32_t i = 0; i < x->tam && k < sizeof buf - 2; i++) {
                char c = x->texto[i];
                if (eh_espaco(c)) { if (!espaco) buf[k++] = ' '; espaco = true; }
                else { buf[k++] = c; espaco = false; }
            }
            buf[k] = 0;
            if (k == 1 && buf[0] == ' ') continue;      /* só espaço entre tags: não aparece */
            printf("%*s\"%s\"\n", nivel * 2, "", buf);
            continue;
        }
        printf("%*s<%s", nivel * 2, "", x->tag ? html_nome_tag(x->tag) : x->texto);
        if (x->atributos)
            for (const char *p = x->atributos; *p;) {
                const char *v = p + strlen(p) + 1;
                printf(" %s=\"%s\"", p, v);
                p = v + strlen(v) + 1;
            }
        printf(">\n");
        mostrar(x->filho, nivel + 1);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static char d[4 << 20];
    uint32_t n = (uint32_t)fread(d, 1, sizeof d, f);
    fclose(f);
    int charset = argc > 2 ? atoi(argv[2]) : -1;
    documento_t *doc = html_ler(d, n, charset);
    if (!doc) return 1;
    printf("titulo: %s\n", doc->titulo);
    printf("utf8: %d\n", doc->utf8);
    mostrar(doc->raiz->filho, 0);
    html_liberar(doc);
    return 0;
}
