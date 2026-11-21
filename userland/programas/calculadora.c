/* calculadora - a Calculadora do SaguiOS (H26).
 *
 * Números em ponto fixo: um inteiro de 64 bits que guarda o valor vezes
 * 10.000 (4 casas decimais). Nada de ponto flutuante: o SaguiOS não salva o
 * estado da FPU entre processos (PRD 4). Num x86 de 32 bits, dividir dois
 * inteiros de 64 bits exigiria uma rotina da biblioteca do compilador, que
 * não usamos; a divisão longa abaixo é feita bit a bit, como no papel.
 *
 * Botões clicáveis e teclado: dígitos, vírgula, + - × ÷, =, C (limpar) e
 * ← (apagar o último dígito). Avisa divisão por zero e número grande demais.
 *
 * Visual: fundo claro, visor sem moldura (a conta em andamento em cima, em
 * cinza, e o número grande à direita), teclas de cantos redondos: os
 * algarismos brancos, as operações cinza e o "=" verde. A tecla fica mais
 * escura enquanto está apertada.
 *
 * Copiar e colar: o botão direito no visor abre Copiar, Colar, Recortar
 * (copia e zera) e Limpar; Ctrl+C e Ctrl+V fazem o mesmo pelo teclado. O
 * número colado aceita vírgula ou ponto, sinal e espaços.
 *
 * Seleção no visor: arrastar sobre o número marca um pedaço dele (dois
 * cliques ou Ctrl+A, ele todo). Com algo marcado, Copiar e Recortar levam
 * só o pedaço, Backspace e Delete o apagam, um algarismo ou Colar entram
 * no lugar dele. Mexer num resultado o transforma no número digitado. */
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

#define C_FUNDO     0xF3F5F7
#define C_FILETE    0xDDE1E5
#define C_NUM       0xFFFFFF
#define C_NUM_APER  0xE4E7EA
#define C_OP        0xE9ECEF
#define C_OP_APER   0xD5DAE0
#define C_IGUAL     0x0B7A3E
#define C_IGUAL_APER 0x086332
#define VISOR_A     74
#define VISOR_FIM   (10 + VISOR_A + 6)

/* Seleção no visor: de sel_a a sel_b (posições entre as letras). */
static int sel_a = -1, sel_b = -1;
static bool marcando;
static int vis_x0, vis_cw = 16, vis_n;    /* onde o número foi desenhado */
static bool tem_sel(void) { return sel_a >= 0 && sel_a != sel_b; }
static int sel_ini(void) { return sel_a < sel_b ? sel_a : sel_b; }
static int sel_fim(void) { return sel_a < sel_b ? sel_b : sel_a; }
static void sem_sel(void) { sel_a = sel_b = -1; }
static void apagar_sel(void);

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
        const unsigned char *g = fonte_sagui[(uint8_t)*t];
        for (int lin = 0; lin < 16; lin++)
            for (int col = 0; col < 8; col++)
                if (g[lin] & (0x80 >> col)) g_retangulo(j, x + col * 2, y + lin * 2, 2, 2, cor);
    }
}

/* O número do visor como texto (o que Copiar leva). */
static void texto_do_visor(char *texto, size_t max)
{
    if (digitando) copia_texto(texto, entrada[0] ? entrada : "0", max);
    else escrever_numero(tem_acumulado ? acumulado : 0, texto, max);
}

static void desenhar_visor(void)
{
    g_retangulo(j, 0, 0, LARG, 10 + VISOR_A + 6, C_FUNDO);
    char texto[48];
    if (erro[0]) {
        g_texto(j, LARG - 18 - g_largura_texto(erro), 50, erro, 0xC0392B);
    } else {
        /* em cima, em cinza: a conta em andamento ("12,5 ×") */
        if (operacao && tem_acumulado) {
            char conta[48], n[32];
            escrever_numero(acumulado, n, sizeof n);
            formatar_texto(conta, sizeof conta, "%s %c", n, operacao == '*' ? (char)0xD7 : operacao == '/' ? (char)0xF7 : operacao);
            g_texto(j, LARG - 18 - g_largura_texto(conta), 16, conta, COR_TEXTO_FRACO);
        }
        texto_do_visor(texto, sizeof texto);
        vis_n = (int)tam_texto(texto);
        bool grande = vis_n * 16 <= LARG - 36;
        vis_cw = grande ? 16 : LETRA_L;
        vis_x0 = LARG - 18 - vis_n * vis_cw;
        int ty = grande ? 40 : 52, ta = grande ? 32 : LETRA_A;
        if (tem_sel() && sel_fim() <= vis_n)   /* o pedaço marcado: fundo azul */
            g_ret_redondo(j, vis_x0 + sel_ini() * vis_cw - 1, ty - 2, (sel_fim() - sel_ini()) * vis_cw + 2, ta + 4, 3, COR_SELECAO, COR_SELECAO);
        for (int i = 0; i < vis_n; i++) {
            char c[2] = { texto[i], 0 };
            uint32_t cor = tem_sel() && i >= sel_ini() && i < sel_fim() ? COR_BRANCO : COR_TEXTO;
            if (grande) texto_grande(vis_x0 + i * 16, ty, c, cor);
            else g_texto(j, vis_x0 + i * LETRA_L, ty, c, cor);
        }
    }
    jan_mostrar_area(j, 0, 0, LARG, 10 + VISOR_A + 6);
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
static int apertada = -1;                 /* a tecla sob o botão do mouse apertado */

static void nova_tecla(int col, int lin, int largura, int altura, const char *texto, char acao)
{
    int l = 62, a = 50, x0 = 10, y0 = 92, esp = 8;
    teclas[qtd_teclas++] = (tecla_t){ { x0 + col * (l + esp), y0 + lin * (a + esp),
                                        l * largura + esp * (largura - 1), a * altura + esp * (altura - 1),
                                        texto, false }, acao };
}

static void desenhar_tecla(int i)
{
    tecla_t *t = &teclas[i];
    botao_t *b = &t->b;
    bool num = (t->acao >= '0' && t->acao <= '9') || t->acao == ',', ig = t->acao == '=';
    uint32_t cor = ig ? (b->apertado ? C_IGUAL_APER : C_IGUAL) : num ? (b->apertado ? C_NUM_APER : C_NUM) : (b->apertado ? C_OP_APER : C_OP);
    uint32_t letra = ig ? COR_BRANCO : COR_TEXTO;
    g_retangulo(j, b->x - 1, b->y - 1, b->l + 2, b->a + 2, C_FUNDO);
    if (!ig) g_ret_redondo(j, b->x, b->y, b->l, b->a, 8, C_FILETE, C_FILETE);
    g_ret_redondo(j, b->x + (ig ? 0 : 1), b->y + (ig ? 0 : 1), b->l - (ig ? 0 : 2), b->a - (ig ? 0 : 2), ig ? 8 : 7, cor, cor);
    int cx = b->x + b->l / 2, cy = b->y + b->a / 2;
    if (t->acao == 'B') {                  /* apagar: uma seta para a esquerda */
        g_retangulo(j, cx - 10, cy - 1, 21, 3, letra);
        for (int k = 0; k < 8; k++) { g_retangulo(j, cx - 10 + k, cy - k, 2, 1, letra); g_retangulo(j, cx - 10 + k, cy + 1 + k, 2, 1, letra); }
    } else {                               /* o rótulo na fonte em dobro, como o visor */
        int w = (int)tam_texto(b->texto) * 16;
        texto_grande(cx - w / 2, cy - 16, b->texto, letra);
    }
    jan_mostrar_area(j, b->x - 1, b->y - 1, b->l + 2, b->a + 2);
}

static int tecla_em(int x, int y)
{
    for (int i = 0; i < qtd_teclas; i++) {
        botao_t *b = &teclas[i].b;
        if (x >= b->x && x < b->x + b->l && y >= b->y && y < b->y + b->a) return i;
    }
    return -1;
}

static void executar_acao(char a)
{
    if (tem_sel() && ((a >= '0' && a <= '9') || a == ',' || a == 'B')) {   /* no lugar do marcado */
        apagar_sel();
        if (a != 'B') digito(a);
        desenhar_visor();
        registrar_visor();
        return;
    }
    sem_sel();
    if (a >= '0' && a <= '9') digito(a);
    else if (a == ',') digito(',');
    else if (a == '+' || a == '-' || a == '*' || a == '/') operador(a);
    else if (a == '=') igual();
    else if (a == 'C') limpar();
    else if (a == 'B') apagar_ultimo();
    desenhar_visor();
}

/* ---- seleção e copiar e colar ---- */

/* Posição entre letras mais perto do x (0 = antes da primeira). */
static int posicao_em(int x)
{
    int p = (x - vis_x0 + vis_cw / 2) / vis_cw;
    return p < 0 ? 0 : p > vis_n ? vis_n : p;
}

/* Um resultado vira o número digitado, para poder ser mexido. */
static void garantir_entrada(void)
{
    if (digitando) return;
    texto_do_visor(entrada, sizeof entrada);
    digitando = true;
}

static void apagar_sel(void)
{
    if (!tem_sel()) return;
    garantir_entrada();
    int a = sel_ini(), b = sel_fim(), n = (int)tam_texto(entrada);
    if (b > n) b = n;
    for (int i = a; i <= n - (b - a); i++) entrada[i] = entrada[i + (b - a)];
    if (!entrada[0] || compara_texto(entrada, "-") == 0) copia_texto(entrada, "0", sizeof entrada);
    sem_sel();
}

static void copiar(void)
{
    if (erro[0]) return;
    char t[48];
    texto_do_visor(t, sizeof t);
    if (tem_sel()) {                       /* só o pedaço marcado */
        int a = sel_ini(), b = sel_fim() < (int)tam_texto(t) ? sel_fim() : (int)tam_texto(t);
        for (int i = 0; i < b - a; i++) t[i] = t[a + i];
        t[b - a] = 0;
    }
    area_copia_gravar(t, (unsigned)tam_texto(t));
    registrar("calculadora: copiado %s", t);
}

/* Lê um número de t: aceita vírgula ou ponto, sinal na frente e espaços
 * (ignorados, como em "1 234,5"). Devolve false se não for um número. */
static bool ler_colado(const char *t, char *n)
{
    int k = 0, dig = 0, dec = -1;
    bool ok = true;
    for (int i = 0; t[i] && ok; i++) {
        char c = t[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '-' && k == 0) { n[k++] = '-'; continue; }
        if ((c == ',' || c == '.') && dec < 0) { if (!dig) n[k++] = '0'; n[k++] = ','; dec = 0; continue; }
        if (c >= '0' && c <= '9') {
            if (dec >= 0 ? dec >= 4 : dig >= 12) { ok = false; break; }
            n[k++] = c; dig++;
            if (dec >= 0) dec++;
            continue;
        }
        ok = false;
    }
    n[k] = 0;
    return ok && dig;
}

/* Cola: com um pedaço marcado, o colado entra no lugar dele. Outra coisa
 * que não um número avisa e não muda nada. */
static void colar(void)
{
    char t[64], junto[128], n[32];
    int tam = area_copia_ler(t, sizeof t - 1);
    if (tam <= 0) return;
    if (tam > (int)sizeof t - 1) tam = (int)sizeof t - 1;
    t[tam] = 0;
    if (tem_sel() && !erro[0]) {
        char v[48];
        texto_do_visor(v, sizeof v);
        int a = sel_ini(), b = sel_fim();
        formatar_texto(junto, sizeof junto, "%s", "");
        int k = 0;
        for (int i = 0; i < a && v[i]; i++) junto[k++] = v[i];
        for (int i = 0; t[i] && k < 120; i++) junto[k++] = t[i];
        for (int i = b; v[i] && k < 126; i++) junto[k++] = v[i];
        junto[k] = 0;
    } else copia_texto(junto, t, sizeof junto);
    if (!ler_colado(junto, n)) {
        copia_texto(erro, "O que foi colado não é um número", sizeof erro);
        registrar("calculadora: colar recusado");
        digitando = false;
        sem_sel();
        return;
    }
    erro[0] = 0;
    copia_texto(entrada, n, sizeof entrada);
    digitando = true;
    sem_sel();
    registrar("calculadora: colado %s", entrada);
}

static void acao_menu(unsigned i)
{
    if (i == 0) copiar();
    else if (i == 1) colar();
    else if (i == 2) {
        copiar();
        if (erro[0]) {}
        else if (tem_sel()) apagar_sel();
        else { if (!digitando) limpar(); copia_texto(entrada, "0", sizeof entrada); digitando = true; }
    }
    else if (i == 3) { limpar(); sem_sel(); }
    desenhar_visor();
    registrar_visor();
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
    g_retangulo(j, 0, 0, LARG, ALT, C_FUNDO);
    for (int i = 0; i < qtd_teclas; i++) desenhar_tecla(i);
    limpar();
    desenhar_visor();
    jan_mostrar(j);
    registrar("calculadora: pronta");

    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_MENU) { acao_menu(e.tecla); continue; }
        if (e.tipo == EV_CLIQUE && (e.botoes & 2) && e.y < 10 + VISOR_A + 6) {   /* botão direito no visor */
            jan_menu(j, e.x, e.y, "Copiar (Ctrl+C)|Colar (Ctrl+V)|Recortar (Ctrl+X)|Limpar (Esc)");
            continue;
        }
        if (e.tipo == EV_CLIQUE && (e.botoes & 1) && e.y < VISOR_FIM && !erro[0]) {   /* marcar no visor */
            if (e.cliques == 2) { sel_a = 0; sel_b = vis_n; }
            else { sel_a = sel_b = posicao_em(e.x); marcando = true; }
            desenhar_visor();
            continue;
        }
        if (e.tipo == EV_MOVER && marcando && (e.botoes & 1)) {
            int p = posicao_em(e.x);
            if (p != sel_b) { sel_b = p; desenhar_visor(); }
            continue;
        }
        if (e.tipo == EV_SOLTOU && marcando) {
            marcando = false;
            if (tem_sel()) registrar("calculadora: selecao %d a %d", sel_ini(), sel_fim());
            continue;
        }
        if (e.tipo == EV_CLIQUE && (e.botoes & 1)) {
            apertada = tecla_em(e.x, e.y);
            if (apertada >= 0) { teclas[apertada].b.apertado = true; desenhar_tecla(apertada); }
            continue;
        }
        if (e.tipo == EV_SOLTOU) {
            if (apertada >= 0) {
                int i = apertada;
                apertada = -1;
                teclas[i].b.apertado = false;
                desenhar_tecla(i);
                if (tecla_em(e.x, e.y) == i) executar_acao(teclas[i].acao);
            }
            continue;
        }
        if (e.tipo != EV_TECLA) continue;
        if (e.tecla == 3) { acao_menu(0); continue; }        /* Ctrl+C */
        if (e.tecla == 22) { acao_menu(1); continue; }       /* Ctrl+V */
        if (e.tecla == 24) { acao_menu(2); continue; }       /* Ctrl+X */
        if (e.tecla == 1 && !erro[0]) { sel_a = 0; sel_b = vis_n; desenhar_visor(); continue; }   /* Ctrl+A */
        if (e.tecla == TECLA_DELETE && tem_sel()) { apagar_sel(); desenhar_visor(); registrar_visor(); continue; }
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
