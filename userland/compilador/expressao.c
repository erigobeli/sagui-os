/* expressao.c - lê as expressões do C, confere os tipos e monta a árvore.
 *
 * Cada função "expr_..." lê um nível da gramática e devolve um nó (No). Os
 * nós já saem conferidos e com as conversões que o C faz por conta própria
 * escritas às claras (N_CONV): quem gera a representação intermediária não
 * precisa conhecer as regras do C, só percorrer a árvore.
 *
 * Três escolhas que simplificam o resto:
 *   - contas só com números são feitas aqui mesmo (2 * 3 vira 6); por isso
 *     "expressão constante" é simplesmente "a árvore virou um N_NUM";
 *   - a soma de ponteiro com inteiro já sai multiplicada pelo tamanho do
 *     elemento (p + i vira p + i * 4), e v[i] vira *(v + i);
 *   - > e >= são escritos como < e <= com os lados trocados.
 *
 * Os operadores de dois lados são lidos por um laço só, guiado pela tabela
 * de precedências, em vez de uma função por nível: gasta menos pilha, que
 * no SaguiOS é curta. */
#include "interno.h"

static No *expr_unaria(void);
static No *expr_binaria(int minimo);

No *no_novo(int op, Tipo *t, Pos p)
{
    No *n = novo(sizeof *n);
    n->op = (u8)op;
    n->tipo = t;
    n->pos = p;
    return n;
}

/* Põe o número no formato do tipo: corta o que não cabe e estende o sinal. */
static i64 normaliza(i64 v, Tipo *t)
{
    switch (t->classe) {
    case TP_BOOL: return v != 0;
    case TP_CHAR: case TP_SCHAR: return (i8)v;
    case TP_UCHAR: return (u8)v;
    case TP_SHORT: return (i16)v;
    case TP_USHORT: return (u16)v;
    case TP_INT: return (i32)v;
    case TP_UINT: case TP_PONTEIRO: return (u32)v;
    }
    return v;
}

No *no_num(i64 v, Tipo *t, Pos p)
{
    No *n = no_novo(N_NUM, t, p);
    n->valor = normaliza(v, t);
    return n;
}

int eh_zero_const(No *n) { return n->op == N_NUM && n->valor == 0 && tp_inteiro(n->tipo); }

static void exige_lvalor(No *n, const char *para_que)
{
    if (!n->lvalor)
        erro(n->pos, "só dá para fazer isso com algo que tem lugar na memória: uma variável, v[i], *p ou s.campo",
             "%s precisa de uma variável, não de um valor", para_que);
}

No *valor_de(No *n)
{
    No *e;
    if (n->tipo->classe == TP_VETOR) {
        e = no_novo(N_END, tp_ponteiro(n->tipo->base), n->pos);
        e->a = n;
        return e;
    }
    if (n->tipo->classe == TP_FUNCAO) {
        e = no_novo(N_END, tp_ponteiro(n->tipo), n->pos);
        e->a = n;
        return e;
    }
    return n;
}

No *converte(No *n, Tipo *para)
{
    No *c;
    if (n->op == N_NUM && tp_escalar(para)) return no_num(n->valor, para, n->pos);
    c = no_novo(N_CONV, para, n->pos);
    c->a = n;
    return c;
}

static No *promove(No *n)
{
    n = valor_de(n);
    if (tp_inteiro(n->tipo) && n->tipo->classe < TP_INT) return converte(n, t_int);
    if (n->tipo->constante || n->tipo->eh_enum) {       /* o valor não leva o const junto */
        if (tp_inteiro(n->tipo) && n->op != N_NUM) return converte(n, n->tipo->puro->eh_enum ? t_int : n->tipo->puro);
    }
    return n;
}

static Tipo *comum(Tipo *a, Tipo *b)
{
    if (a->classe == TP_ULONGO || b->classe == TP_ULONGO) return t_ulongo;
    if (a->classe == TP_LONGO || b->classe == TP_LONGO) return t_longo;
    if (a->classe == TP_UINT || b->classe == TP_UINT) return t_uint;
    return t_int;
}

static const char *nome_op(int op)
{
    switch (op) {
    case N_SOMA: return "+"; case N_SUB: return "-"; case N_MUL: return "*"; case N_DIV: return "/";
    case N_RESTO: return "%"; case N_E: return "&"; case N_OU: return "|"; case N_XOU: return "^";
    case N_DESL_E: return "<<"; case N_DESL_D: return ">>"; case N_IGUAL: return "=="; case N_DIF: return "!=";
    case N_MENOR: return "<"; case N_MENOR_IG: return "<="; case N_E_LOG: return "&&"; case N_OU_LOG: return "||";
    }
    return "?";
}

static void exige_valor(No *n, const char *op)
{
    if (n->tipo->classe == TP_VOID)
        erro(n->pos, "uma função void não devolve nada; não dá para usar o resultado dela numa conta",
             "o operador '%s' recebeu uma expressão void (sem valor)", op);
    if (tp_agreg(n->tipo))
        erro(n->pos, "structs e unions não entram em contas nem comparações; use os campos (a.x == b.x) ou memcmp",
             "o operador '%s' não funciona com '%T'", op, n->tipo);
}

static void exige_inteiro(No *n, const char *op)
{
    exige_valor(n, op);
    if (!tp_inteiro(n->tipo))
        erro(n->pos, "este operador só faz contas com números inteiros", "o operador '%s' não funciona com '%T'", op, n->tipo);
}

/* A conta entre dois números já conhecidos. */
static i64 calcula(int op, Tipo *t, i64 a, i64 b, Pos p)
{
    int largo = t->classe == TP_LONGO || t->classe == TP_ULONGO, sem = tp_sem_sinal(t);
    u64 ua = (u64)a, ub = (u64)b;
    i64 r = 0, resto;
    u64 uresto;
    switch (op) {
    case N_SOMA: r = (i64)(ua + ub); break;
    case N_SUB: r = (i64)(ua - ub); break;
    case N_MUL: r = (i64)(ua * ub); break;
    case N_DIV: case N_RESTO:
        if (b == 0) erro(p, "confira o divisor; se ele vem de uma macro, veja quanto ela vale", "divisão por zero");
        if (sem) { r = (i64)div_u64(ua, ub, &uresto); if (op == N_RESTO) r = (i64)uresto; }
        else { r = div_s64(a, b, &resto); if (op == N_RESTO) r = resto; }
        break;
    case N_E: r = a & b; break;
    case N_OU: r = a | b; break;
    case N_XOU: r = a ^ b; break;
    case N_DESL_E: r = (i64)(ua << (b & (largo ? 63 : 31))); break;
    case N_DESL_D:
        if (sem) r = (i64)(ua >> (b & (largo ? 63 : 31)));
        else r = a >> (b & (largo ? 63 : 31));
        break;
    case N_IGUAL: return a == b;
    case N_DIF: return a != b;
    case N_MENOR: return sem && largo ? ua < ub : a < b;
    case N_MENOR_IG: return sem && largo ? ua <= ub : a <= b;
    }
    return normaliza(r, t);
}

static No *no_bin(int op, Tipo *t, No *a, No *b, Pos p)
{
    No *n;
    if (a->op == N_NUM && b->op == N_NUM && tp_inteiro(a->tipo) && tp_inteiro(b->tipo)) {
        int compara = op >= N_IGUAL && op <= N_MENOR_IG;
        return no_num(calcula(op, compara ? a->tipo : t, a->valor, b->valor, p), t, p);
    }
    if ((op == N_DIV || op == N_RESTO) && eh_zero_const(b))
        erro(p, "confira o divisor; se ele vem de uma macro, veja quanto ela vale", "divisão por zero");
    n = no_novo(op, t, p);
    n->a = a;
    n->b = b;
    return n;
}

static u32 tam_apontado(No *p, Pos onde)
{
    Tipo *b = p->tipo->base;
    if (b->classe == TP_VOID) return 1;     /* como no GCC: void * anda de byte em byte */
    if (b->classe == TP_FUNCAO)
        erro(onde, NULL, "não dá para fazer contas com ponteiro para função");
    if (!tp_completo(b))
        erro(onde, "o compilador precisa saber o tamanho do que o ponteiro aponta; defina o struct antes de usar",
             "conta com ponteiro para '%T', que ainda não foi definido por completo", b);
    return tp_tam(b);
}

static int ponteiros_combinam(Tipo *a, Tipo *b)
{
    Tipo *x = a->base, *y = b->base;
    if (x->classe == TP_VOID || y->classe == TP_VOID) return 1;
    if (tp_igual(x, y)) return 1;
    /* char * e unsigned char * (ou int * e unsigned *) apontam para a mesma coisa na memória */
    if (tp_inteiro(x) && tp_inteiro(y) && x->tam == y->tam) return 1;
    return 0;
}

static No *binaria(int op, No *a, No *b, Pos p)
{
    Tipo *t;
    int pa, pb;
    const char *s = nome_op(op);
    a = promove(a);
    b = promove(b);
    exige_valor(a, s);
    exige_valor(b, s);
    pa = a->tipo->classe == TP_PONTEIRO;
    pb = b->tipo->classe == TP_PONTEIRO;

    switch (op) {
    case N_SOMA:
        if (pa && pb) erro(p, "somar dois endereços não tem sentido; talvez a ideia fosse ponteiro + número", "soma de dois ponteiros");
        if (pb) { No *x = a; a = b; b = x; pa = 1; pb = 0; }
        if (pa) {
            b = no_bin(N_MUL, t_int, converte(b, t_int), no_num(tam_apontado(a, p), t_int, p), p);
            return no_bin(N_SOMA, a->tipo, a, b, p);
        }
        break;
    case N_SUB:
        if (pa && pb) {
            u32 tam = tam_apontado(a, p);
            if (!ponteiros_combinam(a->tipo, b->tipo))
                erro(p, "só se subtraem ponteiros para o mesmo tipo de coisa (dois elementos do mesmo vetor)",
                     "subtração entre '%T' e '%T'", a->tipo, b->tipo);
            return no_bin(N_DIV, t_int, no_bin(N_SUB, t_int, converte(a, t_int), converte(b, t_int), p), no_num(tam, t_int, p), p);
        }
        if (pb) erro(p, "dá para fazer ponteiro - número, mas não número - ponteiro", "subtração de um ponteiro de um número");
        if (pa) {
            b = no_bin(N_MUL, t_int, converte(b, t_int), no_num(tam_apontado(a, p), t_int, p), p);
            return no_bin(N_SUB, a->tipo, a, b, p);
        }
        break;
    case N_IGUAL: case N_DIF: case N_MENOR: case N_MENOR_IG:
        if (pa || pb) {
            if (pa && eh_zero_const(b)) b = converte(b, a->tipo);
            else if (pb && eh_zero_const(a)) a = converte(a, b->tipo);
            else if (!pa || !pb)
                erro(p, "compare ponteiro com ponteiro (ou com NULL); para comparar o que ele aponta use *p",
                     "comparação entre '%T' e '%T'", a->tipo, b->tipo);
            return no_bin(op, t_int, a, b, p);
        }
        t = comum(a->tipo, b->tipo);
        return no_bin(op, t_int, converte(a, t), converte(b, t), p);
    case N_E_LOG: case N_OU_LOG:
        if (a->op == N_NUM && b->op == N_NUM)
            return no_num(op == N_E_LOG ? (a->valor && b->valor) : (a->valor || b->valor), t_int, p);
        {
            No *n = no_novo(op, t_int, p);
            n->a = a;
            n->b = b;
            return n;
        }
    case N_DESL_E: case N_DESL_D:
        exige_inteiro(a, s);
        exige_inteiro(b, s);
        return no_bin(op, a->tipo, a, converte(b, t_int), p);
    }
    exige_inteiro(a, s);
    exige_inteiro(b, s);
    t = comum(a->tipo, b->tipo);
    return no_bin(op, t, converte(a, t), converte(b, t), p);
}

/* Confere se "n" pode ir para um lugar do tipo "para" (atribuição, argumento,
 * return, inicialização) e devolve já convertido. */
No *para_atribuir(No *n, Tipo *para, const char *onde)
{
    Tipo *de;
    n = valor_de(n);
    de = n->tipo;
    if (de->classe == TP_VOID)
        erro(n->pos, "uma função void não devolve nada; não dá para guardar o resultado dela", "expressão void (sem valor) usada em %s", onde);
    if (tp_agreg(para) || tp_agreg(de)) {
        if (!tp_igual(para, de))
            erro(n->pos, "os dois lados precisam ser do mesmo struct; para converter, copie campo por campo",
                 "em %s: esperava '%T', mas veio '%T'", onde, para, de);
        return n;
    }
    if (para->classe == TP_PONTEIRO) {
        if (de->classe == TP_PONTEIRO) {
            if (!ponteiros_combinam(para, de))
                erro(n->pos, "se a conversão é mesmo o que você quer, escreva-a às claras: (tipo *)valor",
                     "em %s: esperava '%T', mas veio '%T'", onde, para, de);
            return converte(n, para);
        }
        if (eh_zero_const(n)) return converte(n, para);
        erro(n->pos, "para o endereço de uma variável use &variavel; para um endereço fixo escreva (tipo *)numero; 'nenhum lugar' é NULL",
             "em %s: esperava o ponteiro '%T', mas veio o inteiro '%T'", onde, para, de);
    }
    if (de->classe == TP_PONTEIRO) {
        if (para->classe == TP_BOOL) return converte(n, para);
        erro(n->pos, "para pegar o valor apontado use *ponteiro; para o número escrito num texto use texto_para_int(texto); se quer mesmo o endereço como número, escreva (unsigned)ponteiro",
             "em %s: esperava o inteiro '%T', mas veio o ponteiro '%T'", onde, para, de);
    }
    if (para->classe == TP_VOID || para->classe == TP_VETOR || para->classe == TP_FUNCAO)
        erro(n->pos, NULL, "em %s: não dá para guardar um valor em '%T'", onde, para);
    return converte(n, para);
}

/* ------------------------------------------------------------ dicas */

static u32 distancia(const char *a, u32 na, const char *b, u32 nb)
{
    static u32 linha[2][48];
    u32 i, j;
    if (na > 40 || nb > 40) return 99;
    for (j = 0; j <= nb; j++) linha[0][j] = j;
    for (i = 1; i <= na; i++) {
        u32 *ant = linha[(i - 1) & 1], *at = linha[i & 1];
        at[0] = i;
        for (j = 1; j <= nb; j++) {
            u32 v = ant[j - 1] + (a[i - 1] != b[j - 1]);
            if (ant[j] + 1 < v) v = ant[j] + 1;
            if (at[j - 1] + 1 < v) v = at[j - 1] + 1;
            at[j] = v;
        }
    }
    return linha[na & 1][nb];
}

/* Uma dica para um nome que não existe: o nome do C tradicional que tem
 * equivalente na libsagui, ou o nome declarado mais parecido. */
static const char *dica_nome(Nome *n, int funcao)
{
    static const char *TROCAS[][2] = {
        { "printf", "imprimir" }, { "puts", "imprimir" }, { "putchar", "imprimir(\"%c\", c)" }, { "fprintf", "imprimir_em" },
        { "sprintf", "formatar_texto" }, { "snprintf", "formatar_texto" }, { "malloc", "aloca" }, { "calloc", "aloca_zerado" },
        { "realloc", "realoca" }, { "free", "libera" }, { "strlen", "tam_texto" }, { "strcmp", "compara_texto" },
        { "strncmp", "compara_texto_n" }, { "strcpy", "copia_texto(destino, origem, tamanho_do_destino)" },
        { "strncpy", "copia_texto" }, { "strcat", "junta_texto(destino, origem, tamanho_do_destino)" },
        { "strchr", "acha_char" }, { "strrchr", "acha_ultimo_char" }, { "atoi", "texto_para_int" }, { "exit", "sair" },
        { "open", "abrir" }, { "fopen", "abrir" }, { "read", "ler" }, { "fread", "ler" }, { "write", "escrever" },
        { "fwrite", "escrever" }, { "close", "fechar" }, { "fclose", "fechar" }, { "sleep", "dormir (em milissegundos)" },
        { "usleep", "dormir (em milissegundos)" }, { "isspace", "eh_espaco" }, { "gets", "ler_linha" }, { "fgets", "ler_linha" },
        { "scanf", "ler_linha e texto_para_int" }, { "getchar", "ler(ENTRADA, &c, 1)" }, { "rand", "bytes_aleatorios" },
        { "time", "tempo_ms ou ler_data" }, { "main", "principal" }, { "true", "true (inclua \"sagui.h\")" },
        { "false", "false (inclua \"sagui.h\")" }, { "NULL", "NULL (inclua \"sagui.h\")" }, { NULL, NULL }
    };
    static char dica[200];
    Nome *melhor = NULL, *x;
    u32 d_melhor = 3, g;
    int i;
    for (i = 0; TROCAS[i][0]; i++)
        if (txt_igual(n->texto, TROCAS[i][0])) {
            formatar(dica, sizeof dica, "o SaguiOS não tem a biblioteca padrão do C; no lugar de '%s' use %s (com #include \"sagui.h\")",
                     TROCAS[i][0], TROCAS[i][1]);
            return dica;
        }
    for (g = 0; g < 1024; g++)
        for (x = G.gavetas[g]; x; x = x->prox) {
            u32 d;
            if (!x->sim || x == n) continue;
            d = distancia(n->texto, n->tam, x->texto, x->tam);
            if (d < d_melhor && d * 2 <= n->tam) { d_melhor = d; melhor = x; }
        }
    if (melhor) {
        formatar(dica, sizeof dica, "existe '%N', com nome parecido; era isso? Se não, declare '%N' antes de usar", melhor, n);
        return dica;
    }
    if (funcao) return "declare a função antes de usar (o protótipo ou a função inteira mais acima) ou inclua o cabeçalho dela, como \"sagui.h\"";
    return "declare a variável antes de usar, por exemplo: int nome = 0;";
}

/* ------------------------------------------------------------ primárias */

static No *chamada(No *f, Pos p)
{
    No *alvo = valor_de(f), *n;
    Tipo *tf;
    No *args[40];
    int qtd = 0, i;
    if (alvo->tipo->classe != TP_PONTEIRO || alvo->tipo->base->classe != TP_FUNCAO)
        erro(f->pos, "só se chama com ( ) uma função ou um ponteiro para função", "isto é '%T', não uma função: não dá para chamar", f->tipo);
    tf = alvo->tipo->base;
    if (!eh_pont(')')) {
        for (;;) {
            if (qtd == 40) erro(tk()->pos, NULL, "argumentos demais numa chamada (o máximo é 40)");
            args[qtd++] = expr_atribuicao();
            if (!aceita(',')) break;
        }
    }
    exige(')', "feche a lista de argumentos da chamada com )");
    if (!tf->sem_lista && (qtd < tf->qtd_params || (qtd > tf->qtd_params && !tf->variadica))) {
        const char *nome = f->op == N_VAR ? f->sim->nome->texto : "(ponteiro)";
        erro(p, "confira a declaração da função e passe exatamente os argumentos que ela pede",
             "a função '%s' recebe %d argumento(s), mas a chamada passa %d", nome, tf->qtd_params, qtd);
    }
    for (i = 0; i < qtd; i++) {
        if (i < tf->qtd_params && !tf->sem_lista) {
            char onde[80];
            formatar(onde, sizeof onde, "argumento %d de '%s'", i + 1, f->op == N_VAR ? f->sim->nome->texto : "(ponteiro)");
            args[i] = para_atribuir(args[i], tf->params[i].tipo->puro, onde);
        } else {
            args[i] = promove(args[i]);
            if (args[i]->tipo->classe == TP_VOID)
                erro(args[i]->pos, NULL, "argumento void (sem valor) numa chamada");
        }
    }
    if (tp_agreg(tf->base) && !tp_completo(tf->base))
        erro(p, NULL, "a função devolve '%T', que ainda não foi definido por completo", tf->base);
    n = no_novo(N_CHAMA, tf->base, p);
    n->a = alvo;
    n->args = novo(sizeof(No *) * (u32)(qtd + 1));
    copia_bytes(n->args, args, sizeof(No *) * (u32)qtd);
    n->qtd_args = qtd;
    return n;
}

static No *membro(No *a, Pos p)
{
    Token *t = tk();
    Campo *c;
    u32 d;
    No *n;
    if (t->tipo != T_NOME) erro(t->pos, "depois de . ou -> vem o nome de um campo", "falta o nome do campo");
    if (!tp_agreg(a->tipo))
        erro(p, a->tipo->classe == TP_PONTEIRO && tp_agreg(a->tipo->base)
                 ? "isto é um ponteiro para struct: use -> no lugar do ponto (p->campo)"
                 : "só structs e unions têm campos",
             "'%T' não tem campos: não dá para usar '.%N'", a->tipo, t->nome);
    if (!tp_completo(a->tipo))
        erro(p, "o struct foi só anunciado (struct nome;); os campos precisam ser definidos antes de usar",
             "'%T' ainda não foi definido por completo", a->tipo);
    c = tp_campo(a->tipo, t->nome, &d);
    if (!c) {
        static char dica[200];
        Campo *x;
        u32 k = (u32)formatar(dica, sizeof dica, "os campos de '%T' são:", a->tipo);
        for (x = a->tipo->puro->campos; x && k + 40 < sizeof dica; x = x->prox)
            if (x->nome) k += (u32)formatar(dica + k, sizeof dica - k, " %N", x->nome);
        erro(t->pos, dica, "'%T' não tem um campo chamado '%N'", a->tipo, t->nome);
    }
    avanca();
    n = no_novo(N_MEMBRO, a->tipo->constante ? tp_const(c->tipo) : c->tipo, p);
    n->a = a;
    n->valor = d;
    n->lvalor = a->lvalor;
    return n;
}

static No *deref(No *a, Pos p)
{
    No *n;
    a = valor_de(a);
    if (a->tipo->classe != TP_PONTEIRO)
        erro(p, "só um ponteiro (ou vetor) pode ser seguido com *, [ ] ou ->", "'%T' não é um ponteiro: não dá para pegar o que ele aponta", a->tipo);
    if (a->tipo->base->classe == TP_VOID)
        erro(p, "converta antes para um ponteiro de tipo conhecido: *(char *)p", "não dá para ler através de um 'void *': ele não diz o tipo do que aponta");
    n = no_novo(N_DEREF, a->tipo->base, p);
    n->a = a;
    n->lvalor = 1;
    return n;
}

static No *incremento(No *a, int op, int pos_fixo, Pos p)
{
    No *n;
    const char *s = op == N_SOMA ? "++" : "--";
    exige_lvalor(a, s);
    if (a->tipo->constante) erro(p, "tire o const da declaração se a variável precisa mudar", "não dá para mudar uma variável const com %s", s);
    if (!tp_escalar(a->tipo)) erro(p, NULL, "o operador %s não funciona com '%T'", s, a->tipo);
    n = no_novo(N_ATR_OP, a->tipo->puro, p);
    n->a = a;
    n->sub = (u8)op;
    n->pos_fixo = (u8)pos_fixo;
    if (a->tipo->classe == TP_PONTEIRO) {
        n->b = no_num(tam_apontado(a, p), t_int, p);
        n->tipo_conta = a->tipo;
    } else {
        n->tipo_conta = a->tipo->classe >= TP_LONGO ? a->tipo->puro : a->tipo->classe == TP_UINT ? t_uint : t_int;
        n->b = no_num(1, n->tipo_conta, p);
    }
    return n;
}

static No *numero(Token *t)
{
    u64 v = (u64)t->valor;
    Tipo *tp;
    int sem = t->marcas & TF_SEM_SINAL, longo = t->marcas & TF_LONGO, hexa = t->marcas & TF_HEXA;
    if (t->marcas & TF_CARACTERE) tp = t_int;
    else if (longo) tp = (sem || (v >> 63)) ? t_ulongo : t_longo;
    else if (sem) tp = (v >> 32) ? t_ulongo : t_uint;
    else if (v <= 0x7FFFFFFFu) tp = t_int;
    else if (hexa && v <= 0xFFFFFFFFu) tp = t_uint;
    else tp = (v >> 63) ? t_ulongo : t_longo;
    return no_num((i64)v, tp, t->pos);
}

static No *primaria(void)
{
    Token *t = tk();
    Pos p = t->pos;
    No *n;
    if (t->tipo == T_NUM) { avanca(); return numero(t); }
    if (t->tipo == T_REAL)
        erro(p, "o C do Sagui não tem ponto flutuante (float, double); use inteiros: guarde 1,5 como 15 décimos ou 1500 milésimos",
             "número com vírgula (ponto flutuante) não é aceito");
    if (t->tipo == T_TEXTO) {
        /* "abc" "def" é um texto só */
        const char *txt = t->texto;
        u32 tam = t->tam;
        avanca();
        while (tk()->tipo == T_TEXTO) {
            char *j = novo(tam + tk()->tam + 1);
            copia_bytes(j, txt, tam);
            copia_bytes(j + tam, tk()->texto, tk()->tam);
            tam += tk()->tam;
            txt = j;
            avanca();
        }
        n = no_novo(N_TEXTO, tp_vetor(t_char, tam + 1, 1), p);
        n->n = texto_global(txt, tam + 1);
        n->lvalor = 1;
        return n;
    }
    if (t->tipo == T_PONT && t->pont == '(') {
        avanca();
        n = expressao();
        exige(')', "cada ( precisa do seu )");
        return n;
    }
    if (t->tipo == T_NOME) {
        Simbolo *s = t->nome->sim;
        if (t->nome->chave == CH_VA_START || t->nome->chave == CH_VA_END) {
            int inicio = t->nome->chave == CH_VA_START;
            avanca();
            exige('(', NULL);
            n = no_novo(N_VA_INICIO, t_void, p);
            n->a = expr_atribuicao();
            exige_lvalor(n->a, "va_start");
            if (inicio) {
                exige(',', "o formato é va_start(lista, ultimo_parametro)");
                n->b = expr_atribuicao();
                if (n->b->op != N_VAR || n->b->sim->global)
                    erro(n->b->pos, "o segundo argumento de va_start é o último parâmetro com nome da função", "va_start: isto não é um parâmetro da função");
            } else {
                n = converte(no_num(0, t_int, p), t_void);     /* va_end não precisa fazer nada */
            }
            exige(')', NULL);
            return n;
        }
        if (t->nome->chave == CH_VA_ARG) {
            avanca();
            exige('(', NULL);
            n = no_novo(N_VA_ARG, NULL, p);
            n->a = expr_atribuicao();
            exige_lvalor(n->a, "va_arg");
            exige(',', "o formato é va_arg(lista, tipo)");
            n->tipo = nome_de_tipo();
            if (!tp_escalar(n->tipo) && !tp_agreg(n->tipo)) erro(p, NULL, "va_arg não aceita o tipo '%T'", n->tipo);
            if (tp_inteiro(n->tipo) && n->tipo->classe < TP_INT)
                erro(p, "os argumentos extras de uma função chegam como int; peça va_arg(lista, int) e converta depois",
                     "va_arg com '%T': tipos menores que int viajam como int", n->tipo);
            exige(')', NULL);
            return n;
        }
        if (t->nome->chave)
            erro(p, "aqui devia haver um valor: um número, uma variável, uma chamada de função...",
                 "a palavra '%N' não pode aparecer no meio de uma expressão", t->nome);
        if (!s && tk_adiante(1)->tipo == T_NOME && !tk_adiante(1)->nome->chave)
            erro(p, "os tipos uint8_t, uint32_t, bool e size_t vêm de #include \"sagui.h\"; um struct é citado com a palavra struct (struct ponto p;) ou por um typedef",
                 "'%N' não é um tipo conhecido", t->nome);
        if (!s) {
            int funcao = tk_adiante(1)->tipo == T_PONT && tk_adiante(1)->pont == '(';
            erro(p, dica_nome(t->nome, funcao), funcao ? "a função '%N' não foi declarada" : "o nome '%N' não foi declarado", t->nome);
        }
        if (s->classe == S_TYPEDEF)
            erro(p, "um nome de tipo serve para declarar variáveis; aqui devia haver um valor", "'%N' é um tipo, não um valor", t->nome);
        avanca();
        if (s->classe == S_CONST_ENUM) return no_num(s->valor, t_int, p);
        n = no_novo(N_VAR, s->tipo, p);
        n->sim = s;
        n->lvalor = s->classe == S_VAR;
        return n;
    }
    if (t->tipo == T_FIM) erro(p, "o arquivo acabou no meio de um comando; confira as chaves { } e os ;", "faltou o resto da expressão");
    erro(p, "aqui devia haver um valor: um número, uma variável, uma chamada de função...",
         "não esperava '%s' aqui", t->tipo == T_PONT ? texto_pont(t->pont) : "isto");
}

static No *expr_posfixa(No *n)
{
    for (;;) {
        Token *t = tk();
        Pos p = t->pos;
        if (t->tipo != T_PONT) break;
        if (t->pont == '[') {
            No *i;
            avanca();
            i = expressao();
            exige(']', "feche o índice com ]");
            if (valor_de(n)->tipo->classe != TP_PONTEIRO && valor_de(i)->tipo->classe != TP_PONTEIRO)
                erro(p, "só vetores e ponteiros aceitam [ ]", "'%T' não é vetor nem ponteiro: não dá para usar [ ]", n->tipo);
            if (!tp_inteiro(valor_de(i)->tipo) && !tp_inteiro(valor_de(n)->tipo))
                erro(i->pos, "entre [ ] vai a posição do elemento: um int. Se o índice está num texto, converta com texto_para_int",
                     "o índice de um vetor precisa ser um número inteiro, não '%T'", i->tipo);
            n = deref(binaria(N_SOMA, n, i, p), p);
        }
        else if (t->pont == '(') { avanca(); n = chamada(n, n->pos); }
        else if (t->pont == '.') { avanca(); n = membro(n, p); }
        else if (t->pont == P_SETA) {
            avanca();
            if (valor_de(n)->tipo->classe != TP_PONTEIRO)
                erro(p, tp_agreg(n->tipo) ? "isto é o próprio struct, não um ponteiro: use ponto (s.campo)" : "-> só serve para ponteiro para struct",
                     "'%T' não é um ponteiro: não dá para usar ->", n->tipo);
            n = membro(deref(n, p), p);
        }
        else if (t->pont == P_INC) { avanca(); n = incremento(n, N_SOMA, 1, p); }
        else if (t->pont == P_DEC) { avanca(); n = incremento(n, N_SUB, 1, p); }
        else break;
    }
    return n;
}

static No *conversao(Tipo *para, No *n, Pos p)
{
    n = valor_de(n);
    if (para->classe == TP_VOID) return converte(n, t_void);
    if (n->tipo->classe == TP_VOID) erro(p, NULL, "não dá para converter uma expressão void (sem valor) para '%T'", para);
    if (tp_agreg(para) || para->classe == TP_VETOR || para->classe == TP_FUNCAO || tp_agreg(n->tipo)) {
        if (tp_agreg(para) && tp_igual(para, n->tipo)) return n;
        erro(p, "só números e ponteiros podem ser convertidos com (tipo); para montar um struct use (tipo){ campos }",
             "não dá para converter '%T' em '%T'", n->tipo, para);
    }
    if (para->classe == TP_PONTEIRO && (n->tipo->classe == TP_LONGO || n->tipo->classe == TP_ULONGO)) n = converte(n, t_uint);
    if (n->tipo->classe == TP_PONTEIRO && (para->classe == TP_LONGO || para->classe == TP_ULONGO)) n = converte(n, t_uint);
    return converte(n, para->puro);
}

static No *expr_unaria(void)
{
    Token *t = tk();
    Pos p = t->pos;
    No *a, *n;
    if (++G.fundura > 200)
        erro(p, "quebre a conta em partes, guardando pedaços em variáveis", "expressão aninhada demais");
    if (t->tipo == T_PONT) {
        switch (t->pont) {
        case '(':
            if (comeca_tipo(tk_adiante(1))) {
                Tipo *tp;
                avanca();
                tp = nome_de_tipo();
                exige(')', "feche a conversão de tipo com )");
                if (eh_pont('{')) {
                    /* (tipo){ ... }: um objeto sem nome, montado na hora */
                    if (!G.f) erro(p, "fora de função, declare uma variável com nome e inicialize-a", "(tipo){...} só é aceito dentro de função");
                    n = no_novo(N_COMPOSTO, NULL, p);
                    n->itens = ler_inicializador(&tp);
                    n->tipo = tp;
                    n->n = novo_local(tp);
                    n->lvalor = 1;
                    n = expr_posfixa(n);
                } else {
                    n = conversao(tp, expr_unaria(), p);
                }
                G.fundura--;
                return n;
            }
            break;
        case '-': case '+': case '~':
            avanca();
            a = promove(expr_unaria());
            exige_inteiro(a, texto_pont(t->pont));
            G.fundura--;
            if (t->pont == '+') return a->op == N_NUM ? a : converte(a, a->tipo);
            if (a->op == N_NUM) return no_num(t->pont == '-' ? (i64)(0 - (u64)a->valor) : ~a->valor, a->tipo, p);
            n = no_novo(t->pont == '-' ? N_NEG : N_INVERTE, a->tipo, p);
            n->a = a;
            return n;
        case '!':
            avanca();
            a = promove(expr_unaria());
            exige_valor(a, "!");
            G.fundura--;
            if (a->op == N_NUM) return no_num(!a->valor, t_int, p);
            n = no_novo(N_NAO, t_int, p);
            n->a = a;
            return n;
        case '*':
            avanca();
            a = expr_unaria();
            G.fundura--;
            return deref(a, p);
        case '&':
            avanca();
            a = expr_unaria();
            G.fundura--;
            if (a->tipo->classe != TP_FUNCAO) exige_lvalor(a, "o operador &");
            n = no_novo(N_END, tp_ponteiro(a->tipo), p);
            n->a = a;
            return n;
        case P_INC: case P_DEC:
            avanca();
            a = expr_unaria();
            G.fundura--;
            return incremento(a, t->pont == P_INC ? N_SOMA : N_SUB, 0, p);
        }
    }
    if (t->tipo == T_NOME && (t->nome->chave == CH_SIZEOF || t->nome->chave == CH_ALIGNOF)) {
        Tipo *tp;
        int alinha = t->nome->chave == CH_ALIGNOF;
        avanca();
        if (eh_pont('(') && comeca_tipo(tk_adiante(1))) {
            avanca();
            tp = nome_de_tipo();
            exige(')', "feche o sizeof com )");
        } else {
            tp = expr_unaria()->tipo;
        }
        G.fundura--;
        if (tp->classe == TP_FUNCAO || tp->classe == TP_VOID || !tp_completo(tp))
            erro(p, "sizeof precisa de um tipo com tamanho conhecido (um struct já definido, um vetor com tamanho)",
                 "não dá para medir o tamanho de '%T'", tp);
        return no_num(alinha ? tp_alinha(tp) : tp_tam(tp), t_uint, p);
    }
    n = expr_posfixa(primaria());
    G.fundura--;
    return n;
}

static int op_binario(Token *t, int *prec)
{
    if (t->tipo != T_PONT) return -1;
    switch (t->pont) {
    case '*': *prec = 10; return N_MUL;
    case '/': *prec = 10; return N_DIV;
    case '%': *prec = 10; return N_RESTO;
    case '+': *prec = 9; return N_SOMA;
    case '-': *prec = 9; return N_SUB;
    case P_DESL_E: *prec = 8; return N_DESL_E;
    case P_DESL_D: *prec = 8; return N_DESL_D;
    case '<': *prec = 7; return N_MENOR;
    case P_MENOR_IG: *prec = 7; return N_MENOR_IG;
    case '>': *prec = 7; return -2;             /* a > b é b < a */
    case P_MAIOR_IG: *prec = 7; return -3;
    case P_IGUAL: *prec = 6; return N_IGUAL;
    case P_DIF: *prec = 6; return N_DIF;
    case '&': *prec = 5; return N_E;
    case '^': *prec = 4; return N_XOU;
    case '|': *prec = 3; return N_OU;
    case P_E_LOG: *prec = 2; return N_E_LOG;
    case P_OU_LOG: *prec = 1; return N_OU_LOG;
    }
    return -1;
}

static No *expr_binaria(int minimo)
{
    No *a = expr_unaria();
    for (;;) {
        int prec = 0, op = op_binario(tk(), &prec);
        Pos p = tk()->pos;
        No *b;
        if (op == -1 || prec < minimo) return a;
        avanca();
        b = expr_binaria(prec + 1);
        if (op == -2) a = binaria(N_MENOR, b, a, p);
        else if (op == -3) a = binaria(N_MENOR_IG, b, a, p);
        else a = binaria(op, a, b, p);
    }
}

No *expr_condicional(void)
{
    No *c = expr_binaria(1), *a, *b, *n;
    Tipo *t;
    Pos p = tk()->pos;
    if (!eh_pont('?')) return c;
    avanca();
    c = promove(c);
    exige_valor(c, "?");
    a = valor_de(expressao());
    exige(':', "o formato é: condicao ? valor_se_sim : valor_se_nao");
    b = valor_de(expr_condicional());
    if (tp_inteiro(a->tipo) && tp_inteiro(b->tipo)) {
        a = promove(a);
        b = promove(b);
        t = comum(a->tipo, b->tipo);
        a = converte(a, t);
        b = converte(b, t);
    }
    else if (a->tipo->classe == TP_PONTEIRO && eh_zero_const(b)) { t = a->tipo; b = converte(b, t); }
    else if (b->tipo->classe == TP_PONTEIRO && eh_zero_const(a)) { t = b->tipo; a = converte(a, t); }
    else if (a->tipo->classe == TP_PONTEIRO && b->tipo->classe == TP_PONTEIRO && ponteiros_combinam(a->tipo, b->tipo))
        t = a->tipo->base->classe == TP_VOID ? b->tipo : a->tipo;
    else if (a->tipo->classe == TP_VOID && b->tipo->classe == TP_VOID) t = t_void;
    else if (tp_agreg(a->tipo) && tp_igual(a->tipo, b->tipo)) t = a->tipo;
    else
        erro(p, "os dois valores de um ? : precisam ser do mesmo tipo (dois números, dois ponteiros iguais...)",
             "os dois lados do ? : têm tipos que não combinam: '%T' e '%T'", a->tipo, b->tipo);
    if (c->op == N_NUM) return c->valor ? a : b;
    n = no_novo(N_COND, t, p);
    n->a = c;
    n->b = a;
    n->c = b;
    return n;
}

No *expr_atribuicao(void)
{
    No *a = expr_condicional(), *b, *n;
    Token *t = tk();
    Pos p = t->pos;
    int op = -1;
    if (t->tipo != T_PONT) return a;
    switch (t->pont) {
    case '=': op = N_ATR; break;
    case P_ATR_SOMA: op = N_SOMA; break;
    case P_ATR_SUB: op = N_SUB; break;
    case P_ATR_MUL: op = N_MUL; break;
    case P_ATR_DIV: op = N_DIV; break;
    case P_ATR_RESTO: op = N_RESTO; break;
    case P_ATR_E: op = N_E; break;
    case P_ATR_OU: op = N_OU; break;
    case P_ATR_XOU: op = N_XOU; break;
    case P_ATR_DESL_E: op = N_DESL_E; break;
    case P_ATR_DESL_D: op = N_DESL_D; break;
    default: return a;
    }
    avanca();
    if (!a->lvalor)
        erro(p, a->op == N_IGUAL || a->op == N_DIF ? "para comparar use ==; um = sozinho guarda um valor numa variável"
                                                   : "à esquerda do = tem de haver algo que guarda valor: uma variável, v[i], *p ou s.campo",
             "o lado esquerdo do '%s' não é uma variável", texto_pont(t->pont));
    if (a->tipo->classe == TP_VETOR)
        erro(p, "copie os elementos um a um (ou com memcpy); para texto use copia_texto(destino, origem, tamanho)",
             "não dá para atribuir a um vetor inteiro");
    if (a->tipo->constante)
        erro(p, "tire o const da declaração se o valor precisa mudar", "não dá para mudar o que foi declarado const");
    if (tp_agreg(a->tipo) && !tp_completo(a->tipo)) erro(p, NULL, "'%T' ainda não foi definido por completo", a->tipo);
    b = expr_atribuicao();
    if (op == N_ATR) {
        n = no_novo(N_ATR, a->tipo->puro, p);
        n->a = a;
        n->b = para_atribuir(b, a->tipo->puro, "atribuição");
        return n;
    }
    n = no_novo(N_ATR_OP, a->tipo->puro, p);
    n->a = a;
    n->sub = (u8)op;
    b = promove(b);
    exige_valor(b, texto_pont(t->pont));
    if (a->tipo->classe == TP_PONTEIRO) {
        if ((op != N_SOMA && op != N_SUB) || !tp_inteiro(b->tipo))
            erro(p, "com ponteiro só valem += e -= com um número inteiro", "o operador '%s' não funciona com '%T' e '%T'", texto_pont(t->pont), a->tipo, b->tipo);
        n->b = no_bin(N_MUL, t_int, converte(b, t_int), no_num(tam_apontado(a, p), t_int, p), p);
        n->tipo_conta = a->tipo->puro;
        return n;
    }
    if (!tp_inteiro(a->tipo) || !tp_inteiro(b->tipo))
        erro(p, NULL, "o operador '%s' não funciona com '%T' e '%T'", texto_pont(t->pont), a->tipo, b->tipo);
    {
        Tipo *pa = a->tipo->classe < TP_INT ? t_int : a->tipo->puro;
        if (pa->eh_enum) pa = t_int;
        if (op == N_DESL_E || op == N_DESL_D) { n->tipo_conta = pa; n->b = converte(b, t_int); }
        else { n->tipo_conta = comum(pa, b->tipo); n->b = converte(b, n->tipo_conta); }
    }
    if ((op == N_DIV || op == N_RESTO) && eh_zero_const(n->b))
        erro(p, "confira o divisor; se ele vem de uma macro, veja quanto ela vale", "divisão por zero");
    return n;
}

No *expressao(void)
{
    No *a = expr_atribuicao();
    while (eh_pont(',')) {
        No *n = no_novo(N_VIRGULA, NULL, tk()->pos);
        avanca();
        n->a = a;
        n->b = expr_atribuicao();
        n->tipo = n->b->tipo;
        n->lvalor = 0;
        a = n;
    }
    return a;
}

i64 expr_constante(const char *para_que, const char *dica)
{
    No *n = expr_condicional();
    if (n->op != N_NUM || !tp_inteiro(n->tipo))
        erro(n->pos, dica ? dica : "aqui o compilador precisa de um número que ele mesmo consiga calcular: números, constantes de enum, #define e contas entre eles (não variáveis)",
             "%s precisa ser uma constante inteira", para_que);
    return n->valor;
}
