/* gui.c - kit gráfico da libsagui (H22): desenho e componentes no anel 3.
 *
 * O programa desenha nos pixels da própria janela (memória que o kernel
 * compartilha com ele) e chama jan_mostrar para o kernel exibir. Os
 * componentes não guardam nada escondido: cada um é uma estrutura do
 * programa, e o programa decide quando desenhar e para quem mandar cada
 * evento. É simples de entender e de testar. */
#include "gui.h"

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

jan_t *jan_abrir_redimensionavel(const char *titulo, int largura, int altura)
{
    jan_t *j = aloca(sizeof(jan_t));
    if (!j) return NULL;
    j->id = criar_janela(titulo, largura, altura | JANELA_REDIMENSIONAVEL, &j->px);
    if (j->id < 0) { libera(j); return NULL; }
    j->larg = largura;
    j->alt = altura;
    g_retangulo(j, 0, 0, largura, altura, COR_FUNDO);
    return j;
}

void jan_tamanho(jan_t *j, const evento_t *e)
{
    j->larg = e->x;
    j->alt = e->y;
}

void jan_menu(jan_t *j, int x, int y, const char *itens) { menu_janela(j->id, x, y, itens); }

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

static inline void ponto(jan_t *j, int x, int y, uint32_t cor)
{
    if ((unsigned)x < (unsigned)j->larg && (unsigned)y < (unsigned)j->alt)
        j->px[y * j->larg + x] = cor;
}

static uint32_t mistura(uint32_t a, uint32_t b, int num, int den)
{
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int ca = (int)((a >> s) & 255), cb = (int)((b >> s) & 255);
        r |= (uint32_t)(ca + (cb - ca) * num / den) << s;
    }
    return r;
}

/* Suaviza a escada das diagonais, como o kernel faz nos títulos. */
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
    /* Degradê com cantos cortados. Os pixels dos cantos ficam como estavam,
     * então o botão serve sobre qualquer fundo. */
    uint32_t topo = b->apertado ? COR_BOTAO_APERTADO : 0xFBFCFD, base = b->apertado ? 0xD6DBE0 : 0xD8DDE2;
    for (int k = 0; k < b->a; k++)
        g_retangulo(j, b->x, b->y + k, b->l, 1, mistura(topo, base, k, b->a > 1 ? b->a - 1 : 1));
    g_retangulo(j, b->x + 1, b->y - 1, b->l - 2, 1, COR_BORDA);
    g_retangulo(j, b->x + 1, b->y + b->a, b->l - 2, 1, 0x6E7780);
    g_retangulo(j, b->x - 1, b->y + 1, 1, b->a - 2, COR_BORDA);
    g_retangulo(j, b->x + b->l, b->y + 1, 1, b->a - 2, COR_BORDA);
    ponto(j, b->x, b->y, COR_BORDA);
    ponto(j, b->x + b->l - 1, b->y, COR_BORDA);
    ponto(j, b->x, b->y + b->a - 1, COR_BORDA);
    ponto(j, b->x + b->l - 1, b->y + b->a - 1, COR_BORDA);
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

#define ARQ_TEXTO "/tmp/.texto"

void transferir_texto(const char *t, int n)
{
    int fd = abrir(ARQ_TEXTO, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) return;
    escrever_tudo(fd, t, (unsigned)n);
    fechar(fd);
}

int colar_texto(char *d, int max)
{
    int fd = abrir(ARQ_TEXTO, ABRIR_LER), n = 0, r;
    if (fd < 0) return 0;
    while (n < max && (r = ler(fd, d + n, (unsigned)(max - n))) > 0) n += r;
    fechar(fd);
    return n;
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
    if (t == 3) { transferir_texto(c->texto, n); return false; }        /* Ctrl+C: a caixa inteira */
    if (t == 22) {                                                      /* Ctrl+V */
        char b[128];
        int k = colar_texto(b, (int)sizeof c->texto - 1 - n);
        memmove(c->texto + c->cursor + k, c->texto + c->cursor, (size_t)(n - c->cursor + 1));
        memcpy(c->texto + c->cursor, b, (size_t)k);
        c->cursor += k;
        caixa_desenhar(j, c);
        return false;
    }
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
        if (a->ancora) {                     /* faixa da seleção nesta linha */
            int s0 = a->ancora - 1 < a->cursor ? a->ancora - 1 : a->cursor;
            int s1 = a->ancora - 1 < a->cursor ? a->cursor : a->ancora - 1;
            int c0 = s0 > pos ? s0 - pos : 0, c1 = s1 < pos + n ? s1 - pos : n;
            if (c0 > cabem) c0 = cabem;
            if (c1 > cabem) c1 = cabem;
            if (s0 <= pos + n && s1 >= pos && c1 > c0)
                g_retangulo(j, a->x + 5 + c0 * LETRA_L, a->y + 4 + k * LETRA_A, (c1 - c0) * LETRA_L, LETRA_A, 0xBFE8CD);
        }
        g_texto_n(j, a->x + 5, a->y + 4 + k * LETRA_A, a->buf + pos, n < cabem ? n : cabem, COR_TEXTO);
        if (a->foco && !a->somente_leitura && a->topo + k == lin_cur && col_cur <= cabem)
            g_retangulo(j, a->x + 5 + col_cur * LETRA_L, a->y + 4 + k * LETRA_A, 2, LETRA_A, COR_TEXTO);
        pos += n + 1;
        if (pos > a->tam) break;
    }
    /* Barra de rolagem: o "polegar" mostra que parte do texto está à vista. */
    int bx = a->x + a->l - BARRA_ROLAGEM, total = total_linhas(a);
    g_retangulo(j, bx, a->y + 1, BARRA_ROLAGEM - 1, a->a - 2, 0xEEF0F2);
    int trilho = a->a - 4;
    int pol_a = total <= vis ? trilho : trilho * vis / total;
    if (pol_a < 12) pol_a = 12;
    int pol_y = total <= vis ? 0 : (trilho - pol_a) * a->topo / (total - vis > 0 ? total - vis : 1);
    g_retangulo(j, bx + 2, a->y + 2 + pol_y, BARRA_ROLAGEM - 5, pol_a, 0xB0B8C0);
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

static int area_posicao(area_t *a, int x, int y)
{
    int lin = a->topo + (y - a->y - 4) / LETRA_A, col = (x - a->x - 5 + LETRA_L / 2) / LETRA_L;
    int total = total_linhas(a);
    if (lin >= total) lin = total - 1;
    if (lin < 0) lin = 0;
    int ini = inicio_da_linha(a, lin), n = tamanho_da_linha(a, ini);
    return ini + (col < 0 ? 0 : col > n ? n : col);
}

/* Apaga o trecho selecionado. Devolve true se havia um. */
static bool apagar_selecao(area_t *a)
{
    if (!a->ancora) return false;
    int s0 = a->ancora - 1 < a->cursor ? a->ancora - 1 : a->cursor;
    int s1 = a->ancora - 1 < a->cursor ? a->cursor : a->ancora - 1;
    for (int k = s0; k < s1; k++) remover(a, s0);
    a->cursor = s0;
    a->ancora = 0;
    return s1 > s0;
}

static void copiar_selecao(area_t *a)
{
    if (!a->ancora) return;
    int s0 = a->ancora - 1 < a->cursor ? a->ancora - 1 : a->cursor;
    int s1 = a->ancora - 1 < a->cursor ? a->cursor : a->ancora - 1;
    transferir_texto(a->buf + s0, s1 - s0);
}

bool area_tratar(jan_t *j, area_t *a, const evento_t *e)
{
    bool mudou = false;
    if (e->tipo == EV_MOVER && (e->botoes & 1) && a->foco && dentro(e->x, e->y, a->x, a->y, a->l - BARRA_ROLAGEM, a->a)) {
        int p = area_posicao(a, e->x, e->y);
        if (!a->ancora) a->ancora = a->cursor + 1;
        a->cursor = p;
        if (a->ancora - 1 == p) a->ancora = 0;
        area_desenhar(j, a);
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
                a->cursor = area_posicao(a, e->x, e->y);
                a->ancora = 0;
            }
        }
        if (a->foco || estava) area_desenhar(j, a);
        return false;
    }
    if (e->tipo != EV_TECLA || !a->foco) return false;
    unsigned t = e->tecla;
    int vis = area_linhas_visiveis(a);
    bool anda = t == TECLA_ESQUERDA || t == TECLA_DIREITA || t == TECLA_CIMA || t == TECLA_BAIXO ||
                t == TECLA_PGUP || t == TECLA_PGDN || t == TECLA_HOME || t == TECLA_END;
    if (anda) {
        if (!(e->modificadores & 1)) a->ancora = 0;
        else if (!a->ancora) a->ancora = a->cursor + 1;
    }
    if (t == 1) { a->ancora = 1; a->cursor = a->tam; area_desenhar(j, a); return false; }       /* Ctrl+A */
    if (t == 3) { copiar_selecao(a); return false; }                                            /* Ctrl+C */
    if (t == 24 && !a->somente_leitura) {                                                       /* Ctrl+X */
        copiar_selecao(a);
        mudou = apagar_selecao(a);
        cursor_visivel(a);
        area_desenhar(j, a);
        return mudou;
    }
    if (t == 22 && !a->somente_leitura) {                                                       /* Ctrl+V */
        static char b[16384];
        apagar_selecao(a);
        int n = colar_texto(b, (int)sizeof b);
        if (n) inserir(a, b, n);
        cursor_visivel(a);
        area_desenhar(j, a);
        return true;
    }
    if (!anda && !a->somente_leitura && a->ancora && (t == '\b' || t == TECLA_DELETE || t == '\n' || (t >= 0x20 && t < 0x100))) {
        bool so_apaga = t == '\b' || t == TECLA_DELETE;
        apagar_selecao(a);
        if (so_apaga) { cursor_visivel(a); area_desenhar(j, a); return true; }
        mudou = true;
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

void lista_desenhar(jan_t *j, lista_t *l)
{
    g_retangulo(j, l->x, l->y, l->l, l->a, COR_BRANCO);
    g_moldura(j, l->x, l->y, l->l, l->a, COR_BORDA);
    int vis = (l->a - 4) / LISTA_ALTURA_ITEM;
    for (int k = 0; k < vis && l->topo + k < l->qtd; k++) {
        int i = l->topo + k, y = l->y + 2 + k * LISTA_ALTURA_ITEM;
        bool sel = i == l->sel || (l->marcas && l->marcas[i]);
        if (sel) g_retangulo(j, l->x + 2, y, l->l - 4, LISTA_ALTURA_ITEM, COR_SELECAO);
        int tx = l->x + 6;
        if (l->icone) { l->icone(j, tx, y + 2, i); tx += 22; }
        g_texto_n(j, tx, y + 2, l->itens[i], (l->x + l->l - tx - 4) / LETRA_L, sel ? COR_BRANCO : COR_TEXTO);
    }
    if (l->qtd > vis) {   /* indicador simples de que há mais itens */
        int total = l->qtd, trilho = l->a - 4;
        int pol_a = trilho * vis / total, pol_y = (trilho - pol_a) * l->topo / (total - vis);
        g_retangulo(j, l->x + l->l - 6, l->y + 2 + pol_y, 4, pol_a, 0xB0B8C0);
    }
    jan_mostrar_area(j, l->x, l->y, l->l, l->a);
}

static void lista_garantir_visivel(lista_t *l)
{
    int vis = (l->a - 4) / LISTA_ALTURA_ITEM;
    if (l->sel < l->topo) l->topo = l->sel;
    if (l->sel >= l->topo + vis) l->topo = l->sel - vis + 1;
    if (l->topo < 0) l->topo = 0;
}

int lista_tratar(jan_t *j, lista_t *l, const evento_t *e)
{
    if (e->tipo == EV_CLIQUE && dentro(e->x, e->y, l->x, l->y, l->l, l->a)) {
        int i = l->topo + (e->y - l->y - 2) / LISTA_ALTURA_ITEM;
        if (i < 0 || i >= l->qtd) return LISTA_NADA;
        if (l->marcas) {
            if (e->modificadores & 2) {                     /* Ctrl: liga e desliga um item */
                if (l->sel >= 0 && !l->marcas[l->sel] && l->sel != i) l->marcas[l->sel] = 1;
                l->marcas[i] = !l->marcas[i];
            } else if ((e->modificadores & 1) && l->sel >= 0) {   /* Shift: faixa */
                int a0 = l->sel < i ? l->sel : i, a1 = l->sel < i ? i : l->sel;
                memset(l->marcas, 0, (size_t)l->qtd);
                for (int k = a0; k <= a1; k++) l->marcas[k] = 1;
            } else {
                memset(l->marcas, 0, (size_t)l->qtd);
            }
        }
        l->sel = i;
        lista_desenhar(j, l);
        return e->cliques >= 2 ? LISTA_ATIVOU : LISTA_SELECIONOU;
    }
    if (e->tipo != EV_TECLA || l->qtd == 0) return LISTA_NADA;
    if (e->tecla == '\n' && l->sel >= 0) return LISTA_ATIVOU;
    int antes = l->sel;
    if (e->tecla == TECLA_BAIXO && l->sel < l->qtd - 1) l->sel++;
    else if (e->tecla == TECLA_CIMA && l->sel > 0) l->sel--;
    else if (e->tecla == TECLA_HOME) l->sel = 0;
    else if (e->tecla == TECLA_END) l->sel = l->qtd - 1;
    if (l->sel == antes) return LISTA_NADA;
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
    jan_t *j = jan_abrir(titulo, 380, alt);
    if (!j) return 0;
    texto_multilinha(j, 16, 16, texto);
    botao_t bs[2] = { { 380 - 16 - 90, alt - 40, 90, 26, b1, false }, { 380 - 16 - 190, alt - 40, 90, 26, b2, false } };
    int qtd = b2 ? 2 : 1;
    if (caixa) {
        caixa->x = 16; caixa->y = alt - 40 - 36; caixa->l = 348; caixa->foco = true;
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
