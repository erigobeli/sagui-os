/* smatrix - efeito Matrix (chuva de caracteres caindo) para o SaguiOS.
 *
 * Motor de partículas texto escrito do zero:
 *   - Cada coluna tem sua própria velocidade e posicao.
 *   - Caracteres aleatorios (alfanumericos e simbolos Latin-1).
 *   - Cabeca branca no topo de cada coluna, corpo verde, rabo escuro.
 *   - Fundo preto tipo terminal Matrix.
 *   - Contador de FPS.
 *
 * Tudo desenhado caractere por caractere com a fonte propria 8x16 do
 * SaguiOS (libsagui). Sem GPU, sem FPU, sem libc.
 *
 * Teclas:
 *   ESPACO - pausar/despausar
 *   ESC    - sair
 */

#include "sagui.h"
#include "gui.h"

extern const unsigned char fonte_sagui[256][16];

#define LARG_JAN 640
#define ALT_JAN  480
#define COLS     (LARG_JAN / LETRA_L)    /* 80 colunas */
#define ROWS     (ALT_JAN / LETRA_A)     /* 30 linhas */

/* Caracteres usados na chuva Matrix (Latin-1). */
static const char *chars =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "@#$%&*+=?<>[](){}|/\\!;:.,-~`";

#define QTD_CHARS 96

/* Estado de cada coluna. */
typedef struct {
    int    y;          /* posicao da cabeca (em linhas) */
    int    velocidade; /* linhas por quadro */
    int    tamanho;    /* tamanho da coluna (em linhas) */
    int    timer;      /* contador para proxima mudanca de caractere */
} coluna_t;

static coluna_t colunas[COLS];
static char tela[ROWS][COLS];   /* caractere em cada posicao */

/* Caracter aleatorio. */
static char char_aleatorio(void)
{
    static unsigned semente = 12345;
    semente = semente * 1103515245 + 12345;
    return chars[(semente >> 16) % QTD_CHARS];
}

/* Inicializa uma coluna. */
static void coluna_iniciar(coluna_t *c, int x)
{
    (void)x;
    c->y = -((unsigned)tempo_ms() % (ROWS * 2));   /* começa fora da tela */
    c->velocidade = 1 + ((unsigned)tempo_ms() * 7) % 4;  /* 1 a 4 linhas/frame */
    c->tamanho = 3 + ((unsigned)tempo_ms() * 3) % 12;    /* 3 a 14 linhas */
    c->timer = 0;
}

/* Converte inteiro para string decimal (sem sprintf). */
static void texto_int_aux(char *buf, unsigned max, int v)
{
    if (v == 0) { copia_texto(buf, "0", max); return; }
    char tmp[16];
    int n = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)(-v) : (unsigned)v;
    while (u && n < 15) { tmp[n++] = (char)('0' + u % 10); u /= 10; }
    if (neg) tmp[n++] = '-';
    size_t k = 0;
    while (n) { if (k < max - 1) buf[k++] = tmp[--n]; }
    buf[k] = 0;
}

/* Desenha texto na janela. */
static void desenha_texto(jan_t *j, int x, int y, const char *t, uint32_t cor)
{
    g_texto(j, x, y, t, cor);
    jan_mostrar_area(j, x, y, g_largura_texto(t), LETRA_A);
}

/* Desenha um caractere na tela (posicao em linhas/colunas). */
static void desenha_char(jan_t *j, int col, int lin, char ch, uint32_t cor)
{
    int x = col * LETRA_L;
    int y = lin * LETRA_A;
    if (x < 0 || x + LETRA_L > LARG_JAN) return;
    if (y < 0 || y + LETRA_A > ALT_JAN) return;
    const unsigned char *g = fonte_sagui[(uint8_t)ch];
    for (int lin2 = 0; lin2 < 16; lin2++) {
        for (int col2 = 0; col2 < 8; col2++) {
            if (g[lin2] & (0x80 >> col2)) {
                j->px[(y + lin2) * LARG_JAN + (x + col2)] = cor;
            }
        }
    }
}

/* Apaga um caractere (fundo preto). */
static void apaga_char(jan_t *j, int col, int lin)
{
    int x = col * LETRA_L;
    int y = lin * LETRA_A;
    if (x < 0 || x + LETRA_L > LARG_JAN) return;
    if (y < 0 || y + LETRA_A > ALT_JAN) return;
    g_retangulo(j, x, y, LETRA_L, LETRA_A, 0x000000);
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    jan_t *j = jan_abrir("Sagui Matrix", LARG_JAN, ALT_JAN);
    if (!j) return 1;

    /* Fundo preto. */
    g_retangulo(j, 0, 0, LARG_JAN, ALT_JAN, 0x000000);
    jan_mostrar(j);

    /* Inicializa colunas e tela. */
    for (int c = 0; c < COLS; c++) {
        coluna_iniciar(&colunas[c], c);
        for (int r = 0; r < ROWS; r++) tela[r][c] = ' ';
    }

    bool rodando = true;
    bool pausado = false;
    unsigned frame = 0;
    unsigned ultimo_fps = tempo_ms();
    unsigned fps = 0;

    while (rodando) {
        unsigned inicio_frame = tempo_ms();

        if (!pausado) {
            /* Atualiza colunas. */
            for (int c = 0; c < COLS; c++) {
                coluna_t *col = &colunas[c];

                /* Apaga o rabo da coluna (ultima posicao visível anterior). */
                int rabo_y = col->y - col->tamanho;
                if (rabo_y >= 0 && rabo_y < ROWS) {
                    apaga_char(j, c, rabo_y);
                    tela[rabo_y][c] = ' ';
                }

                /* Move a cabeca. */
                col->y += col->velocidade;

                /* Desenha a cabeca (branco). */
                if (col->y >= 0 && col->y < ROWS) {
                    desenha_char(j, c, col->y, char_aleatorio(), 0xFFFFFF);
                    tela[col->y][c] = char_aleatorio();
                }

                /* Desenha o corpo (verde) nos anteriores. */
                for (int k = 1; k < col->tamanho; k++) {
                    int yy = col->y - k;
                    if (yy >= 0 && yy < ROWS) {
                        uint32_t cor = (k < col->tamanho / 2) ? 0x00FF00 : 0x004400;
                        desenha_char(j, c, yy, tela[yy][c] ? tela[yy][c] : char_aleatorio(), cor);
                    }
                }

                /* Reinicia coluna que saiu da tela. */
                if (col->y - col->tamanho > ROWS) {
                    coluna_iniciar(col, c);
                }
            }
        }

        /* Atualiza a janela. */
        jan_mostrar(j);

        /* FPS. */
        frame++;
        unsigned agora = tempo_ms();
        if (agora - ultimo_fps >= 1000) {
            fps = frame;
            frame = 0;
            ultimo_fps = agora;
        }

        /* HUD. */
        char buf[16];
        desenha_texto(j, 16, 16, "Sagui Matrix", 0x00FF00);
        texto_int_aux(buf, sizeof buf, fps);
        desenha_texto(j, 16, 36, "FPS: ", 0x00FF00);
        desenha_texto(j, 60, 36, buf, 0xFFFFFF);
        desenha_texto(j, 16, ALT_JAN - 24, "ESPACO: pausar   ESC: sair", 0x008800);

        /* Eventos. */
        evento_t e;
        int r = proximo_evento(j->id, &e, false);
        if (r == 1) {
            if (e.tipo == EV_FECHAR) rodando = false;
            else if (e.tipo == EV_TECLA) {
                if (e.tecla == TECLA_ESC) rodando = false;
                else if (e.tecla == ' ' || e.tecla == 0x20) pausado = !pausado;
            }
        }

        /* Limita a ~60 FPS. */
        unsigned passou = tempo_ms() - inicio_frame;
        if (passou < 16) dormir(16 - passou);
    }

    jan_fechar(j);
    return 0;
}
