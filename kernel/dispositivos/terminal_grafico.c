/* terminal_grafico.c - o terminal desenhado no modo gráfico com a fonte própria (H18).
 *
 * A tela vira uma grade de células de 8x16 pixels (128x48 em 1024x768).
 * Cada célula guarda o caractere e as cores, para que o cursor possa ser
 * desenhado e apagado sem perder o que estava embaixo. Rolar a tela é mover
 * os pixels uma linha de texto para cima no buffer de trás.
 *
 * Além dos caracteres comuns, '\n', '\r', '\t' e '\b' (volta e apaga), o
 * terminal entende três sequências próprias, usadas pelo shell para editar
 * no meio da linha (setas para os lados):
 *   ESC [ D   cursor uma posição à esquerda (sem apagar)
 *   ESC [ C   cursor uma posição à direita
 *   ESC [ K   apaga do cursor até o fim da linha */
#include "console.h"
#include "texto.h"
#include "memoria.h"
#include "video.h"

extern const unsigned char fonte_sagui[256][16];

#define CEL_L 8
#define CEL_A 16

/* As 16 cores do console (mesmos índices do modo texto), em tons próprios. */
static const cor_t paleta[16] = {
    RGB(18, 22, 30),    RGB(40, 80, 200),   RGB(40, 160, 70),   RGB(40, 160, 170),
    RGB(200, 45, 45),   RGB(160, 60, 170),  RGB(180, 120, 30),  RGB(205, 210, 215),
    RGB(110, 115, 125), RGB(100, 150, 255), RGB(110, 230, 120), RGB(110, 230, 230),
    RGB(255, 110, 110), RGB(240, 120, 240), RGB(255, 220, 60),  RGB(255, 255, 255),
};

typedef struct { uint8_t c, cor; } celula_t;

static celula_t *grade;
static int colunas, linhas, col, lin;
static uint8_t cor_atual = (COR_PRETO << 4) | COR_CINZA;
static int estado_esc;   /* 0 normal, 1 viu ESC, 2 viu ESC [ */

static void desenhar_celula(int x, int y, bool cursor)
{
    celula_t *k = &grade[y * colunas + x];
    cor_t frente = paleta[k->cor & 15], fundo = paleta[k->cor >> 4];
    if (cursor) { cor_t t = frente; frente = fundo; fundo = t; }   /* cursor = cores trocadas */
    uint32_t *buf = video_buffer();
    uint32_t largura = video_largura();
    const unsigned char *g = fonte_sagui[k->c];
    for (int j = 0; j < CEL_A; j++) {
        uint32_t *p = buf + (uint32_t)(y * CEL_A + j) * largura + (uint32_t)(x * CEL_L);
        unsigned char bits = g[j];
        for (int i = 0; i < CEL_L; i++)
            p[i] = (bits & (0x80 >> i)) ? frente : fundo;
    }
    video_marcar(x * CEL_L, y * CEL_A, CEL_L, CEL_A);
}

static void mostrar_cursor(bool sim)
{
    if (col < colunas && lin < linhas)
        desenhar_celula(col, lin, sim);
}

bool termgraf_iniciar(void)
{
    if (!video_iniciar()) return false;
    colunas = (int)video_largura() / CEL_L;
    linhas = (int)video_altura() / CEL_A;
    grade = kmalloc((uint32_t)(colunas * linhas) * sizeof(celula_t));
    if (!grade) return false;
    for (int i = 0; i < colunas * linhas; i++) { grade[i].c = ' '; grade[i].cor = cor_atual; }
    video_retangulo(0, 0, (int)video_largura(), (int)video_altura(), paleta[COR_PRETO]);
    col = lin = 0;
    mostrar_cursor(true);
    video_mostrar_tudo();
    return true;
}

/* A resolução mudou (H84, resolução nativa do painel): a grade passa a ter
 * o tamanho da tela nova, o texto que já estava fica onde estava, e tudo é
 * redesenhado. Sem memória, a grade antiga continua (num canto da tela). */
void termgraf_tela_mudou(void)
{
    int nc = (int)video_largura() / CEL_L, nl = (int)video_altura() / CEL_A;
    celula_t *nova = kmalloc((uint32_t)(nc * nl) * sizeof(celula_t));
    if (!nova) return;
    for (int i = 0; i < nc * nl; i++) { nova[i].c = ' '; nova[i].cor = cor_atual; }
    for (int y = 0; y < linhas && y < nl; y++)
        for (int x = 0; x < colunas && x < nc; x++)
            nova[y * nc + x] = grade[y * colunas + x];
    kfree(grade);
    grade = nova;
    colunas = nc;
    linhas = nl;
    if (col >= colunas) col = colunas - 1;
    if (lin >= linhas) lin = linhas - 1;
    video_retangulo(0, 0, (int)video_largura(), (int)video_altura(), paleta[COR_PRETO]);
    for (int y = 0; y < linhas; y++)
        for (int x = 0; x < colunas; x++)
            desenhar_celula(x, y, false);
    mostrar_cursor(true);
    video_mostrar();
}

int termgraf_colunas(void) { return colunas; }
int termgraf_coluna(void)  { return col; }

void termgraf_cor(uint8_t frente, uint8_t fundo)
{
    cor_atual = (uint8_t)((fundo << 4) | (frente & 15));
}

void termgraf_limpar(void)
{
    for (int i = 0; i < colunas * linhas; i++) { grade[i].c = ' '; grade[i].cor = cor_atual; }
    video_retangulo(0, 0, colunas * CEL_L, linhas * CEL_A, paleta[cor_atual >> 4]);
    col = lin = 0;
    mostrar_cursor(true);
}

static void rolar(void)
{
    memmove(grade, grade + colunas, (uint32_t)(colunas * (linhas - 1)) * sizeof(celula_t));
    for (int i = 0; i < colunas; i++) {
        grade[(linhas - 1) * colunas + i].c = ' ';
        grade[(linhas - 1) * colunas + i].cor = cor_atual;
    }
    video_rolar(0, linhas * CEL_A, CEL_A, paleta[cor_atual >> 4]);
    lin = linhas - 1;
}

static void por(uint8_t c)
{
    grade[lin * colunas + col].c = c;
    grade[lin * colunas + col].cor = cor_atual;
    desenhar_celula(col, lin, false);
}

void termgraf_char(char ch)
{
    uint8_t c = (uint8_t)ch;
    mostrar_cursor(false);
    if (estado_esc == 1) {
        estado_esc = c == '[' ? 2 : 0;
    } else if (estado_esc == 2) {
        estado_esc = 0;
        if (c == 'D' && col > 0) col--;
        else if (c == 'C' && col < colunas - 1) col++;
        else if (c == 'J') { termgraf_limpar(); return; }
        else if (c == 'K')
            for (int i = col; i < colunas; i++) {
                grade[lin * colunas + i].c = ' ';
                grade[lin * colunas + i].cor = cor_atual;
                desenhar_celula(i, lin, false);
            }
    } else if (c == 27) {
        estado_esc = 1;
    } else if (c == '\n') {
        col = 0;
        lin++;
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\t') {
        col = (col + 8) & ~7;
    } else if (c == '\b') {
        if (col > 0) { col--; por(' '); }
    } else {
        por(c);
        col++;
    }
    if (col >= colunas) { col = 0; lin++; }
    if (lin >= linhas) rolar();
    mostrar_cursor(true);
}

void termgraf_mostrar(void)
{
    video_mostrar();
}
