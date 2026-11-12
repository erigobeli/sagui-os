/* WebGL em software no host: triângulo, profundidade, textura, blend, FBO */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
int tela2d_nova(int l, int a);
int webgl_op(int op, const double *a, int na, void *dados, int ndados, const char *txt, double *o, int maxo, char *s, int maxs, int *eh_arr);
static int ctx, falhas;
static double R[64]; static char S[1024]; static int arr;
static double gl(int op, const char *txt, void *d, int nd, int na, double a0, double a1, double a2, double a3, double a4, double a5, double a6, double a7, double a8)
{
    double a[10] = { ctx, a0, a1, a2, a3, a4, a5, a6, a7, a8 };
    int r = webgl_op(op, a, na + 1, d, nd, txt, R, 64, S, sizeof S, &arr);
    return r > 0 ? R[0] : r;
}
#define G(op, ...) gl(op, NULL, NULL, 0, ARGN(__VA_ARGS__), __VA_ARGS__)
#define ARGN(...) ARGN_(__VA_ARGS__, 9,8,7,6,5,4,3,2,1)
#define ARGN_(a,b,c,d,e,f,g,h,i,n,...) n
static double gl0(int op) { return gl(op, NULL, NULL, 0, 0, 0,0,0,0,0,0,0,0,0); }
static double g1(int op, double a) { return gl(op, NULL, NULL, 0, 1, a,0,0,0,0,0,0,0,0); }
static double g2(int op, double a, double b) { return gl(op, NULL, NULL, 0, 2, a,b,0,0,0,0,0,0,0); }
static double g4(int op, double a, double b, double c, double d) { return gl(op, NULL, NULL, 0, 4, a,b,c,d,0,0,0,0,0); }
static int programa(const char *vs, const char *fs)
{
    int v = (int)g1(60, 0x8B31), f = (int)g1(60, 0x8B30);
    gl(62, vs, NULL, 0, 1, v,0,0,0,0,0,0,0,0); g1(63, v);
    gl(62, fs, NULL, 0, 1, f,0,0,0,0,0,0,0,0); g1(63, f);
    if (!g2(64, v, 0x8B81)) { gl(65, NULL, NULL, 0, 1, v,0,0,0,0,0,0,0,0); printf("VS: %s\n", S); }
    if (!g2(64, f, 0x8B81)) { gl(65, NULL, NULL, 0, 1, f,0,0,0,0,0,0,0,0); printf("FS: %s\n", S); }
    int p = (int)gl0(67); g2(69, p, v); g2(69, p, f); g1(71, p);
    if (!g2(73, p, 0x8B82)) { gl(74, NULL, NULL, 0, 1, p,0,0,0,0,0,0,0,0); printf("link: %s\n", S); }
    g1(72, p);
    return p;
}
static uint8_t px[64 * 64 * 4];
static void ler(void) { gl(93, NULL, px, sizeof px, 6, 0, 0, 64, 64, 0x1908, 0x1401, 0,0,0); }
static void conferir(const char *nome, int x, int y, int r, int g, int b, int a)
{
    uint8_t *p = px + (y * 64 + x) * 4;
    int ok = abs(p[0]-r) <= 2 && abs(p[1]-g) <= 2 && abs(p[2]-b) <= 2 && abs(p[3]-a) <= 2;
    printf("%s %s (%d,%d) = %d %d %d %d", ok ? "ok   " : "FALHA", nome, x, y, p[0], p[1], p[2], p[3]);
    if (!ok) { printf("  esperado %d %d %d %d", r, g, b, a); falhas++; }
    printf("\n");
}
#include <time.h>
#define _GNU_SOURCE
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
static unsigned long amostras[200000]; static int namostras;
static void prof(int sig, siginfo_t *si, void *uc) { (void)sig; (void)si; if (namostras < 200000) amostras[namostras++] = (unsigned long)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP]; }
static void prof_ligar(void) { struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = prof; sa.sa_flags = SA_SIGINFO | SA_RESTART; sigaction(SIGPROF, &sa, NULL); struct itimerval it = { { 0, 200 }, { 0, 200 } }; setitimer(ITIMER_PROF, &it, NULL); }
static void prof_gravar(void) { struct itimerval z; memset(&z, 0, sizeof z); setitimer(ITIMER_PROF, &z, NULL); FILE *f = fopen("/tmp/amostras.txt", "w"); extern char __executable_start; for (int i = 0; i < namostras; i++) fprintf(f, "%lx\n", amostras[i] - (unsigned long)&__executable_start); fclose(f); }
int main(int argc, char **argv)
{
    int tela = tela2d_nova(64, 64);
    double a[9] = { tela, 64, 64, 1, 1, 0, 1, 0 };
    int ea; webgl_op(0, a, 8, NULL, 0, NULL, R, 64, S, sizeof S, &ea); ctx = (int)R[0];
    /* 1. triângulo colorido por varying */
    int p = programa("attribute vec2 p; attribute vec3 c; varying vec3 vc; void main(){ vc = c; gl_Position = vec4(p, 0.0, 1.0); }",
                     "precision mediump float; varying vec3 vc; void main(){ gl_FragColor = vec4(vc, 1.0); }");
    float v[] = { -1,-1, 1,0,0,  1,-1, 0,1,0,  -1,1, 0,0,1 };
    int b = (int)gl0(33); g2(35, 0x8892, b); gl(36, NULL, v, sizeof v, 2, 0x8892, 0, 0x88E4, 0,0,0,0,0,0);
    gl(77, "p", NULL, 0, 1, p,0,0,0,0,0,0,0,0); int lp = (int)R[0];
    gl(77, "c", NULL, 0, 1, p,0,0,0,0,0,0,0,0); int lc = (int)R[0];
    g1(84, lp); g1(84, lc);
    gl(86, NULL, NULL, 0, 6, lp, 2, 0x1406, 0, 20, 0, 0,0,0);
    gl(86, NULL, NULL, 0, 6, lc, 3, 0x1406, 0, 20, 8, 0,0,0);
    g4(9, 0, 0, 0, 1); g1(12, 0x4000);
    g4(91, 4, 0, 3, 1);
    ler();
    conferir("canto vermelho", 0, 0, 251, 2, 2, 255);
    conferir("meio aresta", 31, 1, 124, 126, 6, 255);
    conferir("fora do tri", 63, 63, 0, 0, 0, 255);
    printf("erro gl: %g\n", gl0(3));
    /* 2. uniform + profundidade */
    int p2 = programa("attribute vec2 p; uniform float z; void main(){ gl_Position = vec4(p, z, 1.0); }",
                      "precision mediump float; uniform vec4 cor; void main(){ gl_FragColor = cor; }");
    float q[] = { -1,-1, 1,-1, -1,1, 1,1 };
    int b2 = (int)gl0(33); g2(35, 0x8892, b2); gl(36, NULL, q, sizeof q, 2, 0x8892, 0, 0x88E4, 0,0,0,0,0,0);
    gl(77, "p", NULL, 0, 1, p2,0,0,0,0,0,0,0,0); int l2 = (int)R[0];
    g1(85, lc);
    gl(86, NULL, NULL, 0, 6, l2, 2, 0x1406, 0, 0, 0, 0,0,0); g1(84, l2);
    gl(78, "z", NULL, 0, 1, p2,0,0,0,0,0,0,0,0); int uz = (int)R[0];
    gl(78, "cor", NULL, 0, 1, p2,0,0,0,0,0,0,0,0); int uc = (int)R[0];
    g1(4, 0xB71); g1(12, 0x4100);
    float z1 = 0.5f, c1[4] = {0,1,0,1}, z2 = 0.8f, c2[4] = {1,0,0,1};
    gl(82, NULL, &z1, 1, 3, uz, 1, 0, 0,0,0,0,0,0); gl(82, NULL, c1, 4, 3, uc, 4, 0, 0,0,0,0,0,0); g4(91, 5, 0, 4, 1);
    gl(82, NULL, &z2, 1, 3, uz, 1, 0, 0,0,0,0,0,0); gl(82, NULL, c2, 4, 3, uc, 4, 0, 0,0,0,0,0,0); g4(91, 5, 0, 4, 1);
    ler(); conferir("z: verde na frente", 10, 10, 0, 255, 0, 255);
    /* 3. blend */
    g1(5, 0xB71); g1(4, 0xBE2); g2(21, 0x302, 0x303);
    float c3[4] = {0,0,1,0.5f}; gl(82, NULL, c3, 4, 3, uc, 4, 0, 0,0,0,0,0,0); g4(91, 5, 0, 4, 1);
    ler(); conferir("blend 50%", 10, 10, 0, 128, 128, 191);
    g1(5, 0xBE2);
    /* 4. textura 2x2 NEAREST */
    int p3 = programa("attribute vec2 p; varying vec2 uv; void main(){ uv = p*0.5+0.5; gl_Position = vec4(p,0,1); }",
                      "precision mediump float; uniform sampler2D t; varying vec2 uv; void main(){ gl_FragColor = texture2D(t, uv); }");
    int t = (int)gl0(38); g2(40, 0xDE1, t);
    uint8_t tx[16] = { 255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255 };
    gl(43, NULL, tx, 16, 8, 0xDE1, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401, 0);
    gl(42, NULL, NULL, 0, 3, 0xDE1, 0x2801, 0x2600, 0,0,0,0,0,0); gl(42, NULL, NULL, 0, 3, 0xDE1, 0x2800, 0x2600, 0,0,0,0,0,0);
    gl(77, "p", NULL, 0, 1, p3,0,0,0,0,0,0,0,0); int l3 = (int)R[0];
    gl(86, NULL, NULL, 0, 6, l3, 2, 0x1406, 0, 0, 0, 0,0,0); g1(84, l3);
    g4(91, 5, 0, 4, 1);
    ler(); conferir("tex baixo-esq", 5, 5, 255, 0, 0, 255); conferir("tex baixo-dir", 60, 5, 0, 255, 0, 255);
    conferir("tex cima-esq", 5, 60, 0, 0, 255, 255); conferir("tex cima-dir", 60, 60, 255, 255, 255, 255);
    /* 5. FBO com textura, depois desenha no padrão */
    int t2 = (int)gl0(38); g2(40, 0xDE1, t2);
    gl(43, NULL, NULL, 0, 8, 0xDE1, 0, 0x1908, 64, 64, 0, 0x1908, 0x1401, 0);
    gl(42, NULL, NULL, 0, 3, 0xDE1, 0x2801, 0x2601, 0,0,0,0,0,0);
    int fb = (int)gl0(50); g2(52, 0x8D40, fb); gl(53, NULL, NULL, 0, 5, 0x8D40, 0x8CE0, 0xDE1, t2, 0, 0,0,0,0);
    printf("%s fbo completo\n", g1(55, 0x8D40) == 0x8CD5 ? "ok   " : "FALHA");
    g4(9, 1, 1, 0, 1); g1(12, 0x4000);
    g2(52, 0x8D40, 0); g4(91, 5, 0, 4, 1);
    ler(); conferir("tex do fbo", 32, 32, 255, 255, 0, 255);
    printf("erro gl: %g\n", gl0(3));
    if (argc > 1) {   /* bench: textura em tela cheia 64x64 x 400 = 1,6 Mpx */
        g2(40, 0xDE1, t); g4(9,0,0,0,1);
        gl(42, NULL, NULL, 0, 3, 0xDE1, 0x2801, 0x2703, 0,0,0,0,0,0); g1(47, 0xDE1);
        g1(72, argv[1][0] == 'c' ? p2 : p3);
        struct timespec a0, a1; clock_gettime(CLOCK_MONOTONIC, &a0); if (argv[1][1]) prof_ligar();
        for (int k = 0; k < (argv[1][1] ? 8000 : 400); k++) { g1(12, 0x4100); if (argv[1][0] != 'l') g4(91, 5, 0, 4, 1); }
        clock_gettime(CLOCK_MONOTONIC, &a1); if (argv[1][1]) prof_gravar();
        double ms = (a1.tv_sec - a0.tv_sec) * 1e3 + (a1.tv_nsec - a0.tv_nsec) / 1e6;
        printf("bench: %.1f ms, %.1f ns/pixel\n", ms, ms * 1e6 / (400.0 * 64 * 64));
    }
    printf(falhas ? "%d FALHAS\n" : "TUDO OK\n", falhas);
    return falhas != 0;
}
