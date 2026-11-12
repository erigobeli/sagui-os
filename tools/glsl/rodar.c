/* roda um shader no host com valores dados: rodar arquivo nome=v1,v2,... (uniforms e attributes; para fragment, varyings)
 * mostra varyings (vértice) ou gl_FragColor (fragmento) da via 0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../userland/libsagui/glsl.h"
static glsl_var *achar(glsl_var *l, int n, const char *nome) { for (int i = 0; i < n; i++) if (!strcmp(l[i].nome, nome)) return &l[i]; return NULL; }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *s = malloc(n + 1); fread(s, 1, n, f); s[n] = 0; fclose(f);
    int frag = strstr(argv[1], ".frag") != NULL;
    char erro[512] = "";
    glsl_prog *p = glsl_compilar(s, (int)n, frag, erro, sizeof erro);
    if (!p) { printf("erro: %s\n", erro); return 1; }
    float *m = calloc((size_t)p->nslots * 4 + 64, sizeof(float));
    for (int k = 2; k < argc; k++) {
        char nome[128]; const char *eq = strchr(argv[k], '='); if (!eq) continue;
        int ln = (int)(eq - argv[k]); memcpy(nome, argv[k], ln); nome[ln] = 0;
        glsl_var *v = achar(p->unif, p->nunif, nome); if (!v) v = achar(p->attr, p->nattr, nome); if (!v) v = achar(p->vary, p->nvary, nome);
        if (!v) { printf("(sem %s)\n", nome); continue; }
        const char *q = eq + 1;
        for (int l = 0; l < 4; l++) {   /* "a,b,c;a,b,c;..." = um grupo por via; um grupo só vale para todas */
            const char *r = q; int i = 0;
            while (*r && *r != ';' && i < v->tam) { float x = strtof(r, (char **)&r); m[(v->slot + i) * 4 + l] = x; i++; if (*r == ',') r++; }
            if (*r == ';') q = r + 1;
        }
    }
    int vivo = glsl_rodar(p, m, 15, NULL, NULL);
    printf("vivo %d\n", vivo);
    if (!frag) {
        printf("gl_Position %g %g %g %g\n", m[p->s_pos * 4], m[(p->s_pos + 1) * 4], m[(p->s_pos + 2) * 4], m[(p->s_pos + 3) * 4]);
        for (int i = 0; i < p->nvary; i++) { printf("%s:", p->vary[i].nome); for (int k = 0; k < p->vary[i].tam; k++) printf(" %g", m[(p->vary[i].slot + k) * 4]); printf("\n"); }
    } else for (int l = 0; l < 4; l++) printf("via %d cor %g %g %g %g\n", l, m[p->s_fragcolor * 4 + l], m[(p->s_fragcolor + 1) * 4 + l], m[(p->s_fragcolor + 2) * 4 + l], m[(p->s_fragcolor + 3) * 4 + l]);
    return 0;
}
