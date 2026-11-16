/* vidro.c - efeitos do visual moderno (H74): vidro, sombra e cantos.
 *
 * Vidro: o que está atrás de uma barra é desfocado e misturado com uma cor.
 * O desfoque é uma média de caixa (cada pixel vira a média dos vizinhos num
 * quadrado de (2R+1) x (2R+1)), feita em duas passadas: primeiro nas linhas,
 * depois nas colunas, com somas corridas (cada pixel novo soma o que entra e
 * tira o que sai da janela), então o custo não depende do raio. Como a
 * imagem de trás tem de estar pronta, o vidro é pintado na hora em que a
 * janela (ou a barra) é desenhada, por cima do que já foi composto. Quem
 * redesenha garante que o recorte inclua R pixels em volta de cada vidro
 * (janelas.c, expandir_vidro), senão a borda do desfoque leria pixels velhos.
 *
 * Sombra: um degradê escuro em volta da janela, mais forte perto dela e um
 * pouco deslocado para baixo, como uma luz vinda de cima.
 *
 * Cantos arredondados: uma tabela diz, para cada uma das primeiras linhas,
 * quantos pixels pular em cada ponta (um quarto de círculo de raio RAIO). */
#include "vidro.h"
#include "memoria.h"


static uint32_t *buf;            /* linhas desfocadas (largura da tela + margem) x altura máxima */
static int buf_l, buf_a;

bool vidro_iniciar(int largura_tela)
{
    buf_l = largura_tela + 2 * VIDRO_RAIO + 8;
    buf_a = VIDRO_ALT_MAX + 2 * VIDRO_RAIO;
    buf = kmalloc((uint32_t)(buf_l * buf_a) * 4);
    return buf != NULL;
}

/* Mistura a e b: k/256 de b. */
static inline uint32_t mistura(uint32_t a, uint32_t b, int k)
{
    uint32_t rb = ((a & 0xFF00FF) * (uint32_t)(256 - k) + (b & 0xFF00FF) * (uint32_t)k) >> 8;
    uint32_t g = ((a & 0x00FF00) * (uint32_t)(256 - k) + (b & 0x00FF00) * (uint32_t)k) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

/* Escurece um pixel: fica com (256 - k)/256 do brilho. */
static inline uint32_t escurecer(uint32_t p, int k)
{
    uint32_t rb = ((p & 0xFF00FF) * (uint32_t)(256 - k)) >> 8;
    uint32_t g = ((p & 0x00FF00) * (uint32_t)(256 - k)) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

/* Linha do canto arredondado: quantos pixels pular na linha r (0 = a de
 * cima). Raio 8: valores de 8 - raiz(64 - (8 - r - 0,5)^2), arredondados. */
static const uint8_t canto[VIDRO_CANTO] = { 5, 3, 2, 1, 1, 0, 0, 0 };
int vidro_canto(int r) { return r >= 0 && r < VIDRO_CANTO ? canto[r] : 0; }

/* Quantos pixels pular na ponta da linha j do retângulo (cantos arredondados). */
static int pular(int j, int y, int a, int cantos)
{
    if ((cantos & CANTOS_EM_CIMA) && j - y < VIDRO_CANTO) return vidro_canto(j - y);
    if ((cantos & CANTOS_EMBAIXO) && y + a - 1 - j < VIDRO_CANTO) return vidro_canto(y + a - 1 - j);
    return 0;
}

void vidro_pintar(superficie_t *s, int x, int y, int l, int a, uint32_t tinta, int k, int cantos)
{
    int x0 = x < s->cx0 ? s->cx0 : x, y0 = y < s->cy0 ? s->cy0 : y;
    int x1 = x + l > s->cx1 ? s->cx1 : x + l, y1 = y + a > s->cy1 ? s->cy1 : y + a;
    if (x0 >= x1 || y0 >= y1) return;
    if (!buf || a > VIDRO_ALT_MAX || !vidro_efeitos) {
        /* Sem desfoque (efeitos leves): só a cor, um pouco transparente. */
        for (int j = y0; j < y1; j++) {
            int pula = pular(j, y, a, cantos);
            uint32_t *p = s->px + j * s->larg;
            for (int i = x0; i < x1; i++) {
                if (i < x + pula || i >= x + l - pula) continue;
                p[i] = vidro_efeitos ? mistura(p[i], tinta, k) : tinta;
            }
        }
        return;
    }
    const int R = VIDRO_RAIO;
    /* A fonte: o destino com R de margem, dentro do recorte. */
    int sx0 = x0 - R < s->cx0 ? s->cx0 : x0 - R, sx1 = x1 + R > s->cx1 ? s->cx1 : x1 + R;
    int sy0 = y0 - R < s->cy0 ? s->cy0 : y0 - R, sy1 = y1 + R > s->cy1 ? s->cy1 : y1 + R;
    int W = sx1 - sx0, H = sy1 - sy0;
    if (W > buf_l || H > buf_a) return;
    /* Passada 1: cada linha da fonte, média horizontal, para buf. */
    for (int j = 0; j < H; j++) {
        const uint32_t *o = s->px + (sy0 + j) * s->larg + sx0;
        uint32_t *d = buf + j * W;
        uint32_t sr = 0, sg = 0, sb = 0;
        int n = 0;
        /* Janela inicial: [0, R] (o que está à esquerda de 0 não existe). */
        for (int i = 0; i <= R && i < W; i++) { sr += (o[i] >> 16) & 255; sg += (o[i] >> 8) & 255; sb += o[i] & 255; n++; }
        for (int i = 0; i < W; i++) {
            d[i] = ((sr / (uint32_t)n) << 16) | ((sg / (uint32_t)n) << 8) | (sb / (uint32_t)n);
            int entra = i + R + 1, sai = i - R;
            if (entra < W) { sr += (o[entra] >> 16) & 255; sg += (o[entra] >> 8) & 255; sb += o[entra] & 255; n++; }
            if (sai >= 0) { sr -= (o[sai] >> 16) & 255; sg -= (o[sai] >> 8) & 255; sb -= o[sai] & 255; n--; }
        }
    }
    /* Passada 2: cada coluna do destino, média vertical sobre buf, misturada com a tinta. */
    for (int i = x0; i < x1; i++) {
        int c = i - sx0;
        uint32_t sr = 0, sg = 0, sb = 0;
        int n = 0;
        int jj0 = y0 - sy0;                          /* linha do buf do primeiro destino */
        for (int j = jj0 - R; j <= jj0 + R; j++) {
            if (j < 0 || j >= H) continue;
            uint32_t v = buf[j * W + c];
            sr += (v >> 16) & 255; sg += (v >> 8) & 255; sb += v & 255; n++;
        }
        for (int yy = y0; yy < y1; yy++) {
            int jb = yy - sy0;
            int pula = pular(yy, y, a, cantos);
            if (i >= x + pula && i < x + l - pula) {
                uint32_t media = ((sr / (uint32_t)n) << 16) | ((sg / (uint32_t)n) << 8) | (sb / (uint32_t)n);
                s->px[yy * s->larg + i] = mistura(media, tinta, k);
            }
            int entra = jb + R + 1, sai = jb - R;
            if (entra < H) { uint32_t v = buf[entra * W + c]; sr += (v >> 16) & 255; sg += (v >> 8) & 255; sb += v & 255; n++; }
            if (sai >= 0) { uint32_t v = buf[sai * W + c]; sr -= (v >> 16) & 255; sg -= (v >> 8) & 255; sb -= v & 255; n--; }
        }
    }
}

/* Retângulo com cantos de raio r e borda suavizada (a doca): quanto do
 * pixel (li, lj), relativo ao canto do retângulo l x a, fica dentro, de 0 a
 * 256, contado com 4 x 4 amostras. As contas são em oitavos de pixel: a
 * amostra u (0 a 3) do pixel p fica em p*8 + 2u + 1. */
static int cobertura_r(int li, int lj, int l, int a, int r)
{
    if ((li >= r && li < l - r) || (lj >= r && lj < a - r)) return 256;
    int R = r * 8, cx = li < r ? R : (l - r) * 8, cy = lj < r ? R : (a - r) * 8, n = 0;
    for (int u = 0; u < 4; u++)
        for (int v = 0; v < 4; v++) {
            int sx = li * 8 + 2 * u + 1, sy = lj * 8 + 2 * v + 1;
            int dx = li < r ? (sx < cx ? cx - sx : 0) : (sx > cx ? sx - cx : 0);
            int dy = lj < r ? (sy < cy ? cy - sy : 0) : (sy > cy ? sy - cy : 0);
            if (dx * dx + dy * dy <= R * R) n++;
        }
    return n * 16;
}

/* Vidro de cantos com raio r e borda suavizada. Para a doca: ela é baixa
 * (DOCA_A < VIDRO_ALT_MAX), então o desfoque é o mesmo do vidro_pintar; só
 * os pixels dos cantos mudam: entram na proporção da cobertura. */
void vidro_pintar_r(superficie_t *s, int x, int y, int l, int a, uint32_t tinta, int k, int r)
{
    if (2 * r > l) r = l / 2;
    if (2 * r > a) r = a / 2;
    int x0 = x < s->cx0 ? s->cx0 : x, y0 = y < s->cy0 ? s->cy0 : y;
    int x1 = x + l > s->cx1 ? s->cx1 : x + l, y1 = y + a > s->cy1 ? s->cy1 : y + a;
    if (x0 >= x1 || y0 >= y1) return;
    /* Guarda os pixels dos quatro cantos, pinta o retângulo inteiro e depois
     * mistura cada pixel de canto entre o antigo e o pintado. */
    enum { RMAX = 24 };
    static uint32_t antes[4][RMAX * RMAX];
    if (r > RMAX) r = RMAX;
    for (int q = 0; q < 4; q++) {
        int qx = q & 1 ? x + l - r : x, qy = q & 2 ? y + a - r : y;
        for (int j = 0; j < r; j++)
            for (int i = 0; i < r; i++) {
                int px = qx + i, py = qy + j;
                if (px >= x0 && px < x1 && py >= y0 && py < y1) antes[q][j * r + i] = s->px[py * s->larg + px];
            }
    }
    vidro_pintar(s, x, y, l, a, tinta, k, 0);
    for (int q = 0; q < 4; q++) {
        int qx = q & 1 ? x + l - r : x, qy = q & 2 ? y + a - r : y;
        for (int j = 0; j < r; j++)
            for (int i = 0; i < r; i++) {
                int px = qx + i, py = qy + j;
                if (px < x0 || px >= x1 || py < y0 || py >= y1) continue;
                int cob = cobertura_r(px - x, py - y, l, a, r);
                if (cob == 256) continue;
                uint32_t *p = &s->px[py * s->larg + px];
                *p = mistura(antes[q][j * r + i], *p, cob);
            }
    }
}

/* Sombra que segue o retângulo de cantos com raio r (a doca): a distância
 * nos cantos é até o arco, não até a quina; os pixels que o vidro cobre por
 * inteiro são pulados. */
void vidro_sombra_r(superficie_t *s, int x, int y, int l, int a, int r)
{
    if (!vidro_efeitos) return;
    if (2 * r > l) r = l / 2;
    if (2 * r > a) r = a / 2;
    const int S = VIDRO_SOMBRA, desce = 4;
    int bx0 = x - S, by0 = y - S + desce, bx1 = x + l + S, by1 = y + a + S + desce;
    int x0 = bx0 < s->cx0 ? s->cx0 : bx0, y0 = by0 < s->cy0 ? s->cy0 : by0;
    int x1 = bx1 > s->cx1 ? s->cx1 : bx1, y1 = by1 > s->cy1 ? s->cy1 : by1;
    int sy = y + desce;                           /* a sombra é o retângulo descido */
    for (int j = y0; j < y1; j++) {
        uint32_t *p = s->px + j * s->larg;
        for (int i = x0; i < x1; i++) {
            if (i >= x && i < x + l && j >= y && j < y + a && cobertura_r(i - x, j - y, l, a, r) == 256) {
                if (j >= y + r && j < y + a - r) i = x + l - 1;   /* meio da doca: pula a linha */
                continue;
            }
            /* Distância (em pixels) até o retângulo arredondado descido. */
            int cx = i < x + r ? x + r : (i >= x + l - r ? x + l - r - 1 : i);
            int cy = j < sy + r ? sy + r : (j >= sy + a - r ? sy + a - r - 1 : j);
            int dx = i - cx, dy = j - cy, d2 = dx * dx + dy * dy, d = 0;
            while ((d + 1) * (d + 1) <= d2) d++;
            d -= r;
            if (d < 0) d = 0;
            if (d >= S) continue;
            int f = S - d;
            p[i] = escurecer(p[i], f * f * 90 / (S * S));
        }
    }
}

void vidro_sombra(superficie_t *s, int x, int y, int l, int a)
{
    if (!vidro_efeitos) return;
    const int S = VIDRO_SOMBRA, desce = 4;
    int bx0 = x - S, by0 = y - S + desce, bx1 = x + l + S, by1 = y + a + S + desce;
    int x0 = bx0 < s->cx0 ? s->cx0 : bx0, y0 = by0 < s->cy0 ? s->cy0 : by0;
    int x1 = bx1 > s->cx1 ? s->cx1 : bx1, y1 = by1 > s->cy1 ? s->cy1 : by1;
    for (int j = y0; j < y1; j++) {
        /* Distância vertical até a janela (deslocada para baixo). */
        int dy = j < y + desce ? y + desce - j : (j >= y + a + desce ? j - (y + a + desce) + 1 : 0);
        uint32_t *p = s->px + j * s->larg;
        for (int i = x0; i < x1; i++) {
            if (i >= x && i < x + l && j >= y && j < y + a) { i = x + l - 1; continue; }   /* debaixo da janela: nada */
            int dx = i < x ? x - i : (i >= x + l ? i - (x + l) + 1 : 0);
            int d = dx > dy ? dx : dy;
            if (dx && dy) d = (dx + dy) * 3 / 4 > d ? (dx + dy) * 3 / 4 : d;   /* cantos mais suaves */
            if (d >= S) continue;
            int f = S - d;                         /* 1..S */
            p[i] = escurecer(p[i], f * f * 90 / (S * S));
        }
    }
}

bool vidro_efeitos = true;
