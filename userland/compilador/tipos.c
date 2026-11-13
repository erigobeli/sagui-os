/* tipos.c - os tipos do C do Sagui e as perguntas que se faz a eles.
 *
 * Tamanhos (os mesmos do GCC para x86 de 32 bits, porque os programas são
 * ligados à libsagui compilada por ele): char 1, short 2, int e long 4,
 * long long 8, ponteiro 4. Dentro de um struct um long long se alinha em 4
 * bytes, como manda a convenção System V do i386. */
#include "interno.h"

Tipo *t_void, *t_bool, *t_char, *t_schar, *t_uchar, *t_short, *t_ushort, *t_int, *t_uint,
     *t_longo, *t_ulongo, *t_ptr_char, *t_ptr_void;

static Tipo *basico(int classe, u32 tam)
{
    Tipo *t = novo(sizeof *t);
    t->classe = (u8)classe;
    t->tam = tam;
    t->alinha = tam > 4 ? 4 : tam ? tam : 1;
    t->completo = classe != TP_VOID;
    t->puro = t;
    return t;
}

void tipos_iniciar(void)
{
    static const struct { const char *t; int c; } CHAVES[] = {
        { "void", CH_VOID }, { "char", CH_CHAR }, { "short", CH_SHORT }, { "int", CH_INT }, { "long", CH_LONG },
        { "signed", CH_SIGNED }, { "unsigned", CH_UNSIGNED }, { "_Bool", CH_BOOL }, { "float", CH_FLOAT },
        { "double", CH_DOUBLE }, { "struct", CH_STRUCT }, { "union", CH_UNION }, { "enum", CH_ENUM },
        { "typedef", CH_TYPEDEF }, { "extern", CH_EXTERN }, { "static", CH_STATIC }, { "auto", CH_AUTO },
        { "register", CH_REGISTER }, { "const", CH_CONST }, { "volatile", CH_VOLATILE }, { "restrict", CH_RESTRICT },
        { "__restrict", CH_RESTRICT }, { "inline", CH_INLINE }, { "__inline", CH_INLINE }, { "__inline__", CH_INLINE },
        { "_Noreturn", CH_NORETURN }, { "__attribute__", CH_ATTRIBUTE }, { "__attribute", CH_ATTRIBUTE },
        { "if", CH_IF }, { "else", CH_ELSE }, { "while", CH_WHILE }, { "do", CH_DO }, { "for", CH_FOR },
        { "switch", CH_SWITCH }, { "case", CH_CASE }, { "default", CH_DEFAULT }, { "break", CH_BREAK },
        { "continue", CH_CONTINUE }, { "return", CH_RETURN }, { "goto", CH_GOTO }, { "sizeof", CH_SIZEOF },
        { "__builtin_va_start", CH_VA_START }, { "__builtin_va_arg", CH_VA_ARG }, { "__builtin_va_end", CH_VA_END },
        { "_Alignof", CH_ALIGNOF }, { "__alignof__", CH_ALIGNOF },
        { NULL, 0 }
    };
    int i;
    for (i = 0; CHAVES[i].t; i++) nome_c(CHAVES[i].t)->chave = (u8)CHAVES[i].c;
    t_void = basico(TP_VOID, 1);
    t_bool = basico(TP_BOOL, 1);
    t_char = basico(TP_CHAR, 1);
    t_schar = basico(TP_SCHAR, 1);
    t_uchar = basico(TP_UCHAR, 1);
    t_short = basico(TP_SHORT, 2);
    t_ushort = basico(TP_USHORT, 2);
    t_int = basico(TP_INT, 4);
    t_uint = basico(TP_UINT, 4);
    t_longo = basico(TP_LONGO, 8);
    t_ulongo = basico(TP_ULONGO, 8);
    t_ptr_char = tp_ponteiro(t_char);
    t_ptr_void = tp_ponteiro(t_void);
}

Tipo *tp_ponteiro(Tipo *base)
{
    Tipo *t = basico(TP_PONTEIRO, 4);
    t->base = base;
    return t;
}

Tipo *tp_vetor(Tipo *base, u32 n, int completo)
{
    Tipo *t = basico(TP_VETOR, base->tam * n);
    t->base = base;
    t->n = n;
    t->alinha = base->alinha;
    t->completo = (u8)completo;
    return t;
}

Tipo *tp_funcao(Tipo *devolve)
{
    Tipo *t = basico(TP_FUNCAO, 1);
    t->base = devolve;
    return t;
}

Tipo *tp_agregado(int classe, Nome *etiqueta)
{
    Tipo *t = basico(classe, 0);
    t->completo = 0;
    t->etiqueta = etiqueta;
    return t;
}

Tipo *tp_puro(Tipo *t) { return t->puro; }

/* A versão "const" de um tipo é uma cópia com a marca. A cópia aponta para
 * o original (puro), e é nele que se consultam os campos e o tamanho de um
 * struct: assim um "const struct x" declarado antes de o struct x ganhar os
 * campos continua certo depois. */
Tipo *tp_const(Tipo *t)
{
    Tipo *c;
    if (t->constante) return t;
    if (t->classe == TP_VETOR) return tp_vetor(tp_const(t->base), t->n, t->completo);
    c = novo(sizeof *c);
    *c = *t;
    c->constante = 1;
    c->puro = t->puro;
    return c;
}

/* Tamanho e alinhamento de verdade (um struct "const" consulta o original). */
u32 tp_tam(Tipo *t)
{
    if (tp_agreg(t)) return t->puro->tam;
    if (t->classe == TP_VETOR) return tp_tam(t->base) * t->n;
    return t->tam;
}

u32 tp_alinha(Tipo *t)
{
    if (tp_agreg(t)) return t->puro->alinha;
    if (t->classe == TP_VETOR) return tp_alinha(t->base);
    return t->alinha;
}

int tp_inteiro(Tipo *t) { return t->classe >= TP_BOOL && t->classe <= TP_ULONGO; }
int tp_escalar(Tipo *t) { return tp_inteiro(t) || t->classe == TP_PONTEIRO; }
int tp_agreg(Tipo *t)   { return t->classe == TP_STRUCT || t->classe == TP_UNIAO; }

int tp_sem_sinal(Tipo *t)
{
    return t->classe == TP_BOOL || t->classe == TP_UCHAR || t->classe == TP_USHORT || t->classe == TP_UINT ||
           t->classe == TP_ULONGO || t->classe == TP_PONTEIRO;
}

int tp_completo(Tipo *t)
{
    if (tp_agreg(t)) return t->puro->completo;
    if (t->classe == TP_VETOR) return t->completo && tp_completo(t->base);
    return t->classe != TP_VOID && t->classe != TP_FUNCAO;
}

int tp_igual(Tipo *a, Tipo *b)
{
    int i;
    if (a->classe != b->classe) return 0;
    switch (a->classe) {
    case TP_PONTEIRO: return tp_igual(a->base, b->base);
    case TP_VETOR: return tp_igual(a->base, b->base) && (!a->completo || !b->completo || a->n == b->n);
    case TP_STRUCT: case TP_UNIAO: return a->puro == b->puro;
    case TP_FUNCAO:
        if (!tp_igual(a->base, b->base)) return 0;
        if (a->sem_lista || b->sem_lista) return 1;
        if (a->qtd_params != b->qtd_params || a->variadica != b->variadica) return 0;
        for (i = 0; i < a->qtd_params; i++)
            if (!tp_igual(a->params[i].tipo, b->params[i].tipo)) return 0;
        return 1;
    }
    return 1;
}

/* Procura um campo pelo nome; entra nos structs e unions sem nome de dentro
 * (os campos deles valem como se fossem do de fora). */
Campo *tp_campo(Tipo *t, Nome *n, u32 *desloc)
{
    Campo *c;
    for (c = t->puro->campos; c; c = c->prox) {
        if (c->nome == n) { *desloc = c->desloc; return c; }
        if (!c->nome && tp_agreg(c->tipo)) {
            u32 d;
            Campo *dentro = tp_campo(c->tipo, n, &d);
            if (dentro) { *desloc = c->desloc + d; return dentro; }
        }
    }
    return NULL;
}

int tp_ri(Tipo *t)
{
    switch (t->classe) {
    case TP_BOOL: case TP_UCHAR: return RT_U8;
    case TP_CHAR: case TP_SCHAR: return RT_I8;
    case TP_SHORT: return RT_I16;
    case TP_USHORT: return RT_U16;
    case TP_LONGO: case TP_ULONGO: return RT_I64;
    case TP_STRUCT: case TP_UNIAO: case TP_VETOR: return RT_BLOCO;
    case TP_VOID: return RT_NADA;
    }
    return RT_I32;
}

/* O tipo por extenso, para as mensagens: "unsigned char *", "struct ponto". */
void tp_texto(Tipo *t, char *dst, u32 max)
{
    static const char *NOMES[] = { "void", "bool", "char", "signed char", "unsigned char", "short", "unsigned short",
                                   "int", "unsigned int", "long long", "unsigned long long" };
    char base[120];
    const char *c = t->constante ? "const " : "";
    switch (t->classe) {
    case TP_PONTEIRO:
        if (t->base->classe == TP_FUNCAO) {
            u32 k;
            tp_texto(t->base, base, sizeof base);       /* "int (int, int)" vira "int (*)(int, int)" */
            for (k = 0; base[k] && !(base[k] == ' ' && base[k + 1] == '('); k++) ;
            if (base[k]) { base[k] = 0; formatar(dst, max, "%s (*)%s", base, base + k + 1); }
            else formatar(dst, max, "ponteiro para função");
            return;
        }
        tp_texto(t->base, base, sizeof base);
        formatar(dst, max, base[tam_txt(base) - 1] == '*' ? "%s*%s" : "%s *%s", base, t->constante ? " const" : "");
        return;
    case TP_VETOR:
        tp_texto(t->base, base, sizeof base);
        if (t->completo) formatar(dst, max, "%s [%u]", base, t->n);
        else formatar(dst, max, "%s []", base);
        return;
    case TP_FUNCAO: {
        u32 k;
        int i;
        tp_texto(t->base, base, sizeof base);
        k = (u32)formatar(dst, max, "%s (", base);
        for (i = 0; i < t->qtd_params && k + 30 < max; i++) {
            tp_texto(t->params[i].tipo, base, sizeof base);
            k += (u32)formatar(dst + k, max - k, i ? ", %s" : "%s", base);
        }
        if (t->variadica && k + 8 < max) k += (u32)formatar(dst + k, max - k, ", ...");
        if (!t->qtd_params && !t->sem_lista && k + 8 < max) k += (u32)formatar(dst + k, max - k, "void");
        if (k + 2 < max) formatar(dst + k, max - k, ")");
        return;
    }
    case TP_STRUCT: case TP_UNIAO:
        if (t->puro->etiqueta) formatar(dst, max, "%s%s %N", c, t->classe == TP_STRUCT ? "struct" : "union", t->puro->etiqueta);
        else if (t->puro->apelido) formatar(dst, max, "%s%N", c, t->puro->apelido);
        else formatar(dst, max, "%s%s sem nome", c, t->classe == TP_STRUCT ? "struct" : "union");
        return;
    }
    formatar(dst, max, "%s%s", c, NOMES[t->classe]);
}
