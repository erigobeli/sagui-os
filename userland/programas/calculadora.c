/* calculadora - a Calculadora do SaguiOS (H26).
 *
 * Números em ponto fixo: um inteiro de 64 bits que guarda o valor vezes
 * 10.000 (4 casas decimais). Nada de ponto flutuante: o SaguiOS não salva o
 * estado da FPU entre processos (PRD 4). Num x86 de 32 bits, dividir dois
 * inteiros de 64 bits exigiria uma rotina da biblioteca do compilador, que
 * não usamos; a divisão longa abaixo é feita bit a bit, como no papel.
 *
 * Botões clicáveis e teclado: dígitos, vírgula, + - × ÷, =, C (limpar) e
 * ← (apagar o último dígito). Avisa divisão por zero e número grande demais. */
#include "sagui.h"
#include "gui.h"

typedef long long fixo_t;
#define ESCALA 10000LL
#define LIMITE 999999999999LL * ESCALA   /* 12 dígitos inteiros */

#define LARG 296
#define ALT  392

static jan_t *j;
static char entrada[32];      /* o que está sendo digitado ("12,5") */
static fixo_t acumulado;
static char operacao;          /* '+', '-', '*', '/' ou 0 */
static bool tem_acumulado, digitando;
static char erro[48];

/* ---- aritmética de 64 bits sem a biblioteca do compilador ---- */

static unsigned long long div_u64(unsigned long long a, unsigned long long b, unsigned long long *resto)
{
    unsigned long long q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { r -= b; q |= 1ULL << i; }
    }
    if (resto) *resto = r;
    return q;
}

static fixo_t div_s64(fixo_t a, fixo_t b)
{
    bool neg = (a < 0) != (b < 0);
    unsigned long long q = div_u64(a < 0 ? (unsigned long long)-a : (unsigned long long)a,
                                   b < 0 ? (unsigned long long)-b : (unsigned long long)b, NULL);
    return neg ? -(fixo_t)q : (fixo_t)q;
}

static fixo_t absoluto(fixo_t v) { return v < 0 ? -v : v; }

/* a * b sem estourar: confere antes, dividindo o limite. */
static bool multiplica(fixo_t a, fixo_t b, fixo_t *r)
{
    if (a != 0 && absoluto(b) > div_s64(0x7FFFFFFFFFFFFFFFLL, absoluto(a))) return false;
    *r = a * b;
    return true;
}

static bool calcular(fixo_t a, char op, fixo_t b, fixo_t *r)
{
    fixo_t t;
    switch (op) {
    case '+': *r = a + b; break;
    case '-': *r = a - b; break;
    case '*':
        if (!multiplica(a, b, &t)) { copia_texto(erro, "Número grande demais", sizeof erro); return false; }
        *r = div_s64(t, ESCALA);
        break;
    case '/':
        if (b == 0) { copia_texto(erro, "Não dá para dividir por zero", sizeof erro); return false; }
        if (!multiplica(a, ESCALA, &t)) { copia_texto(erro, "Número grande demais", sizeof erro); return false; }
        *r = div_s64(t, b);
        break;
    default: *r = b;
    }
    if (absoluto(*r) > LIMITE) { copia_texto(erro, "Número grande demais", sizeof erro); return false; }
    return true;
}

/* "12,5" -> 125000 */
static fixo_t ler_numero(const char *s)
{
    fixo_t inteiro = 0, frac = 0, peso = ESCALA / 10;
    bool neg = *s == '-', depois = false;
    if (neg) s++;
    for (; *s; s++) {
        if (*s == ',') { depois = true; continue; }
        if (!depois) inteiro = inteiro * 10 + (*s - '0');
        else if (peso) { frac += (*s - '0') * peso; peso = div_s64(peso, 10); }
    }
    fixo_t v = inteiro * ESCALA + frac;
    return neg ? -v : v;
}

/* 35000 -> "3,5"; sem zeros sobrando no fim. */
static void escrever_numero(fixo_t v, char *s, size_t max)
{
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)-v : (unsigned long long)v, frac;
    unsigned long long inteiro = div_u64(u, ESCALA, &frac);
    char dig[24];
    int n = 0;
    do { unsigned long long r; inteiro = div_u64(inteiro, 10, &r); dig[n++] = (char)('0' + r); } while (inteiro);
    size_t k = 0;
    if (neg && (u != 0)) s[k++] = '-';
    while (n && k < max - 1) s[k++] = dig[--n];
    if (frac) {
        char f[5] = { 0 };
        unsigned long long p = ESCALA / 10;
        for (int i = 0; i < 4; i++) { f[i] = (char)('0' + div_u64(frac, p, &frac)); p = div_u64(p, 10, NULL); }
        int ultimo = 3;
        while (ultimo >= 0 && f[ultimo] == '0') ultimo--;
        if (k < max - 1) s[k++] = ',';
        for (int i = 0; i <= ultimo && k < max - 1; i++) s[k++] = f[i];
    }
    s[k] = 0;
}

/* ---- tela ---- */

/* Texto ampliado (escala 2) para o visor. */
extern const unsigned char fonte_sagui[256][16];
static void texto_grande(int x, int y, const char *t, uint32_t cor)
{
    for (; *t; t++, x += 16) {
        unsigned c = (uint8_t)*t;
        if (!letra_suave(c)) {
            const unsigned char *g = fonte_sagui[c];
            for (int lin = 0; lin < 16; lin++)
                for (int col = 0; col < 8; col++)
                    if (g[lin] & (0x80 >> col)) g_retangulo(j, x + col * 2, y + lin * 2, 2, 2, cor);
            continue;
        }
        /* fonte suave em dobro: cada tom cobre 2 x 2 pixels */
        const unsigned char *g = fonte_suave[c];
        for (int lin = 0; lin < 32; lin++) {
            int yy = y + lin;
            if ((unsigned)yy >= (unsigned)j->alt) continue;
            for (int col = 0; col < 16; col++) {
                int tt = tom_letra(g, col >> 1, lin >> 1), xx = x + col;
                if (!tt || (unsigned)xx >= (unsigned)j->larg) continue;
                uint32_t *p = &j->px[yy * j->larg + xx];
                *p = tt == 15 ? cor : no_tom(*p, cor, tt);
            }
        }
    }
}

static void desenhar_visor(void)
{
    g_retangulo(j, 10, 10, LARG - 20, 70, 0xDDE8D8);
    g_moldura(j, 10, 10, LARG - 20, 70, 0x5A6470);
    char texto[48], op[4] = { 0 };
    if (erro[0]) {
        g_texto(j, 18, 18, "Aviso:", 0xB02020);
        g_texto(j, 18, 40, erro, 0xB02020);
    } else {
        if (digitando) copia_texto(texto, entrada[0] ? entrada : "0", sizeof texto);
        else escrever_numero(tem_acumulado ? acumulado : 0, texto, sizeof texto);
        int larg = (int)tam_texto(texto) * 16;
        if (larg > LARG - 40) { g_texto(j, LARG - 20 - (int)tam_texto(texto) * 8 - 8, 46, texto, COR_TEXTO); }
        else texto_grande(LARG - 20 - larg - 8, 38, texto, COR_TEXTO);
        if (operacao) { op[0] = operacao == '*' ? (char)0xD7 : operacao == '/' ? (char)0xF7 : operacao; g_texto(j, 18, 16, op, COR_TEXTO_FRACO); }
    }
    jan_mostrar_area(j, 10, 10, LARG - 20, 70);
}

static void registrar_visor(void)
{
    char texto[48];
    if (erro[0]) copia_texto(texto, erro, sizeof texto);
    else if (digitando) copia_texto(texto, entrada, sizeof texto);
    else escrever_numero(acumulado, texto, sizeof texto);
    registrar("calculadora: visor %s", texto);
}

/* ---- lógica ---- */

static void limpar(void)
{
    entrada[0] = 0;
    acumulado = 0;
    operacao = 0;
    tem_acumulado = digitando = false;
    erro[0] = 0;
}

static void digito(char c)
{
    if (erro[0]) limpar();
    if (!digitando) { entrada[0] = 0; digitando = true; }
    size_t n = tam_texto(entrada);
    char *virgula = acha_char(entrada, ',');
    if (c == ',') {
        if (virgula) return;
        if (n == 0) { copia_texto(entrada, "0", sizeof entrada); n = 1; }
    } else {
        int inteiros = virgula ? (int)(virgula - entrada) : (int)n;
        if (virgula && (int)(n - (size_t)(virgula - entrada)) > 4) return;   /* 4 casas */
        if (!virgula && inteiros >= 12) return;                               /* 12 dígitos */
        if (n == 1 && entrada[0] == '0' && c != ',') n = 0;                   /* sem zero à esquerda */
    }
    if (n < sizeof entrada - 1) { entrada[n] = c; entrada[n + 1] = 0; }
}

static void apagar_ultimo(void)
{
    if (!digitando) return;
    size_t n = tam_texto(entrada);
    if (n) entrada[n - 1] = 0;
}

static void aplicar(void)
{
    if (!digitando) return;
    fixo_t b = ler_numero(entrada);
    fixo_t r;
    if (!tem_acumulado || !operacao) r = b;
    else if (!calcular(acumulado, operacao, b, &r)) { digitando = false; return; }
    acumulado = r;
    tem_acumulado = true;
    digitando = false;
}

static void operador(char op)
{
    if (erro[0]) return;
    aplicar();
    if (erro[0]) return;
    if (!tem_acumulado) { acumulado = 0; tem_acumulado = true; }
    operacao = op;
}

static void igual(void)
{
    if (erro[0]) return;
    aplicar();
    operacao = 0;
    registrar_visor();
}

/* ---- botões ---- */

typedef struct { botao_t b; char acao; } tecla_t;
static tecla_t teclas[20];
static int qtd_teclas;

static void nova_tecla(int col, int lin, int largura, int altura, const char *texto, char acao)
{
    int l = 62, a = 50, x0 = 10, y0 = 92, esp = 8;
    teclas[qtd_teclas++] = (tecla_t){ { x0 + col * (l + esp), y0 + lin * (a + esp),
                                        l * largura + esp * (largura - 1), a * altura + esp * (altura - 1),
                                        texto, false }, acao };
}

static void executar_acao(char a)
{
    if (a >= '0' && a <= '9') digito(a);
    else if (a == ',') digito(',');
    else if (a == '+' || a == '-' || a == '*' || a == '/') operador(a);
    else if (a == '=') igual();
    else if (a == 'C') limpar();
    else if (a == 'B') apagar_ultimo();
    desenhar_visor();
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    j = jan_abrir("Calculadora", LARG, ALT);
    if (!j) return 1;
    static const char *const nomes_num[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    nova_tecla(0, 0, 1, 1, "C", 'C');  nova_tecla(1, 0, 1, 1, "<-", 'B');
    nova_tecla(2, 0, 1, 1, "\xF7", '/'); nova_tecla(3, 0, 1, 1, "\xD7", '*');
    for (int i = 0; i < 9; i++)
        nova_tecla(i % 3, 3 - i / 3, 1, 1, nomes_num[i + 1], (char)('1' + i));
    nova_tecla(3, 1, 1, 1, "-", '-');  nova_tecla(3, 2, 1, 1, "+", '+');
    nova_tecla(3, 3, 1, 2, "=", '=');
    nova_tecla(0, 4, 2, 1, "0", '0');  nova_tecla(2, 4, 1, 1, ",", ',');
    for (int i = 0; i < qtd_teclas; i++) botao_desenhar(j, &teclas[i].b);
    limpar();
    desenhar_visor();
    jan_mostrar(j);
    registrar("calculadora: pronta");

    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        for (int i = 0; i < qtd_teclas; i++)
            if (botao_tratar(j, &teclas[i].b, &e)) executar_acao(teclas[i].acao);
        if (e.tipo != EV_TECLA) continue;
        unsigned t = e.tecla;
        char a = 0;
        if (t >= '0' && t <= '9') a = (char)t;
        else if (t == ',' || t == '.') a = ',';
        else if (t == '+' || t == '-' || t == '*' || t == '/') a = (char)t;
        else if (t == '\n' || t == '=') a = '=';
        else if (t == '\b') a = 'B';
        else if (t == TECLA_ESC || t == 'c' || t == 'C') a = 'C';
        if (a) executar_acao(a);
    }
    jan_fechar(j);
    return 0;
}
