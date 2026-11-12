#include <stdio.h>
#include <string.h>
#include "../../userland/libsagui/glsl.h"
int main(int argc, char **argv)
{
    char erro[512] = ""; const char *s = argv[1];
    FILE *ff = fopen(argv[1], "rb"); static char buf[1<<20]; int nb = ff ? (int)fread(buf, 1, sizeof buf - 1, ff) : 0; if (ff) { fclose(ff); s = buf; buf[nb] = 0; }
    glsl_prog *p = glsl_compilar(s, (int)strlen(s), strstr(argv[1], ".frag") ? 1 : 0, erro, sizeof erro);
    if (!p) { printf("erro %s\n", erro); return 1; }
    printf("slots %d fragcolor %d fragcoord %d\n", p->nslots, p->s_fragcolor, p->s_fragcoord);
    for (int i = 0; i < p->nconst; i++) printf("const [%d]=%g\n", p->const_slot[i], p->consts[i]);
    for (int i = 0; i < p->nins; i++) { glsl_ins *I = &p->ins[i]; if (argc > 2 && (i < atoi(argv[2]) || i > atoi(argv[3]))) continue; printf("%3d op%-3d n%d d%d a%d b%d c%d e%d%d%d sw%d%d%d%d alvo%d\n", i, I->op, I->n, I->d, I->a, I->b, I->c, I->ea, I->eb, I->ec, I->sw[0], I->sw[1], I->sw[2], I->sw[3], I->alvo); }
}
