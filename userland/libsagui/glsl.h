/* glsl.h - programa GLSL compilado para a VM de 4 vias (glsl.c) */
#ifndef SAGUI_GLSL_H
#define SAGUI_GLSL_H

/* operações da VM */
enum {
    G_MOV, G_MOVSW, G_ADD, G_SUB, G_MUL, G_DIV, G_MOD, G_MIN, G_MAX, G_POW, G_ATAN2, G_STEP,
    G_NEG, G_NOT, G_ABS, G_SIGN, G_FLOOR, G_CEIL, G_FRACT, G_SQRT, G_INVSQRT, G_EXP, G_EXP2, G_LOG, G_LOG2,
    G_SIN, G_COS, G_TAN, G_ASIN, G_ACOS, G_ATAN, G_TOINT, G_TOBOOL,
    G_LT, G_LE, G_GT, G_GE, G_EQ, G_NE, G_AND, G_OR, G_XOR,
    G_DOT, G_CROSS, G_LEN, G_DIST, G_NORM, G_EQALL, G_NEALL, G_ANY, G_ALL,
    G_MATVEC, G_VECMAT, G_MATMAT, G_MIX, G_CLAMP, G_SMOOTH, G_SEL, G_REFLECT, G_REFRACT, G_FACEFWD,
    G_IDXLD, G_IDXST,
    G_TEX2D, G_TEXCUBE,
    G_DFDX, G_DFDY, G_FWIDTH,
    G_IF, G_ELSE, G_ENDIF, G_LOOP, G_LCOND, G_LCONT, G_JMP, G_LEND, G_BREAK, G_CONTINUE,
    G_FNBEGIN, G_FNEND, G_RETURN, G_DISCARD, G_FIM
};

typedef struct {
    unsigned short op, n;   /* operação, número de componentes */
    int d, a, b, c;         /* posições (componente) na memória */
    unsigned char ea, eb, ec;   /* operando escalar: o mesmo valor para todos os componentes */
    unsigned char sw[4];    /* swizzle / tamanhos auxiliares */
    int alvo;               /* salto */
} glsl_ins;

/* variáveis visíveis de fora (uniforms, attributes, varyings) */
typedef struct {
    char nome[64];
    int slot, tam;          /* posição e número de floats (array inteiro) */
    int base, lin, col, arr, elem;   /* tipo: base 1 float 2 int 3 bool 4 sampler2D 5 samplerCube; elem = floats por elemento */
} glsl_var;

typedef struct glsl_prog {
    int tipo;               /* 0 vértice, 1 fragmento */
    glsl_ins *ins; int nins, cins;
    int nslots;             /* floats por via */
    float *consts; int *const_slot; int nconst, cconst;   /* valores fixos (copiados para a memória antes de rodar) */
    glsl_var *unif; int nunif;
    glsl_var *attr; int nattr;
    glsl_var *vary; int nvary;
    int s_pos, s_ptsize, s_fragcolor, s_fragcoord, s_frontfacing, s_pointcoord;   /* embutidas (-1 = não usada) */
    int usa_discard;
    void *globais;          /* (compilação) */
} glsl_prog;

/* amostragem de textura (webgl.c): coords das 4 vias (u, v[, w]); devolve RGBA x 4 */
typedef void (*glsl_amostrar_fn)(void *ctx, int unidade, int cubo, const float *coord, int ncoord, const float *bias, int lod, float *out);

glsl_prog *glsl_compilar(const char *fonte, int n, int tipo, char *erro, int max_erro);
void glsl_liberar(glsl_prog *p);
/* roda o programa nas 4 vias; mem tem nslots*4 floats (slot*4 + via); vivo: máscara inicial; devolve a máscara final (discard tira vias) */
int  glsl_rodar(const glsl_prog *p, float *mem, int vivo, glsl_amostrar_fn amostrar, void *ctx);
#endif
