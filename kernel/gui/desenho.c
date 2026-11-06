/* desenho.c - rotinas de desenho com recorte, usadas pela área de trabalho
 * e pelo gerenciador de janelas. Tudo é recortado contra um retângulo, para
 * que redesenhar só um pedaço da tela nunca pinte fora dele. */
#include "desenho.h"
#include "texto.h"

extern const unsigned char fonte_sagui[256][16];

void sup_iniciar(superficie_t *s, uint32_t *px, int larg, int alt)
{
    s->px = px; s->larg = larg; s->alt = alt;
    sup_recorte(s, 0, 0, 0, 0);
}

void sup_recorte(superficie_t *s, int x, int y, int l, int a)
{
    if (l <= 0 || a <= 0) { s->cx0 = 0; s->cy0 = 0; s->cx1 = s->larg; s->cy1 = s->alt; return; }
    s->cx0 = x < 0 ? 0 : x;
    s->cy0 = y < 0 ? 0 : y;
    s->cx1 = x + l > s->larg ? s->larg : x + l;
    s->cy1 = y + a > s->alt ? s->alt : y + a;
}

void sup_retangulo(superficie_t *s, int x, int y, int l, int a, uint32_t cor)
{
    int x0 = x < s->cx0 ? s->cx0 : x, y0 = y < s->cy0 ? s->cy0 : y;
    int x1 = x + l > s->cx1 ? s->cx1 : x + l, y1 = y + a > s->cy1 ? s->cy1 : y + a;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = s->px + j * s->larg;
        for (int i = x0; i < x1; i++) p[i] = cor;
    }
}

void sup_moldura(superficie_t *s, int x, int y, int l, int a, uint32_t cor)
{
    sup_retangulo(s, x, y, l, 1, cor);
    sup_retangulo(s, x, y + a - 1, l, 1, cor);
    sup_retangulo(s, x, y, 1, a, cor);
    sup_retangulo(s, x + l - 1, y, 1, a, cor);
}

static inline void ponto(superficie_t *s, int x, int y, uint32_t cor)
{
    if (x >= s->cx0 && x < s->cx1 && y >= s->cy0 && y < s->cy1)
        s->px[y * s->larg + x] = cor;
}

/* Interpolação de cada canal em inteiros. */
uint32_t cor_mistura(uint32_t a, uint32_t b, int num, int den)
{
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int ca = (int)((a >> s) & 255), cb = (int)((b >> s) & 255);
        r |= (uint32_t)(ca + (cb - ca) * num / den) << s;
    }
    return r;
}

static inline void ponto_misturado(superficie_t *s, int x, int y, uint32_t cor, int num, int den)
{
    if (x >= s->cx0 && x < s->cx1 && y >= s->cy0 && y < s->cy1) {
        uint32_t *p = &s->px[y * s->larg + x];
        *p = cor_mistura(*p, cor, num, den);
    }
}

/* Suaviza a escada das diagonais: um pixel vazio com dois vizinhos acesos em
 * "L" recebe um pouco da cor da letra. */
static inline bool aceso(const unsigned char *g, int i, int j)
{
    return i >= 0 && i < 8 && j >= 0 && j < 16 && (g[j] & (0x80 >> i));
}

static inline bool quina(const unsigned char *g, int i, int j)
{
    bool e = aceso(g, i - 1, j), d = aceso(g, i + 1, j), c = aceso(g, i, j - 1), b = aceso(g, i, j + 1);
    return (e != d) && (c != b);
}

int texto_largura(const char *t)
{
    int n = 0;
    while (t[n]) n++;
    return n * 8;
}

void sup_texto(superficie_t *s, int x, int y, const char *t, uint32_t cor)
{
    for (; *t; t++, x += 8) {
        if (x >= s->cx1) break;
        if (x + 8 <= s->cx0) continue;
        const unsigned char *g = fonte_sagui[(uint8_t)*t];
        for (int j = 0; j < 16; j++)
            for (int i = 0; i < 8; i++)
                if (g[j] & (0x80 >> i)) ponto(s, x + i, y + j, cor);
                else if (quina(g, i, j)) ponto_misturado(s, x + i, y + j, cor, 1, 3);
    }
}

/* Texto mais grosso, para títulos: a mesma letra desenhada duas vezes. */
void sup_texto_forte(superficie_t *s, int x, int y, const char *t, uint32_t cor)
{
    sup_texto(s, x, y, t, cor);
    sup_texto(s, x + 1, y, t, cor);
}

void sup_icone(superficie_t *s, int x, int y, const uint32_t *argb, int lado)
{
    for (int j = 0; j < lado; j++)
        for (int i = 0; i < lado; i++) {
            uint32_t c = argb[j * lado + i];
            if (c >> 24) ponto_misturado(s, x + i, y + j, c & 0xFFFFFF, (int)(c >> 24), 255);
        }
}

void sup_icone_escala(superficie_t *s, int x, int y, const uint32_t *argb, int lado, int destino)
{
    for (int j = 0; j < destino; j++)
        for (int i = 0; i < destino; i++) {
            uint32_t c = argb[(j * lado / destino) * lado + i * lado / destino];
            if (c >> 24) ponto_misturado(s, x + i, y + j, c & 0xFFFFFF, (int)(c >> 24), 255);
        }
}

void sup_escurecer(superficie_t *s, int x, int y, int l, int a, int alfa)
{
    int x0 = x < s->cx0 ? s->cx0 : x, y0 = y < s->cy0 ? s->cy0 : y;
    int x1 = x + l > s->cx1 ? s->cx1 : x + l, y1 = y + a > s->cy1 ? s->cy1 : y + a;
    uint32_t fica = (uint32_t)(256 - alfa);
    for (int j = y0; j < y1; j++) {
        uint32_t *p = s->px + j * s->larg;
        for (int i = x0; i < x1; i++) {
            uint32_t c = p[i];
            p[i] = (((c & 0xFF00FF) * fica >> 8) & 0xFF00FF) | (((c & 0x00FF00) * fica >> 8) & 0x00FF00);
        }
    }
}

void sup_degrade(superficie_t *s, int x, int y, int l, int a, uint32_t topo, uint32_t base)
{
    for (int j = 0; j < a; j++)
        sup_retangulo(s, x, y + j, l, 1, cor_mistura(topo, base, j, a > 1 ? a - 1 : 1));
}

void sup_desenho(superficie_t *s, int x, int y, const desenho_t *d, int escala)
{
    for (int j = 0; j < d->altura; j++)
        for (int i = 0; i < d->largura; i++) {
            uint32_t cor;
            if (arte_cor(d->linhas[j][i], &cor))
                sup_retangulo(s, x + i * escala, y + j * escala, escala, escala, cor);
        }
}

void sup_copiar(superficie_t *s, int x, int y, const uint32_t *origem, int larg, int alt, int passo)
{
    int x0 = x < s->cx0 ? s->cx0 : x, y0 = y < s->cy0 ? s->cy0 : y;
    int x1 = x + larg > s->cx1 ? s->cx1 : x + larg, y1 = y + alt > s->cy1 ? s->cy1 : y + alt;
    for (int j = y0; j < y1; j++) {
        const uint32_t *o = origem + (j - y) * passo + (x0 - x);
        if (x1 > x0) memcpy(s->px + j * s->larg + x0, o, (uint32_t)(x1 - x0) * 4);
    }
}
