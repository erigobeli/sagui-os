/* webgl.c - WebGL 1 em software.
 *
 * Estado do GL, objetos (buffers, texturas, framebuffers, renderbuffers,
 * shaders, programas, vertex arrays), processamento de vértices pela VM do
 * glsl.c (4 vértices por vez), recorte nos planos near/far, rasterização de
 * triângulos (regra top-left, interpolação com correção de perspectiva),
 * linhas e pontos em quadrados de 2x2 pixels (as derivadas do fragment shader
 * vêm das vias vizinhas), teste de stencil e profundidade, blending e máscaras.
 *
 * Os buffers internos ficam na orientação do GL (linha 0 embaixo); a tela do
 * canvas recebe a imagem virada e sem pré-multiplicar só quando alguém a lê
 * (tela2d_sincronizar). Toda a interface com o JS passa por webgl_op. */
#include "sagui.h"
#include "cipo.h"
#include "glsl.h"

extern void (*tela2d_sincronizar)(int id);
uint32_t *tela2d_px_escrita(int id, int *l, int *a);
void tela2d_ler(int id, int x, int y, int w, int h, uint8_t *rgba);
const uint32_t *tela2d_pixels(int id, int *l, int *a);
void tela2d_mudou(void);

static float w_floor(float x) { if (x > 8388608.0f || x < -8388608.0f) return x; float i = (float)(int)x; return (x < 0 && i != x) ? i - 1 : i; }
static float w_abs(float x) { return x < 0 ? -x : x; }
static float w_log2(float x)
{
    if (x <= 0) return -126;
    union { float f; uint32_t u; } v; v.f = x;
    int e = (int)((v.u >> 23) & 255) - 127;
    v.u = (v.u & 0x7FFFFFu) | 0x3F800000u;
    float m = v.f;
    return (float)e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}
static int w_pot(int n) { return n > 0 && !(n & (n - 1)); }

/* ---------------------------------------------------------------- constantes do GL */
enum {
    GL_POINTS = 0, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN,
    GL_DEPTH_BUFFER_BIT = 0x100, GL_STENCIL_BUFFER_BIT = 0x400, GL_COLOR_BUFFER_BIT = 0x4000,
    GL_NEVER = 0x200, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS,
    GL_ZERO = 0, GL_ONE = 1, GL_SRC_COLOR = 0x300, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,
    GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE,
    GL_CONSTANT_COLOR = 0x8001, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA,
    GL_FUNC_ADD = 0x8006, GL_MIN_EXT = 0x8007, GL_MAX_EXT = 0x8008, GL_FUNC_SUBTRACT = 0x800A, GL_FUNC_REVERSE_SUBTRACT = 0x800B,
    GL_FRONT = 0x404, GL_BACK = 0x405, GL_FRONT_AND_BACK = 0x408, GL_CW = 0x900, GL_CCW = 0x901,
    GL_CULL_FACE = 0xB44, GL_DEPTH_TEST = 0xB71, GL_STENCIL_TEST = 0xB90, GL_DITHER = 0xBD0, GL_BLEND = 0xBE2,
    GL_SCISSOR_TEST = 0xC11, GL_POLYGON_OFFSET_FILL = 0x8037, GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809E, GL_SAMPLE_COVERAGE = 0x80A0,
    GL_NO_ERROR = 0, GL_INVALID_ENUM = 0x500, GL_INVALID_VALUE, GL_INVALID_OPERATION, GL_OUT_OF_MEMORY = 0x505, GL_INVALID_FRAMEBUFFER_OPERATION,
    GL_BYTE = 0x1400, GL_UNSIGNED_BYTE, GL_SHORT, GL_UNSIGNED_SHORT, GL_INT, GL_UNSIGNED_INT, GL_FLOAT, GL_HALF_FLOAT_OES = 0x8D61,
    GL_DEPTH_COMPONENT = 0x1902, GL_ALPHA = 0x1906, GL_RGB, GL_RGBA, GL_LUMINANCE, GL_LUMINANCE_ALPHA,
    GL_UNSIGNED_SHORT_4_4_4_4 = 0x8033, GL_UNSIGNED_SHORT_5_5_5_1 = 0x8034, GL_UNSIGNED_SHORT_5_6_5 = 0x8363,
    GL_KEEP = 0x1E00, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT = 0x150A, GL_INCR_WRAP = 0x8507, GL_DECR_WRAP,
    GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE = 0x8764, GL_BUFFER_USAGE, GL_STATIC_DRAW = 0x88E4,
    GL_TEXTURE_2D = 0xDE1, GL_TEXTURE_CUBE_MAP = 0x8513, GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515, GL_TEXTURE0 = 0x84C0,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T,
    GL_NEAREST = 0x2600, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST = 0x2700, GL_LINEAR_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR,
    GL_REPEAT = 0x2901, GL_CLAMP_TO_EDGE = 0x812F, GL_MIRRORED_REPEAT = 0x8370,
    GL_FRAMEBUFFER = 0x8D40, GL_RENDERBUFFER, GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_ATTACHMENT = 0x8D00, GL_STENCIL_ATTACHMENT = 0x8D20,
    GL_DEPTH_STENCIL_ATTACHMENT = 0x821A, GL_RGBA4 = 0x8056, GL_RGB5_A1, GL_RGB565 = 0x8D62, GL_DEPTH_COMPONENT16 = 0x81A5,
    GL_STENCIL_INDEX8 = 0x8D48, GL_DEPTH_STENCIL = 0x84F9,
    GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT, GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
    GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS = 0x8CD9, GL_FRAMEBUFFER_UNSUPPORTED = 0x8CDD,
    GL_FRAGMENT_SHADER = 0x8B30, GL_VERTEX_SHADER, GL_SHADER_TYPE = 0x8B4F, GL_DELETE_STATUS = 0x8B80, GL_COMPILE_STATUS, GL_LINK_STATUS,
    GL_VALIDATE_STATUS, GL_ATTACHED_SHADERS = 0x8B85, GL_ACTIVE_UNIFORMS, GL_ACTIVE_ATTRIBUTES = 0x8B89,
    GL_PACK_ALIGNMENT = 0xD05, GL_UNPACK_ALIGNMENT = 0xCF5, GL_UNPACK_FLIP_Y_WEBGL = 0x9240, GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL,
    GL_CONTEXT_LOST_WEBGL, GL_UNPACK_COLORSPACE_CONVERSION_WEBGL,
};

/* ---------------------------------------------------------------- objetos */
enum { O_BUF = 1, O_TEX, O_FB, O_RB, O_SH, O_PROG, O_VAO };
typedef struct { int l, a; uint8_t *px; } nivel_t;          /* RGBA8, linha 0 = primeira linha dos dados (t = 0) */
typedef struct { int ativo, tam, tipo, norm, stride, off, buf, div; } atrib_t;
typedef struct {
    int tipo; char *fonte; int nfonte; glsl_prog *p; char log[512]; int compilado, apagar, usos;
} shader_t;
typedef struct { char nome[64]; int sv, sf, elem, arr, base, lin, col, gltipo; } unif_t;
typedef struct { char nome[64]; int slot, tam, loc, lin, col, gltipo; } attr_t;
typedef struct {
    int vs, fs; glsl_prog *pv, *pf; float *mv, *mf; int ligado, validado, apagar, usos;
    char log[512];
    unif_t *u; int nu;
    attr_t at[16]; int na;
    struct { char nome[64]; int loc; } pedido[16]; int npedido;
    struct { int sv, sf, n; } mapa[32]; int nmapa, nvf;   /* varyings: VS slot -> índice na lista de floats -> FS slot */
    int vf_ini[32];
} prog_t;
typedef struct { atrib_t at[16]; int elem; } vao_t;
typedef struct {
    int tipo, ctx, vivo, alvo;   /* alvo: texture target ou buffer target do primeiro bind */
    /* buffer */ uint8_t *d; int n, uso;
    /* textura */ nivel_t nv[6][14]; int fmt[6][14]; int minf, magf, ws, wt, completa;   /* completa: 0 não sabe, 1 sim, 2 não */
    /* renderbuffer */ int rfmt, rl, ra; uint8_t *rcor; float *rz; uint8_t *rst;
    /* framebuffer */ int cor_tex, cor_face, cor_nivel, cor_rb, z_rb, st_rb, zst_rb;
    shader_t *sh; prog_t *pg; vao_t *va;
} gobj;
static gobj *objs; static int nobjs, cobjs;

static int obj_novo(int ctx, int tipo)
{
    for (int i = 1; i < nobjs; i++) if (!objs[i].tipo) { memset(&objs[i], 0, sizeof objs[i]); objs[i].tipo = tipo; objs[i].ctx = ctx; return i; }
    if (nobjs == 0) nobjs = 1;
    if (nobjs >= cobjs) {
        int nc = cobjs ? cobjs * 2 : 256;
        gobj *n = aloca((unsigned)nc * sizeof(gobj));
        if (!n) return 0;
        memset(n, 0, (unsigned)nc * sizeof(gobj));
        if (cobjs) { memcpy(n, objs, (unsigned)cobjs * sizeof(gobj)); libera(objs); }
        objs = n; cobjs = nc;
    }
    int i = nobjs++;
    memset(&objs[i], 0, sizeof objs[i]); objs[i].tipo = tipo; objs[i].ctx = ctx;
    return i;
}
static gobj *O(int ctx, int id, int tipo) { return id > 0 && id < nobjs && objs[id].tipo == tipo && objs[id].ctx == ctx ? &objs[id] : NULL; }

/* ---------------------------------------------------------------- contexto */
typedef struct {
    int usado, tela, l, a;
    int alfa, tem_z, tem_st, premult, preservar;
    uint8_t *cor; float *zb; uint8_t *sb;
    int sujo, erro, perdido;
    float cc[4], cz; int cs;
    int vp[4], sc[4]; float dr[2];
    int c_z, c_blend, c_cull, c_sc, c_st, c_poff, c_dither, c_sac, c_sc2;
    int bf[4], be[2]; float bcor[4];     /* src rgb, dst rgb, src a, dst a; eq rgb, eq a */
    int zfunc, zmask, cmask[4], cull, frente; float lw, pof, pou;
    int sfunc[2], sref[2], smask[2], swm[2], sfail[2], szfail[2], szpass[2];
    int flip_y, premult_up, conv_cor, al_unpack, al_pack;
    int prog, buf_arr, fb, rb, unidade, tex2d[32], texcube[32], vao;
    float atual[16][4];
    vao_t vao0;
    int estagio;                         /* 0 vértice, 1 fragmento (para o LOD) */
    int hint;
} gctx;
#define MAX_CTX 16
static gctx ctxs[MAX_CTX];
static gctx *C(int id) { return id >= 0 && id < MAX_CTX && ctxs[id].usado ? &ctxs[id] : NULL; }
static void erro_gl(gctx *c, int e) { if (!c->erro) c->erro = e; }
static vao_t *vao_atual(gctx *c, int id) { gobj *o = c->vao ? O(id, c->vao, O_VAO) : NULL; return o ? o->va : &c->vao0; }

static void sincronizar(int tela);
static void fb_padrao(gctx *c)
{
    if (c->cor) libera(c->cor);
    if (c->zb) libera(c->zb);
    if (c->sb) libera(c->sb);
    c->cor = NULL; c->zb = NULL; c->sb = NULL;
    unsigned n = (unsigned)(c->l * c->a);
    if (!n) return;
    c->cor = aloca(n * 4); if (c->cor) memset(c->cor, 0, n * 4);
    if (c->tem_z) { c->zb = aloca(n * 4); if (c->zb) for (unsigned i = 0; i < n; i++) c->zb[i] = 1; }
    if (c->tem_st) { c->sb = aloca(n); if (c->sb) memset(c->sb, 0, n); }
    c->sujo = 1;
}
static int criar(int tela, int l, int a, int alfa, int z, int st, int premult, int preservar)
{
    for (int i = 0; i < MAX_CTX; i++) if (!ctxs[i].usado) {
        gctx *c = &ctxs[i];
        memset(c, 0, sizeof *c);
        c->usado = 1; c->tela = tela; c->l = l; c->a = a;
        c->alfa = alfa; c->tem_z = z; c->tem_st = st; c->premult = premult; c->preservar = preservar;
        c->vp[2] = c->sc[2] = l; c->vp[3] = c->sc[3] = a; c->dr[1] = 1; c->cz = 1;
        c->bf[0] = c->bf[2] = GL_ONE; c->bf[1] = c->bf[3] = GL_ZERO; c->be[0] = c->be[1] = GL_FUNC_ADD;
        c->zfunc = GL_LESS; c->zmask = 1; c->cmask[0] = c->cmask[1] = c->cmask[2] = c->cmask[3] = 1;
        c->cull = GL_BACK; c->frente = GL_CCW; c->lw = 1; c->c_dither = 1;
        for (int f = 0; f < 2; f++) { c->sfunc[f] = GL_ALWAYS; c->smask[f] = c->swm[f] = 0xFF; c->sfail[f] = c->szfail[f] = c->szpass[f] = GL_KEEP; }
        c->al_unpack = c->al_pack = 4; c->conv_cor = 0x9244;
        for (int k = 0; k < 16; k++) c->atual[k][3] = 1;
        fb_padrao(c);
        tela2d_sincronizar = sincronizar;
        return i;
    }
    return -1;
}

/* ---------------------------------------------------------------- alvo de desenho (framebuffer atual) */
typedef struct { int l, a; uint8_t *cor; float *z; uint8_t *st; int ok; } alvo_t;
static int fb_status(gctx *c, int id, gobj *f, alvo_t *r)
{
    memset(r, 0, sizeof *r);
    int l = -1, a = -1, algum = 0;
    #define DIM(L, A) do { if (l < 0) { l = (L); a = (A); } else if (l != (L) || a != (A)) return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS; } while (0)
    if (f->cor_tex) {
        gobj *t = O(id, f->cor_tex, O_TEX);
        if (!t) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        nivel_t *nv = &t->nv[f->cor_face][f->cor_nivel];
        if (!nv->px || nv->l <= 0) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        int fm = t->fmt[f->cor_face][f->cor_nivel];
        if (fm != GL_RGBA && fm != GL_RGB) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        DIM(nv->l, nv->a); r->cor = nv->px; algum = 1;
    } else if (f->cor_rb) {
        gobj *b = O(id, f->cor_rb, O_RB);
        if (!b || !b->rcor) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        DIM(b->rl, b->ra); r->cor = b->rcor; algum = 1;
    }
    int nz = (f->z_rb != 0) + (f->st_rb != 0) + (f->zst_rb != 0);
    if (nz > 1) return GL_FRAMEBUFFER_UNSUPPORTED;
    if (f->z_rb) { gobj *b = O(id, f->z_rb, O_RB); if (!b || !b->rz || b->rst) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT; DIM(b->rl, b->ra); r->z = b->rz; algum = 1; }
    if (f->st_rb) { gobj *b = O(id, f->st_rb, O_RB); if (!b || !b->rst || b->rz) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT; DIM(b->rl, b->ra); r->st = b->rst; algum = 1; }
    if (f->zst_rb) { gobj *b = O(id, f->zst_rb, O_RB); if (!b || !b->rz || !b->rst) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT; DIM(b->rl, b->ra); r->z = b->rz; r->st = b->rst; algum = 1; }
    #undef DIM
    if (!algum) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    r->l = l; r->a = a; r->ok = 1;
    return GL_FRAMEBUFFER_COMPLETE;
}
static int alvo_atual(gctx *c, int id, alvo_t *r)
{
    if (!c->fb) { r->l = c->l; r->a = c->a; r->cor = c->cor; r->z = c->zb; r->st = c->sb; r->ok = c->cor != NULL; return r->ok; }
    gobj *f = O(id, c->fb, O_FB);
    if (!f || fb_status(c, id, f, r) != GL_FRAMEBUFFER_COMPLETE) { erro_gl(c, GL_INVALID_FRAMEBUFFER_OPERATION); return 0; }
    return 1;
}

/* ---------------------------------------------------------------- texturas */
static int tex_completa(gobj *t, int cubo)
{
    int faces = cubo ? 6 : 1;
    nivel_t *b = &t->nv[0][0];
    if (!b->px || b->l <= 0 || b->a <= 0) return 0;
    for (int f = 1; f < faces; f++) if (!t->nv[f][0].px || t->nv[f][0].l != b->l || t->nv[f][0].a != b->a) return 0;
    int mip = t->minf != GL_NEAREST && t->minf != GL_LINEAR;
    int pot = w_pot(b->l) && w_pot(b->a);
    if (!pot && (mip || t->ws != GL_CLAMP_TO_EDGE || t->wt != GL_CLAMP_TO_EDGE)) return 0;   /* regra do WebGL 1 para NPOT */
    if (mip) {
        int l = b->l, a = b->a;
        for (int k = 1; k < 14 && (l > 1 || a > 1); k++) {
            l = l > 1 ? l >> 1 : 1; a = a > 1 ? a >> 1 : 1;
            for (int f = 0; f < faces; f++) if (!t->nv[f][k].px || t->nv[f][k].l != l || t->nv[f][k].a != a) return 0;
        }
    }
    return 1;
}
static int envolver(int i, int n, int modo)
{
    if (modo == GL_CLAMP_TO_EDGE) return i < 0 ? 0 : i >= n ? n - 1 : i;
    if (modo == GL_MIRRORED_REPEAT) {
        int p = n * 2; int k = i % p; if (k < 0) k += p;
        return k < n ? k : p - 1 - k;
    }
    int k = i % n; return k < 0 ? k + n : k;
}
static void texel(const nivel_t *nv, int x, int y, float *o)
{
    const uint8_t *p = nv->px + ((unsigned)y * (unsigned)nv->l + (unsigned)x) * 4;
    o[0] += p[0]; o[1] += p[1]; o[2] += p[2]; o[3] += p[3];
}
static void amostra_nivel(const gobj *t, const nivel_t *nv, float u, float v, int linear, float *o)
{
    float r[4] = { 0, 0, 0, 0 };
    if (!linear) {
        int x = envolver((int)w_floor(u * (float)nv->l), nv->l, t->ws), y = envolver((int)w_floor(v * (float)nv->a), nv->a, t->wt);
        texel(nv, x, y, r);
    } else {
        float fx = u * (float)nv->l - 0.5f, fy = v * (float)nv->a - 0.5f;
        float x0f = w_floor(fx), y0f = w_floor(fy);
        float ax = fx - x0f, ay = fy - y0f;
        int x0 = (int)x0f, y0 = (int)y0f;
        int xa = envolver(x0, nv->l, t->ws), xb = envolver(x0 + 1, nv->l, t->ws);
        int ya = envolver(y0, nv->a, t->wt), yb = envolver(y0 + 1, nv->a, t->wt);
        float q[4][4]; memset(q, 0, sizeof q);
        texel(nv, xa, ya, q[0]); texel(nv, xb, ya, q[1]); texel(nv, xa, yb, q[2]); texel(nv, xb, yb, q[3]);
        for (int i = 0; i < 4; i++) r[i] = (q[0][i] * (1 - ax) + q[1][i] * ax) * (1 - ay) + (q[2][i] * (1 - ax) + q[3][i] * ax) * ay;
    }
    for (int i = 0; i < 4; i++) o[i] = r[i] * (1.0f / 255.0f);
}
static void amostra_tex(const gobj *t, int face, float u, float v, float lambda, float *o)
{
    const nivel_t *b = &t->nv[face][0];
    if (lambda <= 0 || t->minf == GL_NEAREST || t->minf == GL_LINEAR) {
        int lin = lambda <= 0 ? t->magf == GL_LINEAR : t->minf == GL_LINEAR || t->minf == GL_LINEAR_MIPMAP_NEAREST || t->minf == GL_LINEAR_MIPMAP_LINEAR;
        amostra_nivel(t, b, u, v, lin, o);
        return;
    }
    int maxn = 0; { int l = b->l, a = b->a; while (l > 1 || a > 1) { l = l > 1 ? l >> 1 : 1; a = a > 1 ? a >> 1 : 1; maxn++; } }
    if (maxn > 13) maxn = 13;
    if (lambda > (float)maxn) lambda = (float)maxn;
    int lin = t->minf == GL_LINEAR_MIPMAP_NEAREST || t->minf == GL_LINEAR_MIPMAP_LINEAR;
    if (t->minf == GL_NEAREST_MIPMAP_NEAREST || t->minf == GL_LINEAR_MIPMAP_NEAREST) {
        int d = (int)(lambda + 0.5f); if (d > maxn) d = maxn;
        amostra_nivel(t, &t->nv[face][d], u, v, lin, o);
        return;
    }
    int d0 = (int)w_floor(lambda), d1 = d0 + 1 > maxn ? maxn : d0 + 1; float f = lambda - (float)d0;
    float a[4], c[4];
    amostra_nivel(t, &t->nv[face][d0], u, v, lin, a);
    amostra_nivel(t, &t->nv[face][d1], u, v, lin, c);
    for (int i = 0; i < 4; i++) o[i] = a[i] * (1 - f) + c[i] * f;
}
static void cubo_face(float x, float y, float z, int *face, float *s, float *t)
{
    float ax = w_abs(x), ay = w_abs(y), az = w_abs(z), sc, tc, ma;
    if (ax >= ay && ax >= az) { ma = ax; if (x >= 0) { *face = 0; sc = -z; tc = -y; } else { *face = 1; sc = z; tc = -y; } }
    else if (ay >= az) { ma = ay; if (y >= 0) { *face = 2; sc = x; tc = z; } else { *face = 3; sc = x; tc = -z; } }
    else { ma = az; if (z >= 0) { *face = 4; sc = x; tc = -y; } else { *face = 5; sc = -x; tc = -y; } }
    if (ma == 0) ma = 1;
    *s = (sc / ma + 1) * 0.5f; *t = (tc / ma + 1) * 0.5f;
}
static void amostrar(void *ctx, int un, int cubo, const float *co, int nc, const float *bias, int lod, float *out)
{
    gctx *c = ctx; int id = (int)(c - ctxs);
    (void)nc;
    gobj *t = un >= 0 && un < 32 ? O(id, cubo ? c->texcube[un] : c->tex2d[un], O_TEX) : NULL;
    if (t && !t->completa) t->completa = tex_completa(t, cubo) ? 1 : 2;
    if (!t || t->completa != 1) { for (int v = 0; v < 4; v++) { out[v * 4] = out[v * 4 + 1] = out[v * 4 + 2] = 0; out[v * 4 + 3] = 1; } return; }
    float u[4], w[4]; int face[4];
    for (int v = 0; v < 4; v++) {
        if (cubo) cubo_face(co[v * 3], co[v * 3 + 1], co[v * 3 + 2], &face[v], &u[v], &w[v]);
        else { face[v] = 0; u[v] = co[v * 3]; w[v] = co[v * 3 + 1]; }
    }
    float lam[4];
    if (lod || c->estagio == 0) for (int v = 0; v < 4; v++) lam[v] = lod ? bias[v] : 0;
    else {
        /* derivadas do quadrado 2x2: vias 0 1 / 2 3 */
        float L = (float)t->nv[face[0]][0].l, A = (float)t->nv[face[0]][0].a;
        float dux = (u[1] - u[0]) * L, dvx = (w[1] - w[0]) * A, duy = (u[2] - u[0]) * L, dvy = (w[2] - w[0]) * A;
        float px = dux * dux + dvx * dvx, py = duy * duy + dvy * dvy;
        float rho2 = px > py ? px : py;
        float l0 = rho2 > 0 ? 0.5f * w_log2(rho2) : -100;
        for (int v = 0; v < 4; v++) lam[v] = l0 + bias[v];
    }
    for (int v = 0; v < 4; v++) amostra_tex(t, face[v], u[v], w[v], lam[v], &out[v * 4]);
}

/* ---------------------------------------------------------------- fragmentos */
typedef struct {
    int x[4], y[4], m;
    float z[4], w[4];            /* profundidade de janela e 1/w de clip */
    float var[4][64];
    float pc[4][2];
    int frente;
} quadw_t;
static int comparar(int f, float a, float b)
{
    switch (f) { case GL_NEVER: return 0; case GL_LESS: return a < b; case GL_EQUAL: return a == b; case GL_LEQUAL: return a <= b;
    case GL_GREATER: return a > b; case GL_NOTEQUAL: return a != b; case GL_GEQUAL: return a >= b; default: return 1; }
}
static int st_op(int op, int v, int ref)
{
    switch (op) {
    case GL_ZERO: return 0; case GL_REPLACE: return ref & 255; case GL_INCR: return v < 255 ? v + 1 : 255; case GL_DECR: return v > 0 ? v - 1 : 0;
    case GL_INVERT: return ~v & 255; case GL_INCR_WRAP: return (v + 1) & 255; case GL_DECR_WRAP: return (v - 1) & 255; default: return v;
    }
}
static float fator(gctx *c, int f, int i, const float *s, const float *d)
{
    switch (f) {
    case GL_ZERO: return 0; case GL_ONE: return 1;
    case GL_SRC_COLOR: return s[i]; case GL_ONE_MINUS_SRC_COLOR: return 1 - s[i];
    case GL_DST_COLOR: return d[i]; case GL_ONE_MINUS_DST_COLOR: return 1 - d[i];
    case GL_SRC_ALPHA: return s[3]; case GL_ONE_MINUS_SRC_ALPHA: return 1 - s[3];
    case GL_DST_ALPHA: return d[3]; case GL_ONE_MINUS_DST_ALPHA: return 1 - d[3];
    case GL_CONSTANT_COLOR: return c->bcor[i]; case GL_ONE_MINUS_CONSTANT_COLOR: return 1 - c->bcor[i];
    case GL_CONSTANT_ALPHA: return c->bcor[3]; case GL_ONE_MINUS_CONSTANT_ALPHA: return 1 - c->bcor[3];
    case GL_SRC_ALPHA_SATURATE: if (i == 3) return 1; { float a = s[3], b = 1 - d[3]; return a < b ? a : b; }
    }
    return 0;
}
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static uint8_t para8(float x) { x = clamp01(x); return (uint8_t)(int)(x * 255.0f + 0.5f); }

/* teste de stencil/profundidade de uma via; grava stencil/z; devolve 1 se passa */
static int testes(gctx *c, alvo_t *al, int idx, float z, int frente)
{
    int f = frente ? 0 : 1;
    if (c->c_st && al->st) {
        int v = al->st[idx], ref = c->sref[f] & 255, m = c->smask[f];
        if (!comparar(c->sfunc[f], (float)(ref & m), (float)(v & m))) {
            int nv = st_op(c->sfail[f], v, ref); al->st[idx] = (uint8_t)((v & ~c->swm[f]) | (nv & c->swm[f]));
            return 0;
        }
        int passa = !(c->c_z && al->z) || comparar(c->zfunc, z, al->z[idx]);
        int nv = st_op(passa ? c->szpass[f] : c->szfail[f], v, ref); al->st[idx] = (uint8_t)((v & ~c->swm[f]) | (nv & c->swm[f]));
        if (!passa) return 0;
    } else if (c->c_z && al->z && !comparar(c->zfunc, z, al->z[idx])) return 0;
    if (c->c_z && al->z && c->zmask) al->z[idx] = z;
    return 1;
}
static void escrever_cor(gctx *c, alvo_t *al, int idx, const float *s)
{
    uint8_t *p = al->cor + (unsigned)idx * 4;
    float r[4];
    if (c->c_blend) {
        float d[4] = { p[0] * (1.0f / 255), p[1] * (1.0f / 255), p[2] * (1.0f / 255), p[3] * (1.0f / 255) };
        float sc[4]; for (int i = 0; i < 4; i++) sc[i] = clamp01(s[i]);
        for (int i = 0; i < 4; i++) {
            int a = i == 3;
            int eq = c->be[a];
            if (eq == GL_MIN_EXT) { r[i] = sc[i] < d[i] ? sc[i] : d[i]; continue; }
            if (eq == GL_MAX_EXT) { r[i] = sc[i] > d[i] ? sc[i] : d[i]; continue; }
            float fs = fator(c, c->bf[a ? 2 : 0], i, sc, d), fd = fator(c, c->bf[a ? 3 : 1], i, sc, d);
            r[i] = eq == GL_FUNC_SUBTRACT ? sc[i] * fs - d[i] * fd : eq == GL_FUNC_REVERSE_SUBTRACT ? d[i] * fd - sc[i] * fs : sc[i] * fs + d[i] * fd;
        }
    } else for (int i = 0; i < 4; i++) r[i] = s[i];
    for (int i = 0; i < 4; i++) if (c->cmask[i]) p[i] = para8(r[i]);
}

static void sombrear(gctx *c, int id, prog_t *pg, alvo_t *al, quadw_t *q)
{
    glsl_prog *pf = pg->pf; float *m = pg->mf;
    int cobre = q->m;
    if (!cobre) return;
    /* teste de profundidade antecipado (sem discard e sem stencil, o resultado é o mesmo) */
    if (c->c_z && al->z && !c->c_st && !pf->usa_discard) {
        int k = 0;
        for (int v = 0; v < 4; v++) if (cobre & (1 << v)) { int idx = q->y[v] * al->l + q->x[v]; if (comparar(c->zfunc, q->z[v], al->z[idx])) k |= 1 << v; }
        cobre = k;
        if (!cobre) return;
    }
    for (int k = 0; k < pg->nmapa; k++) {
        int sf = pg->mapa[k].sf, n = pg->mapa[k].n, o = pg->vf_ini[k];
        for (int i = 0; i < n; i++) for (int v = 0; v < 4; v++) m[(sf + i) * 4 + v] = q->var[v][o + i];
    }
    if (pf->s_fragcoord >= 0) for (int v = 0; v < 4; v++) {
        m[pf->s_fragcoord * 4 + v] = (float)q->x[v] + 0.5f; m[(pf->s_fragcoord + 1) * 4 + v] = (float)q->y[v] + 0.5f;
        m[(pf->s_fragcoord + 2) * 4 + v] = q->z[v]; m[(pf->s_fragcoord + 3) * 4 + v] = q->w[v];
    }
    if (pf->s_frontfacing >= 0) for (int v = 0; v < 4; v++) m[pf->s_frontfacing * 4 + v] = (float)q->frente;
    if (pf->s_pointcoord >= 0) for (int v = 0; v < 4; v++) { m[pf->s_pointcoord * 4 + v] = q->pc[v][0]; m[(pf->s_pointcoord + 1) * 4 + v] = q->pc[v][1]; }
    if (pf->s_fragcolor >= 0) for (int i = 0; i < 4; i++) for (int v = 0; v < 4; v++) m[(pf->s_fragcolor + i) * 4 + v] = 0;
    c->estagio = 1;
    int vivo = glsl_rodar(pf, m, 15, amostrar, c);   /* todas as vias rodam (derivadas), só as cobertas gravam */
    cobre &= vivo;
    for (int v = 0; v < 4; v++) {
        if (!(cobre & (1 << v))) continue;
        int idx = q->y[v] * al->l + q->x[v];
        if (!testes(c, al, idx, q->z[v], q->frente)) continue;
        float s[4];
        for (int i = 0; i < 4; i++) s[i] = pf->s_fragcolor >= 0 ? m[(pf->s_fragcolor + i) * 4 + v] : 0;
        escrever_cor(c, al, idx, s);
    }
    (void)id;
}

/* ---------------------------------------------------------------- rasterização */
typedef struct { int x0, y0, x1, y1; } caixa_t;   /* [x0, x1) x [y0, y1) */
static caixa_t limites(gctx *c, alvo_t *al)
{
    caixa_t b = { 0, 0, al->l, al->a };
    int vx0 = c->vp[0], vy0 = c->vp[1], vx1 = c->vp[0] + c->vp[2], vy1 = c->vp[1] + c->vp[3];
    if (vx0 > b.x0) b.x0 = vx0;
    if (vy0 > b.y0) b.y0 = vy0;
    if (vx1 < b.x1) b.x1 = vx1;
    if (vy1 < b.y1) b.y1 = vy1;
    if (c->c_sc) {
        if (c->sc[0] > b.x0) b.x0 = c->sc[0];
        if (c->sc[1] > b.y0) b.y0 = c->sc[1];
        if (c->sc[0] + c->sc[2] < b.x1) b.x1 = c->sc[0] + c->sc[2];
        if (c->sc[1] + c->sc[3] < b.y1) b.y1 = c->sc[1] + c->sc[3];
    }
    return b;
}
/* vértice de janela: x, y, z, 1/w, varyings (já multiplicados por 1/w) */
typedef struct { float x, y, z, iw; float v[64]; } vj_t;

static void tri_raster(gctx *c, int id, prog_t *pg, alvo_t *al, caixa_t *cx, const vj_t *a, const vj_t *b, const vj_t *d, int frente)
{
    float area = (b->x - a->x) * (d->y - a->y) - (b->y - a->y) * (d->x - a->x);
    if (area == 0) return;
    const vj_t *v0 = a, *v1 = b, *v2 = d;
    if (area < 0) { v1 = d; v2 = b; area = -area; }
    float minx = v0->x, maxx = v0->x, miny = v0->y, maxy = v0->y;
    if (v1->x < minx) minx = v1->x; if (v1->x > maxx) maxx = v1->x; if (v2->x < minx) minx = v2->x; if (v2->x > maxx) maxx = v2->x;
    if (v1->y < miny) miny = v1->y; if (v1->y > maxy) maxy = v1->y; if (v2->y < miny) miny = v2->y; if (v2->y > maxy) maxy = v2->y;
    int x0 = (int)w_floor(minx), x1 = (int)w_floor(maxx) + 1, y0 = (int)w_floor(miny), y1 = (int)w_floor(maxy) + 1;
    if (x0 < cx->x0) x0 = cx->x0; if (y0 < cx->y0) y0 = cx->y0; if (x1 > cx->x1) x1 = cx->x1; if (y1 > cx->y1) y1 = cx->y1;
    if (x0 >= x1 || y0 >= y1) return;
    x0 &= ~1; y0 &= ~1;
    /* arestas: E(p) = A x + B y + C, positiva dentro */
    float A[3], B[3], Cc[3]; int tl[3];
    const vj_t *e[3][2] = { { v1, v2 }, { v2, v0 }, { v0, v1 } };
    for (int k = 0; k < 3; k++) {
        const vj_t *p = e[k][0], *q = e[k][1];
        A[k] = p->y - q->y; B[k] = q->x - p->x; Cc[k] = p->x * q->y - p->y * q->x;
        /* top-left (área positiva = anti-horário com y para cima): aresta de cima vai para a esquerda; esquerda desce */
        tl[k] = (A[k] == 0 && B[k] < 0) || A[k] > 0;
    }
    float ia = 1 / area;
    int nv = pg->nvf;
    /* deslocamento de polígono */
    float zoff = 0;
    if (c->c_poff) {
        float dzdx = ((v1->z - v0->z) * (v2->y - v0->y) - (v2->z - v0->z) * (v1->y - v0->y)) * ia;
        float dzdy = ((v2->z - v0->z) * (v1->x - v0->x) - (v1->z - v0->z) * (v2->x - v0->x)) * ia;
        float mx = w_abs(dzdx) > w_abs(dzdy) ? w_abs(dzdx) : w_abs(dzdy);
        zoff = c->pof * mx + c->pou * (1.0f / 16777216.0f);
    }
    quadw_t q; q.frente = frente;
    for (int y = y0; y < y1; y += 2)
        for (int x = x0; x < x1; x += 2) {
            q.m = 0;
            float bb[4][3];
            for (int v = 0; v < 4; v++) {
                int px = x + (v & 1), py = y + (v >> 1);
                q.x[v] = px; q.y[v] = py;
                float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
                int dentro = px >= cx->x0 && px < cx->x1 && py >= cx->y0 && py < cx->y1;
                for (int k = 0; k < 3; k++) {
                    float E = A[k] * fx + B[k] * fy + Cc[k];
                    bb[v][k] = E * ia;
                    if (E < 0 || (E == 0 && !tl[k])) dentro = 0;
                }
                if (dentro) q.m |= 1 << v;
            }
            if (!q.m) continue;
            for (int v = 0; v < 4; v++) {
                /* bb[0] pesa v0, bb[1] v1, bb[2] v2 */
                float l0 = bb[v][0], l1 = bb[v][1], l2 = bb[v][2];
                float z = l0 * v0->z + l1 * v1->z + l2 * v2->z + zoff;
                if ((q.m & (1 << v)) && (z < 0 || z > 1)) { if (z < -1e-6f || z > 1 + 1e-6f) q.m &= ~(1 << v); }
                q.z[v] = clamp01(z);
                float iw = l0 * v0->iw + l1 * v1->iw + l2 * v2->iw;
                q.w[v] = iw;
                float k = iw != 0 ? 1 / iw : 0;
                for (int i = 0; i < nv; i++) q.var[v][i] = (l0 * v0->v[i] + l1 * v1->v[i] + l2 * v2->v[i]) * k;
            }
            if (q.m) sombrear(c, id, pg, al, &q);
        }
}

/* clip: x y z w, ptsize, varyings */
#define VS_EXTRA 5
static int recortar(const float *const *ent, int n, float (*sai)[VS_EXTRA + 64], int S, int plano)
{
    /* plano 0: z + w >= 0 (near); 1: w - z >= 0 (far) */
    float tmp[8][VS_EXTRA + 64]; const float *in[8]; int k = 0;
    for (int i = 0; i < n; i++) in[i] = ent[i];
    for (int i = 0; i < n; i++) {
        const float *a = in[i], *b = in[(i + 1) % n];
        float da = plano ? a[3] - a[2] : a[3] + a[2], db = plano ? b[3] - b[2] : b[3] + b[2];
        if (da >= 0) { if (k < 8) memcpy(tmp[k++], a, (unsigned)S * 4); }
        if ((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            if (k < 8) { for (int j = 0; j < S; j++) tmp[k][j] = a[j] + (b[j] - a[j]) * t; k++; }
        }
    }
    for (int i = 0; i < k; i++) memcpy(sai[i], tmp[i], (unsigned)S * 4);
    return k;
}
static void para_janela(gctx *c, const float *s, int nv, vj_t *o)
{
    float w = s[3]; if (w < 1e-20f && w > -1e-20f) w = 1e-20f;
    float iw = 1 / w;
    float nx = s[0] * iw, ny = s[1] * iw, nz = s[2] * iw;
    o->x = (float)c->vp[0] + (nx + 1) * 0.5f * (float)c->vp[2];
    o->y = (float)c->vp[1] + (ny + 1) * 0.5f * (float)c->vp[3];
    o->z = c->dr[0] + (nz + 1) * 0.5f * (c->dr[1] - c->dr[0]);
    o->iw = iw;
    for (int i = 0; i < nv; i++) o->v[i] = s[VS_EXTRA + i] * iw;
}
static void triangulo(gctx *c, int id, prog_t *pg, alvo_t *al, caixa_t *cx, const float *a, const float *b, const float *d)
{
    int S = VS_EXTRA + pg->nvf;
    /* face e descarte pela área já projetada (o recorte não muda a orientação) */
    const float *in[3] = { a, b, d };
    int precisa = 0;
    for (int i = 0; i < 3; i++) if (in[i][3] + in[i][2] < 0 || in[i][3] - in[i][2] < 0) precisa = 1;
    float p1[8][VS_EXTRA + 64], p2[8][VS_EXTRA + 64];
    const float *poli[8]; int n = 3;
    for (int i = 0; i < 3; i++) poli[i] = in[i];
    if (precisa) {
        n = recortar(in, 3, p1, S, 0);
        for (int i = 0; i < n; i++) poli[i] = p1[i];
        n = recortar(poli, n, p2, S, 1);
        for (int i = 0; i < n; i++) poli[i] = p2[i];
        if (n < 3) return;
    }
    vj_t vj[8];
    for (int i = 0; i < n; i++) para_janela(c, poli[i], pg->nvf, &vj[i]);
    /* orientação pelo polígono inteiro */
    float area = 0;
    for (int i = 0; i < n; i++) { vj_t *p = &vj[i], *q = &vj[(i + 1) % n]; area += p->x * q->y - q->x * p->y; }
    if (area == 0) return;
    int ccw = area > 0;
    int frente = c->frente == GL_CCW ? ccw : !ccw;
    if (c->c_cull) {
        if (c->cull == GL_FRONT_AND_BACK) return;
        if (c->cull == GL_BACK && !frente) return;
        if (c->cull == GL_FRONT && frente) return;
    }
    for (int i = 1; i + 1 < n; i++) tri_raster(c, id, pg, al, cx, &vj[0], &vj[i], &vj[i + 1], frente);
}
/* fragmentos avulsos (linhas): junta de 4 em 4 */
typedef struct { quadw_t q; int n; } lote_t;
static void lote_fechar(gctx *c, int id, prog_t *pg, alvo_t *al, lote_t *l)
{
    if (!l->n) return;
    for (int v = l->n; v < 4; v++) { l->q.x[v] = l->q.x[0]; l->q.y[v] = l->q.y[0]; l->q.z[v] = l->q.z[0]; l->q.w[v] = l->q.w[0]; memcpy(l->q.var[v], l->q.var[0], sizeof l->q.var[0]); }
    l->q.m = (1 << l->n) - 1;
    sombrear(c, id, pg, al, &l->q);
    l->n = 0;
}
static void linha(gctx *c, int id, prog_t *pg, alvo_t *al, caixa_t *cx, const float *a, const float *b)
{
    int S = VS_EXTRA + pg->nvf;
    const float *in[2] = { a, b };
    float p[2][VS_EXTRA + 64];
    memcpy(p[0], a, (unsigned)S * 4); memcpy(p[1], b, (unsigned)S * 4);
    for (int pl = 0; pl < 2; pl++) {
        float da = pl ? p[0][3] - p[0][2] : p[0][3] + p[0][2], db = pl ? p[1][3] - p[1][2] : p[1][3] + p[1][2];
        if (da < 0 && db < 0) return;
        if (da < 0 || db < 0) {
            float t = da / (da - db);
            float m[VS_EXTRA + 64]; for (int j = 0; j < S; j++) m[j] = p[0][j] + (p[1][j] - p[0][j]) * t;
            memcpy(da < 0 ? p[0] : p[1], m, (unsigned)S * 4);
        }
    }
    (void)in;
    vj_t va, vb; para_janela(c, p[0], pg->nvf, &va); para_janela(c, p[1], pg->nvf, &vb);
    float dx = vb.x - va.x, dy = vb.y - va.y;
    float adx = w_abs(dx), ady = w_abs(dy);
    int passos = (int)(adx > ady ? adx : ady);
    if (passos < 1) passos = 1;
    lote_t l; l.n = 0; l.q.frente = 1;
    for (int k = 0; k < passos; k++) {
        float t = ((float)k + 0.5f) / (float)passos;
        float x = va.x + dx * t, y = va.y + dy * t;
        int px = (int)w_floor(x), py = (int)w_floor(y);
        if (px < cx->x0 || px >= cx->x1 || py < cx->y0 || py >= cx->y1) continue;
        float iw = va.iw + (vb.iw - va.iw) * t, kk = iw != 0 ? 1 / iw : 0;
        float z = va.z + (vb.z - va.z) * t;
        if (z < 0 || z > 1) continue;
        int v = l.n;
        l.q.x[v] = px; l.q.y[v] = py; l.q.z[v] = z; l.q.w[v] = iw;
        for (int i = 0; i < pg->nvf; i++) l.q.var[v][i] = (va.v[i] + (vb.v[i] - va.v[i]) * t) * kk;
        l.q.pc[v][0] = l.q.pc[v][1] = 0;
        if (++l.n == 4) lote_fechar(c, id, pg, al, &l);
    }
    lote_fechar(c, id, pg, al, &l);
}
static void ponto(gctx *c, int id, prog_t *pg, alvo_t *al, caixa_t *cx, const float *s)
{
    if (s[3] + s[2] < 0 || s[3] - s[2] < 0) return;
    vj_t v; para_janela(c, s, pg->nvf, &v);
    float tam = s[4]; if (tam < 1) tam = 1; if (tam > 256) tam = 256;
    float x0 = v.x - tam * 0.5f, y0 = v.y - tam * 0.5f;
    int ix0 = (int)w_floor(x0 + 0.5f), iy0 = (int)w_floor(y0 + 0.5f), ix1 = (int)w_floor(x0 + tam + 0.5f), iy1 = (int)w_floor(y0 + tam + 0.5f);
    if (ix0 < cx->x0) ix0 = cx->x0; if (iy0 < cx->y0) iy0 = cx->y0; if (ix1 > cx->x1) ix1 = cx->x1; if (iy1 > cx->y1) iy1 = cx->y1;
    if (ix0 >= ix1 || iy0 >= iy1) return;
    float k = v.iw != 0 ? 1 / v.iw : 0;
    quadw_t q; q.frente = 1;
    for (int y = iy0 & ~1; y < iy1; y += 2)
        for (int x = ix0 & ~1; x < ix1; x += 2) {
            q.m = 0;
            for (int l = 0; l < 4; l++) {
                int px = x + (l & 1), py = y + (l >> 1);
                q.x[l] = px; q.y[l] = py; q.z[l] = v.z; q.w[l] = v.iw;
                if (px >= ix0 && px < ix1 && py >= iy0 && py < iy1) q.m |= 1 << l;
                for (int i = 0; i < pg->nvf; i++) q.var[l][i] = v.v[i] * k;
                q.pc[l][0] = ((float)px + 0.5f - x0) / tam;
                q.pc[l][1] = 1 - ((float)py + 0.5f - y0) / tam;
            }
            if (q.m && v.z >= 0 && v.z <= 1) sombrear(c, id, pg, al, &q);
        }
}

/* ---------------------------------------------------------------- vértices */
static int tam_tipo_gl(int t) { return t == GL_BYTE || t == GL_UNSIGNED_BYTE ? 1 : t == GL_SHORT || t == GL_UNSIGNED_SHORT || t == GL_HALF_FLOAT_OES ? 2 : 4; }
static float ler_comp(const uint8_t *p, int tipo, int norm)
{
    switch (tipo) {
    case GL_FLOAT: { float f; memcpy(&f, p, 4); return f; }
    case GL_BYTE: { float f = (float)(int8_t)p[0]; if (norm) { f /= 127.0f; if (f < -1) f = -1; } return f; }
    case GL_UNSIGNED_BYTE: return norm ? p[0] / 255.0f : (float)p[0];
    case GL_SHORT: { int16_t s; memcpy(&s, p, 2); float f = (float)s; if (norm) { f /= 32767.0f; if (f < -1) f = -1; } return f; }
    case GL_UNSIGNED_SHORT: { uint16_t s; memcpy(&s, p, 2); return norm ? s / 65535.0f : (float)s; }
    }
    return 0;
}
static int buscar_atributo(gctx *c, int id, vao_t *va, int loc, int vert, int inst, float *o)
{
    atrib_t *a = &va->at[loc];
    o[0] = c->atual[loc][0]; o[1] = c->atual[loc][1]; o[2] = c->atual[loc][2]; o[3] = c->atual[loc][3];
    if (!a->ativo) return 1;
    gobj *b = O(id, a->buf, O_BUF);
    if (!b) return 0;
    int el = a->div ? inst / a->div : vert;
    int ts = tam_tipo_gl(a->tipo);
    int passo = a->stride ? a->stride : a->tam * ts;
    unsigned off = (unsigned)a->off + (unsigned)el * (unsigned)passo;
    if (off + (unsigned)(a->tam * ts) > (unsigned)b->n) return 0;
    o[0] = 0; o[1] = 0; o[2] = 0; o[3] = 1;
    for (int i = 0; i < a->tam; i++) o[i] = ler_comp(b->d + off + i * ts, a->tipo, a->norm);
    return 1;
}
/* roda o VS em até 4 vértices; saída: S floats por vértice */
static int sombrear_vertices(gctx *c, int id, prog_t *pg, vao_t *va, const int *verts, int n, int inst, float *saida)
{
    glsl_prog *pv = pg->pv; float *m = pg->mv;
    int S = VS_EXTRA + pg->nvf;
    for (int k = 0; k < pg->na; k++) {
        attr_t *at = &pg->at[k];
        int cols = at->col > 1 ? at->col : 1, por = at->col > 1 ? at->lin : at->tam;
        for (int cc = 0; cc < cols; cc++) {
            int loc = at->loc + cc;
            if (loc < 0 || loc >= 16) continue;
            for (int v = 0; v < 4; v++) {
                float val[4];
                if (!buscar_atributo(c, id, va, loc, verts[v < n ? v : 0], inst, val)) { erro_gl(c, GL_INVALID_OPERATION); return 0; }
                for (int i = 0; i < por; i++) m[(at->slot + cc * por + i) * 4 + v] = val[i];
            }
        }
    }
    if (pv->s_pos >= 0) for (int i = 0; i < 4; i++) for (int v = 0; v < 4; v++) m[(pv->s_pos + i) * 4 + v] = 0;
    if (pv->s_ptsize >= 0) for (int v = 0; v < 4; v++) m[pv->s_ptsize * 4 + v] = 1;
    c->estagio = 0;
    glsl_rodar(pv, m, (1 << n) - 1, amostrar, c);
    for (int v = 0; v < n; v++) {
        float *o = saida + v * S;
        for (int i = 0; i < 4; i++) o[i] = pv->s_pos >= 0 ? m[(pv->s_pos + i) * 4 + v] : 0;
        o[4] = pv->s_ptsize >= 0 ? m[pv->s_ptsize * 4 + v] : 1;
        for (int k = 0; k < pg->nmapa; k++) {
            int sv = pg->mapa[k].sv, nn = pg->mapa[k].n, oo = pg->vf_ini[k];
            for (int i = 0; i < nn; i++) o[VS_EXTRA + oo + i] = m[(sv + i) * 4 + v];
        }
    }
    return 1;
}

static void desenhar(gctx *c, int id, int modo, int first, int count, int itipo, int ioff, int instancias)
{
    if (modo < 0 || modo > 6) { erro_gl(c, GL_INVALID_ENUM); return; }
    if (count < 0 || first < 0 || instancias < 0) { erro_gl(c, GL_INVALID_VALUE); return; }
    gobj *po = O(id, c->prog, O_PROG);
    if (!po || !po->pg->ligado) { erro_gl(c, GL_INVALID_OPERATION); return; }
    prog_t *pg = po->pg;
    alvo_t al; if (!alvo_atual(c, id, &al)) return;
    if (!count || !instancias) return;
    vao_t *va = vao_atual(c, id);
    const uint8_t *ind = NULL;
    if (itipo) {
        gobj *eb = O(id, va->elem, O_BUF);
        int ts = itipo == GL_UNSIGNED_BYTE ? 1 : itipo == GL_UNSIGNED_SHORT ? 2 : 4;
        if (!eb) { erro_gl(c, GL_INVALID_OPERATION); return; }
        if (ioff % ts) { erro_gl(c, GL_INVALID_OPERATION); return; }
        if ((unsigned)ioff + (unsigned)count * (unsigned)ts > (unsigned)eb->n) { erro_gl(c, GL_INVALID_OPERATION); return; }
        ind = eb->d + ioff;
    }
    int S = VS_EXTRA + pg->nvf;
    /* índices -> vértices únicos */
    int *lista = aloca((unsigned)count * sizeof(int));
    if (!lista) { erro_gl(c, GL_OUT_OF_MEMORY); return; }
    int mn = 0x7FFFFFFF, mx = -1;
    for (int i = 0; i < count; i++) {
        int k;
        if (!ind) k = first + i;
        else if (itipo == GL_UNSIGNED_BYTE) k = ind[i];
        else if (itipo == GL_UNSIGNED_SHORT) { uint16_t s; memcpy(&s, ind + i * 2, 2); k = s; }
        else { uint32_t u; memcpy(&u, ind + i * 4, 4); k = (int)(u & 0x7FFFFFFF); }
        lista[i] = k; if (k < mn) mn = k; if (k > mx) mx = k;
    }
    int faixa = mx - mn + 1;
    int *mapa = aloca((unsigned)faixa * sizeof(int));
    float *saida = NULL; int *unicos = NULL;
    if (!mapa) { libera(lista); erro_gl(c, GL_OUT_OF_MEMORY); return; }
    for (int i = 0; i < faixa; i++) mapa[i] = -1;
    int nu = 0;
    unicos = aloca((unsigned)(count + 4) * sizeof(int));
    if (unicos) for (int i = 0; i < count; i++) if (mapa[lista[i] - mn] < 0) { mapa[lista[i] - mn] = nu; unicos[nu++] = lista[i]; }
    saida = unicos ? aloca((unsigned)(nu + 4) * (unsigned)S * 4) : NULL;
    if (!saida) { if (unicos) libera(unicos); libera(mapa); libera(lista); erro_gl(c, GL_OUT_OF_MEMORY); return; }
    caixa_t cx = limites(c, &al);
    for (int inst = 0; inst < instancias; inst++) {
        int ok = 1;
        for (int i = 0; i < nu && ok; i += 4) ok = sombrear_vertices(c, id, pg, va, &unicos[i], nu - i < 4 ? nu - i : 4, inst, saida + i * S);
        if (!ok) break;
        if (cx.x0 >= cx.x1 || cx.y0 >= cx.y1) continue;
        #define V(i) (saida + mapa[lista[i] - mn] * S)
        switch (modo) {
        case GL_TRIANGLES: for (int i = 0; i + 2 < count; i += 3) triangulo(c, id, pg, &al, &cx, V(i), V(i + 1), V(i + 2)); break;
        case GL_TRIANGLE_STRIP: for (int i = 0; i + 2 < count; i++) { if (i & 1) triangulo(c, id, pg, &al, &cx, V(i + 1), V(i), V(i + 2)); else triangulo(c, id, pg, &al, &cx, V(i), V(i + 1), V(i + 2)); } break;
        case GL_TRIANGLE_FAN: for (int i = 1; i + 1 < count; i++) triangulo(c, id, pg, &al, &cx, V(0), V(i), V(i + 1)); break;
        case GL_LINES: for (int i = 0; i + 1 < count; i += 2) linha(c, id, pg, &al, &cx, V(i), V(i + 1)); break;
        case GL_LINE_STRIP: for (int i = 0; i + 1 < count; i++) linha(c, id, pg, &al, &cx, V(i), V(i + 1)); break;
        case GL_LINE_LOOP: for (int i = 0; i + 1 < count; i++) linha(c, id, pg, &al, &cx, V(i), V(i + 1)); if (count > 1) linha(c, id, pg, &al, &cx, V(count - 1), V(0)); break;
        case GL_POINTS: for (int i = 0; i < count; i++) ponto(c, id, pg, &al, &cx, V(i)); break;
        }
        #undef V
    }
    libera(saida); libera(unicos); libera(mapa); libera(lista);
    if (!c->fb) { c->sujo = 1; tela2d_mudou(); }
}

static void limpar(gctx *c, int id, int mask)
{
    if (mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) { erro_gl(c, GL_INVALID_VALUE); return; }
    alvo_t al; if (!alvo_atual(c, id, &al)) return;
    caixa_t b = { 0, 0, al.l, al.a };
    if (c->c_sc) {
        if (c->sc[0] > b.x0) b.x0 = c->sc[0];
        if (c->sc[1] > b.y0) b.y0 = c->sc[1];
        if (c->sc[0] + c->sc[2] < b.x1) b.x1 = c->sc[0] + c->sc[2];
        if (c->sc[1] + c->sc[3] < b.y1) b.y1 = c->sc[1] + c->sc[3];
    }
    uint8_t cv[4] = { para8(c->cc[0]), para8(c->cc[1]), para8(c->cc[2]), para8(c->cc[3]) };
    float z = clamp01(c->cz);
    for (int y = b.y0; y < b.y1; y++)
        for (int x = b.x0; x < b.x1; x++) {
            int idx = y * al.l + x;
            if ((mask & GL_COLOR_BUFFER_BIT) && al.cor) { uint8_t *p = al.cor + idx * 4; for (int i = 0; i < 4; i++) if (c->cmask[i]) p[i] = cv[i]; }
            if ((mask & GL_DEPTH_BUFFER_BIT) && al.z && c->zmask) al.z[idx] = z;
            if ((mask & GL_STENCIL_BUFFER_BIT) && al.st) al.st[idx] = (uint8_t)((al.st[idx] & ~c->swm[0]) | (c->cs & c->swm[0]));
        }
    if (!c->fb) { c->sujo = 1; tela2d_mudou(); }
}

/* ---------------------------------------------------------------- apresentação */
static void sincronizar(int tela)
{
    for (int i = 0; i < MAX_CTX; i++) {
        gctx *c = &ctxs[i];
        if (!c->usado || c->tela != tela || !c->sujo || !c->cor) continue;
        int l, a; uint32_t *px = tela2d_px_escrita(tela, &l, &a);
        c->sujo = 0;
        if (!px) continue;
        int L = l < c->l ? l : c->l, A = a < c->a ? a : c->a;
        for (int y = 0; y < A; y++) {
            const uint8_t *s = c->cor + (unsigned)(c->a - 1 - y) * (unsigned)c->l * 4;
            uint32_t *d = px + (unsigned)y * (unsigned)l;
            for (int x = 0; x < L; x++, s += 4) {
                unsigned r = s[0], g = s[1], b = s[2], al = c->alfa ? s[3] : 255;
                if (c->alfa && c->premult && al < 255) {
                    if (!al) r = g = b = 0;
                    else { r = r * 255 / al; g = g * 255 / al; b = b * 255 / al; if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; }
                }
                d[x] = (al << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

/* ---------------------------------------------------------------- envio de imagens */
static void para_rgba(const uint8_t *s, int fmt, int tipo, uint8_t *o)
{
    if (tipo == GL_UNSIGNED_SHORT_5_6_5) { unsigned v = (unsigned)s[0] | (unsigned)s[1] << 8; o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63); o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255; return; }
    if (tipo == GL_UNSIGNED_SHORT_4_4_4_4) { unsigned v = (unsigned)s[0] | (unsigned)s[1] << 8; o[0] = (uint8_t)(((v >> 12) & 15) * 17); o[1] = (uint8_t)(((v >> 8) & 15) * 17); o[2] = (uint8_t)(((v >> 4) & 15) * 17); o[3] = (uint8_t)((v & 15) * 17); return; }
    if (tipo == GL_UNSIGNED_SHORT_5_5_5_1) { unsigned v = (unsigned)s[0] | (unsigned)s[1] << 8; o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 6) & 31) * 255 / 31); o[2] = (uint8_t)(((v >> 1) & 31) * 255 / 31); o[3] = (v & 1) ? 255 : 0; return; }
    uint8_t c[4];
    if (tipo == GL_FLOAT) { for (int i = 0; i < 4; i++) { float f; memcpy(&f, s + i * 4, 4); c[i] = para8(f); } }
    else for (int i = 0; i < 4; i++) c[i] = s[i];
    switch (fmt) {
    case GL_ALPHA: o[0] = o[1] = o[2] = 0; o[3] = c[0]; break;
    case GL_LUMINANCE: o[0] = o[1] = o[2] = c[0]; o[3] = 255; break;
    case GL_LUMINANCE_ALPHA: o[0] = o[1] = o[2] = c[0]; o[3] = c[1]; break;
    case GL_RGB: o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = 255; break;
    default: o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3];
    }
}
static int bytes_px(int fmt, int tipo)
{
    if (tipo == GL_UNSIGNED_SHORT_5_6_5 || tipo == GL_UNSIGNED_SHORT_4_4_4_4 || tipo == GL_UNSIGNED_SHORT_5_5_5_1) return 2;
    int n = fmt == GL_RGBA ? 4 : fmt == GL_RGB ? 3 : fmt == GL_LUMINANCE_ALPHA ? 2 : 1;
    return tipo == GL_FLOAT ? n * 4 : n;
}
static int fmt_valido(int fmt, int tipo)
{
    if (fmt != GL_ALPHA && fmt != GL_RGB && fmt != GL_RGBA && fmt != GL_LUMINANCE && fmt != GL_LUMINANCE_ALPHA) return 0;
    if (tipo == GL_UNSIGNED_BYTE || tipo == GL_FLOAT) return 1;
    if (tipo == GL_UNSIGNED_SHORT_5_6_5) return fmt == GL_RGB;
    if (tipo == GL_UNSIGNED_SHORT_4_4_4_4 || tipo == GL_UNSIGNED_SHORT_5_5_5_1) return fmt == GL_RGBA;
    return 0;
}
static gobj *tex_alvo(gctx *c, int id, int alvo, int *face)
{
    int cubo = alvo >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && alvo < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6;
    if (alvo != GL_TEXTURE_2D && !cubo) { erro_gl(c, GL_INVALID_ENUM); return NULL; }
    *face = cubo ? alvo - GL_TEXTURE_CUBE_MAP_POSITIVE_X : 0;
    gobj *t = O(id, cubo ? c->texcube[c->unidade] : c->tex2d[c->unidade], O_TEX);
    if (!t) erro_gl(c, GL_INVALID_OPERATION);
    return t;
}
/* grava um retângulo RGBA8 (origem: linhas de cima para baixo, como os dados chegam) */
static void gravar_rgba(gctx *c, nivel_t *nv, int x0, int y0, int w, int h, const uint8_t *rgba)
{
    for (int y = 0; y < h; y++) {
        int sy = c->flip_y ? h - 1 - y : y;
        const uint8_t *s = rgba + (unsigned)sy * (unsigned)w * 4;
        uint8_t *d = nv->px + ((unsigned)(y0 + y) * (unsigned)nv->l + (unsigned)x0) * 4;
        for (int x = 0; x < w; x++, s += 4, d += 4) {
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            if (c->premult_up && s[3] < 255) { d[0] = (uint8_t)(s[0] * s[3] / 255); d[1] = (uint8_t)(s[1] * s[3] / 255); d[2] = (uint8_t)(s[2] * s[3] / 255); }
        }
    }
}
static int nivel_alocar(gobj *t, int face, int lv, int w, int h, int fmt)
{
    t->completa = 0;
    nivel_t *nv = &t->nv[face][lv];
    if (nv->px && (nv->l != w || nv->a != h)) { libera(nv->px); nv->px = NULL; }
    if (!nv->px && w > 0 && h > 0) { nv->px = aloca((unsigned)(w * h) * 4); if (!nv->px) return 0; }
    nv->l = w; nv->a = h; t->fmt[face][lv] = fmt;
    if (nv->px) memset(nv->px, 0, (unsigned)(w * h) * 4);
    return 1;
}
/* dados de um ArrayBufferView -> RGBA8 de cima para baixo */
static uint8_t *converter(gctx *c, const uint8_t *d, int n, int w, int h, int fmt, int tipo)
{
    int bp = bytes_px(fmt, tipo);
    int linha = w * bp, al = c->al_unpack;
    int passo = (linha + al - 1) / al * al;
    if (h > 0 && (long)passo * (h - 1) + linha > n) return NULL;
    uint8_t *o = aloca((unsigned)(w * h) * 4 + 4);
    if (!o) return NULL;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) para_rgba(d + y * passo + x * bp, fmt, tipo, o + (y * w + x) * 4);
    return o;
}
/* nível base a partir dos níveis acima (generateMipmap) */
static void mipmaps(gobj *t, int faces)
{
    t->completa = 0;
    for (int f = 0; f < faces; f++) {
        int lv = 0;
        while (lv < 13 && (t->nv[f][lv].l > 1 || t->nv[f][lv].a > 1)) {
            nivel_t *s = &t->nv[f][lv];
            int w = s->l > 1 ? s->l >> 1 : 1, h = s->a > 1 ? s->a >> 1 : 1;
            if (!nivel_alocar(t, f, lv + 1, w, h, t->fmt[f][0])) return;
            nivel_t *d = &t->nv[f][lv + 1];
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                int x0 = x * 2, y0 = y * 2, x1 = x0 + 1 < s->l ? x0 + 1 : x0, y1 = y0 + 1 < s->a ? y0 + 1 : y0;
                for (int i = 0; i < 4; i++) {
                    unsigned v = s->px[(y0 * s->l + x0) * 4 + i] + s->px[(y0 * s->l + x1) * 4 + i] + s->px[(y1 * s->l + x0) * 4 + i] + s->px[(y1 * s->l + x1) * 4 + i];
                    d->px[(y * w + x) * 4 + i] = (uint8_t)((v + 2) / 4);
                }
            }
            lv++;
        }
    }
}

/* ---------------------------------------------------------------- shaders e programas */
static void prog_limpar(prog_t *p)
{
    if (p->mv) libera(p->mv);
    if (p->mf) libera(p->mf);
    if (p->u) libera(p->u);
    p->mv = p->mf = NULL; p->u = NULL; p->nu = 0; p->na = 0; p->nmapa = 0; p->nvf = 0; p->ligado = 0;
    p->pv = p->pf = NULL;
}
static int tipo_gl(int base, int lin, int col)
{
    if (base == 4) return 0x8B5E;
    if (base == 5) return 0x8B60;
    if (col > 1) return lin == 2 ? 0x8B5A : lin == 3 ? 0x8B5B : 0x8B5C;
    if (base == 1) return lin == 1 ? GL_FLOAT : 0x8B50 + lin - 2;
    if (base == 2) return lin == 1 ? GL_INT : 0x8B53 + lin - 2;
    return lin == 1 ? 0x8B56 : 0x8B57 + lin - 2;
}
static void copiar_nome(char *d, const char *s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int nome_igual(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void ligar(gctx *c, int id, gobj *po)
{
    prog_t *p = po->pg;
    /* os shaders anexados continuam valendo mesmo se mudarem depois; guardamos os programas compilados atuais */
    prog_limpar(p);
    p->log[0] = 0;
    gobj *vs = O(id, p->vs, O_SH), *fs = O(id, p->fs, O_SH);
    if (!vs || !fs) { copiar_nome(p->log, "falta vertex ou fragment shader", sizeof p->log); return; }
    if (!vs->sh->compilado || !fs->sh->compilado || !vs->sh->p || !fs->sh->p) { copiar_nome(p->log, "shader nao compilado", sizeof p->log); return; }
    /* cada programa tem a própria cópia compilada (as memórias de uniform são dele) */
    char erro[256];
    p->pv = glsl_compilar(vs->sh->fonte, vs->sh->nfonte, 0, erro, sizeof erro);
    p->pf = glsl_compilar(fs->sh->fonte, fs->sh->nfonte, 1, erro, sizeof erro);
    if (!p->pv || !p->pf) { copiar_nome(p->log, erro, sizeof p->log); if (p->pv) glsl_liberar(p->pv); if (p->pf) glsl_liberar(p->pf); p->pv = p->pf = NULL; return; }
    glsl_prog *pv = p->pv, *pf = p->pf;
    /* varyings: cada um do FS precisa existir no VS com o mesmo tipo */
    for (int i = 0; i < pf->nvary; i++) {
        glsl_var *f = &pf->vary[i], *v = NULL;
        for (int k = 0; k < pv->nvary; k++) if (nome_igual(pv->vary[k].nome, f->nome)) v = &pv->vary[k];
        if (!v) continue;   /* declarado e não escrito pelo VS: só é erro se o FS usar; fica indefinido (0) */
        if (v->base != f->base || v->lin != f->lin || v->col != f->col || v->arr != f->arr) {
            formatar_texto(p->log, sizeof p->log, "varying %s com tipos diferentes", f->nome); goto falhou;
        }
        if (p->nmapa >= 32 || p->nvf + f->tam > 64) { copiar_nome(p->log, "varyings demais", sizeof p->log); goto falhou; }
        p->mapa[p->nmapa].sv = v->slot; p->mapa[p->nmapa].sf = f->slot; p->mapa[p->nmapa].n = f->tam;
        p->vf_ini[p->nmapa] = p->nvf; p->nvf += f->tam; p->nmapa++;
    }
    /* uniforms: junta os dos dois estágios pelo nome */
    p->u = aloca((unsigned)(pv->nunif + pf->nunif + 1) * sizeof(unif_t));
    if (!p->u) goto falhou;
    for (int e = 0; e < 2; e++) {
        glsl_prog *g = e ? pf : pv;
        for (int i = 0; i < g->nunif; i++) {
            glsl_var *v = &g->unif[i];
            unif_t *u = NULL;
            for (int k = 0; k < p->nu; k++) if (nome_igual(p->u[k].nome, v->nome)) u = &p->u[k];
            if (u) {
                if (u->base != v->base || u->lin != v->lin || u->col != v->col || u->arr != v->arr) { formatar_texto(p->log, sizeof p->log, "uniform %s com tipos diferentes", v->nome); goto falhou; }
            } else {
                u = &p->u[p->nu++]; memset(u, 0, sizeof *u);
                copiar_nome(u->nome, v->nome, sizeof u->nome);
                u->sv = u->sf = -1; u->elem = v->elem; u->arr = v->arr; u->base = v->base; u->lin = v->lin; u->col = v->col;
                u->gltipo = tipo_gl(v->base, v->lin, v->col);
            }
            if (e) u->sf = v->slot; else u->sv = v->slot;
        }
    }
    /* atributos: locais pedidos primeiro, depois os livres */
    {
        int usado = 0;
        p->na = 0;
        for (int i = 0; i < pv->nattr && p->na < 16; i++) {
            glsl_var *v = &pv->attr[i]; attr_t *a = &p->at[p->na++];
            memset(a, 0, sizeof *a);
            copiar_nome(a->nome, v->nome, sizeof a->nome);
            a->slot = v->slot; a->tam = v->tam; a->lin = v->lin; a->col = v->col; a->loc = -1; a->gltipo = tipo_gl(v->base, v->lin, v->col);
            for (int k = 0; k < p->npedido; k++) if (nome_igual(p->pedido[k].nome, a->nome)) a->loc = p->pedido[k].loc;
            if (a->loc >= 0) { int nl = a->col > 1 ? a->col : 1; for (int k = 0; k < nl; k++) usado |= 1 << (a->loc + k); }
        }
        for (int i = 0; i < p->na; i++) {
            attr_t *a = &p->at[i];
            if (a->loc >= 0) continue;
            int nl = a->col > 1 ? a->col : 1;
            for (int l = 0; l + nl <= 16; l++) {
                int livre = 1; for (int k = 0; k < nl; k++) if (usado & (1 << (l + k))) livre = 0;
                if (livre) { a->loc = l; for (int k = 0; k < nl; k++) usado |= 1 << (l + k); break; }
            }
            if (a->loc < 0) { copiar_nome(p->log, "atributos demais", sizeof p->log); goto falhou; }
        }
    }
    p->mv = aloca((unsigned)(pv->nslots + 1) * 16); p->mf = aloca((unsigned)(pf->nslots + 1) * 16);
    if (!p->mv || !p->mf) goto falhou;
    memset(p->mv, 0, (unsigned)(pv->nslots + 1) * 16); memset(p->mf, 0, (unsigned)(pf->nslots + 1) * 16);
    p->ligado = 1;
    return;
falhou:
    if (p->pv) glsl_liberar(p->pv);
    if (p->pf) glsl_liberar(p->pf);
    prog_limpar(p);
    (void)c;
}
/* local de uniform: índice + elemento do array (número para o JS; -1 = não há) */
static int uniform_local(prog_t *p, const char *nome)
{
    char base[64]; int el = 0, i = 0;
    while (nome[i] && nome[i] != '[' && i < 63) { base[i] = nome[i]; i++; }
    base[i] = 0;
    if (nome[i] == '[') { el = 0; i++; while (nome[i] >= '0' && nome[i] <= '9') el = el * 10 + (nome[i++] - '0'); if (nome[i] != ']') return -1; i++; }
    const char *resto = nome + i;   /* ".campo" depois de [i] em arrays de struct */
    for (int k = 0; k < p->nu; k++) {
        unif_t *u = &p->u[k];
        if (*resto) {   /* nome[i].campo: o uniform registrado é exatamente esse */
            if (nome_igual(u->nome, nome)) return k * 4096;
            continue;
        }
        if (nome_igual(u->nome, base)) {
            if (el && (!u->arr || el >= u->arr)) return -1;
            return k * 4096 + el;
        }
        /* "a[0]" vale como "a" para arrays; o nome registrado pode já ter [i] (struct) */
        if (nome_igual(u->nome, nome)) return k * 4096;
    }
    return -1;
}

/* ---------------------------------------------------------------- consultas de estado */
static int param(gctx *c, int id, int p, double *o)
{
    alvo_t al; memset(&al, 0, sizeof al);
    switch (p) {
    case 0x0BA2: for (int i = 0; i < 4; i++) o[i] = c->vp[i]; return 4;                 /* VIEWPORT */
    case 0x0C10: for (int i = 0; i < 4; i++) o[i] = c->sc[i]; return 4;                 /* SCISSOR_BOX */
    case 0x0C22: for (int i = 0; i < 4; i++) o[i] = c->cc[i]; return 4;                 /* COLOR_CLEAR_VALUE */
    case 0x0C23: for (int i = 0; i < 4; i++) o[i] = c->cmask[i]; return 4;              /* COLOR_WRITEMASK */
    case 0x8005: for (int i = 0; i < 4; i++) o[i] = c->bcor[i]; return 4;               /* BLEND_COLOR */
    case 0x0B70: o[0] = c->dr[0]; o[1] = c->dr[1]; return 2;                             /* DEPTH_RANGE */
    case 0x846D: o[0] = 1; o[1] = 256; return 2;                                         /* ALIASED_POINT_SIZE_RANGE */
    case 0x846E: o[0] = 1; o[1] = 1; return 2;                                           /* ALIASED_LINE_WIDTH_RANGE */
    case 0x0D3A: o[0] = 4096; o[1] = 4096; return 2;                                     /* MAX_VIEWPORT_DIMS */
    case 0x0B73: o[0] = c->cz; return 1; case 0x0B91: o[0] = c->cs; return 1;
    case 0x0B72: o[0] = c->zmask; return 1; case 0x0B74: o[0] = c->zfunc; return 1;
    case 0x0B45: o[0] = c->cull; return 1; case 0x0B46: o[0] = c->frente; return 1; case 0x0B21: o[0] = c->lw; return 1;
    case 0x8038: o[0] = c->pof; return 1; case 0x2A00: o[0] = c->pou; return 1;
    case 0x80C9: o[0] = c->bf[0]; return 1; case 0x80C8: o[0] = c->bf[1]; return 1; case 0x80CB: o[0] = c->bf[2]; return 1; case 0x80CA: o[0] = c->bf[3]; return 1;
    case 0x8009: o[0] = c->be[0]; return 1; case 0x883D: o[0] = c->be[1]; return 1;
    case 0x0B92: o[0] = c->sfunc[0]; return 1; case 0x0B93: o[0] = c->smask[0]; return 1; case 0x0B97: o[0] = c->sref[0]; return 1;
    case 0x0B94: o[0] = c->sfail[0]; return 1; case 0x0B95: o[0] = c->szfail[0]; return 1; case 0x0B96: o[0] = c->szpass[0]; return 1; case 0x0B98: o[0] = c->swm[0]; return 1;
    case 0x8800: o[0] = c->sfunc[1]; return 1; case 0x8CA4: o[0] = c->smask[1]; return 1; case 0x8CA3: o[0] = c->sref[1]; return 1;
    case 0x8801: o[0] = c->sfail[1]; return 1; case 0x8802: o[0] = c->szfail[1]; return 1; case 0x8803: o[0] = c->szpass[1]; return 1; case 0x8CA5: o[0] = c->swm[1]; return 1;
    case 0x84E0: o[0] = GL_TEXTURE0 + c->unidade; return 1;
    case 0x0D33: o[0] = 4096; return 1;            /* MAX_TEXTURE_SIZE */
    case 0x851C: o[0] = 4096; return 1;            /* MAX_CUBE_MAP_TEXTURE_SIZE */
    case 0x84E8: o[0] = 4096; return 1;            /* MAX_RENDERBUFFER_SIZE */
    case 0x8869: o[0] = 16; return 1;              /* MAX_VERTEX_ATTRIBS */
    case 0x8DFB: o[0] = 256; return 1;             /* MAX_VERTEX_UNIFORM_VECTORS */
    case 0x8DFC: o[0] = 15; return 1;              /* MAX_VARYING_VECTORS */
    case 0x8B4D: o[0] = 32; return 1;              /* MAX_COMBINED_TEXTURE_IMAGE_UNITS */
    case 0x8B4C: o[0] = 16; return 1;              /* MAX_VERTEX_TEXTURE_IMAGE_UNITS */
    case 0x8872: o[0] = 16; return 1;              /* MAX_TEXTURE_IMAGE_UNITS */
    case 0x8DFD: o[0] = 256; return 1;             /* MAX_FRAGMENT_UNIFORM_VECTORS */
    case 0x0D50: case 0x0D51: case 0x0D52: case 0x0D53: alvo_atual(c, id, &al); o[0] = al.cor ? 8 : 0; return 1;   /* RED/GREEN/BLUE/ALPHA_BITS */
    case 0x0D56: alvo_atual(c, id, &al); o[0] = al.z ? 24 : 0; return 1;    /* DEPTH_BITS */
    case 0x0D57: alvo_atual(c, id, &al); o[0] = al.st ? 8 : 0; return 1;    /* STENCIL_BITS */
    case 0x0D55: o[0] = 0; return 1; case 0x80A9: o[0] = 0; return 1; case 0x80A8: o[0] = 0; return 1;   /* SUBPIXEL... SAMPLES, SAMPLE_BUFFERS */
    case 0x0D50 + 0x100: return 0;
    case 0x8B8D: o[0] = c->prog; return 1;         /* CURRENT_PROGRAM (o JS troca pelo objeto) */
    case GL_UNPACK_ALIGNMENT: o[0] = c->al_unpack; return 1; case GL_PACK_ALIGNMENT: o[0] = c->al_pack; return 1;
    case GL_UNPACK_FLIP_Y_WEBGL: o[0] = c->flip_y; return 1; case GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL: o[0] = c->premult_up; return 1;
    case GL_UNPACK_COLORSPACE_CONVERSION_WEBGL: o[0] = c->conv_cor; return 1;
    case 0x8B9A: o[0] = GL_RGBA; return 1; case 0x8B9B: o[0] = GL_UNSIGNED_BYTE; return 1;   /* IMPLEMENTATION_COLOR_READ_FORMAT/TYPE */
    case 0x86A3: return 0;                          /* COMPRESSED_TEXTURE_FORMATS: vazio */
    case 0x8192: o[0] = c->hint ? c->hint : 0x1100; return 1;   /* GENERATE_MIPMAP_HINT */
    case GL_BLEND: o[0] = c->c_blend; return 1; case GL_CULL_FACE: o[0] = c->c_cull; return 1; case GL_DEPTH_TEST: o[0] = c->c_z; return 1;
    case GL_DITHER: o[0] = c->c_dither; return 1; case GL_POLYGON_OFFSET_FILL: o[0] = c->c_poff; return 1; case GL_SCISSOR_TEST: o[0] = c->c_sc; return 1;
    case GL_STENCIL_TEST: o[0] = c->c_st; return 1; case GL_SAMPLE_ALPHA_TO_COVERAGE: o[0] = c->c_sac; return 1; case GL_SAMPLE_COVERAGE: o[0] = c->c_sc2; return 1;
    case 0x80AA: o[0] = 1; return 1; case 0x80AB: o[0] = 0; return 1;   /* SAMPLE_COVERAGE_VALUE/INVERT */
    }
    return -1;
}
static int *capacidade(gctx *c, int cap)
{
    switch (cap) {
    case GL_BLEND: return &c->c_blend; case GL_CULL_FACE: return &c->c_cull; case GL_DEPTH_TEST: return &c->c_z; case GL_DITHER: return &c->c_dither;
    case GL_POLYGON_OFFSET_FILL: return &c->c_poff; case GL_SCISSOR_TEST: return &c->c_sc; case GL_STENCIL_TEST: return &c->c_st;
    case GL_SAMPLE_ALPHA_TO_COVERAGE: return &c->c_sac; case GL_SAMPLE_COVERAGE: return &c->c_sc2;
    }
    return NULL;
}
static int eh_fator(int f) { return f == 0 || f == 1 || (f >= 0x300 && f <= 0x308) || (f >= 0x8001 && f <= 0x8004); }
static int eh_eq(int e) { return e == GL_FUNC_ADD || e == GL_FUNC_SUBTRACT || e == GL_FUNC_REVERSE_SUBTRACT || e == GL_MIN_EXT || e == GL_MAX_EXT; }
static int eh_cmp(int f) { return f >= GL_NEVER && f <= GL_ALWAYS; }
static int eh_stop(int o) { return o == GL_KEEP || o == GL_ZERO || o == GL_REPLACE || o == GL_INCR || o == GL_DECR || o == GL_INVERT || o == GL_INCR_WRAP || o == GL_DECR_WRAP; }

static void obj_apagar(gctx *c, int id, int k)
{
    if (k <= 0 || k >= nobjs || objs[k].ctx != id || !objs[k].tipo) return;
    gobj *o = &objs[k];
    switch (o->tipo) {
    case O_BUF:
        if (o->d) libera(o->d);
        if (c->buf_arr == k) c->buf_arr = 0;
        for (int i = 0; i < 16; i++) if (c->vao0.at[i].buf == k) c->vao0.at[i].buf = 0;
        if (c->vao0.elem == k) c->vao0.elem = 0;
        { vao_t *va = vao_atual(c, id); if (va != &c->vao0) { for (int i = 0; i < 16; i++) if (va->at[i].buf == k) va->at[i].buf = 0; if (va->elem == k) va->elem = 0; } }
        break;
    case O_TEX:
        for (int f = 0; f < 6; f++) for (int l = 0; l < 14; l++) if (o->nv[f][l].px) libera(o->nv[f][l].px);
        for (int u = 0; u < 32; u++) { if (c->tex2d[u] == k) c->tex2d[u] = 0; if (c->texcube[u] == k) c->texcube[u] = 0; }
        if (c->fb) { gobj *f = O(id, c->fb, O_FB); if (f && f->cor_tex == k) f->cor_tex = 0; }
        break;
    case O_RB:
        if (o->rcor) libera(o->rcor);
        if (o->rz) libera(o->rz);
        if (o->rst) libera(o->rst);
        if (c->rb == k) c->rb = 0;
        if (c->fb) { gobj *f = O(id, c->fb, O_FB); if (f) { if (f->cor_rb == k) f->cor_rb = 0; if (f->z_rb == k) f->z_rb = 0; if (f->st_rb == k) f->st_rb = 0; if (f->zst_rb == k) f->zst_rb = 0; } }
        break;
    case O_FB: if (c->fb == k) c->fb = 0; break;
    case O_SH:
        if (o->sh->usos > 0) { o->sh->apagar = 1; return; }   /* apaga de verdade quando desanexar */
        if (o->sh->fonte) libera(o->sh->fonte);
        if (o->sh->p) glsl_liberar(o->sh->p);
        libera(o->sh);
        break;
    case O_PROG:
        if (c->prog == k) { o->pg->apagar = 1; return; }
        if (o->pg->pv) glsl_liberar(o->pg->pv);
        if (o->pg->pf) glsl_liberar(o->pg->pf);
        prog_limpar(o->pg);
        { gobj *v = O(id, o->pg->vs, O_SH), *f = O(id, o->pg->fs, O_SH); if (v) v->sh->usos--; if (f) f->sh->usos--; }
        libera(o->pg);
        break;
    case O_VAO: if (c->vao == k) c->vao = 0; libera(o->va); break;
    }
    memset(o, 0, sizeof *o);
}
static void ctx_liberar(int id)
{
    gctx *c = C(id);
    if (!c) return;
    c->prog = 0;
    for (int k = 1; k < nobjs; k++) if (objs[k].tipo && objs[k].ctx == id) { if (objs[k].tipo == O_SH) objs[k].sh->usos = 0; obj_apagar(c, id, k); }
    for (int k = 1; k < nobjs; k++) if (objs[k].tipo && objs[k].ctx == id) obj_apagar(c, id, k);
    if (c->cor) libera(c->cor);
    if (c->zb) libera(c->zb);
    if (c->sb) libera(c->sb);
    memset(c, 0, sizeof *c);
}
void webgl_liberar_todos(void) { for (int i = 0; i < MAX_CTX; i++) if (ctxs[i].usado) ctx_liberar(i); }

/* ---------------------------------------------------------------- interface com o JS
 * a: argumentos numéricos; dados/ndados: bytes (ou floats em uniform/vertexAttrib*v);
 * txt: texto; o: números de volta; s: texto de volta.
 * Devolve: -1 indefinido, -2 texto em s, -3 null, n >= 0 números em o (eh_arr diz se é lista). */
#define A(i) ((i) < na ? a[i] : 0)
#define I(i) ((int)A(i))
int webgl_op(int op, const double *a, int na, void *dados, int ndados, const char *txt, double *o, int maxo, char *s, int maxs, int *eh_arr)
{
    *eh_arr = 0;
    if (op == 0) { o[0] = criar(I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7)); return 1; }
    int id = I(0);
    gctx *c = C(id);
    if (!c) return -1;
    a++; na--;   /* a[0] era o contexto */
    switch (op) {
    case 1: {   /* tamanho */
        int l = I(0), h = I(1); if (l < 1) l = 1; if (h < 1) h = 1; if (l > 4096) l = 4096; if (h > 4096) h = 4096;
        if ((long)l * h > 4096L * 2048) h = 4096 * 2048 / l;
        c->l = l; c->a = h; fb_padrao(c);
        return -1;
    }
    case 2: ctx_liberar(id); return -1;
    case 3: o[0] = c->erro; c->erro = 0; return 1;
    case 4: case 5: { int *p = capacidade(c, I(0)); if (!p) erro_gl(c, GL_INVALID_ENUM); else *p = op == 4; return -1; }
    case 6: { int *p = capacidade(c, I(0)); if (!p) { erro_gl(c, GL_INVALID_ENUM); o[0] = 0; } else o[0] = *p; return 1; }
    case 7: if (I(2) < 0 || I(3) < 0) { erro_gl(c, GL_INVALID_VALUE); return -1; } for (int i = 0; i < 4; i++) c->vp[i] = I(i); if (c->vp[2] > 4096) c->vp[2] = 4096; if (c->vp[3] > 4096) c->vp[3] = 4096; return -1;
    case 8: if (I(2) < 0 || I(3) < 0) { erro_gl(c, GL_INVALID_VALUE); return -1; } for (int i = 0; i < 4; i++) c->sc[i] = I(i); return -1;
    case 9: for (int i = 0; i < 4; i++) c->cc[i] = (float)A(i); return -1;
    case 10: c->cz = clamp01((float)A(0)); return -1;
    case 11: c->cs = I(0); return -1;
    case 12: limpar(c, id, I(0)); return -1;
    case 13: for (int i = 0; i < 4; i++) c->cmask[i] = I(i) != 0; return -1;
    case 14: c->zmask = I(0) != 0; return -1;
    case 15: if (!eh_cmp(I(0))) erro_gl(c, GL_INVALID_ENUM); else c->zfunc = I(0); return -1;
    case 16: c->dr[0] = clamp01((float)A(0)); c->dr[1] = clamp01((float)A(1)); if (c->dr[0] > c->dr[1]) erro_gl(c, GL_INVALID_OPERATION); return -1;
    case 17: if (I(0) != GL_FRONT && I(0) != GL_BACK && I(0) != GL_FRONT_AND_BACK) erro_gl(c, GL_INVALID_ENUM); else c->cull = I(0); return -1;
    case 18: if (I(0) != GL_CW && I(0) != GL_CCW) erro_gl(c, GL_INVALID_ENUM); else c->frente = I(0); return -1;
    case 19: if (A(0) <= 0) erro_gl(c, GL_INVALID_VALUE); else c->lw = (float)A(0); return -1;
    case 20: c->pof = (float)A(0); c->pou = (float)A(1); return -1;
    case 21: if (!eh_fator(I(0)) || !eh_fator(I(1))) { erro_gl(c, GL_INVALID_ENUM); return -1; } c->bf[0] = c->bf[2] = I(0); c->bf[1] = c->bf[3] = I(1); return -1;
    case 22: for (int i = 0; i < 4; i++) if (!eh_fator(I(i))) { erro_gl(c, GL_INVALID_ENUM); return -1; } for (int i = 0; i < 4; i++) c->bf[i] = I(i); return -1;
    case 23: if (!eh_eq(I(0))) { erro_gl(c, GL_INVALID_ENUM); return -1; } c->be[0] = c->be[1] = I(0); return -1;
    case 24: if (!eh_eq(I(0)) || !eh_eq(I(1))) { erro_gl(c, GL_INVALID_ENUM); return -1; } c->be[0] = I(0); c->be[1] = I(1); return -1;
    case 25: for (int i = 0; i < 4; i++) c->bcor[i] = clamp01((float)A(i)); return -1;
    case 26: case 27: {
        int f = op == 26 ? GL_FRONT_AND_BACK : I(0), k = op == 26 ? 0 : 1;
        if (!eh_cmp(I(k))) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        for (int i = 0; i < 2; i++) if (f == GL_FRONT_AND_BACK || (i == 0 && f == GL_FRONT) || (i == 1 && f == GL_BACK)) { c->sfunc[i] = I(k); c->sref[i] = I(k + 1); c->smask[i] = I(k + 2) & 255; }
        return -1;
    }
    case 28: case 29: {
        int f = op == 28 ? GL_FRONT_AND_BACK : I(0), k = op == 28 ? 0 : 1;
        if (!eh_stop(I(k)) || !eh_stop(I(k + 1)) || !eh_stop(I(k + 2))) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        for (int i = 0; i < 2; i++) if (f == GL_FRONT_AND_BACK || (i == 0 && f == GL_FRONT) || (i == 1 && f == GL_BACK)) { c->sfail[i] = I(k); c->szfail[i] = I(k + 1); c->szpass[i] = I(k + 2); }
        return -1;
    }
    case 30: c->swm[0] = c->swm[1] = I(0) & 255; return -1;
    case 31: { int f = I(0); for (int i = 0; i < 2; i++) if (f == GL_FRONT_AND_BACK || (i == 0 && f == GL_FRONT) || (i == 1 && f == GL_BACK)) c->swm[i] = I(1) & 255; return -1; }
    case 32: {
        int p = I(0), v = I(1);
        if (p == GL_UNPACK_ALIGNMENT || p == GL_PACK_ALIGNMENT) { if (v != 1 && v != 2 && v != 4 && v != 8) { erro_gl(c, GL_INVALID_VALUE); return -1; } if (p == GL_UNPACK_ALIGNMENT) c->al_unpack = v; else c->al_pack = v; }
        else if (p == GL_UNPACK_FLIP_Y_WEBGL) c->flip_y = v != 0;
        else if (p == GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL) c->premult_up = v != 0;
        else if (p == GL_UNPACK_COLORSPACE_CONVERSION_WEBGL) c->conv_cor = v;
        else erro_gl(c, GL_INVALID_ENUM);
        return -1;
    }
    /* buffers */
    case 33: o[0] = obj_novo(id, O_BUF); return 1;
    case 34: { gobj *b = O(id, I(0), O_BUF); if (b) obj_apagar(c, id, I(0)); return -1; }
    case 35: {
        int t = I(0), k = I(1);
        if (t != GL_ARRAY_BUFFER && t != GL_ELEMENT_ARRAY_BUFFER) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (k) { gobj *b = O(id, k, O_BUF); if (!b) { erro_gl(c, GL_INVALID_OPERATION); return -1; } if (b->alvo && b->alvo != t) { erro_gl(c, GL_INVALID_OPERATION); return -1; } b->alvo = t; }
        if (t == GL_ARRAY_BUFFER) c->buf_arr = k; else vao_atual(c, id)->elem = k;
        return -1;
    }
    case 36: case 37: {
        int t = I(0);
        int k = t == GL_ARRAY_BUFFER ? c->buf_arr : t == GL_ELEMENT_ARRAY_BUFFER ? vao_atual(c, id)->elem : -1;
        if (k < 0) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        gobj *b = O(id, k, O_BUF);
        if (!b) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        if (op == 36) {
            int n = dados ? ndados : I(1);
            if (n < 0) { erro_gl(c, GL_INVALID_VALUE); return -1; }
            uint8_t *d = aloca((unsigned)n + 4);
            if (!d) { erro_gl(c, GL_OUT_OF_MEMORY); return -1; }
            if (dados) memcpy(d, dados, (unsigned)n); else memset(d, 0, (unsigned)n);
            if (b->d) libera(b->d);
            b->d = d; b->n = n; b->uso = I(2);
        } else {
            int off = I(1);
            if (off < 0 || !dados || off + ndados > b->n) { erro_gl(c, GL_INVALID_VALUE); return -1; }
            memcpy(b->d + off, dados, (unsigned)ndados);
        }
        return -1;
    }
    /* texturas */
    case 38: { int k = obj_novo(id, O_TEX); if (k) { objs[k].minf = GL_NEAREST_MIPMAP_LINEAR; objs[k].magf = GL_LINEAR; objs[k].ws = objs[k].wt = GL_REPEAT; } o[0] = k; return 1; }
    case 39: { if (O(id, I(0), O_TEX)) obj_apagar(c, id, I(0)); return -1; }
    case 40: {
        int t = I(0), k = I(1);
        if (t != GL_TEXTURE_2D && t != GL_TEXTURE_CUBE_MAP) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (k) { gobj *x = O(id, k, O_TEX); if (!x) { erro_gl(c, GL_INVALID_OPERATION); return -1; } if (x->alvo && x->alvo != t) { erro_gl(c, GL_INVALID_OPERATION); return -1; } x->alvo = t; x->completa = 0; }
        if (t == GL_TEXTURE_2D) c->tex2d[c->unidade] = k; else c->texcube[c->unidade] = k;
        return -1;
    }
    case 41: { int u = I(0) - GL_TEXTURE0; if (u < 0 || u >= 32) erro_gl(c, GL_INVALID_ENUM); else c->unidade = u; return -1; }
    case 42: {
        int t = I(0), p = I(1), v = I(2);
        gobj *x = O(id, t == GL_TEXTURE_2D ? c->tex2d[c->unidade] : t == GL_TEXTURE_CUBE_MAP ? c->texcube[c->unidade] : 0, O_TEX);
        if (t != GL_TEXTURE_2D && t != GL_TEXTURE_CUBE_MAP) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (!x) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        x->completa = 0;
        if (p == GL_TEXTURE_MIN_FILTER && ((v >= GL_NEAREST_MIPMAP_NEAREST && v <= GL_LINEAR_MIPMAP_LINEAR) || v == GL_NEAREST || v == GL_LINEAR)) x->minf = v;
        else if (p == GL_TEXTURE_MAG_FILTER && (v == GL_NEAREST || v == GL_LINEAR)) x->magf = v;
        else if ((p == GL_TEXTURE_WRAP_S || p == GL_TEXTURE_WRAP_T) && (v == GL_REPEAT || v == GL_CLAMP_TO_EDGE || v == GL_MIRRORED_REPEAT)) { if (p == GL_TEXTURE_WRAP_S) x->ws = v; else x->wt = v; }
        else if (p == 0x84FE) { /* anisotropia: aceita e ignora */ }
        else erro_gl(c, GL_INVALID_ENUM);
        return -1;
    }
    case 43: case 45: {   /* texImage2D: 43 de dados (t, nível, ifmt, w, h, borda, fmt, tipo); 45 de tela (t, nível, ifmt, fmt, tipo, tela) */
        int face, lv = I(1), ifmt = I(2), w, h, fmt, tipo;
        if (op == 43) { w = I(3); h = I(4); fmt = I(6); tipo = I(7); if (I(5)) { erro_gl(c, GL_INVALID_VALUE); return -1; } }
        else { fmt = I(3); tipo = I(4); w = h = 0; tela2d_pixels(I(5), &w, &h); }
        gobj *t = tex_alvo(c, id, I(0), &face);
        if (!t) return -1;
        if (lv < 0 || lv > 13 || w < 0 || h < 0 || w > 4096 || h > 4096) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        if (ifmt != fmt || !fmt_valido(fmt, tipo)) { erro_gl(c, fmt_valido(fmt, tipo) ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return -1; }
        if (t->alvo == GL_TEXTURE_CUBE_MAP && w != h) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        if (!nivel_alocar(t, face, lv, w, h, fmt)) { erro_gl(c, GL_OUT_OF_MEMORY); return -1; }
        if (!w || !h) return -1;
        uint8_t *rgba = NULL;
        if (op == 43) { if (dados) { rgba = converter(c, dados, ndados, w, h, fmt, tipo); if (!rgba) { erro_gl(c, GL_INVALID_OPERATION); return -1; } } }
        else {
            rgba = aloca((unsigned)(w * h) * 4); if (!rgba) { erro_gl(c, GL_OUT_OF_MEMORY); return -1; }
            tela2d_ler(I(5), 0, 0, w, h, rgba);
            /* o formato pedido descarta canais (RGB: alfa 1; LUMINANCE: só o vermelho) */
            for (int i = 0; i < w * h; i++) { uint8_t q[4]; para_rgba(rgba + i * 4, fmt == GL_LUMINANCE_ALPHA ? GL_RGBA : fmt, GL_UNSIGNED_BYTE, q);
                if (fmt == GL_LUMINANCE_ALPHA) { q[1] = q[2] = q[0]; } if (fmt == GL_ALPHA) { q[3] = rgba[i * 4 + 3]; q[0] = q[1] = q[2] = 0; } memcpy(rgba + i * 4, q, 4); }
        }
        if (rgba) { gravar_rgba(c, &t->nv[face][lv], 0, 0, w, h, rgba); libera(rgba); }
        return -1;
    }
    case 44: case 46: {   /* texSubImage2D: 44 (t, nível, x, y, w, h, fmt, tipo) dados; 46 (t, nível, x, y, fmt, tipo, tela) */
        int face, lv = I(1), x = I(2), y = I(3), w, h, fmt, tipo;
        if (op == 44) { w = I(4); h = I(5); fmt = I(6); tipo = I(7); } else { fmt = I(4); tipo = I(5); w = h = 0; tela2d_pixels(I(6), &w, &h); }
        gobj *t = tex_alvo(c, id, I(0), &face);
        if (!t) return -1;
        if (lv < 0 || lv > 13) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        nivel_t *nv = &t->nv[face][lv];
        if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > nv->l || y + h > nv->a) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        if (!fmt_valido(fmt, tipo)) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (!w || !h) return -1;
        uint8_t *rgba;
        if (op == 44) { if (!dados) { erro_gl(c, GL_INVALID_VALUE); return -1; } rgba = converter(c, dados, ndados, w, h, fmt, tipo); if (!rgba) { erro_gl(c, GL_INVALID_OPERATION); return -1; } }
        else { rgba = aloca((unsigned)(w * h) * 4); if (!rgba) { erro_gl(c, GL_OUT_OF_MEMORY); return -1; } tela2d_ler(I(6), 0, 0, w, h, rgba); }
        gravar_rgba(c, nv, x, y, w, h, rgba); libera(rgba);
        return -1;
    }
    case 47: {
        int t = I(0);
        gobj *x = O(id, t == GL_TEXTURE_2D ? c->tex2d[c->unidade] : t == GL_TEXTURE_CUBE_MAP ? c->texcube[c->unidade] : 0, O_TEX);
        if (!x) { erro_gl(c, t == GL_TEXTURE_2D || t == GL_TEXTURE_CUBE_MAP ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return -1; }
        nivel_t *b = &x->nv[0][0];
        if (!b->px || !w_pot(b->l) || !w_pot(b->a)) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        mipmaps(x, t == GL_TEXTURE_CUBE_MAP ? 6 : 1);
        return -1;
    }
    case 48: case 49: {   /* copyTexImage2D(t, nível, ifmt, x, y, w, h, borda) / copyTexSubImage2D(t, nível, xo, yo, x, y, w, h) */
        int face; gobj *t = tex_alvo(c, id, I(0), &face);
        if (!t) return -1;
        alvo_t al; if (!alvo_atual(c, id, &al)) return -1;
        int lv = I(1), xo = 0, yo = 0, x, y, w, h;
        if (op == 48) { x = I(3); y = I(4); w = I(5); h = I(6); if (w < 0 || h < 0 || lv < 0 || lv > 13) { erro_gl(c, GL_INVALID_VALUE); return -1; } if (!nivel_alocar(t, face, lv, w, h, I(2))) { erro_gl(c, GL_OUT_OF_MEMORY); return -1; } }
        else { xo = I(2); yo = I(3); x = I(4); y = I(5); w = I(6); h = I(7); }
        nivel_t *nv = &t->nv[face][lv];
        if (xo < 0 || yo < 0 || xo + w > nv->l || yo + h > nv->a) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
            int sx = x + i, sy = y + j;
            uint8_t *d = nv->px + ((yo + j) * nv->l + xo + i) * 4;
            if (sx < 0 || sy < 0 || sx >= al.l || sy >= al.a || !al.cor) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
            memcpy(d, al.cor + (sy * al.l + sx) * 4, 4);
            if (op == 48 && I(2) == GL_RGB) d[3] = 255;
        }
        return -1;
    }
    /* framebuffers e renderbuffers */
    case 50: o[0] = obj_novo(id, O_FB); return 1;
    case 51: { if (O(id, I(0), O_FB)) obj_apagar(c, id, I(0)); return -1; }
    case 52: { if (I(0) != GL_FRAMEBUFFER) { erro_gl(c, GL_INVALID_ENUM); return -1; } if (I(1) && !O(id, I(1), O_FB)) { erro_gl(c, GL_INVALID_OPERATION); return -1; } c->fb = I(1); return -1; }
    case 53: {   /* framebufferTexture2D(t, att, textarget, tex, nível) */
        gobj *f = O(id, c->fb, O_FB);
        if (I(0) != GL_FRAMEBUFFER) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (!f) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        int tt = I(2), face = tt == GL_TEXTURE_2D ? 0 : tt - GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        if (face < 0 || face > 5) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (I(1) == GL_COLOR_ATTACHMENT0) { f->cor_tex = I(3); f->cor_face = face; f->cor_nivel = I(4) >= 0 && I(4) < 14 ? I(4) : 0; f->cor_rb = 0; }
        else if (I(1) == GL_DEPTH_ATTACHMENT || I(1) == GL_STENCIL_ATTACHMENT || I(1) == GL_DEPTH_STENCIL_ATTACHMENT) { if (I(3)) erro_gl(c, GL_INVALID_OPERATION); }   /* sem WEBGL_depth_texture */
        else erro_gl(c, GL_INVALID_ENUM);
        return -1;
    }
    case 54: {   /* framebufferRenderbuffer(t, att, rbt, rb) */
        gobj *f = O(id, c->fb, O_FB);
        if (I(0) != GL_FRAMEBUFFER || I(2) != GL_RENDERBUFFER) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (!f) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        int k = I(3);
        switch (I(1)) {
        case GL_COLOR_ATTACHMENT0: f->cor_rb = k; f->cor_tex = 0; break;
        case GL_DEPTH_ATTACHMENT: f->z_rb = k; f->zst_rb = 0; break;
        case GL_STENCIL_ATTACHMENT: f->st_rb = k; f->zst_rb = 0; break;
        case GL_DEPTH_STENCIL_ATTACHMENT: f->zst_rb = k; f->z_rb = f->st_rb = 0; break;
        default: erro_gl(c, GL_INVALID_ENUM);
        }
        return -1;
    }
    case 55: {
        if (I(0) != GL_FRAMEBUFFER) { erro_gl(c, GL_INVALID_ENUM); o[0] = 0; return 1; }
        if (!c->fb) { o[0] = GL_FRAMEBUFFER_COMPLETE; return 1; }
        alvo_t al; o[0] = fb_status(c, id, O(id, c->fb, O_FB), &al); return 1;
    }
    case 56: o[0] = obj_novo(id, O_RB); return 1;
    case 57: { if (O(id, I(0), O_RB)) obj_apagar(c, id, I(0)); return -1; }
    case 58: { if (I(0) != GL_RENDERBUFFER) { erro_gl(c, GL_INVALID_ENUM); return -1; } if (I(1) && !O(id, I(1), O_RB)) { erro_gl(c, GL_INVALID_OPERATION); return -1; } c->rb = I(1); return -1; }
    case 59: {   /* renderbufferStorage(t, fmt, w, h) */
        gobj *b = O(id, c->rb, O_RB);
        if (I(0) != GL_RENDERBUFFER) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (!b) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        int f = I(1), w = I(2), h = I(3);
        if (w < 0 || h < 0 || w > 4096 || h > 4096) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        int cor = f == GL_RGBA4 || f == GL_RGB5_A1 || f == GL_RGB565 || f == 0x8058 /* RGBA8 */, z = f == GL_DEPTH_COMPONENT16 || f == GL_DEPTH_STENCIL, st = f == GL_STENCIL_INDEX8 || f == GL_DEPTH_STENCIL;
        if (!cor && !z && !st) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (b->rcor) libera(b->rcor);
        if (b->rz) libera(b->rz);
        if (b->rst) libera(b->rst);
        b->rcor = NULL; b->rz = NULL; b->rst = NULL;
        unsigned n = (unsigned)(w * h);
        b->rfmt = f; b->rl = w; b->ra = h;
        if (cor) { b->rcor = aloca(n * 4 + 4); if (b->rcor) memset(b->rcor, 0, n * 4); }
        if (z) { b->rz = aloca(n * 4 + 4); if (b->rz) for (unsigned i = 0; i < n; i++) b->rz[i] = 1; }
        if (st) { b->rst = aloca(n + 4); if (b->rst) memset(b->rst, 0, n); }
        return -1;
    }
    /* shaders */
    case 60: {
        if (I(0) != GL_VERTEX_SHADER && I(0) != GL_FRAGMENT_SHADER) { erro_gl(c, GL_INVALID_ENUM); return -3; }
        int k = obj_novo(id, O_SH);
        if (!k) return -3;
        objs[k].sh = aloca(sizeof(shader_t));
        if (!objs[k].sh) { objs[k].tipo = 0; return -3; }
        memset(objs[k].sh, 0, sizeof(shader_t));
        objs[k].sh->tipo = I(0);
        o[0] = k; return 1;
    }
    case 61: { if (O(id, I(0), O_SH)) obj_apagar(c, id, I(0)); return -1; }
    case 62: {
        gobj *x = O(id, I(0), O_SH);
        if (!x) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        int n = 0; while (txt && txt[n]) n++;
        char *f = aloca((unsigned)n + 1);
        if (!f) return -1;
        memcpy(f, txt ? txt : "", (unsigned)n); f[n] = 0;
        if (x->sh->fonte) libera(x->sh->fonte);
        x->sh->fonte = f; x->sh->nfonte = n;
        return -1;
    }
    case 63: {
        gobj *x = O(id, I(0), O_SH);
        if (!x) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        shader_t *sh = x->sh;
        if (sh->p) { glsl_liberar(sh->p); sh->p = NULL; }
        sh->log[0] = 0;
        sh->p = glsl_compilar(sh->fonte ? sh->fonte : "", sh->nfonte, sh->tipo == GL_VERTEX_SHADER ? 0 : 1, sh->log, sizeof sh->log);
        sh->compilado = sh->p != NULL;
        return -1;
    }
    case 64: {
        gobj *x = O(id, I(0), O_SH);
        if (!x) { erro_gl(c, GL_INVALID_VALUE); return -3; }
        switch (I(1)) { case GL_SHADER_TYPE: o[0] = x->sh->tipo; return 1; case GL_COMPILE_STATUS: o[0] = x->sh->compilado; return 1; case GL_DELETE_STATUS: o[0] = x->sh->apagar; return 1; }
        erro_gl(c, GL_INVALID_ENUM); return -3;
    }
    case 65: case 66: {
        gobj *x = O(id, I(0), O_SH);
        if (!x) { erro_gl(c, GL_INVALID_VALUE); return -3; }
        copiar_nome(s, op == 65 ? x->sh->log : x->sh->fonte ? x->sh->fonte : "", maxs);
        return -2;
    }
    /* programas */
    case 67: {
        int k = obj_novo(id, O_PROG);
        if (!k) return -3;
        objs[k].pg = aloca(sizeof(prog_t));
        if (!objs[k].pg) { objs[k].tipo = 0; return -3; }
        memset(objs[k].pg, 0, sizeof(prog_t));
        o[0] = k; return 1;
    }
    case 68: { if (O(id, I(0), O_PROG)) obj_apagar(c, id, I(0)); return -1; }
    case 69: case 70: {
        gobj *p = O(id, I(0), O_PROG), *x = O(id, I(1), O_SH);
        if (!p || !x) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        int *slot = x->sh->tipo == GL_VERTEX_SHADER ? &p->pg->vs : &p->pg->fs;
        if (op == 69) { if (*slot) { erro_gl(c, GL_INVALID_OPERATION); return -1; } *slot = I(1); x->sh->usos++; }
        else {
            if (*slot != I(1)) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
            *slot = 0; x->sh->usos--;
            if (x->sh->apagar && x->sh->usos <= 0) { x->sh->usos = 0; obj_apagar(c, id, I(1)); }
        }
        return -1;
    }
    case 71: { gobj *p = O(id, I(0), O_PROG); if (!p) { erro_gl(c, GL_INVALID_VALUE); return -1; } ligar(c, id, p); return -1; }
    case 72: {
        int k = I(0);
        if (k) { gobj *p = O(id, k, O_PROG); if (!p) { erro_gl(c, GL_INVALID_VALUE); return -1; } if (!p->pg->ligado) { erro_gl(c, GL_INVALID_OPERATION); return -1; } }
        int velho = c->prog; c->prog = k;
        if (velho && velho != k) { gobj *v = O(id, velho, O_PROG); if (v && v->pg->apagar) obj_apagar(c, id, velho); }
        return -1;
    }
    case 73: {
        gobj *p = O(id, I(0), O_PROG);
        if (!p) { erro_gl(c, GL_INVALID_VALUE); return -3; }
        switch (I(1)) {
        case GL_LINK_STATUS: o[0] = p->pg->ligado; return 1;
        case GL_DELETE_STATUS: o[0] = p->pg->apagar; return 1;
        case GL_VALIDATE_STATUS: o[0] = p->pg->validado; return 1;
        case GL_ATTACHED_SHADERS: o[0] = (p->pg->vs != 0) + (p->pg->fs != 0); return 1;
        case GL_ACTIVE_ATTRIBUTES: o[0] = p->pg->na; return 1;
        case GL_ACTIVE_UNIFORMS: o[0] = p->pg->nu; return 1;
        }
        erro_gl(c, GL_INVALID_ENUM); return -3;
    }
    case 74: { gobj *p = O(id, I(0), O_PROG); if (!p) { erro_gl(c, GL_INVALID_VALUE); return -3; } copiar_nome(s, p->pg->log, maxs); return -2; }
    case 75: { gobj *p = O(id, I(0), O_PROG); if (p) p->pg->validado = p->pg->ligado; return -1; }
    case 76: {
        gobj *p = O(id, I(0), O_PROG);
        if (!p || I(1) < 0 || I(1) >= 16) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        if (txt && txt[0] == 'g' && txt[1] == 'l' && txt[2] == '_') { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        prog_t *pg = p->pg;
        int k; for (k = 0; k < pg->npedido; k++) if (nome_igual(pg->pedido[k].nome, txt ? txt : "")) break;
        if (k == pg->npedido) { if (k >= 16) return -1; pg->npedido++; }
        copiar_nome(pg->pedido[k].nome, txt ? txt : "", 64); pg->pedido[k].loc = I(1);
        return -1;
    }
    case 77: {
        gobj *p = O(id, I(0), O_PROG);
        o[0] = -1;
        if (!p) { erro_gl(c, GL_INVALID_VALUE); return 1; }
        if (!p->pg->ligado) { erro_gl(c, GL_INVALID_OPERATION); return 1; }
        for (int i = 0; i < p->pg->na; i++) if (nome_igual(p->pg->at[i].nome, txt ? txt : "")) o[0] = p->pg->at[i].loc;
        return 1;
    }
    case 78: {
        gobj *p = O(id, I(0), O_PROG);
        if (!p) { erro_gl(c, GL_INVALID_VALUE); return -3; }
        if (!p->pg->ligado) { erro_gl(c, GL_INVALID_OPERATION); return -3; }
        int l = uniform_local(p->pg, txt ? txt : "");
        if (l < 0) return -3;
        o[0] = l; return 1;
    }
    case 79: case 80: {   /* getActiveAttrib/Uniform(p, i): nome em s; o = [tamanho, tipo] */
        gobj *p = O(id, I(0), O_PROG);
        if (!p) { erro_gl(c, GL_INVALID_VALUE); return -3; }
        int i = I(1);
        if (op == 79) {
            if (i < 0 || i >= p->pg->na) { erro_gl(c, GL_INVALID_VALUE); return -3; }
            copiar_nome(s, p->pg->at[i].nome, maxs); o[0] = 1; o[1] = p->pg->at[i].gltipo;
        } else {
            if (i < 0 || i >= p->pg->nu) { erro_gl(c, GL_INVALID_VALUE); return -3; }
            unif_t *u = &p->pg->u[i];
            if (u->arr) formatar_texto(s, maxs, "%s[0]", u->nome); else copiar_nome(s, u->nome, maxs);
            o[0] = u->arr ? u->arr : 1; o[1] = u->gltipo;
        }
        *eh_arr = 1; return 2;
    }
    case 81: { gobj *p = O(id, I(0), O_PROG); if (!p) { erro_gl(c, GL_INVALID_VALUE); return -3; } int n = 0; if (p->pg->vs) o[n++] = p->pg->vs; if (p->pg->fs) o[n++] = p->pg->fs; *eh_arr = 1; return n; }
    case 82: {   /* uniform(loc, componentes, int?) com floats em dados */
        gobj *po = O(id, c->prog, O_PROG);
        if (I(0) < 0) return -1;   /* local null: nada */
        if (!po) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        prog_t *p = po->pg;
        int k = I(0) / 4096, el = I(0) % 4096, nc = I(1), eint = I(2);
        if (k >= p->nu) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        unif_t *u = &p->u[k];
        const float *f = dados; int n = ndados;
        int es = u->elem;
        if (nc != es) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        int tipo_int = u->base == 2 || u->base == 4 || u->base == 5;
        if ((u->base == 1 && eint) || (tipo_int && !eint)) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        if (n < es || n % es) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        int cont = n / es, max = (u->arr ? u->arr : 1) - el;
        if (cont > 1 && !u->arr) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        if (cont > max) cont = max;
        for (int e = 0; e < 2; e++) {
            int sl = e ? u->sf : u->sv; float *m = e ? p->mf : p->mv;
            if (sl < 0) continue;
            for (int j = 0; j < cont * es; j++) {
                float v = f[j]; if (u->base == 3) v = v != 0;
                float *d = &m[(sl + el * es + j) * 4]; d[0] = d[1] = d[2] = d[3] = v;
            }
        }
        return -1;
    }
    case 83: {   /* getUniform(p, loc) */
        gobj *po = O(id, I(0), O_PROG);
        if (!po || !po->pg->ligado) { erro_gl(c, GL_INVALID_OPERATION); return -3; }
        prog_t *p = po->pg; int k = I(1) / 4096, el = I(1) % 4096;
        if (k >= p->nu) return -3;
        unif_t *u = &p->u[k];
        int sl = u->sv >= 0 ? u->sv : u->sf; float *m = u->sv >= 0 ? p->mv : p->mf;
        int n = u->elem; if (n > maxo) n = maxo;
        for (int j = 0; j < n; j++) o[j] = m[(sl + el * u->elem + j) * 4];
        *eh_arr = n > 1;
        o[n] = u->base;   /* o JS converte bool/int */
        return n;
    }
    /* atributos */
    case 84: case 85: { if (I(0) < 0 || I(0) >= 16) { erro_gl(c, GL_INVALID_VALUE); return -1; } vao_atual(c, id)->at[I(0)].ativo = op == 84; return -1; }
    case 86: {   /* vertexAttribPointer(i, tam, tipo, norm, stride, off) */
        int i = I(0), tam = I(1), tipo = I(2), st = I(4), off = I(5);
        if (i < 0 || i >= 16 || tam < 1 || tam > 4 || st < 0 || st > 255 || off < 0) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        if (tipo != GL_BYTE && tipo != GL_UNSIGNED_BYTE && tipo != GL_SHORT && tipo != GL_UNSIGNED_SHORT && tipo != GL_FLOAT) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        int ts = tam_tipo_gl(tipo);
        if (off % ts || st % ts) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        if (!c->buf_arr && off) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        atrib_t *at = &vao_atual(c, id)->at[i];
        at->tam = tam; at->tipo = tipo; at->norm = I(3) != 0; at->stride = st; at->off = off; at->buf = c->buf_arr;
        return -1;
    }
    case 87: { if (I(0) < 0 || I(0) >= 16) { erro_gl(c, GL_INVALID_VALUE); return -1; } for (int k = 0; k < 4; k++) c->atual[I(0)][k] = (float)A(1 + k); return -1; }
    case 88: { if (I(0) < 0 || I(0) >= 16) { erro_gl(c, GL_INVALID_VALUE); return -1; } vao_atual(c, id)->at[I(0)].div = I(1) < 0 ? 0 : I(1); return -1; }
    case 89: {   /* getVertexAttrib(i, p) */
        int i = I(0); if (i < 0 || i >= 16) { erro_gl(c, GL_INVALID_VALUE); return -3; }
        atrib_t *at = &vao_atual(c, id)->at[i];
        switch (I(1)) {
        case 0x8622: o[0] = at->ativo; return 1; case 0x8623: o[0] = at->tam ? at->tam : 4; return 1; case 0x8624: o[0] = at->stride; return 1;
        case 0x8625: o[0] = at->tipo ? at->tipo : GL_FLOAT; return 1; case 0x886A: o[0] = at->norm; return 1; case 0x889F: o[0] = at->buf; return 1;
        case 0x88FE: o[0] = at->div; return 1;
        case 0x8626: for (int k = 0; k < 4; k++) o[k] = c->atual[i][k]; *eh_arr = 1; return 4;
        }
        erro_gl(c, GL_INVALID_ENUM); return -3;
    }
    case 90: { int i = I(0); if (i < 0 || i >= 16) { erro_gl(c, GL_INVALID_VALUE); o[0] = 0; return 1; } o[0] = vao_atual(c, id)->at[i].off; return 1; }
    case 91: desenhar(c, id, I(0), I(1), I(2), 0, 0, na > 3 ? I(3) : 1); return -1;
    case 92: {
        int t = I(2);
        if (t != GL_UNSIGNED_BYTE && t != GL_UNSIGNED_SHORT && t != GL_UNSIGNED_INT) { erro_gl(c, GL_INVALID_ENUM); return -1; }
        if (I(3) < 0) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        desenhar(c, id, I(0), 0, I(1), t, I(3), na > 4 ? I(4) : 1);
        return -1;
    }
    case 93: {   /* readPixels(x, y, w, h, fmt, tipo) -> dados */
        int x = I(0), y = I(1), w = I(2), h = I(3), fmt = I(4), tipo = I(5);
        if (w < 0 || h < 0) { erro_gl(c, GL_INVALID_VALUE); return -1; }
        if (fmt != GL_RGBA || tipo != GL_UNSIGNED_BYTE) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        alvo_t al; if (!alvo_atual(c, id, &al)) return -1;
        int linha = w * 4, passo = (linha + c->al_pack - 1) / c->al_pack * c->al_pack;
        if (!dados || (h > 0 && (long)passo * (h - 1) + linha > ndados)) { erro_gl(c, GL_INVALID_OPERATION); return -1; }
        uint8_t *d = dados;
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
            int sx = x + i, sy = y + j;
            uint8_t *p = d + j * passo + i * 4;
            if (sx < 0 || sy < 0 || sx >= al.l || sy >= al.a || !al.cor) continue;   /* fora: fica como estava */
            memcpy(p, al.cor + (sy * al.l + sx) * 4, 4);
            if (!c->fb && !c->alfa) p[3] = 255;
        }
        return -1;
    }
    case 94: {
        int k = obj_novo(id, O_VAO);
        if (!k) return -3;
        objs[k].va = aloca(sizeof(vao_t));
        if (!objs[k].va) { objs[k].tipo = 0; return -3; }
        memset(objs[k].va, 0, sizeof(vao_t));
        o[0] = k; return 1;
    }
    case 95: { if (O(id, I(0), O_VAO)) obj_apagar(c, id, I(0)); return -1; }
    case 96: { if (I(0) && !O(id, I(0), O_VAO)) { erro_gl(c, GL_INVALID_OPERATION); return -1; } c->vao = I(0); return -1; }
    case 97: { int n = param(c, id, I(0), o); if (n < 0) { erro_gl(c, GL_INVALID_ENUM); return -3; } *eh_arr = n != 1; return n; }
    case 98: {   /* getBufferParameter(t, p) */
        int t = I(0); int k = t == GL_ARRAY_BUFFER ? c->buf_arr : t == GL_ELEMENT_ARRAY_BUFFER ? vao_atual(c, id)->elem : 0;
        gobj *b = O(id, k, O_BUF);
        if (!b) { erro_gl(c, GL_INVALID_OPERATION); return -3; }
        if (I(1) == GL_BUFFER_SIZE) { o[0] = b->n; return 1; }
        if (I(1) == GL_BUFFER_USAGE) { o[0] = b->uso ? b->uso : GL_STATIC_DRAW; return 1; }
        erro_gl(c, GL_INVALID_ENUM); return -3;
    }
    case 99: {
        int t = I(0);
        gobj *x = O(id, t == GL_TEXTURE_2D ? c->tex2d[c->unidade] : t == GL_TEXTURE_CUBE_MAP ? c->texcube[c->unidade] : 0, O_TEX);
        if (!x) { erro_gl(c, GL_INVALID_OPERATION); return -3; }
        switch (I(1)) { case GL_TEXTURE_MIN_FILTER: o[0] = x->minf; return 1; case GL_TEXTURE_MAG_FILTER: o[0] = x->magf; return 1; case GL_TEXTURE_WRAP_S: o[0] = x->ws; return 1; case GL_TEXTURE_WRAP_T: o[0] = x->wt; return 1; }
        erro_gl(c, GL_INVALID_ENUM); return -3;
    }
    case 100: {   /* getFramebufferAttachmentParameter(t, att, p): o[0] tipo do objeto, o[1] id */
        gobj *f = O(id, c->fb, O_FB);
        if (!f) { erro_gl(c, GL_INVALID_OPERATION); return -3; }
        int tex = 0, rb = 0;
        switch (I(1)) { case GL_COLOR_ATTACHMENT0: tex = f->cor_tex; rb = f->cor_rb; break; case GL_DEPTH_ATTACHMENT: rb = f->z_rb; break; case GL_STENCIL_ATTACHMENT: rb = f->st_rb; break; case GL_DEPTH_STENCIL_ATTACHMENT: rb = f->zst_rb; break; }
        switch (I(2)) {
        case 0x8CD0: o[0] = tex ? GL_TEXTURE_2D - 0xDE1 + 0x1702 : rb ? GL_RENDERBUFFER : 0; return 1;   /* OBJECT_TYPE: TEXTURE / RENDERBUFFER / NONE */
        case 0x8CD1: o[0] = tex ? tex : rb; o[1] = tex ? O_TEX : O_RB; *eh_arr = 1; return 2;
        case 0x8CD2: o[0] = f->cor_nivel; return 1;
        case 0x8CD3: o[0] = tex && objs[tex].alvo == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + f->cor_face : 0; return 1;
        }
        erro_gl(c, GL_INVALID_ENUM); return -3;
    }
    case 101: {
        gobj *b = O(id, c->rb, O_RB);
        if (!b) { erro_gl(c, GL_INVALID_OPERATION); return -3; }
        switch (I(1)) {
        case 0x8D42: o[0] = b->rl; return 1; case 0x8D43: o[0] = b->ra; return 1; case 0x8D44: o[0] = b->rfmt ? b->rfmt : GL_RGBA4; return 1;
        case 0x8D50: case 0x8D51: case 0x8D52: case 0x8D53: o[0] = b->rcor ? (b->rfmt == GL_RGB565 ? (I(1) == 0x8D51 ? 6 : I(1) == 0x8D53 ? 0 : 5) : b->rfmt == GL_RGBA4 ? 4 : b->rfmt == GL_RGB5_A1 ? (I(1) == 0x8D53 ? 1 : 5) : 8) : 0; return 1;
        case 0x8D54: o[0] = b->rz ? 16 : 0; return 1; case 0x8D55: o[0] = b->rst ? 8 : 0; return 1;
        }
        erro_gl(c, GL_INVALID_ENUM); return -3;
    }
    case 102: { int k = I(0), t = I(1); o[0] = O(id, k, t) != NULL; return 1; }   /* isBuffer etc. */
    case 103: { if (I(0) != 0x8192 && I(0) != 0x8B8B) erro_gl(c, GL_INVALID_ENUM); else c->hint = I(1); return -1; }   /* hint */
    case 104: { gobj *b = O(id, c->fb, O_FB); if (b) erro_gl(c, GL_INVALID_OPERATION); c->sujo = 1; return -1; }
    case 105: { gobj *p = O(id, I(0), O_SH); o[0] = 0; if (!p) return 1; o[0] = p->sh->compilado; return 1; }
    }
    return -1;
}
#undef A
#undef I
