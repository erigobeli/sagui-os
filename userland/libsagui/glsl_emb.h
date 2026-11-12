/* glsl_emb.h - funções e variáveis embutidas do GLSL ES 1.00 (incluído por glsl.c) */

enum {
    E_RADIANS, E_DEGREES, E_SIN, E_COS, E_TAN, E_ASIN, E_ACOS, E_ATAN, E_ATAN2, E_POW, E_EXP, E_LOG, E_EXP2, E_LOG2, E_SQRT, E_INVSQRT,
    E_ABS, E_SIGN, E_FLOOR, E_CEIL, E_FRACT, E_MOD, E_MIN, E_MAX, E_CLAMP, E_MIX, E_STEP, E_SMOOTH,
    E_LENGTH, E_DISTANCE, E_DOT, E_CROSS, E_NORMALIZE, E_FACEFWD, E_REFLECT, E_REFRACT, E_MATCOMP,
    E_LT, E_LE, E_GT, E_GE, E_EQ, E_NE, E_ANY, E_ALL, E_NOT,
    E_TEX2D, E_TEX2DB, E_TEX2DPROJ, E_TEX2DPROJB, E_TEX2DLOD, E_TEX2DPROJLOD, E_TEXCUBE, E_TEXCUBEB, E_TEXCUBELOD,
    E_DFDX, E_DFDY, E_FWIDTH
};

static int gen_float(const tipo *t) { return t->base == T_FLOAT && t->col == 1 && !t->arr; }
static int mesmo(const tipo *a, const tipo *b) { return tipo_igual(a, b); }

static int emb_achar(const char *n, no **a, int na, tipo *ret)
{
    #define IG(s) (!compara_texto(n, s))
    tipo t0 = na > 0 ? a[0]->t : t_esc(T_VOID);
    /* genType f(genType) */
    static const struct { const char *n; int e; } um[] = {
        {"radians",E_RADIANS},{"degrees",E_DEGREES},{"sin",E_SIN},{"cos",E_COS},{"tan",E_TAN},{"asin",E_ASIN},{"acos",E_ACOS},
        {"exp",E_EXP},{"log",E_LOG},{"exp2",E_EXP2},{"log2",E_LOG2},{"sqrt",E_SQRT},{"inversesqrt",E_INVSQRT},{"abs",E_ABS},
        {"sign",E_SIGN},{"floor",E_FLOOR},{"ceil",E_CEIL},{"fract",E_FRACT},{"normalize",E_NORMALIZE},
        {"dFdx",E_DFDX},{"dFdy",E_DFDY},{"fwidth",E_FWIDTH},
    };
    for (unsigned i = 0; i < sizeof um / sizeof um[0]; i++)
        if (IG(um[i].n) && na == 1 && gen_float(&t0)) { *ret = t0; return um[i].e; }
    if (IG("atan") && na == 1 && gen_float(&t0)) { *ret = t0; return E_ATAN; }
    if (IG("atan") && na == 2 && gen_float(&t0) && mesmo(&t0, &a[1]->t)) { *ret = t0; return E_ATAN2; }
    if (IG("pow") && na == 2 && gen_float(&t0) && mesmo(&t0, &a[1]->t)) { *ret = t0; return E_POW; }
    /* genType f(genType, genType|float) */
    if ((IG("mod") || IG("min") || IG("max")) && na == 2 && gen_float(&t0) && (mesmo(&t0, &a[1]->t) || (a[1]->t.base == T_FLOAT && eh_escalar(&a[1]->t)))) {
        *ret = t0; return IG("mod") ? E_MOD : IG("min") ? E_MIN : E_MAX;
    }
    if (IG("clamp") && na == 3 && gen_float(&t0)) { *ret = t0; return E_CLAMP; }
    if (IG("mix") && na == 3 && gen_float(&t0)) { *ret = t0; return E_MIX; }
    if (IG("step") && na == 2 && gen_float(&a[1]->t)) { *ret = a[1]->t; return E_STEP; }
    if (IG("smoothstep") && na == 3 && gen_float(&a[2]->t)) { *ret = a[2]->t; return E_SMOOTH; }
    if (IG("length") && na == 1 && gen_float(&t0)) { *ret = t_esc(T_FLOAT); return E_LENGTH; }
    if (IG("distance") && na == 2 && gen_float(&t0)) { *ret = t_esc(T_FLOAT); return E_DISTANCE; }
    if (IG("dot") && na == 2 && gen_float(&t0)) { *ret = t_esc(T_FLOAT); return E_DOT; }
    if (IG("cross") && na == 2 && t0.lin == 3) { *ret = t0; return E_CROSS; }
    if (IG("faceforward") && na == 3) { *ret = t0; return E_FACEFWD; }
    if (IG("reflect") && na == 2) { *ret = t0; return E_REFLECT; }
    if (IG("refract") && na == 3) { *ret = t0; return E_REFRACT; }
    if (IG("matrixCompMult") && na == 2) { *ret = t0; return E_MATCOMP; }
    static const struct { const char *n; int e; } cmp[] = { {"lessThan",E_LT},{"lessThanEqual",E_LE},{"greaterThan",E_GT},{"greaterThanEqual",E_GE},{"equal",E_EQ},{"notEqual",E_NE} };
    for (unsigned i = 0; i < sizeof cmp / sizeof cmp[0]; i++) if (IG(cmp[i].n) && na == 2) { *ret = t_vec(T_BOOL, t0.lin); return cmp[i].e; }
    if (IG("any") && na == 1) { *ret = t_esc(T_BOOL); return E_ANY; }
    if (IG("all") && na == 1) { *ret = t_esc(T_BOOL); return E_ALL; }
    if (IG("not") && na == 1) { *ret = t0; return E_NOT; }
    tipo v4 = t_vec(T_FLOAT, 4);
    if ((IG("texture2D") || IG("texture")) && t0.base == T_SAMPLER2D) { *ret = v4; return na == 3 ? E_TEX2DB : E_TEX2D; }
    if ((IG("texture2DProj") || IG("textureProj")) && t0.base == T_SAMPLER2D) { *ret = v4; return na == 3 ? E_TEX2DPROJB : E_TEX2DPROJ; }
    if ((IG("texture2DLod") || IG("texture2DLodEXT") || IG("textureLod") || IG("texture2DGradEXT")) && t0.base == T_SAMPLER2D) { *ret = v4; return E_TEX2DLOD; }
    if ((IG("texture2DProjLod") || IG("texture2DProjLodEXT")) && t0.base == T_SAMPLER2D) { *ret = v4; return E_TEX2DPROJLOD; }
    if ((IG("textureCube") || IG("texture")) && t0.base == T_SAMPLERCUBE) { *ret = v4; return na == 3 ? E_TEXCUBEB : E_TEXCUBE; }
    if ((IG("textureCubeLod") || IG("textureCubeLodEXT") || IG("textureLod") || IG("textureCubeGradEXT")) && t0.base == T_SAMPLERCUBE) { *ret = v4; return E_TEXCUBELOD; }
    #undef IG
    return -1;
}

/* variáveis embutidas: o gerador dá slots a elas como a qualquer global */
static void embutidas_globais(comp *c)
{
    tipo v4 = t_vec(T_FLOAT, 4), f = t_esc(T_FLOAT), b = t_esc(T_BOOL), v2 = t_vec(T_FLOAT, 2), i1 = t_esc(T_INT);
    if (c->tipo_shader == 0) {
        novo_sim(c, "gl_Position", &v4, 5);
        novo_sim(c, "gl_PointSize", &f, 5);
    } else {
        novo_sim(c, "gl_FragColor", &v4, 5);
        tipo fd = v4; fd.arr = 1;
        novo_sim(c, "gl_FragData", &fd, 5);
        novo_sim(c, "gl_FragCoord", &v4, 5);
        novo_sim(c, "gl_FrontFacing", &b, 5);
        novo_sim(c, "gl_PointCoord", &v2, 5);
    }
    static const struct { const char *n; int v; } lim[] = {
        {"gl_MaxVertexAttribs",16},{"gl_MaxVertexUniformVectors",256},{"gl_MaxVaryingVectors",15},{"gl_MaxVertexTextureImageUnits",16},
        {"gl_MaxCombinedTextureImageUnits",32},{"gl_MaxTextureImageUnits",16},{"gl_MaxFragmentUniformVectors",256},{"gl_MaxDrawBuffers",1} };
    for (unsigned k = 0; k < sizeof lim / sizeof lim[0]; k++) { sim *s = novo_sim(c, lim[k].n, &i1, 4); if (s) { s->cval[0] = lim[k].v; s->tem_cval = 1; } }
    /* gl_DepthRange */
}
