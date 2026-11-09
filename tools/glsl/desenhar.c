/* desenha um quad com shaders de arquivo pelo webgl.c (host): desenhar vs fs nome=v... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int tela2d_nova(int l, int a);
int webgl_op(int op, const double *a, int na, void *dados, int ndados, const char *txt, double *o, int maxo, char *s, int maxs, int *eh_arr);
static int ctx; static double R[64]; static char S[1024]; static int arr;
static double gl(int op, const char *txt, void *d, int nd, int na, ...)
{
    double a[12] = { ctx }; __builtin_va_list ap; __builtin_va_start(ap, na);
    for (int i = 0; i < na; i++) a[1 + i] = __builtin_va_arg(ap, double);
    __builtin_va_end(ap);
    int r = webgl_op(op, a, na + 1, d, nd, txt, R, 64, S, sizeof S, &arr);
    return r > 0 ? R[0] : r;
}
static char *ler(const char *f) { FILE *x = fopen(f, "rb"); fseek(x, 0, SEEK_END); long n = ftell(x); fseek(x, 0, SEEK_SET); char *s = malloc(n + 1); fread(s, 1, n, x); s[n] = 0; fclose(x); return s; }
int main(int argc, char **argv)
{
    int tela = tela2d_nova(16, 16);
    double a[9] = { tela, 16, 16, 1, 1, 0, 1, 0 }; int ea; webgl_op(0, a, 8, NULL, 0, NULL, R, 64, S, sizeof S, &ea); ctx = (int)R[0];
    int v = (int)gl(60, NULL, NULL, 0, 1, (double)0x8B31), f = (int)gl(60, NULL, NULL, 0, 1, (double)0x8B30);
    gl(62, ler(argv[1]), NULL, 0, 1, (double)v); gl(63, NULL, NULL, 0, 1, (double)v);
    gl(62, ler(argv[2]), NULL, 0, 1, (double)f); gl(63, NULL, NULL, 0, 1, (double)f);
    int p = (int)gl(67, NULL, NULL, 0, 0); gl(69, NULL, NULL, 0, 2, (double)p, (double)v); gl(69, NULL, NULL, 0, 2, (double)p, (double)f); gl(71, NULL, NULL, 0, 1, (double)p);
    gl(74, NULL, NULL, 0, 1, (double)p); printf("link %g [%s]\n", gl(73, NULL, NULL, 0, 2, (double)p, (double)0x8B82), S);
    gl(72, NULL, NULL, 0, 1, (double)p);
    for (int k = 3; k < argc; k++) {
        char nome[128]; const char *eq = strchr(argv[k], '='); int ln = (int)(eq - argv[k]); memcpy(nome, argv[k], ln); nome[ln] = 0;
        float val[64]; int n = 0; const char *q = eq + 1; while (*q && n < 64) { val[n++] = strtof(q, (char **)&q); if (*q == ',') q++; }
        double loc = gl(77, nome, NULL, 0, 1, (double)p);
        if (loc >= 0) { gl(87, NULL, NULL, 0, 5, loc, (double)val[0], (double)(n > 1 ? val[1] : 0), (double)(n > 2 ? val[2] : 0), (double)(n > 3 ? val[3] : 1)); printf("attr %s loc %g\n", nome, loc); continue; }
        double ul = gl(78, nome, NULL, 0, 1, (double)p);
        if (ul < 0) { printf("sem %s\n", nome); continue; }
        int isint = !strcmp(nome, "isOrthographic") || !strcmp(nome, "receiveShadow");
        gl(82, NULL, val, n, 3, ul, (double)n, (double)isint);
        if (gl(3, NULL, NULL, 0, 0)) printf("erro uniform %s\n", nome);
    }
    float quad[] = { -1,-1,0, 1,-1,0, -1,1,0, 1,1,0 };
    int b = (int)gl(33, NULL, NULL, 0, 0); gl(35, NULL, NULL, 0, 2, (double)0x8892, (double)b); gl(36, NULL, quad, sizeof quad, 3, (double)0x8892, 0.0, (double)0x88E4);
    double lp = gl(77, "position", NULL, 0, 1, (double)p);
    gl(86, NULL, NULL, 0, 6, lp, 3.0, (double)0x1406, 0.0, 0.0, 0.0); gl(84, NULL, NULL, 0, 1, lp);
    gl(9, NULL, NULL, 0, 4, 0.0, 0.0, 1.0, 1.0); gl(12, NULL, NULL, 0, 1, (double)0x4100);
    gl(91, NULL, NULL, 0, 4, 5.0, 0.0, 4.0, 1.0);
    printf("erro gl %g\n", gl(3, NULL, NULL, 0, 0));
    uint8_t px[16 * 16 * 4]; gl(93, NULL, px, sizeof px, 6, 0.0, 0.0, 16.0, 16.0, (double)0x1908, (double)0x1401);
    printf("pixel %d %d %d %d\n", px[(8 * 16 + 8) * 4], px[(8 * 16 + 8) * 4 + 1], px[(8 * 16 + 8) * 4 + 2], px[(8 * 16 + 8) * 4 + 3]);
    return 0;
}
