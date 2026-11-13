/* Um programa maior: um interpretador de contas com variáveis, feito com
 * análise descendente (uma função para cada nível de precedência). */
#include "sagui.h"

typedef struct { char nome[12]; int valor; } variavel_t;

typedef struct {
    const char *p;          /* onde a leitura está */
    variavel_t vars[16];
    int qtd_vars;
    const char *erro;
} interp_t;

static int expressao(interp_t *in);

static void brancos(interp_t *in) { while (*in->p == ' ') in->p++; }

static variavel_t *variavel(interp_t *in, const char *nome, bool criar)
{
    for (int i = 0; i < in->qtd_vars; i++)
        if (compara_texto(in->vars[i].nome, nome) == 0) return &in->vars[i];
    if (!criar || in->qtd_vars == 16) return NULL;
    copia_texto(in->vars[in->qtd_vars].nome, nome, sizeof in->vars[0].nome);
    in->vars[in->qtd_vars].valor = 0;
    return &in->vars[in->qtd_vars++];
}

static bool le_nome(interp_t *in, char *nome, int max)
{
    int n = 0;
    brancos(in);
    while ((*in->p >= 'a' && *in->p <= 'z') || *in->p == '_') {
        if (n < max - 1) nome[n++] = *in->p;
        in->p++;
    }
    nome[n] = 0;
    return n > 0;
}

static int fator(interp_t *in)
{
    char nome[12];
    brancos(in);
    if (*in->p == '(') {
        in->p++;
        int v = expressao(in);
        brancos(in);
        if (*in->p == ')') in->p++;
        else if (!in->erro) in->erro = "falta )";
        return v;
    }
    if (*in->p == '-') { in->p++; return -fator(in); }
    if (*in->p >= '0' && *in->p <= '9') {
        int v = 0;
        while (*in->p >= '0' && *in->p <= '9') v = v * 10 + (*in->p++ - '0');
        return v;
    }
    if (le_nome(in, nome, sizeof nome)) {
        variavel_t *var = variavel(in, nome, false);
        if (!var) { if (!in->erro) in->erro = "variavel desconhecida"; return 0; }
        return var->valor;
    }
    if (!in->erro) in->erro = "esperava um numero";
    return 0;
}

static int termo(interp_t *in)
{
    int v = fator(in);
    for (;;) {
        brancos(in);
        char op = *in->p;
        if (op != '*' && op != '/' && op != '%') return v;
        in->p++;
        int d = fator(in);
        if (op == '*') v *= d;
        else if (d == 0) { if (!in->erro) in->erro = "divisao por zero"; }
        else v = op == '/' ? v / d : v % d;
    }
}

static int expressao(interp_t *in)
{
    int v = termo(in);
    for (;;) {
        brancos(in);
        if (*in->p == '+') { in->p++; v += termo(in); }
        else if (*in->p == '-') { in->p++; v -= termo(in); }
        else return v;
    }
}

/* "nome = conta" guarda; "conta" só calcula. */
static void linha(interp_t *in, const char *texto)
{
    char nome[12];
    const char *comeco = texto;
    in->p = texto;
    in->erro = NULL;
    if (le_nome(in, nome, sizeof nome)) {
        brancos(in);
        if (*in->p == '=') {
            in->p++;
            int v = expressao(in);
            brancos(in);
            if (*in->p && !in->erro) in->erro = "sobrou texto";
            if (in->erro) { imprimir("%-22s erro: %s\n", texto, in->erro); return; }
            variavel(in, nome, true)->valor = v;
            imprimir("%-22s %s = %d\n", texto, nome, v);
            return;
        }
    }
    in->p = comeco;
    int v = expressao(in);
    brancos(in);
    if (*in->p && !in->erro) in->erro = "sobrou texto";
    if (in->erro) imprimir("%-22s erro: %s\n", texto, in->erro);
    else imprimir("%-22s %d\n", texto, v);
}

int principal(int argc, char **argv)
{
    static interp_t in;
    static const char *programa[] = {
        "2 + 3 * 4", "(2 + 3) * 4", "lado = 12", "area = lado * lado", "area - 100 / (lado - 2)",
        "-(-lado) % 5", "x = (area + 1", "area / (lado - 12)", "y + 1", "2 3", "total = area + lado * 4 - -1",
        "total",
    };
    (void)argc; (void)argv;
    for (unsigned i = 0; i < sizeof programa / sizeof programa[0]; i++) linha(&in, programa[i]);
    imprimir("%d variaveis\n", in.qtd_vars);
    return 0;
}
