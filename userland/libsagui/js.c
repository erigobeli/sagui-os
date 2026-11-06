/* js.c - um interpretador pequeno de JavaScript para o Cipó.
 *
 * Anda pelo texto do programa e executa na hora, sem árvore. Entende var, let,
 * const, function, if, else, while, for, return, inteiros, textos, booleanos,
 * operadores comuns, document.write, document.title, getElementById com
 * textContent e innerHTML, console.log, alert, parseInt e .length.
 *
 * Não é JavaScript completo: sem objetos, vetores, classes, closures, arrow
 * functions, eventos, timers ou fetch. Números são inteiros de 32 bits, porque
 * os programas não usam a FPU. Script com algo desconhecido para ali mesmo. */
#include "sagui.h"

#define MAX_VARS 256
#define PASSOS_MAX 300000
#define PROF_MAX 48

enum { V_INDEF, V_NUM, V_TEXTO, V_BOOL, V_FUNCAO, V_DOC, V_CONSOLE, V_ELEM };
typedef struct { int tipo; int n; const char *s; } valor_t;
typedef struct { char nome[32]; valor_t v; int nivel; } var_t;

enum { T_FIM = 256, T_NUM, T_TEXTO, T_ID, T_IGUAL, T_DIF, T_MENORI, T_MAIORI, T_E, T_OU, T_MAISI, T_MENOSI, T_MAISMAIS, T_MENOSMENOS };

static const js_hooks_t *gancho;
static const char *src;
static int tam, pos, tpos, tk, tnum, passos, nivel, qtd_vars, prof;   /* tpos: início do token atual */
static char tid[32], *ttexto;
static var_t vars[MAX_VARS];
static bool erro, retornando, parando, continuando;
static valor_t retorno;

/* Textos vivem numa arena que some quando o script acaba. */
static char *arena;
static int arena_n;
#define ARENA_MAX (256 * 1024)

static char *guardar(const char *t, int n)
{
    if (arena_n + n + 1 > ARENA_MAX) { erro = true; return (char *)""; }
    char *d = arena + arena_n;
    memcpy(d, t, (size_t)n);
    d[n] = 0;
    arena_n += n + 1;
    return d;
}

static void parar(const char *m)
{
    if (!erro) registrar("js: parou: %s (perto de %d)\n", m, pos);
    erro = true;
}

static bool letra_id(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
static bool digito(char c) { return c >= '0' && c <= '9'; }

static void proximo(void)
{
    for (;;) {
        while (pos < tam && (src[pos] == ' ' || src[pos] == '\n' || src[pos] == '\t' || src[pos] == '\r')) pos++;
        if (pos + 1 < tam && src[pos] == '/' && src[pos + 1] == '/') { while (pos < tam && src[pos] != '\n') pos++; continue; }
        if (pos + 1 < tam && src[pos] == '/' && src[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < tam && !(src[pos] == '*' && src[pos + 1] == '/')) pos++;
            pos += 2;
            continue;
        }
        break;
    }
    tpos = pos;
    if (pos >= tam) { tk = T_FIM; return; }
    char c = src[pos];
    if (digito(c)) {
        tnum = 0;
        while (pos < tam && digito(src[pos])) tnum = tnum * 10 + src[pos++] - '0';
        tk = T_NUM;
        return;
    }
    if (letra_id(c)) {
        int k = 0;
        while (pos < tam && (letra_id(src[pos]) || digito(src[pos]))) { if (k < 31) tid[k++] = src[pos]; pos++; }
        tid[k] = 0;
        tk = T_ID;
        return;
    }
    if (c == '"' || c == '\'') {
        static char b[4096];
        int k = 0;
        pos++;
        while (pos < tam && src[pos] != c) {
            char x = src[pos++];
            if (x == '\\' && pos < tam) {
                x = src[pos++];
                x = x == 'n' ? '\n' : x == 't' ? '\t' : x;
            }
            if (k < (int)sizeof b - 1) b[k++] = x;
        }
        pos++;
        ttexto = guardar(b, k);
        tk = T_TEXTO;
        return;
    }
    char d = pos + 1 < tam ? src[pos + 1] : 0, e = pos + 2 < tam ? src[pos + 2] : 0;
    int duplo = 0, n = 2;
    if (c == '=' && d == '=') { duplo = T_IGUAL; if (e == '=') n = 3; }
    else if (c == '!' && d == '=') { duplo = T_DIF; if (e == '=') n = 3; }
    else if (c == '<' && d == '=') duplo = T_MENORI;
    else if (c == '>' && d == '=') duplo = T_MAIORI;
    else if (c == '&' && d == '&') duplo = T_E;
    else if (c == '|' && d == '|') duplo = T_OU;
    else if (c == '+' && d == '=') duplo = T_MAISI;
    else if (c == '-' && d == '=') duplo = T_MENOSI;
    else if (c == '+' && d == '+') duplo = T_MAISMAIS;
    else if (c == '-' && d == '-') duplo = T_MENOSMENOS;
    if (duplo) { pos += n; tk = duplo; return; }
    pos++;
    tk = (unsigned char)c;
}

/* Volta a ler a partir de um ponto: o token é lido de novo. */
static void voltar(int p) { pos = p; proximo(); }

static void exigir(int t)
{
    if (tk != t) parar("sintaxe");
    else proximo();
}

static bool palavra(const char *p) { return tk == T_ID && compara_texto(tid, p) == 0; }

static valor_t num(int n) { valor_t v = { V_NUM, n, NULL }; return v; }
static valor_t texto(const char *s) { valor_t v = { V_TEXTO, 0, s }; return v; }
static valor_t booleano(bool b) { valor_t v = { V_BOOL, b, NULL }; return v; }

static bool verdade(valor_t v)
{
    if (v.tipo == V_TEXTO) return v.s[0] != 0;
    if (v.tipo == V_INDEF) return false;
    return v.tipo != V_NUM && v.tipo != V_BOOL ? true : v.n != 0;
}

static const char *como_texto(valor_t v)
{
    char b[16];
    if (v.tipo == V_TEXTO) return v.s;
    if (v.tipo == V_NUM) { int n = formatar_texto(b, sizeof b, "%d", v.n); return guardar(b, n); }
    if (v.tipo == V_BOOL) return v.n ? "true" : "false";
    return v.tipo == V_INDEF ? "undefined" : "[object]";
}

static valor_t juntar(valor_t a, valor_t b)
{
    const char *p = como_texto(a), *q = como_texto(b);
    int n1 = (int)tam_texto(p), n2 = (int)tam_texto(q);
    if (arena_n + n1 + n2 + 1 > ARENA_MAX) { erro = true; return a; }
    char *d = arena + arena_n;
    memmove(d, p, (size_t)n1);
    memmove(d + n1, q, (size_t)n2);
    d[n1 + n2] = 0;
    arena_n += n1 + n2 + 1;
    return texto(d);
}

static var_t *achar(const char *n)
{
    for (int i = qtd_vars - 1; i >= 0; i--)
        if ((vars[i].nivel == nivel || vars[i].nivel == 0) && compara_texto(vars[i].nome, n) == 0) return &vars[i];
    return NULL;
}

static var_t *declarar(const char *n, valor_t v)
{
    if (qtd_vars == MAX_VARS) { parar("variáveis demais"); return &vars[0]; }
    var_t *x = &vars[qtd_vars++];
    copia_texto(x->nome, n, sizeof x->nome);
    x->v = v;
    x->nivel = nivel;
    return x;
}

static valor_t expressao(bool vivo);
static void comando(bool vivo);

/* Pula um bloco { ... }; tk está no "{". Devolve com tk depois do "}". */
static void pular_bloco(void)
{
    int d = 0;
    do {
        if (tk == '{') d++;
        else if (tk == '}') d--;
        else if (tk == T_FIM) { parar("bloco aberto"); return; }
        proximo();
    } while (d && !erro);
}

/* Depois de "function": lê a lista de parâmetros e guarda onde ela começa. */
static valor_t ler_funcao(void)
{
    valor_t f = { V_FUNCAO, 0, NULL };
    int ini = pos;
    if (tk != '(') { parar("função sem ("); return f; }
    f.n = ini - 1;
    while (tk != ')' && tk != T_FIM) proximo();
    proximo();
    pular_bloco();
    return f;
}

static valor_t chamar(valor_t f, valor_t *a, int n)
{
    if (f.tipo != V_FUNCAO) { parar("não é função"); return num(0); }
    if (prof >= PROF_MAX) { parar("recursão funda demais"); return num(0); }
    int s_pos = pos, s_tk = tk, s_num = tnum, s_vars = qtd_vars, s_nivel = nivel;
    char s_id[32];
    char *s_texto = ttexto;
    copia_texto(s_id, tid, sizeof s_id);
    static int contador = 1;
    pos = f.n;
    proximo();
    exigir('(');
    nivel = ++contador;
    prof++;
    for (int i = 0; tk == T_ID; i++) {
        declarar(tid, i < n ? a[i] : (valor_t){ V_INDEF, 0, NULL });
        proximo();
        if (tk == ',') proximo();
    }
    exigir(')');
    retorno = (valor_t){ V_INDEF, 0, NULL };
    comando(true);
    retornando = false;
    prof--;
    valor_t r = retorno;
    pos = s_pos; tk = s_tk; tnum = s_num; ttexto = s_texto; qtd_vars = s_vars; nivel = s_nivel;
    copia_texto(tid, s_id, sizeof tid);
    return r;
}

static int argumentos(valor_t *a, bool vivo)
{
    int n = 0;
    exigir('(');
    while (tk != ')' && tk != T_FIM && !erro) {
        valor_t v = expressao(vivo);
        if (n < 8) a[n++] = v;
        if (tk == ',') proximo();
    }
    exigir(')');
    return n;
}

static valor_t membro(valor_t o, bool vivo)
{
    while (tk == '.' && !erro) {
        proximo();
        if (tk != T_ID) { parar("nome depois do ponto"); break; }
        char m[32];
        copia_texto(m, tid, sizeof m);
        proximo();
        valor_t a[8];
        if (o.tipo == V_DOC && compara_texto(m, "write") == 0) {
            int n = argumentos(a, vivo);
            if (vivo) for (int i = 0; i < n; i++) gancho->escrever(como_texto(a[i]));
            o = (valor_t){ V_INDEF, 0, NULL };
        } else if (o.tipo == V_DOC && compara_texto(m, "getElementById") == 0) {
            int n = argumentos(a, vivo);
            o = (valor_t){ V_ELEM, 0, n ? como_texto(a[0]) : "" };
        } else if (o.tipo == V_DOC && compara_texto(m, "title") == 0) {
            if (tk == '=') { proximo(); valor_t v = expressao(vivo); if (vivo) gancho->titulo(como_texto(v)); o = v; }
            else o = texto("");
        } else if (o.tipo == V_CONSOLE) {
            int n = argumentos(a, vivo);
            if (vivo) for (int i = 0; i < n; i++) registrar("js: %s\n", como_texto(a[i]));
            o = (valor_t){ V_INDEF, 0, NULL };
        } else if (o.tipo == V_ELEM && (compara_texto(m, "textContent") == 0 || compara_texto(m, "innerHTML") == 0 || compara_texto(m, "innerText") == 0)) {
            if (tk == '=') {
                proximo();
                valor_t v = expressao(vivo);
                if (vivo) gancho->definir(o.s, como_texto(v));
                o = v;
            } else {
                char b[1024];
                int n = vivo ? gancho->ler(o.s, b, sizeof b) : 0;
                o = texto(guardar(b, n));
            }
        } else if (o.tipo == V_TEXTO && compara_texto(m, "length") == 0) {
            o = num((int)tam_texto(o.s));
        } else { parar(m); break; }
    }
    return o;
}

static valor_t primario(bool vivo)
{
    valor_t v = { V_INDEF, 0, NULL };
    if (tk == T_NUM) { v = num(tnum); proximo(); return v; }
    if (tk == T_TEXTO) { v = texto(ttexto); proximo(); return membro(v, vivo); }
    if (tk == '(') { proximo(); v = expressao(vivo); exigir(')'); return membro(v, vivo); }
    if (tk != T_ID) { parar("expressão"); return v; }
    if (palavra("true")) { proximo(); return booleano(true); }
    if (palavra("false")) { proximo(); return booleano(false); }
    if (palavra("null") || palavra("undefined")) { proximo(); return v; }
    if (palavra("function")) {
        proximo();
        if (tk == T_ID) proximo();
        return ler_funcao();
    }
    if (palavra("document")) { proximo(); return membro((valor_t){ V_DOC, 0, NULL }, vivo); }
    if (palavra("console")) { proximo(); return membro((valor_t){ V_CONSOLE, 0, NULL }, vivo); }
    char n[32];
    copia_texto(n, tid, sizeof n);
    proximo();
    valor_t a[8];
    if (compara_texto(n, "alert") == 0 && tk == '(') {
        int q = argumentos(a, vivo);
        if (vivo && q) gancho->alerta(como_texto(a[0]));
        return v;
    }
    if (compara_texto(n, "parseInt") == 0 && tk == '(') {
        int q = argumentos(a, vivo);
        return num(q ? texto_para_int(como_texto(a[0])) : 0);
    }
    var_t *x = achar(n);
    if (tk == '=') {
        proximo();
        v = expressao(vivo);
        if (vivo) { if (x) x->v = v; else { int s = nivel; nivel = 0; declarar(n, v); nivel = s; } }
        return v;
    }
    if (tk == T_MAISI || tk == T_MENOSI) {
        int op = tk;
        proximo();
        v = expressao(vivo);
        if (!vivo) return v;
        if (!x) { parar(n); return v; }
        if (op == T_MENOSI) x->v = num(x->v.n - v.n);
        else if (x->v.tipo == V_TEXTO || v.tipo == V_TEXTO) {
            x->v = juntar(x->v, v);
        } else x->v = num(x->v.n + v.n);
        return x->v;
    }
    if (tk == T_MAISMAIS || tk == T_MENOSMENOS) {
        int d = tk == T_MAISMAIS ? 1 : -1;
        proximo();
        if (!vivo) return num(0);
        if (!x) { parar(n); return v; }
        v = x->v;
        x->v = num(x->v.n + d);
        return v;
    }
    if (tk == '(') {
        int q = argumentos(a, vivo);
        if (!vivo) return v;
        if (!x) { parar(n); return v; }
        return membro(chamar(x->v, a, q), vivo);
    }
    if (!x) { if (vivo) parar(n); return v; }
    return membro(x->v, vivo);
}

static valor_t unario(bool vivo)
{
    if (tk == '!') { proximo(); return booleano(!verdade(unario(vivo))); }
    if (tk == '-') { proximo(); return num(-unario(vivo).n); }
    if (palavra("typeof")) { proximo(); unario(vivo); return texto("object"); }
    return primario(vivo);
}

static valor_t multiplicacao(bool vivo)
{
    valor_t a = unario(vivo);
    while ((tk == '*' || tk == '/' || tk == '%') && !erro) {
        int op = tk;
        proximo();
        valor_t b = unario(vivo);
        if (!vivo) continue;
        if (op != '*' && b.n == 0) { parar("divisão por zero"); return a; }
        a = num(op == '*' ? a.n * b.n : op == '/' ? a.n / b.n : a.n % b.n);
    }
    return a;
}

static valor_t soma(bool vivo)
{
    valor_t a = multiplicacao(vivo);
    while ((tk == '+' || tk == '-') && !erro) {
        int op = tk;
        proximo();
        valor_t b = multiplicacao(vivo);
        if (!vivo) continue;
        if (op == '+' && (a.tipo == V_TEXTO || b.tipo == V_TEXTO)) {
            a = juntar(a, b);
        } else a = num(op == '+' ? a.n + b.n : a.n - b.n);
    }
    return a;
}

static bool iguais(valor_t a, valor_t b)
{
    if (a.tipo == V_TEXTO || b.tipo == V_TEXTO) return compara_texto(como_texto(a), como_texto(b)) == 0;
    return a.n == b.n;
}

static valor_t comparacao(bool vivo)
{
    valor_t a = soma(vivo);
    while ((tk == '<' || tk == '>' || tk == T_MENORI || tk == T_MAIORI || tk == T_IGUAL || tk == T_DIF) && !erro) {
        int op = tk;
        proximo();
        valor_t b = soma(vivo);
        if (op == T_IGUAL) a = booleano(iguais(a, b));
        else if (op == T_DIF) a = booleano(!iguais(a, b));
        else a = booleano(op == '<' ? a.n < b.n : op == '>' ? a.n > b.n : op == T_MENORI ? a.n <= b.n : a.n >= b.n);
    }
    return a;
}

static valor_t expressao(bool vivo)
{
    valor_t a = comparacao(vivo);
    while ((tk == T_E || tk == T_OU) && !erro) {
        int op = tk;
        proximo();
        bool precisa = op == T_E ? verdade(a) : !verdade(a);
        valor_t b = comparacao(vivo && precisa);
        if (precisa) a = b;
    }
    if (tk == '?' && !erro) {
        proximo();
        bool c = verdade(a);
        valor_t x = expressao(vivo && c);
        exigir(':');
        valor_t y = expressao(vivo && !c);
        a = c ? x : y;
    }
    return a;
}

static void fim_comando(void) { if (tk == ';') proximo(); }

static void comando(bool vivo)
{
    if (erro || tk == T_FIM) return;
    if (++passos > PASSOS_MAX) { parar("passos demais"); return; }
    bool ativo = vivo && !retornando && !parando && !continuando;
    if (tk == ';') { proximo(); return; }
    if (tk == '{') {
        proximo();
        while (tk != '}' && tk != T_FIM && !erro) comando(vivo);
        exigir('}');
        return;
    }
    if (palavra("var") || palavra("let") || palavra("const")) {
        do {
            proximo();
            if (tk != T_ID) { parar("nome da variável"); return; }
            char n[32];
            copia_texto(n, tid, sizeof n);
            proximo();
            valor_t v = { V_INDEF, 0, NULL };
            if (tk == '=') { proximo(); v = expressao(ativo); }
            if (ativo) declarar(n, v);
        } while (tk == ',' && !erro);
        fim_comando();
        return;
    }
    if (palavra("function")) {
        proximo();
        char n[32];
        copia_texto(n, tid, sizeof n);
        proximo();
        valor_t f = ler_funcao();
        if (ativo) declarar(n, f);
        return;
    }
    if (palavra("if")) {
        proximo();
        exigir('(');
        bool c = verdade(expressao(ativo));
        exigir(')');
        comando(ativo && c);
        if (palavra("else")) { proximo(); comando(ativo && !c); }
        return;
    }
    if (palavra("while")) {
        proximo();
        int ini = tpos;
        for (;;) {
            voltar(ini);
            exigir('(');
            bool c = verdade(expressao(ativo));
            exigir(')');
            comando(ativo && c);
            continuando = false;
            if (!ativo || !c || parando || retornando || erro) break;
        }
        parando = false;
        return;
    }
    if (palavra("for")) {
        proximo();
        exigir('(');
        comando(ativo);                         /* início, com o ";" */
        int p_cond = tpos;
        bool primeira = true;
        int p_passo = 0;
        for (;;) {
            voltar(p_cond);
            bool c = tk == ';' ? true : verdade(expressao(ativo));
            exigir(';');
            if (primeira) { p_passo = tpos; primeira = false; }
            if (tk != ')') expressao(false);
            exigir(')');
            comando(ativo && c);
            continuando = false;
            if (!ativo || !c || parando || retornando || erro) break;
            voltar(p_passo);
            if (tk != ')') expressao(true);
        }
        parando = false;
        return;
    }
    if (palavra("return")) {
        proximo();
        valor_t v = { V_INDEF, 0, NULL };
        if (tk != ';' && tk != '}') v = expressao(ativo);
        if (ativo) { retorno = v; retornando = true; }
        fim_comando();
        return;
    }
    if (palavra("break")) { proximo(); if (ativo) parando = true; fim_comando(); return; }
    if (palavra("continue")) { proximo(); if (ativo) continuando = true; fim_comando(); return; }
    expressao(ativo);
    fim_comando();
}

bool js_executar(const char *codigo, int n, const js_hooks_t *h)
{
    if (!arena) arena = aloca(ARENA_MAX);
    if (!arena) return false;
    gancho = h;
    src = codigo; tam = n; pos = 0;
    arena_n = 0; passos = 0; nivel = 0; prof = 0;
    erro = retornando = parando = continuando = false;
    proximo();
    while (tk != T_FIM && !erro) comando(true);
    return !erro;
}

/* As variáveis globais valem entre os scripts da mesma página. */
void js_reiniciar(void) { qtd_vars = 0; }
