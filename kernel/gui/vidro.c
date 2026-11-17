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
 * quanto de cada pixel da ponta fica dentro (um quarto de círculo de raio RAIO). */
#include "vidro.h"
#include "suave.h"
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

/* A média do desfoque: 1/n em 16 bits, arredondado para cima, troca as três
 * divisões por pixel por multiplicações (no QEMU, e em processadores
 * antigos, a divisão custa caro). Cada passada é de uma dimensão só, então n
 * vai de 1 a 2R+1 e a soma de cada cor fica até 255 * (2R+1). Nessa faixa o
 * resultado é igual ao da divisão, sem erro de arredondamento (conferido
 * para todas as somas e todos os n). */
#define MEDIA_MAX (2 * VIDRO_RAIO + 1)
static uint32_t inverso[MEDIA_MAX + 1];

static inline uint32_t media_de(uint32_t sr, uint32_t sg, uint32_t sb, int n)
{
    uint32_t r = inverso[n];
    return (((sr * r) >> 16) << 16) | (((sg * r) >> 16) << 8) | ((sb * r) >> 16);
}

/* Canto arredondado suavizado: quanto de cada pixel da ponta fica dentro
 * do quarto de círculo, contado com 4 x 4 amostras (suave.h). Antes o canto
 * só pulava pixels inteiros (5, 3, 2, 1, 1), e a borda ficava em escada. O
 * raio é VIDRO_CANTO, ou menos se o retângulo for baixo ou estreito. */
static int raio_de(int l, int a)
{
    int r = VIDRO_CANTO;
    if (2 * r > l) r = l / 2;
    if (2 * r > a) r = a / 2;
    return r;
}

/* Quanto do pixel (i, j) fica dentro do retângulo (0 a 256). */
static int cobertura(int i, int j, int x, int y, int l, int a, int cantos, int r)
{
    int li = i - x, lj = j - y;
    if (lj < r ? !(cantos & CANTOS_EM_CIMA) : lj >= a - r ? !(cantos & CANTOS_EMBAIXO) : true) return 256;
    return suave_amostras_ret(li, lj, l, a, r) * 16;
}

/* Pixels todo de fora na ponta da linha "linha" de um canto de raio
 * VIDRO_CANTO (0 = a da ponta). */
int vidro_canto(int linha)
{
    int R = VIDRO_CANTO, n = 0;
    if (linha >= 0 && linha < R) while (n < R && suave_amostras_ret(n, linha, 2 * R, 2 * R, R) == 0) n++;
    return n;
}

void vidro_pintar(superficie_t *s, int x, int y, int l, int a, uint32_t tinta, int k, int cantos)
{
    int x0 = x < s->cx0 ? s->cx0 : x, y0 = y < s->cy0 ? s->cy0 : y;
    int x1 = x + l > s->cx1 ? s->cx1 : x + l, y1 = y + a > s->cy1 ? s->cy1 : y + a;
    if (x0 >= x1 || y0 >= y1) return;
    int r = raio_de(l, a);
    if (!buf || a > VIDRO_ALT_MAX || !vidro_efeitos) {
        /* Sem desfoque (efeitos leves): só a cor, um pouco transparente. */
        for (int j = y0; j < y1; j++) {
            uint32_t *p = s->px + j * s->larg;
            bool linha_canto = j < y + r || j >= y + a - r;
            for (int i = x0; i < x1; i++) {
                int cob = linha_canto && (i < x + r || i >= x + l - r) ? cobertura(i, j, x, y, l, a, cantos, r) : 256;
                if (!cob) continue;
                uint32_t v = vidro_efeitos ? mistura(p[i], tinta, k) : tinta;
                p[i] = cob == 256 ? v : mistura(p[i], v, cob);
            }
        }
        return;
    }
    const int R = VIDRO_RAIO;
    if (!inverso[1]) for (int n = 1; n <= MEDIA_MAX; n++) inverso[n] = (65536u + (uint32_t)n - 1) / (uint32_t)n;
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
            d[i] = media_de(sr, sg, sb, n);
            int entra = i + R + 1, sai = i - R;
            if (entra < W) { sr += (o[entra] >> 16) & 255; sg += (o[entra] >> 8) & 255; sb += o[entra] & 255; n++; }
            if (sai >= 0) { sr -= (o[sai] >> 16) & 255; sg -= (o[sai] >> 8) & 255; sb -= o[sai] & 255; n--; }
        }
    }
    /* Passada 2: cada coluna do destino, média vertical sobre buf, misturada com a tinta. */
    for (int i = x0; i < x1; i++) {
        int c = i - sx0;
        bool coluna_canto = i < x + r || i >= x + l - r;   /* só as colunas dos cantos têm conta */
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
            int cob = coluna_canto && (yy < y + r || yy >= y + a - r) ? cobertura(i, yy, x, y, l, a, cantos, r) : 256;
            if (cob) {
                uint32_t media = media_de(sr, sg, sb, n);
                uint32_t *p = &s->px[yy * s->larg + i];
                uint32_t v = mistura(media, tinta, k);
                *p = cob == 256 ? v : mistura(*p, v, cob);
            }
            int entra = jb + R + 1, sai = jb - R;
            if (entra < H) { uint32_t v = buf[entra * W + c]; sr += (v >> 16) & 255; sg += (v >> 8) & 255; sb += v & 255; n++; }
            if (sai >= 0) { uint32_t v = buf[sai * W + c]; sr -= (v >> 16) & 255; sg -= (v >> 8) & 255; sb -= v & 255; n--; }
        }
    }
}

/* Quanto do pixel (li, lj), relativo ao canto do retângulo l x a de cantos
 * com raio r, fica dentro, de 0 a 256 (4 x 4 amostras, suave.h). */
static int cobertura_r(int li, int lj, int l, int a, int r)
{
    return suave_amostras_ret(li, lj, l, a, r) * 16;
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
 * inteiro são pulados.
 *
 * Para ficar leve: a raiz inteira dos cantos sai de uma tabela por raio
 * (a mesma conta de antes, feita uma vez só; os raios em uso são poucos), e
 * nas linhas da janela o miolo, que o vidro cobre por inteiro, é pulado de
 * uma vez (antes, nas linhas dos cantos, cada pixel do miolo passava pela
 * conta de cobertura). */
#define SOMBRA_RMAX 24
#define SOMBRA_M (VIDRO_SOMBRA + SOMBRA_RMAX)
static struct { int r; uint8_t d[SOMBRA_M][SOMBRA_M]; } raizes[4];
static int qtd_raizes;

/* d[dy-1][dx-1] = floor(raiz(dx² + dy²)), para dx, dy de 1 a SOMBRA_M. */
static const uint8_t (*tabela_raiz(int r))[SOMBRA_M]
{
    for (int k = 0; k < qtd_raizes; k++) if (raizes[k].r == r) return raizes[k].d;
    int k = qtd_raizes < 4 ? qtd_raizes++ : 3;
    raizes[k].r = r;
    for (int dy = 1; dy <= SOMBRA_M; dy++)
        for (int dx = 1; dx <= SOMBRA_M; dx++) {
            int d2 = dx * dx + dy * dy, d = 0;
            while ((d + 1) * (d + 1) <= d2) d++;
            raizes[k].d[dy - 1][dx - 1] = (uint8_t)d;
        }
    return raizes[k].d;
}

void vidro_sombra_r(superficie_t *s, int x, int y, int l, int a, int r)
{
    if (!vidro_efeitos) return;
    if (2 * r > l) r = l / 2;
    if (2 * r > a) r = a / 2;
    if (r > SOMBRA_RMAX) r = SOMBRA_RMAX;
    const int S = VIDRO_SOMBRA, desce = 4;
    const uint8_t (*raiz)[SOMBRA_M] = tabela_raiz(r);
    int bx0 = x - S, by0 = y - S + desce, bx1 = x + l + S, by1 = y + a + S + desce;
    int x0 = bx0 < s->cx0 ? s->cx0 : bx0, y0 = by0 < s->cy0 ? s->cy0 : by0;
    int x1 = bx1 > s->cx1 ? s->cx1 : bx1, y1 = by1 > s->cy1 ? s->cy1 : by1;
    int sy = y + desce;                           /* a sombra é o retângulo descido */
    for (int j = y0; j < y1; j++) {
        uint32_t *p = s->px + j * s->larg;
        bool linha_vidro = j >= y && j < y + a;
        /* Nas linhas do vidro, as colunas de x + r a x + l - r estão todas
         * debaixo dele (só os cantos têm curva); no meio, a linha inteira. */
        int pula0 = x + r, pula1 = x + l - r;
        if (j >= y + r && j < y + a - r) { pula0 = x; pula1 = x + l; }
        for (int i = x0; i < x1; i++) {
            if (linha_vidro && i >= pula0 && i < pula1) { i = pula1 - 1; continue; }
            if (linha_vidro && i >= x && i < x + l && cobertura_r(i - x, j - y, l, a, r) == 256) continue;
            /* Distância (em pixels) até o retângulo arredondado descido. */
            int cx = i < x + r ? x + r : (i >= x + l - r ? x + l - r - 1 : i);
            int cy = j < sy + r ? sy + r : (j >= sy + a - r ? sy + a - r - 1 : j);
            int dx = i - cx, dy = j - cy, d;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (!dx || !dy) d = dx + dy;                                 /* ao lado de uma reta */
            else if (dx <= SOMBRA_M && dy <= SOMBRA_M) d = raiz[dy - 1][dx - 1];
            else continue;                                               /* longe demais: sem sombra */
            d -= r;
            if (d < 0) d = 0;
            if (d >= S) continue;
            int f = S - d;
            p[i] = escurecer(p[i], f * f * 90 / (S * S));
        }
    }
}

/* Sombra das janelas e dos menus: a mesma da doca, que segue a curva dos
 * cantos. A antiga era a de um retângulo de cantos retos e deixava uma
 * ponta escura por fora do canto arredondado. */
void vidro_sombra(superficie_t *s, int x, int y, int l, int a)
{
    vidro_sombra_r(s, x, y, l, a, VIDRO_CANTO);
}

bool vidro_efeitos = true;

/* Cantos de baixo das janelas. O conteúdo da janela é copiado por cima de
 * tudo, então os cantos são feitos assim: antes de pintar a moldura, guarda
 * os pixels de fundo dos dois cantos de baixo; depois de copiar o conteúdo,
 * mistura o fundo guardado de volta onde o canto fica de fora. */
static uint32_t fundo_canto[2][VIDRO_CANTO][VIDRO_CANTO];

void vidro_guardar_cantos(superficie_t *s, int x, int y, int l, int a)
{
    for (int lado = 0; lado < 2; lado++)
        for (int r = 0; r < VIDRO_CANTO; r++)
            for (int c = 0; c < VIDRO_CANTO; c++) {
                int px = lado ? x + l - 1 - c : x + c, py = y + a - 1 - r;
                if (px >= s->cx0 && px < s->cx1 && py >= s->cy0 && py < s->cy1)
                    fundo_canto[lado][r][c] = s->px[py * s->larg + px];
            }
}

void vidro_aplicar_cantos(superficie_t *s, int x, int y, int l, int a)
{
    for (int lado = 0; lado < 2; lado++)
        for (int r = 0; r < VIDRO_CANTO; r++)
            for (int c = 0; c < VIDRO_CANTO; c++) {
                int cob = suave_amostras_ret(c, a - 1 - r, l, a, raio_de(l, a)) * 16;
                if (cob == 256) continue;
                int px = lado ? x + l - 1 - c : x + c, py = y + a - 1 - r;
                if (px >= s->cx0 && px < s->cx1 && py >= s->cy0 && py < s->cy1) {
                    uint32_t *p = &s->px[py * s->larg + px];
                    *p = mistura(fundo_canto[lado][r][c], *p, cob);
                }
            }
}
