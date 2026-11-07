/* h264.c - decodificador H.264 perfil Baseline (CAVLC, quadros I e P).
 *
 * É o vídeo do itag 18 do YouTube (avc1.42001E) e da maioria dos MP4
 * "compatíveis". Cobre: SPS/PPS, fatias I e P, vários quadros de referência
 * com janela deslizante e reordenação da lista, predição intra 4x4 e 16x16,
 * croma 4:2:0, compensação de movimento com quarto de pixel e o filtro de
 * desbloqueio. Fica de fora o que o Baseline não usa: CABAC, quadros B,
 * campos entrelaçados, 8x8 e predição ponderada.
 *
 * Só inteiros. Compila também no Linux (H264_HOST) para comparar quadro a
 * quadro com o ffmpeg. */
#ifdef H264_HOST
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define DBG(...) do { if (getenv("H264_DBG")) fprintf(stderr, __VA_ARGS__); } while (0)
#define aloca malloc
#define libera free
static void *aloca_zerado(size_t n) { return calloc(1, n); }
#else
#include "sagui.h"
#define DBG(...) do { } while (0)
#endif
#include "h264.h"

/* ------------------------------------------------------------------ bits */

typedef struct {
    const uint8_t *d;
    uint32_t pos, fim;      /* em bits; fim = bit de parada do RBSP */
} bits_t;

static inline uint32_t espiar32(const bits_t *b)
{
    uint32_t i = b->pos >> 3, s = b->pos & 7;
    uint32_t v = ((uint32_t)b->d[i] << 24) | ((uint32_t)b->d[i + 1] << 16) |
                 ((uint32_t)b->d[i + 2] << 8) | b->d[i + 3];
    if (s) v = (v << s) | (b->d[i + 4] >> (8 - s));
    return v;
}
static inline uint32_t ler_bits(bits_t *b, int n)
{
    if (!n) return 0;
    uint32_t v = espiar32(b) >> (32 - n);
    b->pos += n;
    return v;
}
static inline int ler_bit(bits_t *b)
{
    int v = (b->d[b->pos >> 3] >> (7 - (b->pos & 7))) & 1;
    b->pos++;
    return v;
}
static inline uint32_t ue(bits_t *b)
{
    uint32_t v = espiar32(b);
    int z = v ? __builtin_clz(v) : 32;
    if (z > 15) {           /* raro: código longo */
        b->pos += z;
        ler_bit(b);
        return (1u << z) - 1 + ler_bits(b, z > 31 ? 31 : z);
    }
    b->pos += 2 * z + 1;
    return (v >> (31 - 2 * z)) - 1;
}
static inline int se(bits_t *b)
{
    uint32_t k = ue(b);
    return (k & 1) ? (int)((k + 1) >> 1) : -(int)(k >> 1);
}
static inline bool mais_dados(const bits_t *b) { return b->pos < b->fim; }

/* ------------------------------------------------------------- tabelas */

static const uint8_t ct_len[4][68] = {
 { 1,0,0,0, 6,2,0,0, 8,6,3,0, 9,8,7,5, 10,9,8,6, 11,10,9,7, 13,11,10,8, 13,13,11,9, 13,13,13,10,
   14,14,13,11, 14,14,14,13, 15,15,14,14, 15,15,15,14, 16,15,15,15, 16,16,16,15, 16,16,16,16, 16,16,16,16 },
 { 2,0,0,0, 6,2,0,0, 6,5,3,0, 7,6,6,4, 8,6,6,4, 8,7,7,5, 9,8,8,6, 11,9,9,6, 11,11,11,7,
   12,11,11,9, 12,12,12,11, 12,12,12,11, 13,13,13,12, 13,13,13,13, 13,14,13,13, 14,14,14,13, 14,14,14,14 },
 { 4,0,0,0, 6,4,0,0, 6,5,4,0, 6,5,5,4, 7,5,5,4, 7,5,5,4, 7,6,6,4, 7,6,6,4, 8,7,7,5,
   8,8,7,6, 9,8,8,7, 9,9,8,8, 9,9,9,8, 10,9,9,9, 10,10,10,10, 10,10,10,10, 10,10,10,10 },
 { 6,0,0,0, 6,6,0,0, 6,6,6,0, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6,
   6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6 },
};
static const uint8_t ct_bits[4][68] = {
 { 1,0,0,0, 5,1,0,0, 7,4,1,0, 7,6,5,3, 7,6,5,3, 7,6,5,4, 15,6,5,4, 11,14,5,4, 8,10,13,4,
   15,14,9,4, 11,10,13,12, 15,14,9,12, 11,10,13,8, 15,1,9,12, 11,14,13,8, 7,10,9,12, 4,6,5,8 },
 { 3,0,0,0, 11,2,0,0, 7,7,3,0, 7,10,9,5, 7,6,5,4, 4,6,5,6, 7,6,5,8, 15,6,5,4, 11,14,13,4,
   15,10,9,4, 11,14,13,12, 8,10,9,8, 15,14,13,12, 11,10,9,12, 7,11,6,8, 9,8,10,1, 7,6,5,4 },
 { 15,0,0,0, 15,14,0,0, 11,15,13,0, 8,12,14,12, 15,10,11,11, 11,8,9,10, 9,14,13,9, 8,10,9,8, 15,14,13,13,
   11,14,10,12, 15,10,13,12, 11,14,9,12, 8,10,13,8, 13,7,9,12, 9,12,11,10, 5,8,7,6, 1,4,3,2 },
 { 3,0,0,0, 0,1,0,0, 4,5,6,0, 8,9,10,11, 12,13,14,15, 16,17,18,19, 20,21,22,23, 24,25,26,27, 28,29,30,31,
   32,33,34,35, 36,37,38,39, 40,41,42,43, 44,45,46,47, 48,49,50,51, 52,53,54,55, 56,57,58,59, 60,61,62,63 },
};
static const uint8_t cdc_len[20] = { 2,0,0,0, 6,1,0,0, 6,6,3,0, 6,7,7,6, 6,8,8,7 };
static const uint8_t cdc_bits[20] = { 1,0,0,0, 7,1,0,0, 4,6,1,0, 3,3,2,5, 2,3,2,0 };

static const uint8_t tz_len[15][16] = {
 {1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9}, {3,3,3,3,3,4,4,4,4,5,5,6,6,6,6}, {4,3,3,3,4,4,3,3,4,5,5,6,5,6},
 {5,3,4,4,3,3,3,4,3,4,5,5,5}, {4,4,4,3,3,3,3,3,4,5,4,5}, {6,5,3,3,3,3,3,3,4,3,6},
 {6,5,3,3,3,2,3,4,3,6}, {6,4,5,3,2,2,3,3,6}, {6,6,4,2,2,3,2,5}, {5,5,3,2,2,2,4},
 {4,4,3,3,1,3}, {4,4,2,1,3}, {3,3,1,2}, {2,2,1}, {1,1},
};
static const uint8_t tz_bits[15][16] = {
 {1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1}, {7,6,5,4,3,5,4,3,2,3,2,3,2,1,0}, {5,7,6,5,4,3,4,3,2,3,2,1,1,0},
 {3,7,5,4,6,5,4,3,3,2,2,1,0}, {5,4,3,7,6,5,4,3,2,1,1,0}, {1,1,7,6,5,4,3,2,1,1,0},
 {1,1,5,4,3,3,2,1,1,0}, {1,1,1,3,3,2,2,1,0}, {1,0,1,3,2,1,1,1}, {1,0,1,3,2,1,1},
 {0,1,1,2,1,3}, {0,1,1,1,1}, {0,1,1,1}, {0,1,1}, {0,1},
};
static const uint8_t tzc_len[3][4] = { {1,2,3,3}, {1,2,2}, {1,1} };
static const uint8_t tzc_bits[3][4] = { {1,1,1,0}, {1,1,0}, {1,0} };
static const uint8_t rb_len[7][16] = {
 {1,1}, {1,2,2}, {2,2,2,2}, {2,2,2,3,3}, {2,2,3,3,3,3}, {2,3,3,3,3,3,3},
 {3,3,3,3,3,3,3,4,5,6,7,8,9,10,11},
};
static const uint8_t rb_bits[7][16] = {
 {1,0}, {1,1,0}, {3,2,1,0}, {3,2,1,1,0}, {3,2,3,2,1,0}, {3,0,1,3,2,5,4},
 {7,6,5,4,3,2,1,1,1,1,1,1,1,1,1},
};

static const uint8_t cbp_intra[48] = {
 47,31,15,0,23,27,29,30,7,11,13,14,39,43,45,46,16,3,5,10,12,19,21,26,28,35,37,42,44,1,2,4,
 8,17,18,20,24,6,9,22,25,32,33,34,36,40,38,41 };
static const uint8_t cbp_inter[48] = {
 0,16,1,2,4,8,32,3,5,10,12,15,47,7,11,13,14,6,9,31,35,37,42,44,33,34,36,40,39,43,45,46,
 17,18,20,24,19,21,26,28,23,27,29,30,22,25,38,41 };

static const uint8_t zigzag[16] = { 0,1,4,8,5,2,3,6,9,12,13,10,7,11,14,15 };
static const uint8_t dq_v[6][3] = { {10,16,13},{11,18,14},{13,20,16},{14,23,18},{16,25,20},{18,29,23} };
static const uint8_t qp_croma[52] = {
 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,
 29,30,31,32,32,33,34,34,35,35,36,36,37,37,37,38,38,38,39,39,39,39 };
static const uint8_t alfa_t[52] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 4,4,5,6,7,8,9,10,12,13,15,17,20,22,25,28,32,36,40,45,50,56,63,71,80,90,101,113,127,144,162,182,203,226,255,255 };
static const uint8_t beta_t[52] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 2,2,2,3,3,3,3,4,4,4,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14,15,15,16,16,17,17,18,18 };
static const uint8_t tc0_t[52][3] = {
 {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
 {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,1,1},{0,1,1},{1,1,1},{1,1,1},{1,1,1},
 {1,1,1},{1,1,2},{1,1,2},{1,1,2},{1,1,2},{1,2,3},{1,2,3},{2,2,3},{2,2,4},{2,3,4},{2,3,4},{3,3,5},{3,4,6},
 {3,4,6},{4,5,7},{4,5,8},{4,6,9},{5,7,10},{6,8,11},{6,8,13},{7,10,14},{8,11,16},{9,12,18},{10,13,20},
 {11,15,23},{13,17,25} };

/* Bloco 4x4 de luma: índice de decodificação -> posição (x,y) em blocos. */
static const uint8_t blk_x[16] = { 0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3 };
static const uint8_t blk_y[16] = { 0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3 };
static uint8_t blk_ordem[4][4];   /* [y][x] -> índice */

/* Tabelas de consulta direta: espia N bits, sai (tamanho << 8) | valor. */
static uint16_t *vlc_ct[5];        /* 4 de nC + croma DC; 16 bits */
static uint16_t *vlc_tz[15], *vlc_tzc[3], *vlc_rb[7];
static bool vlc_pronto;

static uint16_t *vlc_montar(int nbits, int qtd, const uint8_t *len, const uint8_t *cod, const uint8_t *val)
{
    uint16_t *t = aloca_zerado(sizeof(uint16_t) << nbits);
    if (!t) return NULL;
    for (int i = 0; i < qtd; i++) {
        int l = len[i];
        if (!l) continue;
        uint32_t base = (uint32_t)cod[i] << (nbits - l), n = 1u << (nbits - l);
        for (uint32_t k = 0; k < n; k++) t[base + k] = (uint16_t)((l << 8) | val[i]);
    }
    return t;
}

static bool vlc_iniciar(void)
{
    if (vlc_pronto) return true;
    uint8_t val[68];
    for (int i = 0; i < 68; i++) val[i] = (uint8_t)(((i >> 2) << 2) | (i & 3));   /* TC<<2 | T1 */
    for (int k = 0; k < 4; k++) vlc_ct[k] = vlc_montar(16, 68, ct_len[k], ct_bits[k], val);
    vlc_ct[4] = vlc_montar(16, 20, cdc_len, cdc_bits, val);
    uint8_t seq[16];
    for (int i = 0; i < 16; i++) seq[i] = (uint8_t)i;
    for (int k = 0; k < 15; k++) vlc_tz[k] = vlc_montar(9, 16, tz_len[k], tz_bits[k], seq);
    for (int k = 0; k < 3; k++) vlc_tzc[k] = vlc_montar(3, 4, tzc_len[k], tzc_bits[k], seq);
    for (int k = 0; k < 7; k++) vlc_rb[k] = vlc_montar(11, 16, rb_len[k], rb_bits[k], seq);
    for (int i = 0; i < 16; i++) blk_ordem[blk_y[i]][blk_x[i]] = (uint8_t)i;
    vlc_pronto = true;
    return true;
}

static inline int vlc_ler(bits_t *b, const uint16_t *t, int nbits)
{
    uint16_t e = t[espiar32(b) >> (32 - nbits)];
    if (!e) return -1;
    b->pos += e >> 8;
    return e & 255;
}

/* ------------------------------------------------------------- estado */

#define MAX_REF 16
#define REF_NADA (-2)     /* vizinho fora da imagem/fatia */
#define REF_INTRA (-1)

typedef struct {
    uint8_t *y, *u, *v;
    int frame_num, pic_num;
    bool ref, usado;
    int id;
} quadro_t;

typedef struct {        /* informação guardada por macrobloco */
    int8_t  intra;          /* 0 inter, 1 intra, 2 I_PCM */
    int8_t  i4x4;           /* usou intra 4x4 */
    int8_t  qp, qpc;
    int     fatia;
    uint8_t nz[16 + 8];     /* coeficientes por bloco 4x4 (luma raster, depois Cb 4, Cr 4) */
    int8_t  modo4[16];      /* raster */
    int8_t  ref[4];         /* por 8x8; id do quadro de referência ou -1 */
    int16_t mv[16][2];      /* raster */
    int8_t  refidx[4];
} mbinfo_t;

struct h264 {
    /* SPS */
    bool sps_ok, pps_ok;
    int larg_mb, alt_mb, larg, alt;
    int corte[4];               /* esq, dir, cima, baixo em pixels */
    int log2_frame_num, poc_tipo, log2_poc_lsb, delta_poc_zero;
    int max_ref;
    /* PPS */
    int ref_ativas_padrao, qp_inicial, croma_qp_off, ctrl_desbloq, redundante, poc_inferior;
    int ponderada;
    /* fatia */
    int tipo, frame_num, nal_ref, idr, qp, num_ref_ativas;
    int desbloq_modo, alfa_off, beta_off;
    int fatia_atual;
    quadro_t *lista[MAX_REF + 1];
    /* quadros */
    quadro_t q[MAX_REF + 2];
    quadro_t *atual;
    int prox_id;
    mbinfo_t *mb;
    int passo, passo_c;
    bool quadro_aberto;
    int ultimo_frame_num;
    /* rascunho para RBSP */
    uint8_t *rbsp;
    int rbsp_cap;
    int erros;
};

/* --------------------------------------------------------- utilidades */

static inline int clip(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline uint8_t clip1(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static inline int absi(int v) { return v < 0 ? -v : v; }
static inline int mediana(int a, int b, int c)
{
    int mx = a > b ? a : b, mn = a < b ? a : b;
    return c > mx ? mx : c < mn ? mn : c;
}

h264_t *h264_novo(void)
{
    if (!vlc_iniciar()) return NULL;
    h264_t *h = aloca_zerado(sizeof(h264_t));
    return h;
}

static void liberar_quadros(h264_t *h)
{
    for (int i = 0; i < MAX_REF + 2; i++) {
        if (h->q[i].y) libera(h->q[i].y);
        h->q[i].y = h->q[i].u = h->q[i].v = NULL;
        h->q[i].usado = h->q[i].ref = false;
    }
    if (h->mb) libera(h->mb);
    h->mb = NULL;
}

void h264_fim(h264_t *h)
{
    if (!h) return;
    liberar_quadros(h);
    if (h->rbsp) libera(h->rbsp);
    libera(h);
}

/* ---------------------------------------------------------- SPS / PPS */

static void pular_escala(bits_t *b, int n)
{
    int ultimo = 8, prox = 8;
    for (int j = 0; j < n; j++) {
        if (prox) prox = (ultimo + se(b) + 256) % 256;
        ultimo = prox ? prox : ultimo;
    }
}

static int ler_sps(h264_t *h, bits_t *b)
{
    int perfil = (int)ler_bits(b, 8);
    ler_bits(b, 16);   /* restrições + nível */
    ue(b);
    if (perfil == 100 || perfil == 110 || perfil == 122 || perfil == 244 || perfil == 44 ||
        perfil == 83 || perfil == 86 || perfil == 118 || perfil == 128) {
        int croma = (int)ue(b);
        if (croma != 1) return H264_SEM_SUPORTE;
        ue(b); ue(b); ler_bit(b);
        if (ler_bit(b))
            for (int i = 0; i < 8; i++) if (ler_bit(b)) pular_escala(b, i < 6 ? 16 : 64);
    }
    h->log2_frame_num = (int)ue(b) + 4;
    h->poc_tipo = (int)ue(b);
    if (h->poc_tipo == 0) h->log2_poc_lsb = (int)ue(b) + 4;
    else if (h->poc_tipo == 1) {
        h->delta_poc_zero = ler_bit(b);
        se(b); se(b);
        int n = (int)ue(b);
        for (int i = 0; i < n; i++) se(b);
    }
    h->max_ref = (int)ue(b);
    if (h->max_ref > MAX_REF) h->max_ref = MAX_REF;
    if (h->max_ref < 1) h->max_ref = 1;
    ler_bit(b);
    int lm = (int)ue(b) + 1, am = (int)ue(b) + 1;
    if (!ler_bit(b)) return H264_SEM_SUPORTE;      /* entrelaçado */
    ler_bit(b);
    int c[4] = { 0, 0, 0, 0 };
    if (ler_bit(b)) for (int i = 0; i < 4; i++) c[i] = (int)ue(b) * 2;
    if (lm > 256 || am > 256) return H264_SEM_SUPORTE;
    if (lm != h->larg_mb || am != h->alt_mb) {
        liberar_quadros(h);
        h->larg_mb = lm;
        h->alt_mb = am;
        h->larg = lm * 16;
        h->alt = am * 16;
        h->passo = h->larg;
        h->passo_c = h->larg / 2;
        h->mb = aloca_zerado(sizeof(mbinfo_t) * lm * am);
        if (!h->mb) return H264_SEM_MEMORIA;
        for (int i = 0; i < MAX_REF + 2; i++) {
            uint8_t *p = aloca(h->larg * h->alt * 3 / 2);
            if (!p) return H264_SEM_MEMORIA;
            h->q[i].y = p;
            h->q[i].u = p + h->larg * h->alt;
            h->q[i].v = h->q[i].u + h->larg * h->alt / 4;
        }
    }
    memcpy(h->corte, c, sizeof c);
    h->sps_ok = true;
    return 0;
}

static int ler_pps(h264_t *h, bits_t *b)
{
    ue(b); ue(b);
    if (ler_bit(b)) return H264_SEM_SUPORTE;       /* CABAC */
    h->poc_inferior = ler_bit(b);
    if (ue(b) != 0) return H264_SEM_SUPORTE;       /* grupos de fatias */
    h->ref_ativas_padrao = (int)ue(b) + 1;
    ue(b);
    h->ponderada = ler_bit(b);
    ler_bits(b, 2);
    h->qp_inicial = 26 + se(b);
    se(b);
    h->croma_qp_off = se(b);
    h->ctrl_desbloq = ler_bit(b);
    ler_bit(b);          /* intra restrita: tratada como 0 */
    h->redundante = ler_bit(b);
    h->pps_ok = true;
    return 0;
}

/* ------------------------------------------------- quadros de referência */

static void janela_deslizante(h264_t *h)
{
    int n = 0;
    int maxf = 1 << h->log2_frame_num;
    for (int i = 0; i < MAX_REF + 2; i++)
        if (h->q[i].ref) h->q[i].pic_num = h->q[i].frame_num > h->frame_num ? h->q[i].frame_num - maxf : h->q[i].frame_num;
    for (int i = 0; i < MAX_REF + 2; i++) if (h->q[i].ref) n++;
    while (n > h->max_ref) {
        quadro_t *velho = NULL;
        for (int i = 0; i < MAX_REF + 2; i++)
            if (h->q[i].ref && (!velho || h->q[i].pic_num < velho->pic_num)) velho = &h->q[i];
        velho->ref = false;
        n--;
    }
}

static void montar_lista(h264_t *h)
{
    int maxf = 1 << h->log2_frame_num, k = 0;
    for (int i = 0; i < MAX_REF + 2; i++) {
        quadro_t *q = &h->q[i];
        if (!q->ref || q == h->atual) continue;
        q->pic_num = q->frame_num > h->frame_num ? q->frame_num - maxf : q->frame_num;
        h->lista[k++] = q;
    }
    for (int i = 1; i < k; i++)                    /* PicNum decrescente */
        for (int j = i; j > 0 && h->lista[j]->pic_num > h->lista[j - 1]->pic_num; j--) {
            quadro_t *t = h->lista[j]; h->lista[j] = h->lista[j - 1]; h->lista[j - 1] = t;
        }
    for (int i = k; i <= MAX_REF; i++) h->lista[i] = k ? h->lista[i % k] : NULL;
}

static void modificar_lista(h264_t *h, bits_t *b)
{
    if (!ler_bit(b)) return;
    int maxf = 1 << h->log2_frame_num, pred = h->frame_num, idx = 0;
    for (;;) {
        int op = (int)ue(b);
        if (op == 3) break;
        if (op > 2) { h->erros++; break; }
        int v = (int)ue(b);
        if (op == 2) continue;                      /* longo prazo: não suportado */
        int sem_volta = op == 0 ? pred - (v + 1) : pred + (v + 1);
        if (sem_volta < 0) sem_volta += maxf;
        if (sem_volta >= maxf) sem_volta -= maxf;
        pred = sem_volta;
        int num = sem_volta > h->frame_num ? sem_volta - maxf : sem_volta;
        quadro_t *alvo = NULL;
        for (int i = 0; i < MAX_REF + 2; i++)
            if (h->q[i].ref && &h->q[i] != h->atual && h->q[i].pic_num == num) alvo = &h->q[i];
        if (!alvo || idx >= h->num_ref_ativas) continue;
        int n = h->num_ref_ativas;
        for (int i = n; i > idx; i--) h->lista[i] = h->lista[i - 1];
        h->lista[idx++] = alvo;
        int k = idx;
        for (int i = idx; i <= n; i++) if (h->lista[i] != alvo) h->lista[k++] = h->lista[i];
    }
}

/* ------------------------------------------------------ predição intra */

static void pred4x4(uint8_t *d, int passo, int modo, const int *t, const int *l, int lt, bool tem_t, bool tem_l)
{
    int p[4][4];
#define T(i) ((i) < 0 ? lt : t[i])
#define L(i) ((i) < 0 ? lt : l[i])
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            int v;
            switch (modo) {
            case 0: v = t[x]; break;
            case 1: v = l[y]; break;
            case 2:
                if (tem_t && tem_l) v = (t[0] + t[1] + t[2] + t[3] + l[0] + l[1] + l[2] + l[3] + 4) >> 3;
                else if (tem_l) v = (l[0] + l[1] + l[2] + l[3] + 2) >> 2;
                else if (tem_t) v = (t[0] + t[1] + t[2] + t[3] + 2) >> 2;
                else v = 128;
                break;
            case 3:
                v = (x == 3 && y == 3) ? (t[6] + 3 * t[7] + 2) >> 2
                                       : (t[x + y] + 2 * t[x + y + 1] + t[x + y + 2] + 2) >> 2;
                break;
            case 4:
                if (x > y) v = (T(x - y - 2) + 2 * T(x - y - 1) + T(x - y) + 2) >> 2;
                else if (x < y) v = (L(y - x - 2) + 2 * L(y - x - 1) + L(y - x) + 2) >> 2;
                else v = (t[0] + 2 * lt + l[0] + 2) >> 2;
                break;
            case 5: {
                int z = 2 * x - y;
                if (z >= 0 && !(z & 1)) v = (T(x - (y >> 1) - 1) + T(x - (y >> 1)) + 1) >> 1;
                else if (z >= 0) v = (T(x - (y >> 1) - 2) + 2 * T(x - (y >> 1) - 1) + T(x - (y >> 1)) + 2) >> 2;
                else if (z == -1) v = (l[0] + 2 * lt + t[0] + 2) >> 2;
                else v = (L(y - 1) + 2 * L(y - 2) + L(y - 3) + 2) >> 2;
                break;
            }
            case 6: {
                int z = 2 * y - x;
                if (z >= 0 && !(z & 1)) v = (L(y - (x >> 1) - 1) + L(y - (x >> 1)) + 1) >> 1;
                else if (z >= 0) v = (L(y - (x >> 1) - 2) + 2 * L(y - (x >> 1) - 1) + L(y - (x >> 1)) + 2) >> 2;
                else if (z == -1) v = (l[0] + 2 * lt + t[0] + 2) >> 2;
                else v = (T(x - 1) + 2 * T(x - 2) + T(x - 3) + 2) >> 2;
                break;
            }
            case 7:
                if (!(y & 1)) v = (t[x + (y >> 1)] + t[x + (y >> 1) + 1] + 1) >> 1;
                else v = (t[x + (y >> 1)] + 2 * t[x + (y >> 1) + 1] + t[x + (y >> 1) + 2] + 2) >> 2;
                break;
            default: {
                int z = x + 2 * y;
                if (z < 5 && !(z & 1)) v = (l[y + (x >> 1)] + l[y + (x >> 1) + 1] + 1) >> 1;
                else if (z < 5) v = (l[y + (x >> 1)] + 2 * l[y + (x >> 1) + 1] + l[y + (x >> 1) + 2] + 2) >> 2;
                else if (z == 5) v = (l[2] + 3 * l[3] + 2) >> 2;
                else v = l[3];
            }
            }
            p[y][x] = v;
        }
#undef T
#undef L
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) d[y * passo + x] = (uint8_t)p[y][x];
}

static void pred_plano(uint8_t *d, int passo, int n, const uint8_t *t, const uint8_t *l, int lt)
{
    /* n = 16 (luma) ou 8 (croma 4:2:0) */
    int m = n / 2, H = 0, V = 0;
    for (int i = 0; i < m; i++) {
        int a = (m - 2 - i) < 0 ? lt : t[m - 2 - i];
        int c = (m - 2 - i) < 0 ? lt : l[m - 2 - i];
        H += (i + 1) * (t[m + i] - a);
        V += (i + 1) * (l[m + i] - c);
    }
    int a = 16 * (l[n - 1] + t[n - 1]);
    int bb = n == 16 ? (5 * H + 32) >> 6 : (34 * H + 32) >> 6;
    int cc = n == 16 ? (5 * V + 32) >> 6 : (34 * V + 32) >> 6;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            d[y * passo + x] = clip1((a + bb * (x - (m - 1)) + cc * (y - (m - 1)) + 16) >> 5);
}

/* ------------------------------------------------------- transformadas */

static void idct4_somar(uint8_t *d, int passo, int *c)
{
    int t[16];
    for (int i = 0; i < 4; i++) {
        int *r = &t[i * 4];
        int *s = &c[i * 4];
        int e0 = s[0] + s[2], e1 = s[0] - s[2], e2 = (s[1] >> 1) - s[3], e3 = s[1] + (s[3] >> 1);
        r[0] = e0 + e3; r[1] = e1 + e2; r[2] = e1 - e2; r[3] = e0 - e3;
    }
    for (int i = 0; i < 4; i++) {
        int e0 = t[i] + t[8 + i], e1 = t[i] - t[8 + i];
        int e2 = (t[4 + i] >> 1) - t[12 + i], e3 = t[4 + i] + (t[12 + i] >> 1);
        int f[4] = { e0 + e3, e1 + e2, e1 - e2, e0 - e3 };
        for (int k = 0; k < 4; k++) d[k * passo + i] = clip1(d[k * passo + i] + ((f[k] + 32) >> 6));
    }
}

/* ---------------------------------------------------------- CAVLC */

static int residuo(bits_t *b, int nC, int16_t *coef, int inicio, int max)
{
    int r;
    if (nC == -1) r = vlc_ler(b, vlc_ct[4], 16);
    else r = vlc_ler(b, vlc_ct[nC < 2 ? 0 : nC < 4 ? 1 : nC < 8 ? 2 : 3], 16);
    if (r < 0) return -1;
    int total = r >> 2, t1 = r & 3;
    if (!total) return 0;
    if (total > max) return -1;
    int nivel[16], corrida[16];
    int sufixo = (total > 10 && t1 < 3) ? 1 : 0;
    for (int i = 0; i < total; i++) {
        if (i < t1) { nivel[i] = ler_bit(b) ? -1 : 1; continue; }
        uint32_t v = espiar32(b);
        int pref = v ? __builtin_clz(v) : 32;
        if (pref > 25) return -1;
        b->pos += pref + 1;
        int codigo = (pref < 15 ? pref : 15) << sufixo;
        int tam = (pref == 14 && sufixo == 0) ? 4 : pref >= 15 ? pref - 3 : sufixo;
        if (tam) codigo += (int)ler_bits(b, tam);
        if (pref >= 15 && sufixo == 0) codigo += 15;
        if (pref >= 16) codigo += (1 << (pref - 3)) - 4096;
        if (i == t1 && t1 < 3) codigo += 2;
        nivel[i] = (codigo & 1) ? (-codigo - 1) >> 1 : (codigo + 2) >> 1;
        if (!sufixo) sufixo = 1;
        if (absi(nivel[i]) > (3 << (sufixo - 1)) && sufixo < 6) sufixo++;
    }
    int zeros = 0;
    if (total < max) {
        zeros = nC == -1 ? vlc_ler(b, vlc_tzc[total - 1], 3) : vlc_ler(b, vlc_tz[total - 1], 9);
        if (zeros < 0) return -1;
    }
    for (int i = 0; i < total - 1; i++) {
        if (zeros > 0) {
            int rr = vlc_ler(b, vlc_rb[(zeros > 7 ? 7 : zeros) - 1], 11);
            if (rr < 0 || rr > zeros) return -1;
            corrida[i] = rr;
            zeros -= rr;
        } else corrida[i] = 0;
    }
    corrida[total - 1] = zeros;
    int pos = -1;
    for (int i = total - 1; i >= 0; i--) {
        pos += corrida[i] + 1;
        if (pos >= max) return -1;
        coef[inicio + pos] = (int16_t)nivel[i];
    }
    return total;
}

/* ------------------------------------------------- vizinhança do MB */

typedef struct {
    int mbx, mby, idx;
    mbinfo_t *m;
    mbinfo_t *a, *b, *c, *d;   /* esquerda, cima, cima-direita, cima-esquerda (NULL = indisponível) */
} ctx_t;

static mbinfo_t *vizinho(h264_t *h, int x, int y)
{
    if (x < 0 || y < 0 || x >= h->larg_mb || y >= h->alt_mb) return NULL;
    mbinfo_t *m = &h->mb[y * h->larg_mb + x];
    return m->fatia == h->fatia_atual ? m : NULL;
}

/* Coeficientes (para nC) do bloco 4x4 em (bx,by), relativo ao MB, -1 = fora. */
static int nz_luma(ctx_t *c, int bx, int by, bool *ok)
{
    mbinfo_t *m = c->m;
    if (bx < 0) { m = c->a; bx += 4; }
    else if (by < 0) { m = c->b; by += 4; }
    if (!m) { *ok = false; return 0; }
    *ok = true;
    return m->nz[by * 4 + bx];
}

static int calc_nc(int na, bool oa, int nb, bool ob)
{
    if (oa && ob) return (na + nb + 1) >> 1;
    if (oa) return na;
    if (ob) return nb;
    return 0;
}

static int nc_luma(ctx_t *c, int bx, int by)
{
    bool oa, ob;
    int na = nz_luma(c, bx - 1, by, &oa), nb = nz_luma(c, bx, by - 1, &ob);
    return calc_nc(na, oa, nb, ob);
}

static int nc_croma(ctx_t *c, int comp, int bx, int by)
{
    int base = 16 + comp * 4;
    bool oa = true, ob = true;
    int na, nb;
    if (bx > 0) na = c->m->nz[base + by * 2 + bx - 1];
    else if (c->a) na = c->a->nz[base + by * 2 + 1];
    else { na = 0; oa = false; }
    if (by > 0) nb = c->m->nz[base + (by - 1) * 2 + bx];
    else if (c->b) nb = c->b->nz[base + 2 + bx];
    else { nb = 0; ob = false; }
    return calc_nc(na, oa, nb, ob);
}

/* ------------------------------------------------- compensação de movimento */

static inline int tap6(const uint8_t *p, int s)
{
    return p[-2 * s] - 5 * p[-s] + 20 * p[0] + 20 * p[s] - 5 * p[2 * s] + p[3 * s];
}

/* Copia a região [x-2, x+w+3) x [y-2, y+h+3) do plano, repetindo a borda. */
static void buscar(uint8_t *dst, int dp, const uint8_t *src, int sp, int sl, int sa, int x, int y, int w, int hh)
{
    if (x >= 0 && y >= 0 && x + w <= sl && y + hh <= sa) {
        for (int j = 0; j < hh; j++) memcpy(dst + j * dp, src + (y + j) * sp + x, w);
        return;
    }
    for (int j = 0; j < hh; j++) {
        int yy = clip(y + j, 0, sa - 1);
        for (int i = 0; i < w; i++) dst[j * dp + i] = src[yy * sp + clip(x + i, 0, sl - 1)];
    }
}

static void mc_luma(h264_t *h, quadro_t *ref, uint8_t *d, int x, int y, int w, int hh, int mvx, int mvy)
{
    uint8_t tmp[21 * 21];
    int fx = mvx & 3, fy = mvy & 3;
    int xi = x + (mvx >> 2), yi = y + (mvy >> 2);
    buscar(tmp, 21, ref->y, h->passo, h->larg, h->alt, xi - 2, yi - 2, w + 5, hh + 5);
    const uint8_t *s = tmp + 2 * 21 + 2;
    int ps = h->passo;
    if (!fx && !fy) {
        for (int j = 0; j < hh; j++) memcpy(d + j * ps, s + j * 21, w);
        return;
    }
    for (int j = 0; j < hh; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *p = s + j * 21 + i;
            int G = p[0], v;
#define BH(q) clip1((tap6((q), 1) + 16) >> 5)
#define HV(q) clip1((tap6((q), 21) + 16) >> 5)
            if (fy == 0) {
                int b = BH(p);
                v = fx == 1 ? (G + b + 1) >> 1 : fx == 2 ? b : (p[1] + b + 1) >> 1;
            } else if (fx == 0) {
                int hv = HV(p);
                v = fy == 1 ? (G + hv + 1) >> 1 : fy == 2 ? hv : (p[21] + hv + 1) >> 1;
            } else if (fx == 2 || fy == 2) {
                int j1 = 0;
                for (int k = -2; k <= 3; k++) {
                    int c = tap6(p + k * 21, 1);
                    static const int w6[6] = { 1, -5, 20, 20, -5, 1 };
                    j1 += w6[k + 2] * c;
                }
                int jj = clip1((j1 + 512) >> 10);
                if (fx == 2 && fy == 2) v = jj;
                else if (fx == 2) {   /* f ou q */
                    int b = fy == 1 ? BH(p) : BH(p + 21);
                    v = (b + jj + 1) >> 1;
                } else {         /* i ou k */
                    int hv = fx == 1 ? HV(p) : HV(p + 1);
                    v = (hv + jj + 1) >> 1;
                }
            } else {             /* e, g, p, r */
                int b = fy == 1 ? BH(p) : BH(p + 21);
                int hv = fx == 1 ? HV(p) : HV(p + 1);
                v = (b + hv + 1) >> 1;
            }
#undef BH
#undef HV
            d[j * ps + i] = (uint8_t)v;
        }
}

static void mc_croma(h264_t *h, const uint8_t *plano, uint8_t *d, int x, int y, int w, int hh, int mvx, int mvy)
{
    uint8_t tmp[9 * 9];
    int fx = mvx & 7, fy = mvy & 7;
    int lc = h->larg / 2, ac = h->alt / 2;
    buscar(tmp, 9, plano, h->passo_c, lc, ac, x + (mvx >> 3), y + (mvy >> 3), w + 1, hh + 1);
    int a = (8 - fx) * (8 - fy), b = fx * (8 - fy), c = (8 - fx) * fy, dd = fx * fy;
    for (int j = 0; j < hh; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *p = tmp + j * 9 + i;
            d[j * h->passo_c + i] = (uint8_t)((a * p[0] + b * p[1] + c * p[9] + dd * p[10] + 32) >> 6);
        }
}

/* --------------------------------------------- predição de vetor de movimento */

/* Cache 6x5: linha 0 = acima, colunas 0 = esquerda, 5 = direita. */
typedef struct {
    int8_t  ref[5][6];
    int16_t mv[5][6][2];
} mvc_t;

static void mvc_preencher(h264_t *h, ctx_t *c, mvc_t *k)
{
    for (int r = 0; r < 5; r++)
        for (int q = 0; q < 6; q++) { k->ref[r][q] = REF_NADA; k->mv[r][q][0] = k->mv[r][q][1] = 0; }
    (void)h;
    mbinfo_t *ns[4] = { c->a, c->b, c->c, c->d };
    for (int n = 0; n < 4; n++) {
        mbinfo_t *m = ns[n];
        if (!m) continue;
        int linhas[4], colunas[4], qtd = 0;
        if (n == 0) for (int y = 0; y < 4; y++) { linhas[qtd] = y; colunas[qtd++] = 3; }
        else if (n == 1) for (int x = 0; x < 4; x++) { linhas[qtd] = 3; colunas[qtd++] = x; }
        else if (n == 2) { linhas[0] = 3; colunas[0] = 0; qtd = 1; }
        else { linhas[0] = 3; colunas[0] = 3; qtd = 1; }
        for (int i = 0; i < qtd; i++) {
            int by = linhas[i], bx = colunas[i];
            int r, q;
            if (n == 0) { r = by + 1; q = 0; }
            else if (n == 1) { r = 0; q = bx + 1; }
            else if (n == 2) { r = 0; q = 5; }
            else { r = 0; q = 0; }
            if (m->intra) { k->ref[r][q] = REF_INTRA; continue; }
            k->ref[r][q] = m->refidx[(by >> 1) * 2 + (bx >> 1)];
            k->mv[r][q][0] = m->mv[by * 4 + bx][0];
            k->mv[r][q][1] = m->mv[by * 4 + bx][1];
        }
    }
}

static void mvc_escrever(mvc_t *k, int bx, int by, int w, int hh, int ref, int mx, int my)
{
    for (int y = by; y < by + hh; y++)
        for (int x = bx; x < bx + w; x++) {
            k->ref[y + 1][x + 1] = (int8_t)ref;
            k->mv[y + 1][x + 1][0] = (int16_t)mx;
            k->mv[y + 1][x + 1][1] = (int16_t)my;
        }
}

/* forma: 0 normal, 1 16x8 de cima, 2 16x8 de baixo, 3 8x16 esquerda, 4 8x16 direita */
static void prever_mv(mvc_t *k, int bx, int by, int w, int ref, int forma, int *px, int *py)
{
    int ra = k->ref[by + 1][bx], rb = k->ref[by][bx + 1], rc = k->ref[by][bx + w + 1];
    const int16_t *ma = k->mv[by + 1][bx], *mb = k->mv[by][bx + 1], *mc = k->mv[by][bx + w + 1];
    /* C dentro do MB ainda não decodificado, ou à direita: usa D. */
    bool c_ok = rc != REF_NADA;
    if (bx + w >= 4 && by > 0) c_ok = false;
    if (by > 0 && bx + w < 4 && rc == REF_NADA) c_ok = false;
    if (!c_ok) { rc = k->ref[by][bx]; mc = k->mv[by][bx]; }
    if (forma == 1 && rb == ref) { *px = mb[0]; *py = mb[1]; return; }
    if (forma == 2 && ra == ref) { *px = ma[0]; *py = ma[1]; return; }
    if (forma == 3 && ra == ref) { *px = ma[0]; *py = ma[1]; return; }
    if (forma == 4 && rc == ref) { *px = mc[0]; *py = mc[1]; return; }
    if (rb == REF_NADA && rc == REF_NADA && ra != REF_NADA) { *px = ma[0]; *py = ma[1]; return; }
    int iguais = (ra == ref) + (rb == ref) + (rc == ref);
    if (iguais == 1) {
        const int16_t *m = ra == ref ? ma : rb == ref ? mb : mc;
        *px = m[0]; *py = m[1];
        return;
    }
    *px = mediana(ma[0], mb[0], mc[0]);
    *py = mediana(ma[1], mb[1], mc[1]);
}

/* --------------------------------------------------- decodificação de MB */

static inline uint8_t *py_(h264_t *h, quadro_t *q, int mbx, int mby) { return q->y + mby * 16 * h->passo + mbx * 16; }

static void reconstruir_inter(h264_t *h, ctx_t *c, mvc_t *k)
{
    quadro_t *q = h->atual;
    int x0 = c->mbx * 16, y0 = c->mby * 16;
    /* Vetores de cada bloco 4x4 do cache; agrupa por 8x8 quando iguais. */
    for (int b8 = 0; b8 < 4; b8++) {
        int bx = (b8 & 1) * 2, by = (b8 >> 1) * 2;
        int ri = k->ref[by + 1][bx + 1];
        quadro_t *ref = (ri >= 0 && ri <= MAX_REF) ? h->lista[ri] : NULL;
        if (!ref) ref = h->lista[0] ? h->lista[0] : q;
        bool iguais = true;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                if (k->mv[by + 1 + j][bx + 1 + i][0] != k->mv[by + 1][bx + 1][0] ||
                    k->mv[by + 1 + j][bx + 1 + i][1] != k->mv[by + 1][bx + 1][1]) iguais = false;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++) {
                if (iguais && (i || j)) continue;
                int xx = bx + i, yy = by + j, tam = iguais ? 8 : 4;
                int mx = k->mv[yy + 1][xx + 1][0], my = k->mv[yy + 1][xx + 1][1];
                int lx = x0 + xx * 4, ly = y0 + yy * 4;
                mc_luma(h, ref, q->y + ly * h->passo + lx, lx, ly, tam, tam, mx, my);
                int cx = lx / 2, cy = ly / 2, ct = tam / 2;
                mc_croma(h, ref->u, q->u + cy * h->passo_c + cx, cx, cy, ct, ct, mx, my);
                mc_croma(h, ref->v, q->v + cy * h->passo_c + cx, cx, cy, ct, ct, mx, my);
            }
    }
}

static void bordas_luma(h264_t *h, ctx_t *c, int bx, int by, int *t, int *l, int *lt, bool *tt, bool *tl)
{
    /* bx,by em blocos 4x4 dentro do MB; vizinhos intra 4x4 */
    quadro_t *q = h->atual;
    int px = c->mbx * 16 + bx * 4, pyy = c->mby * 16 + by * 4;
    const uint8_t *p = q->y + pyy * h->passo + px;
    bool ta = by > 0 || c->b, la = bx > 0 || c->a;
    bool lta = (bx > 0 && by > 0) || (bx == 0 && by > 0 && c->a) || (by == 0 && bx > 0 && c->b) ||
               (bx == 0 && by == 0 && c->d);
    bool tra;
    if (by == 0) tra = bx < 3 ? (c->b != NULL) : (c->c != NULL);
    else if (bx == 3) tra = false;
    else tra = blk_ordem[by - 1][bx + 1] < blk_ordem[by][bx];
    *tt = ta; *tl = la;
    for (int i = 0; i < 4; i++) {
        t[i] = ta ? p[-h->passo + i] : 128;
        l[i] = la ? p[i * h->passo - 1] : 128;
    }
    for (int i = 4; i < 8; i++) t[i] = tra ? p[-h->passo + i] : t[3];
    *lt = lta ? p[-h->passo - 1] : 128;
}

static int getenv_dbg_mb = -1;
static int decodificar_mb(h264_t *h, bits_t *b, ctx_t *c, int tipo_mb, bool skip)
{
    mbinfo_t *m = c->m;
    quadro_t *q = h->atual;
    int passo = h->passo, pc = h->passo_c;
    memset(m->nz, 0, sizeof m->nz);
    m->fatia = h->fatia_atual;
    m->intra = 0;
    m->i4x4 = 0;
    for (int i = 0; i < 16; i++) m->modo4[i] = 2;

    mvc_t k;
    bool inter = !skip && h->tipo == 0 && tipo_mb < 5;
    if (h->tipo == 0 && !skip && tipo_mb >= 5) tipo_mb -= 5;
    bool intra = !skip && !inter;
    int cbp = 0, modo16 = -1, modo_croma = 0;

    if (intra && tipo_mb == 25) {        /* I_PCM */
        b->pos = (b->pos + 7) & ~7u;
        uint8_t *y = py_(h, q, c->mbx, c->mby);
        for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) y[j * passo + i] = (uint8_t)ler_bits(b, 8);
        for (int comp = 0; comp < 2; comp++) {
            uint8_t *d = (comp ? q->v : q->u) + c->mby * 8 * pc + c->mbx * 8;
            for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) d[j * pc + i] = (uint8_t)ler_bits(b, 8);
        }
        m->intra = 2;
        memset(m->nz, 16, sizeof m->nz);
        m->qp = 0; m->qpc = 0;
        for (int i = 0; i < 4; i++) { m->refidx[i] = -1; m->ref[i] = -1; }
        memset(m->mv, 0, sizeof m->mv);
        return 0;
    }

    if (intra) {
        m->intra = 1;
        if (tipo_mb == 0) {
            m->i4x4 = 1;
            for (int i = 0; i < 16; i++) {
                int bx = blk_x[i], by = blk_y[i];
                int ma, mb;
                bool oka, okb;
                if (bx > 0) { ma = m->modo4[by * 4 + bx - 1]; oka = true; }
                else if (c->a) { ma = c->a->i4x4 ? c->a->modo4[by * 4 + 3] : 2; oka = true; }
                else { ma = 2; oka = false; }
                if (by > 0) { mb = m->modo4[(by - 1) * 4 + bx]; okb = true; }
                else if (c->b) { mb = c->b->i4x4 ? c->b->modo4[12 + bx] : 2; okb = true; }
                else { mb = 2; okb = false; }
                int pred = (oka && okb) ? (ma < mb ? ma : mb) : 2;
                int modo;
                if (ler_bit(b)) modo = pred;
                else { int r = (int)ler_bits(b, 3); modo = r < pred ? r : r + 1; }
                m->modo4[by * 4 + bx] = (int8_t)modo;
            }
        } else {
            modo16 = (tipo_mb - 1) % 4;
            cbp = (((tipo_mb - 1) / 4) % 3) << 4;
            if (tipo_mb >= 13) cbp |= 15;
        }
        modo_croma = (int)ue(b);
        for (int i = 0; i < 4; i++) { m->refidx[i] = -1; m->ref[i] = -1; }
        memset(m->mv, 0, sizeof m->mv);
    } else {
        mvc_preencher(h, c, &k);
        int nref = h->num_ref_ativas;
        if (skip) {
            int ra = k.ref[1][0], rb = k.ref[0][1];
            int mx = 0, my = 0;
            if (ra == REF_NADA || rb == REF_NADA ||
                (ra == 0 && !k.mv[1][0][0] && !k.mv[1][0][1]) ||
                (rb == 0 && !k.mv[0][1][0] && !k.mv[0][1][1])) mx = my = 0;
            else prever_mv(&k, 0, 0, 4, 0, 0, &mx, &my);
            mvc_escrever(&k, 0, 0, 4, 4, 0, mx, my);
        } else if (tipo_mb < 3) {
            int np = tipo_mb == 0 ? 1 : 2;
            int ri[2] = { 0, 0 };
            for (int p = 0; p < np; p++)
                if (nref > 1) ri[p] = nref == 2 ? !ler_bit(b) : (int)ue(b);
            for (int p = 0; p < np; p++) {
                int bx, by, w, hh, forma;
                if (tipo_mb == 0) { bx = 0; by = 0; w = 4; hh = 4; forma = 0; }
                else if (tipo_mb == 1) { bx = 0; by = p * 2; w = 4; hh = 2; forma = 1 + p; }
                else { bx = p * 2; by = 0; w = 2; hh = 4; forma = 3 + p; }
                int mx, my;
                prever_mv(&k, bx, by, w, ri[p], forma, &mx, &my);
                mx += se(b); my += se(b);
                mvc_escrever(&k, bx, by, w, hh, ri[p], mx, my);
            }
        } else {
            int sub[4], ri[4] = { 0, 0, 0, 0 };
            for (int i = 0; i < 4; i++) sub[i] = (int)ue(b) & 3;
            if (nref > 1 && tipo_mb != 4)
                for (int i = 0; i < 4; i++) ri[i] = nref == 2 ? !ler_bit(b) : (int)ue(b);
            for (int i = 0; i < 4; i++) {
                int bx0 = (i & 1) * 2, by0 = (i >> 1) * 2;
                int n = sub[i] == 0 ? 1 : sub[i] == 3 ? 4 : 2;
                for (int s = 0; s < n; s++) {
                    int bx = bx0, by = by0, w = 2, hh = 2;
                    if (sub[i] == 1) { hh = 1; by += s; }
                    else if (sub[i] == 2) { w = 1; bx += s; }
                    else if (sub[i] == 3) { w = 1; hh = 1; bx += s & 1; by += s >> 1; }
                    int mx, my;
                    prever_mv(&k, bx, by, w, ri[i], 0, &mx, &my);
                    mx += se(b); my += se(b);
                    mvc_escrever(&k, bx, by, w, hh, ri[i], mx, my);
                }
            }
        }
        for (int i = 0; i < 4; i++) {
            int r = k.ref[(i >> 1) * 2 + 1][(i & 1) * 2 + 1];
            m->refidx[i] = (int8_t)r;
            quadro_t *rq = (r >= 0 && r <= MAX_REF) ? h->lista[r] : NULL;
            m->ref[i] = (int8_t)(rq ? rq->id & 127 : -1);
        }
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                m->mv[y * 4 + x][0] = k.mv[y + 1][x + 1][0];
                m->mv[y * 4 + x][1] = k.mv[y + 1][x + 1][1];
            }
        if (getenv_dbg_mb == c->idx) { DBG("MB %d tipo %d skip %d:", c->idx, tipo_mb, skip); for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) DBG(" [%d %d r%d]", k.mv[y+1][x+1][0], k.mv[y+1][x+1][1], k.ref[y+1][x+1]); DBG("\n"); }
        reconstruir_inter(h, c, &k);
    }

    if (!skip && modo16 < 0) {
        uint32_t cod = ue(b);
        if (cod > 47) return -1;
        cbp = intra ? cbp_intra[cod] : cbp_inter[cod];
    }
    if (skip) cbp = 0;
    if (cbp || modo16 >= 0) {
        int d = se(b);
        if (d < -26 || d > 25) return -1;
        h->qp = (h->qp + d + 52) % 52;
    }
    m->qp = (int8_t)h->qp;
    int qpc = qp_croma[clip(h->qp + h->croma_qp_off, 0, 51)];
    m->qpc = (int8_t)qpc;

    /* Coeficientes */
    int lum[16][16];
    int16_t dc[16];
    memset(lum, 0, sizeof lum);
    int qp = h->qp, q6 = qp / 6, r6 = qp % 6;
    if (modo16 >= 0) {
        memset(dc, 0, sizeof dc);
        int n = residuo(b, nc_luma(c, 0, 0), dc, 0, 16);
        if (n < 0) return -1;
        if (cbp & 15)
            for (int i = 0; i < 16; i++) {
                int bx = blk_x[i], by = blk_y[i];
                int16_t t[16];
                memset(t, 0, sizeof t);
                n = residuo(b, nc_luma(c, bx, by), t, 0, 15);
                if (n < 0) return -1;
                m->nz[by * 4 + bx] = (uint8_t)n;
                for (int j = 0; j < 15; j++) lum[by * 4 + bx][zigzag[j + 1]] = t[j];
            }
        /* DC: Hadamard 4x4 */
        int cm[16], tmp[16];
        for (int i = 0; i < 16; i++) cm[zigzag[i]] = dc[i];
        for (int i = 0; i < 4; i++) {
            int *r = &cm[i * 4];
            int a = r[0] + r[1], bq = r[0] - r[1], cc = r[2] + r[3], dd = r[2] - r[3];
            tmp[i * 4 + 0] = a + cc; tmp[i * 4 + 1] = a - cc; tmp[i * 4 + 2] = bq - dd; tmp[i * 4 + 3] = bq + dd;
        }
        for (int i = 0; i < 4; i++) {
            int a = tmp[i] + tmp[4 + i], bq = tmp[i] - tmp[4 + i], cc = tmp[8 + i] + tmp[12 + i], dd = tmp[8 + i] - tmp[12 + i];
            int f[4] = { a + cc, a - cc, bq - dd, bq + dd };
            for (int kk = 0; kk < 4; kk++) {
                int v = f[kk], ls = 16 * dq_v[r6][0];
                v = qp >= 36 ? (v * ls) << (q6 - 6) : (v * ls + (1 << (5 - q6))) >> (6 - q6);
                lum[kk * 4 + i][0] = v;   /* linha kk, coluna i do arranjo de blocos */
            }
        }
    } else {
        for (int i = 0; i < 16; i++) {
            if (!(cbp & (1 << (i >> 2)))) continue;
            int bx = blk_x[i], by = blk_y[i];
            int16_t t[16];
            memset(t, 0, sizeof t);
            int n = residuo(b, nc_luma(c, bx, by), t, 0, 16);
            if (n < 0) return -1;
            m->nz[by * 4 + bx] = (uint8_t)n;
            for (int j = 0; j < 16; j++) lum[by * 4 + bx][zigzag[j]] = t[j];
        }
    }
    int cro[2][4][16];
    memset(cro, 0, sizeof cro);
    if (cbp & 0x30) {
        int16_t cdc[2][4];
        for (int comp = 0; comp < 2; comp++) {
            memset(cdc[comp], 0, sizeof cdc[comp]);
            if (residuo(b, -1, cdc[comp], 0, 4) < 0) return -1;
        }
        if (cbp & 0x20)
            for (int comp = 0; comp < 2; comp++)
                for (int i = 0; i < 4; i++) {
                    int16_t t[16];
                    memset(t, 0, sizeof t);
                    int n = residuo(b, nc_croma(c, comp, i & 1, i >> 1), t, 0, 15);
                    if (n < 0) return -1;
                    m->nz[16 + comp * 4 + i] = (uint8_t)n;
                    for (int j = 0; j < 15; j++) cro[comp][i][zigzag[j + 1]] = t[j];
                }
        int c6 = qpc / 6, cr6 = qpc % 6;
        for (int comp = 0; comp < 2; comp++) {
            int a = cdc[comp][0], bq = cdc[comp][1], cc = cdc[comp][2], dd = cdc[comp][3];
            int f[4] = { a + bq + cc + dd, a - bq + cc - dd, a + bq - cc - dd, a - bq - cc + dd };
            for (int i = 0; i < 4; i++)
                cro[comp][i][0] = (((f[i] * 16 * dq_v[cr6][0]) << c6) >> 5);
        }
        for (int comp = 0; comp < 2; comp++)
            for (int i = 0; i < 4; i++)
                for (int j = 1; j < 16; j++) {
                    int cl = ((j & 3) & 1) == 0 && ((j >> 2) & 1) == 0 ? 0 : (((j & 3) & 1) && ((j >> 2) & 1)) ? 1 : 2;
                    cro[comp][i][j] = ((cro[comp][i][j] * dq_v[cr6][cl]) << c6);
                }
    }
    /* desquantiza luma (AC, ou tudo se não for 16x16) */
    for (int blk = 0; blk < 16; blk++)
        for (int j = modo16 >= 0 ? 1 : 0; j < 16; j++) {
            if (!lum[blk][j]) continue;
            int cl = ((j & 1) == 0 && ((j >> 2) & 1) == 0) ? 0 : ((j & 1) && ((j >> 2) & 1)) ? 1 : 2;
            lum[blk][j] = ((lum[blk][j] * dq_v[r6][cl]) << q6);
        }

    /* Reconstrução da luma */
    uint8_t *Y = py_(h, q, c->mbx, c->mby);
    if (intra && modo16 < 0) {
        for (int i = 0; i < 16; i++) {
            int bx = blk_x[i], by = blk_y[i];
            int t[8], l[4], lt;
            bool tt, tl;
            bordas_luma(h, c, bx, by, t, l, &lt, &tt, &tl);
            uint8_t *d = Y + by * 4 * passo + bx * 4;
            pred4x4(d, passo, m->modo4[by * 4 + bx], t, l, lt, tt, tl);
            idct4_somar(d, passo, lum[by * 4 + bx]);
        }
    } else {
        if (intra) {
            uint8_t t[16], l[16];
            int lt = 128;
            bool ta = c->b != NULL, la = c->a != NULL;
            for (int i = 0; i < 16; i++) {
                t[i] = ta ? Y[-passo + i] : 128;
                l[i] = la ? Y[i * passo - 1] : 128;
            }
            if (c->d) lt = Y[-passo - 1];
            if (modo16 == 0) for (int j = 0; j < 16; j++) memcpy(Y + j * passo, t, 16);
            else if (modo16 == 1) for (int j = 0; j < 16; j++) memset(Y + j * passo, l[j], 16);
            else if (modo16 == 2) {
                int s = 0, v;
                for (int i = 0; i < 16; i++) s += (ta ? t[i] : 0) + (la ? l[i] : 0);
                v = ta && la ? (s + 16) >> 5 : (ta || la) ? (s + 8) >> 4 : 128;
                for (int j = 0; j < 16; j++) memset(Y + j * passo, v, 16);
            } else pred_plano(Y, passo, 16, t, l, lt);
        }
        for (int i = 0; i < 16; i++) {
            int bx = i & 3, by = i >> 2;
            bool tem = modo16 >= 0 ? true : (cbp & (1 << ((by >> 1) * 2 + (bx >> 1)))) != 0;
            if (tem) idct4_somar(Y + by * 4 * passo + bx * 4, passo, lum[i]);
        }
    }
    /* Croma */
    for (int comp = 0; comp < 2; comp++) {
        uint8_t *C = (comp ? q->v : q->u) + c->mby * 8 * pc + c->mbx * 8;
        if (intra) {
            uint8_t t[8], l[8];
            int lt = 128;
            bool ta = c->b != NULL, la = c->a != NULL;
            for (int i = 0; i < 8; i++) { t[i] = ta ? C[-pc + i] : 128; l[i] = la ? C[i * pc - 1] : 128; }
            if (c->d) lt = C[-pc - 1];
            if (modo_croma == 0) {
                for (int bl = 0; bl < 4; bl++) {
                    int xo = (bl & 1) * 4, yo = (bl >> 1) * 4, st = 0, sl = 0, v;
                    for (int i = 0; i < 4; i++) { st += t[xo + i]; sl += l[yo + i]; }
                    if ((xo == 0 && yo == 0) || (xo && yo)) {
                        v = ta && la ? (st + sl + 4) >> 3 : la ? (sl + 2) >> 2 : ta ? (st + 2) >> 2 : 128;
                    } else if (xo) {
                        v = ta ? (st + 2) >> 2 : la ? (sl + 2) >> 2 : 128;
                    } else {
                        v = la ? (sl + 2) >> 2 : ta ? (st + 2) >> 2 : 128;
                    }
                    for (int j = 0; j < 4; j++) memset(C + (yo + j) * pc + xo, v, 4);
                }
            } else if (modo_croma == 1) for (int j = 0; j < 8; j++) memset(C + j * pc, l[j], 8);
            else if (modo_croma == 2) for (int j = 0; j < 8; j++) memcpy(C + j * pc, t, 8);
            else pred_plano(C, pc, 8, t, l, lt);
        }
        if (cbp & 0x30)
            for (int i = 0; i < 4; i++)
                idct4_somar(C + (i >> 1) * 4 * pc + (i & 1) * 4, pc, cro[comp][i]);
    }
    return 0;
}

/* -------------------------------------------------------- desbloqueio */

static void filtrar_borda(uint8_t *p, int passo_borda, int passo_amostra, int n, const int *bs,
                          int qpm, int ia_off, int ib_off, bool croma)
{
    int ia = clip(qpm + ia_off, 0, 51), ib = clip(qpm + ib_off, 0, 51);
    int alfa = alfa_t[ia], beta = beta_t[ib];
    if (!alfa || !beta) return;
    for (int i = 0; i < n; i++) {
        int s = bs[croma ? i / 2 : i / 4];
        if (!s) continue;
        uint8_t *q = p + i * passo_amostra;
        int p0 = q[-passo_borda], p1 = q[-2 * passo_borda], q0 = q[0], q1 = q[passo_borda];
        if (absi(p0 - q0) >= alfa || absi(p1 - p0) >= beta || absi(q1 - q0) >= beta) continue;
        if (croma) {
            if (s < 4) {
                int tc = tc0_t[ia][s - 1] + 1;
                int d = clip((((q0 - p0) << 2) + (p1 - q1) + 4) >> 3, -tc, tc);
                q[-passo_borda] = clip1(p0 + d);
                q[0] = clip1(q0 - d);
            } else {
                q[-passo_borda] = (uint8_t)((2 * p1 + p0 + q1 + 2) >> 2);
                q[0] = (uint8_t)((2 * q1 + q0 + p1 + 2) >> 2);
            }
            continue;
        }
        int p2 = q[-3 * passo_borda], q2 = q[2 * passo_borda];
        int ap = absi(p2 - p0), aq = absi(q2 - q0);
        if (s < 4) {
            int c0 = tc0_t[ia][s - 1];
            int tc = c0 + (ap < beta) + (aq < beta);
            int d = clip((((q0 - p0) << 2) + (p1 - q1) + 4) >> 3, -tc, tc);
            q[-passo_borda] = clip1(p0 + d);
            q[0] = clip1(q0 - d);
            if (ap < beta) q[-2 * passo_borda] = (uint8_t)(p1 + clip((p2 + ((p0 + q0 + 1) >> 1) - (p1 << 1)) >> 1, -c0, c0));
            if (aq < beta) q[passo_borda] = (uint8_t)(q1 + clip((q2 + ((p0 + q0 + 1) >> 1) - (q1 << 1)) >> 1, -c0, c0));
        } else {
            int p3 = q[-4 * passo_borda], q3 = q[3 * passo_borda];
            bool forte = absi(p0 - q0) < ((alfa >> 2) + 2);
            if (ap < beta && forte) {
                q[-passo_borda] = (uint8_t)((p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3);
                q[-2 * passo_borda] = (uint8_t)((p2 + p1 + p0 + q0 + 2) >> 2);
                q[-3 * passo_borda] = (uint8_t)((2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3);
            } else q[-passo_borda] = (uint8_t)((2 * p1 + p0 + q1 + 2) >> 2);
            if (aq < beta && forte) {
                q[0] = (uint8_t)((p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3);
                q[passo_borda] = (uint8_t)((p0 + q0 + q1 + q2 + 2) >> 2);
                q[2 * passo_borda] = (uint8_t)((2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3);
            } else q[0] = (uint8_t)((2 * q1 + q0 + p1 + 2) >> 2);
        }
    }
}

static int calc_bs(mbinfo_t *p, int bp, mbinfo_t *q, int bq, bool borda_mb)
{
    if (p->intra || q->intra) return borda_mb ? 4 : 3;
    if (p->nz[bp] || q->nz[bq]) return 2;
    int rp = p->ref[(bp >> 3) * 2 + ((bp & 3) >> 1)], rq = q->ref[(bq >> 3) * 2 + ((bq & 3) >> 1)];
    if (rp != rq) return 1;
    if (absi(p->mv[bp][0] - q->mv[bq][0]) >= 4 || absi(p->mv[bp][1] - q->mv[bq][1]) >= 4) return 1;
    return 0;
}

typedef struct { int modo, alfa, beta; } desbloq_t;

static void desbloquear(h264_t *h, const desbloq_t *dbf)
{
    quadro_t *qd = h->atual;
    for (int my = 0; my < h->alt_mb; my++)
        for (int mx = 0; mx < h->larg_mb; mx++) {
            mbinfo_t *m = &h->mb[my * h->larg_mb + mx];
            const desbloq_t *f = &dbf[m->fatia & 255];
            if (f->modo == 1) continue;
            for (int dir = 0; dir < 2; dir++)
                for (int e = 0; e < 4; e++) {
                    mbinfo_t *p = m;
                    if (e == 0) {
                        if (dir == 0 && mx == 0) continue;
                        if (dir == 1 && my == 0) continue;
                        p = dir == 0 ? m - 1 : m - h->larg_mb;
                        if (f->modo == 2 && p->fatia != m->fatia) continue;
                    }
                    int bs[4];
                    bool algum = false;
                    for (int i = 0; i < 4; i++) {
                        int bq = dir == 0 ? i * 4 + e : e * 4 + i;
                        int bp = e ? (dir == 0 ? bq - 1 : bq - 4) : (dir == 0 ? i * 4 + 3 : 12 + i);
                        bs[i] = calc_bs(p, bp, m, bq, e == 0);
                        if (bs[i]) algum = true;
                    }
                    if (!algum) continue;
                    int qpl = (p->qp + m->qp + 1) >> 1;
                    uint8_t *Y = qd->y + my * 16 * h->passo + mx * 16;
                    if (dir == 0) filtrar_borda(Y + e * 4, 1, h->passo, 16, bs, qpl, f->alfa, f->beta, false);
                    else filtrar_borda(Y + e * 4 * h->passo, h->passo, 1, 16, bs, qpl, f->alfa, f->beta, false);
                    if (e & 1) continue;
                    int qpc = (p->qpc + m->qpc + 1) >> 1;
                    for (int comp = 0; comp < 2; comp++) {
                        uint8_t *C = (comp ? qd->v : qd->u) + my * 8 * h->passo_c + mx * 8;
                        if (dir == 0) filtrar_borda(C + e * 2, 1, h->passo_c, 8, bs, qpc, f->alfa, f->beta, true);
                        else filtrar_borda(C + e * 2 * h->passo_c, h->passo_c, 1, 8, bs, qpc, f->alfa, f->beta, true);
                    }
                }
        }
}

/* ------------------------------------------------------------- fatias */

static desbloq_t dbf_fatias[256];

static void fechar_quadro(h264_t *h)
{
    if (!h->quadro_aberto) return;
    desbloquear(h, dbf_fatias);
    if (h->nal_ref) {
        h->atual->ref = true;
        janela_deslizante(h);
    }
    h->quadro_aberto = false;
}

static int ler_fatia(h264_t *h, bits_t *b, int tipo_nal, int nal_ref)
{
    if (!h->sps_ok || !h->pps_ok) return H264_SEM_SUPORTE;
    int primeiro = (int)ue(b);
    int tipo = (int)ue(b) % 5;
    if (tipo != 0 && tipo != 2) return H264_SEM_SUPORTE;
    ue(b);
    int frame_num = (int)ler_bits(b, h->log2_frame_num);
    bool idr = tipo_nal == 5;
    if (primeiro == 0) {
        fechar_quadro(h);
        if (idr) for (int i = 0; i < MAX_REF + 2; i++) h->q[i].ref = false;
        quadro_t *livre = NULL;
        for (int i = 0; i < MAX_REF + 2; i++)
            if (!h->q[i].ref && &h->q[i] != h->atual) { livre = &h->q[i]; break; }
        if (!livre) livre = &h->q[0];
        h->atual = livre;
        livre->frame_num = frame_num;
        livre->id = ++h->prox_id;
        livre->ref = false;
        h->quadro_aberto = true;
        h->fatia_atual = 0;
        for (int i = 0; i < h->larg_mb * h->alt_mb; i++) h->mb[i].fatia = -1;
    } else if (!h->quadro_aberto) return H264_ERRO;
    else h->fatia_atual++;
    h->tipo = tipo;
    h->frame_num = frame_num;
    h->nal_ref = nal_ref;
    h->idr = idr;
    if (idr) ue(b);
    if (h->poc_tipo == 0) {
        ler_bits(b, h->log2_poc_lsb);
        if (h->poc_inferior) se(b);
    } else if (h->poc_tipo == 1 && !h->delta_poc_zero) {
        se(b);
        if (h->poc_inferior) se(b);
    }
    if (h->redundante) ue(b);
    h->num_ref_ativas = h->ref_ativas_padrao;
    if (tipo == 0) {
        if (ler_bit(b)) h->num_ref_ativas = (int)ue(b) + 1;
        if (h->num_ref_ativas > MAX_REF) return H264_ERRO;
        montar_lista(h);
        modificar_lista(h, b);
        if (!h->lista[0]) {            /* começou sem referência: usa cinza */
            h->lista[0] = h->atual;
            memset(h->atual->y, 128, h->larg * h->alt * 3 / 2);
        }
        if (h->ponderada) {
            ue(b); ue(b);
            for (int i = 0; i < h->num_ref_ativas; i++) {
                if (ler_bit(b)) { se(b); se(b); }
                if (ler_bit(b)) { se(b); se(b); se(b); se(b); }
            }
        }
    }
    if (nal_ref) {
        if (idr) { ler_bit(b); ler_bit(b); }
        else if (ler_bit(b)) {
            int maxf = 1 << h->log2_frame_num;
            for (;;) {
                int op = (int)ue(b);
                if (op == 0) break;
                if (op == 1 || op == 3) {
                    int dif = (int)ue(b) + 1;
                    if (op == 1) {
                        int num = h->frame_num - dif;
                        for (int i = 0; i < MAX_REF + 2; i++) {
                            quadro_t *q = &h->q[i];
                            if (!q->ref || q == h->atual) continue;
                            int pn = q->frame_num > h->frame_num ? q->frame_num - maxf : q->frame_num;
                            if (pn == num) q->ref = false;
                        }
                    }
                }
                if (op == 2 || op == 3 || op == 6 || op == 4) ue(b);
                if (op == 5) for (int i = 0; i < MAX_REF + 2; i++) if (&h->q[i] != h->atual) h->q[i].ref = false;
                if (op > 6) return H264_ERRO;
            }
        }
    }
    h->qp = h->qp_inicial + se(b);
    desbloq_t *f = &dbf_fatias[h->fatia_atual & 255];
    f->modo = 0; f->alfa = 0; f->beta = 0;
    if (h->ctrl_desbloq) {
        f->modo = (int)ue(b);
        if (f->modo != 1) { f->alfa = se(b) * 2; f->beta = se(b) * 2; }
    }
    /* dados da fatia */
    int total = h->larg_mb * h->alt_mb, mb = primeiro;
    while (mb < total) {
        if (tipo == 0) {
            int corrida = (int)ue(b);
            while (corrida-- > 0 && mb < total) {
                ctx_t c = { mb % h->larg_mb, mb / h->larg_mb, mb, &h->mb[mb], 0, 0, 0, 0 };
                c.a = vizinho(h, c.mbx - 1, c.mby); c.b = vizinho(h, c.mbx, c.mby - 1);
                c.c = vizinho(h, c.mbx + 1, c.mby - 1); c.d = vizinho(h, c.mbx - 1, c.mby - 1);
                h->mb[mb].fatia = h->fatia_atual;
                decodificar_mb(h, b, &c, 0, true);
                mb++;
            }
            if (mb >= total || !mais_dados(b)) break;
        }
        ctx_t c = { mb % h->larg_mb, mb / h->larg_mb, mb, &h->mb[mb], 0, 0, 0, 0 };
        c.a = vizinho(h, c.mbx - 1, c.mby); c.b = vizinho(h, c.mbx, c.mby - 1);
        c.c = vizinho(h, c.mbx + 1, c.mby - 1); c.d = vizinho(h, c.mbx - 1, c.mby - 1);
        h->mb[mb].fatia = h->fatia_atual;
        int tipo_mb = (int)ue(b);
        DBG("mb %d tipo_mb %d pos %u qp %d\n", mb, tipo_mb, b->pos, h->qp);
        if (tipo_mb > (tipo == 0 ? 30 : 25)) return H264_ERRO;
        if (decodificar_mb(h, b, &c, tipo_mb, false) < 0) { DBG("mb %d tipo %d fatia %d pos %u/%u\n", mb, tipo_mb, tipo, b->pos, b->fim); return H264_ERRO; }
        mb++;
        if (!mais_dados(b)) break;
        if (b->pos > b->fim) return H264_ERRO;
    }
    return 0;
}

/* ---------------------------------------------------------- NAL / API */

int h264_nal(h264_t *h, const uint8_t *d, int n)
{
    if (n < 1) return 0;
    if (n + 8 > h->rbsp_cap) {
        if (h->rbsp) libera(h->rbsp);
        h->rbsp_cap = n + 64 + n / 4;
        h->rbsp = aloca(h->rbsp_cap);
        if (!h->rbsp) { h->rbsp_cap = 0; return H264_SEM_MEMORIA; }
    }
    int k = 0;
    for (int i = 1; i < n; i++) {   /* tira os bytes de prevenção 00 00 03 */
        if (i + 2 < n && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 3) {
            h->rbsp[k++] = 0; h->rbsp[k++] = 0; i += 2;
            continue;
        }
        h->rbsp[k++] = d[i];
    }
    memset(h->rbsp + k, 0, 8);
    while (k > 0 && !h->rbsp[k - 1]) k--;
    if (!k) return 0;
    bits_t b = { h->rbsp, 0, (uint32_t)k * 8 - 1 - (uint32_t)__builtin_ctz(h->rbsp[k - 1]) };
    int tipo = d[0] & 31, ref = (d[0] >> 5) & 3;
    switch (tipo) {
    case 7: return ler_sps(h, &b);
    case 8: return ler_pps(h, &b);
    case 1: case 5: {
        int r = ler_fatia(h, &b, tipo, ref);
        if (r < 0) h->erros++;
        return r;
    }
    default: return 0;
    }
}

int h264_amostra(h264_t *h, const uint8_t *d, int n, int tam_len)
{
    int pos = 0, r = 0;
    while (pos + tam_len <= n) {
        uint32_t l = 0;
        for (int i = 0; i < tam_len; i++) l = (l << 8) | d[pos + i];
        pos += tam_len;
        if (l > (uint32_t)(n - pos)) return H264_ERRO;
        int e = h264_nal(h, d + pos, (int)l);
        if (e < 0) r = e;
        pos += (int)l;
    }
    fechar_quadro(h);
    return r;
}

int h264_config(h264_t *h, const uint8_t *avcc, int n, int *tam_len)
{
    if (n < 7) return H264_ERRO;
    *tam_len = (avcc[4] & 3) + 1;
    int pos = 5, qtd = avcc[pos++] & 31;
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < qtd; i++) {
            if (pos + 2 > n) return H264_ERRO;
            int l = (avcc[pos] << 8) | avcc[pos + 1];
            pos += 2;
            if (pos + l > n) return H264_ERRO;
            int e = h264_nal(h, avcc + pos, l);
            if (e < 0) return e;
            pos += l;
        }
        if (t == 0) { if (pos >= n) return H264_ERRO; qtd = avcc[pos++]; }
    }
    return 0;
}

bool h264_imagem(h264_t *h, h264_imagem_t *im)
{
    if (!h->atual) return false;
    quadro_t *q = h->atual;
    int cx = h->corte[0], cy = h->corte[2];
    im->larg = h->larg - h->corte[0] - h->corte[1];
    im->alt = h->alt - h->corte[2] - h->corte[3];
    im->passo_y = h->passo;
    im->passo_c = h->passo_c;
    im->y = q->y + cy * h->passo + cx;
    im->u = q->u + (cy / 2) * h->passo_c + cx / 2;
    im->v = q->v + (cy / 2) * h->passo_c + cx / 2;
    im->erros = h->erros;
    return true;
}
#ifdef H264_HOST
int h264_dbg_mb(int m) { getenv_dbg_mb = m; return 0; }
#endif
