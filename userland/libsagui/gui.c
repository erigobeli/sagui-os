/* gui.c - kit gráfico da libsagui (H22): desenho e componentes no anel 3.
 *
 * O programa desenha nos pixels da própria janela (memória que o kernel
 * compartilha com ele) e chama jan_mostrar para o kernel exibir. Os
 * componentes não guardam nada escondido: cada um é uma estrutura do
 * programa, e o programa decide quando desenhar e para quem mandar cada
 * evento. É simples de entender e de testar. */
#include "gui.h"
#include "suave.h"

extern const unsigned char fonte_sagui[256][16];

/* ------------------------------------------------------------ janela e desenho */

jan_t *jan_abrir(const char *titulo, int largura, int altura)
{
    jan_t *j = aloca(sizeof(jan_t));
    if (!j) return NULL;
    j->id = criar_janela(titulo, largura, altura, &j->px);
    if (j->id < 0) { libera(j); return NULL; }
    j->larg = largura;
    j->alt = altura;
    g_retangulo(j, 0, 0, largura, altura, COR_FUNDO);
    return j;
}

void jan_fechar(jan_t *j)
{
    if (!j) return;
    fechar_janela(j->id);
    libera(j);
}

void jan_mostrar(jan_t *j) { atualizar_janela(j->id, NULL); }

void jan_mostrar_area(jan_t *j, int x, int y, int l, int a)
{
    retangulo_t r = { x, y, l, a };
    atualizar_janela(j->id, &r);
}

void g_retangulo(jan_t *j, int x, int y, int l, int a, uint32_t cor)
{
    int x1 = x + l, y1 = y + a;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > j->larg) x1 = j->larg;
    if (y1 > j->alt) y1 = j->alt;
    for (int yy = y; yy < y1; yy++) {
        uint32_t *p = j->px + yy * j->larg;
        for (int xx = x; xx < x1; xx++) p[xx] = cor;
    }
}

void g_moldura(jan_t *j, int x, int y, int l, int a, uint32_t cor)
{
    g_retangulo(j, x, y, l, 1, cor);
    g_retangulo(j, x, y + a - 1, l, 1, cor);
    g_retangulo(j, x, y, 1, a, cor);
    g_retangulo(j, x + l - 1, y, 1, a, cor);
}

/* Formas com a borda suavizada (suave.h), numa área que cobre a janela toda. */
static suave_t sv_de(jan_t *j)
{
    suave_t s = { j->px, j->larg, 0, 0, j->larg, j->alt };
    return s;
}

void g_ret_redondo(jan_t *j, int x, int y, int l, int a, int raio, uint32_t topo, uint32_t base)
{
    suave_t s = sv_de(j);
    suave_ret(&s, x, y, l, a, raio, topo, base, 256);
}

void g_contorno_redondo(jan_t *j, int x, int y, int l, int a, int raio, uint32_t cor)
{
    suave_t s = sv_de(j);
    suave_contorno(&s, x, y, l, a, raio, cor, 256);
}

static inline void ponto(jan_t *j, int x, int y, uint32_t cor)
{
    if ((unsigned)x < (unsigned)j->larg && (unsigned)y < (unsigned)j->alt)
        j->px[y * j->larg + x] = cor;
}

void g_linha(jan_t *j, int x0, int y0, int x1, int y1, uint32_t cor)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1, sy = y0 < y1 ? 1 : -1;
    int passos = dx > dy ? dx : dy, acum = 0;
    for (int k = 0; k <= passos; k++) {
        ponto(j, x0, y0, cor);
        if (dx >= dy) { x0 += sx; acum += dy; if (2 * acum >= dx) { y0 += sy; acum -= dx; } }
        else          { y0 += sy; acum += dx; if (2 * acum >= dy) { x0 += sx; acum -= dy; } }
    }
}

/* Mistura de duas cores, canal a canal: num/den de b. */
static uint32_t mistura(uint32_t a, uint32_t b, int num, int den)
{
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int ca = (int)((a >> s) & 255), cb = (int)((b >> s) & 255);
        r |= (uint32_t)(ca + (cb - ca) * num / den) << s;
    }
    return r;
}

/* Suaviza a escada das diagonais, como o kernel (H81, do pull request #1 do
 * Robson): um pixel apagado com vizinhos acesos em "L" recebe 1/3 da cor. */
static inline bool aceso(const unsigned char *g, int i, int l)
{
    return i >= 0 && i < 8 && l >= 0 && l < 16 && (g[l] & (0x80 >> i));
}

static inline void quina(jan_t *j, const unsigned char *g, int x, int y, int i, int l, uint32_t cor)
{
    bool e = aceso(g, i - 1, l), d = aceso(g, i + 1, l), c = aceso(g, i, l - 1), b = aceso(g, i, l + 1);
    if (e != d && c != b && (unsigned)x < (unsigned)j->larg && (unsigned)y < (unsigned)j->alt) {
        uint32_t *p = &j->px[y * j->larg + x];
        *p = mistura(*p, cor, 1, 3);
    }
}

void g_texto_n(jan_t *j, int x, int y, const char *t, int n, uint32_t cor)
{
    for (int k = 0; k < n && t[k]; k++, x += LETRA_L) {
        if (x >= j->larg) break;
        const unsigned char *g = fonte_sagui[(uint8_t)t[k]];
        for (int lin = 0; lin < 16; lin++)
            for (int col = 0; col < 8; col++)
                if (g[lin] & (0x80 >> col)) ponto(j, x + col, y + lin, cor);
                else quina(j, g, x + col, y + lin, col, lin, cor);
    }
}

void g_texto(jan_t *j, int x, int y, const char *t, uint32_t cor)
{
    g_texto_n(j, x, y, t, 1 << 30, cor);
}

int g_largura_texto(const char *t) { return (int)tam_texto(t) * LETRA_L; }

void g_desenho(jan_t *j, int x, int y, const char *const *linhas, int l, int a,
               uint32_t (*paleta)(char), int escala)
{
    for (int lin = 0; lin < a; lin++)
        for (int col = 0; col < l; col++) {
            uint32_t c = paleta(linhas[lin][col]);
            if (c != 0xFF000000)
                g_retangulo(j, x + col * escala, y + lin * escala, escala, escala, c);
        }
}

/* Imagem ARGB (0xAARRGGBB) com transparência: cada pixel é misturado com o
 * que já está na janela, na proporção do alfa. Serve para os ícones gerados
 * por tools/mkarte.py (H92: o lançador de jogos) e para imagens com canal
 * alfa. Só inteiros: (fundo * (255 - a) + cor * a) / 255 por canal. */
void g_imagem(jan_t *j, int x, int y, const uint32_t *px, int l, int a)
{
    for (int lin = 0; lin < a; lin++) {
        int yy = y + lin;
        if (yy < 0 || yy >= j->alt) continue;
        for (int col = 0; col < l; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= j->larg) continue;
            uint32_t c = px[lin * l + col], al = c >> 24;
            if (!al) continue;
            uint32_t *d = &j->px[yy * j->larg + xx];
            if (al == 255) { *d = c & 0xFFFFFF; continue; }
            uint32_t r = (((*d >> 16) & 255) * (255 - al) + ((c >> 16) & 255) * al) / 255;
            uint32_t g = (((*d >> 8) & 255) * (255 - al) + ((c >> 8) & 255) * al) / 255;
            uint32_t b = ((*d & 255) * (255 - al) + (c & 255) * al) / 255;
            *d = r << 16 | g << 8 | b;
        }
    }
}

/* Moldura em relevo: clara em cima e à esquerda, escura embaixo e à direita
 * (ou o contrário, para parecer afundada). */
void g_relevo(jan_t *j, int x, int y, int l, int a, bool afundado)
{
    uint32_t claro = 0xFFFFFF, escuro = COR_BORDA;
    if (afundado) { uint32_t t = claro; claro = escuro; escuro = t; }
    g_retangulo(j, x, y, l, 1, claro);
    g_retangulo(j, x, y, 1, a, claro);
    g_retangulo(j, x, y + a - 1, l, 1, escuro);
    g_retangulo(j, x + l - 1, y, 1, a, escuro);
}

static bool dentro(int px, int py, int x, int y, int l, int a)
{
    return px >= x && py >= y && px < x + l && py < y + a;
}

/* ------------------------------------------------------------ rótulo */

void rotulo(jan_t *j, int x, int y, const char *texto, uint32_t cor)
{
    g_texto(j, x, y, texto, cor);
}

/* ------------------------------------------------------------ botão */

void botao_desenhar(jan_t *j, botao_t *b)
{
    /* Degradê com cantos cortados (H81, do Robson). Os pixels dos cantos
     * ficam como estavam, então o botão serve sobre qualquer fundo. */
    uint32_t topo = b->apertado ? COR_BOTAO_APERTADO : 0xFBFCFD, base = b->apertado ? 0xD6DBE0 : 0xD8DDE2;
    for (int k = 0; k < b->a; k++)
        g_retangulo(j, b->x, b->y + k, b->l, 1, mistura(topo, base, k, b->a > 1 ? b->a - 1 : 1));
    g_retangulo(j, b->x + 1, b->y - 1, b->l - 2, 1, 0x5A6470);
    g_retangulo(j, b->x + 1, b->y + b->a, b->l - 2, 1, 0x4A535C);
    g_retangulo(j, b->x - 1, b->y + 1, 1, b->a - 2, 0x5A6470);
    g_retangulo(j, b->x + b->l, b->y + 1, 1, b->a - 2, 0x5A6470);
    ponto(j, b->x, b->y, 0x5A6470);
    ponto(j, b->x + b->l - 1, b->y, 0x5A6470);
    ponto(j, b->x, b->y + b->a - 1, 0x5A6470);
    ponto(j, b->x + b->l - 1, b->y + b->a - 1, 0x5A6470);
    int tx = b->x + (b->l - g_largura_texto(b->texto)) / 2 + (b->apertado ? 1 : 0);
    int ty = b->y + (b->a - LETRA_A) / 2 + (b->apertado ? 1 : 0);
    g_texto(j, tx, ty, b->texto, COR_TEXTO);
    jan_mostrar_area(j, b->x - 1, b->y - 1, b->l + 2, b->a + 2);
}

/* Clicado = apertou e soltou dentro do botão (soltar fora cancela). */
bool botao_tratar(jan_t *j, botao_t *b, const evento_t *e)
{
    if (e->tipo == EV_CLIQUE && (e->botoes & 1) && dentro(e->x, e->y, b->x, b->y, b->l, b->a)) {
        b->apertado = true;
        botao_desenhar(j, b);
        return false;
    }
    if (e->tipo == EV_SOLTOU && b->apertado) {
        b->apertado = false;
        botao_desenhar(j, b);
        return dentro(e->x, e->y, b->x, b->y, b->l, b->a);
    }
    return false;
}

/* ------------------------------------------------------------ caixa de texto */

void caixa_definir(caixa_t *c, const char *texto)
{
    copia_texto(c->texto, texto, sizeof c->texto);
    c->cursor = (int)tam_texto(c->texto);
}

void caixa_desenhar(jan_t *j, caixa_t *c)
{
    int a = LETRA_A + 8;
    g_retangulo(j, c->x, c->y, c->l, a, COR_BRANCO);
    g_moldura(j, c->x, c->y, c->l, a, c->foco ? COR_DESTAQUE : COR_BORDA);
    if (c->foco) g_moldura(j, c->x + 1, c->y + 1, c->l - 2, a - 2, COR_DESTAQUE);
    int cabem = (c->l - 10) / LETRA_L;
    int ini = c->cursor > cabem ? c->cursor - cabem : 0;
    g_texto_n(j, c->x + 5, c->y + 4, c->texto + ini, cabem, COR_TEXTO);
    if (c->foco)
        g_retangulo(j, c->x + 5 + (c->cursor - ini) * LETRA_L, c->y + 4, 2, LETRA_A, COR_TEXTO);
    jan_mostrar_area(j, c->x, c->y, c->l, a);
}

bool caixa_tratar(jan_t *j, caixa_t *c, const evento_t *e)
{
    if (e->tipo == EV_CLIQUE) {
        bool estava = c->foco;
        c->foco = dentro(e->x, e->y, c->x, c->y, c->l, LETRA_A + 8);
        if (c->foco) {
            int col = (e->x - c->x - 5 + LETRA_L / 2) / LETRA_L;
            int n = (int)tam_texto(c->texto);
            c->cursor = col < 0 ? 0 : col > n ? n : col;
        }
        if (c->foco || estava) caixa_desenhar(j, c);
        return false;
    }
    if (e->tipo != EV_TECLA || !c->foco) return false;
    int n = (int)tam_texto(c->texto);
    unsigned t = e->tecla;
    if (t == '\n') return true;
    if (t == '\b') {
        if (c->cursor > 0) { memmove(c->texto + c->cursor - 1, c->texto + c->cursor, (size_t)(n - c->cursor + 1)); c->cursor--; }
    } else if (t == TECLA_DELETE) {
        if (c->cursor < n) memmove(c->texto + c->cursor, c->texto + c->cursor + 1, (size_t)(n - c->cursor));
    } else if (t == TECLA_ESQUERDA) { if (c->cursor > 0) c->cursor--; }
    else if (t == TECLA_DIREITA) { if (c->cursor < n) c->cursor++; }
    else if (t == TECLA_HOME) c->cursor = 0;
    else if (t == TECLA_END) c->cursor = n;
    else if (t >= 0x20 && t < 0x100 && t != 0x7F && n < (int)sizeof c->texto - 1) {
        memmove(c->texto + c->cursor + 1, c->texto + c->cursor, (size_t)(n - c->cursor + 1));
        c->texto[c->cursor++] = (char)t;
    } else return false;
    caixa_desenhar(j, c);
    return false;
}

/* ------------------------------------------------------------ área de texto */

#define BARRA_ROLAGEM 12

int area_linhas_visiveis(area_t *a) { return (a->a - 8) / LETRA_A; }

bool area_iniciar(area_t *a, int x, int y, int l, int al)
{
    memset(a, 0, sizeof *a);
    a->x = x; a->y = y; a->l = l; a->a = al;
    a->cap = 4096;
    a->buf = aloca((size_t)a->cap);
    if (!a->buf) return false;
    a->buf[0] = 0;
    return true;
}

static bool garantir(area_t *a, int precisa)
{
    if (precisa < a->cap) return true;
    int nova = a->cap;
    while (nova <= precisa) nova *= 2;
    char *b = realoca(a->buf, (size_t)nova);
    if (!b) return false;
    a->buf = b;
    a->cap = nova;
    return true;
}

void area_definir(area_t *a, const char *texto, int n)
{
    if (!garantir(a, n + 1)) n = a->cap - 1;
    memcpy(a->buf, texto, (size_t)n);
    a->buf[n] = 0;
    a->tam = n;
    a->cursor = 0;
    a->topo = 0;
    a->sel = 0;
    a->modificado = false;
}

/* Linha e coluna de uma posição do buffer. */
static void linha_coluna(area_t *a, int pos, int *lin, int *col)
{
    int l = 0, ini = 0;
    for (int i = 0; i < pos; i++)
        if (a->buf[i] == '\n') { l++; ini = i + 1; }
    *lin = l;
    *col = pos - ini;
}

static int inicio_da_linha(area_t *a, int lin)
{
    int pos = 0;
    for (int l = 0; l < lin && pos < a->tam; pos++)
        if (a->buf[pos] == '\n') l++;
    return pos;
}

static int tamanho_da_linha(area_t *a, int ini)
{
    int n = 0;
    while (ini + n < a->tam && a->buf[ini + n] != '\n') n++;
    return n;
}

static int total_linhas(area_t *a)
{
    int n = 1;
    for (int i = 0; i < a->tam; i++) if (a->buf[i] == '\n') n++;
    return n;
}

static void cursor_visivel(area_t *a)
{
    int lin, col, vis = area_linhas_visiveis(a);
    linha_coluna(a, a->cursor, &lin, &col);
    if (lin < a->topo) a->topo = lin;
    if (lin >= a->topo + vis) a->topo = lin - vis + 1;
}

/* Seleção, para copiar e colar. Devolve false se não há. */
static bool selecao(area_t *a, int *ini, int *fim)
{
    if (!a->sel || a->sel - 1 == a->cursor) return false;
    int s = a->sel - 1 > a->tam ? a->tam : a->sel - 1;
    *ini = s < a->cursor ? s : a->cursor;
    *fim = s < a->cursor ? a->cursor : s;
    return true;
}

void area_desenhar(jan_t *j, area_t *a)
{
    int texto_l = a->l - BARRA_ROLAGEM;
    g_retangulo(j, a->x, a->y, a->l, a->a, COR_BRANCO);
    g_moldura(j, a->x, a->y, a->l, a->a, a->foco ? COR_DESTAQUE : COR_BORDA);
    int vis = area_linhas_visiveis(a), cabem = (texto_l - 10) / LETRA_L;
    int lin_cur, col_cur;
    linha_coluna(a, a->cursor, &lin_cur, &col_cur);
    int pos = inicio_da_linha(a, a->topo);
    for (int k = 0; k < vis && pos <= a->tam; k++) {
        int n = tamanho_da_linha(a, pos);
        int mostra = n < cabem ? n : cabem, s0, s1;
        g_texto_n(j, a->x + 5, a->y + 4 + k * LETRA_A, a->buf + pos, mostra, COR_TEXTO);
        if (selecao(a, &s0, &s1)) {
            /* O trecho selecionado desta linha: fundo azul e letras brancas. */
            int i0 = s0 > pos ? s0 - pos : 0, i1 = s1 < pos + n ? s1 - pos : n + (s1 > pos + n ? 1 : 0);
            if (i1 > mostra) i1 = mostra;
            if (i0 < i1) {
                g_retangulo(j, a->x + 5 + i0 * LETRA_L, a->y + 4 + k * LETRA_A, (i1 - i0) * LETRA_L, LETRA_A, COR_SELECAO);
                g_texto_n(j, a->x + 5 + i0 * LETRA_L, a->y + 4 + k * LETRA_A, a->buf + pos + i0, i1 - i0 > n - i0 ? n - i0 : i1 - i0, COR_BRANCO);
            }
        }
        if (a->foco && !a->somente_leitura && a->topo + k == lin_cur && col_cur <= cabem)
            g_retangulo(j, a->x + 5 + col_cur * LETRA_L, a->y + 4 + k * LETRA_A, 2, LETRA_A, COR_TEXTO);
        pos += n + 1;
        if (pos > a->tam) break;
    }
    /* Barra de rolagem: o "polegar" mostra que parte do texto está à vista. */
    int bx = a->x + a->l - BARRA_ROLAGEM, total = total_linhas(a);
    g_retangulo(j, bx, a->y + 1, BARRA_ROLAGEM - 1, a->a - 2, 0xE4E7EA);
    int trilho = a->a - 4;
    int pol_a = total <= vis ? trilho : trilho * vis / total;
    if (pol_a < 12) pol_a = 12;
    int pol_y = total <= vis ? 0 : (trilho - pol_a) * a->topo / (total - vis > 0 ? total - vis : 1);
    g_retangulo(j, bx + 2, a->y + 2 + pol_y, BARRA_ROLAGEM - 5, pol_a, 0x9AA3AC);
    jan_mostrar_area(j, a->x, a->y, a->l, a->a);
}

static void inserir(area_t *a, const char *t, int n)
{
    if (!garantir(a, a->tam + n + 1)) return;
    memmove(a->buf + a->cursor + n, a->buf + a->cursor, (size_t)(a->tam - a->cursor + 1));
    memcpy(a->buf + a->cursor, t, (size_t)n);
    a->tam += n;
    a->cursor += n;
    a->modificado = true;
}

static void remover(area_t *a, int pos)
{
    if (pos < 0 || pos >= a->tam) return;
    memmove(a->buf + pos, a->buf + pos + 1, (size_t)(a->tam - pos));
    a->tam--;
    a->modificado = true;
}

static void mover_vertical(area_t *a, int delta)
{
    int lin, col;
    linha_coluna(a, a->cursor, &lin, &col);
    int nova = lin + delta, total = total_linhas(a);
    if (nova < 0) nova = 0;
    if (nova >= total) nova = total - 1;
    int ini = inicio_da_linha(a, nova), n = tamanho_da_linha(a, ini);
    a->cursor = ini + (col < n ? col : n);
}

/* Posição do texto sob o ponto (x, y) da janela. */
static int posicao_em(area_t *a, int x, int y)
{
    int lin = a->topo + (y - a->y - 4) / LETRA_A, col = (x - a->x - 5 + LETRA_L / 2) / LETRA_L;
    int total = total_linhas(a);
    if (lin >= total) lin = total - 1;
    if (lin < 0) lin = 0;
    int ini = inicio_da_linha(a, lin), n = tamanho_da_linha(a, ini);
    return ini + (col < 0 ? 0 : col > n ? n : col);
}

/* Apaga o que está selecionado (digitar por cima, Backspace, Ctrl+X). */
static bool apagar_selecao(area_t *a)
{
    int s0, s1;
    if (!selecao(a, &s0, &s1)) { a->sel = 0; return false; }
    memmove(a->buf + s0, a->buf + s1, (size_t)(a->tam - s1 + 1));
    a->tam -= s1 - s0;
    a->cursor = s0;
    a->sel = 0;
    a->modificado = true;
    return true;
}

static void copiar_selecao_area(area_t *a)
{
    int s0, s1;
    if (selecao(a, &s0, &s1)) area_copia_gravar(a->buf + s0, (unsigned)(s1 - s0));
}

bool area_tratar(jan_t *j, area_t *a, const evento_t *e)
{
    bool mudou = false;
    if (e->tipo == EV_RODA && dentro(e->x, e->y, a->x, a->y, a->l, a->a)) {   /* roda: 3 linhas por passo (H134) */
        int max = total_linhas(a) - area_linhas_visiveis(a);
        int t = a->topo + 3 * (int)e->tecla;
        a->topo = t > max ? (max > 0 ? max : 0) : t < 0 ? 0 : t;
        area_desenhar(j, a);
        return false;
    }
    /* Arrastar com o botão apertado estende a seleção. */
    if (e->tipo == EV_MOVER && a->arrastando && (e->botoes & 1)) {
        int p = posicao_em(a, e->x, e->y);
        if (p != a->cursor) { a->cursor = p; cursor_visivel(a); area_desenhar(j, a); }
        return false;
    }
    if (e->tipo == EV_SOLTOU) {
        if (a->arrastando && a->sel - 1 == a->cursor) a->sel = 0;
        a->arrastando = false;
        return false;
    }
    if (e->tipo == EV_CLIQUE) {
        bool estava = a->foco;
        a->foco = dentro(e->x, e->y, a->x, a->y, a->l, a->a);
        if (a->foco) {
            if (e->x >= a->x + a->l - BARRA_ROLAGEM) {
                /* Clique na barra de rolagem: uma página para cima ou para baixo. */
                int vis = area_linhas_visiveis(a), total = total_linhas(a);
                a->topo += e->y < a->y + a->a / 2 ? -vis : vis;
                if (a->topo > total - vis) a->topo = total - vis;
                if (a->topo < 0) a->topo = 0;
            } else {
                int p = posicao_em(a, e->x, e->y);
                if ((e->botoes & 1) && (e->modificadores & 1)) {   /* Shift+clique: seleciona até aqui */
                    if (!a->sel) a->sel = a->cursor + 1;
                } else if (e->botoes & 1) {
                    a->sel = p + 1;                                 /* começa uma seleção nova */
                    a->arrastando = true;
                }
                if (e->botoes & 1) a->cursor = p;
            }
        }
        if (a->foco || estava) area_desenhar(j, a);
        return false;
    }
    if (e->tipo != EV_TECLA || !a->foco) return false;
    unsigned t = e->tecla;
    int vis = area_linhas_visiveis(a);
    /* Ctrl+A seleciona tudo; Ctrl+C copia; Ctrl+X recorta; Ctrl+V cola. */
    if (t == 1) { a->sel = 1; a->cursor = a->tam; area_desenhar(j, a); return false; }
    if (t == 3) { copiar_selecao_area(a); return false; }
    if (t == 24 && !a->somente_leitura) { copiar_selecao_area(a); mudou = apagar_selecao(a); cursor_visivel(a); area_desenhar(j, a); return mudou; }
    if (t == 22 && !a->somente_leitura) {
        int n = area_copia_ler(NULL, 0);
        char *c = n > 0 ? aloca((size_t)n + 1) : NULL;
        if (c) {
            n = area_copia_ler(c, (unsigned)n);
            apagar_selecao(a);
            for (int i = 0; i < n; i++) if (c[i] == '\r') c[i] = ' ';
            inserir(a, c, n);
            libera(c);
            mudou = true;
        }
        cursor_visivel(a);
        area_desenhar(j, a);
        return mudou;
    }
    /* Setas com Shift estendem a seleção; sem Shift, ela some. */
    bool movimento = t == TECLA_ESQUERDA || t == TECLA_DIREITA || t == TECLA_CIMA || t == TECLA_BAIXO ||
                     t == TECLA_PGUP || t == TECLA_PGDN || t == TECLA_HOME || t == TECLA_END;
    if (movimento) {
        if (e->modificadores & 1) { if (!a->sel) a->sel = a->cursor + 1; }
        else a->sel = 0;
    } else if (!a->somente_leitura && a->sel && (t == '\b' || t == TECLA_DELETE)) {
        mudou = apagar_selecao(a);
        cursor_visivel(a);
        area_desenhar(j, a);
        return mudou;
    } else if (!a->somente_leitura && a->sel && ((t >= 0x20 && t < 0x100 && t != 0x7F) || t == '\n' || t == '\t')) {
        apagar_selecao(a);   /* digitar por cima troca o selecionado */
    }
    if (t == TECLA_ESQUERDA) { if (a->cursor > 0) a->cursor--; }
    else if (t == TECLA_DIREITA) { if (a->cursor < a->tam) a->cursor++; }
    else if (t == TECLA_CIMA) mover_vertical(a, -1);
    else if (t == TECLA_BAIXO) mover_vertical(a, 1);
    else if (t == TECLA_PGUP) { mover_vertical(a, -vis); a->topo -= vis; if (a->topo < 0) a->topo = 0; }
    else if (t == TECLA_PGDN) { mover_vertical(a, vis); a->topo += vis; }
    else if (t == TECLA_HOME) { int l, c; linha_coluna(a, a->cursor, &l, &c); a->cursor -= c; }
    else if (t == TECLA_END) { int l, c; linha_coluna(a, a->cursor, &l, &c); a->cursor += tamanho_da_linha(a, a->cursor - c) - c; }
    else if (a->somente_leitura) return false;
    else if (t == '\b') { if (a->cursor > 0) { remover(a, a->cursor - 1); a->cursor--; mudou = true; } }
    else if (t == TECLA_DELETE) { if (a->cursor < a->tam) { remover(a, a->cursor); mudou = true; } }
    else if (t == '\n') { inserir(a, "\n", 1); mudou = true; }
    else if (t == '\t') { inserir(a, "    ", 4); mudou = true; }
    else if (t >= 0x20 && t < 0x100 && t != 0x7F) { char c = (char)t; inserir(a, &c, 1); mudou = true; }
    else return false;
    int total = total_linhas(a);
    if (a->topo > total - 1) a->topo = total - 1 > 0 ? total - 1 : 0;
    cursor_visivel(a);
    area_desenhar(j, a);
    return mudou;
}

/* ------------------------------------------------------------ lista */

bool lista_marcado(const lista_t *l, int i)
{
    if (i < 0 || i >= l->qtd) return false;
    return l->marc ? l->marc[i] != 0 : i == l->sel;
}

int lista_qtd_marcados(const lista_t *l)
{
    int n = 0;
    for (int i = 0; i < l->qtd; i++) n += lista_marcado(l, i);
    return n;
}

static int lista_alt(const lista_t *l) { return l->alt_item > 0 ? l->alt_item : LISTA_ALTURA_ITEM; }
static int lista_vis(const lista_t *l) { return (l->a - 4) / lista_alt(l); }
static int lista_tx(const lista_t *l) { return l->x + (l->claro ? 12 : 6) + (l->icone ? 22 : 0); }
/* Onde termina a coluna do nome. */
static int lista_fim_nome(const lista_t *l) { return l->colunas && l->larg_nome > 0 ? l->x + l->larg_nome : l->x + l->l; }

/* Onde termina o texto do item (o laço e o clique "no vazio" usam isso). */
static int lista_fim_texto(const lista_t *l, int i)
{
    int tx = lista_tx(l), n = (int)tam_texto(l->itens[i]), cabe = (lista_fim_nome(l) - tx - 4) / LETRA_L;
    return tx + (n < cabe ? n : cabe) * LETRA_L;
}

static void lista_ed_faixa(const lista_t *l, int *i0, int *i1)
{
    *i0 = l->ed_marca < l->ed_cur ? l->ed_marca : l->ed_cur;
    *i1 = l->ed_marca < l->ed_cur ? l->ed_cur : l->ed_marca;
}

void lista_desenhar(jan_t *j, lista_t *l)
{
    int alt = lista_alt(l), dy = (alt - LETRA_A) / 2;
    g_retangulo(j, l->x, l->y, l->l, l->a, COR_BRANCO);
    if (!l->claro) g_moldura(j, l->x, l->y, l->l, l->a, 0xA9B2BB);
    int vis = lista_vis(l);
    int larg_sel = l->l - 6 - (l->qtd > vis ? 6 : 0);
    int fim_nome = lista_fim_nome(l);
    for (int k = 0; k < vis && l->topo + k < l->qtd; k++) {
        int i = l->topo + k, y = l->y + 2 + k * alt;
        bool sel = lista_marcado(l, i);
        bool ed = l->editando && i == l->ed_item;
        if (l->claro) {   /* azul-claro, texto escuro; a linha sob o mouse mais clara ainda */
            if (sel && !ed) g_ret_redondo(j, l->x + 4, y + 1, larg_sel - 2, alt - 2, 4, 0xCCE8FF, 0xCCE8FF);
            else if (l->sob == i + 1 && !ed) g_ret_redondo(j, l->x + 4, y + 1, larg_sel - 2, alt - 2, 4, 0xEEF5FC, 0xEEF5FC);
            if (l->marc && i == l->sel && !ed && lista_qtd_marcados(l) == 0)
                g_contorno_redondo(j, l->x + 4, y + 1, larg_sel - 2, alt - 2, 4, 0x8DB8E8);
        } else if (sel && !ed) g_ret_redondo(j, l->x + 3, y + 1, larg_sel, alt - 2, 4, COR_SELECAO, COR_SELECAO);   /* 1 pixel de folga: marcados seguidos não grudam */
        else if (l->marc && i == l->sel && !ed && lista_qtd_marcados(l) == 0)
            g_contorno_redondo(j, l->x + 3, y, larg_sel, alt, 4, 0xA9C4EE);   /* o item do teclado */
        int tx = lista_tx(l);
        if (l->icone) l->icone(j, tx - 22, y + (alt - 16) / 2, i);
        if (ed) {
            /* A caixa de edição na própria linha: fundo branco, contorno verde,
             * a parte marcada em azul e o cursor. */
            int cabe = (fim_nome - tx - 10) / LETRA_L;
            int n = (int)tam_texto(l->ed_texto), ini = l->ed_cur > cabe ? l->ed_cur - cabe : 0;
            suave_t sv = sv_de(j);
            suave_ret(&sv, tx - 3, y, fim_nome - tx - 3, alt, 3, COR_BRANCO, COR_BRANCO, 256);
            suave_contorno(&sv, tx - 3, y, fim_nome - tx - 3, alt, 3, COR_DESTAQUE, 256);
            int i0, i1;
            lista_ed_faixa(l, &i0, &i1);
            if (i1 > i0) {
                int a0 = i0 < ini ? ini : i0, a1 = i1 > ini + cabe ? ini + cabe : i1;
                if (a1 > a0) g_retangulo(j, tx + (a0 - ini) * LETRA_L, y + dy, (a1 - a0) * LETRA_L, LETRA_A, 0xBFD6F6);
            }
            g_texto_n(j, tx, y + dy, l->ed_texto + ini, n - ini < cabe ? n - ini : cabe, COR_TEXTO);
            g_retangulo(j, tx + (l->ed_cur - ini) * LETRA_L, y + dy, 2, LETRA_A, COR_TEXTO);
            if (l->colunas) l->colunas(j, y, i);
            continue;
        }
        g_texto_n(j, tx, y + dy, l->itens[i], (fim_nome - tx - 4) / LETRA_L, sel && !l->claro ? COR_BRANCO : COR_TEXTO);
        if (l->colunas) l->colunas(j, y, i);
    }
    if (l->qtd > vis) {   /* indicador simples de que há mais itens */
        int total = l->qtd, trilho = l->a - 4;
        int pol_a = trilho * vis / total, pol_y = (trilho - pol_a) * l->topo / (total - vis);
        g_retangulo(j, l->x + l->l - 6, l->y + 2 + pol_y, 4, pol_a, 0x9AA3AC);
    }
    if (l->laco) {
        /* O laço: azul translúcido, recortado na lista. */
        int x0 = l->laco_x0 < l->laco_x1 ? l->laco_x0 : l->laco_x1, x1 = l->laco_x0 ^ l->laco_x1 ^ x0;
        int y0 = l->laco_y0 < l->laco_y1 ? l->laco_y0 : l->laco_y1, y1 = l->laco_y0 ^ l->laco_y1 ^ y0;
        suave_t sv = sv_de(j);
        sv.x0 = l->x + 1; sv.y0 = l->y + 1; sv.x1 = l->x + l->l - 1; sv.y1 = l->y + l->a - 1;
        suave_ret(&sv, x0, y0, x1 - x0 + 1, y1 - y0 + 1, 2, COR_SELECAO, COR_SELECAO, 64);
        suave_contorno(&sv, x0, y0, x1 - x0 + 1, y1 - y0 + 1, 2, COR_SELECAO, 230);
    }
    jan_mostrar_area(j, l->x, l->y, l->l, l->a);
}

static void lista_garantir_visivel(lista_t *l)
{
    int vis = lista_vis(l);
    if (l->sel < l->topo) l->topo = l->sel;
    if (l->sel >= l->topo + vis) l->topo = l->sel - vis + 1;
    if (l->topo < 0) l->topo = 0;
}

static void lista_desmarcar(lista_t *l)
{
    if (l->marc) for (int i = 0; i < l->qtd; i++) l->marc[i] = 0;
}

/* Marca os itens que o retângulo do laço toca (a linha e o texto). */
static void lista_marcar_laco(lista_t *l)
{
    int x0 = l->laco_x0 < l->laco_x1 ? l->laco_x0 : l->laco_x1, x1 = l->laco_x0 ^ l->laco_x1 ^ x0;
    int y0 = l->laco_y0 < l->laco_y1 ? l->laco_y0 : l->laco_y1, y1 = l->laco_y0 ^ l->laco_y1 ^ y0;
    int vis = lista_vis(l);
    for (int k = 0; k < vis && l->topo + k < l->qtd; k++) {
        int i = l->topo + k, ry0 = l->y + 2 + k * lista_alt(l), ry1 = ry0 + lista_alt(l) - 1;
        bool toca = y0 <= ry1 && ry0 <= y1 && x0 <= lista_fim_texto(l, i) && l->x + 3 <= x1;
        l->marc[i] = toca;
    }
}

void lista_editar(jan_t *j, lista_t *l, int i, int marcar)
{
    if (i < 0 || i >= l->qtd) return;
    l->editando = true;
    l->ed_item = i;
    l->sel = i;
    copia_texto(l->ed_texto, l->itens[i], sizeof l->ed_texto);
    int n = (int)tam_texto(l->ed_texto);
    l->ed_marca = 0;
    l->ed_cur = marcar < 0 || marcar > n ? n : marcar;
    lista_garantir_visivel(l);
    lista_desenhar(j, l);
}

/* Teclas da edição na linha. */
static int lista_tecla_edicao(jan_t *j, lista_t *l, unsigned t)
{
    char *s = l->ed_texto;
    int n = (int)tam_texto(s), i0, i1;
    lista_ed_faixa(l, &i0, &i1);
    if (t == '\n') { l->editando = false; lista_desenhar(j, l); return LISTA_EDITOU; }
    if (t == TECLA_ESC) { l->editando = false; lista_desenhar(j, l); return LISTA_DESISTIU; }
    if ((t == '\b' || t == TECLA_DELETE) && i1 > i0) {           /* apaga a parte marcada */
        memmove(s + i0, s + i1, (size_t)(n - i1 + 1));
        l->ed_cur = l->ed_marca = i0;
    } else if (t == '\b') {
        if (l->ed_cur > 0) { memmove(s + l->ed_cur - 1, s + l->ed_cur, (size_t)(n - l->ed_cur + 1)); l->ed_cur--; }
        l->ed_marca = l->ed_cur;
    } else if (t == TECLA_DELETE) {
        if (l->ed_cur < n) memmove(s + l->ed_cur, s + l->ed_cur + 1, (size_t)(n - l->ed_cur));
        l->ed_marca = l->ed_cur;
    } else if (t == TECLA_ESQUERDA) { l->ed_cur = i1 > i0 ? i0 : l->ed_cur > 0 ? l->ed_cur - 1 : 0; l->ed_marca = l->ed_cur; }
    else if (t == TECLA_DIREITA) { l->ed_cur = i1 > i0 ? i1 : l->ed_cur < n ? l->ed_cur + 1 : n; l->ed_marca = l->ed_cur; }
    else if (t == TECLA_HOME) l->ed_cur = l->ed_marca = 0;
    else if (t == TECLA_END) l->ed_cur = l->ed_marca = n;
    else if (t >= 0x20 && t < 0x100 && t != 0x7F && t != '/') {   /* '/' não pode em nome */
        if (i1 > i0) { memmove(s + i0, s + i1, (size_t)(n - i1 + 1)); n -= i1 - i0; l->ed_cur = i0; }
        if (n < (int)sizeof l->ed_texto - 1) {
            memmove(s + l->ed_cur + 1, s + l->ed_cur, (size_t)(n - l->ed_cur + 1));
            s[l->ed_cur++] = (char)t;
        }
        l->ed_marca = l->ed_cur;
    } else return LISTA_NADA;
    lista_desenhar(j, l);
    return LISTA_NADA;
}

int lista_tratar(jan_t *j, lista_t *l, const evento_t *e)
{
    if (l->editando) {
        if (e->tipo == EV_TECLA) return lista_tecla_edicao(j, l, e->tecla);
        if (e->tipo == EV_CLIQUE) {
            int y = l->y + 2 + (l->ed_item - l->topo) * lista_alt(l), tx = lista_tx(l);
            if (dentro(e->x, e->y, tx - 3, y, lista_fim_nome(l) - tx - 3, lista_alt(l))) {
                int c = (e->x - tx + LETRA_L / 2) / LETRA_L, n = (int)tam_texto(l->ed_texto);
                l->ed_cur = l->ed_marca = c < 0 ? 0 : c > n ? n : c;
                lista_desenhar(j, l);
                return LISTA_NADA;
            }
            l->editando = false;                    /* clicou fora: termina */
            lista_desenhar(j, l);
            return LISTA_EDITOU;
        }
        return LISTA_NADA;
    }
    if (l->marc && l->laco) {
        if (e->tipo == EV_MOVER && (e->botoes & 1)) {
            int x = e->x, y = e->y;
            l->laco_x1 = x < l->x + 1 ? l->x + 1 : x >= l->x + l->l - 1 ? l->x + l->l - 2 : x;
            l->laco_y1 = y < l->y + 1 ? l->y + 1 : y >= l->y + l->a - 1 ? l->y + l->a - 2 : y;
            lista_marcar_laco(l);
            lista_desenhar(j, l);
            return LISTA_NADA;
        }
        if (e->tipo == EV_SOLTOU || e->tipo == EV_MOVER) {
            l->laco = false;
            int dx = l->laco_x1 - l->laco_x0, dy = l->laco_y1 - l->laco_y0;
            if (l->laco_linha > 0 && dx * dx + dy * dy < 16) {   /* não arrastou: foi um clique na linha */
                int i = l->laco_linha - 1;
                lista_desmarcar(l);
                l->marc[i] = 1;
                l->sel = i;
                lista_desenhar(j, l);
                return LISTA_SELECIONOU;
            }
            for (int i = 0; i < l->qtd; i++) if (l->marc[i]) { l->sel = i; break; }
            lista_desenhar(j, l);
            return LISTA_SELECIONOU;
        }
    }
    if (e->tipo == EV_RODA && dentro(e->x, e->y, l->x, l->y, l->l, l->a)) {   /* roda: 3 itens por passo (H134) */
        int max = l->qtd - lista_vis(l);
        int t = l->topo + 3 * (int)e->tecla;
        l->topo = t > max ? (max > 0 ? max : 0) : t < 0 ? 0 : t;
        lista_desenhar(j, l);
        return LISTA_NADA;
    }
    if (l->claro && e->tipo == EV_MOVER && !(e->botoes & 1)) {   /* a linha sob o mouse */
        int k = (e->y - l->y - 2) / lista_alt(l), i = l->topo + k;
        int sob = dentro(e->x, e->y, l->x, l->y, l->l, l->a) && e->y >= l->y + 2 && k < lista_vis(l) && i < l->qtd ? i + 1 : 0;
        if (sob != l->sob) { l->sob = sob; lista_desenhar(j, l); }
        return LISTA_NADA;
    }
    if (e->tipo == EV_CLIQUE && dentro(e->x, e->y, l->x, l->y, l->l, l->a)) {
        int i = l->topo + (e->y - l->y - 2) / lista_alt(l);
        bool no_item = i >= 0 && i < l->qtd && (e->y - l->y - 2) >= 0 && e->x <= lista_fim_texto(l, i) + 12;
        if (!l->marc) {
            if (i < 0 || i >= l->qtd) return LISTA_NADA;
            l->sel = i;
            lista_desenhar(j, l);
            return e->cliques >= 2 ? LISTA_ATIVOU : LISTA_SELECIONOU;
        }
        bool ctrl = (e->modificadores & 2) != 0, shift = (e->modificadores & 1) != 0;
        if (e->botoes & 2) {                          /* botão direito */
            if (no_item) { if (!l->marc[i]) { lista_desmarcar(l); l->marc[i] = 1; } l->sel = i; }
            else { lista_desmarcar(l); l->sel = -1; }
            lista_desenhar(j, l);
            return LISTA_SELECIONOU;
        }
        bool na_linha = i >= 0 && i < l->qtd && (e->y - l->y - 2) >= 0;
        if (!no_item && na_linha && e->cliques >= 2) {   /* duplo clique no fim da linha: abre o item */
            lista_desmarcar(l); l->marc[i] = 1; l->sel = i;
            lista_desenhar(j, l);
            return LISTA_ATIVOU;
        }
        if (!no_item) {                               /* no vazio: começa o laço */
            if (!ctrl) lista_desmarcar(l);
            l->laco = true;
            l->laco_linha = na_linha ? i + 1 : 0;
            l->laco_x0 = l->laco_x1 = e->x;
            l->laco_y0 = l->laco_y1 = e->y;
            lista_desenhar(j, l);
            return LISTA_SELECIONOU;
        }
        /* Clique simples no nome do item que já era o único marcado: pode ser
         * o começo de um "renomear devagar" (o programa espera o duplo clique
         * não vir antes de abrir a edição). */
        bool era_unico = !ctrl && !shift && e->cliques < 2 && l->sel == i && l->marc[i] && lista_qtd_marcados(l) == 1;
        if (ctrl) l->marc[i] = !l->marc[i];
        else if (shift && l->sel >= 0) {
            int a = l->sel < i ? l->sel : i, b = l->sel < i ? i : l->sel;
            lista_desmarcar(l);
            for (int k = a; k <= b; k++) l->marc[k] = 1;
        } else { lista_desmarcar(l); l->marc[i] = 1; }
        if (!shift) l->sel = i;
        lista_desenhar(j, l);
        if (era_unico) return LISTA_DEVAGAR;
        return e->cliques >= 2 && !ctrl && !shift ? LISTA_ATIVOU : LISTA_SELECIONOU;
    }
    if (e->tipo != EV_TECLA || l->qtd == 0) return LISTA_NADA;
    if (e->tecla == '\n' && l->sel >= 0) return LISTA_ATIVOU;
    if (l->marc && e->tecla == 1) {                    /* Ctrl+A */
        for (int i = 0; i < l->qtd; i++) l->marc[i] = 1;
        lista_desenhar(j, l);
        return LISTA_SELECIONOU;
    }
    int antes = l->sel;
    if (e->tecla == TECLA_BAIXO && l->sel < l->qtd - 1) l->sel++;
    else if (e->tecla == TECLA_CIMA && l->sel > 0) l->sel--;
    else if (e->tecla == TECLA_HOME) l->sel = 0;
    else if (e->tecla == TECLA_END) l->sel = l->qtd - 1;
    if (l->sel == antes) return LISTA_NADA;
    if (l->marc) { lista_desmarcar(l); l->marc[l->sel] = 1; }
    lista_garantir_visivel(l);
    lista_desenhar(j, l);
    return LISTA_SELECIONOU;
}

/* ------------------------------------------------------------ diálogos */

/* Escreve um texto com '\n' em várias linhas. */
static void texto_multilinha(jan_t *j, int x, int y, const char *t)
{
    while (*t) {
        const char *fim = acha_char(t, '\n');
        int n = fim ? (int)(fim - t) : (int)tam_texto(t);
        g_texto_n(j, x, y, t, n, COR_TEXTO);
        y += LETRA_A + 2;
        t += n;
        if (*t == '\n') t++;
    }
}

static int linhas_de(const char *t)
{
    int n = 1;
    for (; *t; t++) if (*t == '\n') n++;
    return n;
}

static int dialogo(const char *titulo, const char *texto, const char *b1, const char *b2, caixa_t *caixa)
{
    int alt = 70 + linhas_de(texto) * (LETRA_A + 2) + (caixa ? 34 : 0);
    /* Largura: 380, ou a da linha mais comprida (até 900), para mensagens
     * como as do compilador caberem inteiras. */
    int larg = 380, n = 0;
    for (const char *p = texto; ; p++) {
        if (*p == '\n' || !*p) { if (n * LETRA_L + 32 > larg) larg = n * LETRA_L + 32; n = 0; if (!*p) break; }
        else n++;
    }
    if (larg > 900) larg = 900;
    if (alt > 680) alt = 680;
    jan_t *j = jan_abrir(titulo, larg, alt);
    if (!j) return 0;
    texto_multilinha(j, 16, 16, texto);
    botao_t bs[2] = { { larg - 16 - 90, alt - 40, 90, 26, b1, false }, { larg - 16 - 190, alt - 40, 90, 26, b2, false } };
    int qtd = b2 ? 2 : 1;
    if (caixa) {
        caixa->x = 16; caixa->y = alt - 40 - 36; caixa->l = larg - 32; caixa->foco = true;
        caixa_desenhar(j, caixa);
    }
    for (int i = 0; i < qtd; i++) botao_desenhar(j, &bs[i]);
    jan_mostrar(j);
    int resposta = 0;
    evento_t e;
    while (!resposta) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) { resposta = 2; break; }
        if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) { resposta = qtd == 2 ? 2 : 1; break; }
        if (caixa && caixa_tratar(j, caixa, &e)) { resposta = 1; break; }
        if (!caixa && e.tipo == EV_TECLA && e.tecla == '\n') { resposta = 1; break; }
        for (int i = 0; i < qtd; i++)
            if (botao_tratar(j, &bs[i], &e)) resposta = i + 1;
    }
    jan_fechar(j);
    return resposta;
}

bool confirmar(const char *titulo, const char *pergunta)
{
    return dialogo(titulo, pergunta, "Sim", "Não", NULL) == 1;
}

void mensagem(const char *titulo, const char *texto)
{
    dialogo(titulo, texto, "OK", NULL, NULL);
}

bool pedir_texto(const char *titulo, const char *pergunta, char *resposta, int max)
{
    caixa_t c;
    memset(&c, 0, sizeof c);
    caixa_definir(&c, resposta);
    if (dialogo(titulo, pergunta, "OK", "Cancelar", &c) != 1) return false;
    copia_texto(resposta, c.texto, (size_t)max);
    return true;
}

/* ------------------------------------------------------------ tamanho (H78) */

bool jan_redimensionavel(jan_t *j, int larg_min, int alt_min)
{
    return janela_opcoes(j->id, JAN_REDIMENSIONAVEL, larg_min, alt_min) >= 0;
}

void jan_novo_tamanho(jan_t *j, const evento_t *e)
{
    if (e->tipo != EV_REDIMENSIONAR) return;
    j->larg = e->x;
    j->alt = e->y;
}

/* Menu do próprio programa no ponto (x, y) da janela (H81): itens separados
 * por "|"; a escolha chega como EV_MENU com o índice em e.tecla. */
void jan_menu(jan_t *j, int x, int y, const char *itens) { menu_janela(j->id, x, y, itens); }
