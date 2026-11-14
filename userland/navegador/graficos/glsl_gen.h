/* glsl_gen.h - geração de código e máquina virtual (incluído por glsl.c) */

/* ---------------------------------------------------------------- emissão */
static glsl_ins *emitir(comp *c, int op, int n, int d, int a, int b)
{
    glsl_prog *p = c->prog;
    if (p->nins == p->cins) {
        int nc = p->cins ? p->cins * 2 : 1024;
        glsl_ins *ni = aloca((unsigned)nc * sizeof(glsl_ins));
        if (!ni) { falha(c, "sem memoria", NULL); static glsl_ins z; return &z; }
        if (p->nins) memcpy(ni, p->ins, (size_t)p->nins * sizeof(glsl_ins));
        if (p->ins) libera(p->ins);
        p->ins = ni; p->cins = nc;
    }
    glsl_ins *i = &p->ins[p->nins++];
    memset(i, 0, sizeof *i);
    i->op = (unsigned short)op; i->n = (unsigned short)n; i->d = d; i->a = a; i->b = b;
    return i;
}
static int slots(comp *c, int n) { int s = c->prog->nslots; c->prog->nslots += n > 0 ? n : 1; return s; }
static int constante(comp *c, const double *v, int n)
{
    glsl_prog *p = c->prog;
    /* procura igual (só escalares, os mais comuns) */
    if (n == 1) for (int i = 0; i < p->nconst; i++) if (p->consts[i] == (float)v[0]) return p->const_slot[i];
    int s = slots(c, n);
    for (int i = 0; i < n; i++) {
        if (p->nconst == p->cconst) {
            int nc = p->cconst ? p->cconst * 2 : 256;
            float *nv = aloca((unsigned)nc * sizeof(float)); int *ns = aloca((unsigned)nc * sizeof(int));
            if (!nv || !ns) { falha(c, "sem memoria", NULL); return s; }
            if (p->nconst) { memcpy(nv, p->consts, (size_t)p->nconst * sizeof(float)); memcpy(ns, p->const_slot, (size_t)p->nconst * sizeof(int)); libera(p->consts); libera(p->const_slot); }
            p->consts = nv; p->const_slot = ns; p->cconst = nc;
        }
        p->consts[p->nconst] = (float)v[i]; p->const_slot[p->nconst] = s + i; p->nconst++;
    }
    return s;
}
static int cte1(comp *c, double x) { return constante(c, &x, 1); }

static void registrar_var(comp *c, glsl_var **lista, int *n, sim *s)
{
    glsl_var *nv = aloca((unsigned)(*n + 1) * sizeof(glsl_var));
    if (!nv) return;
    if (*n) { memcpy(nv, *lista, (size_t)*n * sizeof(glsl_var)); libera(*lista); }
    glsl_var *v = &nv[*n];
    memset(v, 0, sizeof *v);
    copia_texto(v->nome, s->nome, sizeof v->nome);
    v->slot = s->slot; v->tam = tam_tipo(&s->t); v->base = s->t.base; v->lin = s->t.lin; v->col = s->t.col; v->arr = s->t.arr; v->elem = tam_elem(&s->t);
    *lista = nv; (*n)++;
}
/* uniforms de struct: cada campo vira "nome.campo" (e "nome[i].campo") como o WebGL expõe */
static void registrar_unif(comp *c, const char *nome, const tipo *t, int slot)
{
    glsl_prog *p = c->prog;
    if (t->base == T_STRUCT) {
        int k = t->arr ? t->arr : 1;
        for (int i = 0; i < k; i++)
            for (int f = 0; f < t->s->nc; f++) {
                char nn[96];
                if (t->arr) formatar_texto(nn, sizeof nn, "%s[%d].%s", nome, i, t->s->c[f].nome);
                else formatar_texto(nn, sizeof nn, "%s.%s", nome, t->s->c[f].nome);
                registrar_unif(c, nn, &t->s->c[f].t, slot + i * t->s->tam + t->s->c[f].desl);
            }
        return;
    }
    sim tmp; memset(&tmp, 0, sizeof tmp);
    copia_texto(tmp.nome, nome, sizeof tmp.nome); tmp.t = *t; tmp.slot = slot;
    registrar_var(c, &p->unif, &p->nunif, &tmp);
}

/* ---------------------------------------------------------------- expressões */
typedef struct { int slot; tipo t; } val;
typedef struct { int slot; tipo t; int nsw; unsigned char sw[4]; int idx; int idx_elem, idx_n; } lval;   /* idx >= 0: índice dinâmico */

static val gexpr(comp *c, no *n);
static void atribuir_l(comp *c, lval *l, val v);
static lval glval(comp *c, no *n);
static val carregar_l(comp *c, lval *l);

static val novo_val(comp *c, tipo t) { val v; v.t = t; v.slot = slots(c, tam_tipo(&t)); return v; }
static void mov(comp *c, int d, int a, int n) { if (n > 0) emitir(c, G_MOV, n, d, a, 0); }
/* escalar promovido para vetor quando a operação mistura os dois */
static val bin_comp(comp *c, int op, val a, val b, tipo rt)
{
    val r = novo_val(c, rt);
    glsl_ins *i = emitir(c, op, tam_tipo(&rt), r.slot, a.slot, b.slot);
    i->ea = (unsigned char)(tam_tipo(&a.t) == 1 && tam_tipo(&rt) > 1);
    i->eb = (unsigned char)(tam_tipo(&b.t) == 1 && tam_tipo(&rt) > 1);
    return r;
}
static val un_comp(comp *c, int op, val a)
{
    val r = novo_val(c, a.t);
    emitir(c, op, tam_tipo(&a.t), r.slot, a.slot, 0);
    return r;
}
static val conv_int(comp *c, val v)   /* resultado int: trunca */
{
    if (v.t.base != T_INT) return v;
    return un_comp(c, G_TOINT, v);
}

static val construtor(comp *c, no *n)
{
    tipo t = n->t;
    val r = novo_val(c, t);
    int k = tam_tipo(&t);
    val args[20]; int na = n->nargs, total = 0;
    for (int i = 0; i < na; i++) { args[i] = gexpr(c, n->args[i]); total += tam_tipo(&args[i].t); }
    if (t.base == T_STRUCT || t.arr) {   /* campos em ordem */
        int o = 0;
        for (int i = 0; i < na; i++) { int ka = tam_tipo(&args[i].t); mov(c, r.slot + o, args[i].slot, ka); o += ka; }
        return r;
    }
    int conv = t.base == T_INT ? G_TOINT : t.base == T_BOOL ? G_TOBOOL : -1;
    if (eh_matriz(&t)) {
        int N = t.lin;
        if (na == 1 && eh_escalar(&args[0].t)) {   /* diagonal */
            double z = 0; int sz = constante(c, &z, 1);
            for (int i = 0; i < N * N; i++) mov(c, r.slot + i, (i % (N + 1)) == 0 ? args[0].slot : sz, 1);
            return r;
        }
        if (na == 1 && eh_matriz(&args[0].t)) {   /* outra matriz: o canto superior esquerdo, o resto da identidade */
            int M = args[0].t.lin;
            double z = 0, u = 1; int sz = constante(c, &z, 1), su = constante(c, &u, 1);
            for (int col = 0; col < N; col++)
                for (int lin = 0; lin < N; lin++) {
                    int dst = r.slot + col * N + lin;
                    if (col < M && lin < M) mov(c, dst, args[0].slot + col * M + lin, 1);
                    else mov(c, dst, col == lin ? su : sz, 1);
                }
            return r;
        }
    }
    if (na == 1 && eh_escalar(&args[0].t) && k > 1) {   /* vec3(1.0): repete */
        glsl_ins *i = emitir(c, conv >= 0 ? conv : G_MOV, k, r.slot, args[0].slot, 0);
        i->ea = 1;
        return r;
    }
    int o = 0;
    for (int i = 0; i < na && o < k; i++) {
        int ka = tam_tipo(&args[i].t);
        if (ka > k - o) ka = k - o;
        emitir(c, conv >= 0 ? conv : G_MOV, ka, r.slot + o, args[i].slot, 0);
        o += ka;
    }
    (void)total;
    return r;
}

static val chamar_usuario(comp *c, no *n);
static val embutida(comp *c, no *n);

static val gexpr(comp *c, no *n)
{
    val r; memset(&r, 0, sizeof r);
    if (!c->falhou && n && !n->ok) { if (checar(c, n)) n->ok = 1; }
    if (c->falhou || !n) { r.t = t_esc(T_FLOAT); r.slot = slots(c, 1); return r; }
    switch (n->k) {
    case N_NUM: {
        r.t = n->t; r.slot = constante(c, n->v, tam_tipo(&n->t) > 0 ? tam_tipo(&n->t) : 1);
        return r;
    }
    case N_VAR:
        if (n->s->tem_cval && n->s->classe == 4) { r.t = n->t; r.slot = constante(c, n->s->cval, tam_tipo(&n->t)); return r; }
        r.t = n->t; r.slot = n->s->slot; return r;
    case N_SEQ: gexpr(c, n->a); return gexpr(c, n->b);
    case N_CONSTRUTOR: return construtor(c, n);
    case N_CHAMA: return n->f ? chamar_usuario(c, n) : embutida(c, n);
    case N_CAMPO: case N_IDX: { lval l = glval(c, n); return carregar_l(c, &l); }
    case N_UN: {
        val a = gexpr(c, n->a);
        if (n->op == '-') return un_comp(c, G_NEG, a);
        if (n->op == '!') return un_comp(c, G_NOT, a);
        return a;
    }
    case N_PRE: case N_POS: {
        lval l = glval(c, n->a);
        val v = carregar_l(c, &l);
        val antes = novo_val(c, v.t); mov(c, antes.slot, v.slot, tam_tipo(&v.t));
        val um; um.t = t_esc(v.t.base); um.slot = cte1(c, 1);
        val nv = bin_comp(c, n->op == K_INC ? G_ADD : G_SUB, v, um, v.t);
        atribuir_l(c, &l, nv);
        return n->k == N_PRE ? nv : antes;
    }
    case N_ATR: {
        lval l = glval(c, n->a);
        val b = gexpr(c, n->b);
        if (n->op != '=') {
            val a = carregar_l(c, &l);
            int op = n->op == K_ADDA ? G_ADD : n->op == K_SUBA ? G_SUB : n->op == K_MULA ? G_MUL : G_DIV;
            if (n->op == K_MULA && (eh_matriz(&a.t) || eh_matriz(&b.t)) && !(eh_escalar(&b.t) || eh_escalar(&a.t))) {
                val r2 = novo_val(c, a.t);
                if (eh_matriz(&a.t) && eh_matriz(&b.t)) { glsl_ins *i = emitir(c, G_MATMAT, a.t.lin, r2.slot, a.slot, b.slot); (void)i; }
                else { glsl_ins *i = emitir(c, G_VECMAT, a.t.lin, r2.slot, a.slot, b.slot); (void)i; }   /* v *= m */
                b = r2;
            } else {
                b = bin_comp(c, op, a, b, a.t);
                if (a.t.base == T_INT && op == G_DIV) b = conv_int(c, b);
            }
        }
        atribuir_l(c, &l, b);
        return b;
    }
    case N_COND: {
        /* os dois lados são calculados sob máscara; o resultado escolhe por via */
        val cnd = gexpr(c, n->a);
        int cs = slots(c, 1); mov(c, cs, cnd.slot, 1);
        val r2 = novo_val(c, n->t);
        glsl_ins *i = emitir(c, G_IF, 1, 0, cs, 0); int pif = c->prog->nins - 1; (void)i;
        val a = gexpr(c, n->b); mov(c, r2.slot, a.slot, tam_tipo(&n->t));
        emitir(c, G_ELSE, 1, 0, cs, 0); int pel = c->prog->nins - 1;
        val b = gexpr(c, n->c); mov(c, r2.slot, b.slot, tam_tipo(&n->t));
        emitir(c, G_ENDIF, 1, 0, 0, 0); int pend = c->prog->nins - 1;
        c->prog->ins[pif].alvo = pel; c->prog->ins[pel].alvo = pend;
        return r2;
    }
    case N_BIN: {
        int op = n->op;
        if (op == K_AND || op == K_OR) {   /* curto-circuito sob máscara */
            val a = gexpr(c, n->a);
            val r2 = novo_val(c, t_esc(T_BOOL)); mov(c, r2.slot, a.slot, 1);
            int cs = slots(c, 1);
            if (op == K_AND) mov(c, cs, a.slot, 1); else emitir(c, G_NOT, 1, cs, a.slot, 0);
            emitir(c, G_IF, 1, 0, cs, 0); int pif = c->prog->nins - 1;
            val b = gexpr(c, n->b); mov(c, r2.slot, b.slot, 1);
            emitir(c, G_ENDIF, 1, 0, 0, 0); int pend = c->prog->nins - 1;
            c->prog->ins[pif].alvo = pend;
            return r2;
        }
        val a = gexpr(c, n->a), b = gexpr(c, n->b);
        if (op == K_XOR) return bin_comp(c, G_XOR, a, b, t_esc(T_BOOL));
        if (op == K_EQ || op == K_NE) {
            val r2 = novo_val(c, t_esc(T_BOOL));
            glsl_ins *i = emitir(c, op == K_EQ ? G_EQALL : G_NEALL, tam_tipo(&a.t), r2.slot, a.slot, b.slot);
            i->ea = (unsigned char)(tam_tipo(&a.t) == 1 && tam_tipo(&b.t) > 1); i->eb = (unsigned char)(tam_tipo(&b.t) == 1 && tam_tipo(&a.t) > 1);
            if (i->ea) i->n = (unsigned short)tam_tipo(&b.t);
            return r2;
        }
        if (op == '<' || op == '>' || op == K_LE || op == K_GE)
            return bin_comp(c, op == '<' ? G_LT : op == '>' ? G_GT : op == K_LE ? G_LE : G_GE, a, b, t_esc(T_BOOL));
        if (op == '*' && (eh_matriz(&a.t) || eh_matriz(&b.t)) && !eh_escalar(&a.t) && !eh_escalar(&b.t)) {
            val r2 = novo_val(c, n->t);
            if (eh_matriz(&a.t) && eh_matriz(&b.t)) emitir(c, G_MATMAT, a.t.lin, r2.slot, a.slot, b.slot);
            else if (eh_matriz(&a.t)) emitir(c, G_MATVEC, a.t.lin, r2.slot, a.slot, b.slot);
            else emitir(c, G_VECMAT, b.t.lin, r2.slot, a.slot, b.slot);
            return r2;
        }
        int g = op == '+' ? G_ADD : op == '-' ? G_SUB : op == '*' ? G_MUL : op == '/' ? G_DIV : G_MOD;
        val r2 = bin_comp(c, g, a, b, n->t);
        if (n->t.base == T_INT && g == G_DIV) r2 = conv_int(c, r2);
        return r2;
    }
    }
    r.t = t_esc(T_FLOAT); r.slot = slots(c, 1);
    return r;
}

/* ---- lvalues: variável, campo, swizzle, índice constante ou dinâmico */
static lval glval(comp *c, no *n)
{
    lval l; memset(&l, 0, sizeof l); l.idx = -1;
    if (!c->falhou && !n->ok) { if (checar(c, n)) n->ok = 1; }
    if (c->falhou) { l.slot = slots(c, 1); l.t = t_esc(T_FLOAT); return l; }
    if (n->k == N_VAR) { l.slot = n->s->slot; l.t = n->t; return l; }
    if (n->k == N_CAMPO) {
        lval b = glval(c, n->a);
        if (n->nsw < 0) {   /* campo de struct */
            if (b.idx >= 0) { val v = carregar_l(c, &b); b.slot = v.slot; b.idx = -1; }
            b.slot += n->sw[0]; b.t = n->t; b.nsw = 0;
            return b;
        }
        if (b.idx >= 0 || b.nsw) { val v = carregar_l(c, &b); memset(&b, 0, sizeof b); b.slot = v.slot; b.t = v.t; b.idx = -1; }
        b.nsw = n->nsw; for (int i = 0; i < n->nsw; i++) b.sw[i] = (unsigned char)n->sw[i];
        b.t = n->t;
        return b;
    }
    if (n->k == N_IDX) {
        lval b = glval(c, n->a);
        if (b.idx >= 0 || b.nsw) { val v = carregar_l(c, &b); memset(&b, 0, sizeof b); b.slot = v.slot; b.t = v.t; b.idx = -1; }
        tipo t = b.t;
        int elem = t.arr ? tam_elem(&t) : t.col > 1 ? t.lin : 1;
        int cont = t.arr ? t.arr : t.col > 1 ? t.col : t.lin;
        double k[16];
        if (avaliar_const(c, n->b, k)) {
            int i = (int)k[0]; if (i < 0) i = 0; if (i >= cont) i = cont - 1;
            b.slot += i * elem; b.t = n->t;
            return b;
        }
        val iv = gexpr(c, n->b);
        b.idx = iv.slot; b.idx_elem = elem; b.idx_n = cont; b.t = n->t;
        return b;
    }
    /* expressão comum: valor temporário */
    val v = gexpr(c, n);
    l.slot = v.slot; l.t = v.t;
    return l;
}
static val carregar_l(comp *c, lval *l)
{
    val r;
    if (l->idx >= 0) {
        r = novo_val(c, l->t);
        glsl_ins *i = emitir(c, G_IDXLD, l->idx_elem, r.slot, l->slot, l->idx);
        i->c = l->idx_n;
        if (l->nsw) { val s = novo_val(c, t_vec(l->t.base, l->nsw)); glsl_ins *j = emitir(c, G_MOVSW, l->nsw, s.slot, r.slot, 0); memcpy(j->sw, l->sw, 4); return s; }
        return r;
    }
    if (l->nsw) {
        r = novo_val(c, l->t);
        glsl_ins *i = emitir(c, G_MOVSW, l->nsw, r.slot, l->slot, 0);
        memcpy(i->sw, l->sw, 4);
        return r;
    }
    r.slot = l->slot; r.t = l->t;
    return r;
}
static void atribuir_l(comp *c, lval *l, val v)
{
    int n = tam_tipo(&l->t);
    if (l->idx >= 0) {
        if (l->nsw) {   /* v[i].xy = ... : carrega o elemento, troca os componentes, grava */
            val e = novo_val(c, t_vec(l->t.base, 4));
            glsl_ins *i = emitir(c, G_IDXLD, l->idx_elem, e.slot, l->slot, l->idx); i->c = l->idx_n;
            glsl_ins *j = emitir(c, G_MOVSW, l->nsw, e.slot, v.slot, 0); memcpy(j->sw, l->sw, 4); j->ec = 1;
            glsl_ins *k = emitir(c, G_IDXST, l->idx_elem, l->slot, e.slot, l->idx); k->c = l->idx_n;
            return;
        }
        glsl_ins *i = emitir(c, G_IDXST, l->idx_elem, l->slot, v.slot, l->idx);
        i->c = l->idx_n;
        return;
    }
    if (l->nsw) {
        glsl_ins *i = emitir(c, G_MOVSW, l->nsw, l->slot, v.slot, 0);
        memcpy(i->sw, l->sw, 4); i->ec = 1;   /* swizzle no destino */
        if (tam_tipo(&v.t) == 1 && l->nsw > 1) i->ea = 1;
        return;
    }
    glsl_ins *i = emitir(c, G_MOV, n, l->slot, v.slot, 0);
    if (tam_tipo(&v.t) == 1 && n > 1) i->ea = 1;
}

/* ---- funções embutidas */
static val embutida(comp *c, no *n)
{
    val a[4]; int na = n->nargs;
    for (int i = 0; i < na && i < 4; i++) a[i] = gexpr(c, n->args[i]);
    int k = tam_tipo(&n->t);
    val r;
    switch (n->embutida) {
    case E_RADIANS: { val m; m.t = t_esc(T_FLOAT); m.slot = cte1(c, 3.14159265358979 / 180); return bin_comp(c, G_MUL, a[0], m, a[0].t); }
    case E_DEGREES: { val m; m.t = t_esc(T_FLOAT); m.slot = cte1(c, 180 / 3.14159265358979); return bin_comp(c, G_MUL, a[0], m, a[0].t); }
    case E_SIN: return un_comp(c, G_SIN, a[0]); case E_COS: return un_comp(c, G_COS, a[0]); case E_TAN: return un_comp(c, G_TAN, a[0]);
    case E_ASIN: return un_comp(c, G_ASIN, a[0]); case E_ACOS: return un_comp(c, G_ACOS, a[0]); case E_ATAN: return un_comp(c, G_ATAN, a[0]);
    case E_ATAN2: return bin_comp(c, G_ATAN2, a[0], a[1], a[0].t);
    case E_POW: return bin_comp(c, G_POW, a[0], a[1], a[0].t);
    case E_EXP: return un_comp(c, G_EXP, a[0]); case E_LOG: return un_comp(c, G_LOG, a[0]);
    case E_EXP2: return un_comp(c, G_EXP2, a[0]); case E_LOG2: return un_comp(c, G_LOG2, a[0]);
    case E_SQRT: return un_comp(c, G_SQRT, a[0]); case E_INVSQRT: return un_comp(c, G_INVSQRT, a[0]);
    case E_ABS: return un_comp(c, G_ABS, a[0]); case E_SIGN: return un_comp(c, G_SIGN, a[0]);
    case E_FLOOR: return un_comp(c, G_FLOOR, a[0]); case E_CEIL: return un_comp(c, G_CEIL, a[0]); case E_FRACT: return un_comp(c, G_FRACT, a[0]);
    case E_MOD: return bin_comp(c, G_MOD, a[0], a[1], a[0].t);
    case E_MIN: return bin_comp(c, G_MIN, a[0], a[1], a[0].t);
    case E_MAX: return bin_comp(c, G_MAX, a[0], a[1], a[0].t);
    case E_STEP: return bin_comp(c, G_STEP, a[0], a[1], a[1].t);
    case E_CLAMP: case E_MIX: case E_SMOOTH: {
        r = novo_val(c, n->t);
        glsl_ins *i = emitir(c, n->embutida == E_CLAMP ? G_CLAMP : n->embutida == E_MIX ? G_MIX : G_SMOOTH, k, r.slot, a[0].slot, a[1].slot);
        i->c = a[2].slot;
        i->ea = (unsigned char)(tam_tipo(&a[0].t) == 1 && k > 1); i->eb = (unsigned char)(tam_tipo(&a[1].t) == 1 && k > 1); i->ec = (unsigned char)(tam_tipo(&a[2].t) == 1 && k > 1);
        if (n->embutida == E_MIX && a[2].t.base == T_BOOL) i->sw[0] = 1;   /* mix com bvec: escolhe */
        return r;
    }
    case E_LENGTH: r = novo_val(c, n->t); emitir(c, G_LEN, tam_tipo(&a[0].t), r.slot, a[0].slot, 0); return r;
    case E_DISTANCE: r = novo_val(c, n->t); emitir(c, G_DIST, tam_tipo(&a[0].t), r.slot, a[0].slot, a[1].slot); return r;
    case E_DOT: r = novo_val(c, n->t); emitir(c, G_DOT, tam_tipo(&a[0].t), r.slot, a[0].slot, a[1].slot); return r;
    case E_CROSS: r = novo_val(c, n->t); emitir(c, G_CROSS, 3, r.slot, a[0].slot, a[1].slot); return r;
    case E_NORMALIZE: r = novo_val(c, n->t); emitir(c, G_NORM, k, r.slot, a[0].slot, 0); return r;
    case E_FACEFWD: { r = novo_val(c, n->t); glsl_ins *i = emitir(c, G_FACEFWD, k, r.slot, a[0].slot, a[1].slot); i->c = a[2].slot; return r; }
    case E_REFLECT: r = novo_val(c, n->t); emitir(c, G_REFLECT, k, r.slot, a[0].slot, a[1].slot); return r;
    case E_REFRACT: { r = novo_val(c, n->t); glsl_ins *i = emitir(c, G_REFRACT, k, r.slot, a[0].slot, a[1].slot); i->c = a[2].slot; return r; }
    case E_MATCOMP: return bin_comp(c, G_MUL, a[0], a[1], a[0].t);
    case E_LT: return bin_comp(c, G_LT, a[0], a[1], n->t); case E_LE: return bin_comp(c, G_LE, a[0], a[1], n->t);
    case E_GT: return bin_comp(c, G_GT, a[0], a[1], n->t); case E_GE: return bin_comp(c, G_GE, a[0], a[1], n->t);
    case E_EQ: return bin_comp(c, G_EQ, a[0], a[1], n->t); case E_NE: return bin_comp(c, G_NE, a[0], a[1], n->t);
    case E_ANY: r = novo_val(c, n->t); emitir(c, G_ANY, tam_tipo(&a[0].t), r.slot, a[0].slot, 0); return r;
    case E_ALL: r = novo_val(c, n->t); emitir(c, G_ALL, tam_tipo(&a[0].t), r.slot, a[0].slot, 0); return r;
    case E_NOT: return un_comp(c, G_NOT, a[0]);
    case E_DFDX: return un_comp(c, G_DFDX, a[0]); case E_DFDY: return un_comp(c, G_DFDY, a[0]); case E_FWIDTH: return un_comp(c, G_FWIDTH, a[0]);
    default: {   /* texturas */
        int e = n->embutida;
        int cubo = e == E_TEXCUBE || e == E_TEXCUBEB || e == E_TEXCUBELOD;
        int proj = e == E_TEX2DPROJ || e == E_TEX2DPROJB || e == E_TEX2DPROJLOD;
        int lod = e == E_TEX2DLOD || e == E_TEX2DPROJLOD || e == E_TEXCUBELOD;
        r = novo_val(c, n->t);
        val coord = a[1];
        if (proj) {   /* divide por q (último componente) */
            int nc = tam_tipo(&coord.t);
            val q; q.t = t_esc(T_FLOAT); q.slot = coord.slot + nc - 1;
            val xy; xy.t = t_vec(T_FLOAT, 2); xy.slot = coord.slot;
            coord = bin_comp(c, G_DIV, xy, q, t_vec(T_FLOAT, 2));
        }
        glsl_ins *i = emitir(c, cubo ? G_TEXCUBE : G_TEX2D, 4, r.slot, a[0].slot, coord.slot);
        i->c = na >= 3 ? a[2].slot : -1;
        i->sw[0] = (unsigned char)lod;
        return r;
    }
    }
}

/* ---- comandos */
static void gcmd(comp *c, cmd *s);
static void gdecl(comp *c, cmd *d)
{
    sim *s = d->decl;
    if (!s) return;
    if (s->classe == 4 && s->tem_cval) { s->slot = constante(c, s->cval, tam_tipo(&s->t)); return; }
    s->slot = slots(c, tam_tipo(&s->t));
    if (d->init) {
        val v = gexpr(c, d->init);
        if (!c->falhou && !tipo_igual(&v.t, &s->t)) { falha(c, "inicializacao com tipo diferente:", s->nome); return; }
        lval l; memset(&l, 0, sizeof l); l.idx = -1; l.slot = s->slot; l.t = s->t;
        atribuir_l(c, &l, v);
    }
}
static int ret_slot_atual = -1;
static void gcmd(comp *c, cmd *s)
{
    for (; s && !c->falhou; s = s->prox) {
        switch (s->k) {
        case 0: gexpr(c, s->e); break;
        case 1: gdecl(c, s); break;
        case 2: gcmd(c, s->filhos); break;
        case 3: {
            val cnd = gexpr(c, s->e);
            int cs = slots(c, 1); mov(c, cs, cnd.slot, 1);
            emitir(c, G_IF, 1, 0, cs, 0); int pif = c->prog->nins - 1;
            gcmd(c, s->a);
            int pel = -1;
            if (s->b) { emitir(c, G_ELSE, 1, 0, cs, 0); pel = c->prog->nins - 1; gcmd(c, s->b); }
            emitir(c, G_ENDIF, 1, 0, 0, 0); int pend = c->prog->nins - 1;
            c->prog->ins[pif].alvo = pel >= 0 ? pel : pend;
            if (pel >= 0) c->prog->ins[pel].alvo = pend;
            break;
        }
        case 4: case 5: case 6: {
            if (s->k == 4 && s->a) gcmd(c, s->a);   /* inicialização do for */
            emitir(c, G_LOOP, 1, 0, 0, 0); int ploop = c->prog->nins - 1;
            int topo = c->prog->nins;
            int pcond = -1;
            if (s->k != 6 && s->e) { val cnd = gexpr(c, s->e); emitir(c, G_LCOND, 1, 0, cnd.slot, 0); pcond = c->prog->nins - 1; }
            gcmd(c, s->b);
            emitir(c, G_LCONT, 1, 0, 0, 0);
            if (s->k == 4 && s->e3) gexpr(c, s->e3);
            if (s->k == 6 && s->e) { val cnd = gexpr(c, s->e); emitir(c, G_LCOND, 1, 0, cnd.slot, 0); pcond = c->prog->nins - 1; }
            glsl_ins *j = emitir(c, G_JMP, 1, 0, 0, 0); j->alvo = topo;
            emitir(c, G_LEND, 1, 0, 0, 0); int pend = c->prog->nins - 1;
            c->prog->ins[ploop].alvo = pend;
            if (pcond >= 0) c->prog->ins[pcond].alvo = pend;
            break;
        }
        case 7:
            if (s->e && ret_slot_atual >= 0) { val v = gexpr(c, s->e); mov(c, ret_slot_atual, v.slot, tam_tipo(&v.t)); }
            emitir(c, G_RETURN, 1, 0, 0, 0);
            break;
        case 8: emitir(c, G_BREAK, 1, 0, 0, 0); break;
        case 9: emitir(c, G_CONTINUE, 1, 0, 0, 0); break;
        case 10: emitir(c, G_DISCARD, 1, 0, 0, 0); c->prog->usa_discard = 1; break;
        }
    }
}
static int prof_inline;
static val chamar_usuario(comp *c, no *n)
{
    fn *f = n->f;
    val r; memset(&r, 0, sizeof r); r.t = f->ret;
    if (++prof_inline > 64) { falha(c, "chamadas aninhadas demais (recursao?)", f->nome); prof_inline--; return r; }
    /* argumentos: cópia para os parâmetros (slots novos a cada chamada) */
    lval saidas[16]; int qual[16];
    val args[16];
    for (int i = 0; i < f->np; i++) {
        qual[i] = f->qual[i];
        if (qual[i]) saidas[i] = glval(c, n->args[i]);
        args[i] = qual[i] == 1 ? (val){ 0, f->params[i]->t } : gexpr(c, n->args[i]);
    }
    for (int i = 0; i < f->np; i++) {
        f->params[i]->slot = slots(c, tam_tipo(&f->params[i]->t));
        if (qual[i] != 1) mov(c, f->params[i]->slot, qual[i] == 2 ? carregar_l(c, &saidas[i]).slot : args[i].slot, tam_tipo(&f->params[i]->t));
    }
    int ret_antes = ret_slot_atual;
    r.slot = f->ret.base != T_VOID ? slots(c, tam_tipo(&f->ret)) : -1;
    ret_slot_atual = r.slot;
    emitir(c, G_FNBEGIN, 1, 0, 0, 0);
    gcmd(c, f->corpo ? f->corpo->filhos : NULL);
    emitir(c, G_FNEND, 1, 0, 0, 0);
    ret_slot_atual = ret_antes;
    for (int i = 0; i < f->np; i++) if (qual[i]) { val v; v.slot = f->params[i]->slot; v.t = f->params[i]->t; atribuir_l(c, &saidas[i], v); }
    prof_inline--;
    if (r.slot < 0) r.slot = slots(c, 1);
    return r;
}

static void gerar(comp *c)
{
    glsl_prog *p = c->prog;
    p->s_pos = p->s_ptsize = p->s_fragcolor = p->s_fragcoord = p->s_frontfacing = p->s_pointcoord = -1;
    /* embutidas e globais ganham slots fixos */
    for (sim *s = c->escopo[0]; s; s = s->prox) {
        if (s->classe != 5) continue;
        s->slot = slots(c, tam_tipo(&s->t));
        if (!compara_texto(s->nome, "gl_Position")) p->s_pos = s->slot;
        else if (!compara_texto(s->nome, "gl_PointSize")) p->s_ptsize = s->slot;
        else if (!compara_texto(s->nome, "gl_FragColor")) p->s_fragcolor = s->slot;
        else if (!compara_texto(s->nome, "gl_FragCoord")) p->s_fragcoord = s->slot;
        else if (!compara_texto(s->nome, "gl_FrontFacing")) p->s_frontfacing = s->slot;
        else if (!compara_texto(s->nome, "gl_PointCoord")) p->s_pointcoord = s->slot;
    }
    /* gl_FragData[0] é gl_FragColor */
    for (sim *s = c->escopo[0]; s; s = s->prox) if (!compara_texto(s->nome, "gl_FragData")) s->slot = p->s_fragcolor;
    for (cmd *g = p->globais; g; g = g->prox) {
        sim *s = g->decl;
        if (!s) continue;
        if (s->classe == 4 && s->tem_cval) { s->slot = constante(c, s->cval, tam_tipo(&s->t)); continue; }
        s->slot = slots(c, tam_tipo(&s->t));
        if (s->classe == 1) registrar_unif(c, s->nome, &s->t, s->slot);
        else if (s->classe == 2) registrar_var(c, &p->attr, &p->nattr, s);
        else if (s->classe == 3) registrar_var(c, &p->vary, &p->nvary, s);
    }
    /* inicializadores das globais, depois o main */
    for (cmd *g = p->globais; g; g = g->prox) {
        sim *s = g->decl;
        if (!s || !g->init || (s->classe == 4 && s->tem_cval)) continue;
        val v = gexpr(c, g->init);
        lval l; memset(&l, 0, sizeof l); l.idx = -1; l.slot = s->slot; l.t = s->t;
        atribuir_l(c, &l, v);
    }
    fn *m = NULL;
    for (fn *f = c->fns; f; f = f->prox) if (!compara_texto(f->nome, "main") && f->definida) m = f;
    if (!m) { falha(c, "sem main()", NULL); return; }
    no chamada; memset(&chamada, 0, sizeof chamada);
    chamada.k = N_CHAMA; chamada.f = m; chamada.t = m->ret;
    prof_inline = 0;
    chamar_usuario(c, &chamada);
    emitir(c, G_FIM, 1, 0, 0, 0);
}

void glsl_liberar(glsl_prog *p)
{
    if (!p) return;
    if (p->ins) libera(p->ins);
    if (p->consts) libera(p->consts);
    if (p->const_slot) libera(p->const_slot);
    if (p->unif) libera(p->unif);
    if (p->attr) libera(p->attr);
    if (p->vary) libera(p->vary);
    libera(p);
}

/* ================================================================ máquina virtual */
#define M(s, i, v) mem[((s) + (i)) * 4 + (v)]
static float f_mod(float x, float y) { return x - y * g_floor(x / y); }
static float f_atan(float y, float x) { return (float)x87_atan2(y, x); }
static float f_pow(float x, float y)
{
    if (x == 0) return y > 0 ? 0 : y == 0 ? 1 : 1e30f;
    if (x < 0) { float iy = g_floor(y); if (iy == y) { float r = (float)x87_exp2(y * x87_log2(-x)); return ((int)iy & 1) ? -r : r; } return 0; }
    return (float)x87_exp2(y * x87_log2(x));
}

int glsl_rodar(const glsl_prog *p, float *mem, int vivo, glsl_amostrar_fn amostrar, void *ctx)
{
    /* constantes */
    for (int i = 0; i < p->nconst; i++) { float v = p->consts[i]; float *d = &mem[p->const_slot[i] * 4]; d[0] = d[1] = d[2] = d[3] = v; }
    int em = vivo & 15, morto = 0;
    /* pilhas: if (máscara salva), laços e funções (máscara de entrada, paradas, continue) */
    int pif[64], npif = 0;
    struct { int entrada, parou, cont, ret; int funcao; } reg[64]; int nreg = 0;
    reg[nreg].entrada = em; reg[nreg].parou = 0; reg[nreg].cont = 0; reg[nreg].ret = 0; reg[nreg].funcao = 1; nreg++;
    #define PARADAS() (reg[nreg - 1].parou | reg[nreg - 1].cont | reg[nreg - 1].ret | morto)
    const glsl_ins *ins = p->ins;
    int pc = 0, passos = 0;
    for (;;) {
        const glsl_ins *I = &ins[pc++];
        if (++passos > 4000000) break;   /* laço sem fim: corta */
        int n = I->n;
#ifdef GLSL_TRACE
        if (I->op >= G_IF) printf("pc %d op %d em %d nreg %d npif %d\n", pc - 1, I->op, em, nreg, npif);
#endif
        switch (I->op) {
        case G_FIM: return em | 0;
        case G_IF: {
            if (npif >= 64) break;
            pif[npif++] = em;
            int m = 0; for (int v = 0; v < 4; v++) if (M(I->a, 0, v) != 0) m |= 1 << v;
            em &= m;
            if (!em) pc = I->alvo;   /* ninguém entra: pula para o else/endif */
            if (!em && ins[pc].op == G_ELSE) { /* o else é processado normalmente */ }
            break;
        }
        case G_ELSE: {
            int salvo = npif ? pif[npif - 1] : 0;
            int m = 0; for (int v = 0; v < 4; v++) if (M(I->a, 0, v) != 0) m |= 1 << v;
            em = salvo & ~m & ~PARADAS();
            if (!em) pc = I->alvo;
            break;
        }
        case G_ENDIF: if (npif) { em = pif[--npif] & ~PARADAS(); } break;
        case G_LOOP:
            if (nreg >= 64) break;
            reg[nreg].entrada = em; reg[nreg].parou = 0; reg[nreg].cont = 0; reg[nreg].ret = 0; reg[nreg].funcao = 0; nreg++;
            if (!em) pc = I->alvo;
            break;
        case G_LCOND: {
            int m = 0; for (int v = 0; v < 4; v++) if (M(I->a, 0, v) != 0) m |= 1 << v;
            reg[nreg - 1].parou |= em & ~m;   /* quem saiu pela condição para de vez */
            em &= m;
            if (!em) pc = I->alvo;
            break;
        }
        case G_LCONT:   /* fim do corpo: quem deu continue volta */
            em = (em | reg[nreg - 1].cont) & ~(reg[nreg - 1].parou | reg[nreg - 1].ret | morto);
            reg[nreg - 1].cont = 0;
            if (!em) { while (ins[pc].op != G_LEND) pc++; }
            break;
        case G_JMP: pc = I->alvo; break;
        case G_LEND: {
            int r = reg[nreg - 1].ret, e = reg[nreg - 1].entrada;
            nreg--;
            reg[nreg - 1].ret |= r;   /* return dentro do laço vale para a função */
            em = e & ~r & ~morto;
            break;
        }
        case G_BREAK: { reg[nreg - 1].parou |= em; em = 0; break; }
        case G_CONTINUE: { reg[nreg - 1].cont |= em; em = 0; break; }
        case G_FNBEGIN:
            if (nreg >= 64) break;
            reg[nreg].entrada = em; reg[nreg].parou = 0; reg[nreg].cont = 0; reg[nreg].ret = 0; reg[nreg].funcao = 1; nreg++;
            break;
        case G_FNEND: { int e = reg[nreg - 1].entrada; nreg--; em = e & ~morto; break; }
        case G_RETURN: {
            /* marca em todas as regiões até a função */
            for (int k = nreg - 1; k >= 0; k--) { reg[k].ret |= em; if (reg[k].funcao) break; }
            em = 0;
            break;
        }
        case G_DISCARD: morto |= em; for (int k = 0; k < nreg; k++) reg[k].ret |= em; em = 0; break;
        default: break;
        }
        if (I->op >= G_IF) {
            /* pulos para dentro: quando a máscara zera, avança até o fim do bloco correspondente */
            continue;
        }
        if (!em) continue;
        int ea = I->ea, eb = I->eb, ec = I->ec;
        if (I->op == G_MOV && n > 16) {   /* struct ou array inteiro */
            const float *A = &mem[I->a * 4]; float *D = &mem[I->d * 4]; int k = n * 4;
            if (ea) { for (int j = 0; j < k; j++) if (em & (1 << (j & 3))) D[j] = A[j & 3]; }
            else if (em == 15) memmove(D, A, (unsigned)k * 4);
            else for (int j = 0; j < k; j++) if (em & (1 << (j & 3))) D[j] = A[j];
            continue;
        }
        if (I->op <= G_XOR && I->op != G_MOVSW && n <= 16) {   /* elemento a elemento: as 4 vias de uma vez, contíguas */
            float t[64]; int k = n * 4;
            const float *A = &mem[I->a * 4], *B = &mem[I->b * 4];
            switch (I->op) {
            #define EB(OP, EXPR) case OP: for (int j = 0; j < k; j++) { float x = A[ea ? (j & 3) : j], y = B[eb ? (j & 3) : j]; t[j] = (EXPR); } break;
            #define EU(OP, EXPR) case OP: for (int j = 0; j < k; j++) { float x = A[ea ? (j & 3) : j]; t[j] = (EXPR); } break;
            EU(G_MOV, x)
            EB(G_ADD, x + y) EB(G_SUB, x - y) EB(G_MUL, x * y) EB(G_DIV, y != 0 ? x / y : (x == 0 ? 0 : x > 0 ? 1e30f : -1e30f))
            EB(G_MOD, y != 0 ? f_mod(x, y) : 0) EB(G_MIN, x < y ? x : y) EB(G_MAX, x > y ? x : y)
            EB(G_POW, f_pow(x, y)) EB(G_ATAN2, f_atan(x, y)) EB(G_STEP, y < x ? 0.0f : 1.0f)
            EB(G_LT, x < y) EB(G_LE, x <= y) EB(G_GT, x > y) EB(G_GE, x >= y) EB(G_EQ, x == y) EB(G_NE, x != y)
            EB(G_AND, (x != 0) && (y != 0)) EB(G_OR, (x != 0) || (y != 0)) EB(G_XOR, (x != 0) != (y != 0))
            EU(G_NEG, -x) EU(G_NOT, x == 0) EU(G_ABS, x < 0 ? -x : x) EU(G_SIGN, x > 0 ? 1.0f : x < 0 ? -1.0f : 0.0f)
            EU(G_FLOOR, g_floor(x)) EU(G_CEIL, -g_floor(-x)) EU(G_FRACT, x - g_floor(x))
            EU(G_SQRT, x > 0 ? g_sqrt(x) : 0) EU(G_INVSQRT, x > 0 ? 1 / g_sqrt(x) : 1e30f)
            EU(G_EXP, (float)x87_exp2(x * 1.4426950408889634)) EU(G_EXP2, (float)x87_exp2(x))
            EU(G_LOG, x > 0 ? (float)(x87_log2(x) * 0.6931471805599453) : -1e30f) EU(G_LOG2, x > 0 ? (float)x87_log2(x) : -1e30f)
            EU(G_SIN, (float)x87_1(0, x)) EU(G_COS, (float)x87_1(1, x)) EU(G_TAN, (float)(x87_1(0, x) / x87_1(1, x)))
            EU(G_ASIN, (float)x87_atan2(x, x87_1(2, (1 - (double)x * x) > 0 ? 1 - (double)x * x : 0)))
            EU(G_ACOS, (float)x87_atan2(x87_1(2, (1 - (double)x * x) > 0 ? 1 - (double)x * x : 0), x))
            EU(G_ATAN, f_atan(x, 1)) EU(G_TOINT, x < 0 ? -g_floor(-x) : g_floor(x)) EU(G_TOBOOL, x != 0)
            #undef EB
            #undef EU
            default: break;
            }
            float *D = &mem[I->d * 4];
            if (em == 15) for (int j = 0; j < k; j++) D[j] = t[j];
            else for (int j = 0; j < k; j++) if (em & (1 << (j & 3))) D[j] = t[j];
            continue;
        }
        for (int v = 0; v < 4; v++) {
            if (!(em & (1 << v))) continue;
            switch (I->op) {
            case G_MOV: for (int i = 0; i < n; i++) M(I->d, i, v) = M(I->a, ea ? 0 : i, v); break;
            case G_MOVSW:
                if (ec) for (int i = 0; i < n; i++) M(I->d, I->sw[i], v) = M(I->a, ea ? 0 : i, v);
                else { float t[4]; for (int i = 0; i < n; i++) t[i] = M(I->a, I->sw[i], v); for (int i = 0; i < n; i++) M(I->d, i, v) = t[i]; }
                break;
            #define BIN(OP, EXPR) case OP: for (int i = 0; i < n; i++) { float x = M(I->a, ea ? 0 : i, v), y = M(I->b, eb ? 0 : i, v); M(I->d, i, v) = (EXPR); } break;
            BIN(G_ADD, x + y) BIN(G_SUB, x - y) BIN(G_MUL, x * y) BIN(G_DIV, y != 0 ? x / y : (x == 0 ? 0 : x > 0 ? 1e30f : -1e30f))
            BIN(G_MOD, y != 0 ? f_mod(x, y) : 0) BIN(G_MIN, x < y ? x : y) BIN(G_MAX, x > y ? x : y)
            BIN(G_POW, f_pow(x, y)) BIN(G_ATAN2, f_atan(x, y)) BIN(G_STEP, y < x ? 0.0f : 1.0f)
            BIN(G_LT, x < y) BIN(G_LE, x <= y) BIN(G_GT, x > y) BIN(G_GE, x >= y) BIN(G_EQ, x == y) BIN(G_NE, x != y)
            BIN(G_AND, (x != 0) && (y != 0)) BIN(G_OR, (x != 0) || (y != 0)) BIN(G_XOR, (x != 0) != (y != 0))
            #undef BIN
            #define UN(OP, EXPR) case OP: for (int i = 0; i < n; i++) { float x = M(I->a, ea ? 0 : i, v); M(I->d, i, v) = (EXPR); } break;
            UN(G_NEG, -x) UN(G_NOT, x == 0) UN(G_ABS, x < 0 ? -x : x) UN(G_SIGN, x > 0 ? 1.0f : x < 0 ? -1.0f : 0.0f)
            UN(G_FLOOR, g_floor(x)) UN(G_CEIL, -g_floor(-x)) UN(G_FRACT, x - g_floor(x))
            UN(G_SQRT, x > 0 ? g_sqrt(x) : 0) UN(G_INVSQRT, x > 0 ? 1 / g_sqrt(x) : 1e30f)
            UN(G_EXP, (float)x87_exp2(x * 1.4426950408889634)) UN(G_EXP2, (float)x87_exp2(x))
            UN(G_LOG, x > 0 ? (float)(x87_log2(x) * 0.6931471805599453) : -1e30f) UN(G_LOG2, x > 0 ? (float)x87_log2(x) : -1e30f)
            UN(G_SIN, (float)x87_1(0, x)) UN(G_COS, (float)x87_1(1, x)) UN(G_TAN, (float)(x87_1(0, x) / x87_1(1, x)))
            UN(G_ASIN, (float)x87_atan2(x, x87_1(2, (1 - (double)x * x) > 0 ? 1 - (double)x * x : 0)))
            UN(G_ACOS, (float)x87_atan2(x87_1(2, (1 - (double)x * x) > 0 ? 1 - (double)x * x : 0), x))
            UN(G_ATAN, f_atan(x, 1)) UN(G_TOINT, x < 0 ? -g_floor(-x) : g_floor(x)) UN(G_TOBOOL, x != 0)
            #undef UN
            case G_DOT: { float s = 0; for (int i = 0; i < n; i++) s += M(I->a, i, v) * M(I->b, i, v); M(I->d, 0, v) = s; break; }
            case G_LEN: { float s = 0; for (int i = 0; i < n; i++) s += M(I->a, i, v) * M(I->a, i, v); M(I->d, 0, v) = g_sqrt(s); break; }
            case G_DIST: { float s = 0; for (int i = 0; i < n; i++) { float d = M(I->a, i, v) - M(I->b, i, v); s += d * d; } M(I->d, 0, v) = g_sqrt(s); break; }
            case G_NORM: { float s = 0; for (int i = 0; i < n; i++) s += M(I->a, i, v) * M(I->a, i, v); float k = s > 0 ? 1 / g_sqrt(s) : 0; for (int i = 0; i < n; i++) M(I->d, i, v) = M(I->a, i, v) * k; break; }
            case G_CROSS: {
                float a0 = M(I->a, 0, v), a1 = M(I->a, 1, v), a2 = M(I->a, 2, v), b0 = M(I->b, 0, v), b1 = M(I->b, 1, v), b2 = M(I->b, 2, v);
                M(I->d, 0, v) = a1 * b2 - a2 * b1; M(I->d, 1, v) = a2 * b0 - a0 * b2; M(I->d, 2, v) = a0 * b1 - a1 * b0; break;
            }
            case G_EQALL: case G_NEALL: { int ig = 1; for (int i = 0; i < n; i++) if (M(I->a, ea ? 0 : i, v) != M(I->b, eb ? 0 : i, v)) ig = 0; M(I->d, 0, v) = (float)(I->op == G_EQALL ? ig : !ig); break; }
            case G_ANY: { int r = 0; for (int i = 0; i < n; i++) if (M(I->a, i, v) != 0) r = 1; M(I->d, 0, v) = (float)r; break; }
            case G_ALL: { int r = 1; for (int i = 0; i < n; i++) if (M(I->a, i, v) == 0) r = 0; M(I->d, 0, v) = (float)r; break; }
            case G_MATVEC: {   /* d = M * v (colunas) */
                float t[4];
                for (int l = 0; l < n; l++) { float s = 0; for (int k = 0; k < n; k++) s += M(I->a, k * n + l, v) * M(I->b, k, v); t[l] = s; }
                for (int l = 0; l < n; l++) M(I->d, l, v) = t[l];
                break;
            }
            case G_VECMAT: {   /* d = v * M */
                float t[4];
                for (int k = 0; k < n; k++) { float s = 0; for (int l = 0; l < n; l++) s += M(I->a, l, v) * M(I->b, k * n + l, v); t[k] = s; }
                for (int k = 0; k < n; k++) M(I->d, k, v) = t[k];
                break;
            }
            case G_MATMAT: {
                float t[16];
                for (int col = 0; col < n; col++)
                    for (int l = 0; l < n; l++) { float s = 0; for (int k = 0; k < n; k++) s += M(I->a, k * n + l, v) * M(I->b, col * n + k, v); t[col * n + l] = s; }
                for (int i = 0; i < n * n; i++) M(I->d, i, v) = t[i];
                break;
            }
            case G_MIX: for (int i = 0; i < n; i++) { float x = M(I->a, ea ? 0 : i, v), y = M(I->b, eb ? 0 : i, v), a = M(I->c, ec ? 0 : i, v); M(I->d, i, v) = I->sw[0] ? (a != 0 ? y : x) : x + (y - x) * a; } break;
            case G_CLAMP: for (int i = 0; i < n; i++) { float x = M(I->a, ea ? 0 : i, v), lo = M(I->b, eb ? 0 : i, v), hi = M(I->c, ec ? 0 : i, v); M(I->d, i, v) = x < lo ? lo : x > hi ? hi : x; } break;
            case G_SMOOTH: for (int i = 0; i < n; i++) { float e0 = M(I->a, ea ? 0 : i, v), e1 = M(I->b, eb ? 0 : i, v), x = M(I->c, ec ? 0 : i, v); float t = e1 != e0 ? (x - e0) / (e1 - e0) : (x < e0 ? 0 : 1); t = t < 0 ? 0 : t > 1 ? 1 : t; M(I->d, i, v) = t * t * (3 - 2 * t); } break;
            case G_REFLECT: { float d = 0; for (int i = 0; i < n; i++) d += M(I->a, i, v) * M(I->b, i, v); for (int i = 0; i < n; i++) M(I->d, i, v) = M(I->a, i, v) - 2 * d * M(I->b, i, v); break; }
            case G_REFRACT: {
                float d = 0, eta = M(I->c, 0, v); for (int i = 0; i < n; i++) d += M(I->a, i, v) * M(I->b, i, v);
                float k = 1 - eta * eta * (1 - d * d);
                if (k < 0) for (int i = 0; i < n; i++) M(I->d, i, v) = 0;
                else { float q = eta * d + g_sqrt(k); for (int i = 0; i < n; i++) M(I->d, i, v) = eta * M(I->a, i, v) - q * M(I->b, i, v); }
                break;
            }
            case G_FACEFWD: { float d = 0; for (int i = 0; i < n; i++) d += M(I->c, i, v) * M(I->b, i, v); for (int i = 0; i < n; i++) M(I->d, i, v) = d < 0 ? M(I->a, i, v) : -M(I->a, i, v); break; }
            case G_IDXLD: { int k = (int)M(I->b, 0, v); if (k < 0) k = 0; if (k >= I->c) k = I->c - 1; for (int i = 0; i < n; i++) M(I->d, i, v) = M(I->a, k * n + i, v); break; }
            case G_IDXST: { int k = (int)M(I->b, 0, v); if (k < 0 || k >= I->c) break; for (int i = 0; i < n; i++) M(I->d, k * n + i, v) = M(I->a, i, v); break; }
            case G_DFDX: case G_DFDY: case G_FWIDTH:
                /* quadrado 2x2: vias 0 1 em cima, 2 3 embaixo */
                for (int i = 0; i < n; i++) {
                    int lin = v & 2, col = v & 1;
                    float dx = M(I->a, i, lin | 1) - M(I->a, i, lin), dy = M(I->a, i, 2 | col) - M(I->a, i, col);
                    M(I->d, i, v) = I->op == G_DFDX ? dx : I->op == G_DFDY ? dy : (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                }
                break;
            default: break;
            }
        }
        /* texturas: as 4 vias de uma vez (derivadas para o LOD) */
        if (I->op == G_TEX2D || I->op == G_TEXCUBE) {
            float coord[12], bias[4], out[16];
            int nc = I->op == G_TEXCUBE ? 3 : 2;
            for (int v = 0; v < 4; v++) { for (int i = 0; i < nc; i++) coord[v * 3 + i] = M(I->b, i, v); bias[v] = I->c >= 0 ? M(I->c, 0, v) : 0; }
            int un = (int)M(I->a, 0, 0);
            if (amostrar) amostrar(ctx, un, I->op == G_TEXCUBE, coord, nc, bias, I->sw[0], out);
            else memset(out, 0, sizeof out);
            for (int v = 0; v < 4; v++) if (em & (1 << v)) for (int i = 0; i < 4; i++) M(I->d, i, v) = out[v * 4 + i];
        }
    }
    return em & ~morto;
}
