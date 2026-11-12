/* terminal - o Terminal em janela (H24).
 *
 * Abre uma janela e roda o sagsh dentro dela, ligado por dois pipes:
 *   teclas da janela  -> pipe de entrada -> entrada 0 do sagsh
 *   saída 1 e 2 do sagsh -> pipe de saída -> desenhado na janela
 * As teclas vão como o terminal do kernel entrega no modo bruto (caracteres
 * Latin-1; setas e afins como ESC + letra), então o sagsh funciona igual,
 * com histórico, Tab e edição. Na saída, o Terminal entende as mesmas
 * sequências do terminal do kernel: ESC [ D / C / K / J.
 *
 * Tudo o que o shell escreve também vai para a saída do próprio Terminal
 * (que, com a área de trabalho no ar, é a serial): é o que os testes leem. */
#include "sagui.h"
#include "gui.h"

#define COLS_INICIO 94
#define LINS_INICIO 28
#define COLS_MAX 160   /* a grade cabe na maior tela; cols e lins dizem o que aparece */
#define LINS_MAX 80
#define MARGEM 4
#define COR_FUNDO_T 0x12161E
#define COR_LETRA   0xD8DDE2

static jan_t *j;
static char grade[LINS_MAX][COLS_MAX];
static int cols = COLS_INICIO, lins = LINS_INICIO;
static int col, lin, estado_esc;
static int ent_escrita;   /* ponta de escrita do pipe de entrada do shell */
static int suj_l0 = LINS_MAX, suj_l1 = -1;   /* linhas a redesenhar */

static void sujar(int l) { if (l < suj_l0) suj_l0 = l; if (l > suj_l1) suj_l1 = l; }

static void desenhar_linha(int l)
{
    int y = MARGEM + l * LETRA_A;
    g_retangulo(j, 0, y, j->larg, LETRA_A, COR_FUNDO_T);
    g_texto_n(j, MARGEM, y, grade[l], cols, COR_LETRA);
    if (l == lin && col < cols) {   /* cursor: bloco com a letra em cor invertida */
        g_retangulo(j, MARGEM + col * LETRA_L, y, LETRA_L, LETRA_A, COR_LETRA);
        char c = grade[l][col];
        g_texto_n(j, MARGEM + col * LETRA_L, y, &c, 1, COR_FUNDO_T);
    }
}

static void mostrar(void)
{
    if (suj_l1 < 0) return;
    for (int l = suj_l0; l <= suj_l1; l++) desenhar_linha(l);
    jan_mostrar_area(j, 0, MARGEM + suj_l0 * LETRA_A, j->larg, (suj_l1 - suj_l0 + 1) * LETRA_A);
    suj_l0 = LINS_MAX; suj_l1 = -1;
}

static void limpar(void)
{
    memset(grade, ' ', sizeof grade);
    col = lin = 0;
    for (int l = 0; l < lins; l++) sujar(l);
}

static void rolar(void)
{
    memmove(grade[0], grade[1], sizeof grade - COLS_MAX);
    memset(grade[lins - 1], ' ', COLS_MAX);
    lin = lins - 1;
    for (int l = 0; l < lins; l++) sujar(l);
}

/* A janela mudou de tamanho: mantém o fim do texto à vista. */
static void redimensionar(const evento_t *e)
{
    jan_tamanho(j, e);
    int novas_l = (j->alt - 2 * MARGEM) / LETRA_A, novas_c = (j->larg - 2 * MARGEM) / LETRA_L;
    if (novas_l > LINS_MAX) novas_l = LINS_MAX;
    if (novas_c > COLS_MAX) novas_c = COLS_MAX;
    while (lin >= novas_l) {
        memmove(grade[0], grade[1], sizeof grade - COLS_MAX);
        memset(grade[LINS_MAX - 1], ' ', COLS_MAX);
        lin--;
    }
    lins = novas_l;
    cols = novas_c;
    if (col >= cols) col = cols - 1;
    g_retangulo(j, 0, 0, j->larg, j->alt, COR_FUNDO_T);
    jan_mostrar(j);
    for (int l = 0; l < lins; l++) sujar(l);
}

static void receber(char c)
{
    sujar(lin);
    if (estado_esc == 1) { estado_esc = c == '[' ? 2 : 0; return; }
    if (estado_esc == 2) {
        estado_esc = 0;
        if (c == 'D' && col > 0) col--;
        else if (c == 'C' && col < cols - 1) col++;
        else if (c == 'K') memset(&grade[lin][col], ' ', (size_t)(COLS_MAX - col));
        else if (c == 'J') limpar();
        return;
    }
    if (c == 27) { estado_esc = 1; return; }
    if (c == '\n') { col = 0; lin++; }
    else if (c == '\r') col = 0;
    else if (c == '\t') col = (col + 8) & ~7;
    else if (c == '\b') { if (col > 0) grade[lin][--col] = ' '; }
    else { grade[lin][col++] = c; }
    if (col >= cols) { col = 0; lin++; }
    if (lin >= lins) rolar();
    sujar(lin);
}

/* Tecla da janela -> bytes para o shell, no formato do modo bruto. */
static void enviar_tecla(unsigned t)
{
    char b[2];
    int n = 1;
    if (t < 0x100) b[0] = (char)t;
    else {
        b[0] = 27;
        switch (t) {
        case TECLA_CIMA: b[1] = TECLA_B_CIMA; break;
        case TECLA_BAIXO: b[1] = TECLA_B_BAIXO; break;
        case TECLA_ESQUERDA: b[1] = TECLA_B_ESQUERDA; break;
        case TECLA_DIREITA: b[1] = TECLA_B_DIREITA; break;
        case TECLA_HOME: b[1] = TECLA_B_HOME; break;
        case TECLA_END: b[1] = TECLA_B_END; break;
        case TECLA_DELETE: b[1] = TECLA_B_DELETE; break;
        case TECLA_PGUP: b[1] = TECLA_B_PGUP; break;
        case TECLA_PGDN: b[1] = TECLA_B_PGDN; break;
        case TECLA_ESC: b[1] = 27; break;
        default: return;
        }
        n = 2;
    }
    escrever_tudo(ent_escrita, b, (unsigned)n);
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    j = jan_abrir_redimensionavel("Terminal", COLS_INICIO * LETRA_L + 2 * MARGEM, LINS_INICIO * LETRA_A + 2 * MARGEM);
    if (!j) return 1;
    g_retangulo(j, 0, 0, j->larg, j->alt, COR_FUNDO_T);   /* margens também escuras */
    jan_mostrar(j);
    limpar();
    mostrar();

    int entrada[2], saida[2];
    if (criar_pipe(entrada) < 0 || criar_pipe(saida) < 0) return 1;
    /* O shell nasce com o pipe de entrada como 0 e o de saída como 1 e 2. */
    int g0 = duplicar(ENTRADA, 13), g1 = duplicar(SAIDA, 14), g2 = duplicar(ERROS, 15);
    duplicar(entrada[0], ENTRADA);
    duplicar(saida[1], SAIDA);
    duplicar(saida[1], ERROS);
    char *args[] = { "sagsh", NULL };
    int shell = executar("/bin/sagsh", args);
    duplicar(g0, ENTRADA); duplicar(g1, SAIDA); duplicar(g2, ERROS);
    fechar(g0); fechar(g1); fechar(g2);
    fechar(entrada[0]);
    fechar(saida[1]);
    ent_escrita = entrada[1];
    if (shell < 0) { imprimir("terminal: não consegui abrir o sagsh (%s)\n", nome_erro(shell)); return 1; }

    char buf[512];
    evento_t e;
    for (;;) {
        bool fez = false;
        int d = disponivel(saida[0]);
        if (d == ERRO_PIPE_QUEBRADO) break;              /* o shell terminou (sair) */
        if (d > 0) {
            int n = ler(saida[0], buf, sizeof buf);
            for (int i = 0; i < n; i++) receber(buf[i]);
            if (n > 0) escrever(SAIDA, buf, (unsigned)n);  /* cópia para a serial */
            fez = true;
        }
        while (proximo_evento(j->id, &e, false) == 1) {
            fez = true;
            if (e.tipo == EV_FECHAR) { matar(shell); goto fim; }
            if (e.tipo == EV_TECLA) enviar_tecla(e.tecla);
            if (e.tipo == EV_TAMANHO) redimensionar(&e);
        }
        mostrar();
        /* O shell terminou? (Não dá para depender só do fim do pipe: um
         * programa gráfico aberto pelo shell herda a saída e a mantém aberta.) */
        int c;
        if (recolher(&c) == shell) { shell = -1; break; }
        if (!fez) dormir(10);
    }
fim:
    if (shell > 0) {
        int c;
        esperar(shell, &c);
    }
    jan_fechar(j);
    return 0;
}
