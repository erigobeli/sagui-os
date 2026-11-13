/* preproc.c - o pré-processador: #include, #define, #if e companhia.
 *
 * Ele fica entre o leitor de tokens e o analisador. Trabalha com tokens, não
 * com texto: uma macro é uma lista de tokens guardada, e "expandir" é pôr
 * essa lista na frente da fila de entrada. A fila é uma pilha de "fontes":
 * em cima ficam as listas de macros sendo expandidas; quando acabam, a
 * leitura volta para o arquivo.
 *
 * Para uma macro não se expandir para sempre (#define x x), enquanto a
 * lista dela está na pilha o nome dela não é trocado de novo, e o token que
 * tentou fica marcado para nunca mais ser (é a regra do padrão C, 6.10.3.4).
 *
 * No fim, o programa inteiro vira uma lista só (G.toks), que o analisador
 * percorre podendo olhar adiante e voltar. */
#include "interno.h"

struct Macro {
    Nome  *nome;
    u8     funcao;          /* tem parênteses: M(a, b) */
    u8     variadica;       /* o último parâmetro é ... (__VA_ARGS__) */
    u8     expandindo;
    u8     especial;        /* 1 = __LINE__, 2 = __FILE__ */
    int    qtd_params;
    Nome **params;
    Token *corpo; int qtd_corpo;
};

typedef struct { Token *t; int n, i; Macro *m; } Fonte;
typedef struct { u8 ja_pegou, tem_else; Pos pos; } Cond;
typedef struct { Token *t; int n, cap; } Lista;

#define MAX_FONTES   200
#define MAX_ARQUIVOS 24
#define MAX_COND     64

static Fonte  *fontes;  static int nf;
static Leitor *leit;    static int nl;
static int    *base_cond;           /* quantos #if estavam abertos quando cada arquivo começou */
static Cond   *cond;    static int nc;
static Nome   *n_defined, *n_va_args;
static int     arq_cola = -1;

static void expandido(Token *t);

static void junta(Lista *l, Token *t)
{
    l->t = crescer(l->t, &l->cap, l->n, sizeof(Token));
    l->t[l->n++] = *t;
}

static void empilha(Token *t, int n, Macro *m)
{
    if (nf == MAX_FONTES)
        erro(t[0].pos, "alguma macro usa outra que usa outra... sem parar; simplifique os #define", "macros aninhadas demais");
    fontes[nf].t = t;
    fontes[nf].n = n;
    fontes[nf].i = 0;
    fontes[nf].m = m;
    if (m) m->expandindo = 1;
    nf++;
}

static void devolve(Token *t)
{
    Token *c = novo(sizeof(Token));
    *c = *t;
    empilha(c, 1, NULL);
}

/* ------------------------------------------------------------ diretivas */

/* O próximo token da linha da diretiva; 0 quando a linha acabou (o token da
 * linha seguinte fica onde estava). */
static int da_linha(Token *t)
{
    Leitor salvo = leit[nl - 1];
    lex_proximo(&leit[nl - 1], t);
    if (t->tipo == T_FIM || (t->marcas & TF_INICIO_LINHA)) {
        leit[nl - 1] = salvo;
        return 0;
    }
    return 1;
}

static void resto_da_linha(void)
{
    Token t;
    int antes = G.pulando;
    G.pulando = 1;
    while (da_linha(&t)) ;
    G.pulando = antes;
}

static void sobra_na_linha(const char *diretiva)
{
    Token t;
    if (da_linha(&t))
        erro(t.pos, "cada diretiva ocupa uma linha inteira; apague o que sobrou ou passe para a linha de baixo",
             "sobrou texto depois de #%s", diretiva);
}

static void definir(void)
{
    Token t, c;
    Macro *m = novo(sizeof *m);
    Lista corpo = { NULL, 0, 0 };
    Leitor salvo;
    if (!da_linha(&t) || t.tipo != T_NOME)
        erro(t.pos, "o formato é: #define NOME valor", "falta o nome depois de #define");
    if (t.nome == n_defined)
        erro(t.pos, NULL, "'defined' não pode ser nome de macro");
    m->nome = t.nome;
    /* M(a) é macro com parâmetros só se o ( vem colado no nome. */
    salvo = leit[nl - 1];
    if (da_linha(&c) && c.tipo == T_PONT && c.pont == '(' && !(c.marcas & TF_ESPACO)) {
        Nome *ps[32];
        m->funcao = 1;
        for (;;) {
            if (!da_linha(&c)) erro(t.pos, "feche a lista de parâmetros da macro com )", "falta ) em #define %N", t.nome);
            if (c.tipo == T_PONT && c.pont == ')' && m->qtd_params == 0) break;
            if (c.tipo == T_PONT && c.pont == P_RETIC) { m->variadica = 1; c.tipo = T_NOME; c.nome = n_va_args; }
            if (c.tipo != T_NOME)
                erro(c.pos, "os parâmetros de uma macro são só nomes: #define SOMA(a, b) ((a) + (b))", "parâmetro de macro inválido");
            if (m->qtd_params == 32) erro(c.pos, NULL, "macro com parâmetros demais (o máximo é 32)");
            ps[m->qtd_params++] = c.nome;
            if (!da_linha(&c)) erro(t.pos, "feche a lista de parâmetros da macro com )", "falta ) em #define %N", t.nome);
            if (c.tipo == T_PONT && c.pont == ')') break;
            if (c.tipo != T_PONT || c.pont != ',' || m->variadica)
                erro(c.pos, "separe os parâmetros com vírgula e feche com )", "lista de parâmetros da macro mal escrita");
        }
        m->params = novo(sizeof(Nome *) * (u32)(m->qtd_params + 1));
        copia_bytes(m->params, ps, sizeof(Nome *) * (u32)m->qtd_params);
    } else {
        leit[nl - 1] = salvo;
    }
    while (da_linha(&c)) junta(&corpo, &c);
    if (corpo.n && corpo.t[0].tipo == T_PONT && corpo.t[0].pont == P_COLA)
        erro(corpo.t[0].pos, "## junta o que vem antes com o que vem depois; precisa ter algo dos dois lados", "## no começo da macro");
    if (corpo.n && corpo.t[corpo.n - 1].tipo == T_PONT && corpo.t[corpo.n - 1].pont == P_COLA)
        erro(corpo.t[corpo.n - 1].pos, "## junta o que vem antes com o que vem depois; precisa ter algo dos dois lados", "## no fim da macro");
    m->corpo = corpo.t;
    m->qtd_corpo = corpo.n;
    t.nome->macro = m;
}

static void abrir_arquivo(const char *caminho, u8 *d, u32 n, Pos onde, int tem_onde)
{
    int i, a = -1;
    for (i = 0; i < G.qtd_arqs; i++)
        if (txt_igual(G.arqs[i].nome, caminho)) { a = i; break; }
    if (a >= 0 && G.arqs[a].uma_vez) return;
    if (a < 0) a = novo_arquivo(caminho, d, n);
    if (nl == MAX_ARQUIVOS) {
        if (tem_onde) erro(onde, "provavelmente um arquivo inclui a si mesmo; proteja o cabeçalho com #ifndef/#define/#endif", "#include aninhado demais");
        erro_geral(NULL, "#include aninhado demais");
    }
    base_cond[nl] = nc;
    lex_abrir(&leit[nl++], a);
}

static int tenta_ler(const char *pasta, u32 tam_pasta, const char *nome, char *caminho, u32 max, u8 **d, u32 *n)
{
    u32 k = 0, i;
    int a;
    for (i = 0; i < tam_pasta && k + 2 < max; i++) caminho[k++] = pasta[i];
    if (k && caminho[k - 1] != '/') caminho[k++] = '/';
    for (i = 0; nome[i] && k + 1 < max; i++) caminho[k++] = nome[i];
    caminho[k] = 0;
    for (a = 0; a < G.qtd_arqs; a++)        /* já lido antes (outro #include do mesmo) */
        if (txt_igual(G.arqs[a].nome, caminho)) { *d = G.arqs[a].dados; *n = G.arqs[a].tam; return 1; }
    return ler_arquivo(caminho, d, n) == 0;
}

static const char *dica_include(const char *nome)
{
    static const char *libc[] = { "stdio.h", "stdlib.h", "string.h", "stdint.h", "stdbool.h", "stddef.h", "stdarg.h",
                                  "unistd.h", "ctype.h", "math.h", "assert.h", "limits.h", "time.h", "errno.h", NULL };
    int i;
    for (i = 0; libc[i]; i++)
        if (txt_igual(nome, libc[i]))
            return "o SaguiOS não tem a biblioteca padrão do C. Use #include \"sagui.h\": ele já traz uint8_t, bool, NULL, "
                   "va_list e as funções do sistema (imprimir no lugar de printf, aloca no lugar de malloc, tam_texto no lugar de strlen)";
    return "os cabeçalhos do sistema são \"sagui.h\", \"gui.h\" e \"http.h\"; um arquivo seu precisa estar na mesma pasta do .c";
}

static void incluir(Pos onde)
{
    char nome[128], caminho[200];
    const char *atual = G.arqs[leit[nl - 1].arq].nome;
    Token t;
    u8 *d = NULL;
    u32 n = 0, barra = 0, i;
    int achou = 0, entre_aspas = 0, p;
    if (lex_resto_include(&leit[nl - 1], nome, sizeof nome)) {
        /* <arquivo.h>: só nas pastas do sistema */
    } else if (da_linha(&t) && t.tipo == T_TEXTO && t.tam < sizeof nome) {
        copia_bytes(nome, t.texto, t.tam + 1);
        entre_aspas = 1;
    } else {
        erro(onde, "o formato é: #include \"sagui.h\"", "#include sem nome de arquivo");
    }
    sobra_na_linha("include");
    if (entre_aspas) {
        for (i = 0; atual[i]; i++) if (atual[i] == '/') barra = i + 1;
        achou = tenta_ler(atual, barra, nome, caminho, sizeof caminho, &d, &n);
    }
    for (p = 0; !achou && p < G.op->qtd_pastas; p++)
        achou = tenta_ler(G.op->pastas[p], tam_txt(G.op->pastas[p]), nome, caminho, sizeof caminho, &d, &n);
    if (!achou) erro(onde, dica_include(nome), "não achei o arquivo '%s' do #include", nome);
    abrir_arquivo(caminho, d, n, onde, 1);
}

/* ---- contas do #if ---- */

static Token *ct; static int cn, ci;
static Pos    cpos;

static i64 conta_cond(void);

static int c_pont(int p) { return ci < cn && ct[ci].tipo == T_PONT && ct[ci].pont == p; }

static i64 conta_un(void)
{
    i64 v;
    if (cn - ci > 400) erro(cpos, NULL, "condição do #if comprida demais");
    if (ci >= cn) erro(cpos, "depois de #if vem uma conta, como: #if VERSAO >= 2", "a condição do #if está incompleta");
    if (c_pont('!')) { ci++; return !conta_un(); }
    if (c_pont('-')) { ci++; return -conta_un(); }
    if (c_pont('+')) { ci++; return conta_un(); }
    if (c_pont('~')) { ci++; return ~conta_un(); }
    if (c_pont('(')) {
        ci++;
        v = conta_cond();
        if (!c_pont(')')) erro(cpos, NULL, "falta ) na condição do #if");
        ci++;
        return v;
    }
    if (ct[ci].tipo == T_NUM) return ct[ci++].valor;
    if (ct[ci].tipo == T_NOME) { ci++; return 0; }      /* nome sem #define vale 0 */
    erro(ct[ci].pos, "a condição de um #if só pode ter números, macros, defined(NOME) e contas com eles",
         "isto não pode aparecer na condição de um #if");
}

static int prec_de(Token *t)
{
    if (t->tipo != T_PONT) return -1;
    switch (t->pont) {
    case '*': case '/': case '%': return 10;
    case '+': case '-': return 9;
    case P_DESL_E: case P_DESL_D: return 8;
    case '<': case '>': case P_MENOR_IG: case P_MAIOR_IG: return 7;
    case P_IGUAL: case P_DIF: return 6;
    case '&': return 5;
    case '^': return 4;
    case '|': return 3;
    case P_E_LOG: return 2;
    case P_OU_LOG: return 1;
    }
    return -1;
}

static i64 conta_bin(int minimo)
{
    i64 a = conta_un();
    while (ci < cn && prec_de(&ct[ci]) >= minimo) {
        int op = ct[ci].pont, p = prec_de(&ct[ci]);
        Pos onde = ct[ci].pos;
        i64 b;
        ci++;
        b = conta_bin(p + 1);
        switch (op) {
        case '*': a *= b; break;
        case '/': case '%':
            if (b == 0) erro(onde, NULL, "divisão por zero na condição do #if");
            { i64 r; i64 q = div_s64(a, b, &r); a = op == '/' ? q : r; }
            break;
        case '+': a += b; break;
        case '-': a -= b; break;
        case P_DESL_E: a = (i64)((u64)a << (b & 63)); break;
        case P_DESL_D: a >>= (b & 63); break;
        case '<': a = a < b; break;
        case '>': a = a > b; break;
        case P_MENOR_IG: a = a <= b; break;
        case P_MAIOR_IG: a = a >= b; break;
        case P_IGUAL: a = a == b; break;
        case P_DIF: a = a != b; break;
        case '&': a &= b; break;
        case '^': a ^= b; break;
        case '|': a |= b; break;
        case P_E_LOG: a = a && b; break;
        case P_OU_LOG: a = a || b; break;
        }
    }
    return a;
}

static i64 conta_cond(void)
{
    i64 c = conta_bin(1);
    if (c_pont('?')) {
        i64 a, b;
        ci++;
        a = conta_cond();
        if (!c_pont(':')) erro(cpos, NULL, "falta : na condição do #if");
        ci++;
        b = conta_cond();
        return c ? a : b;
    }
    return c;
}

/* Lê a condição de um #if ou #elif e diz se vale. */
static int avalia_if(Pos onde)
{
    Lista bruta = { NULL, 0, 0 }, pronta = { NULL, 0, 0 };
    Token t, marca;
    i64 v;
    /* 1: "defined NOME" vira 1 ou 0 antes de qualquer expansão */
    while (da_linha(&t)) {
        if (t.tipo == T_NOME && t.nome == n_defined) {
            Token n;
            int par = 0;
            if (!da_linha(&n)) erro(t.pos, "o formato é: defined(NOME)", "falta o nome depois de defined");
            if (n.tipo == T_PONT && n.pont == '(') { par = 1; if (!da_linha(&n)) n.tipo = T_FIM; }
            if (n.tipo != T_NOME) erro(t.pos, "o formato é: defined(NOME)", "falta o nome depois de defined");
            t.tipo = T_NUM;
            t.valor = n.nome->macro != NULL;
            if (par && (!da_linha(&n) || n.tipo != T_PONT || n.pont != ')'))
                erro(t.pos, "o formato é: defined(NOME)", "falta ) depois de defined(NOME");
        }
        junta(&bruta, &t);
    }
    if (bruta.n == 0) erro(onde, "depois de #if vem uma conta, como: #if VERSAO >= 2", "#if sem condição");
    /* 2: expande as macros da linha */
    zera_bytes(&marca, sizeof marca);
    marca.tipo = T_MARCA;
    junta(&bruta, &marca);
    empilha(bruta.t, bruta.n, NULL);
    for (;;) {
        expandido(&t);
        if (t.tipo == T_MARCA) break;
        junta(&pronta, &t);
    }
    /* 3: faz a conta */
    ct = pronta.t; cn = pronta.n; ci = 0; cpos = onde;
    v = conta_cond();
    if (ci < cn) erro(ct[ci].pos, NULL, "sobrou texto na condição do #if");
    return v != 0;
}

/* Passa por cima de um trecho desligado, até o #elif/#else que vale ou o
 * #endif. Os #if de dentro são só contados. */
static void pular(void)
{
    Token t, d;
    int fundo = 0;
    Cond *c = &cond[nc - 1];
    G.pulando = 1;
    for (;;) {
        lex_proximo(&leit[nl - 1], &t);
        if (t.tipo == T_FIM) {
            G.pulando = 0;
            erro(c->pos, "todo #if, #ifdef e #ifndef precisa de um #endif", "este #if não foi fechado com #endif");
        }
        if (t.tipo != T_PONT || t.pont != '#' || !(t.marcas & TF_INICIO_LINHA)) continue;
        if (!da_linha(&d) || d.tipo != T_NOME) continue;
        if (txt_igual(d.nome->texto, "if") || txt_igual(d.nome->texto, "ifdef") || txt_igual(d.nome->texto, "ifndef")) fundo++;
        else if (txt_igual(d.nome->texto, "endif")) {
            if (fundo == 0) { nc--; break; }
            fundo--;
        }
        else if (fundo == 0 && txt_igual(d.nome->texto, "else")) {
            if (c->tem_else) { G.pulando = 0; erro(d.pos, "cada #if só pode ter um #else", "#else repetido"); }
            c->tem_else = 1;
            if (!c->ja_pegou) { c->ja_pegou = 1; break; }
        }
        else if (fundo == 0 && txt_igual(d.nome->texto, "elif")) {
            if (c->tem_else) { G.pulando = 0; erro(d.pos, "o #elif tem de vir antes do #else", "#elif depois do #else"); }
            if (!c->ja_pegou) {
                int v;
                G.pulando = 0;
                v = avalia_if(d.pos);
                G.pulando = 1;
                if (v) { c->ja_pegou = 1; break; }
            }
        }
    }
    G.pulando = 0;
}

static void abre_cond(Pos onde, int vale)
{
    if (nc == MAX_COND) erro(onde, NULL, "#if aninhado demais");
    cond[nc].ja_pegou = (u8)vale;
    cond[nc].tem_else = 0;
    cond[nc].pos = onde;
    nc++;
    if (!vale) pular();
}

static void diretiva(Pos onde)
{
    Token d, t;
    const char *n;
    if (!da_linha(&d)) return;          /* "#" sozinho numa linha não faz nada */
    if (d.tipo != T_NOME)
        erro(d.pos, "as diretivas são #include, #define, #undef, #if, #ifdef, #ifndef, #elif, #else, #endif e #error",
             "depois de # tem de vir o nome de uma diretiva");
    n = d.nome->texto;
    if (txt_igual(n, "define")) definir();
    else if (txt_igual(n, "undef")) {
        if (!da_linha(&t) || t.tipo != T_NOME) erro(d.pos, "o formato é: #undef NOME", "falta o nome depois de #undef");
        t.nome->macro = NULL;
        sobra_na_linha("undef");
    }
    else if (txt_igual(n, "include")) incluir(d.pos);
    else if (txt_igual(n, "ifdef") || txt_igual(n, "ifndef")) {
        if (!da_linha(&t) || t.tipo != T_NOME) erro(d.pos, "o formato é: #ifdef NOME", "falta o nome depois de #%s", n);
        sobra_na_linha(n);
        abre_cond(onde, (t.nome->macro != NULL) == (n[2] == 'd'));
    }
    else if (txt_igual(n, "if")) abre_cond(onde, avalia_if(d.pos));
    else if (txt_igual(n, "elif") || txt_igual(n, "else")) {
        /* Chegar aqui lendo normalmente quer dizer que o trecho de cima valeu:
         * o resto do #if é para pular. */
        if (nc <= base_cond[nl - 1]) erro(onde, "um #else ou #elif só pode vir depois de um #if, #ifdef ou #ifndef", "#%s sem #if antes", n);
        if (cond[nc - 1].tem_else) erro(onde, "cada #if só pode ter um #else, e ele vem por último", "#%s depois do #else", n);
        if (n[2] == 's') { cond[nc - 1].tem_else = 1; sobra_na_linha("else"); }
        else resto_da_linha();
        cond[nc - 1].ja_pegou = 1;
        pular();
    }
    else if (txt_igual(n, "endif")) {
        if (nc <= base_cond[nl - 1]) erro(onde, "apague este #endif ou ponha o #if que falta", "#endif sem #if antes");
        sobra_na_linha("endif");
        nc--;
    }
    else if (txt_igual(n, "error")) {
        char texto[200];
        u32 k = 0, i;
        while (da_linha(&t)) {
            const char *s = t.tipo == T_NOME ? t.nome->texto : t.tipo == T_TEXTO ? t.texto : t.tipo == T_PONT ? texto_pont(t.pont) : "";
            char num[24];
            if (t.tipo == T_NUM) { formatar(num, sizeof num, "%D", t.valor); s = num; }
            if (k && (t.marcas & TF_ESPACO) && k + 1 < sizeof texto) texto[k++] = ' ';
            for (i = 0; s[i] && k + 1 < sizeof texto; i++) texto[k++] = s[i];
        }
        texto[k] = 0;
        erro(onde, "esta linha #error foi escrita no programa para parar a compilação de propósito", "#error: %s", texto);
    }
    else if (txt_igual(n, "pragma")) {
        if (da_linha(&t) && t.tipo == T_NOME && txt_igual(t.nome->texto, "once")) G.arqs[leit[nl - 1].arq].uma_vez = 1;
        resto_da_linha();
    }
    else
        erro(d.pos, "as diretivas são #include, #define, #undef, #if, #ifdef, #ifndef, #elif, #else, #endif e #error",
             "diretiva #%s desconhecida", n);
}

/* ------------------------------------------------------------ leitura */

/* O próximo token sem expandir macros: da pilha de fontes ou do arquivo. */
static void bruto(Token *t)
{
    while (nf && fontes[nf - 1].i == fontes[nf - 1].n) {
        if (fontes[nf - 1].m) fontes[nf - 1].m->expandindo = 0;
        nf--;
    }
    if (nf) { *t = fontes[nf - 1].t[fontes[nf - 1].i++]; return; }
    for (;;) {
        if (nl == 0) { zera_bytes(t, sizeof *t); t->tipo = T_FIM; return; }
        lex_proximo(&leit[nl - 1], t);
        if (t->tipo == T_FIM) {
            if (nc > base_cond[nl - 1])
                erro(cond[nc - 1].pos, "todo #if, #ifdef e #ifndef precisa de um #endif", "este #if não foi fechado com #endif");
            if (nl == 1) { nl = 0; return; }    /* o fim do arquivo principal é o fim de tudo */
            nl--;
            continue;
        }
        if (t->tipo == T_PONT && t->pont == '#' && (t->marcas & TF_INICIO_LINHA)) { diretiva(t->pos); continue; }
        return;
    }
}

/* ------------------------------------------------------------ macros */

/* Como um token se escreve (para # e ##). */
static u32 soletra(Token *t, char *dst, u32 max)
{
    u32 k = 0, i;
    const char *s = "";
    char num[24];
    if (t->tipo == T_NOME) s = t->nome->texto;
    else if (t->tipo == T_PONT) s = texto_pont(t->pont);
    else if (t->tipo == T_NUM) { formatar(num, sizeof num, "%D", t->valor); s = num; }
    else if (t->tipo == T_TEXTO) {
        if (k + 1 < max) dst[k++] = '"';
        for (i = 0; i < t->tam && k + 2 < max; i++) {
            if (t->texto[i] == '"' || t->texto[i] == '\\') dst[k++] = '\\';
            dst[k++] = t->texto[i];
        }
        if (k + 1 < max) dst[k++] = '"';
        dst[k] = 0;
        return k;
    }
    for (i = 0; s[i] && k + 1 < max; i++) dst[k++] = s[i];
    dst[k] = 0;
    return k;
}

/* a ## b: escreve os dois juntos e lê de novo como um token só. */
static void cola(Token *a, Token *b, Pos onde)
{
    static char buf[256];
    Leitor l;
    Token r, sobra;
    u32 n = soletra(a, buf, sizeof buf);
    n += soletra(b, buf + n, sizeof buf - n);
    if (arq_cola < 0) arq_cola = novo_arquivo("<macro>", (u8 *)buf, n);
    G.arqs[arq_cola].dados = (u8 *)buf;
    G.arqs[arq_cola].tam = n;
    lex_abrir(&l, arq_cola);
    G.pulando = 1;      /* um erro aqui deve apontar para o programa, não para o rascunho */
    lex_proximo(&l, &r);
    lex_proximo(&l, &sobra);
    G.pulando = 0;
    if (r.tipo == T_LIXO || r.tipo == T_FIM || sobra.tipo != T_FIM)
        erro(onde, "## só pode juntar pedaços que formem um nome ou um número, como pre ## fixo", "'%s' (resultado de ##) não é um token válido", buf);
    r.pos = onde;
    r.marcas = a->marcas & TF_ESPACO;
    *a = r;
}

static int indice_param(Macro *m, Token *t)
{
    int i;
    if (t->tipo != T_NOME) return -1;
    for (i = 0; i < m->qtd_params; i++) if (m->params[i] == t->nome) return i;
    return -1;
}

/* Troca a chamada de uma macro com parâmetros pelo corpo dela. */
static void expande_funcao(Macro *m, Token *chamada)
{
    Lista *args = novo(sizeof(Lista) * (u32)(m->qtd_params + 1));
    Lista *prontos = novo(sizeof(Lista) * (u32)(m->qtd_params + 1));
    u8 *feito = novo((u32)m->qtd_params + 1);
    Lista saida = { NULL, 0, 0 };
    Token t, marca;
    int qtd = 0, fundo = 0, i, colar = 0;

    for (;;) {
        bruto(&t);
        if (t.tipo == T_FIM || t.tipo == T_MARCA)
            erro(chamada->pos, "confira se cada ( tem o seu )", "falta ) na chamada da macro '%N'", m->nome);
        if (t.tipo == T_PONT && t.pont == ')' && fundo == 0) break;
        if (t.tipo == T_PONT && t.pont == '(') fundo++;
        if (t.tipo == T_PONT && t.pont == ')') fundo--;
        if (t.tipo == T_PONT && t.pont == ',' && fundo == 0 && !(m->variadica && qtd == m->qtd_params - 1)) {
            qtd++;
            if (qtd >= m->qtd_params)
                erro(chamada->pos, "confira as vírgulas e os parênteses da chamada; a macro foi criada num #define mais acima",
                     "a macro '%N' recebe %d argumento(s), mas vieram mais", m->nome, m->qtd_params);
            continue;
        }
        if (m->qtd_params == 0)
            erro(chamada->pos, NULL, "a macro '%N' não recebe argumentos", m->nome);
        junta(&args[qtd], &t);
    }
    if (m->qtd_params) qtd++;
    if (qtd < m->qtd_params && !(m->variadica && qtd == m->qtd_params - 1))
        erro(chamada->pos, "confira as vírgulas e os parênteses da chamada; a macro foi criada num #define mais acima",
             "a macro '%N' recebe %d argumento(s), mas vieram %d", m->nome, m->qtd_params, qtd);

    zera_bytes(&marca, sizeof marca);
    marca.tipo = T_MARCA;
    for (i = 0; i < m->qtd_corpo; i++) {
        Token *b = &m->corpo[i];
        int p = indice_param(m, b), k;
        Lista *l;
        int antes = saida.n;
        if (b->tipo == T_PONT && b->pont == '#' && i + 1 < m->qtd_corpo && indice_param(m, &m->corpo[i + 1]) >= 0) {
            /* #param: o argumento vira um texto entre aspas */
            static char buf[512];
            u32 n = 0;
            l = &args[indice_param(m, &m->corpo[++i])];
            for (k = 0; k < l->n; k++) {
                if (k && (l->t[k].marcas & TF_ESPACO) && n + 1 < sizeof buf) buf[n++] = ' ';
                n += soletra(&l->t[k], buf + n, sizeof buf - n);
            }
            t = *b;
            t.tipo = T_TEXTO;
            t.texto = copia_txt(buf, n);
            t.tam = n;
            junta(&saida, &t);
        }
        else if (b->tipo == T_PONT && b->pont == P_COLA) { colar = 1; continue; }
        else if (p >= 0) {
            int vizinho_cola = colar || (i + 1 < m->qtd_corpo && m->corpo[i + 1].tipo == T_PONT && m->corpo[i + 1].pont == P_COLA);
            if (vizinho_cola) l = &args[p];
            else {
                if (!feito[p]) {
                    /* o argumento é expandido sozinho, antes de entrar no corpo */
                    Lista copia = { NULL, 0, 0 };
                    for (k = 0; k < args[p].n; k++) junta(&copia, &args[p].t[k]);
                    junta(&copia, &marca);
                    empilha(copia.t, copia.n, NULL);
                    for (;;) {
                        expandido(&t);
                        if (t.tipo == T_MARCA) break;
                        junta(&prontos[p], &t);
                    }
                    feito[p] = 1;
                }
                l = &prontos[p];
            }
            for (k = 0; k < l->n; k++) {
                junta(&saida, &l->t[k]);
                if (k == 0) saida.t[saida.n - 1].marcas = (u8)((saida.t[saida.n - 1].marcas & ~TF_ESPACO) | (b->marcas & TF_ESPACO));
            }
        }
        else junta(&saida, b);
        if (colar) {
            /* junta o último que já estava com o primeiro que acabou de entrar */
            if (antes > 0 && saida.n > antes) {
                cola(&saida.t[antes - 1], &saida.t[antes], chamada->pos);
                for (k = antes; k + 1 < saida.n; k++) saida.t[k] = saida.t[k + 1];
                saida.n--;
            }
            colar = 0;
        }
    }
    for (i = 0; i < saida.n; i++) saida.t[i].pos = chamada->pos;
    if (saida.n) empilha(saida.t, saida.n, m);
}

/* O próximo token com as macros já trocadas. */
static void expandido(Token *t)
{
    for (;;) {
        Macro *m;
        bruto(t);
        if (t->tipo != T_NOME || !t->nome->macro || (t->marcas & TF_NAO_EXPANDE)) return;
        m = t->nome->macro;
        if (m->expandindo) { t->marcas |= TF_NAO_EXPANDE; return; }
        if (m->especial == 1) { t->tipo = T_NUM; t->valor = t->pos.lin; return; }
        if (m->especial == 2) {
            t->tipo = T_TEXTO;
            t->texto = G.arqs[t->pos.arq].nome;
            t->tam = tam_txt(t->texto);
            return;
        }
        if (m->funcao) {
            Token p;
            bruto(&p);
            if (p.tipo != T_PONT || p.pont != '(') {    /* sem parênteses é um nome comum */
                if (p.tipo != T_FIM) devolve(&p);
                return;
            }
            expande_funcao(m, t);
            continue;
        }
        if (m->qtd_corpo) {
            Token *c = novo(sizeof(Token) * (u32)m->qtd_corpo);
            int i;
            copia_bytes(c, m->corpo, sizeof(Token) * (u32)m->qtd_corpo);
            for (i = 0; i < m->qtd_corpo; i++) c[i].pos = t->pos;
            empilha(c, m->qtd_corpo, m);
        }
    }
}

static void macro_fixa(const char *nome, int especial, i64 valor)
{
    Macro *m = novo(sizeof *m);
    m->nome = nome_c(nome);
    m->especial = (u8)especial;
    if (!especial) {
        m->corpo = novo(sizeof(Token));
        m->corpo[0].tipo = T_NUM;
        m->corpo[0].valor = valor;
        m->qtd_corpo = 1;
    }
    m->nome->macro = m;
}

void pp_iniciar(void)
{
    fontes = novo(sizeof(Fonte) * MAX_FONTES);
    leit = novo(sizeof(Leitor) * MAX_ARQUIVOS);
    base_cond = novo(sizeof(int) * MAX_ARQUIVOS);
    cond = novo(sizeof(Cond) * MAX_COND);
    nf = nl = nc = 0;
    { int i; for (i = 0; i < G.qtd_arqs; i++) G.arqs[i].uma_vez = 0; }
    arq_cola = -1;
    n_defined = nome_c("defined");
    n_va_args = nome_c("__VA_ARGS__");
    macro_fixa("__LINE__", 1, 0);
    macro_fixa("__FILE__", 2, 0);
    macro_fixa("__SAGUI__", 0, 1);      /* para um programa saber que é o compilador do SaguiOS */
    macro_fixa("__STDC__", 0, 1);
}

void pp_arquivo(const char *caminho)
{
    u8 *d;
    u32 n;
    Token t;
    Pos nada = { 0, 0, 0 };
    if (ler_arquivo(caminho, &d, &n) < 0)
        erro_geral("confira o nome e a pasta; para ver os arquivos use o comando ls", "não achei o arquivo '%s'", caminho);
    abrir_arquivo(caminho, d, n, nada, 0);
    for (;;) {
        expandido(&t);
        G.toks = crescer(G.toks, &G.cap_toks, G.qtd_toks, sizeof(Token));
        G.toks[G.qtd_toks++] = t;
        if (t.tipo == T_FIM) break;
    }
    /* O fim leva a posição do último token, para "falta }" apontar para algum lugar. */
    if (G.qtd_toks > 1) G.toks[G.qtd_toks - 1].pos = G.toks[G.qtd_toks - 2].pos;
    else { G.toks[0].pos.arq = 0; G.toks[0].pos.lin = 1; G.toks[0].pos.col = 1; }
}
