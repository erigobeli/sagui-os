/* teste do compilador GLSL no host: gcc -DMOTOR_HOST -I../motor/host ... */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../../userland/libsagui/glsl.h"

static int falhas;
static void amostra(void *ctx, int un, int cubo, const float *co, int nc, const float *b, int lod, float *out)
{ (void)ctx;(void)cubo;(void)nc;(void)b;(void)lod; for (int v = 0; v < 4; v++) { out[v*4]=co[v*3]; out[v*4+1]=co[v*3+1]; out[v*4+2]=(float)un; out[v*4+3]=1; } }
static glsl_var *achar(glsl_var *l, int n, const char *nome) { for (int i = 0; i < n; i++) if (!strcmp(l[i].nome, nome)) return &l[i]; return NULL; }

/* roda fragment shader; uniforms "u" (float) opcional; devolve gl_FragColor das 4 vias */
static int rodar(const char *src, int tipo, float u, float out[4][4], int *masc)
{
    char erro[512] = "";
    glsl_prog *p = glsl_compilar(src, (int)strlen(src), tipo, erro, sizeof erro);
    if (!p) { printf("  ERRO compilar: %s\n", erro); return 0; }
    float *mem = calloc((size_t)p->nslots * 4 + 16, sizeof(float));
    glsl_var *vu = achar(p->unif, p->nunif, "u");
    if (vu) for (int v = 0; v < 4; v++) mem[vu->slot*4+v] = u;
    glsl_var *vt = achar(p->unif, p->nunif, "t");
    if (vt) for (int v = 0; v < 4; v++) mem[vt->slot*4+v] = 3;
    if (p->s_fragcoord >= 0) for (int v = 0; v < 4; v++) { mem[p->s_fragcoord*4+v] = (float)(v&1)+0.5f; mem[(p->s_fragcoord+1)*4+v] = (float)(v>>1)+0.5f; }
    int m = glsl_rodar(p, mem, 15, amostra, NULL);
    int s = tipo ? p->s_fragcolor : p->s_pos;
    for (int v = 0; v < 4; v++) for (int i = 0; i < 4; i++) out[v][i] = mem[(s+i)*4+v];
    if (masc) *masc = m;
    free(mem); glsl_liberar(p);
    return 1;
}
static void caso(const char *nome, const char *src, float u, const float esp[4][4], int esp_m)
{
    float o[4][4]; int m = 15;
    if (!rodar(src, 1, u, o, &m)) { printf("FALHA %s\n", nome); falhas++; return; }
    int ok = m == esp_m;
    for (int v = 0; v < 4; v++) if (esp_m & (1<<v)) for (int i = 0; i < 4; i++) if (fabsf(o[v][i]-esp[v][i]) > 1e-3f) ok = 0;
    printf("%s %s", ok ? "ok   " : "FALHA", nome);
    if (!ok) { printf("  m=%d:", m); for (int v = 0; v < 4; v++) printf(" (%g %g %g %g)", o[v][0],o[v][1],o[v][2],o[v][3]); falhas++; }
    printf("\n");
}
#define TODOS(a,b,c,d) {{a,b,c,d},{a,b,c,d},{a,b,c,d},{a,b,c,d}}
int main(void)
{
    const char *P = "precision mediump float;\nuniform float u;\n";
    char s[4096];
    #define F(corpo) (snprintf(s, sizeof s, "%s%s", P, corpo), s)
    { float e[4][4] = TODOS(1,0.5f,0.25f,1); caso("constante", F("void main(){ gl_FragColor = vec4(1.0, 0.5, 0.25, 1.0); }"), 0, e, 15); }
    { float e[4][4] = TODOS(2,4,6,1); caso("uniform+vec", F("void main(){ vec3 a = vec3(u); a *= vec3(1,2,3); gl_FragColor = vec4(a, 1.0); }"), 2, e, 15); }
    { float e[4][4] = {{0.5f,0.5f,0,1},{1.5f,0.5f,0,1},{0.5f,1.5f,0,1},{1.5f,1.5f,0,1}}; caso("fragcoord", F("void main(){ gl_FragColor = vec4(gl_FragCoord.xy, 0.0, 1.0); }"), 0, e, 15); }
    { float e[4][4] = {{0,0,0,1},{1,0,0,1},{0,0,0,1},{1,0,0,1}}; caso("if divergente", F("void main(){ float r = 0.0; if (gl_FragCoord.x > 1.0) r = 1.0; else r = 0.0; gl_FragColor = vec4(r,0,0,1); }"), 0, e, 15); }
    { float e[4][4] = TODOS(10,0,0,1); caso("for+break", F("void main(){ float s = 0.0; for (int i = 0; i < 100; i++) { if (i == 4) break; s += float(i); } s += 4.0; gl_FragColor = vec4(s,0,0,1); }"), 0, e, 15); }
    { float e[4][4] = {{1,0,0,1},{2,0,0,1},{1,0,0,1},{2,0,0,1}}; caso("for divergente+continue", F("void main(){ float s = 0.0; for (int i = 0; i < 4; i++) { if (float(i) > gl_FragCoord.x) continue; s += 1.0; } gl_FragColor = vec4(s,0,0,1); }"), 0, e, 15); }
    { float e[4][4] = {{5,0,0,1},{7,0,0,1},{5,0,0,1},{7,0,0,1}}; caso("funcao+return", F("float f(float x){ if (x > 1.0) return 7.0; return 5.0; }\nvoid main(){ gl_FragColor = vec4(f(gl_FragCoord.x),0,0,1); }"), 0, e, 15); }
    { float e[4][4] = TODOS(0,0,0,1); caso("discard", F("void main(){ if (gl_FragCoord.y > 1.0) discard; gl_FragColor = vec4(0,0,0,1); }"), 0, e, 3); }
    { float e[4][4] = TODOS(3,1,0,1); caso("mat2*vec2", F("void main(){ mat2 m = mat2(1.0,0.0, 1.0,1.0); vec2 v = m * vec2(2.0,1.0); gl_FragColor = vec4(v,0,1); }"), 0, e, 15); }
    { float e[4][4] = TODOS(0.5f,0.8660254f,1,1); caso("sin/cos/length", F("void main(){ float a = radians(30.0); gl_FragColor = vec4(sin(a), cos(a), length(vec2(0.6,0.8)), 1.0); }"), 0, e, 15); }
    { float e[4][4] = {{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1}}; caso("dFdx", F("void main(){ gl_FragColor = vec4(dFdx(gl_FragCoord.x), dFdy(gl_FragCoord.x), 0, 1); }"), 0, e, 15); }
    { float e[4][4] = TODOS(2,8,0.5f,1); caso("define+swizzle", F("#define DOIS 2.0\n#define DOBRO(x) ((x)*DOIS)\nvoid main(){ vec4 c = vec4(1,2,3,4); c.xy = c.yx; gl_FragColor = vec4(c.x, DOBRO(c.w), 0.5, 1); }"), 0, e, 15); }
    { float e[4][4] = TODOS(6,0,0,1); caso("array dinamico", F("void main(){ float a[4]; for (int i=0;i<4;i++) a[i]=float(i); int k = int(u); gl_FragColor = vec4(a[k]+a[3],0,0,1); }"), 3, e, 15); }
    { float e[4][4] = TODOS(3,4,0,1); caso("struct", F("struct L { vec2 p; float k; };\nvoid main(){ L l = L(vec2(1,2), 2.0); l.p *= vec2(3,2)/l.k*2.0; gl_FragColor = vec4(l.p,0,1); }"), 0, e, 15); }
    { float e[4][4] = TODOS(0.25f,0.75f,0,1); caso("textura", F("uniform sampler2D tx;\nvoid main(){ gl_FragColor = texture2D(tx, vec2(0.25,0.75)); }"), 0, e, 15); }
    { float e[4][4] = TODOS(1,0,0.5f,1); caso("smoothstep/mix/clamp", F("void main(){ gl_FragColor = vec4(smoothstep(0.0,1.0,2.0), clamp(-1.0,0.0,1.0), mix(0.0,1.0,0.5), 1); }"), 0, e, 15); }
    { float e[4][4] = TODOS(1,0,0,1); caso("out param+ternario", F("void g(out float a, inout float b){ a = 1.0; b = b > 0.0 ? 0.0 : 9.0; }\nvoid main(){ float x, y = 2.0; g(x, y); gl_FragColor = vec4(x, y, 0, 1); }"), 0, e, 15); }
    { float e[4][4] = TODOS(1,1,1,1); caso("&&/|| e int div", F("void main(){ int a = 7 / 2; bool b = a == 3 && (u > 0.0 || false); gl_FragColor = vec4(b ? 1.0 : 0.0, float(a)-2.0, 1, 1); }"), 1, e, 15); }
    /* vértice */
    {
        const char *vs = "attribute vec3 p; uniform mat4 m; varying vec2 uv; void main(){ uv = p.xy; gl_Position = m * vec4(p, 1.0); gl_PointSize = 1.0; }";
        char erro[512]; glsl_prog *pv = glsl_compilar(vs, (int)strlen(vs), 0, erro, sizeof erro);
        printf("%s vertice compila %s (attr %d unif %d vary %d)\n", pv && pv->nattr==1 && pv->nunif==1 && pv->nvary==1 ? "ok   " : "FALHA", pv ? "" : erro, pv?pv->nattr:0, pv?pv->nunif:0, pv?pv->nvary:0);
        if (!pv) falhas++; else glsl_liberar(pv);
    }
    /* erro esperado */
    { char erro[512] = ""; glsl_prog *pe = glsl_compilar("void main(){ gl_FragColor = vec3(1); }", 39, 1, erro, sizeof erro); printf("%s erro de tipo detectado: %s\n", pe ? "FALHA" : "ok   ", erro); if (pe) falhas++; }
    printf(falhas ? "%d FALHAS\n" : "TUDO OK\n", falhas);
    return falhas != 0;
}
