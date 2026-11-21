/* desenho.c - rotinas de desenho com recorte, usadas pela área de trabalho
 * e pelo gerenciador de janelas. Tudo é recortado contra um retângulo, para
 * que redesenhar só um pedaço da tela nunca pinte fora dele. */
#include "desenho.h"
#include "texto.h"

extern const unsigned char fonte_sagui[256][16];
/* Fonte suave (tools/mkfonte_suave.py): cada pixel da letra tem um tom de
 * 0 (vazio) a 15 (cheio), já calculado no build; desenhar é só misturar a
 * cor nesse tom, e só nas bordas da letra (o miolo é cor cheia). As faixas
 * sem desenho na fonte suave (controle e 0x80-0x9F, as molduras e blocos do
 * terminal) continuam com a fonte bitmap. */
extern const unsigned char fonte_suave[256][64], fonte_suave_negrito[256][64];
static inline bool letra_suave(unsigned c) { return c >= 0x20 && c != 0x7F && (c < 0x80 || c >= 0xA0); }
static inline int tom_letra(const unsigned char *g, int col, int lin)
{
    unsigned char b = g[lin * 4 + (col >> 1)];
    return col & 1 ? b & 15 : b >> 4;
}
/* fundo levado t/15 na direção da cor, sem divisão (t * 17 / 256 ~ t / 15). */
static inline uint32_t no_tom(uint32_t fundo, uint32_t cor, int t)
{
    int a = t * 17 + (t >> 1);
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int f = (int)((fundo >> s) & 255), c = (int)((cor >> s) & 255);
        r |= (uint32_t)(f + (((c - f) * a + 128) >> 8)) << s;
    }
    return r;
}

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

/* Mistura de duas cores, canal a canal, em inteiros: num/den de b. As
 * rotinas de cor_mistura até sup_degrade e o suavizado das letras vieram do
 * pull request #1 do Robson de Lima Barbosa (H81). */
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

/* Suaviza a escada das diagonais: um pixel apagado com dois vizinhos acesos
 * em "L" (um na horizontal, outro na vertical) recebe 1/3 da cor da letra. */
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

static void texto_face(superficie_t *s, int x, int y, const char *t, uint32_t cor, const unsigned char (*face)[64])
{
    for (; *t; t++, x += 8) {
        if (x >= s->cx1) break;
        if (x + 8 <= s->cx0) continue;
        unsigned c = (uint8_t)*t;
        if (!letra_suave(c)) {
            const unsigned char *g = fonte_sagui[c];
            for (int j = 0; j < 16; j++)
                for (int i = 0; i < 8; i++)
                    if (g[j] & (0x80 >> i)) ponto(s, x + i, y + j, cor);
                    else if (quina(g, i, j)) ponto_misturado(s, x + i, y + j, cor, 1, 3);
            continue;
        }
        const unsigned char *g = face[c];
        for (int j = 0; j < 16; j++) {
            int yy = y + j;
            if (yy < s->cy0 || yy >= s->cy1) continue;
            uint32_t *linha = s->px + yy * s->larg;
            for (int i = 0; i < 8; i++) {
                int tt = tom_letra(g, i, j), xx = x + i;
                if (!tt || xx < s->cx0 || xx >= s->cx1) continue;
                linha[xx] = tt == 15 ? cor : no_tom(linha[xx], cor, tt);
            }
        }
    }
}

void sup_texto(superficie_t *s, int x, int y, const char *t, uint32_t cor) { texto_face(s, x, y, t, cor, fonte_suave); }

/* Texto mais grosso, para títulos: a face negrita da fonte suave, na mesma
 * largura (antes era a letra desenhada duas vezes, e as letras colavam). */
void sup_texto_forte(superficie_t *s, int x, int y, const char *t, uint32_t cor) { texto_face(s, x, y, t, cor, fonte_suave_negrito); }

/* Ícone ARGB (tools/mkarte.py): o alfa diz quanto da cor cobre o fundo. */
/* O mesmo que cor_mistura(a, b, al, 255), sem as três divisões: t / 255,
 * truncado para zero, é (|t| * 0x8081) >> 23 para todo |t| <= 255 * 255
 * (conferido para todos). */
static inline uint32_t mistura_255(uint32_t a, uint32_t b, int al)
{
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int ca = (int)((a >> s) & 255), t = ((int)((b >> s) & 255) - ca) * al;
        int q = (int)(((uint32_t)(t < 0 ? -t : t) * 0x8081u) >> 23);
        r |= (uint32_t)(ca + (t < 0 ? -q : q)) << s;
    }
    return r;
}

void sup_icone(superficie_t *s, int x, int y, const uint32_t *argb, int lado)
{
    if (x >= s->cx1 || y >= s->cy1 || x + lado <= s->cx0 || y + lado <= s->cy0) return;
    /* Só as linhas e colunas dentro do recorte; o pixel opaco (a maioria) é
     * copiado, e o meio transparente é misturado sem divisão. */
    int j0 = s->cy0 > y ? s->cy0 - y : 0, j1 = s->cy1 < y + lado ? s->cy1 - y : lado;
    int i0 = s->cx0 > x ? s->cx0 - x : 0, i1 = s->cx1 < x + lado ? s->cx1 - x : lado;
    for (int j = j0; j < j1; j++) {
        uint32_t *p = s->px + (y + j) * s->larg + x;
        const uint32_t *o = argb + j * lado;
        for (int i = i0; i < i1; i++) {
            uint32_t c = o[i], al = c >> 24;
            if (!al) continue;
            p[i] = al == 255 ? c & 0xFFFFFF : mistura_255(p[i], c & 0xFFFFFF, (int)al);
        }
    }
}

/* Escurece um retângulo (alfa de 0 a 256): sombras sem desfoque. */
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

/* Degradê vertical de topo a base. */
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
    if (x0 >= x1) return;
    for (int j = y0; j < y1; j++)                 /* uma linha por vez, de uma vez só */
        memcpy(s->px + j * s->larg + x0, origem + (j - y) * passo + (x0 - x), (size_t)(x1 - x0) * 4);
}
