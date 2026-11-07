/* teste_motor.c - roda o motor do Cipó no Linux e grava a página em PPM.
 *   teste_motor pagina.html saida.ppm [largura] */
#include "sagui.h"
#include "../../userland/inc/motor.h"

static uint8_t *ler_arquivo(const char *c, unsigned *n)
{
    FILE *f = fopen(c, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long t = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(t + 1);
    if (fread(d, 1, t, f) != (size_t)t) { fclose(f); free(d); return NULL; }
    fclose(f); d[t] = 0; *n = (unsigned)t;
    return d;
}
static const char *pasta;
/* Recursos: arquivo local ao lado da página, ou curl se a URL for http(s). */
static uint8_t *baixar(const char *url, unsigned *n)
{
    if (strncmp(url, "http://teste/", 13) == 0) { char c[600]; snprintf(c, sizeof c, "%s/%s", pasta, url + 13); return ler_arquivo(c, n); }
    char cmd[1400];
    snprintf(cmd, sizeof cmd, "curl -sL --max-time 10 -A 'Mozilla/5.0 Chrome/130' '%s'", url);
    FILE *f = popen(cmd, "r");
    if (!f) return NULL;
    size_t cap = 65536, k = 0; uint8_t *d = malloc(cap);
    size_t r;
    while ((r = fread(d + k, 1, cap - k, f)) > 0) { k += r; if (k == cap) { cap *= 2; d = realloc(d, cap); } }
    pclose(f);
    fprintf(stderr, "baixou %s: %zu bytes\n", url, k);
    *n = (unsigned)k;
    return k ? d : (free(d), NULL);
}
static bool imagem(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a) { (void)d; (void)n; *l = 40; *a = 30; *px = calloc(40 * 30, 4); for (int i = 0; i < 1200; i++) (*px)[i] = 0x3366CC; return true; }

int main(int argc, char **argv)
{
    unsigned n;
    uint8_t *h = ler_arquivo(argv[1], &n);
    if (!h) return 1;
    pasta = argv[1];
    char *p = strrchr(argv[1], '/'); static char dir[512];
    if (p) { memcpy(dir, argv[1], p - argv[1]); pasta = dir; } else pasta = ".";
    int larg = argc > 3 ? atoi(argv[3]) : 846;
    motor_ganchos_t g = { baixar, imagem };
    motor_t *m = motor_novo(&g);
    clock_t t0 = clock();
    motor_ler(m, (const char *)h, (int)n, argc > 4 ? argv[4] : "http://teste/pagina.html");
    while (motor_imagens(m, 8)) {}
    int alt = motor_layout(m, larg, 560);
    if (getenv("CMDS")) { extern void motor_dump_cmds(motor_t *, int, int); int a0 = atoi(getenv("CMDS")); motor_dump_cmds(m, a0, a0 + 40); }
    if (getenv("ARVORE")) { extern void motor_dump(motor_t *, int); motor_dump(m, atoi(getenv("ARVORE"))); }
    int nos, cmds, imgs; motor_numeros(m, &nos, &cmds, &imgs);
    fprintf(stderr, "titulo \"%s\" altura %d nos %d cmds %d imgs %d (%.0f ms)\n", motor_titulo(m), alt, nos, cmds, imgs, (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC);
    int a = alt < 2400 ? (alt < 100 ? 100 : alt) : 2400;
    uint32_t *px = malloc((size_t)larg * a * 4);
    motor_pintar(m, px, larg, larg, 0, a);
    FILE *o = fopen(argv[2], "wb");
    fprintf(o, "P6\n%d %d\n255\n", larg, a);
    for (int i = 0; i < larg * a; i++) { uint8_t c[3] = { px[i] >> 16, px[i] >> 8, px[i] }; fwrite(c, 1, 3, o); }
    fclose(o);
    return 0;
}
