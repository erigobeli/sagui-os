/* declaracao.c - declarações, funções e comandos do C.
 *
 * É o analisador "de cima para baixo": uma função para cada construção da
 * linguagem, que olha o token da vez e decide o caminho. As expressões
 * viram árvores (expressao.c); os comandos não: if, while, for e switch
 * são traduzidos na hora em rótulos e saltos da representação
 * intermediária, na ordem em que aparecem.
 *
 * Declaradores (a parte "*v[10]" de "int *v[10]") são lidos de fora para
 * dentro, como o C manda: quando há parênteses, como em "int (*f)(int)",
 * primeiro se lê o que vem DEPOIS deles (a lista de parâmetros), e só então
 * o que está dentro. Como o programa todo já está numa lista de tokens,
 * basta guardar a posição, pular e voltar.
 *
 * Escopos: cada nome guarda o significado atual; ao declarar algo num bloco,
 * o significado antigo fica pendurado no novo e volta quando o bloco fecha. */
#include "interno.h"

/* ------------------------------------------------------------ tokens */

Token *tk(void) { return &G.toks[G.it]; }

Token *tk_adiante(int n)
{
    int i = G.it + n;
    if (i >= G.qtd_toks) i = G.qtd_toks - 1;
    return &G.toks[i];
}

void avanca(void) { if (G.it < G.qtd_toks - 1) G.it++; }
int  eh_pont(int p) { return tk()->tipo == T_PONT && tk()->pont == p; }
int  eh_chave(int c) { return tk()->tipo == T_NOME && tk()->nome->chave == c; }

int aceita(int p)
{
    if (!eh_pont(p)) return 0;
    avanca();
    return 1;
}

/* A posição logo depois do token anterior: é onde falta o ; esquecido. */
Pos pos_anterior_fim(void)
{
    Token *a;
    Pos p;
    if (G.it == 0) return tk()->pos;
    a = &G.toks[G.it - 1];
    p = a->pos;
    p.col = (u16)(p.col + (a->larg ? a->larg : 1));
    return p;
}

static const char *descreve(Token *t)
{
    static char b[64];
    if (t->tipo == T_FIM) return "o fim do arquivo";
    if (t->tipo == T_NOME) formatar(b, sizeof b, "'%N'", t->nome);
    else if (t->tipo == T_PONT) formatar(b, sizeof b, "'%s'", texto_pont(t->pont));
    else if (t->tipo == T_NUM) formatar(b, sizeof b, "o número %D", t->valor);
    else if (t->tipo == T_TEXTO) formatar(b, sizeof b, "um texto entre aspas");
    else formatar(b, sizeof b, "isto");
    return b;
}

void exige(int p, const char *dica)
{
    if (aceita(p)) return;
    if (p == ';')
        erro(pos_anterior_fim(), dica ? dica : "todo comando e toda declaração terminam com ponto e vírgula",
             "faltou ';' antes de %s", descreve(tk()));
    if (p == ')' || p == ']' || p == '}')
        erro(tk()->pos, dica, "faltou '%s' antes de %s", texto_pont(p), descreve(tk()));
    erro(tk()->pos, dica, "esperava '%s', mas veio %s", texto_pont(p), descreve(tk()));
}

/* ------------------------------------------------------------ escopos */

#define MAX_NIVEL 64
static int       nivel;
static Simbolo  *esc_sims[MAX_NIVEL];
static Etiqueta *esc_etqs[MAX_NIVEL];

static void abre_escopo(Pos p)
{
    if (++nivel == MAX_NIVEL) erro(p, "divida a função em funções menores", "blocos { } aninhados demais");
    esc_sims[nivel] = NULL;
    esc_etqs[nivel] = NULL;
}

static void fecha_escopo(void)
{
    Simbolo *s;
    Etiqueta *e;
    for (s = esc_sims[nivel]; s; s = s->prox_escopo) s->nome->sim = s->anterior;
    for (e = esc_etqs[nivel]; e; e = e->prox_escopo) e->nome->etiqueta = e->anterior;
    nivel--;
}

Simbolo *declarar(Nome *n, int classe, Tipo *t, Pos p)
{
    Simbolo *s;
    if (n->chave) erro(p, "escolha outro nome", "'%N' é uma palavra reservada do C e não pode ser nome de nada", n);
    if (n->sim && n->sim->nivel == nivel) {
        static char dica[100];
        formatar(dica, sizeof dica, "a outra declaração está na linha %u; use outro nome ou apague uma das duas", n->sim->pos.lin);
        erro(p, dica, "'%N' já foi declarado neste mesmo bloco", n);
    }
    s = novo(sizeof *s);
    s->nome = n;
    s->classe = (u8)classe;
    s->tipo = t;
    s->pos = p;
    s->nivel = nivel;
    s->local = s->sim = -1;
    s->anterior = n->sim;
    s->prox_escopo = esc_sims[nivel];
    esc_sims[nivel] = s;
    n->sim = s;
    return s;
}

static Etiqueta *declarar_etiqueta(Nome *n, Tipo *t, Pos p)
{
    Etiqueta *e = novo(sizeof *e);
    e->nome = n;
    e->tipo = t;
    e->pos = p;
    e->nivel = nivel;
    e->anterior = n->etiqueta;
    e->prox_escopo = esc_etqs[nivel];
    esc_etqs[nivel] = e;
    n->etiqueta = e;
    return e;
}

/* ------------------------------------------------------------ tipos escritos */

typedef struct {
    Tipo *tipo;
    int   guarda;       /* 0, CH_TYPEDEF, CH_EXTERN ou CH_STATIC */
    Pos   pos;
} Espec;

static int   espec(Espec *e);
static Tipo *declarador(Tipo *base, Nome **nome, Pos *pos);

int comeca_tipo(Token *t)
{
    if (t->tipo != T_NOME) return 0;
    if (t->nome->chave) return t->nome->chave <= CH_ATTRIBUTE;
    return t->nome->sim && t->nome->sim->classe == S_TYPEDEF;
}

static void pula_atributo(void)
{
    /* __attribute__((...)): o compilador do Sagui aceita e ignora */
    while (eh_chave(CH_ATTRIBUTE)) {
        int fundo = 0;
        avanca();
        if (!eh_pont('(')) erro(tk()->pos, NULL, "esperava '(' depois de __attribute__");
        do {
            if (eh_pont('(')) fundo++;
            else if (eh_pont(')')) fundo--;
            else if (tk()->tipo == T_FIM) erro(tk()->pos, NULL, "faltou ')' no __attribute__");
            avanca();
        } while (fundo);
    }
}

static u32 alinhado(u32 v, u32 a) { return (v + a - 1) / a * a; }

static Tipo *agregado(int classe)
{
    Token *t = tk();
    Pos p = t->pos;
    Nome *etq = NULL;
    Tipo *tp = NULL;
    Etiqueta *e;
    Campo **fim;
    u32 pos_byte = 0, maior = 1;
    const char *nome_cl = classe == TP_STRUCT ? "struct" : "union";
    pula_atributo();
    t = tk();
    if (t->tipo == T_NOME && !t->nome->chave) { etq = t->nome; avanca(); }
    if (!eh_pont('{')) {
        if (!etq) erro(p, "o formato é: struct nome { campos };", "depois de %s vem um nome ou os campos entre { }", nome_cl);
        e = etq->etiqueta;
        if (e && !(eh_pont(';') && e->nivel != nivel)) {
            if (e->tipo->classe != classe)
                erro(p, "use a mesma palavra (struct, union ou enum) da definição", "'%N' foi definido como outra coisa, não como %s", etq, nome_cl);
            return e->tipo;
        }
        tp = tp_agregado(classe, etq);     /* só anunciado; os campos vêm depois */
        declarar_etiqueta(etq, tp, p);
        return tp;
    }
    if (etq) {
        e = etq->etiqueta;
        if (e && e->nivel == nivel) {
            if (e->tipo->classe != classe || e->tipo->completo) {
                static char dica[100];
                formatar(dica, sizeof dica, "a outra definição está na linha %u; use outro nome ou apague uma das duas", e->pos.lin);
                erro(p, dica, "'%s %N' já foi definido", nome_cl, etq);
            }
            tp = e->tipo;
        }
    }
    if (!tp) {
        tp = tp_agregado(classe, etq);
        if (etq) declarar_etiqueta(etq, tp, p);
    }
    avanca();       /* { */
    fim = &tp->campos;
    while (!eh_pont('}')) {
        Espec es;
        if (tk()->tipo == T_FIM) erro(p, "feche a lista de campos com };", "faltou '}' no fim deste %s", nome_cl);
        if (!espec(&es))
            erro(tk()->pos, "cada campo é declarado como uma variável: tipo nome;", "esperava a declaração de um campo, mas veio %s", descreve(tk()));
        if (es.guarda) erro(es.pos, NULL, "um campo não pode ser static, extern nem typedef");
        for (;;) {
            Campo *c = novo(sizeof *c);
            Nome *n = NULL;
            Pos pc = tk()->pos;
            Tipo *tc = es.tipo;
            u32 al;
            if (!(eh_pont(';') && tp_agreg(tc))) tc = declarador(es.tipo, &n, &pc);     /* struct/union sem nome dentro */
            if (eh_pont(':'))
                erro(tk()->pos, "use um campo inteiro comum e mexa nos bits com & | << >>", "campos de bits (nome : largura) não são aceitos no C do Sagui");
            if (!n && !tp_agreg(tc)) erro(pc, "o formato é: tipo nome;", "falta o nome do campo");
            if (tc->classe == TP_FUNCAO) erro(pc, "guarde um ponteiro para função: tipo (*nome)(parametros);", "um campo não pode ser uma função");
            if (!tp_completo(tc))
                erro(pc, tc->puro == tp ? "um struct não pode conter a si mesmo; guarde um ponteiro: struct nome *prox;"
                                        : "o tipo do campo precisa estar definido por completo (e vetores precisam de tamanho)",
                     "o campo '%s' tem tipo incompleto '%T'", n ? n->texto : "(sem nome)", tc);
            if (n) {
                Campo *x;
                for (x = tp->campos; x; x = x->prox)
                    if (x->nome == n) erro(pc, "cada campo precisa de um nome diferente", "o campo '%N' aparece duas vezes", n);
            }
            al = tp_alinha(tc);
            if (al > maior) maior = al;
            c->nome = n;
            c->tipo = tc;
            if (classe == TP_STRUCT) {
                c->desloc = alinhado(pos_byte, al);
                pos_byte = c->desloc + tp_tam(tc);
            } else if (tp_tam(tc) > pos_byte) pos_byte = tp_tam(tc);
            *fim = c;
            fim = &c->prox;
            if (!aceita(',')) break;
        }
        exige(';', "cada campo termina com ponto e vírgula");
    }
    avanca();       /* } */
    if (!tp->campos) erro(p, "ponha pelo menos um campo", "%s sem nenhum campo", nome_cl);
    tp->tam = alinhado(pos_byte, maior);
    tp->alinha = maior;
    tp->completo = 1;
    pula_atributo();
    return tp;
}

static Tipo *enumeracao(void)
{
    Pos p = tk()->pos;
    i32 valor = 0;
    if (tk()->tipo == T_NOME && !tk()->nome->chave) {
        Nome *etq = tk()->nome;
        avanca();
        if (!eh_pont('{')) {
            if (!etq->etiqueta)
                erro(p, "defina o enum antes: enum nome { A, B, C };", "o enum '%N' não foi definido", etq);
            return t_int;
        }
        if (etq->etiqueta && etq->etiqueta->nivel == nivel) erro(p, NULL, "'enum %N' já foi definido", etq);
        declarar_etiqueta(etq, t_int, p);
    }
    if (!aceita('{')) erro(p, "o formato é: enum nome { A, B, C };", "depois de enum vem um nome ou a lista entre { }");
    for (;;) {
        Token *t = tk();
        Simbolo *s;
        if (t->tipo != T_NOME || t->nome->chave)
            erro(t->pos, "a lista de um enum tem só nomes, separados por vírgula: enum cor { VERDE, AZUL };", "esperava o nome de uma constante do enum, mas veio %s", descreve(t));
        avanca();
        if (aceita('=')) valor = (i32)expr_constante("o valor de uma constante de enum", NULL);
        s = declarar(t->nome, S_CONST_ENUM, t_int, t->pos);
        s->valor = valor++;
        if (!aceita(',')) break;
        if (eh_pont('}')) break;
    }
    exige('}', "feche a lista do enum com }");
    return t_int;
}

/* A primeira parte de uma declaração: "static const unsigned int". */
static int espec(Espec *e)
{
    int viu = 0, eh_const = 0, n_long = 0, com_sinal = 0, sem_sinal = 0, base = 0, algum_tipo = 0;
    e->tipo = NULL;
    e->guarda = 0;
    e->pos = tk()->pos;
    for (;;) {
        Token *t = tk();
        int c;
        if (t->tipo != T_NOME) break;
        c = t->nome->chave;
        if (!c) {
            if (algum_tipo || !t->nome->sim || t->nome->sim->classe != S_TYPEDEF) break;
            e->tipo = t->nome->sim->tipo;
            algum_tipo = 1;
            avanca();
            viu = 1;
            continue;
        }
        if (c > CH_ATTRIBUTE) break;
        viu = 1;
        switch (c) {
        case CH_TYPEDEF: case CH_EXTERN: case CH_STATIC:
            if (e->guarda) erro(t->pos, "use só uma entre typedef, extern e static", "duas classes de armazenamento na mesma declaração");
            e->guarda = c;
            break;
        case CH_AUTO: case CH_REGISTER: case CH_VOLATILE: case CH_RESTRICT: case CH_INLINE: case CH_NORETURN: break;
        case CH_ATTRIBUTE: pula_atributo(); continue;
        case CH_CONST: eh_const = 1; break;
        case CH_FLOAT: case CH_DOUBLE:
            erro(t->pos, "o C do Sagui não tem ponto flutuante; use int e guarde o valor multiplicado (centavos, milésimos)",
                 "o tipo '%N' não é aceito", t->nome);
        case CH_VOID: case CH_CHAR: case CH_INT: case CH_BOOL:
            if (base || e->tipo) erro(t->pos, "uma declaração tem um tipo só: apague um deles", "dois tipos na mesma declaração");
            base = c;
            algum_tipo = 1;
            break;
        case CH_SHORT:
            if (base == CH_SHORT || e->tipo) erro(t->pos, NULL, "dois tipos na mesma declaração");
            if (base && base != CH_INT) erro(t->pos, NULL, "'short' não combina com o outro tipo da declaração");
            base = CH_SHORT;
            algum_tipo = 1;
            break;
        case CH_LONG: n_long++; algum_tipo = 1; break;
        case CH_SIGNED: com_sinal = 1; algum_tipo = 1; break;
        case CH_UNSIGNED: sem_sinal = 1; algum_tipo = 1; break;
        case CH_STRUCT: case CH_UNION:
            if (algum_tipo) erro(t->pos, NULL, "dois tipos na mesma declaração");
            avanca();
            e->tipo = agregado(c == CH_STRUCT ? TP_STRUCT : TP_UNIAO);
            algum_tipo = 1;
            continue;
        case CH_ENUM:
            if (algum_tipo) erro(t->pos, NULL, "dois tipos na mesma declaração");
            avanca();
            e->tipo = enumeracao();
            algum_tipo = 1;
            continue;
        }
        avanca();
    }
    if (!viu) return 0;
    if (!e->tipo) {
        if (!algum_tipo)
            erro(tk()->pos, "diga o tipo antes do nome: int, char, unsigned, um struct...", "falta o tipo nesta declaração");
        if (com_sinal && sem_sinal) erro(e->pos, NULL, "'signed' e 'unsigned' juntos");
        if (n_long > 2) erro(e->pos, NULL, "'long' demais: o maior inteiro é o long long");
        if (n_long && (base == CH_CHAR || base == CH_VOID || base == CH_BOOL || base == CH_SHORT)) erro(e->pos, NULL, "'long' não combina com o outro tipo da declaração");
        if ((com_sinal || sem_sinal) && (base == CH_VOID || base == CH_BOOL)) erro(e->pos, NULL, "'signed' e 'unsigned' só valem para inteiros");
        if (base == CH_VOID) e->tipo = t_void;
        else if (base == CH_BOOL) e->tipo = t_bool;
        else if (base == CH_CHAR) e->tipo = sem_sinal ? t_uchar : com_sinal ? t_schar : t_char;
        else if (base == CH_SHORT) e->tipo = sem_sinal ? t_ushort : t_short;
        else if (n_long == 2) e->tipo = sem_sinal ? t_ulongo : t_longo;
        else e->tipo = sem_sinal ? t_uint : t_int;      /* int e long: 32 bits */
    } else if (n_long || com_sinal || sem_sinal || base) {
        erro(e->pos, NULL, "dois tipos na mesma declaração");
    }
    if (eh_const) e->tipo = tp_const(e->tipo);
    return 1;
}

static Tipo *sufixos(Tipo *base)
{
    Pos p = tk()->pos;
    if (aceita('[')) {
        Tipo *dentro;
        i64 n = 0;
        int tem = 0;
        if (!eh_pont(']')) {
            n = expr_constante("o tamanho de um vetor", "o C do Sagui não tem vetores de tamanho variável: use um tamanho fixo (um número ou #define) ou peça a memória com aloca(n * sizeof(tipo)) e devolva com libera");
            tem = 1;
            if (n <= 0) erro(p, "um vetor precisa de pelo menos um elemento", "vetor com tamanho %d", (int)n);
            if (n > 0x7FFFFFF) erro(p, NULL, "vetor grande demais");
        }
        exige(']', "feche o tamanho do vetor com ]");
        dentro = sufixos(base);
        if (dentro->classe == TP_FUNCAO) erro(p, NULL, "não existe vetor de funções; use um vetor de ponteiros para função");
        if (dentro->classe == TP_VOID) erro(p, "use char para bytes, ou void * para ponteiros sem tipo", "não existe vetor de void");
        if (!tp_completo(dentro))
            erro(p, "só o primeiro [ ] de um vetor pode ficar sem tamanho; os outros (e o tipo dos elementos) precisam estar completos",
                 "vetor de elementos de tipo incompleto '%T'", dentro);
        return tp_vetor(dentro, (u32)n, tem);
    }
    if (aceita('(')) {
        Tipo *f;
        Param ps[32];
        if (base->classe == TP_VETOR) erro(p, "devolva um ponteiro, ou um struct com o vetor dentro", "uma função não pode devolver um vetor");
        if (base->classe == TP_FUNCAO) erro(p, "devolva um ponteiro para função", "uma função não pode devolver outra função");
        f = tp_funcao(base);
        if (aceita(')')) { f->sem_lista = 1; return f; }
        if (eh_chave(CH_VOID) && tk_adiante(1)->tipo == T_PONT && tk_adiante(1)->pont == ')') { avanca(); avanca(); return f; }
        for (;;) {
            Espec e;
            Param *q;
            if (aceita(P_RETIC)) {
                if (f->qtd_params == 0) erro(p, "o formato é: tipo nome(tipo primeiro, ...)", "uma função com ... precisa de pelo menos um parâmetro com nome antes");
                f->variadica = 1;
                break;
            }
            if (!espec(&e))
                erro(tk()->pos, "cada parâmetro tem tipo e nome: int soma(int a, int b)", "esperava o tipo de um parâmetro, mas veio %s", descreve(tk()));
            if (e.guarda) erro(e.pos, NULL, "um parâmetro não pode ser static, extern nem typedef");
            if (f->qtd_params == 32) erro(e.pos, "junte os dados num struct e passe um ponteiro", "parâmetros demais (o máximo é 32)");
            q = &ps[f->qtd_params++];
            q->nome = NULL;
            q->pos = tk()->pos;
            q->tipo = declarador(e.tipo, &q->nome, &q->pos);
            if (q->tipo->classe == TP_VETOR) q->tipo = tp_ponteiro(q->tipo->base);      /* int v[] é int *v */
            else if (q->tipo->classe == TP_FUNCAO) q->tipo = tp_ponteiro(q->tipo);
            if (q->tipo->classe == TP_VOID)
                erro(q->pos, "para 'sem parâmetros' escreva só (void); para 'qualquer ponteiro' use void *", "um parâmetro não pode ser void");
            if (!aceita(',')) break;
        }
        exige(')', "feche a lista de parâmetros com )");
        f->params = novo(sizeof(Param) * (u32)f->qtd_params);
        copia_bytes(f->params, ps, sizeof(Param) * (u32)f->qtd_params);
        pula_atributo();
        return f;
    }
    return base;
}

static Tipo *declarador(Tipo *base, Nome **nome, Pos *pos)
{
    Token *t;
    pula_atributo();
    while (aceita('*')) {
        base = tp_ponteiro(base);
        for (;;) {
            if (eh_chave(CH_CONST)) { base = tp_const(base); avanca(); }
            else if (eh_chave(CH_VOLATILE) || eh_chave(CH_RESTRICT)) avanca();
            else break;
        }
    }
    pula_atributo();
    t = tk();
    if (t->tipo == T_PONT && t->pont == '(') {
        Token *d = tk_adiante(1);
        int aninhado = (d->tipo == T_PONT && (d->pont == '*' || d->pont == '(')) ||
                       (d->tipo == T_NOME && !d->nome->chave && !(d->nome->sim && d->nome->sim->classe == S_TYPEDEF));
        if (aninhado) {
            int abre = G.it, fundo = 0, depois;
            Tipo *tp;
            do {                        /* acha o ) que fecha */
                if (eh_pont('(')) fundo++;
                else if (eh_pont(')')) fundo--;
                else if (tk()->tipo == T_FIM) erro(t->pos, NULL, "faltou ')' nesta declaração");
                avanca();
            } while (fundo);
            tp = sufixos(base);         /* o que vem depois dos parênteses vale primeiro */
            depois = G.it;
            G.it = abre + 1;
            tp = declarador(tp, nome, pos);
            exige(')', NULL);
            G.it = depois;
            return tp;
        }
    }
    if (t->tipo == T_NOME && !t->nome->chave) {
        *nome = t->nome;
        *pos = t->pos;
        avanca();
    }
    pula_atributo();
    return sufixos(base);
}

Tipo *nome_de_tipo(void)
{
    Espec e;
    Nome *n = NULL;
    Pos p = tk()->pos;
    Tipo *t;
    if (!espec(&e)) erro(p, NULL, "esperava um tipo, mas veio %s", descreve(tk()));
    if (e.guarda) erro(p, NULL, "static, extern e typedef não cabem aqui");
    t = declarador(e.tipo, &n, &p);
    if (n) erro(p, "aqui vai só o tipo, sem nome de variável", "não esperava o nome '%N' depois do tipo", n);
    return t;
}

/* ------------------------------------------------------------ inicializadores */

static ItemInic **ini_fim;
static No        *pend;         /* valor já lido, esperando o lugar dele */

static void poe_item(u32 desloc, Tipo *t, No *valor, const char *texto, u32 tam)
{
    ItemInic *i = novo(sizeof *i);
    i->desloc = desloc;
    i->tipo = t;
    i->valor = valor;
    i->texto = texto;
    i->tam_texto = tam;
    *ini_fim = i;
    ini_fim = &i->prox;
}

static void inic_obj(Tipo **pt, u32 desloc, int dentro_de_lista);

/* Há mais um valor para os campos de um agregado escrito sem chaves? */
static int mais(void)
{
    Token *d = tk_adiante(1);
    if (!eh_pont(',') || (d->tipo == T_PONT && d->pont == '}')) return 0;
    if (d->tipo == T_PONT && (d->pont == '.' || d->pont == '[')) return 0;      /* um designador é da lista de fora */
    avanca();
    return 1;
}

/* { a, b, c }: os valores entre chaves de um vetor, struct ou union. */
static void lista(Tipo **pt, u32 desloc)
{
    Tipo *t = *pt;
    u32 idx = 0, maior = 0;
    Campo *c = tp_agreg(t) ? t->puro->campos : NULL;
    int feito_uniao = 0;
    while (!eh_pont('}')) {
        Pos p = tk()->pos;
        Tipo *alvo;
        u32 d_alvo;
        int designado = 0;
        if (t->classe == TP_VETOR && eh_pont('[')) {
            i64 v;
            avanca();
            v = expr_constante("o índice de um inicializador", NULL);
            exige(']', NULL);
            if (v < 0 || (t->completo && v >= (i64)t->n)) erro(p, NULL, "índice %d fora do vetor no inicializador", (int)v);
            idx = (u32)v;
            designado = 1;
        } else if (tp_agreg(t) && eh_pont('.')) {
            Token *n;
            avanca();
            n = tk();
            for (c = t->puro->campos; c && !(n->tipo == T_NOME && c->nome == n->nome); c = c->prox) ;
            if (!c) erro(n->pos, "confira o nome do campo na definição do struct", "'%T' não tem o campo que este inicializador cita", t);
            avanca();
            feito_uniao = 0;
            designado = 1;
        } else if (eh_pont('[') || eh_pont('.')) {
            erro(p, "[indice] = só serve para vetor e .campo = só para struct ou union", "este designador não combina com '%T'", t);
        }
        /* o lugar deste valor: o próximo elemento ou campo, ou o que o designador disse */
        if (t->classe == TP_VETOR) {
            if (t->completo && idx >= t->n)
                erro(p, "aumente o tamanho do vetor ou tire valores da lista", "valores demais: o vetor tem só %u elemento(s)", t->n);
            alvo = t->base;
            d_alvo = desloc + idx * tp_tam(t->base);
            idx++;
            if (idx > maior) maior = idx;
        } else {
            if (!c || feito_uniao)
                erro(p, t->classe == TP_UNIAO ? "uma union guarda um campo de cada vez: inicialize só um" : "tire valores da lista ou acrescente campos ao struct",
                     "valores demais no inicializador de '%T'", t);
            alvo = c->tipo;
            d_alvo = desloc + c->desloc;
            c = c->prox;
            if (t->classe == TP_UNIAO) feito_uniao = 1;
        }
        if (designado) {
            /* .a.b = v e [1][2] = v: desce pelos campos e índices seguintes */
            while (eh_pont('.') || eh_pont('[')) {
                Pos pd = tk()->pos;
                if (aceita('[')) {
                    i64 v = expr_constante("o índice de um inicializador", NULL);
                    exige(']', NULL);
                    if (alvo->classe != TP_VETOR || v < 0 || v >= (i64)alvo->n) erro(pd, NULL, "índice %d fora do vetor no inicializador", (int)v);
                    alvo = alvo->base;
                    d_alvo += (u32)v * tp_tam(alvo);
                } else {
                    Campo *x;
                    u32 dd = 0;
                    avanca();
                    x = tp_agreg(alvo) && tk()->tipo == T_NOME ? tp_campo(alvo, tk()->nome, &dd) : NULL;
                    if (!x) erro(tk()->pos, "confira o nome do campo na definição do struct", "'%T' não tem o campo que este inicializador cita", alvo);
                    avanca();
                    alvo = x->tipo;
                    d_alvo += dd;
                }
            }
            exige('=', "o formato é: .campo = valor ou [indice] = valor");
        }
        inic_obj(&alvo, d_alvo, 1);
        if (!aceita(',')) break;
    }
    if (t->classe == TP_VETOR && !t->completo) *pt = tp_vetor(t->base, maior, 1);
}

static void inic_obj(Tipo **pt, u32 desloc, int dentro_de_lista)
{
    Tipo *t = *pt;
    No *v;
    if (t->classe == TP_VETOR && tp_inteiro(t->base) && tp_tam(t->base) == 1 && !pend) {
        /* vetor de char iniciado com "texto" (com ou sem chaves em volta) */
        int chaves = eh_pont('{') && tk_adiante(1)->tipo == T_TEXTO;
        if (chaves || tk()->tipo == T_TEXTO) {
            No *s;
            Pos p = tk()->pos;
            u32 tam;
            if (chaves) avanca();
            s = expr_condicional();
            if (s->op != N_TEXTO) erro(p, NULL, "esperava só um texto entre aspas para iniciar o vetor de char");
            if (chaves) { aceita(','); exige('}', NULL); }
            tam = s->tipo->n;                   /* com o zero do fim */
            if (!t->completo) *pt = t = tp_vetor(t->base, tam, 1);
            else if (tam - 1 > t->n)
                erro(p, "aumente o vetor (lembre do zero que termina o texto) ou encurte o texto",
                     "o texto tem %u letras e não cabe num vetor de %u", tam - 1, t->n);
            poe_item(desloc, NULL, NULL, G.mod->sims[s->n].bytes ? (const char *)G.mod->sims[s->n].bytes : "", tam < t->n ? tam : t->n);
            return;
        }
    }
    if (t->classe == TP_VETOR || tp_agreg(t)) {
        Pos p = tk()->pos;
        if (!pend && eh_pont('{')) {
            avanca();
            lista(pt, desloc);
            exige('}', "feche a lista de valores com }");
            return;
        }
        if (tp_agreg(t)) {
            if (!pend) pend = expr_atribuicao();
            if (tp_igual(valor_de(pend)->tipo, t)) {    /* um struct inteiro copiado */
                poe_item(desloc, t, pend, NULL, 0);
                pend = NULL;
                return;
            }
        }
        if (!dentro_de_lista)
            erro(p, "os valores de um vetor ou struct vêm entre chaves: int v[3] = { 1, 2, 3 };",
                 "o inicializador de '%T' precisa de chaves { }", t);
        /* Agregado de dentro escrito sem chaves: pega da lista de fora só os
         * valores de que precisa (int m[2][2] = { 1, 2, 3, 4 }). */
        if (t->classe == TP_VETOR) {
            u32 i;
            Tipo *b = t->base;
            if (!t->completo) erro(p, NULL, "vetor sem tamanho dentro de um inicializador");
            for (i = 0; i < t->n; i++) {
                inic_obj(&b, desloc + i * tp_tam(b), 1);
                if (i + 1 < t->n && !mais()) break;
            }
        } else {
            Campo *c;
            for (c = t->puro->campos; c; c = c->prox) {
                Tipo *tc = c->tipo;
                inic_obj(&tc, desloc + c->desloc, 1);
                if (t->classe == TP_UNIAO || !c->prox || !mais()) break;
            }
        }
        return;
    }
    if (!pend && eh_pont('{')) {
        avanca();
        v = expr_atribuicao();
        aceita(',');
        exige('}', "um valor simples leva no máximo um par de chaves");
    } else {
        v = pend ? pend : expr_atribuicao();
        pend = NULL;
    }
    poe_item(desloc, t->puro, para_atribuir(v, t->puro, "inicialização"), NULL, 0);
}

ItemInic *ler_inicializador(Tipo **tipo)
{
    ItemInic *lista_itens = NULL, **fim_antes = ini_fim;
    No *pend_antes = pend;
    ini_fim = &lista_itens;
    pend = NULL;
    inic_obj(tipo, 0, 0);
    ini_fim = fim_antes;
    pend = pend_antes;
    return lista_itens;
}

static void poe_bytes(u8 *d, i64 v, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) d[i] = (u8)((u64)v >> (8 * i));
}

/* Inicializador de um dado global: tudo precisa ser conhecido ao compilar
 * (números e endereços de outras coisas globais). */
static void inic_global(SimboloRI *s, ItemInic *itens)
{
    ItemInic *i;
    s->bytes = novo(s->tam ? s->tam : 1);
    for (i = itens; i; i = i->prox) {
        i64 v;
        int sim;
        if (!i->valor) { copia_bytes(s->bytes + i->desloc, i->texto, i->tam_texto); continue; }
        if (!avalia_const(i->valor, &v, &sim) || tp_agreg(i->tipo))
            erro(i->valor->pos, "o valor inicial de uma variável global (ou static) é calculado pelo compilador: só números, textos e endereços de outras globais. "
                 "Para um valor que depende de contas do programa, atribua dentro de uma função",
                 "o inicializador de uma variável global precisa ser constante");
        if (sim >= 0) {
            if (tp_tam(i->tipo) != 4) erro(i->valor->pos, NULL, "um endereço só cabe num ponteiro (ou inteiro de 32 bits)");
            ri_reloc(s, i->desloc, sim);
        }
        poe_bytes(s->bytes + i->desloc, v, tp_tam(i->tipo));
    }
}

int texto_global(const char *bytes, u32 n)
{
    int sim = ri_simbolo_anonimo("texto");
    SimboloRI *s = &G.mod->sims[sim];
    s->especie = SR_DADO;
    s->so_leitura = 1;
    s->tam = n;
    s->alinha = 1;
    s->bytes = novo(n);
    copia_bytes(s->bytes, bytes, n);
    return sim;
}

int novo_local(Tipo *t) { return ri_local(tp_tam(t), tp_alinha(t), -1); }

/* ------------------------------------------------------------ globais */

static const char *dica_anterior(Simbolo *s)
{
    static char dica[120];
    formatar(dica, sizeof dica, "a outra declaração está na linha %u e diz '%T'; as duas precisam ser iguais", s->pos.lin, s->tipo);
    return dica;
}

/* Declara (ou reencontra) uma variável global ou função. */
static Simbolo *global(Nome *n, Tipo *t, Pos p, int guarda)
{
    Simbolo *s = n->sim, *g;
    int funcao = t->classe == TP_FUNCAO;
    /* procura a declaração de nível 0, mesmo que um nome local a esconda */
    for (g = s; g && !g->global; g = g->anterior) ;
    if (g) {
        if (g->classe != (funcao ? S_FUNCAO : S_VAR) || !tp_igual(g->tipo, t))
            erro(p, dica_anterior(g), "'%N' já foi declarado de outro jeito", n);
        if (funcao && g->tipo->sem_lista && !t->sem_lista) g->tipo = t;
        if (t->classe == TP_VETOR && t->completo) g->tipo = t;
        if (guarda == CH_STATIC && !g->estatico && nivel == 0)
            erro(p, dica_anterior(g), "'%N' foi declarado antes sem static e agora com static", n);
        if (s && s->nivel == nivel) return g;
    }
    if (s && s->nivel == nivel && !s->global) {
        static char dica[100];
        formatar(dica, sizeof dica, "a outra declaração está na linha %u; use outro nome ou apague uma das duas", s->pos.lin);
        erro(p, dica, "'%N' já foi declarado neste mesmo bloco", n);
    }
    s = declarar(n, funcao ? S_FUNCAO : S_VAR, g ? g->tipo : t, p);
    s->global = 1;
    s->estatico = g ? g->estatico : (u8)(guarda == CH_STATIC);
    s->sim = g ? g->sim : ri_simbolo(n->texto);
    if (!g) G.mod->sims[s->sim].global = !s->estatico;
    return s;
}

static void define_dado(Simbolo *s, Tipo *t, ItemInic *itens, int tem_inic, Pos p)
{
    SimboloRI *r = &G.mod->sims[s->sim];
    if (r->especie == SR_FUNCAO) erro(p, NULL, "'%N' já é uma função", s->nome);
    if (tem_inic && r->bytes)
        erro(p, dica_anterior(s), "a variável '%N' já recebeu um valor inicial em outra declaração", s->nome);
    if (!tp_completo(t))
        erro(p, t->classe == TP_VETOR ? "dê o tamanho entre [ ] ou um inicializador: int v[10]; ou int v[] = { 1, 2 };"
                                      : "defina o struct por completo antes de criar uma variável dele",
             "a variável '%N' tem tipo incompleto '%T'", s->nome, t);
    r->especie = SR_DADO;
    r->tam = tp_tam(t);
    r->alinha = tp_alinha(t);
    r->so_leitura = t->constante || (t->classe == TP_VETOR && t->base->constante);
    s->tipo = t;
    s->definido = 1;
    if (tem_inic) inic_global(r, itens);
}

static void sem_nome(Pos p)
{
    Token *t = tk();
    if (t->tipo == T_NOME && t->nome->chave)
        erro(t->pos, "escolha outro nome para a variável ou função", "'%N' é uma palavra reservada do C e não pode ser usada como nome", t->nome);
    erro(p, "o formato é: tipo nome;", "falta o nome nesta declaração (veio %s)", descreve(t));
}

/* typedef struct { ... } ponto_t: as mensagens passam a chamar o struct de ponto_t. */
static void apelida(Tipo *t, Nome *n)
{
    if (tp_agreg(t) && !t->puro->etiqueta && !t->puro->apelido) t->puro->apelido = n;
}

/* ------------------------------------------------------------ comandos */

typedef struct Caso { struct Caso *prox; i32 valor; int rotulo; } Caso;
typedef struct Selecao {
    struct Selecao *fora;
    Caso *casos, **fim;
    int   rot_padrao;
} Selecao;
typedef struct Marca { struct Marca *prox; Nome *nome; int rotulo; u8 posta; Pos pos; } Marca;

#define MAX_LACOS 64
static int      rot_sai[MAX_LACOS], rot_segue[MAX_LACOS], n_sai, n_segue;
static Selecao *selecao;
static Marca   *marcas;
static Tipo    *tipo_retorno;
static int      local_escondido;
static Nome    *nome_funcao;
static int      conta_estaticos;

static void comando(void);

static Marca *marca_de(Nome *n, Pos p)
{
    Marca *m;
    for (m = marcas; m; m = m->prox) if (m->nome == n) return m;
    m = novo(sizeof *m);
    m->nome = n;
    m->rotulo = ri_rotulo();
    m->pos = p;
    m->prox = marcas;
    marcas = m;
    return m;
}

static void declaracao_local(void)
{
    Espec e;
    espec(&e);
    if (aceita(';')) return;
    for (;;) {
        Nome *n = NULL;
        Pos p = tk()->pos;
        Tipo *t = declarador(e.tipo, &n, &p);
        Simbolo *s;
        if (!n) sem_nome(p);
        if (e.guarda == CH_TYPEDEF) { apelida(t, n); declarar(n, S_TYPEDEF, t, p); }
        else if (t->classe == TP_FUNCAO) {
            if (eh_pont('{')) erro(p, "defina a função fora, antes da função que a usa", "não se define uma função dentro de outra");
            global(n, t, p, 0);
        }
        else if (e.guarda == CH_EXTERN) global(n, t, p, CH_EXTERN);
        else if (t->classe == TP_VOID)
            erro(p, "void quer dizer 'nada'; para um ponteiro sem tipo use void *", "a variável '%N' não pode ser void", n);
        else if (e.guarda == CH_STATIC) {
            /* variável que mora fora da pilha e guarda o valor entre as chamadas */
            char nome[120];
            ItemInic *itens = NULL;
            int tem = aceita('=');
            if (tem) itens = ler_inicializador(&t);
            formatar(nome, sizeof nome, "%N.%N.%d", nome_funcao, n, conta_estaticos++);
            s = declarar(n, S_VAR, t, p);
            s->global = 1;
            s->estatico = 1;
            s->sim = ri_simbolo(nome);
            G.mod->sims[s->sim].global = 0;         /* o nome não sai deste arquivo */
            define_dado(s, t, itens, tem, p);
        }
        else if (aceita('=')) {
            ItemInic *itens;
            Token *ini = tk();
            int com_chaves = eh_pont('{') || t->classe == TP_VETOR;
            /* o nome já vale dentro do próprio inicializador: no_t *n = aloca(sizeof *n) */
            s = declarar(n, S_VAR, t, p);
            itens = ler_inicializador(&t);
            if (!tp_completo(t)) erro(ini->pos, NULL, "a variável '%N' tem tipo incompleto '%T'", n, t);
            s->tipo = t;            /* int v[] = { ... } só agora sabe o tamanho */
            s->local = novo_local(t);
            gera_inic_local(s->local, t, itens, com_chaves);
        }
        else {
            if (!tp_completo(t))
                erro(p, t->classe == TP_VETOR ? "dê o tamanho entre [ ] ou um inicializador: int v[10]; ou int v[] = { 1, 2 };"
                                              : "defina o struct por completo antes de criar uma variável dele",
                     "a variável '%N' tem tipo incompleto '%T'", n, t);
            s = declarar(n, S_VAR, t, p);
            s->local = novo_local(t);
        }
        if (!aceita(',')) break;
    }
    exige(';', NULL);
}

static No *condicao(void)
{
    No *c;
    exige('(', "a condição vem entre parênteses: if (x > 0)");
    c = valor_de(expressao());
    if (c->tipo->classe == TP_VOID || tp_agreg(c->tipo))
        erro(c->pos, "uma condição é um número (zero = falso) ou um ponteiro", "'%T' não serve como condição", c->tipo);
    exige(')', "feche a condição com )");
    return c;
}

static void bloco(void)
{
    Pos p = tk()->pos;
    avanca();       /* { */
    while (!eh_pont('}')) {
        if (tk()->tipo == T_FIM) erro(p, "cada { precisa do seu }", "este bloco aberto com '{' não foi fechado");
        if (comeca_tipo(tk()) && !(tk_adiante(1)->tipo == T_PONT && tk_adiante(1)->pont == ':')) declaracao_local();
        else comando();
    }
    avanca();
}

static void comando(void)
{
    Token *t = tk();
    Pos p = t->pos;
    int a, b, c;
    No *n;
    if (t->tipo == T_PONT && t->pont == '{') {
        abre_escopo(p);
        bloco();
        fecha_escopo();
        return;
    }
    if (aceita(';')) return;
    if (t->tipo == T_NOME && !t->nome->chave && tk_adiante(1)->tipo == T_PONT && tk_adiante(1)->pont == ':') {
        Marca *m = marca_de(t->nome, p);
        if (m->posta) erro(p, "cada rótulo de goto precisa de um nome diferente dentro da função", "o rótulo '%N' aparece duas vezes", t->nome);
        m->posta = 1;
        avanca(); avanca();
        ri_poe_rotulo(m->rotulo);
        if (eh_pont('}')) return;       /* rótulo no fim do bloco */
        comando();
        return;
    }
    if (t->tipo != T_NOME || !t->nome->chave || t->nome->chave < CH_IF || t->nome->chave >= CH_SIZEOF) {
        if (comeca_tipo(t))
            erro(p, "ponha a declaração dentro de um bloco { } ou antes do comando", "uma declaração não pode ser o corpo direto de um if, else ou laço");
        gera_descarta(expressao());
        exige(';', NULL);
        return;
    }
    avanca();
    switch (t->nome->chave) {
    case CH_IF:
        n = condicao();
        a = ri_rotulo();
        gera_salto(n, a, 0);
        comando();
        if (eh_chave(CH_ELSE)) {
            avanca();
            b = ri_rotulo();
            ri_salta(b);
            ri_poe_rotulo(a);
            comando();
            ri_poe_rotulo(b);
        } else ri_poe_rotulo(a);
        return;
    case CH_ELSE:
        erro(p, "o else vem logo depois do comando (ou do bloco { }) do if; confira se há um ; ou } sobrando antes", "este 'else' não tem um 'if' antes");
    case CH_WHILE:
        a = ri_rotulo(); b = ri_rotulo();
        ri_poe_rotulo(a);
        n = condicao();
        gera_salto(n, b, 0);
        if (n_sai == MAX_LACOS) erro(p, NULL, "laços aninhados demais");
        rot_sai[n_sai++] = b; rot_segue[n_segue++] = a;
        comando();
        n_sai--; n_segue--;
        ri_salta(a);
        ri_poe_rotulo(b);
        return;
    case CH_DO:
        a = ri_rotulo(); b = ri_rotulo(); c = ri_rotulo();
        ri_poe_rotulo(a);
        if (n_sai == MAX_LACOS) erro(p, NULL, "laços aninhados demais");
        rot_sai[n_sai++] = b; rot_segue[n_segue++] = c;
        comando();
        n_sai--; n_segue--;
        if (!eh_chave(CH_WHILE)) erro(tk()->pos, "o formato é: do { ... } while (condicao);", "faltou o 'while' no fim do 'do'");
        avanca();
        ri_poe_rotulo(c);
        n = condicao();
        gera_salto(n, a, 1);
        ri_poe_rotulo(b);
        exige(';', "o do ... while (condicao) termina com ponto e vírgula");
        return;
    case CH_FOR: {
        No *passo = NULL;
        exige('(', "o formato é: for (inicio; condicao; passo)");
        abre_escopo(p);
        if (comeca_tipo(tk())) declaracao_local();
        else {
            if (!eh_pont(';')) gera_descarta(expressao());
            exige(';', "as três partes do for são separadas por ponto e vírgula: for (i = 0; i < n; i++)");
        }
        a = ri_rotulo(); b = ri_rotulo(); c = ri_rotulo();
        ri_poe_rotulo(a);
        if (!eh_pont(';')) {
            n = valor_de(expressao());
            if (n->tipo->classe == TP_VOID || tp_agreg(n->tipo)) erro(n->pos, NULL, "'%T' não serve como condição", n->tipo);
            gera_salto(n, b, 0);
        }
        exige(';', "as três partes do for são separadas por ponto e vírgula: for (i = 0; i < n; i++)");
        if (!eh_pont(')')) passo = expressao();
        exige(')', "feche o for com )");
        if (n_sai == MAX_LACOS) erro(p, NULL, "laços aninhados demais");
        rot_sai[n_sai++] = b; rot_segue[n_segue++] = c;
        comando();
        n_sai--; n_segue--;
        ri_poe_rotulo(c);
        if (passo) gera_descarta(passo);
        ri_salta(a);
        ri_poe_rotulo(b);
        fecha_escopo();
        return;
    }
    case CH_SWITCH: {
        Selecao s;
        Caso *k;
        Oper v;
        int tmp, teste = ri_rotulo(), fim = ri_rotulo();
        n = condicao();
        if (!tp_inteiro(n->tipo)) erro(n->pos, "o switch escolhe por um número inteiro (int, char, enum)", "o switch não funciona com '%T'", n->tipo);
        v = gera(converte(n, t_int));
        tmp = ri_temp(4);
        ri_emite(RI_MOVE, RT_I32, tmp, v, o_const(0));
        ri_salta(teste);
        s.fora = selecao; s.casos = NULL; s.fim = &s.casos; s.rot_padrao = -1;
        selecao = &s;
        if (n_sai == MAX_LACOS) erro(p, NULL, "laços aninhados demais");
        rot_sai[n_sai++] = fim;
        comando();
        n_sai--;
        selecao = s.fora;
        ri_salta(fim);
        /* A escolha vem depois do corpo, quando todos os casos já são conhecidos:
         * uma fila de comparações, cada uma saltando para o seu caso. */
        ri_poe_rotulo(teste);
        for (k = s.casos; k; k = k->prox) {
            int igual = ri_temp(4);
            ri_emite(RI_IGUAL, RT_I32, igual, o_temp(tmp), o_const(k->valor));
            ri_emite(RI_SALTA_SE_NAO_ZERO, RT_I32, -1, o_temp(igual), o_const(0))->x = k->rotulo;
        }
        if (s.rot_padrao >= 0) ri_salta(s.rot_padrao);
        ri_poe_rotulo(fim);
        return;
    }
    case CH_CASE: {
        Caso *k, *x;
        if (!selecao) erro(p, "o case só existe dentro das chaves de um switch", "'case' fora de um switch");
        k = novo(sizeof *k);
        k->valor = (i32)expr_constante("o valor de um case", "cada case compara com um número fixo: um número, uma letra entre aspas simples, uma constante de enum ou um #define; para comparar com variáveis use if e else if");
        for (x = selecao->casos; x; x = x->prox)
            if (x->valor == k->valor) erro(p, "cada valor só pode aparecer num case do mesmo switch", "o case %d aparece duas vezes", k->valor);
        exige(':', "o formato é: case valor:");
        k->rotulo = ri_rotulo();
        ri_poe_rotulo(k->rotulo);
        *selecao->fim = k;
        selecao->fim = &k->prox;
        if (!eh_pont('}')) comando();
        return;
    }
    case CH_DEFAULT:
        if (!selecao) erro(p, "o default só existe dentro das chaves de um switch", "'default' fora de um switch");
        if (selecao->rot_padrao >= 0) erro(p, NULL, "o switch já tem um 'default'");
        exige(':', "o formato é: default:");
        selecao->rot_padrao = ri_rotulo();
        ri_poe_rotulo(selecao->rot_padrao);
        if (!eh_pont('}')) comando();
        return;
    case CH_BREAK:
        if (!n_sai) erro(p, "o break sai de um while, do, for ou switch; fora deles use return", "'break' fora de laço e de switch");
        ri_salta(rot_sai[n_sai - 1]);
        exige(';', NULL);
        return;
    case CH_CONTINUE:
        if (!n_segue) erro(p, "o continue volta ao começo de um while, do ou for", "'continue' fora de laço");
        ri_salta(rot_segue[n_segue - 1]);
        exige(';', NULL);
        return;
    case CH_RETURN:
        if (eh_pont(';')) {
            if (tipo_retorno->classe != TP_VOID)
                erro(p, "escreva o valor depois do return, ou declare a função como void", "'return' sem valor numa função que devolve '%T'", tipo_retorno);
            gera_retorno(NULL, tipo_retorno, local_escondido);
        } else {
            n = expressao();
            if (tipo_retorno->classe == TP_VOID) {
                if (valor_de(n)->tipo->classe != TP_VOID)
                    erro(n->pos, "tire o valor do return, ou troque o void da declaração pelo tipo do valor", "'return' com valor numa função void");
                gera_descarta(n);
                gera_retorno(NULL, tipo_retorno, local_escondido);
            } else gera_retorno(para_atribuir(n, tipo_retorno->puro, "return"), tipo_retorno, local_escondido);
        }
        exige(';', NULL);
        return;
    case CH_GOTO: {
        Token *r = tk();
        if (r->tipo != T_NOME || r->nome->chave) erro(r->pos, "o formato é: goto nome_do_rotulo;", "falta o nome do rótulo depois de goto");
        avanca();
        ri_salta(marca_de(r->nome, r->pos)->rotulo);
        exige(';', NULL);
        return;
    }
    }
}

/* ------------------------------------------------------------ funções */

static void funcao(Simbolo *s, Tipo *t, Pos p)
{
    SimboloRI *r = &G.mod->sims[s->sim];
    u32 desloc = 0;
    int i;
    Marca *m;
    if (r->especie != SR_INDEFINIDO) {
        static char dica[120];
        formatar(dica, sizeof dica, "a outra definição está na linha %u; apague uma das duas ou dê outro nome", s->pos.lin);
        erro(p, dica, "a função '%N' já foi definida", s->nome);
    }
    if (t->sem_lista) t->sem_lista = 0;         /* numa definição, () quer dizer sem parâmetros */
    s->tipo = t;
    s->definido = 1;
    r->especie = SR_FUNCAO;
    ri_funcao_inicio(s->sim);
    tipo_retorno = t->base;
    nome_funcao = s->nome;
    marcas = NULL;
    local_escondido = -1;
    if (tp_agreg(t->base)) {
        /* Função que devolve struct: quem chama passa, escondido na frente dos
         * argumentos, o endereço de onde quer o resultado (convenção do i386). */
        if (!tp_completo(t->base)) erro(p, NULL, "a função devolve '%T', que ainda não foi definido por completo", t->base);
        local_escondido = ri_local(4, 4, 0);
        desloc = 4;
        G.f->desempilha = 4;
    }
    abre_escopo(p);
    for (i = 0; i < t->qtd_params; i++) {
        Param *q = &t->params[i];
        Simbolo *ps;
        if (!q->nome) erro(q->pos, "na definição de uma função todo parâmetro tem nome: int dobro(int x)", "falta o nome do parâmetro %d", i + 1);
        if (!tp_completo(q->tipo)) erro(q->pos, NULL, "o parâmetro '%N' tem tipo incompleto '%T'", q->nome, q->tipo);
        ps = declarar(q->nome, S_VAR, q->tipo, q->pos);
        ps->local = ri_local(tp_tam(q->tipo), tp_alinha(q->tipo), (i32)desloc);
        desloc += alinhado(tp_tam(q->tipo), 4);
    }
    bloco();
    fecha_escopo();
    for (m = marcas; m; m = m->prox)
        if (!m->posta) erro(m->pos, "crie o rótulo em algum lugar da mesma função, assim: nome:", "o goto vai para o rótulo '%N', que não existe nesta função", m->nome);
    /* Quem chega ao fim sem return volta com zero (ou sem nada). */
    gera_retorno(tp_escalar(t->base) ? no_num(0, t->base->puro, p) : NULL, t->base, local_escondido);
    ri_funcao_fim();
}

static void declaracao_externa(void)
{
    Espec e;
    Token *t = tk();
    int primeiro = 1;
    if (aceita(';')) return;
    if (!espec(&e)) {
        if (t->tipo == T_NOME && t->nome->chave >= CH_IF && t->nome->chave <= CH_GOTO)
            erro(t->pos, "comandos (if, while, return...) só podem ficar dentro de uma função, como a principal",
                 "o comando '%N' está fora de qualquer função", t->nome);
        if (t->tipo == T_NOME && !t->nome->chave) {
            Token *d = tk_adiante(1);
            if (t->nome->sim && d->tipo == T_PONT && (d->pont == '(' || d->pont == '=' || d->pont == '['))
                erro(t->pos, "fora das funções só cabem declarações; ponha este comando dentro de uma função, como a principal",
                     "este comando está fora de qualquer função");
            erro(t->pos, "toda declaração começa pelo tipo: int, char, void, um struct, um typedef... Os tipos uint8_t, uint32_t, bool e size_t vêm de #include \"sagui.h\"",
                 "esperava um tipo no começo da declaração, mas '%N' não é um tipo conhecido", t->nome);
        }
        if (t->tipo == T_PONT && t->pont == '}')
            erro(t->pos, "há um } a mais; confira se cada { tem um só }", "este '}' não fecha nada");
        erro(t->pos, "fora das funções só cabem declarações de variáveis, tipos e funções", "não esperava %s aqui", descreve(t));
    }
    if (aceita(';')) return;        /* só um struct, union ou enum */
    for (;;) {
        Nome *n = NULL;
        Pos p = tk()->pos;
        Tipo *tp = declarador(e.tipo, &n, &p);
        Simbolo *s;
        if (!n) sem_nome(p);
        if (e.guarda == CH_TYPEDEF) {
            apelida(tp, n);
            if (n->sim && n->sim->nivel == 0 && n->sim->classe == S_TYPEDEF && tp_igual(n->sim->tipo, tp)) ;   /* repetido igual: tudo bem */
            else declarar(n, S_TYPEDEF, tp, p);
        }
        else if (tp->classe == TP_FUNCAO) {
            s = global(n, tp, p, e.guarda);
            if (eh_pont('{')) {
                if (!primeiro) erro(p, NULL, "a definição de uma função não pode vir depois de uma vírgula");
                funcao(s, tp, p);
                return;
            }
        }
        else {
            ItemInic *itens = NULL;
            int tem;
            if (tp->classe == TP_VOID)
                erro(p, "void quer dizer 'nada'; para um ponteiro sem tipo use void *", "a variável '%N' não pode ser void", n);
            s = global(n, tp, p, e.guarda);
            tem = aceita('=');
            if (tem) {
                if (e.guarda == CH_EXTERN) erro(p, "tire o extern (ele só anuncia uma variável definida em outro lugar) ou tire o valor", "variável extern com valor inicial");
                itens = ler_inicializador(&tp);
            }
            if (e.guarda != CH_EXTERN) define_dado(s, tp, itens, tem, p);
        }
        primeiro = 0;
        if (!aceita(',')) break;
    }
    if (!eh_pont(';') && eh_pont('{'))
        erro(tk()->pos, "antes do { de uma função vem o cabeçalho completo: tipo nome(parametros)", "não esperava '{' aqui");
    exige(';', NULL);
}

void analisar_unidade(void)
{
    Pos nada = { 0, 0, 0 };
    nivel = 0;
    esc_sims[0] = NULL;
    esc_etqs[0] = NULL;
    n_sai = n_segue = 0;
    selecao = NULL;
    conta_estaticos = 0;
    pend = NULL;
    G.fundura = 0;
    declarar(nome_c("__builtin_va_list"), S_TYPEDEF, t_ptr_char, nada);
    while (tk()->tipo != T_FIM) declaracao_externa();
}
