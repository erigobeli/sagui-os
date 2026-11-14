/* chuva - chuva de letras: colunas de letras verdes caindo num fundo preto
 * (H83, vindo do pull request #7, do Gabriel).
 *
 * A janela é uma grade de letras (8x16 pixels cada, a fonte do SaguiOS).
 * Cada coluna tem uma "gota": a cabeça desce algumas linhas por segundo,
 * deixando letras sorteadas para trás. A letra da cabeça sai quase branca;
 * as que ficaram para trás vão apagando aos poucos (o "brilho" de cada
 * casa diminui a cada quadro), o que forma o rastro verde. Quando a gota
 * sai pela parte de baixo, a coluna espera um pouco e começa outra no
 * alto, com outra velocidade.
 *
 * Espaço pausa; Esc ou o X da janela fecham. */
#include "sagui.h"
#include "gui.h"

#define COLS 70
#define LINS 26
#define LARG (COLS * LETRA_L)
#define ALT  (LINS * LETRA_A)
#define QUADRO_MS 40               /* 25 quadros por segundo */
#define BRILHO_MAX 32

typedef struct {
    int cabeca;       /* linha da cabeça, em dezesseis avos de linha */
    int velocidade;   /* quanto a cabeça desce por quadro (mesma unidade) */
    int espera;       /* quadros até a próxima gota, depois que esta acabou */
} coluna_t;

static coluna_t col[COLS];
static unsigned char letra[LINS][COLS];
static unsigned char brilho[LINS][COLS];   /* 0 = apagada */
static uint32_t semente;

/* Gerador de números "aleatórios" (xorshift de 32 bits: três
 * deslocamentos com ou-exclusivo). A semente vem do kernel, para cada vez
 * que o programa abre a chuva ser diferente. */
static unsigned sorteio(void)
{
    semente ^= semente << 13;
    semente ^= semente >> 17;
    semente ^= semente << 5;
    return semente;
}

/* Letras, números e alguns símbolos; também letras acentuadas do Latin-1,
 * que a fonte do SaguiOS tem. */
static unsigned char letra_sorteada(void)
{
    static const char simbolos[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*+=<>?!:;"
                                   "ÁÉÍÓÚÃÕÇáéíóúãõç";
    return (unsigned char)simbolos[sorteio() % (sizeof simbolos - 1)];
}

static void nova_gota(coluna_t *c)
{
    c->cabeca = -(int)(sorteio() % (LINS * 16));    /* começa acima da janela */
    c->velocidade = 4 + (int)(sorteio() % 12);      /* de 1/4 a quase 1 linha por quadro */
    c->espera = 0;
}

/* A cor de uma letra pelo brilho: verde que vai escurecendo; a cabeça é
 * quase branca. */
static uint32_t cor(int b, bool cabeca)
{
    if (cabeca) return 0xD8FFD8;
    unsigned g = 40 + (unsigned)b * 215 / BRILHO_MAX;
    return (g / 6) << 16 | g << 8 | (g / 6);
}

static void avancar(void)
{
    /* Todo o rastro apaga um pouco; de vez em quando uma letra troca,
     * como se a gota estivesse "viva". */
    for (int l = 0; l < LINS; l++)
        for (int c = 0; c < COLS; c++) {
            if (brilho[l][c]) brilho[l][c]--;
            if (brilho[l][c] && sorteio() % 50 == 0) letra[l][c] = letra_sorteada();
        }
    for (int c = 0; c < COLS; c++) {
        coluna_t *g = &col[c];
        if (g->espera > 0) { if (--g->espera == 0) nova_gota(g); continue; }
        int antes = g->cabeca >> 4;
        g->cabeca += g->velocidade;
        int agora = g->cabeca >> 4;
        /* Cada linha nova que a cabeça alcançou ganha uma letra acesa. */
        for (int l = antes + 1; l <= agora; l++)
            if (l >= 0 && l < LINS) { letra[l][c] = letra_sorteada(); brilho[l][c] = BRILHO_MAX; }
        if (agora >= LINS + 4) g->espera = 1 + (int)(sorteio() % 30);
    }
}

static void desenhar(jan_t *j)
{
    g_retangulo(j, 0, 0, LARG, ALT, 0x000000);
    for (int c = 0; c < COLS; c++) {
        int cab = col[c].espera ? -1 : col[c].cabeca >> 4;
        for (int l = 0; l < LINS; l++) {
            if (!brilho[l][c]) continue;
            char t[2] = { (char)letra[l][c], 0 };
            g_texto(j, c * LETRA_L, l * LETRA_A, t, cor(brilho[l][c], l == cab));
        }
    }
    jan_mostrar(j);
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (bytes_aleatorios(&semente, sizeof semente) < 0 || semente == 0) semente = tempo_ms() | 1;
    jan_t *j = jan_abrir("Chuva de letras", LARG, ALT);
    if (!j) { imprimir_em(ERROS, "chuva: não consegui abrir a janela\n"); return 1; }
    for (int c = 0; c < COLS; c++) nova_gota(&col[c]);
    registrar("chuva: %d colunas e %d linhas", COLS, LINS);

    bool pausado = false;
    for (;;) {
        unsigned inicio = tempo_ms();
        evento_t e;
        while (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) goto fim;
            if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) goto fim;
            if (e.tipo == EV_TECLA && e.tecla == ' ') pausado = !pausado;
        }
        if (!pausado) { avancar(); desenhar(j); }
        /* Um quadro a cada QUADRO_MS: a chuva cai na mesma velocidade em
         * qualquer computador, e o resto do tempo fica livre. */
        unsigned gasto = tempo_ms() - inicio;
        dormir(gasto < QUADRO_MS ? QUADRO_MS - gasto : 1);
    }
fim:
    jan_fechar(j);
    return 0;
}
