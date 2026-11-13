/* gerar.c - da árvore do C para a representação intermediária (ri.h).
 *
 * É a última parte da frente. Aqui cada nó da árvore vira uma ou mais
 * instruções simples, com o resultado num temporário. Três ideias guiam o
 * arquivo:
 *
 *   - gera() devolve o VALOR de uma expressão; gera_end() devolve o ENDEREÇO
 *     de algo que mora na memória. Um struct (ou vetor) nunca cabe num
 *     temporário, então o "valor" dele é o endereço: copiar um struct é
 *     copiar um bloco de bytes de um endereço para outro.
 *   - condições (if, while, &&, ||) não calculam 0 ou 1 para depois testar:
 *     gera_salto() traduz direto em saltos, que é como && e || "param no
 *     meio" quando o resultado já é conhecido.
 *   - endereços de variáveis não gastam instrução: "o local 3 mais 8 bytes"
 *     é um operando pronto (O_LOCAL), e somar uma constante a ele só muda o
 *     deslocamento.
 */
#include "interno.h"

/* ------------------------------------------------------------ montagem */

int ri_simbolo(const char *nome)
{
    ModuloRI *m = G.mod;
    int i;
    for (i = 0; i < m->qtd; i++)
        if (txt_igual(m->sims[i].nome, nome)) return i;
    m->sims = crescer(m->sims, &m->cap, m->qtd, sizeof(SimboloRI));
    zera_bytes(&m->sims[m->qtd], sizeof(SimboloRI));
    m->sims[m->qtd].nome = copia_txt(nome, tam_txt(nome));
    m->sims[m->qtd].global = 1;
    return m->qtd++;
}

int ri_simbolo_anonimo(const char *prefixo)
{
    ModuloRI *m = G.mod;
    char nome[64];
    formatar(nome, sizeof nome, ".%s.%d", prefixo, m->qtd);
    m->sims = crescer(m->sims, &m->cap, m->qtd, sizeof(SimboloRI));
    zera_bytes(&m->sims[m->qtd], sizeof(SimboloRI));
    m->sims[m->qtd].nome = copia_txt(nome, tam_txt(nome));
    return m->qtd++;
}

void ri_reloc(SimboloRI *s, u32 desloc, int sim)
{
    s->relocs = crescer(s->relocs, &s->cap_relocs, s->qtd_relocs, sizeof(RelocRI));
    s->relocs[s->qtd_relocs].desloc = desloc;
    s->relocs[s->qtd_relocs].sim = sim;
    s->qtd_relocs++;
}

void ri_funcao_inicio(int sim)
{
    G.f = novo(sizeof(FuncaoRI));
    G.mod->sims[sim].funcao = G.f;
}

void ri_funcao_fim(void) { G.f = NULL; }

int ri_local(u32 tam, u32 alinha, i32 parametro)
{
    FuncaoRI *f = G.f;
    f->locais = crescer(f->locais, &f->cap_locais, f->qtd_locais, sizeof(LocalRI));
    f->locais[f->qtd_locais].tam = tam;
    f->locais[f->qtd_locais].alinha = alinha;
    f->locais[f->qtd_locais].parametro = parametro;
    return f->qtd_locais++;
}

int ri_temp(int largura)
{
    FuncaoRI *f = G.f;
    f->larg = crescer(f->larg, &f->cap_temps, f->qtd_temps, 1);
    f->larg[f->qtd_temps] = (u8)largura;
    return f->qtd_temps++;
}

Oper o_const(i64 v) { Oper o; o.classe = O_CONST; o.n = 0; o.c = v; return o; }
Oper o_temp(int t) { Oper o; o.classe = O_TEMP; o.n = t; o.c = 0; return o; }
Oper o_local(int l, i64 d) { Oper o; o.classe = O_LOCAL; o.n = l; o.c = d; return o; }
Oper o_global(int s, i64 d) { Oper o; o.classe = O_GLOBAL; o.n = s; o.c = d; return o; }
static Oper o_nada(void) { Oper o; o.classe = O_NADA; o.n = 0; o.c = 0; return o; }

Instr *ri_emite(int op, int tipo, i32 dst, Oper a, Oper b)
{
    FuncaoRI *f = G.f;
    Instr *i;
    f->ins = crescer(f->ins, &f->cap, f->qtd, sizeof(Instr));
    i = &f->ins[f->qtd++];
    zera_bytes(i, sizeof *i);
    i->op = (u8)op;
    i->tipo = (u8)tipo;
    i->dst = dst;
    i->a = a;
    i->b = b;
    return i;
}

int  ri_rotulo(void) { return G.f->qtd_rotulos++; }
void ri_poe_rotulo(int r) { ri_emite(RI_ROTULO, RT_NADA, -1, o_nada(), o_nada())->x = r; }
void ri_salta(int r) { ri_emite(RI_SALTA, RT_NADA, -1, o_nada(), o_nada())->x = r; }

/* ------------------------------------------------------------ expressões */

static Oper gera_end(No *n);

static int largura(Tipo *t) { return t->classe == TP_LONGO || t->classe == TP_ULONGO ? 8 : 4; }
static int tipo_conta(Tipo *t) { return largura(t) == 8 ? RT_I64 : RT_I32; }
static int eh_bloco(Tipo *t) { return tp_agreg(t) || t->classe == TP_VETOR || t->classe == TP_FUNCAO; }

static Oper conta(int op, int tipo, int larg_dst, Oper a, Oper b)
{
    int t;
    if (tipo == RT_I32 && b.classe == O_CONST && (a.classe == O_LOCAL || a.classe == O_GLOBAL)) {
        if (op == RI_SOMA) { a.c += (i32)b.c; return a; }
        if (op == RI_SUB) { a.c -= (i32)b.c; return a; }
    }
    if (tipo == RT_I32 && op == RI_SOMA && a.classe == O_CONST && (b.classe == O_LOCAL || b.classe == O_GLOBAL)) {
        b.c += (i32)a.c;
        return b;
    }
    t = ri_temp(larg_dst);
    ri_emite(op, tipo, t, a, b);
    return o_temp(t);
}

static Oper carrega(Oper end, Tipo *t)
{
    int tmp;
    if (eh_bloco(t)) return end;
    tmp = ri_temp(largura(t));
    ri_emite(RI_CARREGA, tp_ri(t), tmp, end, o_nada());
    return o_temp(tmp);
}

/* Um tipo pequeno cabe inteiro no outro? (então converter não muda nada) */
static int cabe(Tipo *de, Tipo *para)
{
    if (de->classe == para->classe || de->classe == TP_BOOL) return 1;
    if (para->tam >= 4) return 1;
    return de->tam < para->tam && (tp_sem_sinal(de) || !tp_sem_sinal(para));
}

static Oper converte_valor(Oper v, Tipo *de, Tipo *para)
{
    int t;
    if (para->classe == TP_VOID || eh_bloco(de) || eh_bloco(para)) return v;
    if (para->classe == TP_BOOL) {
        if (de->classe == TP_BOOL) return v;
        return conta(RI_DIFERENTE, tipo_conta(de), 4, v, o_const(0));
    }
    if (largura(de) == 8 && largura(para) == 4) {
        t = ri_temp(4);
        ri_emite(RI_ESTREITA, RT_I64, t, v, o_nada());
        v = o_temp(t);
        if (para->tam < 4) {
            t = ri_temp(4);
            ri_emite(RI_ESTENDE, tp_ri(para), t, v, o_nada());
            v = o_temp(t);
        }
        return v;
    }
    if (largura(de) == 4 && largura(para) == 8) {
        t = ri_temp(8);
        ri_emite(tp_sem_sinal(de) ? RI_ALARGA_U : RI_ALARGA_S, RT_I32, t, v, o_nada());
        return o_temp(t);
    }
    if (largura(para) == 4 && para->tam < 4 && !cabe(de, para)) {
        t = ri_temp(4);
        ri_emite(RI_ESTENDE, tp_ri(para), t, v, o_nada());
        return o_temp(t);
    }
    return v;
}

static Oper soma_const(Oper base, i64 d)
{
    if (d == 0) return base;
    return conta(RI_SOMA, RT_I32, 4, base, o_const(d));
}

static u32 quatro(u32 n) { return (n + 3) & ~3u; }

static Oper gera_chamada(No *n)
{
    ArgRI args[42];
    int qtd = 0, i, primeiro, dst = -1, temp_bloco = -1;
    Oper alvo, r = o_const(0);
    Instr *ins;
    Tipo *dev = n->tipo;
    if (tp_agreg(dev)) {
        temp_bloco = ri_local(tp_tam(dev), tp_alinha(dev), -1);
        args[qtd].v = o_local(temp_bloco, 0);
        args[qtd].tipo = RT_I32;
        args[qtd++].tam = 4;
    }
    alvo = gera(n->a);
    for (i = 0; i < n->qtd_args; i++) {
        Tipo *t = n->args[i]->tipo;
        args[qtd].v = gera(n->args[i]);
        if (tp_agreg(t)) { args[qtd].tipo = RT_BLOCO; args[qtd].tam = tp_tam(t); }
        else { args[qtd].tipo = (u8)tipo_conta(t); args[qtd].tam = (u32)largura(t); }
        qtd++;
    }
    /* Só agora os argumentos entram na lista da função: uma chamada dentro de
     * um argumento já pôs os dela, e cada lista precisa ficar junta. */
    primeiro = G.f->qtd_args;
    for (i = 0; i < qtd; i++) {
        G.f->args = crescer(G.f->args, &G.f->cap_args, G.f->qtd_args, sizeof(ArgRI));
        G.f->args[G.f->qtd_args++] = args[i];
    }
    if (dev->classe != TP_VOID && temp_bloco < 0) dst = ri_temp(largura(dev));
    ins = ri_emite(RI_CHAMA, temp_bloco >= 0 ? RT_BLOCO : dev->classe == TP_VOID ? RT_NADA : tipo_conta(dev), dst, alvo, o_nada());
    ins->x = primeiro;
    ins->y = qtd;
    ins->memoria = temp_bloco >= 0;
    if (temp_bloco >= 0) return o_local(temp_bloco, 0);
    if (dst >= 0) {
        r = o_temp(dst);
        /* Uma função que devolve char ou short só garante os bits de baixo. */
        if (dev->tam < 4) {
            int t = ri_temp(4);
            ri_emite(RI_ESTENDE, tp_ri(dev), t, r, o_nada());
            r = o_temp(t);
        }
    }
    return r;
}

static int op_ri(int op, Tipo *t)
{
    int sem = tp_sem_sinal(t);
    switch (op) {
    case N_SOMA: return RI_SOMA;
    case N_SUB: return RI_SUB;
    case N_MUL: return RI_MUL;
    case N_DIV: return sem ? RI_DIV_U : RI_DIV_S;
    case N_RESTO: return sem ? RI_RESTO_U : RI_RESTO_S;
    case N_E: return RI_E;
    case N_OU: return RI_OU;
    case N_XOU: return RI_XOU;
    case N_DESL_E: return RI_DESL_E;
    case N_DESL_D: return sem ? RI_DESL_D_U : RI_DESL_D_S;
    case N_IGUAL: return RI_IGUAL;
    case N_DIF: return RI_DIFERENTE;
    case N_MENOR: return sem ? RI_MENOR_U : RI_MENOR_S;
    case N_MENOR_IG: return sem ? RI_MENOR_IG_U : RI_MENOR_IG_S;
    }
    return RI_NADA;
}

Oper gera(No *n)
{
    Oper a, b, e;
    int t, r1, r2;
    switch (n->op) {
    case N_NUM: return o_const(n->valor);
    case N_TEXTO: return o_global(n->n, 0);
    case N_VAR: case N_DEREF: case N_MEMBRO: case N_COMPOSTO:
        return carrega(gera_end(n), n->tipo);
    case N_END: return gera_end(n->a);
    case N_CONV: return converte_valor(gera(n->a), n->a->tipo, n->tipo);
    case N_NEG: case N_INVERTE:
        a = gera(n->a);
        t = ri_temp(largura(n->tipo));
        ri_emite(n->op == N_NEG ? RI_NEG : RI_INVERTE, tipo_conta(n->tipo), t, a, o_nada());
        return o_temp(t);
    case N_NAO:
        a = gera(n->a);
        return conta(RI_IGUAL, tipo_conta(n->a->tipo), 4, a, o_const(0));
    case N_SOMA: case N_SUB: case N_MUL: case N_DIV: case N_RESTO: case N_E: case N_OU: case N_XOU:
    case N_DESL_E: case N_DESL_D:
        a = gera(n->a);
        b = gera(n->b);
        return conta(op_ri(n->op, n->tipo), tipo_conta(n->tipo), largura(n->tipo), a, b);
    case N_IGUAL: case N_DIF: case N_MENOR: case N_MENOR_IG:
        a = gera(n->a);
        b = gera(n->b);
        return conta(op_ri(n->op, n->a->tipo), tipo_conta(n->a->tipo), 4, a, b);
    case N_E_LOG: case N_OU_LOG:
        t = ri_temp(4);
        r1 = ri_rotulo();
        r2 = ri_rotulo();
        gera_salto(n, r1, 0);
        ri_emite(RI_MOVE, RT_I32, t, o_const(1), o_nada());
        ri_salta(r2);
        ri_poe_rotulo(r1);
        ri_emite(RI_MOVE, RT_I32, t, o_const(0), o_nada());
        ri_poe_rotulo(r2);
        return o_temp(t);
    case N_COND:
        r1 = ri_rotulo();
        r2 = ri_rotulo();
        t = n->tipo->classe == TP_VOID ? -1 : ri_temp(eh_bloco(n->tipo) ? 4 : largura(n->tipo));
        gera_salto(n->a, r1, 0);
        a = gera(n->b);
        if (t >= 0) ri_emite(RI_MOVE, largura(n->tipo) == 8 ? RT_I64 : RT_I32, t, a, o_nada());
        ri_salta(r2);
        ri_poe_rotulo(r1);
        b = gera(n->c);
        if (t >= 0) ri_emite(RI_MOVE, largura(n->tipo) == 8 ? RT_I64 : RT_I32, t, b, o_nada());
        ri_poe_rotulo(r2);
        return t >= 0 ? o_temp(t) : o_const(0);
    case N_VIRGULA:
        gera(n->a);
        return gera(n->b);
    case N_ATR:
        if (tp_agreg(n->tipo)) {
            b = gera(n->b);
            e = gera_end(n->a);
            ri_emite(RI_COPIA, RT_BLOCO, -1, e, b)->x = (i32)tp_tam(n->tipo);
            return e;
        }
        e = gera_end(n->a);
        b = gera(n->b);
        ri_emite(RI_GUARDA, tp_ri(n->tipo), -1, e, b);
        return b;
    case N_ATR_OP: {
        Oper antigo, nv;
        Tipo *tc = n->tipo_conta;
        e = gera_end(n->a);
        b = gera(n->b);
        antigo = carrega(e, n->a->tipo);
        a = converte_valor(antigo, n->a->tipo, tc);
        nv = conta(op_ri(n->sub, tc), tipo_conta(tc), largura(tc), a, b);
        /* p += n com p local: a "conta" pode ter virado só um endereço pronto */
        nv = converte_valor(nv, tc, n->tipo);
        ri_emite(RI_GUARDA, tp_ri(n->tipo), -1, e, nv);
        return n->pos_fixo ? antigo : nv;
    }
    case N_CHAMA: return gera_chamada(n);
    case N_VA_INICIO:
        /* Os argumentos extras ficam na pilha logo depois do último parâmetro com nome. */
        e = gera_end(n->a);
        b = gera_end(n->b);
        ri_emite(RI_GUARDA, RT_I32, -1, e, soma_const(b, quatro(tp_tam(n->b->tipo))));
        return o_const(0);
    case N_VA_ARG:
        e = gera_end(n->a);
        t = ri_temp(4);
        ri_emite(RI_CARREGA, RT_I32, t, e, o_nada());
        ri_emite(RI_GUARDA, RT_I32, -1, e, conta(RI_SOMA, RT_I32, 4, o_temp(t), o_const(quatro(tp_tam(n->tipo)))));
        return carrega(o_temp(t), n->tipo);
    }
    erro(n->pos, NULL, "erro interno do compilador: nó %d sem tradução", n->op);
}

static Oper gera_end(No *n)
{
    switch (n->op) {
    case N_VAR: return n->sim->global ? o_global(n->sim->sim, 0) : o_local(n->sim->local, 0);
    case N_DEREF: return gera(n->a);
    case N_MEMBRO: return soma_const(gera(n->a), n->valor);     /* o "valor" de um struct é o endereço dele */
    case N_TEXTO: return o_global(n->n, 0);
    case N_COMPOSTO:
        gera_inic_local(n->n, n->tipo, n->itens, 1);
        return o_local(n->n, 0);
    case N_CHAMA: case N_COND: case N_ATR: case N_VIRGULA:
        if (eh_bloco(n->tipo)) return gera(n);
    }
    erro(n->pos, NULL, "erro interno do compilador: endereço de um nó %d", n->op);
}

void gera_descarta(No *n) { gera(n); }

void gera_salto(No *n, int rotulo, int se_verdade)
{
    Oper v;
    int r;
    switch (n->op) {
    case N_NUM:
        if ((n->valor != 0) == (se_verdade != 0)) ri_salta(rotulo);
        return;
    case N_NAO:
        gera_salto(n->a, rotulo, !se_verdade);
        return;
    case N_E_LOG: case N_OU_LOG:
        if ((n->op == N_E_LOG) == !se_verdade) {
            /* (a && b) falso, ou (a || b) verdadeiro: basta um dos dois */
            gera_salto(n->a, rotulo, se_verdade);
            gera_salto(n->b, rotulo, se_verdade);
        } else {
            r = ri_rotulo();
            gera_salto(n->a, r, !se_verdade);
            gera_salto(n->b, rotulo, se_verdade);
            ri_poe_rotulo(r);
        }
        return;
    }
    v = gera(n);
    if (largura(n->tipo) == 8 && !eh_bloco(n->tipo)) v = conta(RI_DIFERENTE, RT_I64, 4, v, o_const(0));
    ri_emite(se_verdade ? RI_SALTA_SE_NAO_ZERO : RI_SALTA_SE_ZERO, RT_I32, -1, v, o_nada())->x = rotulo;
}

void gera_inic_local(int local, Tipo *t, ItemInic *itens, int zerar)
{
    ItemInic *i;
    if (zerar) ri_emite(RI_ZERA, RT_BLOCO, -1, o_local(local, 0), o_nada())->x = (i32)tp_tam(t);
    for (i = itens; i; i = i->prox) {
        Oper v;
        if (!i->valor) {
            if (i->tam_texto <= 1) continue;        /* texto vazio: o zero já está lá */
            v = o_global(texto_global(i->texto, i->tam_texto), 0);
            ri_emite(RI_COPIA, RT_BLOCO, -1, o_local(local, i->desloc), v)->x = (i32)i->tam_texto;
            continue;
        }
        v = gera(i->valor);
        if (tp_agreg(i->tipo)) ri_emite(RI_COPIA, RT_BLOCO, -1, o_local(local, i->desloc), v)->x = (i32)tp_tam(i->tipo);
        else ri_emite(RI_GUARDA, tp_ri(i->tipo), -1, o_local(local, i->desloc), v);
    }
}

void gera_retorno(No *n, Tipo *devolve, int local_escondido)
{
    Oper v;
    if (local_escondido >= 0) {
        int p = ri_temp(4);
        if (n) {
            v = gera(n);
            ri_emite(RI_CARREGA, RT_I32, p, o_local(local_escondido, 0), o_nada());
            ri_emite(RI_COPIA, RT_BLOCO, -1, o_temp(p), v)->x = (i32)tp_tam(devolve);
        } else ri_emite(RI_CARREGA, RT_I32, p, o_local(local_escondido, 0), o_nada());
        ri_emite(RI_RETORNA, RT_I32, -1, o_temp(p), o_nada());
        return;
    }
    if (!n) { ri_emite(RI_RETORNA, RT_NADA, -1, o_nada(), o_nada()); return; }
    v = gera(n);
    ri_emite(RI_RETORNA, tipo_conta(devolve), -1, v, o_nada());
}

/* ------------------------------------------------------------ constantes */

static int const_end(No *n, i64 *valor, int *sim)
{
    switch (n->op) {
    case N_VAR:
        if (!n->sim->global) return 0;
        *valor = 0;
        *sim = n->sim->sim;
        return 1;
    case N_TEXTO: *valor = 0; *sim = n->n; return 1;
    case N_DEREF: return avalia_const(n->a, valor, sim);
    case N_MEMBRO:
        if (!const_end(n->a, valor, sim)) return 0;
        *valor += n->valor;
        return 1;
    }
    return 0;
}

/* O valor de uma expressão que não depende de o programa rodar: um número,
 * ou o endereço de um símbolo global mais um número. */
int avalia_const(No *n, i64 *valor, int *sim)
{
    i64 vb;
    int sb;
    *sim = -1;
    switch (n->op) {
    case N_NUM: *valor = n->valor; return 1;
    case N_END: return const_end(n->a, valor, sim);
    case N_CONV:
        if (!avalia_const(n->a, valor, sim)) return 0;
        if (*sim >= 0) return n->tipo->tam == 4 && tp_escalar(n->tipo);
        if (tp_escalar(n->tipo)) *valor = no_num(*valor, n->tipo, n->pos)->valor;
        return 1;
    case N_SOMA: case N_SUB:
        if (!avalia_const(n->a, valor, sim) || !avalia_const(n->b, &vb, &sb)) return 0;
        if (sb >= 0) {
            if (*sim >= 0 || n->op == N_SUB) return 0;
            *sim = sb;
        }
        *valor = n->op == N_SOMA ? *valor + vb : *valor - vb;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ listagem */

static const char *NOME_OP[RI_QTD] = {
    "nada", "move", "carrega", "guarda", "soma", "sub", "mul", "div", "div_u", "resto", "resto_u",
    "e", "ou", "xou", "desl_e", "desl_d", "desl_d_u", "igual", "diferente", "menor", "menor_ig", "menor_u", "menor_ig_u",
    "neg", "inverte", "estende", "alarga", "alarga_u", "estreita", "rotulo", "salta", "salta_se_zero", "salta_se_nao_zero",
    "chama", "retorna", "copia", "zera"
};
static const char *NOME_TIPO[] = { "", ".i8", ".u8", ".i16", ".u16", ".i32", ".i64", ".bloco" };

static u32 lista_oper(char *d, u32 max, Oper o)
{
    switch (o.classe) {
    case O_TEMP: return (u32)formatar(d, max, "t%d", o.n);
    case O_CONST: return (u32)formatar(d, max, "%D", o.c);
    case O_LOCAL: return (u32)formatar(d, max, o.c ? "&local%d+%D" : "&local%d", o.n, o.c);
    case O_GLOBAL: return (u32)formatar(d, max, o.c ? "&%s+%D" : "&%s", G.mod->sims[o.n].nome, o.c);
    }
    d[0] = 0;
    return 0;
}

/* A RI em texto (cc --ri): serve para ver o que a frente entrega ao fundo. */
void ri_listar(ModuloRI *m)
{
    char l[400];
    int s, i, k;
    if (!G.amb->listagem) return;
    for (s = 0; s < m->qtd; s++) {
        SimboloRI *x = &m->sims[s];
        FuncaoRI *f = x->funcao;
        if (x->especie == SR_DADO) {
            formatar(l, sizeof l, "dado %s: %u bytes%s%s\n", x->nome, x->tam, x->bytes ? "" : ", zerado", x->so_leitura ? ", só leitura" : "");
            G.amb->listagem(G.amb->ctx, l);
        }
        if (x->especie != SR_FUNCAO) continue;
        formatar(l, sizeof l, "funcao %s: %d locais, %d temporarios\n", x->nome, f->qtd_locais, f->qtd_temps);
        G.amb->listagem(G.amb->ctx, l);
        for (i = 0; i < f->qtd; i++) {
            Instr *in = &f->ins[i];
            u32 n = 0;
            if (in->op == RI_ROTULO) { formatar(l, sizeof l, "  r%d:\n", in->x); G.amb->listagem(G.amb->ctx, l); continue; }
            n += (u32)formatar(l + n, sizeof l - n, "    ");
            if (in->dst >= 0) n += (u32)formatar(l + n, sizeof l - n, "t%d = ", in->dst);
            n += (u32)formatar(l + n, sizeof l - n, "%s%s ", NOME_OP[in->op], NOME_TIPO[in->tipo]);
            n += lista_oper(l + n, sizeof l - n, in->a);
            if (in->b.classe != O_NADA) { n += (u32)formatar(l + n, sizeof l - n, ", "); n += lista_oper(l + n, sizeof l - n, in->b); }
            if (in->op == RI_SALTA || in->op == RI_SALTA_SE_ZERO || in->op == RI_SALTA_SE_NAO_ZERO)
                n += (u32)formatar(l + n, sizeof l - n, "%sr%d", in->op == RI_SALTA ? "" : ", ", in->x);
            if (in->op == RI_COPIA || in->op == RI_ZERA) n += (u32)formatar(l + n, sizeof l - n, ", %d bytes", in->x);
            if (in->op == RI_CHAMA) {
                n += (u32)formatar(l + n, sizeof l - n, " (");
                for (k = 0; k < in->y && n + 60 < sizeof l; k++) {
                    if (k) n += (u32)formatar(l + n, sizeof l - n, ", ");
                    n += lista_oper(l + n, sizeof l - n, f->args[in->x + k].v);
                }
                n += (u32)formatar(l + n, sizeof l - n, ")");
            }
            formatar(l + n, sizeof l - n, "\n");
            G.amb->listagem(G.amb->ctx, l);
        }
    }
}
