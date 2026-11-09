/* compila arquivos .vert/.frag com o glsl.c e mostra o erro (host) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../userland/libsagui/glsl.h"
int main(int argc, char **argv)
{
    int falhas = 0;
    for (int k = 1; k < argc; k++) {
        FILE *f = fopen(argv[k], "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        char *s = malloc(n + 1); fread(s, 1, n, f); s[n] = 0; fclose(f);
        char erro[512] = "";
        glsl_prog *p = glsl_compilar(s, (int)n, strstr(argv[k], ".frag") ? 1 : 0, erro, sizeof erro);
        if (p) printf("ok    %s: %d ins, %d slots, %d unif, %d attr, %d vary\n", argv[k], p->nins, p->nslots, p->nunif, p->nattr, p->nvary);
        else { printf("FALHA %s: %s\n", argv[k], erro); falhas++; }
        if (p) glsl_liberar(p);
        free(s);
    }
    return falhas != 0;
}
