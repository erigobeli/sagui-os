/* navegador.c - o Cipó, o navegador do SaguiOS (H62; o nome e o motor, H82).
 *
 * O nome (sugerido por M4R4G0N no pull request #3, e usado por Robson de
 * Lima Barbosa no #6): o sagui atravessa a mata pulando de cipó em cipó; o
 * navegador atravessa a web pulando de link em link.
 *
 *   ( [ícone] página 1   x )( [ícone] página 2   x ) +           <- as abas
 *   (<) (>) (R) (casa) ( cadeado  endereço ............. estrela )  (:)
 *   [favorito] [favorito] [favorito] ...                       <- favoritos
 *   +------------------------------------------------------------------+-+
 *   |  a página (o motor pinta; a rolagem é só um deslocamento)        |#|
 *   | (balão: o link sob o mouse, "Carregando...", erros)              | |
 *   +------------------------------------------------------------------+-+
 *
 * O "+" abre outra aba na mesma janela; o "x" (ou o botão do meio do
 * mouse) fecha a aba, e fechar a última fecha a janela. A estrela
 * guarda a página nos favoritos (ou tira, se já está), e eles aparecem na
 * barra de baixo. O (:) abre o menu.
 *
 * Endereços aceitos: http://, https://, um caminho de arquivo do SaguiOS
 * (/usb/pagina.html) e as páginas internas sagui:inicio e sagui:favoritos.
 * Sem "http" na frente, vai https://.
 *
 * Como uma página abre (2.1): o corpo chega inteiro (rede.c, com cookies),
 * vira UTF-8, os scripts rodam (js/, que devolve o HTML do DOM depois do
 * "load"), e o motor (motor/) lê o HTML e o CSS, diagrama e pinta. Depois as
 * fontes da web e as imagens chegam, umas poucas por vez, e a cada leva o
 * layout é refeito. Se o usuário fizer alguma coisa no meio, o resto fica
 * para trás. Com JavaScript, a página continua viva: timers, animações e
 * cliques mudam o DOM, e a página é relida no mesmo ponto da rolagem.
 *
 * A janela, a barra, o histórico e os favoritos são da 2.0; a ligação com o
 * motor e o JavaScript (ganchos, cache, iframes, página viva, campos) veio
 * do Cipó do pull request #6 (Robson de Lima Barbosa), adaptada à 2.0.
 *
 * Teclado: setas, PgUp/PgDn, Home/End rolam; Backspace volta; Ctrl+L vai
 * para o endereço; Ctrl+D guarda nos favoritos; F5 e Ctrl+R recarregam;
 * Ctrl+T abre outra aba, Ctrl+W fecha a aba, Ctrl+Tab passa para a
 * próxima e Ctrl+N abre outra janela. Favoritos ficam em /usb/.saguios/favoritos.txt (ou
 * /tmp/favoritos.txt sem pendrive), uma linha "endereço<TAB>título" cada. */
#include "navegador.h"

#define NOME_NAVEGADOR "Cipó"

/* O tamanho da janela muda (H78): começa em 960 x 660. */
static int larg_jan = 960, alt_jan = 660;
#define LARG       larg_jan
#define ALT        alt_jan
#define ABAS       36             /* a faixa das abas */
#define FERR       40             /* os botões e o endereço */
#define FAVS       30             /* a barra dos favoritos */
#define BARRA      (ABAS + FERR + FAVS)
#define ROLAGEM_L  14
#define VISTA_L    (LARG - ROLAGEM_L)
#define VISTA_A    (ALT - BARRA)
#define MAX_URL    MAX_URL_NAV
#define MAX_PAGINA (6 * 1024 * 1024)
#define MAX_RECURSO (6 * 1024 * 1024)
#define MAX_HIST   64

static jan_t *j;
static motor_t *motor;            /* a página atual */
static int altura;                /* altura da página diagramada */
static char url_atual[MAX_URL];
static int rolagem;
static char status[200];
static char status_pronto[sizeof status];   /* "Pronto: ..." da página atual: não aparece no balão */
static char link_sob_mouse[MAX_URL];
static char titulo_js[128];       /* document.title = (vale mais que o <title>) */
static int progresso;             /* 0 = parado; 1 a 100 = a linha azul embaixo do endereço */

typedef struct { char url[MAX_URL]; int rolagem; } passo_hist_t;
static passo_hist_t hist[MAX_HIST];
static int hist_qtd, hist_pos = -1;

/* As abas: a da frente usa as variáveis acima (motor, histórico, rolagem).
 * As de trás guardam o endereço, o histórico, a rolagem e o título; a
 * página delas é aberta de novo quando a aba volta para a frente (o motor,
 * os scripts e as fontes da web são um só por janela). */
#define MAX_ABAS 8
typedef struct {
    char url[MAX_URL];
    char titulo[64];
    int rolagem, hist_qtd, hist_pos;
    passo_hist_t hist[MAX_HIST];
} aba_t;
static aba_t *abas[MAX_ABAS];
static int qtd_abas, aba_atual;
static int aba_sob = -1, fechar_sob = -1;   /* a aba e o "x" sob o mouse */

/* Barra de endereço própria (a caixa_t do kit só guarda 128 letras). */
static struct { char t[MAX_URL]; int cursor, inicio; bool foco, tudo; } ende;   /* tudo: texto todo selecionado */

/* Os favoritos, lidos do arquivo para a barra e para a estrela. */
#define MAX_FAV 24
static struct { char url[MAX_URL]; char titulo[48]; int x, l; } favs[MAX_FAV];
static int qtd_favs;

static void carregar(const char *pedido, bool novo_no_historico, int rolagem_inicial);
static void pasta_config(char *out, int max);

/* ------------------------------------------------------------ cores e ícones */

#define C_FAIXA   0xDEE3E7   /* fundo da faixa da aba */
#define C_ABA_SOB 0xEBEEF1
#define C_SOB     0xE4E8EB   /* botão sob o mouse */
#define C_APERTO  0xD2D8DD
#define C_CAMPO   0xEEF1F3   /* o endereço sem foco */
#define C_ICONE   0x474C52
#define C_APAGADO 0xB4BAC0
#define C_LINHA   0xD5DADF
#define C_FRACO   0x6A737C

/* Os ícones são desenhados por formas (retas, arcos, círculos, polígonos)
 * com a borda suavizada, num quadro de 16 x 16. */
enum { IC_VOLTAR, IC_AVANCAR, IC_RECARREGAR, IC_CASA, IC_ESTRELA, IC_ESTRELA_CHEIA, IC_CADEADO, IC_INFO,
       IC_MUNDO, IC_FOLHA, IC_MAIS, IC_FECHAR, IC_PONTOS, IC_ESPERA };

static float raiz(float x) { float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
static float maior(float a, float b) { return a > b ? a : b; }

/* Quanto o ponto (x, y) está dentro de um traço de largura w: 1 dentro, 0 fora. */
static float cob(float d, float w) { float c = w / 2 + 0.5f - d; return c < 0 ? 0 : c > 1 ? 1 : c; }

static float reta(float x, float y, float ax, float ay, float bx, float by, float w)
{
    float dx = bx - ax, dy = by - ay, t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float ex = x - ax - t * dx, ey = y - ay - t * dy;
    return cob(raiz(ex * ex + ey * ey), w);
}

static float anel(float x, float y, float cx, float cy, float r, float w)
{
    float d = raiz((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r;
    return cob(d < 0 ? -d : d, w);
}

static float disco(float x, float y, float cx, float cy, float r)
{
    float d = raiz((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r;
    return d <= -0.5f ? 1 : d >= 0.5f ? 0 : 0.5f - d;
}

static bool dentro_poligono(float x, float y, const float *p, int n)
{
    bool d = false;
    for (int i = 0, k = n - 1; i < n; k = i++) {
        float yi = p[2 * i + 1], yk = p[2 * k + 1];
        if ((yi > y) != (yk > y) && x < (p[2 * k] - p[2 * i]) * (y - yi) / (yk - yi) + p[2 * i]) d = !d;
    }
    return d;
}

static const float estrela[20] = { 8, 1.8f, 9.6f, 6.1f, 14.4f, 6.4f, 10.6f, 9.3f, 11.9f, 14,
                                    8, 11.4f, 4.1f, 14, 5.4f, 9.3f, 1.6f, 6.4f, 6.4f, 6.1f };

static float forma(int qual, float x, float y)
{
    float c = 0;
    switch (qual) {
    case IC_VOLTAR: case IC_AVANCAR:
        if (qual == IC_AVANCAR) x = 16 - x;
        c = maior(reta(x, y, 3, 8, 13.5f, 8, 1.8f), maior(reta(x, y, 3, 8, 7.6f, 3.4f, 1.8f), reta(x, y, 3, 8, 7.6f, 12.6f, 1.8f)));
        break;
    case IC_RECARREGAR:
        /* o arco aberto em cima, à direita, com a ponta da seta ali */
        if (!(x > 7.6f && y < 8.5f && (8.5f - y) > (x - 8) * 0.6f)) c = anel(x, y, 8, 8.5f, 5.2f, 1.8f);
        { float p[6] = { 7.4f, 0.2f, 12.4f, 3.3f, 7.4f, 6.4f }; if (dentro_poligono(x, y, p, 3)) c = 1; }
        break;
    case IC_CASA:
        c = maior(reta(x, y, 2.2f, 8.2f, 8, 2.6f, 1.6f), reta(x, y, 8, 2.6f, 13.8f, 8.2f, 1.6f));
        c = maior(c, maior(reta(x, y, 4.2f, 7, 4.2f, 13.6f, 1.6f), reta(x, y, 11.8f, 7, 11.8f, 13.6f, 1.6f)));
        c = maior(c, reta(x, y, 4.2f, 13.6f, 11.8f, 13.6f, 1.6f));
        break;
    case IC_ESTRELA: case IC_ESTRELA_CHEIA:
        for (int i = 0; i < 10; i++) {
            int k = (i + 1) % 10;
            c = maior(c, reta(x, y, estrela[2 * i], estrela[2 * i + 1], estrela[2 * k], estrela[2 * k + 1], 1.4f));
        }
        if (qual == IC_ESTRELA_CHEIA && dentro_poligono(x, y, estrela, 10)) c = 1;
        break;
    case IC_CADEADO:
        if (y < 7.5f) c = anel(x, y, 8, 6.5f, 3, 1.5f);
        if (y >= 6.5f && y < 8.5f) c = maior(c, maior(cob(x < 5 ? 5 - x : x - 5, 1.5f), cob(x < 11 ? 11 - x : x - 11, 1.5f)));
        if (x > 3.5f && x < 12.5f && y > 7.5f && y < 14.2f) c = 1;
        break;
    case IC_INFO:
        c = maior(anel(x, y, 8, 8, 6, 1.3f), maior(disco(x, y, 8, 5, 1), reta(x, y, 8, 7.6f, 8, 11.6f, 1.6f)));
        break;
    case IC_MUNDO: {
        c = maior(anel(x, y, 8, 8, 6.2f, 1.2f), reta(x, y, 2, 8, 14, 8, 1.1f));
        float ex = (x - 8) / 2.8f, ey = (y - 8) / 6.2f, e = raiz(ex * ex + ey * ey) - 1;
        c = maior(c, cob((e < 0 ? -e : e) * 2.8f, 1.1f));
        break;
    }
    case IC_FOLHA:
        c = disco(x, y, 4.5f, 4.5f, 8) < disco(x, y, 11.5f, 11.5f, 8) ? disco(x, y, 4.5f, 4.5f, 8) : disco(x, y, 11.5f, 11.5f, 8);
        break;
    case IC_MAIS:
        c = maior(reta(x, y, 8, 3, 8, 13, 1.6f), reta(x, y, 3, 8, 13, 8, 1.6f));
        break;
    case IC_FECHAR:
        c = maior(reta(x, y, 4.5f, 4.5f, 11.5f, 11.5f, 1.4f), reta(x, y, 11.5f, 4.5f, 4.5f, 11.5f, 1.4f));
        break;
    case IC_PONTOS:
        c = maior(disco(x, y, 8, 3.2f, 1.5f), maior(disco(x, y, 8, 8, 1.5f), disco(x, y, 8, 12.8f, 1.5f)));
        break;
    case IC_ESPERA:   /* três quartos de um anel: a página está carregando */
        if (!(x < 8 && y < 8)) c = anel(x, y, 8, 8, 5.5f, 2);
        break;
    }
    return c;
}

static void icone(int x, int y, int qual, uint32_t cor)
{
    static uint32_t px[16 * 16];
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 16; k++) {
            float c = forma(qual, (float)k + 0.5f, (float)i + 0.5f);
            px[i * 16 + k] = (uint32_t)(c * 255 + 0.5f) << 24 | (cor & 0xFFFFFF);
        }
    g_imagem(j, x, y, px, 16, 16);
}

/* ------------------------------------------------------------ desenho */

/* Os botões da barra: as posições dependem da largura da janela. */
enum { BT_VOLTAR, BT_AVANCAR, BT_RECARREGAR, BT_CASA, BT_ESTRELA, BT_MENU, BT_NOVA, QTD_BT };
static struct { int x, y, l, a; } botoes[QTD_BT];
static int sob = -1, apertado = -1;   /* botão sob o mouse e o que está sendo apertado */
static int fav_sob = -1;
#define ENDE_X (4 * 34 + 14)
#define ENDE_Y (ABAS + 4)
#define ENDE_A 32
#define ENDE_L (LARG - ENDE_X - 48)
#define ABA_X  8
#define ABA_Y  6

/* A largura de cada aba: 240, ou menos quando não cabem todas. */
static int aba_l(void)
{
    int n = qtd_abas > 0 ? qtd_abas : 1, l = (LARG - ABA_X - 48) / n;
    return l > 240 ? 240 : l;
}
#define ABA_L aba_l()
/* O "x" de cada aba; nas abas estreitas, só a da frente tem. */
static bool aba_tem_x(int i) { return i == aba_atual || ABA_L >= 90; }
#define ABA_X_FECHAR(i) (ABA_X + (i) * ABA_L + ABA_L - 28)

static void posicionar_botoes(void)
{
    for (int i = 0; i < 4; i++) { botoes[i].x = 8 + i * 34; botoes[i].y = ABAS + 4; botoes[i].l = botoes[i].a = 32; }
    botoes[BT_ESTRELA].x = ENDE_X + ENDE_L - 34; botoes[BT_ESTRELA].y = ENDE_Y + 2; botoes[BT_ESTRELA].l = 30; botoes[BT_ESTRELA].a = 28;
    botoes[BT_MENU].x = LARG - 40; botoes[BT_MENU].y = ABAS + 4; botoes[BT_MENU].l = botoes[BT_MENU].a = 32;
    botoes[BT_NOVA].x = ABA_X + (qtd_abas > 0 ? qtd_abas : 1) * ABA_L + 6; botoes[BT_NOVA].y = 6; botoes[BT_NOVA].l = botoes[BT_NOVA].a = 28;
}

static bool ativo(int i)
{
    if (i == BT_VOLTAR) return hist_pos > 0;
    if (i == BT_AVANCAR) return hist_pos < hist_qtd - 1;
    if (i == BT_ESTRELA) return motor && !comeca_com(url_atual, "sagui:");
    return true;
}

static bool favoritado(void)
{
    for (int i = 0; i < qtd_favs; i++) if (compara_texto(favs[i].url, url_atual) == 0) return true;
    return false;
}

/* O fundo redondo do botão sob o mouse (ou apertado) e o ícone. */
static void desenhar_botao(int i, int qual, uint32_t fundo)
{
    int x = botoes[i].x, y = botoes[i].y, l = botoes[i].l, a = botoes[i].a;
    bool at = ativo(i);
    if (at && (apertado == i || sob == i)) g_ret_redondo(j, x, y, l, a, a / 2, apertado == i ? C_APERTO : C_SOB, apertado == i ? C_APERTO : C_SOB);
    else g_retangulo(j, x, y, l, a, fundo);
    icone(x + (l - 16) / 2, y + (a - 16) / 2, qual, !at ? C_APAGADO : qual == IC_ESTRELA_CHEIA ? COR_SELECAO : C_ICONE);
}

/* A interface do SaguiOS desenha em Latin-1; a página é UTF-8. */
static void utf8_para_sistema(char *s)
{
    int k = 0, n = (int)tam_texto(s);
    for (int i = 0; i < n;) {
        uint32_t cp;
        i += utf8_ler(s + i, n - i, &cp);
        s[k++] = cp < 0x100 ? (char)cp : cp == 0x2013 || cp == 0x2014 ? '-' : cp == 0x2019 || cp == 0x2018 ? '\'' : '?';
    }
    s[k] = 0;
}

static void titulo_da_pagina(char *t, int max)
{
    copia_texto(t, titulo_js[0] ? titulo_js : motor && motor_titulo(motor)[0] ? motor_titulo(motor) : url_atual, (size_t)max);
    utf8_para_sistema(t);
}

static void desenhar_abas(void)
{
    g_retangulo(j, 0, 0, LARG, ABAS, C_FAIXA);
    int l = ABA_L;
    for (int i = 0; i < qtd_abas; i++) {
        int x = ABA_X + i * l;
        bool frente = i == aba_atual;
        uint32_t fundo = C_FAIXA;
        if (frente) {
            /* a da frente: branca, de cantos redondos em cima, emendada na barra de baixo */
            fundo = COR_BRANCO;
            g_ret_redondo(j, x, ABA_Y, l, ABAS, 8, fundo, fundo);
        } else if (aba_sob == i) {
            fundo = C_ABA_SOB;
            g_ret_redondo(j, x + 2, ABA_Y + 2, l - 4, ABAS - ABA_Y - 6, 8, fundo, fundo);
        } else if (i + 1 != aba_atual && i + 1 < qtd_abas && aba_sob != i + 1)
            g_retangulo(j, x + l - 1, ABA_Y + 8, 1, 16, C_APAGADO);   /* o risco entre duas abas de trás */
        const char *u = frente ? url_atual : abas[i]->url;
        int ic = frente && progresso ? IC_ESPERA : comeca_com(u, "sagui:") ? IC_FOLHA : IC_MUNDO;
        icone(x + 12, ABA_Y + 9, ic, ic == IC_FOLHA ? COR_DESTAQUE : ic == IC_ESPERA ? COR_SELECAO : C_FRACO);
        char t[128];
        if (frente) titulo_da_pagina(t, sizeof t);
        else copia_texto(t, abas[i]->titulo, sizeof t);
        int cabe = (l - 12 - 16 - 8 - (aba_tem_x(i) ? 34 : 4)) / LETRA_L;
        if (cabe > 0) g_texto_n(j, x + 36, ABA_Y + 9, t, cabe, frente ? COR_TEXTO : C_ICONE);
        if (aba_tem_x(i)) {
            int fx = ABA_X_FECHAR(i), fy = ABA_Y + 6;
            if (fechar_sob == i) g_ret_redondo(j, fx, fy, 20, 20, 10, frente ? C_SOB : C_APERTO, frente ? C_SOB : C_APERTO);
            icone(fx + 2, fy + 2, IC_FECHAR, C_ICONE);
        }
    }
    desenhar_botao(BT_NOVA, IC_MAIS, C_FAIXA);
}

/* O endereço sem foco: sem o "https://", o servidor escuro e o resto apagado. */
static void desenhar_endereco(void)
{
    int x0 = ENDE_X, y0 = ENDE_Y, l0 = ENDE_L;
    if (ende.foco) {
        g_ret_redondo(j, x0, y0, l0, ENDE_A, ENDE_A / 2, COR_SELECAO, COR_SELECAO);
        g_ret_redondo(j, x0 + 2, y0 + 2, l0 - 4, ENDE_A - 4, ENDE_A / 2 - 2, COR_BRANCO, COR_BRANCO);
    } else g_ret_redondo(j, x0, y0, l0, ENDE_A, ENDE_A / 2, C_CAMPO, C_CAMPO);
    int ic = comeca_com(url_atual, "https://") ? IC_CADEADO : comeca_com(url_atual, "sagui:") ? IC_FOLHA : IC_INFO;
    icone(x0 + 12, y0 + 8, ic, ic == IC_FOLHA ? COR_DESTAQUE : C_ICONE);
    int tx = x0 + 38, ty = y0 + 8;
    int fim_texto = x0 + l0 - 40;
    int cabe = (fim_texto - tx) / LETRA_L;
    if (!ende.foco) {
        const char *s = ende.t;
        if (comeca_com(s, "https://")) s += 8;
        const char *barra = acha_char(s, '/');
        int n_serv = barra ? (int)(barra - s) : (int)tam_texto(s);
        if (comeca_com(ende.t, "sagui:") || ende.t[0] == '/') n_serv = (int)tam_texto(s);
        int m = n_serv < cabe ? n_serv : cabe;
        g_texto_n(j, tx, ty, s, m, COR_TEXTO);
        if (cabe > m) g_texto_n(j, tx + m * LETRA_L, ty, s + m, cabe - m, C_FRACO);
    } else {
        int n = (int)tam_texto(ende.t);
        if (ende.cursor < ende.inicio) ende.inicio = ende.cursor;
        if (ende.cursor > ende.inicio + cabe) ende.inicio = ende.cursor - cabe;
        int m = n - ende.inicio < cabe ? n - ende.inicio : cabe;
        if (ende.tudo && m > 0) g_retangulo(j, tx - 1, ty - 1, m * LETRA_L + 2, LETRA_A + 2, 0xC8DBF8);
        if (m > 0) g_texto_n(j, tx, ty, ende.t + ende.inicio, m, COR_TEXTO);
        g_retangulo(j, tx + (ende.cursor - ende.inicio) * LETRA_L, ty, 1, LETRA_A, COR_TEXTO);
    }
    desenhar_botao(BT_ESTRELA, favoritado() ? IC_ESTRELA_CHEIA : IC_ESTRELA, ende.foco ? COR_BRANCO : C_CAMPO);
}

static void desenhar_favoritos(void)
{
    int y = ABAS + FERR;
    g_retangulo(j, 0, y, LARG, FAVS, COR_BRANCO);
    if (!qtd_favs) {
        g_texto(j, 14, y + 6, "Para guardar uma página aqui, clique na estrela do endereço.", C_FRACO);
    }
    int x = 8;
    for (int i = 0; i < qtd_favs; i++) {
        int n = (int)tam_texto(favs[i].titulo);
        if (n > 22) n = 22;
        int l = 16 + 6 + n * LETRA_L + 16;
        if (x + l > LARG - 8) { favs[i].l = 0; continue; }
        favs[i].x = x; favs[i].l = l;
        if (fav_sob == i) g_ret_redondo(j, x, y + 2, l, FAVS - 6, (FAVS - 6) / 2, C_SOB, C_SOB);
        icone(x + 8, y + 5, IC_MUNDO, C_FRACO);
        g_texto_n(j, x + 8 + 22, y + 5, favs[i].titulo, n, COR_TEXTO);
        x += l + 2;
    }
    g_retangulo(j, 0, BARRA - 1, LARG, 1, C_LINHA);
}

static void desenhar_barra(void)
{
    desenhar_abas();
    g_retangulo(j, 0, ABAS, LARG, FERR, COR_BRANCO);
    desenhar_botao(BT_VOLTAR, IC_VOLTAR, COR_BRANCO);
    desenhar_botao(BT_AVANCAR, IC_AVANCAR, COR_BRANCO);
    desenhar_botao(BT_RECARREGAR, IC_RECARREGAR, COR_BRANCO);
    desenhar_botao(BT_CASA, IC_CASA, COR_BRANCO);
    desenhar_endereco();
    desenhar_botao(BT_MENU, IC_PONTOS, COR_BRANCO);
    desenhar_favoritos();
    if (progresso) g_retangulo(j, 0, BARRA - 2, LARG * progresso / 100, 2, COR_SELECAO);
}

static void mostrar_barra(void) { desenhar_barra(); jan_mostrar_area(j, 0, 0, LARG, BARRA); }

/* O balão do status, no canto de baixo, por cima da página. O que fica
 * embaixo dele é guardado, para ele sumir sem pintar a página de novo. */
#define BALAO_A 24
#define BALAO_MAX 720
static uint32_t sob_balao[BALAO_MAX * BALAO_A];
static int balao_l;                 /* 0 = sem balão na tela */

static void apagar_balao(void)
{
    if (!balao_l) return;
    int y0 = ALT - BALAO_A;
    for (int y = 0; y < BALAO_A; y++) memcpy(j->px + (y0 + y) * j->larg, sob_balao + y * BALAO_MAX, (size_t)balao_l * 4);
    balao_l = 0;
}

static void desenhar_status(void)
{
    const char *t = link_sob_mouse[0] ? link_sob_mouse : compara_texto(status, status_pronto) == 0 ? "" : status;
    if (!t[0]) return;
    int max_l = VISTA_L * 2 / 3 < BALAO_MAX ? VISTA_L * 2 / 3 : BALAO_MAX;
    int n = (int)tam_texto(t), cabe = (max_l - 16) / LETRA_L;
    if (n > cabe) n = cabe;
    int l = n * LETRA_L + 16, y0 = ALT - BALAO_A;
    for (int y = 0; y < BALAO_A; y++) memcpy(sob_balao + y * BALAO_MAX, j->px + (y0 + y) * j->larg, (size_t)l * 4);
    balao_l = l;
    g_retangulo(j, 0, y0, l, BALAO_A, C_LINHA);
    g_retangulo(j, 0, y0 + 1, l - 1, BALAO_A - 1, 0xF6F7F8);
    g_texto_n(j, 8, y0 + 4, t, n, COR_TEXTO);
}

/* Troca o balão: apaga o velho, desenha o novo e mostra só aquele canto. */
static void trocar_balao(void)
{
    int antes = balao_l;
    apagar_balao();
    desenhar_status();
    int l = antes > balao_l ? antes : balao_l;
    if (l) jan_mostrar_area(j, 0, ALT - BALAO_A, l, BALAO_A);
}

static void desenhar_rolagem(void)
{
    int x = VISTA_L, y = BARRA;
    g_retangulo(j, x, y, ROLAGEM_L, VISTA_A, 0xF6F7F8);
    if (altura <= VISTA_A) return;
    int a = VISTA_A * VISTA_A / altura;
    if (a < 24) a = 24;
    int ya = y + (int)((int)rolagem * (VISTA_A - a) / (altura - VISTA_A));
    g_ret_redondo(j, x + 3, ya + 2, ROLAGEM_L - 6, a - 4, (ROLAGEM_L - 6) / 2, 0xBCC2C8, 0xBCC2C8);
}

static void limitar_rolagem(void)
{
    int max = altura - VISTA_A;
    if (rolagem > max) rolagem = max;
    if (rolagem < 0) rolagem = 0;
}

/* Só a página e a barra de rolagem (e o balão, que fica por cima). */
static void desenhar_pagina(void)
{
    balao_l = 0;
    if (motor) motor_pintar(motor, j->px + BARRA * j->larg, VISTA_L, j->larg, rolagem, VISTA_A);
    else g_retangulo(j, 0, BARRA, VISTA_L, VISTA_A, COR_BRANCO);
    desenhar_rolagem();
    desenhar_status();
}

static void desenhar_tudo(void)
{
    desenhar_pagina();
    desenhar_barra();
    jan_mostrar(j);
}

static void definir_status(const char *t)
{
    copia_texto(status, t, sizeof status);
    trocar_balao();
}

static void definir_progresso(int p)
{
    progresso = p;
    mostrar_barra();
}

static void definir_endereco(const char *u)
{
    copia_texto(ende.t, u, sizeof ende.t);
    ende.cursor = (int)tam_texto(ende.t);
    ende.inicio = 0;
}

/* Lê os favoritos do arquivo (endereço<TAB>título por linha). */
static void ler_favoritos(void)
{
    qtd_favs = 0;
    char caminho[64];
    pasta_config(caminho, sizeof caminho);
    junta_texto(caminho, "/favoritos.txt", sizeof caminho);
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return;
    static char buf[16 * 1024];
    int n = ler(fd, buf, sizeof buf - 1);
    fechar(fd);
    if (n <= 0) return;
    buf[n] = 0;
    for (char *p = buf; *p && qtd_favs < MAX_FAV;) {
        char *fim = acha_char(p, '\n');
        if (fim) *fim = 0;
        char *tab = acha_char(p, '\t');
        if (tab) *tab = 0;
        if (*p) {
            copia_texto(favs[qtd_favs].url, p, MAX_URL);
            const char *t = tab && tab[1] ? tab + 1 : p;
            if (comeca_com(t, "https://")) t += 8;
            copia_texto(favs[qtd_favs].titulo, t, sizeof favs[qtd_favs].titulo);
            qtd_favs++;
        }
        if (!fim) break;
        p = fim + 1;
    }
}

/* ------------------------------------------------------------ texto da página */

/* O motor trabalha em UTF-8. charset: 1 UTF-8, 0 Latin-1/Windows-1252, -1 não
 * sabe (UTF-8 válido fica como está; senão é Windows-1252). Do #6, com o
 * charset declarado da 2.0. Devolve o bloco novo (o antigo é liberado). */
static char *para_utf8(char *s, uint32_t *tam, int charset)
{
    int n = (int)*tam;
    if (charset == 1) return s;
    if (charset < 0) {
        bool valido = true;
        for (int i = 0; i < n;) {
            uint32_t cp; int k = utf8_ler(s + i, n - i, &cp);
            if (k == 1 && (uint8_t)s[i] >= 0x80) { valido = false; break; }
            i += k;
        }
        if (valido) return s;
    }
    int cresce = 0;
    for (int i = 0; i < n; i++) if ((uint8_t)s[i] >= 0x80) cresce += (uint8_t)s[i] < 0xA0 ? 2 : 1;
    if (!cresce) return s;
    char *d = aloca((size_t)n + (size_t)cresce + 1);
    if (!d) return s;
    static const uint16_t cp1252[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F,
                                         0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 };
    int k = 0;
    for (int i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        k += utf8_por(d + k, c >= 0x80 && c < 0xA0 ? cp1252[c - 0x80] : c);
    }
    d[k] = 0;
    libera(s);
    *tam = (uint32_t)k;
    return d;
}

static bool sem_caixa(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        char x = a[i] >= 'A' && a[i] <= 'Z' ? (char)(a[i] + 32) : a[i];
        if (x != b[i]) return false;
    }
    return true;
}

/* "charset=" num texto (Content-Type ou o começo do HTML): 1 UTF-8, 0 outro, -1 nada. */
static int charset_em(const char *t, int n)
{
    for (int i = 0; i + 8 < n; i++)
        if (sem_caixa(t + i, "charset=", 8)) {
            const char *v = t + i + 8;
            while (*v == '"' || *v == '\'' || *v == ' ') v++;
            return sem_caixa(v, "utf-8", 5) || sem_caixa(v, "utf8", 4) ? 1 : 0;
        }
    return -1;
}

/* ------------------------------------------------------------ ganchos do motor e do JavaScript */

/* Cache dos recursos da página (CSS, imagens, scripts): a página viva é
 * relida a cada mudança do DOM e não deve baixar tudo de novo. Limpo ao
 * navegar. (#6) */
#define CACHE_N 48
#define CACHE_MAX (6u << 20)
static struct { char *url; uint8_t *d; unsigned n; } cache[CACHE_N];
static int qtd_cache; static unsigned cache_total;

static void cache_limpar(void)
{
    for (int i = 0; i < qtd_cache; i++) { libera(cache[i].url); libera(cache[i].d); }
    qtd_cache = 0; cache_total = 0;
}

static uint8_t *copia_bloco(const uint8_t *d, unsigned n)
{
    uint8_t *c = aloca(n + 1);
    if (c) { memcpy(c, d, n); c[n] = 0; }
    return c;
}

/* Para onde vai o tempo de uma página (registrado em "navegador: tempos"). */
static struct { unsigned rede, n_rede, decod, n_decod, diag, n_diag, desenho; } perf;

/* Evento que chegou enquanto a página carregava (tratado depois, no laço). */
static evento_t evento_guardado;
static bool tem_evento_guardado;

static uint8_t *baixar_sem_cache(const char *u, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax)
{
    char end[MAX_URL];
    endereco_resolver(url_atual, u, end, sizeof end);
    if (comeca_com(end, "sagui:") || comeca_com(end, "javascript:")) return NULL;
    char msg[200];
    formatar_texto(msg, sizeof msg, "Baixando %s", end);
    definir_status(msg);
    rede_resposta_t r;
    unsigned t0 = tempo_ms();
    int s = rede_buscar(end, &r, MAX_RECURSO, cab, cmax);
    perf.rede += tempo_ms() - t0; perf.n_rede++;
    if (codigo) *codigo = s < 0 ? 0 : s;
    if (final) copia_texto(final, r.final, (size_t)fmax);
    if (s < 0 || !r.d) { registrar("navegador: recurso %s: %s", end, r.erro[0] ? r.erro : "veio vazio"); libera(r.d); return NULL; }
    *n = r.n;
    return r.d;
}

/* Imagens que chegaram juntas (pipelining, H129), esperando o motor pedir. */
#define ADIANTADAS 16
static struct { char *url; uint8_t *d; unsigned n; } adiantadas[ADIANTADAS];

static void soltar_adiantadas(void)
{
    for (int i = 0; i < ADIANTADAS; i++) {
        libera(adiantadas[i].url);
        libera(adiantadas[i].d);
        adiantadas[i].url = NULL;
        adiantadas[i].d = NULL;
    }
}

/* Busca juntas as próximas imagens do mesmo servidor, se nenhuma está
 * esperando: uma ida e volta pela rede para até 16, em vez de uma para cada. */
static void adiantar_imagens(void)
{
    for (int i = 0; i < ADIANTADAS; i++) if (adiantadas[i].url) return;
    const char *pedidas[ADIANTADAS];
    int q = motor_imagens_proximas(motor, pedidas, ADIANTADAS);
    static char ends[ADIANTADAS][MAX_URL];
    const char *urls[ADIANTADAS];
    int k = 0;
    for (int i = 0; i < q; i++) {
        endereco_resolver(url_atual, pedidas[i], ends[k], MAX_URL);
        if (!comeca_com(ends[k], "http://") && !comeca_com(ends[k], "https://")) continue;
        bool repetida = false;
        for (int c = 0; c < qtd_cache && !repetida; c++) if (compara_texto(cache[c].url, ends[k]) == 0) repetida = true;
        for (int c = 0; c < k && !repetida; c++) if (compara_texto(ends[c], ends[k]) == 0) repetida = true;
        if (repetida) continue;
        /* um servidor por vez: o da primeira; os outros ficam para a próxima leva */
        if (k > 0) {
            const char *a = acha_char(ends[0] + 8, '/'), *b = acha_char(ends[k] + 8, '/');
            int la = a ? (int)(a - ends[0]) : (int)tam_texto(ends[0]), lb = b ? (int)(b - ends[k]) : (int)tam_texto(ends[k]);
            if (la != lb || compara_texto_n(ends[0], ends[k], (size_t)la) != 0) continue;
        }
        urls[k] = ends[k];
        k++;
    }
    if (k < 2) return;   /* uma só: o caminho de sempre */
    static rede_resposta_t rs[ADIANTADAS];
    unsigned t0 = tempo_ms();
    int prontas = rede_lote(urls, k, rs, MAX_RECURSO);
    perf.rede += tempo_ms() - t0; perf.n_rede += (unsigned)prontas;
    for (int i = 0; i < k; i++) {
        if (i < prontas && rs[i].d && rs[i].situacao >= 200 && rs[i].situacao < 300) {
            int t = (int)tam_texto(urls[i]);
            adiantadas[i].url = aloca((unsigned)t + 1);
            if (adiantadas[i].url) {
                memcpy(adiantadas[i].url, urls[i], (unsigned)t + 1);
                adiantadas[i].d = rs[i].d;
                adiantadas[i].n = rs[i].n;
                continue;
            }
        }
        libera(rs[i].d);
    }
}

/* Recurso da página (CSS, imagem, fonte, script): só os que deram certo. */
static uint8_t *baixar_recurso(const char *u, unsigned *n)
{
    char end[MAX_URL];
    endereco_resolver(url_atual, u, end, sizeof end);
    for (int i = 0; i < ADIANTADAS; i++)
        if (adiantadas[i].url && compara_texto(adiantadas[i].url, end) == 0) {
            uint8_t *d = adiantadas[i].d;
            *n = adiantadas[i].n;
            libera(adiantadas[i].url);
            adiantadas[i].url = NULL;
            adiantadas[i].d = NULL;
            return d;
        }
    for (int i = 0; i < qtd_cache; i++)
        if (compara_texto(cache[i].url, end) == 0) { *n = cache[i].n; return copia_bloco(cache[i].d, cache[i].n); }
    int codigo = 0;
    uint8_t *d = baixar_sem_cache(end, n, &codigo, NULL, 0, NULL, 0);
    if (d && codigo >= 400) { libera(d); return NULL; }
    if (d && qtd_cache < CACHE_N && cache_total + *n <= CACHE_MAX) {
        int k = (int)tam_texto(end);
        char *cu = aloca((unsigned)k + 1);
        uint8_t *cd = copia_bloco(d, *n);
        if (cu && cd) { memcpy(cu, end, (unsigned)k + 1); cache[qtd_cache].url = cu; cache[qtd_cache].d = cd; cache[qtd_cache].n = *n; qtd_cache++; cache_total += *n; }
        else { libera(cu); libera(cd); }
    }
    return d;
}

/* fetch/XHR: corpo de qualquer código, com código, cabeçalhos e URL final. */
static uint8_t *js_resposta(const char *u, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax)
{
    *codigo = 0; cab[0] = 0; final[0] = 0;
    return baixar_sem_cache(u, n, codigo, cab, cmax, final, fmax);
}

static void js_titulo(const char *t) { copia_texto(titulo_js, t, sizeof titulo_js); }
static void js_alerta(const char *t)
{
    registrar("navegador: alert %s", t);
    char msg[200];
    formatar_texto(msg, sizeof msg, "Alerta da página: %s", t);
    utf8_para_sistema(msg);
    copia_texto(status, msg, sizeof status);
}

/* H96: imagens já decodificadas nesta página. Quando um script muda o DOM,
 * a página é lida de novo num motor novo, que pedia todas as imagens outra
 * vez (2,8 s por vez no quanticbit.qzz.io, rolando). A chave é o conteúdo
 * (tamanho + hash); falhas também ficam, para não tentar de novo. O motor
 * libera os pixels que recebe, então sai sempre uma cópia. */
#define IMGC_N   48
#define IMGC_MAX (16u << 20)
static struct { unsigned n, hash; uint32_t *px; int l, a; } imgc[IMGC_N];
static int qtd_imgc; static unsigned imgc_total;

static void imgc_limpar(void)
{
    for (int i = 0; i < qtd_imgc; i++) libera(imgc[i].px);
    qtd_imgc = 0; imgc_total = 0;
}

static unsigned hash_bloco(const uint8_t *d, unsigned n) { unsigned x = 2166136261u; for (unsigned i = 0; i < n; i++) x = (x ^ d[i]) * 16777619u; return x; }

static bool decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a)
{
    unsigned h = hash_bloco(d, n);
    for (int i = 0; i < qtd_imgc; i++) {
        if (imgc[i].n != n || imgc[i].hash != h) continue;
        if (!imgc[i].px) return false;                    /* já falhou antes */
        size_t t = (size_t)imgc[i].l * (size_t)imgc[i].a * 4;
        uint32_t *c = aloca(t);
        if (!c) return false;
        memcpy(c, imgc[i].px, t);
        *px = c; *l = imgc[i].l; *a = imgc[i].a;
        return true;
    }
    unsigned t0 = tempo_ms();
    bool ok = imagem_decodificar(d, n, px, l, a);
    perf.decod += tempo_ms() - t0; perf.n_decod++;
    if (ok) registrar("navegador: imagem %dx%d", *l, *a);
    else registrar("navegador: imagem nao decodificou (%u bytes)", n);
    if (qtd_imgc < IMGC_N) {
        size_t t = ok ? (size_t)*l * (size_t)*a * 4 : 0;
        uint32_t *c = NULL;
        if (ok && imgc_total + t <= IMGC_MAX && (c = aloca(t))) memcpy(c, *px, t);
        if (!ok || c) { imgc[qtd_imgc].n = n; imgc[qtd_imgc].hash = h; imgc[qtd_imgc].px = c; imgc[qtd_imgc].l = ok ? *l : 0; imgc[qtd_imgc].a = ok ? *a : 0; qtd_imgc++; imgc_total += (unsigned)t; }
    }
    return ok;
}

static const motor_ganchos_t ganchos_motor = { baixar_recurso, decodificar, tela2d_pixels };

/* Layout pedido por um script (medidas): um motor à parte diagrama o DOM
 * atual com o CSS em cache e devolve as caixas. Fica vivo para o
 * getComputedStyle até o próximo pedido ou a próxima página. (#6) */
static motor_t *medidor;
static char *medidor_html;     /* H96: o DOM que o medidor diagramou por último */
static int medidor_n = -1;
static int js_layout(const char *html, int n, int *caixas, int qtd)
{
    /* Scripts pedem medidas muitas vezes sem mudar o DOM (rolagem,
     * IntersectionObserver): com o mesmo HTML, as caixas são as mesmas. */
    if (medidor && medidor_html && n == medidor_n && memcmp(html, medidor_html, (size_t)n) == 0) {
        motor_caixas(medidor, caixas, qtd);
        return 1;
    }
    if (medidor_html) { libera(medidor_html); medidor_html = NULL; }
    medidor_n = -1;
    medidor_html = aloca((size_t)n + 1);
    if (medidor_html) { memcpy(medidor_html, html, (size_t)n); medidor_n = n; }
    if (medidor) motor_fim(medidor);
    medidor = motor_novo(&ganchos_motor);
    if (!medidor) return 0;
    motor_ler(medidor, html, n, url_atual);
    motor_layout(medidor, VISTA_L, VISTA_A);
    motor_caixas(medidor, caixas, qtd);
    return 1;
}
static bool js_estilo(int k, const char *prop, char *o, int max) { return medidor && motor_estilo_de(medidor, k, prop, o, max); }
static struct fonte *js_fonte(const char *fam, int peso, int ital, int larg) { return medidor ? motor_fonte_nomeada(medidor, fam, peso, ital, larg) : NULL; }

/* iframes: cada quadro tem o seu motor (para pintar e para achar o clique). (#6) */
#define MAX_QM 16
static struct { int tela; motor_t *m; } qm[MAX_QM];
static int js_quadro_pintar(int tela, const char *html, int n, const char *url, int l, int a)
{
    int k = 0;
    while (k < MAX_QM && !(qm[k].m && qm[k].tela == tela)) k++;
    if (k == MAX_QM) { k = 0; while (k < MAX_QM && qm[k].m) k++; }
    if (k == MAX_QM) return 0;
    if (qm[k].m) motor_fim(qm[k].m);
    motor_t *m = motor_novo(&ganchos_motor);
    qm[k].m = m; qm[k].tela = tela;
    if (!m) return 0;
    motor_ler(m, html, n, url);
    motor_layout(m, l, a);
    while (motor_imagens(m, 8)) motor_layout(m, l, a);
    int tl, ta;
    uint32_t *px = tela2d_px_escrita(tela, &tl, &ta);
    if (!px) return 0;
    int w = l < tl ? l : tl, h = a < ta ? a : ta;
    motor_pintar(m, px, w, tl, 0, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px[y * tl + x] |= 0xFF000000u;
    return 1;
}
static int js_quadro_alvo(int tela, int x, int y)
{
    for (int k = 0; k < MAX_QM; k++) {
        if (!qm[k].m || qm[k].tela != tela) continue;
        const motor_alvo_t *a = motor_alvo(qm[k].m, x, y);
        if (!a) return -1;
        if (a->js >= 0) return a->js;
        if (a->href && comeca_com(a->href, "sagui-js:")) return texto_para_int(a->href + 9);
        return -1;
    }
    return -1;
}
static void quadros_esquecer(void) { for (int k = 0; k < MAX_QM; k++) { if (qm[k].m) motor_fim(qm[k].m); qm[k].m = NULL; } }

static bool tem_script(const char *h, uint32_t n)
{
    for (uint32_t i = 0; i + 7 < n; i++) if (h[i] == '<' && sem_caixa(h + i + 1, "script", 6)) return true;
    return false;
}

/* Roda os scripts: devolve o HTML do DOM depois do load (ou o mesmo). */
static char *rodar_scripts(char *h, uint32_t *n)
{
    static const js_hooks_t ganchos = { baixar_recurso, js_titulo, js_alerta, js_resposta, js_layout, js_estilo, js_fonte,
                                        js_quadro_pintar, js_quadro_alvo };
    quadros_esquecer();
    if (!tem_script(h, *n)) return h;
    definir_status("Rodando os scripts da página...");
    unsigned t0 = tempo_ms();
    int t = 0;
    js_janela(VISTA_L, VISTA_A, 0, 0);
    char *novo = js_pagina(h, (int)*n, url_atual, &ganchos, &t);
    registrar("navegador: scripts em %u ms (%s)", tempo_ms() - t0, novo ? "o DOM mudou" : "nada mudou");
    if (!novo) return h;
    libera(h);
    *n = (uint32_t)t;
    return novo;
}

/* ------------------------------------------------------------ páginas internas */

typedef struct { char *d; uint32_t n, cap; } texto_t;

static void acrescentar(texto_t *r, const char *s, unsigned n)
{
    if (r->n + n + 1 > r->cap) {
        uint32_t cap = r->cap ? r->cap * 2 : 4096;
        while (cap < r->n + n + 1) cap *= 2;
        char *novo = realoca(r->d, cap);
        if (!novo) return;
        r->d = novo; r->cap = cap;
    }
    memcpy(r->d + r->n, s, n);
    r->n += n;
    r->d[r->n] = 0;
}
#define T(r, s) acrescentar(r, s, (unsigned)tam_texto(s))

static void html_escapar(texto_t *r, const char *s, int n)
{
    for (int i = 0; i < n && s[i]; i++) {
        if (s[i] == '<') T(r, "&lt;");
        else if (s[i] == '&') T(r, "&amp;");
        else if (s[i] == '"') T(r, "&quot;");
        else acrescentar(r, s + i, 1);
    }
}

static void pasta_config(char *out, int max)
{
    info_arquivo_t i;
    copia_texto(out, info_arquivo("/usb/.saguios", &i) >= 0 ? "/usb/.saguios" : "/tmp", (size_t)max);
}

static void pagina_interna(const char *url, texto_t *r)
{
    T(r, "<html><head><meta charset=\"iso-8859-1\">");
    if (compara_texto(url, "sagui:favoritos") == 0) {
        T(r, "<title>Favoritos</title></head><body><h1>Favoritos</h1>");
        char caminho[64];
        pasta_config(caminho, sizeof caminho);
        junta_texto(caminho, "/favoritos.txt", sizeof caminho);
        rede_resposta_t f;
        int n = 0;
        if (rede_buscar(caminho, &f, 128 * 1024, NULL, 0) == 200 && f.d) {
            T(r, "<ul>");
            for (char *p = (char *)f.d; *p;) {
                char *fim = acha_char(p, '\n');
                if (fim) *fim = 0;
                char *tab = acha_char(p, '\t');
                if (tab) *tab = 0;
                if (*p) {
                    T(r, "<li><a href=\"");
                    html_escapar(r, p, MAX_URL);
                    T(r, "\">");
                    html_escapar(r, tab && tab[1] ? tab + 1 : p, MAX_URL);
                    T(r, "</a></li>");
                    n++;
                }
                if (!fim) break;
                p = fim + 1;
            }
            T(r, "</ul>");
        }
        libera(f.d);
        if (!n) T(r, "<p>Nenhum favorito ainda. Abra uma página e clique na estrela do endereço.</p>");
        T(r, "<p><small>Guardados em ");
        html_escapar(r, caminho, 64);
        T(r, "</small></p>");
    } else {
        T(r, "<title>" NOME_NAVEGADOR "</title>"
             "<style>body{font-family:sans-serif;max-width:760px;margin:24px auto;padding:0 16px;color:#1e2328}"
             "h1{color:#0b7a3e}a{color:#1a4fb0}.dica{background:#eef6f0;border-left:4px solid #0b7a3e;padding:8px 12px}</style>"
             "</head><body><h1>" NOME_NAVEGADOR "</h1>"
             "<p><i>O sagui atravessa a mata pulando de cipó em cipó; o Cipó atravessa a web pulando de link em link.</i></p>"
             "<p>O navegador do SaguiOS: HTML e CSS (flex, grid, tabelas), JavaScript, fontes TrueType, "
             "imagens PNG, JPEG, GIF e WebP, Canvas e SVG, e HTTPS com o TLS 1.3 do próprio sistema.</p>"
             "<h2>Para começar</h2><ul>"
             "<li><a href=\"https://pt.wikipedia.org/wiki/Sagui\">Wikipédia: Sagui</a></li>"
             "<li><a href=\"https://pt.wikipedia.org/wiki/Wikip%C3%A9dia:P%C3%A1gina_principal\">Wikipédia em português</a></li>"
             "<li><a href=\"https://example.com/\">example.com</a></li>"
             "<li><a href=\"https://raw.githubusercontent.com/erigobeli/sagui-os/main/README.md\">README do SaguiOS (texto)</a></li>"
             "<li><a href=\"sagui:favoritos\">Favoritos</a></li>"
             "</ul>"
             /* Sites leves, sem HTTPS (ideia do pull request #3, de M4R4G0N): abrem
              * mesmo quando o TLS não consegue (relógio errado, certificado raro). */
             "<h2>Sites leves (sem HTTPS)</h2><ul>"
             "<li><a href=\"http://info.cern.ch/\">info.cern.ch</a>: o primeiro site da web, de 1991</li>"
             "<li><a href=\"http://68k.news/\">68k.news</a>: notícias em páginas simples, para computadores antigos</li>"
             "<li><a href=\"http://neverssl.com/\">neverssl.com</a>: para testar a conexão sem HTTPS</li>"
             "</ul><h2>Procurar na Wikipédia</h2>"
             "<form action=\"https://pt.wikipedia.org/w/index.php\"><input type=\"hidden\" name=\"title\" value=\"Especial:Pesquisar\">"
             "<input type=\"search\" name=\"search\"> <input type=\"submit\" value=\"Procurar\"></form>"
             /* Busca na web: o Google bloqueia navegadores que ele não conhece
              * ("tráfego incomum"); o DuckDuckGo Lite responde em HTML simples,
              * sem JavaScript. */
             "<h2>Procurar na web</h2>"
             "<form action=\"https://lite.duckduckgo.com/lite/\" method=\"post\">"
             "<input type=\"text\" name=\"q\"> <input type=\"submit\" value=\"Procurar no DuckDuckGo\"></form>"
             "<h2>Dicas</h2><div class=\"dica\"><ul><li>Setas, PgUp, PgDn, Home e End rolam a página.</li>"
             "<li>Backspace volta para a página anterior; Ctrl+L vai para o endereço.</li>"
             "<li>Também abre arquivos do SaguiOS, como <a href=\"/leia-me.txt\">/leia-me.txt</a>.</li></ul></div>");
    }
    T(r, "</body></html>");
}

static void pagina_erro(texto_t *r, const char *url, const char *erro)
{
    T(r, "<html><head><meta charset=\"iso-8859-1\"><title>Não deu para abrir</title></head><body><h1>Não deu para abrir a página</h1><p>");
    html_escapar(r, url, MAX_URL);
    T(r, "</p><p><b>Motivo:</b> ");
    html_escapar(r, erro, 200);
    T(r, "</p><p><a href=\"sagui:inicio\">Página inicial</a></p></body></html>");
}

/* ------------------------------------------------------------ carregar */

static void guardar_rolagem(void)
{
    if (hist_pos >= 0) hist[hist_pos].rolagem = rolagem;
}

static unsigned custo_diagramar;   /* ms da última diagramação */
static bool falta_diagramar;       /* chegaram imagens que ainda não estão na tela */

static void diagramar(void)
{
    if (!motor) { altura = 0; return; }
    unsigned t0 = tempo_ms();
    altura = motor_layout(motor, VISTA_L, VISTA_A);
    custo_diagramar = tempo_ms() - t0;
    perf.diag += custo_diagramar; perf.n_diag++;
    limitar_rolagem();
}

/* Lê o HTML (UTF-8) num motor novo e diagrama. */
static void ler_pagina(const char *h, int n)
{
    soltar_adiantadas();
    falta_diagramar = false;
    if (motor) motor_fim(motor);
    motor = motor_novo(&ganchos_motor);
    if (!motor) { altura = 0; return; }
    motor_ler(motor, h, n, url_atual);
    diagramar();
}

/* Fontes da web e imagens, umas poucas por vez. Para se chegar um evento.
 * Diagramar uma página grande custa caro (a do Gato, na Wikipédia, tem 19
 * mil nós: uns 400 ms), então, enquanto as imagens chegam, a página é
 * diagramada de novo só depois de REDIAGRAMAR_MS ou de 4 vezes o que a
 * última diagramação custou (o que for maior), e uma vez no fim, em vez de
 * depois de cada par de imagens (H129). Assim diagramar fica em no máximo
 * um quinto do tempo. */
#define REDIAGRAMAR_MS 300
static void carregar_imagens(void)
{
    unsigned ultimo = tempo_ms();
    while (motor) {
        evento_t e;
        if (!tem_evento_guardado && proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo != EV_MOVER) {
                /* Uma ação do usuário interrompe; o evento é tratado depois, no laço principal. */
                evento_guardado = e;
                tem_evento_guardado = true;
                break;
            }
        }
        if (tem_evento_guardado) break;
        adiantar_imagens();
        if (!motor_imagens(motor, 2)) break;
        falta_diagramar = true;
        unsigned espera = custo_diagramar * 4 > REDIAGRAMAR_MS ? custo_diagramar * 4 : REDIAGRAMAR_MS;
        if (tempo_ms() - ultimo >= espera) {
            diagramar();
            desenhar_pagina();
            jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A);
            if (progresso) {
                int nos, cmds, imgs, total = motor_imagens_total(motor);
                motor_numeros(motor, &nos, &cmds, &imgs);
                definir_progresso(70 + (total ? 29 * imgs / total : 0));
            }
            ultimo = tempo_ms();
            falta_diagramar = false;
        }
    }
    /* Interrompida, a diagramação fica para quando as imagens voltarem: o
     * clique ou a rolagem são tratados antes. */
    if (motor && falta_diagramar && !tem_evento_guardado) {
        falta_diagramar = false;
        diagramar();
        desenhar_pagina();
        jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A);
    }
    if (motor) {
        int nos, cmds, imgs;
        motor_numeros(motor, &nos, &cmds, &imgs);
        registrar("navegador: imagens %s (%d de %d)", tem_evento_guardado ? "interrompidas" : "prontas", imgs, motor_imagens_total(motor));
    }
}

static void mostrar_titulo(void)
{
    char t[128];
    titulo_da_pagina(t, sizeof t);
    /* O kernel guarda títulos curtos: o da página é cortado com "...". */
    char curto[28], titulo[64];
    copia_texto(curto, t, sizeof curto);
    if (tam_texto(t) >= sizeof curto) copia_texto(curto + sizeof curto - 4, "...", 4);
    formatar_texto(titulo, sizeof titulo, "%s - " NOME_NAVEGADOR, curto);
    titulo_janela(j->id, titulo);
}

static void carregar(const char *pedido, bool novo_no_historico, int rolagem_inicial)
{
    char url[MAX_URL];
    if (!endereco_tem_esquema(pedido) && pedido[0] != '/') formatar_texto(url, sizeof url, "https://%s", pedido);
    else copia_texto(url, pedido, sizeof url);
    /* Só o fragmento mudou: rola até a âncora, sem buscar de novo. */
    const char *frag = acha_char(url, '#');
    if (frag && motor && compara_texto_n(url, url_atual, (size_t)(frag - url)) == 0 &&
        (url_atual[frag - url] == 0 || url_atual[frag - url] == '#')) {
        int y = motor_ancora(motor, frag + 1);
        if (y >= 0) {
            guardar_rolagem();
            rolagem = y - 4;
            limitar_rolagem();
            copia_texto(url_atual, url, sizeof url_atual);
            definir_endereco(url);
            desenhar_tudo();
            registrar("navegador: ancora %s em %d", frag + 1, y);
            return;
        }
    }
    guardar_rolagem();
    ende.foco = false;
    link_sob_mouse[0] = 0;
    titulo_js[0] = 0;
    definir_endereco(url);
    char msg[220];
    formatar_texto(msg, sizeof msg, "Carregando %s ...", url);
    copia_texto(status, msg, sizeof status);
    progresso = 10;
    apagar_balao();
    desenhar_status();
    desenhar_barra();
    jan_mostrar(j);
    unsigned inicio = tempo_ms();
    /* a página anterior: motores, scripts, fontes da web, cache */
    if (medidor) { motor_fim(medidor); medidor = NULL; }
    if (medidor_html) { libera(medidor_html); medidor_html = NULL; }
    medidor_n = -1;
    if (motor) { motor_fim(motor); motor = NULL; }
    quadros_esquecer();
    motor_fontes_limpar();
    js_fim();
    cache_limpar();
    imgc_limpar();
    copia_texto(url_atual, url, sizeof url_atual);

    memset(&perf, 0, sizeof perf);
    unsigned t_pagina = 0, t_scripts = 0, t_ler = 0;
    texto_t pag = { 0, 0, 0 };
    char tipo[64] = "text/html", final[MAX_URL], erro[160] = "";
    int situacao = 200;
    uint32_t bytes = 0;
    if (comeca_com(url, "sagui:")) { pagina_interna(url, &pag); copia_texto(final, url, sizeof final); }
    else {
        rede_resposta_t r;
        t_pagina = tempo_ms();
        situacao = rede_buscar(url, &r, MAX_PAGINA, NULL, 0);
        t_pagina = tempo_ms() - t_pagina;
        definir_progresso(45);
        copia_texto(tipo, r.tipo[0] ? r.tipo : "text/html", sizeof tipo);
        copia_texto(final, r.final[0] ? r.final : url, sizeof final);
        copia_texto(erro, r.erro, sizeof erro);
        bytes = r.n;
        if (situacao < 0 || !r.d) {
            libera(r.d);
            pagina_erro(&pag, url, erro[0] ? erro : "a página veio vazia");
            copia_texto(tipo, "text/html", sizeof tipo);
            registrar("navegador: erro em %s: %s", url, erro);
        } else if (comeca_com(tipo, "image/")) {
            /* Uma imagem sozinha: página com um <img> que aponta para ela. */
            libera(r.d);
            T(&pag, "<html><head><title>Imagem</title></head><body><img src=\"");
            html_escapar(&pag, final, MAX_URL);
            T(&pag, "\"></body></html>");
        } else if (comeca_com(tipo, "video/mp4") || comeca_com(tipo, "audio/mp4")) {
            /* O vídeo toca num programa próprio, enquanto baixa (H82). */
            libera(r.d);
            char *args[] = { "video", final, NULL };
            int pid = executar("/bin/video", args);
            registrar("navegador: video %s pid %d", final, pid);
            pagina_erro(&pag, url, pid > 0 ? "o vídeo abriu no tocador de vídeo" : "não consegui abrir o tocador de vídeo");
        } else if (comeca_com(tipo, "text/html") || comeca_com(tipo, "application/xhtml")) {
            int cs = charset_em(tipo, (int)tam_texto(tipo));
            if (cs < 0) cs = charset_em((const char *)r.d, r.n < 2048 ? (int)r.n : 2048);
            uint32_t n = r.n;
            char *u8 = para_utf8((char *)r.d, &n, cs);
            pag.d = u8; pag.n = n; pag.cap = n + 1;
        } else {
            /* Texto puro: cada linha como veio, em letra de máquina de escrever. */
            int cs = charset_em(tipo, (int)tam_texto(tipo));
            uint32_t n = r.n;
            char *u8 = para_utf8((char *)r.d, &n, cs);
            const char *ult = acha_ultimo_char(final, '/');
            T(&pag, "<html><head><meta charset=\"utf-8\"><title>");
            html_escapar(&pag, ult && ult[1] ? ult + 1 : final, 120);
            T(&pag, "</title></head><body><pre style=\"white-space:pre-wrap\">");
            for (uint32_t i = 0; i < n; i++) {
                if (u8[i] == '\r') continue;   /* fim de linha do Windows: o \n basta */
                if (u8[i] == '<') T(&pag, "&lt;");
                else if (u8[i] == '&') T(&pag, "&amp;");
                else acrescentar(&pag, u8 + i, 1);
            }
            T(&pag, "</pre></body></html>");
            libera(u8);
        }
    }
    if (!pag.d) { definir_status("memória insuficiente"); return; }
    /* As páginas internas e de erro são escritas aqui, em Latin-1. */
    if (comeca_com(pag.d, "<html><head><meta charset=\"iso-8859-1\">")) pag.d = para_utf8(pag.d, &pag.n, 0);
    copia_texto(url_atual, final[0] && !comeca_com(url, "sagui:") ? final : url, sizeof url_atual);
    if (acha_char(url, '#') && !acha_char(url_atual, '#')) junta_texto(url_atual, acha_char(url, '#'), sizeof url_atual);
    t_scripts = tempo_ms();
    pag.d = rodar_scripts(pag.d, &pag.n);
    t_scripts = tempo_ms() - t_scripts;
    unsigned rede_antes = perf.rede, diag_antes = perf.diag;
    t_ler = tempo_ms();
    ler_pagina(pag.d, (int)pag.n);
    t_ler = tempo_ms() - t_ler;
    unsigned css_rede = perf.rede - rede_antes, ler_diag = perf.diag - diag_antes;
    libera(pag.d);
    copia_texto(url_atual, url, sizeof url_atual);
    rolagem = rolagem_inicial;
    if (frag && motor) { int y = motor_ancora(motor, frag + 1); if (y >= 0) rolagem = y - 4; }
    limitar_rolagem();
    if (novo_no_historico) {
        if (hist_pos < MAX_HIST - 1) hist_pos++;
        else memmove(&hist[0], &hist[1], sizeof hist[0] * (MAX_HIST - 1));
        copia_texto(hist[hist_pos].url, url, sizeof hist[hist_pos].url);
        hist[hist_pos].rolagem = 0;
        hist_qtd = hist_pos + 1;
    }
    mostrar_titulo();
    progresso = 70;
    formatar_texto(status, sizeof status, "%s%s%u KB em %u ms", erro[0] ? erro : "", erro[0] ? " | " : "", bytes / 1024, tempo_ms() - inicio);
    int nos = 0, cmds = 0, imgs = 0;
    if (motor) motor_numeros(motor, &nos, &cmds, &imgs);
    registrar("navegador: carregada %s (%u bytes, %s, situacao %d) em %u ms: %d nos, %d pecas, %d links, %d imagens, altura %d",
              url, bytes, tipo[0] ? tipo : "?", situacao, tempo_ms() - inicio, nos, cmds, motor ? motor_links(motor) : 0,
              motor ? motor_imagens_total(motor) : 0, altura);
    char tit[128];
    copia_texto(tit, titulo_js[0] ? titulo_js : motor ? motor_titulo(motor) : "", sizeof tit);
    utf8_para_sistema(tit);
    registrar("navegador: titulo %s", tit);
    unsigned t_des = tempo_ms();
    desenhar_tudo();
    t_des = tempo_ms() - t_des;
    registrar("navegador: tempos da pagina: rede %u ms, scripts %u ms, ler %u ms (css pela rede %u ms, diagramar %u ms), desenhar %u ms",
              t_pagina, t_scripts, t_ler, css_rede, ler_diag, t_des);
    memset(&perf, 0, sizeof perf);
    unsigned t_imgs = tempo_ms();
    carregar_imagens();
    registrar("navegador: tempos das imagens: %u ms: rede %u ms (%u), decodificar %u ms (%u), diagramar %u ms (%u)",
              tempo_ms() - t_imgs, perf.rede, perf.n_rede, perf.decod, perf.n_decod, perf.diag, perf.n_diag);
    /* Imagens novas podem ter mudado a altura: a âncora pode ter descido. */
    if (frag && motor) { int y = motor_ancora(motor, frag + 1); if (y >= 0) { rolagem = y - 4; limitar_rolagem(); } }
    formatar_texto(status, sizeof status, "%s%sPronto: %u KB em %u ms", erro[0] ? erro : "", erro[0] ? " | " : "", bytes / 1024, tempo_ms() - inicio);
    copia_texto(status_pronto, status, sizeof status_pronto);
    progresso = 0;
    desenhar_tudo();
    registrar("navegador: pronto %s", url);
}

/* ------------------------------------------------------------ página viva (JavaScript) */

/* O DOM mudou: relê a página no mesmo ponto da rolagem, com o foco no campo. (#6) */
static void reler(char *h, int n)
{
    int campo = motor && motor_tem_foco(motor) ? motor_foco_campo(motor) : -1;
    int r = rolagem;
    ler_pagina(h, n);
    libera(h);
    if (motor && campo >= 0) motor_focar_campo(motor, campo);
    while (motor && motor_imagens(motor, 8)) diagramar();
    rolagem = r;
    limitar_rolagem();
    desenhar_pagina();
    jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A);
}

static void clique_js(int id)
{
    int n = 0;
    char *h = js_clicar(id, &n);
    registrar("navegador: clique js %d (%s)", id, h ? "o DOM mudou" : "nada mudou");
    if (h) reler(h, n);
}

static unsigned ult_tique, hash_viva;
static int js_rol;

static unsigned hash_texto(const char *h, int n) { unsigned x = 2166136261u; for (int i = 0; i < n; i++) x = (x ^ (uint8_t)h[i]) * 16777619u; return x; }

static void pagina_viva(void)
{
    int n = 0, nr = 0;
    unsigned agora = tempo_ms(), v0 = tela2d_versao();
    char *hr = NULL, *h;
    if (rolagem != js_rol) { js_rol = rolagem; hr = js_rolar(0, js_rol, &nr); }   /* scroll e observadores antes dos timers */
    h = js_tique((int)(agora - ult_tique > 1000 ? 1000 : agora - ult_tique), &n);
    if (h) libera(hr); else { h = hr; n = nr; }   /* vale o estado mais novo */
    ult_tique = agora;
    int px, py;
    if (js_rolagem_pedida(&px, &py)) { rolagem = py; limitar_rolagem(); }
    if (!h) {
        /* só um <canvas> mudou: pinta de novo, sem layout */
        if (tela2d_versao() != v0 && motor) { desenhar_pagina(); jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A); }
        return;
    }
    unsigned x = hash_texto(h, n);
    if (x == hash_viva) { libera(h); return; }
    hash_viva = x;
    reler(h, n);
    registrar("navegador: pagina viva redesenhada");
}

/* ------------------------------------------------------------ formulários */

static void enviar(const motor_alvo_t *quem, int campo)
{
    char url[MAX_URL];
    if (campo >= 0) {
        char *h; int n;
        int cancelou = js_enviar(campo, &h, &n);
        if (cancelou) { if (h) reler(h, n); return; }
        libera(h);
        if (motor_envio_campo(motor, campo, url, sizeof url)) { registrar("navegador: formulario -> %s", url); carregar(url, true, 0); }
        return;
    }
    if (motor_envio(motor, quem, url, sizeof url)) { registrar("navegador: formulario -> %s", url); carregar(url, true, 0); }
}

/* Tecla num campo com foco (UTF-8 por dentro; o teclado manda Latin-1). (#6) */
static void tecla_no_campo(unsigned t)
{
    if (t == TECLA_ESC) { motor_foco(motor, NULL); diagramar(); desenhar_tudo(); return; }
    if (t == '\n' || t == '\r') { enviar(NULL, motor_foco_campo(motor)); return; }
    char v[1024];
    copia_texto(v, motor_foco_valor(motor), sizeof v);
    int k = (int)tam_texto(v);
    if (t == 8) {
        if (!k) return;
        do k--; while (k > 0 && ((uint8_t)v[k] & 0xC0) == 0x80);
        v[k] = 0;
    } else if (t >= 32 && t < 256 && t != 127) {
        if (k + 4 >= (int)sizeof v) return;
        k += utf8_por(v + k, t);
        v[k] = 0;
    } else return;
    motor_foco_definir(motor, v);
    int campo = motor_foco_campo(motor);
    if (campo >= 0) {
        int n = 0;
        char *h = js_campo(campo, v, t == 8 ? 8 : (int)t, &n);
        if (h) { reler(h, n); return; }
    }
    diagramar();
    desenhar_pagina();
    jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A);
}

/* ------------------------------------------------------------ favoritos */

static void favoritar(void)
{
    if (!motor || comeca_com(url_atual, "sagui:")) return;
    char caminho[64];
    pasta_config(caminho, sizeof caminho);
    junta_texto(caminho, "/favoritos.txt", sizeof caminho);
    char msg[200];
    if (favoritado()) {
        /* já está: a estrela tira (o arquivo é escrito de novo, sem a linha) */
        int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
        if (fd < 0) { definir_status("não consegui gravar os favoritos"); return; }
        for (int i = 0; i < qtd_favs; i++) {
            if (compara_texto(favs[i].url, url_atual) == 0) continue;
            char linha[MAX_URL + 64];
            int n = formatar_texto(linha, sizeof linha, "%s\t%s\n", favs[i].url, favs[i].titulo);
            escrever_tudo(fd, linha, (unsigned)n);
        }
        fechar(fd);
        formatar_texto(msg, sizeof msg, "Tirado dos favoritos");
        registrar("navegador: favorito tirado %s", url_atual);
    } else {
        int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR);
        if (fd < 0) { definir_status("não consegui gravar os favoritos"); return; }
        char t[128], linha[MAX_URL + 160];
        titulo_da_pagina(t, sizeof t);
        int n = formatar_texto(linha, sizeof linha, "%s\t%s\n", url_atual, t);
        escrever_tudo(fd, linha, (unsigned)n);
        fechar(fd);
        formatar_texto(msg, sizeof msg, "Favorito guardado em %s", caminho);
        registrar("navegador: favorito %s", url_atual);
    }
    ler_favoritos();
    mostrar_barra();
    definir_status(msg);
}

/* O menu dos três pontos. */
static const char *const itens_menu = "Nova aba|Nova janela|Página inicial|Favoritos|Adicionar aos favoritos|Recarregar";
enum { M_ABA, M_NOVA, M_INICIO, M_FAVORITOS, M_FAVORITAR, M_RECARREGAR };

static void nova_janela(void)
{
    char *args[] = { "navegador", NULL };
    int pid = executar("/bin/navegador", args);
    registrar("navegador: nova janela pid %d", pid);
}

/* ------------------------------------------------------------ abas */

/* A aba da frente vai para trás: guarda o que é preciso para reabrir. */
static void guardar_aba(void)
{
    aba_t *a = abas[aba_atual];
    guardar_rolagem();
    copia_texto(a->url, url_atual, sizeof a->url);
    a->rolagem = rolagem;
    titulo_da_pagina(a->titulo, sizeof a->titulo);
    a->hist_qtd = hist_qtd;
    a->hist_pos = hist_pos;
    memcpy(a->hist, hist, sizeof hist);
}

/* A aba i vem para a frente: o histórico volta e a página abre de novo. */
static void trazer_aba(int i)
{
    aba_t *a = abas[i];
    aba_atual = i;
    hist_qtd = a->hist_qtd;
    hist_pos = a->hist_pos;
    memcpy(hist, a->hist, sizeof hist);
    rolagem = a->rolagem;
    aba_sob = fechar_sob = -1;
    posicionar_botoes();
    registrar("navegador: aba %d de %d", i + 1, qtd_abas);
    carregar(a->url[0] ? a->url : "sagui:inicio", false, a->rolagem);
}

static void trocar_aba(int i)
{
    if (i == aba_atual || i < 0 || i >= qtd_abas) return;
    guardar_aba();
    trazer_aba(i);
}

/* Outra aba, logo depois das outras, já na frente. */
static void nova_aba(const char *url)
{
    if (qtd_abas == MAX_ABAS) { definir_status("Já são 8 abas nesta janela; feche uma para abrir outra."); return; }
    aba_t *a = aloca(sizeof *a);
    if (!a) { definir_status("Não há memória para outra aba."); return; }
    memset(a, 0, sizeof *a);
    if (qtd_abas > 0) guardar_aba();
    abas[qtd_abas] = a;
    aba_atual = qtd_abas++;
    hist_qtd = 0;
    hist_pos = -1;
    rolagem = 0;
    url_atual[0] = 0;
    aba_sob = fechar_sob = -1;
    posicionar_botoes();
    registrar("navegador: nova aba, agora %d", qtd_abas);
    carregar(url ? url : "sagui:inicio", true, 0);
}

/* Fecha a aba i; fechar a última fecha a janela. */
static void fechar_aba(int i)
{
    if (i < 0 || i >= qtd_abas) return;
    if (qtd_abas == 1) { registrar("navegador: aba fechada"); jan_fechar(j); sair(0); }
    libera(abas[i]);
    for (int k = i; k < qtd_abas - 1; k++) abas[k] = abas[k + 1];
    qtd_abas--;
    registrar("navegador: aba fechada, ficam %d", qtd_abas);
    aba_sob = fechar_sob = -1;
    if (i == aba_atual) { trazer_aba(i < qtd_abas ? i : qtd_abas - 1); return; }
    if (i < aba_atual) aba_atual--;
    posicionar_botoes();
    desenhar_tudo();
}

/* A aba sob (x, y), e se o ponto está no "x" dela. */
static int aba_em(int x, int y, bool *no_x)
{
    *no_x = false;
    if (y < ABA_Y || y >= ABAS || x < ABA_X) return -1;
    int i = (x - ABA_X) / ABA_L;
    if (i >= qtd_abas) return -1;
    int fx = ABA_X_FECHAR(i), fy = ABA_Y + 6;
    *no_x = aba_tem_x(i) && x >= fx && x < fx + 20 && y >= fy && y < fy + 20;
    return i;
}

/* ------------------------------------------------------------ eventos */

static int botao_em(int x, int y)
{
    for (int i = 0; i < QTD_BT; i++)
        if (x >= botoes[i].x && x < botoes[i].x + botoes[i].l && y >= botoes[i].y && y < botoes[i].y + botoes[i].a) return i;
    return -1;
}

static int favorito_em(int x, int y)
{
    if (y < ABAS + FERR || y >= BARRA) return -1;
    for (int i = 0; i < qtd_favs; i++) if (favs[i].l && x >= favs[i].x && x < favs[i].x + favs[i].l) return i;
    return -1;
}

static void voltar(int passo)
{
    int novo = hist_pos + passo;
    if (novo < 0 || novo >= hist_qtd) return;
    guardar_rolagem();
    hist_pos = novo;
    carregar(hist[hist_pos].url, false, hist[hist_pos].rolagem);
}

static void rolar(int d)
{
    int antes = rolagem;
    rolagem += d;
    limitar_rolagem();
    if (rolagem != antes) desenhar_tudo();
}

static void editar_texto(char *t, int max, int *cursor, unsigned tecla)
{
    int n = (int)tam_texto(t);
    if (tecla == TECLA_ESQUERDA) { if (*cursor > 0) (*cursor)--; }
    else if (tecla == TECLA_DIREITA) { if (*cursor < n) (*cursor)++; }
    else if (tecla == TECLA_HOME) *cursor = 0;
    else if (tecla == TECLA_END) *cursor = n;
    else if (tecla == 8) { if (*cursor > 0) { memmove(t + *cursor - 1, t + *cursor, (size_t)(n - *cursor + 1)); (*cursor)--; } }
    else if (tecla == TECLA_DELETE) { if (*cursor < n) memmove(t + *cursor, t + *cursor + 1, (size_t)(n - *cursor)); }
    else if (tecla >= 32 && tecla < 256 && tecla != 127 && n < max - 1) {
        memmove(t + *cursor + 1, t + *cursor, (size_t)(n - *cursor + 1));
        t[*cursor] = (char)tecla;
        (*cursor)++;
    }
}

static void focar_endereco(void)
{
    ende.foco = true; ende.tudo = true;
    ende.cursor = (int)tam_texto(ende.t);
    if (motor && motor_tem_foco(motor)) { motor_foco(motor, NULL); diagramar(); }
    desenhar_tudo();
}

static void tratar_tecla(const evento_t *e)
{
    unsigned t = e->tecla;
    bool ctrl = (e->modificadores & 2) != 0;
    if ((t == 12 || t == 'l') && ctrl) { focar_endereco(); return; }
    if ((t == 4 || t == 'd') && ctrl) { favoritar(); return; }
    if ((t == 14 || t == 'n') && ctrl) { nova_janela(); return; }
    if ((t == 20 || t == 't') && ctrl) { nova_aba(NULL); return; }
    if ((t == 23 || t == 'w') && ctrl) { fechar_aba(aba_atual); return; }
    if (t == '\t' && ctrl) { if (qtd_abas > 1) trocar_aba((aba_atual + 1) % qtd_abas); return; }
    if (t == TECLA_F5 || ((t == 18 || t == 'r') && ctrl)) { if (url_atual[0]) carregar(url_atual, false, rolagem); return; }
    if (ende.foco) {
        if (t == '\n' || t == '\r') {
            char u[MAX_URL];
            copia_texto(u, ende.t, sizeof u);
            if (u[0]) carregar(u, true, 0);
            return;
        }
        if (t == TECLA_ESC) { ende.foco = false; definir_endereco(url_atual); desenhar_tudo(); return; }
        if (ende.tudo) {
            ende.tudo = false;
            /* Com tudo selecionado, digitar ou apagar troca o endereço inteiro; as setas só desfazem a seleção. */
            if (t == 8 || t == TECLA_DELETE || (t >= 32 && t < 256)) { ende.t[0] = 0; ende.cursor = 0; ende.inicio = 0; }
            if (t == 8 || t == TECLA_DELETE) { mostrar_barra(); return; }
        }
        editar_texto(ende.t, sizeof ende.t, &ende.cursor, t);
        mostrar_barra();
        return;
    }
    if (motor && motor_tem_foco(motor)) { tecla_no_campo(t); return; }
    switch (t) {
    case TECLA_CIMA: rolar(-48); break;
    case TECLA_BAIXO: rolar(48); break;
    case TECLA_PGUP: rolar(-(VISTA_A - 40)); break;
    case TECLA_PGDN: case ' ': rolar(VISTA_A - 40); break;
    case TECLA_HOME: rolar(-altura); break;
    case TECLA_END: rolar(altura); break;
    case 8: voltar(-1); break;
    default: break;
    }
}

static bool arrastando_rolagem;
static int arrasto_y0, arrasto_rol0;

static void abrir_link(const char *href)
{
    char u[MAX_URL];
    copia_texto(u, href, sizeof u);
    registrar("navegador: link %s", u);
    if (comeca_com(u, "sagui-js:")) { clique_js(texto_para_int(u + 9)); return; }
    if (comeca_com(u, "javascript:") || comeca_com(u, "mailto:")) { definir_status("Esse tipo de link não funciona aqui."); return; }
    carregar(u, true, 0);
}

static void tratar_clique(const evento_t *e)
{
    if (e->y < BARRA) {
        bool no_x;
        int ab = aba_em(e->x, e->y, &no_x);
        if (ab >= 0) {
            if (no_x) fechar_aba(ab);
            else trocar_aba(ab);
            return;
        }
        int b = botao_em(e->x, e->y);
        if (b >= 0 && ativo(b)) {
            apertado = b;
            mostrar_barra();
            registrar("navegador: botao %d", b);
            switch (b) {
            case BT_VOLTAR: voltar(-1); break;
            case BT_AVANCAR: voltar(1); break;
            case BT_RECARREGAR: if (url_atual[0]) carregar(url_atual, false, rolagem); break;
            case BT_CASA: carregar("sagui:inicio", true, 0); break;
            case BT_ESTRELA: favoritar(); break;
            case BT_MENU: jan_menu(j, LARG - 228, ABAS + FERR - 2, itens_menu); break;
            case BT_NOVA: nova_aba(NULL); break;
            }
            apertado = -1;
            desenhar_tudo();
            return;
        }
        int f = favorito_em(e->x, e->y);
        if (f >= 0) { char u[MAX_URL]; copia_texto(u, favs[f].url, sizeof u); registrar("navegador: favorito aberto %s", u); carregar(u, true, 0); return; }
        if (e->x >= ENDE_X && e->x < ENDE_X + ENDE_L && e->y >= ENDE_Y && e->y < ENDE_Y + ENDE_A) {
            bool primeiro = !ende.foco;
            ende.tudo = primeiro;              /* o primeiro clique seleciona tudo; o segundo põe o cursor */
            ende.foco = true;
            if (motor && motor_tem_foco(motor)) { motor_foco(motor, NULL); diagramar(); }
            int c = ende.inicio + (e->x - ENDE_X - 38 + LETRA_L / 2) / LETRA_L;
            int n = (int)tam_texto(ende.t);
            ende.cursor = primeiro ? n : c < 0 ? 0 : c > n ? n : c;
            desenhar_tudo();
        }
        return;
    }
    if (e->x >= VISTA_L) {                       /* barra de rolagem */
        if (altura <= VISTA_A) return;
        int a = VISTA_A * VISTA_A / altura;
        if (a < 20) a = 20;
        int ya = BARRA + (int)((int)rolagem * (VISTA_A - a) / (altura - VISTA_A));
        if (e->y >= ya && e->y < ya + a) { arrastando_rolagem = true; arrasto_y0 = e->y; arrasto_rol0 = rolagem; }
        else rolar(e->y < ya ? -(VISTA_A - 40) : VISTA_A - 40);
        return;
    }
    if (!motor) return;
    bool tinha_foco = ende.foco;
    ende.foco = false;
    int py = e->y - BARRA + rolagem;
    const motor_alvo_t *ma = motor_alvo(motor, e->x, py);
    if (ma && ma->tipo == MA_LINK && ma->href) { abrir_link(ma->href); return; }
    if (ma && ma->tipo == MA_QUADRO) {
        /* dentro de um iframe: o quadro acha o elemento e roda o clique na janela dele */
        js_clicar_quadro(ma->js, e->x - ma->rx, py - ma->ry);
        desenhar_tudo();
        return;
    }
    if (ma && ma->tipo == MA_CAMPO) {
        /* foco no campo: a digitação vai para dentro da caixa */
        motor_foco(motor, ma);
        registrar("navegador: foco no campo %d", motor_foco_campo(motor));
        diagramar();
        desenhar_tudo();
        return;
    }
    if (ma && ma->tipo == MA_BOTAO) {
        /* o clique vai ao script; depois, se o botão é de envio, o formulário
         * vai (a não ser que o script cancele). ma deixa de valer se o clique
         * reler a página: guarda o que precisa. */
        int js = ma->js, campo = ma->campo;
        bool envia = ma->envia, cancelou = false;
        if (js >= 0) { clique_js(js); cancelou = js_clique_cancelado(); }
        if (envia && !cancelou) enviar(js >= 0 ? NULL : ma, campo);
        return;
    }
    if (motor_tem_foco(motor)) { motor_foco(motor, NULL); diagramar(); desenhar_tudo(); return; }
    if (tinha_foco) desenhar_tudo();
}

static void tratar_movimento(const evento_t *e)
{
    if (arrastando_rolagem) {
        if (!(e->botoes & 1)) { arrastando_rolagem = false; return; }
        int a = VISTA_A * VISTA_A / (altura ? altura : 1);
        if (a < 20) a = 20;
        if (VISTA_A - a <= 0) return;
        rolagem = arrasto_rol0 + (int)((int)(e->y - arrasto_y0) * (altura - VISTA_A) / (VISTA_A - a));
        limitar_rolagem();
        desenhar_tudo();
        return;
    }
    /* o botão e o favorito sob o mouse ficam com fundo */
    int b = e->y < BARRA ? botao_em(e->x, e->y) : -1, f = e->y < BARRA ? favorito_em(e->x, e->y) : -1;
    bool no_x;
    int ab = aba_em(e->x, e->y, &no_x), fx = no_x ? ab : -1;
    if (b != sob || f != fav_sob || ab != aba_sob || fx != fechar_sob) {
        sob = b; fav_sob = f; aba_sob = ab; fechar_sob = fx;
        mostrar_barra();
    }
    char novo[MAX_URL] = "";
    if (f >= 0) copia_texto(novo, favs[f].url, sizeof novo);
    if (motor && e->y >= BARRA && e->x < VISTA_L) {
        const motor_alvo_t *ma = motor_alvo(motor, e->x, e->y - BARRA + rolagem);
        if (ma && ma->tipo == MA_LINK && ma->href && !comeca_com(ma->href, "sagui-js:")) {
            copia_texto(novo, ma->href, sizeof novo);
            utf8_para_sistema(novo);
        }
    }
    if (compara_texto(novo, link_sob_mouse) != 0) {
        copia_texto(link_sob_mouse, novo, sizeof link_sob_mouse);
        trocar_balao();
    }
}

/* A janela mudou de tamanho (H78): só o layout muda, nada é baixado de novo;
 * o script recebe resize (e pode mudar a página). */
static void mudar_tamanho(const evento_t *e)
{
    jan_novo_tamanho(j, e);
    larg_jan = j->larg;
    alt_jan = j->alt;
    posicionar_botoes();
    if (motor) {
        int alt_antes = altura;
        int nn = 0;
        char *hh = js_ativo() ? js_redimensionar(VISTA_L, VISTA_A, &nn) : NULL;
        if (hh) reler(hh, nn);
        else diagramar();
        /* Mantém o começo da parte visível mais ou menos no mesmo lugar. */
        if (alt_antes > 0) rolagem = (int)((long)rolagem * altura / alt_antes);
        limitar_rolagem();
    }
    desenhar_tudo();
    registrar("navegador: tamanho %dx%d, pagina com altura %d", LARG, ALT, altura);
}

int principal(int argc, char **argv)
{
    j = jan_abrir(NOME_NAVEGADOR, LARG, ALT);
    if (!j) { imprimir_em(ERROS, "navegador: não consegui abrir a janela\n"); return 1; }
    jan_redimensionavel(j, 520, 300);
    posicionar_botoes();
    ler_favoritos();
    registrar("navegador: janela %d", j->id);
    http_guardar_conexoes(true);   /* as imagens e o CSS reaproveitam a conexão da página (H129) */
    nova_aba(argc > 1 ? argv[1] : NULL);
    for (;;) {
        /* o script pediu outra página (location.href, assign, replace, reload) */
        char u[MAX_URL]; int subst = 0;
        if (js_navegacao(u, sizeof u, &subst)) {
            registrar("navegador: o script navegou para %s", u);
            if (subst && hist_pos >= 0) { hist_pos--; hist_qtd = hist_pos + 1; }
            carregar(u, true, 0);
            continue;
        }
        evento_t e;
        if (tem_evento_guardado) { e = evento_guardado; tem_evento_guardado = false; }
        else if (motor && motor_imagens_faltam(motor)) {
            /* Um clique, a rolagem ou o menu interromperam as imagens: tratado o
             * evento, elas continuam de onde pararam (antes ficavam faltando, H129). */
            carregar_imagens();
            /* terminaram: a barra volta a dizer "Pronto", não "Baixando" a última imagem */
            if (motor && !motor_imagens_faltam(motor) && status_pronto[0]) definir_status(status_pronto);
            continue;
        }
        else if (js_vivo() || (js_ativo() && rolagem != js_rol)) {
            /* página viva: timers e animações no ritmo deles, sem travar os eventos */
            if (proximo_evento(j->id, &e, false) != 1) {
                unsigned passo = js_animando() ? 16 : 100;
                if (tempo_ms() - ult_tique >= passo || rolagem != js_rol) pagina_viva();
                else dormir(10);
                continue;
            }
        } else if (proximo_evento(j->id, &e, true) != 1) continue;
        switch (e.tipo) {
        case EV_FECHAR:
            jan_fechar(j);
            return 0;
        case EV_REDIMENSIONAR:
            mudar_tamanho(&e);
            break;
        case EV_CLIQUE:
            if (e.botoes & 1) tratar_clique(&e);
            else if (e.botoes & 4) {   /* o botão do meio fecha a aba */
                bool no_x;
                int ab = aba_em(e.x, e.y, &no_x);
                if (ab >= 0) fechar_aba(ab);
            }
            break;
        case EV_SOLTOU:
            arrastando_rolagem = false;
            break;
        case EV_MOVER:
            tratar_movimento(&e);
            break;
        case EV_TECLA:
            tratar_tecla(&e);
            break;
        case EV_MENU:
            registrar("navegador: menu %u", e.tecla);
            if (e.tecla == M_ABA) nova_aba(NULL);
            else if (e.tecla == M_NOVA) nova_janela();
            else if (e.tecla == M_INICIO) carregar("sagui:inicio", true, 0);
            else if (e.tecla == M_FAVORITOS) carregar("sagui:favoritos", true, 0);
            else if (e.tecla == M_FAVORITAR) { if (!favoritado()) favoritar(); }
            else if (e.tecla == M_RECARREGAR) { if (url_atual[0]) carregar(url_atual, false, rolagem); }
            break;
        }
    }
}
