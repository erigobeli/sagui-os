/* janelas.c - o gerenciador de janelas (H21). Ver D021.
 *
 * O kernel guarda a lista de janelas em ordem de empilhamento (a última é a
 * de cima e tem o foco), desenha as molduras e entrega os eventos. O
 * conteúdo de cada janela é desenhado pelo próprio programa, no anel 3,
 * em páginas que o kernel compartilha com ele.
 *
 * Redesenhar = recompor só o retângulo que mudou: fundo pronto da área de
 * trabalho, janelas de baixo para cima (moldura + conteúdo), barra superior
 * por último. Tudo recortado no retângulo, num buffer de trás (vídeo).
 *
 * Uma tarefa do kernel acorda a cada tique, consome as filas do mouse e do
 * teclado, arrasta, foca e fecha janelas, e entrega os eventos. Ela e as
 * chamadas de sistema mexem nas mesmas listas; por isso a tarefa trabalha
 * com as interrupções desligadas (as chamadas já rodam assim). */
#include "gui.h"
#include "abi.h"
#include "console.h"
#include "memoria.h"
#include "mouse.h"
#include "paginacao.h"
#include "processo.h"
#include "relogio.h"
#include "sistema.h"
#include "teclado.h"
#include "texto.h"
#include "video.h"
#include "disco.h"

#define MAX_JANELAS     32
#define FILA_EVENTOS    64
#define BARRA_TITULO    24
#define BORDA           2
#define SOMBRA          8   /* margem da sombra em volta da janela */
#define JANELAS_USUARIO 0xA0000000u   /* onde os pixels aparecem no espaço do programa */
#define ESPACO_JANELA   0x00400000u   /* 4 MB por janela */
#define DUPLO_CLIQUE    45            /* tiques (450 ms) */

typedef struct {
    bool       usada;
    int        id;
    int        pid;
    int        x, y;          /* canto da moldura */
    int        larg, alt;     /* área de conteúdo */
    char       titulo[48];
    uint32_t  *px;            /* pixels (físico = virtual no kernel) */
    uint32_t   paginas;
    uint32_t   virt;          /* endereço dos pixels no espaço do programa */
    evento_t   fila[FILA_EVENTOS];
    uint32_t   ini, fim;
    bool       minimizada, maximizada, redimensiona;
    int        rx, ry, rl, ra;   /* posição e tamanho antes de maximizar */
} janela_t;

static janela_t janelas[MAX_JANELAS];
static int ordem[MAX_JANELAS], qtd;   /* índices em janelas[], de baixo para cima */
static int proximo_id = 1;
static bool ativa;
static superficie_t tela;
static int suj_x0, suj_y0, suj_x1, suj_y1;   /* retângulo a recompor */

/* Estado do mouse no gerenciador. */
static int arrastando = -1, arr_dx, arr_dy;   /* janela sendo arrastada e onde foi pega */
static int captura = -1;                       /* janela que recebeu o último clique */
static int icone_pego = -1, pego_dx, pego_dy, pego_x, pego_y, fantasma_x, fantasma_y;
static bool icone_arrastando;
static bool laco_ativo;
static int laco_x0, laco_y0, laco_x1, laco_y1;
static void laco_retangulo(int *x, int *y, int *l, int *a);
static uint32_t ult_clique_tique;
static int ult_clique_x, ult_clique_y, ult_clique_alvo = -999;

bool gui_ativa(void) { return ativa; }

/* ------------------------------------------------------------ notificação (H34) */
#define AVISO_L 420
#define AVISO_A 62
#define AVISO_COLUNAS 49   /* letras por linha dentro da caixa */
static char aviso_texto[96];
static uint32_t aviso_ate;   /* tique em que some; 0 = sem aviso */

static void aviso_retangulo(int *x, int *y) { *x = (int)video_largura() - AVISO_L - 12; *y = BARRA_SUPERIOR + 10; }

void gui_aviso(const char *texto)
{
    txt_copia_n(aviso_texto, texto, sizeof aviso_texto);
    aviso_ate = relogio_tiques() + 5 * TIQUES_POR_SEGUNDO;
    int x, y;
    aviso_retangulo(&x, &y);
    gui_redesenhar(x - 2, y - 2, AVISO_L + 4, AVISO_A + 4);
    klog("[GUI] notificacao: %s\n", texto);
}

static void desenhar_aviso(superficie_t *t)
{
    if (!aviso_ate) return;
    int x, y;
    aviso_retangulo(&x, &y);
    sup_degrade(t, x, y, AVISO_L, AVISO_A, 0x222C38, 0x141A22);
    sup_moldura(t, x, y, AVISO_L, AVISO_A, 0xFFDF4A);
    sup_retangulo(t, x, y, 6, AVISO_A, 0xFFDF4A);
    sup_texto(t, x + 16, y + 6, "SaguiOS", 0xFFDF4A);
    /* Até duas linhas, quebrando no último espaço que couber. */
    const char *p = aviso_texto;
    for (int l = 0; l < 2 && *p; l++) {
        int n = (int)txt_tamanho(p);
        if (n > AVISO_COLUNAS) {
            n = AVISO_COLUNAS;
            while (n > 0 && p[n] != ' ') n--;
            if (n == 0) n = AVISO_COLUNAS;
        }
        char linha[AVISO_COLUNAS + 1];
        txt_copia_n(linha, p, (size_t)n + 1);
        sup_texto(t, x + 16, y + 24 + l * 17, linha, 0xFFFFFF);
        p += n;
        while (*p == ' ') p++;
    }
}

static int moldura_l(janela_t *j) { return j->larg + 2 * BORDA; }
static int moldura_a(janela_t *j) { return j->alt + BARRA_TITULO + BORDA; }

void gui_redesenhar(int x, int y, int l, int a)
{
    if (l <= 0 || a <= 0) return;
    if (suj_x0 >= suj_x1) { suj_x0 = x; suj_y0 = y; suj_x1 = x + l; suj_y1 = y + a; return; }
    if (x < suj_x0) suj_x0 = x;
    if (y < suj_y0) suj_y0 = y;
    if (x + l > suj_x1) suj_x1 = x + l;
    if (y + a > suj_y1) suj_y1 = y + a;
}

/* Inclui a sombra, desenhada por fora da moldura e um pouco abaixo (sem
 * isso, arrastar deixava rastros). */
static void redesenhar_janela(janela_t *j)
{
    gui_redesenhar(j->x - SOMBRA, j->y - SOMBRA, moldura_l(j) + 2 * SOMBRA, moldura_a(j) + 2 * SOMBRA + 3);
}

/* A de cima que não está minimizada. */
static janela_t *focada(void)
{
    for (int k = qtd - 1; k >= 0; k--)
        if (!janelas[ordem[k]].minimizada) return &janelas[ordem[k]];
    return NULL;
}

static void sujar_barra(void) { gui_redesenhar(0, 0, (int)video_largura(), BARRA_SUPERIOR); }

/* Sombra macia: anéis de 1 pixel, mais fortes perto da janela. */
static void desenhar_sombra(int x, int y, int l, int a, bool foco)
{
    y += 3;
    for (int k = SOMBRA; k >= 1; k--) {
        int alfa = (SOMBRA + 1 - k) * (SOMBRA + 1 - k) * (foco ? 2 : 1), c = k / 2;
        sup_escurecer(&tela, x - k + c, y - k, l + 2 * k - 2 * c, 1, alfa);
        sup_escurecer(&tela, x - k + c, y + a + k - 1, l + 2 * k - 2 * c, 1, alfa);
        sup_escurecer(&tela, x - k, y - k + c, 1, a + 2 * k - 2 * c, alfa);
        sup_escurecer(&tela, x + l + k - 1, y - k + c, 1, a + 2 * k - 2 * c, alfa);
    }
    /* Os cantos arredondados deixam ver a sombra por baixo. */
    sup_escurecer(&tela, x, y - 3, 6, 6, 70);
    sup_escurecer(&tela, x + l - 6, y - 3, 6, 6, 70);
}

void gui_sombra(int x, int y, int l, int a) { desenhar_sombra(x, y, l, a, true); }

/* Disco de 16 pixels: metade da largura de cada linha. */
static void desenhar_disco(int bx, int by, uint32_t topo, uint32_t base)
{
    static const uint8_t meia[8] = { 3, 5, 6, 7, 7, 8, 8, 8 };
    for (int k = 0; k < 16; k++) {
        int m = meia[k < 8 ? k : 15 - k];
        sup_retangulo(&tela, bx + 8 - m, by + k, 2 * m, 1, cor_mistura(topo, base, k, 15));
    }
}

/* Os três botões da moldura, da direita para a esquerda. */
#define BOTAO_FECHAR(l) ((l) - BORDA - 20)
#define BOTAO_MAX(l)    ((l) - BORDA - 40)
#define BOTAO_MIN(l)    ((l) - BORDA - 60)

static void desenhar_botoes(janela_t *j, int x, int y, int l, bool foco)
{
    int bx = x + BOTAO_FECHAR(l), by = y + 4;
    desenhar_disco(bx, by, foco ? 0xEC5A4E : 0x9A6A66, foco ? 0xC02E26 : 0x7A524E);
    for (int k = 4; k < 12; k++) {
        sup_retangulo(&tela, bx + k, by + k, 2, 1, 0xFFFFFF);
        sup_retangulo(&tela, bx + 14 - k, by + k, 2, 1, 0xFFFFFF);
    }
    bx = x + BOTAO_MAX(l);
    if (j->redimensiona) desenhar_disco(bx, by, foco ? 0x4CC47C : 0x7C9A88, foco ? 0x1E8A4C : 0x5C7A68);
    else desenhar_disco(bx, by, 0x8A949E, 0x6A747E);
    sup_moldura(&tela, bx + 4, by + 4, 8, 8, j->redimensiona ? 0xFFFFFF : 0xB0B8C0);
    sup_retangulo(&tela, bx + 4, by + 5, 8, 1, j->redimensiona ? 0xFFFFFF : 0xB0B8C0);
    bx = x + BOTAO_MIN(l);
    desenhar_disco(bx, by, foco ? 0xF6CE50 : 0xA89A6A, foco ? 0xD09A18 : 0x887A4E);
    sup_retangulo(&tela, bx + 4, by + 10, 8, 2, 0xFFFFFF);
}

static void desenhar_moldura(janela_t *j, bool foco)
{
    static const uint8_t canto[6] = { 5, 3, 2, 1, 1, 0 };
    int x = j->x, y = j->y, l = moldura_l(j), a = moldura_a(j);
    uint32_t topo = foco ? 0x18A65A : 0x7A8590, base = foco ? 0x0A6E38 : 0x5A6470;
    desenhar_sombra(x, y, l, a, foco);
    for (int k = 0; k < BARRA_TITULO; k++) {
        int c = k < 6 ? canto[k] : 0;
        sup_retangulo(&tela, x + c, y + k, l - 2 * c, 1, k == 0 ? cor_mistura(topo, 0xFFFFFF, 2, 5) : cor_mistura(topo, base, k, BARRA_TITULO - 1));
    }
    if (foco) sup_retangulo(&tela, x, y + BARRA_TITULO - 1, l, 1, 0xE8BE30);
    sup_retangulo(&tela, x, y + BARRA_TITULO, BORDA, a - BARRA_TITULO - 1, base);
    sup_retangulo(&tela, x + l - BORDA, y + BARRA_TITULO, BORDA, a - BARRA_TITULO - 1, base);
    sup_retangulo(&tela, x + 1, y + a - BORDA, l - 2, BORDA, base);
    sup_texto_forte(&tela, x + 11, y + 4, j->titulo, 0x063A1E);
    sup_texto_forte(&tela, x + 10, y + 3, j->titulo, foco ? 0xFFFFFF : 0xE0E4E8);
    desenhar_botoes(j, x, y, l, foco);
}

/* ------------------------------------------------------------ menus */

#define MENU_ITENS  12
#define MENU_ITEM_A 24
#define MENU_SEP_A  9
#define MENU_L_MIN  170

enum { A_NADA, A_ABRIR_ICONE, A_REMOVER_PEN, A_FUNDOS, A_FUNDO, A_ICONES, A_TAMANHO, A_GRADE, A_ORGANIZAR, A_ATUALIZAR, A_ICONE_LIXO, A_RESTAURAR_ICONES, A_ESVAZIAR, A_MOSTRAR_AREA,
       A_RESTAURAR, A_MINIMIZAR, A_MAXIMIZAR, A_FECHAR, A_PROGRAMA };

typedef struct { char texto[30]; int acao, arg; bool separador; } item_menu_t;

static struct {
    bool aberto;
    int x, y, l, a, qtd, sob;   /* sob: item debaixo do mouse, ou -1 */
    int janela;                 /* id da janela a que o menu se refere */
    item_menu_t itens[MENU_ITENS];
} menu;

static void sujar_menu(void)
{
    gui_redesenhar(menu.x - SOMBRA, menu.y - SOMBRA, menu.l + 2 * SOMBRA, menu.a + 2 * SOMBRA + 3);
}

static void menu_fechar(void)
{
    if (menu.aberto) sujar_menu();
    menu.aberto = false;
}

static void menu_novo(int id)
{
    menu_fechar();
    menu.qtd = 0;
    menu.janela = id;
}

static void menu_item(const char *texto, int acao, int arg)
{
    if (menu.qtd == MENU_ITENS) return;
    item_menu_t *i = &menu.itens[menu.qtd++];
    txt_copia_n(i->texto, texto, sizeof i->texto);
    i->acao = acao; i->arg = arg; i->separador = false;
}

static void menu_separador(void)
{
    if (menu.qtd) menu.itens[menu.qtd - 1].separador = true;
}

static void menu_abrir(int x, int y)
{
    menu.l = MENU_L_MIN;
    menu.a = 8;
    for (int i = 0; i < menu.qtd; i++) {
        int l = texto_largura(menu.itens[i].texto) + 44;
        if (l > menu.l) menu.l = l;
        menu.a += MENU_ITEM_A + (menu.itens[i].separador ? MENU_SEP_A : 0);
    }
    int vl = (int)video_largura(), va = (int)video_altura();
    menu.x = x + menu.l > vl - 4 ? vl - 4 - menu.l : x;
    menu.y = y + menu.a > va - 4 ? va - 4 - menu.a : y;
    if (menu.y < BARRA_SUPERIOR) menu.y = BARRA_SUPERIOR;
    menu.sob = -1;
    menu.aberto = true;
    sujar_menu();
    klog("[GUI] menu aberto com %d itens em (%d,%d)\n", menu.qtd, menu.x, menu.y);
}

/* Item sob o ponto, ou -1 (fora do menu ou num separador). */
static int menu_item_em(int x, int y)
{
    if (x < menu.x || x >= menu.x + menu.l) return -1;
    int yy = menu.y + 4;
    for (int i = 0; i < menu.qtd; i++) {
        if (y >= yy && y < yy + MENU_ITEM_A) return i;
        yy += MENU_ITEM_A + (menu.itens[i].separador ? MENU_SEP_A : 0);
    }
    return -1;
}

static void desenhar_menu(void)
{
    if (!menu.aberto) return;
    int x = menu.x, y = menu.y, l = menu.l, a = menu.a;
    desenhar_sombra(x, y, l, a, true);
    sup_degrade(&tela, x + 1, y + 1, l - 2, a - 2, 0xFBFCFA, 0xECEFEA);
    sup_retangulo(&tela, x + 2, y, l - 4, 1, 0x7A8590);
    sup_retangulo(&tela, x + 2, y + a - 1, l - 4, 1, 0x7A8590);
    sup_retangulo(&tela, x, y + 2, 1, a - 4, 0x7A8590);
    sup_retangulo(&tela, x + l - 1, y + 2, 1, a - 4, 0x7A8590);
    int yy = y + 4;
    for (int i = 0; i < menu.qtd; i++) {
        item_menu_t *it = &menu.itens[i];
        bool sob = i == menu.sob;
        if (sob) {
            sup_degrade(&tela, x + 4, yy, l - 8, MENU_ITEM_A, 0x18A65A, 0x0A6E38);
            sup_retangulo(&tela, x + 4, yy, 3, MENU_ITEM_A, 0xE8BE30);
        }
        sup_texto(&tela, x + 16, yy + 4, it->texto, sob ? 0xFFFFFF : 0x1E2328);
        if (it->acao == A_FUNDOS || it->acao == A_ICONES) sup_texto(&tela, x + l - 18, yy + 4, ">", sob ? 0xFFFFFF : 0x6A737C);
        yy += MENU_ITEM_A;
        if (it->separador) {
            sup_retangulo(&tela, x + 10, yy + 4, l - 20, 1, 0xC4CBD2);
            yy += MENU_SEP_A;
        }
    }
}

/* ------------------------------------------------------------ barra de tarefas */

#define TAREFA_X0 116
#define TAREFA_L  124
#define TAREFA_PASSO (TAREFA_L + 4)

/* Quantos botões cabem antes do relógio. */
static int tarefas_max(void) { return ((int)video_largura() - 320 - TAREFA_X0) / TAREFA_PASSO; }

static void desenhar_tarefas(void)
{
    janela_t *f = focada();
    int n = 0, max = tarefas_max();
    for (int i = 0; i < MAX_JANELAS && n < max; i++) {
        janela_t *j = &janelas[i];
        if (!j->usada) continue;
        int x = TAREFA_X0 + n++ * TAREFA_PASSO;
        bool ativa_j = j == f;
        sup_degrade(&tela, x, 3, TAREFA_L, 17, ativa_j ? 0x18A65A : 0x34404C, ativa_j ? 0x0A6E38 : 0x242C36);
        if (j->minimizada) sup_retangulo(&tela, x + 6, 17, TAREFA_L - 12, 1, 0x6A737C);
        else sup_retangulo(&tela, x + 6, 18, TAREFA_L - 12, 1, 0xE8BE30);
        char t[15];
        txt_copia_n(t, j->titulo, sizeof t);
        sup_texto(&tela, x + 6, 3, t, j->minimizada ? 0x9AA8B8 : 0xFFFFFF);
    }
}

static janela_t *tarefa_em(int x, int y)
{
    if (y >= BARRA_SUPERIOR || x < TAREFA_X0) return NULL;
    int n = (x - TAREFA_X0) / TAREFA_PASSO, k = 0;
    if ((x - TAREFA_X0) % TAREFA_PASSO >= TAREFA_L || n >= tarefas_max()) return NULL;
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && k++ == n) return &janelas[i];
    return NULL;
}

/* Recompõe o retângulo sujo e mostra. */
void gui_mostrar(void)
{
    if (!ativa || suj_x0 >= suj_x1) return;
    int x = suj_x0, y = suj_y0, l = suj_x1 - suj_x0, a = suj_y1 - suj_y0;
    suj_x0 = suj_x1 = 0;
    sup_recorte(&tela, x, y, l, a);
    if (tela.cx0 >= tela.cx1 || tela.cy0 >= tela.cy1) return;
    area_fundo(&tela);
    janela_t *foco = focada();
    for (int k = 0; k < qtd; k++) {
        janela_t *j = &janelas[ordem[k]];
        if (j->minimizada) continue;
        /* Fora do retângulo sujo não há o que pintar. */
        if (j->x - SOMBRA >= tela.cx1 || j->y - SOMBRA >= tela.cy1 ||
            j->x + moldura_l(j) + SOMBRA <= tela.cx0 || j->y + moldura_a(j) + SOMBRA + 3 <= tela.cy0) continue;
        desenhar_moldura(j, j == foco);
        sup_copiar(&tela, j->x + BORDA, j->y + BARRA_TITULO, j->px, j->larg, j->alt, j->larg);
    }
    desenhar_aviso(&tela);
    if (tela.cy0 < BARRA_SUPERIOR) {
        area_barra(&tela, qtd == 0);
        desenhar_tarefas();
    }
    if (laco_ativo) {
        int lx, ly, ll, la;
        laco_retangulo(&lx, &ly, &ll, &la);
        sup_tingir(&tela, lx, ly, ll, la, 0x18A65A, 70);
        sup_moldura(&tela, lx, ly, ll, la, 0xE8BE30);
    }
    if (icone_arrastando) area_icone_fantasma(&tela, icone_pego, fantasma_x, fantasma_y);
    iniciar_desenhar(&tela);
    desenhar_menu();
    video_marcar(tela.cx0, tela.cy0, tela.cx1 - tela.cx0, tela.cy1 - tela.cy0);
    video_mostrar();
}

/* ------------------------------------------------------------ eventos */

static void entregar(janela_t *j, evento_t *e)
{
    e->janela = j->id;
    uint32_t prox = (j->fim + 1) % FILA_EVENTOS;
    if (prox == j->ini) {
        /* Fila cheia: movimentos podem ser jogados fora; o resto não. */
        if (e->tipo == EV_MOVER) return;
        j->ini = (j->ini + 1) % FILA_EVENTOS;
    }
    j->fila[j->fim] = *e;
    j->fim = prox;
    acordar(processo_por_pid(j->pid));
}

static int janela_em(int x, int y)   /* índice em ordem[], de cima para baixo */
{
    for (int k = qtd - 1; k >= 0; k--) {
        janela_t *j = &janelas[ordem[k]];
        if (!j->minimizada && x >= j->x && x < j->x + moldura_l(j) && y >= j->y && y < j->y + moldura_a(j))
            return k;
    }
    return -1;
}

static void trazer_para_cima(int k)
{
    if (k == qtd - 1) return;
    janela_t *antes = focada();
    sujar_barra();
    int i = ordem[k];
    for (; k < qtd - 1; k++) ordem[k] = ordem[k + 1];
    ordem[qtd - 1] = i;
    if (antes) redesenhar_janela(antes);
    redesenhar_janela(&janelas[i]);
}

static int posicao_em_ordem(janela_t *j)
{
    for (int k = 0; k < qtd; k++)
        if (&janelas[ordem[k]] == j) return k;
    return -1;
}

static void minimizar(janela_t *j)
{
    if (j->minimizada) return;
    janela_t *antes = focada();
    redesenhar_janela(j);
    j->minimizada = true;
    if (arrastando == j - janelas) arrastando = -1;
    if (captura == j - janelas) captura = -1;
    janela_t *f = focada();
    if (f && f != antes) redesenhar_janela(f);
    sujar_barra();
    klog("[GUI] janela %d minimizada\n", j->id);
}

static void restaurar(janela_t *j)
{
    bool estava = j->minimizada;
    j->minimizada = false;
    redesenhar_janela(j);
    trazer_para_cima(posicao_em_ordem(j));
    sujar_barra();
    if (estava) klog("[GUI] janela %d restaurada\n", j->id);
}

static void avisar_tamanho(janela_t *j)
{
    for (uint32_t k = 0; k < (uint32_t)(j->larg * j->alt); k++) j->px[k] = 0xF4F4F0;
    evento_t e = { .tipo = EV_TAMANHO, .x = j->larg, .y = j->alt };
    entregar(j, &e);
}

/* Maximizar precisa do programa: só ele sabe reorganizar o conteúdo. */
static void alternar_maximizar(janela_t *j)
{
    if (!j->redimensiona) return;
    redesenhar_janela(j);
    if (j->maximizada) {
        j->x = j->rx; j->y = j->ry; j->larg = j->rl; j->alt = j->ra;
    } else {
        j->rx = j->x; j->ry = j->y; j->rl = j->larg; j->ra = j->alt;
        j->x = 0; j->y = BARRA_SUPERIOR;
        j->larg = (int)video_largura() - 2 * BORDA;
        j->alt = (int)video_altura() - BARRA_SUPERIOR - BARRA_TITULO - BORDA;
    }
    j->maximizada = !j->maximizada;
    redesenhar_janela(j);
    avisar_tamanho(j);
    klog("[GUI] janela %d %s: %dx%d\n", j->id, j->maximizada ? "maximizada" : "voltou ao tamanho", j->larg, j->alt);
}

static void pedir_fechar(janela_t *j)
{
    evento_t e = { .tipo = EV_FECHAR };
    entregar(j, &e);
    klog("[GUI] fechar janela %d (%s)\n", j->id, j->titulo);
}

static janela_t *janela_por_id(int id)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].id == id) return &janelas[i];
    return NULL;
}

static void menu_de_janela(janela_t *j, int x, int y)
{
    menu_novo(j->id);
    if (j->minimizada) menu_item("Restaurar", A_RESTAURAR, 0);
    else menu_item("Minimizar", A_MINIMIZAR, 0);
    if (j->redimensiona) menu_item(j->maximizada ? "Voltar ao tamanho" : "Maximizar", A_MAXIMIZAR, 0);
    menu_separador();
    menu_item("Fechar", A_FECHAR, 0);
    menu_abrir(x, y);
}

static void menu_da_area(int x, int y)
{
    menu_novo(0);
    menu_item("Novo Terminal", A_ABRIR_ICONE, 1);
    menu_item("Arquivos", A_ABRIR_ICONE, 0);
    menu_item("Editor de texto", A_ABRIR_ICONE, 2);
    menu_separador();
    menu_item("Papel de parede", A_FUNDOS, 0);
    menu_item("Ícones", A_ICONES, 0);
    menu_item("Mostrar área de trabalho", A_MOSTRAR_AREA, 0);
    menu_separador();
    menu_item("Sobre o SaguiOS", A_ABRIR_ICONE, 6);
    menu_separador();
    menu_item("Atualizar", A_ATUALIZAR, 0);
    menu_abrir(x, y);
}

static void menu_do_icone(int ic, int x, int y)
{
    menu_novo(0);
    if (area_icone_e_pendrive(ic)) {
        menu_item("Abrir", A_ABRIR_ICONE, ic);
        menu_item("Remover com segurança", A_REMOVER_PEN, ic);
    } else if (area_icone_e_lixeira(ic)) {
        menu_item("Abrir", A_ABRIR_ICONE, ic);
        menu_item("Restaurar ícones", A_RESTAURAR_ICONES, 0);
        menu_separador();
        menu_item("Esvaziar lixeira", A_ESVAZIAR, 0);
    } else {
        menu_item("Abrir", A_ABRIR_ICONE, ic);
        menu_separador();
        menu_item("Mover para a lixeira", A_ICONE_LIXO, ic);
    }
    menu_abrir(x, y);
}

static void menu_executar(item_menu_t *it)
{
    int acao = it->acao, arg = it->arg, x = menu.x, y = menu.y;
    janela_t *j = janela_por_id(menu.janela);
    klog("[GUI] menu: %s\n", it->texto);
    menu_fechar();
    switch (acao) {
    case A_ABRIR_ICONE: area_icone_abrir(arg); break;
    case A_REMOVER_PEN: area_icone_direito(arg); break;
    case A_FUNDOS:
        menu_novo(0);
        for (int i = 0; i < QTD_FUNDOS; i++) {
            char t[30];
            snformatar(t, sizeof t, "%s %s", i == area_fundo_atual() ? "*" : " ", area_fundo_nome(i));
            menu_item(t, A_FUNDO, i);
        }
        menu_abrir(x + 24, y + 24);
        break;
    case A_ICONES: {
        int t = area_icones_lado();
        menu_novo(0);
        menu_item(t == 48 ? "* Pequenos" : "  Pequenos", A_TAMANHO, 48);
        menu_item(t == 64 ? "* Médios" : "  Médios", A_TAMANHO, 64);
        menu_item(t == 96 ? "* Grandes" : "  Grandes", A_TAMANHO, 96);
        menu_separador();
        menu_item(area_grade_ligada() ? "* Alinhar à grade" : "  Alinhar à grade", A_GRADE, 0);
        menu_item("  Organizar agora", A_ORGANIZAR, 0);
        menu_abrir(x + 24, y + 24);
        break;
    }
    case A_TAMANHO: area_icones_tamanho(arg); gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura()); break;
    case A_GRADE: area_grade(!area_grade_ligada()); gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura()); break;
    case A_ORGANIZAR: area_icones_organizar(); gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura()); break;
    case A_ATUALIZAR: area_atualizar(); gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura()); break;
    case A_ICONE_LIXO: area_jogar_fora(arg); gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura()); break;
    case A_RESTAURAR_ICONES: area_restaurar_icones(); gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura()); break;
    case A_ESVAZIAR: {
        char *argv[] = { "arquivos", "--esvaziar", NULL };
        processo_executar("/bin/arquivos", 2, argv, NULL);
        break;
    }
    case A_FUNDO:
        area_fundo_escolher(arg);
        gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
        break;
    case A_MOSTRAR_AREA:
        for (int i = 0; i < MAX_JANELAS; i++)
            if (janelas[i].usada) minimizar(&janelas[i]);
        break;
    case A_RESTAURAR: if (j) restaurar(j); break;
    case A_MINIMIZAR: if (j) minimizar(j); break;
    case A_MAXIMIZAR: if (j) alternar_maximizar(j); break;
    case A_FECHAR:    if (j) pedir_fechar(j); break;
    case A_PROGRAMA:
        if (j) { evento_t e = { .tipo = EV_MENU, .tecla = (unsigned)arg }; entregar(j, &e); }
        break;
    default: break;
    }
}

/* Com o menu aberto, o mouse é todo dele. */
static void menu_tratar_mouse(evento_mouse_t *m)
{
    int i = menu_item_em(m->x, m->y);
    if (i != menu.sob) { menu.sob = i; sujar_menu(); }
    if (!(m->mudaram & m->botoes)) return;
    if (i >= 0 && (m->mudaram & BOTAO_ESQUERDO)) menu_executar(&menu.itens[i]);
    else if (i < 0) menu_fechar();
}

/* Ícone pego com o botão esquerdo. Só vira arrasto depois de andar um pouco,
 * para o duplo clique continuar funcionando. */

static void sujar_fantasma(void)
{
    gui_redesenhar(fantasma_x - 4, fantasma_y - 4, 140, 130);
}

static void tratar_icone_pego(evento_mouse_t *m)
{
    if (!(m->botoes & BOTAO_ESQUERDO)) {
        if (icone_arrastando) {
            int ix, iy, il, ia;
            area_icone_retangulo(icone_pego, &ix, &iy, &il, &ia);
            if (area_icone_selecionado(icone_pego)) area_mover_selecao(icone_pego, fantasma_x - ix, fantasma_y - iy);
            area_icone_mover(icone_pego, fantasma_x, fantasma_y);
            gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
        }
        icone_pego = -1;
        icone_arrastando = false;
        return;
    }
    int dx = m->x - pego_x, dy = m->y - pego_y;
    if (!icone_arrastando && dx * dx + dy * dy < 25) return;
    if (icone_arrastando) sujar_fantasma();
    icone_arrastando = true;
    fantasma_x = m->x - pego_dx;
    fantasma_y = m->y - pego_dy;
    sujar_fantasma();
}

static void laco_retangulo(int *x, int *y, int *l, int *a)
{
    *x = laco_x0 < laco_x1 ? laco_x0 : laco_x1;
    *y = laco_y0 < laco_y1 ? laco_y0 : laco_y1;
    *l = (laco_x0 < laco_x1 ? laco_x1 - laco_x0 : laco_x0 - laco_x1) + 1;
    *a = (laco_y0 < laco_y1 ? laco_y1 - laco_y0 : laco_y0 - laco_y1) + 1;
}

static void sujar_laco(void)
{
    int x, y, l, a;
    laco_retangulo(&x, &y, &l, &a);
    gui_redesenhar(x - 1, y - 1, l + 2, a + 2);
}

static void tratar_laco(evento_mouse_t *m)
{
    sujar_laco();
    if (m->botoes & BOTAO_ESQUERDO) {
        laco_x1 = m->x;
        laco_y1 = m->y < BARRA_SUPERIOR ? BARRA_SUPERIOR : m->y;
        sujar_laco();
        return;
    }
    laco_ativo = false;
    int x, y, l, a;
    laco_retangulo(&x, &y, &l, &a);
    if (l > 4 || a > 4) {
        area_selecionar(x, y, l, a);
        gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
    }
}

static void tratar_mouse(evento_mouse_t *m)
{
    if (menu.aberto) { menu_tratar_mouse(m); return; }
    if (iniciar_aberto()) { iniciar_mouse(m); return; }
    if (icone_pego >= 0) { tratar_icone_pego(m); return; }
    if (laco_ativo) { tratar_laco(m); return; }
    bool apertou = (m->mudaram & m->botoes) != 0;
    bool soltou = (m->mudaram & ~m->botoes) != 0;

    if (arrastando >= 0) {
        janela_t *j = &janelas[arrastando];
        if (!j->usada || !(m->botoes & BOTAO_ESQUERDO)) {
            if (j->usada) klog("[GUI] janela %d movida para (%d,%d)\n", j->id, j->x, j->y);
            arrastando = -1;
            return;
        }
        int nx = m->x - arr_dx, ny = m->y - arr_dy;
        if (ny < BARRA_SUPERIOR) ny = BARRA_SUPERIOR;
        if (ny > (int)video_altura() - BARRA_TITULO) ny = (int)video_altura() - BARRA_TITULO;
        if (nx > (int)video_largura() - 40) nx = (int)video_largura() - 40;
        if (nx < 40 - moldura_l(j)) nx = 40 - moldura_l(j);
        if (nx != j->x || ny != j->y) {
            redesenhar_janela(j);
            j->x = nx; j->y = ny;
            redesenhar_janela(j);
        }
        return;
    }

    if (apertou && m->y < BARRA_SUPERIOR) {
        if (m->x < 108 && (m->mudaram & BOTAO_ESQUERDO)) { iniciar_abrir(); captura = -1; return; }
        if (area_desligar_clicado(m->x, m->y) && (m->mudaram & BOTAO_ESQUERDO)) {
            char *argv[] = { "desligar", NULL };
            int pid = processo_executar("/bin/desligar", 1, argv, NULL);
            klog("[GUI] botao Desligar clicado -> /bin/desligar (pid %d)\n", pid);
            captura = -1;
            return;
        }
        janela_t *t = tarefa_em(m->x, m->y);
        if (t && (m->mudaram & BOTAO_DIREITO)) menu_de_janela(t, m->x, BARRA_SUPERIOR);
        else if (t && t == focada()) minimizar(t);
        else if (t) restaurar(t);
        captura = -1;
        return;
    }

    if (apertou) {
        int k = janela_em(m->x, m->y);
        if (k < 0) {
            /* Na área de trabalho: duplo clique num ícone abre o programa. */
            int ic = area_icone_em(m->x, m->y);
            uint32_t agora = relogio_tiques();
            bool duplo = ic >= 0 && ult_clique_alvo == 1000 + ic && agora - ult_clique_tique < DUPLO_CLIQUE;
            ult_clique_alvo = ic >= 0 ? 1000 + ic : -999;
            ult_clique_tique = agora;
            if (duplo && (m->mudaram & BOTAO_ESQUERDO)) { area_icone_abrir(ic); ult_clique_alvo = -999; }
            if ((ic < 0 || !area_icone_selecionado(ic)) && area_limpar_selecao())
                gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
            if (ic < 0 && (m->mudaram & BOTAO_ESQUERDO)) {
                laco_ativo = true;
                laco_x0 = laco_x1 = m->x;
                laco_y0 = laco_y1 = m->y;
            }
            if (ic >= 0 && (m->mudaram & BOTAO_ESQUERDO) && !duplo) {
                int ix, iy, il, ia;
                area_icone_retangulo(ic, &ix, &iy, &il, &ia);
                icone_pego = ic;
                pego_x = m->x; pego_y = m->y;
                pego_dx = m->x - ix; pego_dy = m->y - iy;
            }
            if (m->mudaram & BOTAO_DIREITO) {
                if (ic >= 0) menu_do_icone(ic, m->x, m->y);
                else menu_da_area(m->x, m->y);
            }
            captura = -1;
            return;
        }
        if (area_limpar_selecao()) gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
        trazer_para_cima(k);
        janela_t *j = &janelas[ordem[qtd - 1]];
        int idx = ordem[qtd - 1];
        int rx = m->x - j->x, ry = m->y - j->y;
        if (ry < BARRA_TITULO) {
            int l = moldura_l(j);
            bool em_y = ry >= 4 && ry < 20;
            if (m->mudaram & BOTAO_DIREITO) {
                menu_de_janela(j, m->x, m->y);
            } else if (em_y && rx >= BOTAO_FECHAR(l) && rx < BOTAO_FECHAR(l) + 16) {
                pedir_fechar(j);
            } else if (em_y && rx >= BOTAO_MAX(l) && rx < BOTAO_MAX(l) + 16) {
                alternar_maximizar(j);
            } else if (em_y && rx >= BOTAO_MIN(l) && rx < BOTAO_MIN(l) + 16) {
                minimizar(j);
            } else if (m->mudaram & BOTAO_ESQUERDO) {
                uint32_t agora = relogio_tiques();
                bool duplo = ult_clique_alvo == 2000 + idx && agora - ult_clique_tique < DUPLO_CLIQUE;
                ult_clique_alvo = duplo ? -999 : 2000 + idx;
                ult_clique_tique = agora;
                if (duplo) alternar_maximizar(j);
                else if (!j->maximizada) { arrastando = idx; arr_dx = rx; arr_dy = ry; }
            }
            captura = -1;
            return;
        }
        int cx = m->x - j->x - BORDA, cy = m->y - j->y - BARRA_TITULO;
        if (cx < 0 || cy < 0 || cx >= j->larg || cy >= j->alt) return;   /* na borda */
        uint32_t agora = relogio_tiques();
        int dx = cx - ult_clique_x, dy = cy - ult_clique_y;
        bool duplo = ult_clique_alvo == j->id && agora - ult_clique_tique < DUPLO_CLIQUE &&
                     dx > -5 && dx < 5 && dy > -5 && dy < 5;
        evento_t e = { .tipo = EV_CLIQUE, .x = cx, .y = cy, .botoes = m->mudaram & m->botoes,
                       .cliques = duplo ? 2 : 1, .modificadores = teclado_modificadores() & 3 };
        ult_clique_alvo = duplo ? -999 : j->id;
        ult_clique_tique = agora;
        ult_clique_x = cx; ult_clique_y = cy;
        entregar(j, &e);
        captura = idx;
        return;
    }

    if (captura >= 0 && janelas[captura].usada) {
        janela_t *j = &janelas[captura];
        evento_t e = { .tipo = soltou ? EV_SOLTOU : EV_MOVER, .x = m->x - j->x - BORDA,
                       .y = m->y - j->y - BARRA_TITULO, .botoes = soltou ? m->mudaram : m->botoes };
        entregar(j, &e);
        if (soltou) captura = -1;
        return;
    }
    /* Movimento sem botão: só para a janela com foco, se o mouse estiver nela. */
    janela_t *j = focada();
    if (j) {
        int cx = m->x - j->x - BORDA, cy = m->y - j->y - BARRA_TITULO;
        if (cx >= 0 && cy >= 0 && cx < j->larg && cy < j->alt) {
            evento_t e = { .tipo = EV_MOVER, .x = cx, .y = cy, .botoes = 0 };
            entregar(j, &e);
        }
    }
}

static void tarefa_gerenciador(void *arg)
{
    (void)arg;
    static const char canal = 0;
    for (;;) {
        int_desliga();
        evento_mouse_t m;
        while (mouse_tentar(&m))
            tratar_mouse(&m);
        int t;
        while ((t = teclado_tentar()) >= 0) {
            if (menu.aberto) { if (t == TECLA_ESC) menu_fechar(); continue; }
            if (iniciar_aberto()) { iniciar_tecla(t); continue; }
            if (t == TECLA_DELETE && area_tem_selecao()) {
                area_jogar_fora(-1);
                gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
                continue;
            }
            janela_t *j = focada();
            if (!j) continue;
            evento_t e = { .tipo = EV_TECLA, .tecla = (unsigned)t, .modificadores = teclado_modificadores() & 3 };
            entregar(j, &e);
        }
        if (area_relogio_mudou())
            gui_redesenhar(0, 0, (int)video_largura(), BARRA_SUPERIOR);
        /* Pendrive: avisos do kernel viram notificações; o ícone acompanha. */
        int tipo;
        char texto[96];
        while (disco_proximo_aviso(&tipo, texto, sizeof texto))
            gui_aviso(texto);
        disco_t *pd = NULL;
        for (int k = 0; k < MAX_DISCOS && !pd; k++) { disco_t *d = disco_indice(k); if (d && d->presente && !d->de_boot) pd = d; }
        if (area_pendrive(pd != NULL, pd ? (pd->rotulo[0] ? pd->rotulo : "Pendrive") : NULL))
            gui_redesenhar(0, BARRA_SUPERIOR, (int)video_largura(), (int)video_altura() - BARRA_SUPERIOR);
        if (aviso_ate && (int32_t)(relogio_tiques() - aviso_ate) >= 0) {
            aviso_ate = 0;
            int x, y;
            aviso_retangulo(&x, &y);
            gui_redesenhar(x - 2, y - 2, AVISO_L + 4, AVISO_A + 4);
        }
        gui_mostrar();
        /* Dorme até o próximo tique (o escalonador acorda pelo despertador). */
        atual->acordar_em = relogio_tiques() + 1;
        bloquear(&canal);
        ceder();
    }
}

bool gui_iniciar(void)
{
    if (!video_ativo() || !area_iniciar()) return false;
    sup_iniciar(&tela, video_buffer(), (int)video_largura(), (int)video_altura());
    ativa = true;
    gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
    gui_mostrar();
    tarefa_kernel("janelas", tarefa_gerenciador, NULL);
    klog("[GUI] area de trabalho pronta\n");
    return true;
}

/* ------------------------------------------------------------ chamadas de sistema */

static janela_t *janela_do_processo(int id)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].id == id)
            return janelas[i].pid == atual->pid ? &janelas[i] : NULL;
    return NULL;
}

static void destruir(int i)
{
    janela_t *j = &janelas[i];
    redesenhar_janela(j);
    processo_t *dono = processo_por_pid(j->pid);
    if (dono && dono->usuario && dono->espaco != espaco_kernel())
        for (uint32_t k = 0; k < j->paginas; k++)
            espaco_desmapear(dono->espaco, j->virt + k * PAGINA);
    memfisica_liberar_seguidas((uint32_t)j->px, j->paginas);
    j->usada = false;
    int k = 0;
    while (k < qtd && ordem[k] != i) k++;
    for (; k < qtd - 1; k++) ordem[k] = ordem[k + 1];
    if (k < qtd) qtd--;
    if (arrastando == i) arrastando = -1;
    if (captura == i) captura = -1;
    if (menu.aberto && menu.janela == j->id) menu_fechar();
    sujar_barra();
    if (focada()) redesenhar_janela(focada());   /* a nova janela de cima ganha o foco */
}

void janelas_do_processo_fechar(processo_t *p)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].pid == p->pid)
            destruir(i);
    gui_mostrar();
}

int32_t sis_criar_janela(uint32_t ptitulo, uint32_t dims, uint32_t ppixels, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (!ativa) return ERRO_ACESSO;
    char titulo[48];
    int r = usuario_texto((const char *)ptitulo, titulo, sizeof titulo);
    if (r < 0) return r;
    int l = (int)(dims & 0xFFFF), a = (int)(dims >> 16);
    bool redimensiona = (a & JANELA_REDIMENSIONAVEL) != 0;
    a &= ~JANELA_REDIMENSIONAVEL;
    if (l < 40 || a < 20 || l > JANELA_LARG_MAX || a > JANELA_ALT_MAX) return ERRO_INVALIDO;
    if (!usuario_ok((void *)ppixels, 4, true)) return ERRO_ENDERECO;
    int i = 0;
    while (i < MAX_JANELAS && janelas[i].usada) i++;
    if (i == MAX_JANELAS) return ERRO_LIMITE;
    /* Quem pode maximizar já nasce com memória para a tela inteira. */
    uint32_t px_max = redimensiona ? video_largura() * video_altura() : (uint32_t)(l * a);
    uint32_t paginas = (px_max * 4 + PAGINA - 1) / PAGINA;
    uint32_t fis = memfisica_alocar_seguidas(paginas);
    if (!fis) return ERRO_SEM_MEMORIA;
    janela_t *j = &janelas[i];
    memset(j, 0, sizeof *j);
    j->usada = true;
    j->id = proximo_id++;
    j->pid = atual->pid;
    j->larg = l; j->alt = a;
    j->redimensiona = redimensiona;
    j->px = (uint32_t *)fis;
    j->paginas = paginas;
    j->virt = JANELAS_USUARIO + (uint32_t)i * ESPACO_JANELA;
    txt_copia_n(j->titulo, titulo, sizeof j->titulo);
    for (uint32_t k = 0; k < (uint32_t)(l * a); k++) j->px[k] = 0xF4F4F0;
    for (uint32_t k = 0; k < paginas; k++)
        espaco_mapear(atual->espaco, j->virt + k * PAGINA, fis + k * PAGINA, PG_USUARIO | PG_ESCRITA | PG_EMPRESTADA);
    /* Posição em cascata, sem passar da tela. */
    int n = j->id % 8;
    j->x = 150 + n * 34;
    j->y = BARRA_SUPERIOR + 30 + n * 28;
    if (j->x + moldura_l(j) > (int)video_largura()) j->x = (int)video_largura() - moldura_l(j);
    if (j->y + moldura_a(j) > (int)video_altura()) j->y = (int)video_altura() - moldura_a(j);
    if (j->x < 0) j->x = 0;
    if (j->y < BARRA_SUPERIOR) j->y = BARRA_SUPERIOR;
    janela_t *antes = focada();
    if (antes) redesenhar_janela(antes);
    ordem[qtd++] = i;
    redesenhar_janela(j);
    sujar_barra();
    gui_mostrar();
    *(uint32_t *)ppixels = j->virt;
    klog("[GUI] janela %d criada por %d: \"%s\" %dx%d em (%d,%d)\n", j->id, j->pid, j->titulo, l, a, j->x, j->y);
    return j->id;
}

int32_t sis_fechar_janela(uint32_t id, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    destruir((int)(j - janelas));
    gui_mostrar();
    return 0;
}

int32_t sis_atualizar_janela(uint32_t id, uint32_t pret, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    int x = 0, y = 0, l = j->larg, a = j->alt;
    if (pret) {
        if (!usuario_ok((void *)pret, sizeof(retangulo_t), false)) return ERRO_ENDERECO;
        retangulo_t *r = (retangulo_t *)pret;
        x = r->x; y = r->y; l = r->largura; a = r->altura;
        if (x < 0) { l += x; x = 0; }
        if (y < 0) { a += y; y = 0; }
        if (x + l > j->larg) l = j->larg - x;
        if (y + a > j->alt) a = j->alt - y;
    }
    gui_redesenhar(j->x + BORDA + x, j->y + BARRA_TITULO + y, l, a);
    gui_mostrar();
    return 0;
}

int32_t sis_proximo_evento(uint32_t id, uint32_t pev, uint32_t esperar, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (!usuario_ok((void *)pev, sizeof(evento_t), true)) return ERRO_ENDERECO;
    bool tem_janela = false;
    for (int i = 0; i < MAX_JANELAS; i++) {
        janela_t *j = &janelas[i];
        if (!j->usada || j->pid != atual->pid || (id && j->id != (int)id)) continue;
        tem_janela = true;
        if (j->ini != j->fim) {
            *(evento_t *)pev = j->fila[j->ini];
            j->ini = (j->ini + 1) % FILA_EVENTOS;
            return 1;
        }
    }
    if (!tem_janela) return ERRO_INVALIDO;
    if (!esperar) return 0;
    bloquear(atual);   /* "entregar" acorda o dono da janela */
    return SIS_REPETIR;
}

/* SIS_TECLAS_SEGURADAS (&mapa[32]): quais teclas estão apertadas agora.
 * Só quem tem a janela em foco recebe o mapa; os outros recebem tudo zerado,
 * para um programa em segundo plano não espiar o que se digita. */
int32_t sis_teclas_seguradas(uint32_t pmapa, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (!usuario_ok((void *)pmapa, 32, true)) return ERRO_INVALIDO;
    uint8_t mapa[32];
    memset(mapa, 0, sizeof mapa);
    janela_t *j = focada();
    bool foco = j && j->pid == atual->pid;
    if (foco) teclado_seguradas(mapa);
    memcpy((void *)pmapa, mapa, 32);
    return foco ? 1 : 0;
}

int32_t sis_titulo_janela(uint32_t id, uint32_t ptitulo, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    int r = usuario_texto((const char *)ptitulo, j->titulo, sizeof j->titulo);
    if (r < 0) return r;
    gui_redesenhar(j->x, j->y, moldura_l(j), BARRA_TITULO);
    sujar_barra();
    gui_mostrar();
    return 0;
}

/* SIS_MENU_JANELA: menu de um programa, em coordenadas da janela. Os itens vêm
 * separados por "|"; um item vazio vira separador. */
int32_t sis_menu_janela(uint32_t id, uint32_t pos, uint32_t ptexto, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    char texto[200];
    int r = usuario_texto((const char *)ptexto, texto, sizeof texto);
    if (r < 0) return r;
    menu_novo(j->id);
    char *p = texto;
    for (int n = 0; *p; ) {
        char *f = p;
        while (*f && *f != '|') f++;
        bool fim = !*f;
        *f = 0;
        if (*p) menu_item(p, A_PROGRAMA, n++);
        else menu_separador();
        if (fim) break;
        p = f + 1;
    }
    menu_abrir(j->x + BORDA + (int)(pos & 0xFFFF), j->y + BARRA_TITULO + (int)(pos >> 16));
    gui_mostrar();
    return 0;
}
