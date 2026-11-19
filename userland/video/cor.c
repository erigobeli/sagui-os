/* cor.c - leva um quadro YUV 4:2:0 para a janela: escala e conversão de cor (D071).
 *
 * O H.264 entrega a imagem em Y (brilho), U e V (cor, metade da resolução
 * nos dois sentidos); a tela quer 0x00RRGGBB. A conta é a da Recomendação
 * ITU-R BT.601 com faixa limitada (Y de 16 a 235, U e V de 16 a 240), que é
 * o padrão dos vídeos que não dizem outra coisa:
 *
 *   Kr = 0,299, Kb = 0,114, Kg = 1 - Kr - Kb = 0,587
 *   R = 1,164 (Y-16)                    + 1,596 (V-128)
 *   G = 1,164 (Y-16) - 0,391 (U-128)    - 0,813 (V-128)
 *   B = 1,164 (Y-16) + 2,018 (U-128)
 *
 * onde 1,164 = 255/219, 1,596 = 2 (1-Kr) 255/224, 2,018 = 2 (1-Kb) 255/224,
 * 0,391 = 2 (1-Kb) (Kb/Kg) 255/224 e 0,813 = 2 (1-Kr) (Kr/Kg) 255/224.
 *
 * Por que um arquivo só para isso: num vídeo de 1080p mostrado numa janela
 * de 1020x574 são 585 mil pontos por quadro, 30 vezes por segundo. A versão
 * antiga fazia três consultas a tabelas e três cortes (if) por ponto. Aqui:
 *   1. a escala é por "vizinho mais próximo", com as colunas de origem
 *      calculadas uma vez por tamanho (mapa_x), e uma linha de saída que cai
 *      na mesma linha de origem da anterior é só copiada;
 *   2. cada linha é "juntada" em três vetores de 16 bits já centrados
 *      (Y-16, U-128, V-128, vezes 128) e convertida de 8 em 8 pontos com
 *      SSE2 (que os programas podem usar desde a D042): pmulhw faz as oito
 *      multiplicações de uma vez e packuswb corta em 0..255 sem nenhum if.
 *
 * Contas em ponto fixo: com Ys = (Y-16)*128 e pmulhw(a, b) = (a*b) >> 16,
 * pmulhw(Ys, 1,164*16384) = 32 * 1,164 (Y-16). Todos os termos ficam
 * multiplicados por 32 (5 bits de fração) e o fim é (soma + 16) >> 5. O
 * 2,018*16384 não cabe em 16 bits com sinal, então o U do azul vai com o
 * dobro do tamanho ((U-128)*256) e a constante pela metade (1,009*16384).
 * Os maiores valores no caminho (|32*1,164*239 + 32*1,596*128| < 15500)
 * cabem em 16 bits. */
#include "sagui.h"
#include "h264.h"
#include "cor.h"

typedef short v8hi __attribute__((vector_size(16)));
typedef char v16qi __attribute__((vector_size(16)));

#define K_Y   19071    /* 1,164 * 16384 */
#define K_RV  26149    /* 1,596 * 16384 */
#define K_GU   6406    /* 0,391 * 16384 */
#define K_GV  13320    /* 0,813 * 16384 */
#define K_BU  16531    /* 2,018 * 16384 / 2 */

static int16_t tab_y[256], tab_c[256];
/* A linha de saída mais larga: a tela do G470 tem 1366 pontos. */
#define LMAX 2048
static int16_t lin_y[LMAX + 8] __attribute__((aligned(16)));
static int16_t lin_u[LMAX + 8] __attribute__((aligned(16)));
static int16_t lin_v[LMAX + 8] __attribute__((aligned(16)));
static uint16_t mapa_x[LMAX], mapa_c[LMAX];
static int mapa_l, mapa_ol;

void cor_iniciar(void)
{
    for (int i = 0; i < 256; i++) {
        tab_y[i] = (int16_t)((i - 16) * 128);
        tab_c[i] = (int16_t)((i - 128) * 128);
    }
}

static inline v8hi constante(short k) { return (v8hi){ k, k, k, k, k, k, k, k }; }

/* Converte n pontos (n múltiplo de 8) das linhas juntadas para XRGB em d. */
static void converter(uint32_t *d, const int16_t *ly, const int16_t *lu, const int16_t *lv, int n)
{
    const v8hi ky = constante(K_Y), krv = constante(K_RV), kgu = constante(K_GU), kgv = constante(K_GV);
    const v8hi kbu = constante(K_BU), arred = constante(16);
    const v16qi zero = { 0 };
    for (int x = 0; x < n; x += 8) {
        v8hi ys = *(const v8hi *)(ly + x), us = *(const v8hi *)(lu + x), vs = *(const v8hi *)(lv + x);
        v8hi yy = __builtin_ia32_pmulhw128(ys, ky) + arred;
        v8hi r = (yy + __builtin_ia32_pmulhw128(vs, krv)) >> 5;
        v8hi g = (yy - __builtin_ia32_pmulhw128(us, kgu) - __builtin_ia32_pmulhw128(vs, kgv)) >> 5;
        v8hi b = (yy + __builtin_ia32_pmulhw128(us + us, kbu)) >> 5;
        /* corta em 0..255 e intercala B, G, R, 0 (a ordem dos bytes de 0x00RRGGBB) */
        v16qi b8 = __builtin_ia32_packuswb128(b, b), g8 = __builtin_ia32_packuswb128(g, g), r8 = __builtin_ia32_packuswb128(r, r);
        v8hi bg = (v8hi)__builtin_ia32_punpcklbw128(b8, g8), r0 = (v8hi)__builtin_ia32_punpcklbw128(r8, zero);
        __builtin_ia32_storedqu((char *)(d + x), (v16qi)__builtin_ia32_punpcklwd128(bg, r0));
        __builtin_ia32_storedqu((char *)(d + x + 4), (v16qi)__builtin_ia32_punpckhwd128(bg, r0));
    }
}

void cor_desenhar(uint32_t *px, int passo, int ox, int oy, int ol, int oa, const h264_imagem_t *im)
{
    if (ol <= 0 || oa <= 0) return;
    if (ol > LMAX) ol = LMAX;
    if (mapa_l != im->larg || mapa_ol != ol) {
        /* coluna de origem do centro de cada ponto de saída */
        for (int x = 0; x < ol; x++) {
            int sx = (2 * x + 1) * im->larg / (2 * ol);
            mapa_x[x] = (uint16_t)sx;
            mapa_c[x] = (uint16_t)(sx / 2);
        }
        mapa_l = im->larg;
        mapa_ol = ol;
    }
    int sy_ant = -1;
    uint32_t *ant = NULL;
    for (int y = 0; y < oa; y++) {
        int sy = (2 * y + 1) * im->alt / (2 * oa);
        uint32_t *d = px + (oy + y) * passo + ox;
        if (sy == sy_ant) { memcpy(d, ant, (size_t)ol * 4); continue; }
        const uint8_t *py = im->y + sy * im->passo_y;
        const uint8_t *pu = im->u + (sy / 2) * im->passo_c, *pv = im->v + (sy / 2) * im->passo_c;
        for (int x = 0; x < ol; x++) {
            lin_y[x] = tab_y[py[mapa_x[x]]];
            lin_u[x] = tab_c[pu[mapa_c[x]]];
            lin_v[x] = tab_c[pv[mapa_c[x]]];
        }
        int x0 = ol & ~7;
        converter(d, lin_y, lin_u, lin_v, x0);
        if (x0 < ol) {
            /* o último grupo de 8 passaria do fim da linha: converte num rascunho */
            uint32_t resto[8];
            converter(resto, lin_y + x0, lin_u + x0, lin_v + x0, 8);
            memcpy(d + x0, resto, (size_t)(ol - x0) * 4);
        }
        sy_ant = sy;
        ant = d;
    }
}
