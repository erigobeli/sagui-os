/* jpeg.c - imagens JPEG, sequenciais e progressivas (H60).
 *
 * Um JPEG guarda a imagem em blocos de 8x8 pixels por componente (o brilho Y
 * e as duas cores Cb e Cr, em geral com as cores em resolução menor). Cada
 * bloco não guarda pixels, e sim 64 "coeficientes" da transformada do
 * cosseno: o primeiro (DC) é a média do bloco, os outros (AC) as variações
 * cada vez mais finas. Os coeficientes são divididos por uma tabela de
 * quantização (é aí que se perde qualidade e se ganha tamanho), postos em
 * ordem de zigue-zague (os mais importantes primeiro) e codificados por
 * Huffman, com o DC guardado como diferença em relação ao bloco anterior.
 *
 * - Sequencial: cada bloco vem inteiro, em ordem.
 * - Progressivo: a imagem vem em várias passadas; primeiro só os DC (uma
 *   imagem borrada), depois faixas de AC, e passadas de "refinamento" que
 *   mandam mais um bit de precisão dos coeficientes já vistos.
 * Este decodificador guarda os coeficientes de todos os blocos e só no fim
 * faz a transformada inversa; assim os dois tipos usam o mesmo caminho.
 *
 * A transformada inversa é feita em inteiros (tabela de cossenos com 12
 * bits de fração), primeiro nas linhas e depois nas colunas. A conversão de
 * YCbCr para RGB também é em inteiros. Cores em resolução menor são
 * esticadas com interpolação linear entre as amostras vizinhas. */
#include "navegador.h"

typedef struct {
    uint8_t  tamanhos[17];        /* quantos códigos de cada tamanho (1 a 16) */
    uint8_t  simbolos[256];
    int      maximo[18];          /* maior código de cada tamanho (-1 = nenhum) */
    int      deslocamento[17];
    bool     definida;
} huff_t;

typedef struct {
    int      id, h, v, tq;
    int      td, ta;              /* tabelas de Huffman DC e AC da passada atual */
    int      blocos_l, blocos_a;  /* blocos na largura e na altura (completando o MCU) */
    int16_t *coef;                /* 64 por bloco */
    int      dc_anterior;
} comp_t;

typedef struct {
    const uint8_t *d;
    uint32_t n, pos;
    uint32_t bits;
    int      qtd;
    bool     marcador;            /* achou um marcador no meio dos dados */
    uint16_t quant[4][64];
    huff_t   dc[4], ac[4];
    comp_t   c[4];
    int      ncomp, larg, alt, hmax, vmax, mcu_l, mcu_a;
    bool     progressivo;
    int      intervalo;           /* reinício a cada tantos MCUs (0 = nunca) */
    int      eobrun;
    bool     adobe_rgb;
} jpeg_t;

static const uint8_t zigue[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61,
    54, 47, 55, 62, 63,
};

/* ------------------------------------------------------------ bits */

static int bit(jpeg_t *j)
{
    if (j->qtd == 0) {
        if (j->marcador || j->pos >= j->n) return 0;   /* depois do fim: zeros (a especificação manda assim) */
        uint8_t b = j->d[j->pos++];
        if (b == 0xFF) {
            uint8_t seguinte = j->pos < j->n ? j->d[j->pos] : 0;
            if (seguinte == 0x00) j->pos++;            /* FF 00 = o byte FF dentro dos dados */
            else { j->marcador = true; j->pos--; return 0; }
        }
        j->bits = b;
        j->qtd = 8;
    }
    j->qtd--;
    return (int)((j->bits >> j->qtd) & 1);
}

static int bits(jpeg_t *j, int n)
{
    int v = 0;
    while (n--) v = (v << 1) | bit(j);
    return v;
}

/* Um valor de "categoria" s: s bits, e se o primeiro for 0 o número é negativo. */
static int estender(int v, int s) { return s && v < (1 << (s - 1)) ? v - (1 << s) + 1 : v; }

static void montar_huff(huff_t *h)
{
    int codigo = 0, k = 0;
    for (int t = 1; t <= 16; t++) {
        h->deslocamento[t] = k - codigo;
        codigo += h->tamanhos[t];
        k += h->tamanhos[t];
        h->maximo[t] = h->tamanhos[t] ? codigo - 1 : -1;
        codigo <<= 1;
    }
    h->maximo[17] = 0x7FFFFFFF;
    h->definida = true;
}

static int decodificar(jpeg_t *j, const huff_t *h)
{
    int codigo = 0;
    for (int t = 1; t <= 16; t++) {
        codigo = (codigo << 1) | bit(j);
        if (codigo <= h->maximo[t]) return h->simbolos[codigo + h->deslocamento[t]];
    }
    return -1;
}

/* ------------------------------------------------------------ passadas */

static int16_t *bloco(comp_t *c, int bx, int by) { return c->coef + ((size_t)by * (size_t)c->blocos_l + (size_t)bx) * 64; }

static bool decodificar_bloco(jpeg_t *j, comp_t *c, int16_t *b, int ss, int se, int ah, int al)
{
    if (!j->progressivo) {
        int s = decodificar(j, &j->dc[c->td]);
        if (s < 0) return false;
        c->dc_anterior += estender(bits(j, s), s);
        b[0] = (int16_t)c->dc_anterior;
        for (int k = 1; k < 64;) {
            int rs = decodificar(j, &j->ac[c->ta]);
            if (rs < 0) return false;
            int r = rs >> 4, t = rs & 15;
            if (t == 0) { if (r == 15) { k += 16; continue; } break; }
            k += r;
            if (k > 63) return false;
            b[zigue[k++]] = (int16_t)estender(bits(j, t), t);
        }
        return true;
    }
    if (ss == 0) {                                     /* DC */
        if (ah == 0) {
            int s = decodificar(j, &j->dc[c->td]);
            if (s < 0) return false;
            c->dc_anterior += estender(bits(j, s), s);
            b[0] = (int16_t)(c->dc_anterior * (1 << al));
        } else if (bit(j)) b[0] |= (int16_t)(1 << al);
        return true;
    }
    if (ah == 0) {                                     /* AC, primeira vez */
        if (j->eobrun > 0) { j->eobrun--; return true; }
        for (int k = ss; k <= se;) {
            int rs = decodificar(j, &j->ac[c->ta]);
            if (rs < 0) return false;
            int r = rs >> 4, t = rs & 15;
            if (t == 0) {
                if (r < 15) { j->eobrun = (1 << r) - 1; if (r) j->eobrun += bits(j, r); break; }
                k += 16;
                continue;
            }
            k += r;
            if (k > 63) return false;
            b[zigue[k++]] = (int16_t)(estender(bits(j, t), t) * (1 << al));
        }
        return true;
    }
    /* AC, refinamento: os coeficientes já não-nulos ganham um bit de
     * correção; os novos aparecem com valor ±(1 << al), depois de pular
     * "r" coeficientes ainda nulos. */
    int p1 = 1 << al, m1 = -1 * (1 << al);
    int k = ss;
    if (j->eobrun <= 0) {
        for (; k <= se;) {
            int rs = decodificar(j, &j->ac[c->ta]);
            if (rs < 0) return false;
            int r = rs >> 4, t = rs & 15, novo = 0;
            if (t == 0) {
                if (r < 15) { j->eobrun = (1 << r); if (r) j->eobrun += bits(j, r); break; }
            } else {
                novo = bit(j) ? p1 : m1;
            }
            while (k <= se) {
                int16_t *coef = &b[zigue[k]];
                if (*coef != 0) {
                    if (bit(j) && (*coef & p1) == 0) *coef = (int16_t)(*coef >= 0 ? *coef + p1 : *coef + m1);
                } else {
                    if (r == 0) { if (novo) *coef = (int16_t)novo; k++; break; }
                    r--;
                }
                k++;
            }
        }
    }
    if (j->eobrun > 0) {
        for (; k <= se; k++) {
            int16_t *coef = &b[zigue[k]];
            if (*coef != 0 && bit(j) && (*coef & p1) == 0) *coef = (int16_t)(*coef >= 0 ? *coef + p1 : *coef + m1);
        }
        j->eobrun--;
    }
    return true;
}

/* Recomeço depois de um marcador RSTn: zera as previsões do DC. */
static void reiniciar_passada(jpeg_t *j)
{
    j->qtd = 0;
    j->marcador = false;
    if (j->pos + 1 < j->n && j->d[j->pos] == 0xFF && j->d[j->pos + 1] >= 0xD0 && j->d[j->pos + 1] <= 0xD7) j->pos += 2;
    for (int i = 0; i < j->ncomp; i++) j->c[i].dc_anterior = 0;
    j->eobrun = 0;
}

static bool passada(jpeg_t *j, const uint8_t *s, uint32_t tam)
{
    int n = s[0];
    if (n < 1 || n > 4 || tam < (uint32_t)(1 + 2 * n + 3)) return false;
    comp_t *usados[4];
    for (int i = 0; i < n; i++) {
        usados[i] = NULL;
        for (int k = 0; k < j->ncomp; k++)
            if (j->c[k].id == s[1 + 2 * i]) usados[i] = &j->c[k];
        if (!usados[i]) return false;
        usados[i]->td = s[2 + 2 * i] >> 4;
        usados[i]->ta = s[2 + 2 * i] & 15;
        if (usados[i]->td > 3 || usados[i]->ta > 3) return false;
    }
    int ss = s[1 + 2 * n], se = s[2 + 2 * n], ah = s[3 + 2 * n] >> 4, al = s[3 + 2 * n] & 15;
    if (!j->progressivo) { ss = 0; se = 63; ah = al = 0; }
    for (int i = 0; i < j->ncomp; i++) j->c[i].dc_anterior = 0;
    j->eobrun = 0;
    j->qtd = 0;
    j->marcador = false;
    int contador = 0;
    if (n == 1) {
        /* Uma componente só: os blocos vêm na ordem da imagem dela, sem MCU. */
        comp_t *c = usados[0];
        int bl = (j->larg * c->h / j->hmax + 7) / 8, ba = (j->alt * c->v / j->vmax + 7) / 8;
        for (int by = 0; by < ba; by++)
            for (int bx = 0; bx < bl; bx++) {
                if (j->intervalo && contador == j->intervalo) { reiniciar_passada(j); contador = 0; }
                if (!decodificar_bloco(j, c, bloco(c, bx, by), ss, se, ah, al)) return false;
                contador++;
            }
    } else {
        for (int my = 0; my < j->mcu_a; my++)
            for (int mx = 0; mx < j->mcu_l; mx++) {
                if (j->intervalo && contador == j->intervalo) { reiniciar_passada(j); contador = 0; }
                for (int i = 0; i < n; i++) {
                    comp_t *c = usados[i];
                    for (int v = 0; v < c->v; v++)
                        for (int h = 0; h < c->h; h++)
                            if (!decodificar_bloco(j, c, bloco(c, mx * c->h + h, my * c->v + v), ss, se, ah, al)) return false;
                }
                contador++;
            }
    }
    return true;
}

/* ------------------------------------------------------------ transformada inversa */

/* cos(k * pi / 16) com 12 bits de fração, para k = 0 a 8. */
static const int cos16[9] = { 4096, 4017, 3784, 3406, 2896, 2276, 1567, 799, 0 };

static int cosseno_k(int k)
{
    k %= 32;
    if (k > 16) k = 32 - k;               /* cos é par em torno de 2pi */
    return k <= 8 ? cos16[k] : -cos16[16 - k];
}

static void idct(const int16_t *entrada, const uint16_t *q, uint8_t *saida, int passo)
{
    static int tabela[8][8];              /* C(u) * cos((2x+1) u pi / 16), 12 bits de fração */
    static bool pronta;
    if (!pronta) {
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++) {
                int v = cosseno_k((2 * x + 1) * u);
                tabela[x][u] = u == 0 ? v * 2896 / 4096 : v;   /* C(0) = 1/raiz(2) */
            }
        pronta = true;
    }
    int f[64], tmp[64];
    for (int i = 0; i < 64; i++) f[i] = entrada[i] * q[i];
    for (int y = 0; y < 8; y++)          /* linhas */
        for (int x = 0; x < 8; x++) {
            int s = 0;
            for (int u = 0; u < 8; u++) s += tabela[x][u] * f[y * 8 + u];
            tmp[y * 8 + x] = s >> 8;      /* fica com 4 bits de fração */
        }
    for (int x = 0; x < 8; x++)          /* colunas */
        for (int y = 0; y < 8; y++) {
            int s = 0;
            for (int v = 0; v < 8; v++) s += tabela[y][v] * tmp[v * 8 + x];
            /* 12 + 4 bits de fração, mais o 1/4 da fórmula (2 bits), mais 128 de deslocamento */
            int p = (s + (1 << 17)) >> 18;
            p += 128;
            saida[y * passo + x] = (uint8_t)(p < 0 ? 0 : p > 255 ? 255 : p);
        }
}

/* ------------------------------------------------------------ cores em resolução menor */

/* Um ponto de um plano em resolução menor (fator h/hmax e v/vmax), por
 * interpolação linear entre as quatro amostras vizinhas. As amostras ficam
 * no centro dos pixels que cobrem: com fator 1/2, o pixel x da imagem cai na
 * posição (x + 0,5) / 2 - 0,5 do plano. Contas em 1/256. */
static int amostra_esticada(const uint8_t *pl, int pl_l, int pl_a, int x, int y, int h, int hmax, int v, int vmax)
{
    if (h == hmax && v == vmax) return pl[y * pl_l + x];
    int fx = ((2 * x + 1) * h * 256 / hmax - 256) / 2, fy = ((2 * y + 1) * v * 256 / vmax - 256) / 2;
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    int x0 = fx >> 8, y0 = fy >> 8, ax = fx & 255, ay = fy & 255;
    int x1 = x0 + 1 < pl_l ? x0 + 1 : x0, y1 = y0 + 1 < pl_a ? y0 + 1 : y0;
    int cima = pl[y0 * pl_l + x0] * (256 - ax) + pl[y0 * pl_l + x1] * ax;
    int baixo = pl[y1 * pl_l + x0] * (256 - ax) + pl[y1 * pl_l + x1] * ax;
    return (cima * (256 - ay) + baixo * ay + 32768) >> 16;
}

/* ------------------------------------------------------------ leitura */

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

imagem_t *jpeg_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro)
{
    jpeg_t *j = aloca_zerado(sizeof(jpeg_t));
    imagem_t *img = NULL;
    uint8_t *planos[4] = { 0, 0, 0, 0 };
    if (!j) { copia_texto(erro, "memória insuficiente", (size_t)max_erro); return NULL; }
    #define FALHA(t) do { copia_texto(erro, t, (size_t)max_erro); goto fim; } while (0)
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) FALHA("não é JPEG");
    j->d = d;
    j->n = n;
    bool quadro = false;
    for (uint32_t p = 2; p + 4 <= n;) {
        if (d[p] != 0xFF) { p++; continue; }
        uint8_t m = d[p + 1];
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) { p += m == 0xFF ? 1 : 2; continue; }
        if (m == 0xD9) break;
        uint32_t tam = be16(d + p + 2);
        if (tam < 2 || p + 2 + tam > n) FALHA("JPEG truncado");
        const uint8_t *s = d + p + 4;
        uint32_t ts = tam - 2;
        if (m == 0xDB) {                                     /* tabelas de quantização */
            for (uint32_t k = 0; k < ts;) {
                int precisao = s[k] >> 4, id = s[k] & 3;
                k++;
                for (int i = 0; i < 64 && k < ts; i++) {
                    uint16_t v = precisao ? be16(s + k) : s[k];
                    k += precisao ? 2 : 1;
                    j->quant[id][zigue[i]] = v;
                }
            }
        } else if (m == 0xC4) {                              /* tabelas de Huffman */
            for (uint32_t k = 0; k + 17 <= ts;) {
                int classe = s[k] >> 4, id = s[k] & 3;
                huff_t *h = classe ? &j->ac[id] : &j->dc[id];
                int total = 0;
                for (int t = 1; t <= 16; t++) { h->tamanhos[t] = s[k + (uint32_t)t]; total += s[k + (uint32_t)t]; }
                if (total > 256 || k + 17 + (uint32_t)total > ts) FALHA("tabela de Huffman inválida");
                memcpy(h->simbolos, s + k + 17, (size_t)total);
                montar_huff(h);
                k += 17 + (uint32_t)total;
            }
        } else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {    /* início do quadro */
            j->progressivo = m == 0xC2;
            if (s[0] != 8) FALHA("JPEG de 12 bits não é suportado");
            j->alt = be16(s + 1);
            j->larg = be16(s + 3);
            j->ncomp = s[5];
            if (!j->larg || !j->alt || j->larg > 4096 || j->alt > 4096 || (j->ncomp != 1 && j->ncomp != 3)) FALHA("JPEG grande demais ou com cores não suportadas (CMYK)");
            j->hmax = j->vmax = 1;
            for (int i = 0; i < j->ncomp; i++) {
                comp_t *c = &j->c[i];
                c->id = s[6 + 3 * i];
                c->h = s[7 + 3 * i] >> 4;
                c->v = s[7 + 3 * i] & 15;
                c->tq = s[8 + 3 * i] & 3;
                if (c->h < 1 || c->v < 1 || c->h > 4 || c->v > 4) FALHA("amostragem inválida");
                if (c->h > j->hmax) j->hmax = c->h;
                if (c->v > j->vmax) j->vmax = c->v;
            }
            j->mcu_l = (j->larg + 8 * j->hmax - 1) / (8 * j->hmax);
            j->mcu_a = (j->alt + 8 * j->vmax - 1) / (8 * j->vmax);
            for (int i = 0; i < j->ncomp; i++) {
                comp_t *c = &j->c[i];
                c->blocos_l = j->mcu_l * c->h;
                c->blocos_a = j->mcu_a * c->v;
                c->coef = aloca_zerado((size_t)c->blocos_l * (size_t)c->blocos_a * 64 * sizeof(int16_t));
                if (!c->coef) FALHA("memória insuficiente para o JPEG");
            }
            quadro = true;
        } else if (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            FALHA("tipo de JPEG não suportado (sem perdas ou aritmético)");
        } else if (m == 0xDD) {
            j->intervalo = be16(s);
        } else if (m == 0xEE && ts >= 12 && !memcmp(s, "Adobe", 5)) {
            j->adobe_rgb = s[11] == 0;                       /* transformação 0: as cores já são RGB */
        } else if (m == 0xDA) {                              /* começo de uma passada */
            if (!quadro) FALHA("JPEG sem cabeçalho de quadro");
            j->pos = p + 2 + tam;
            if (!passada(j, s, ts)) FALHA("dados do JPEG corrompidos");
            /* Procura o próximo marcador depois dos dados comprimidos. */
            p = j->pos;
            while (p + 1 < n && !(d[p] == 0xFF && d[p + 1] != 0x00 && !(d[p + 1] >= 0xD0 && d[p + 1] <= 0xD7))) p++;
            continue;
        }
        p += 2 + tam;
    }
    if (!quadro) FALHA("JPEG sem imagem");
    /* Transformada inversa de todos os blocos, em planos por componente. */
    for (int i = 0; i < j->ncomp; i++) {
        comp_t *c = &j->c[i];
        int pl = c->blocos_l * 8;
        planos[i] = aloca((size_t)pl * (size_t)c->blocos_a * 8);
        if (!planos[i]) FALHA("memória insuficiente para o JPEG");
        for (int by = 0; by < c->blocos_a; by++)
            for (int bx = 0; bx < c->blocos_l; bx++)
                idct(bloco(c, bx, by), j->quant[c->tq], planos[i] + (by * 8) * pl + bx * 8, pl);
    }
    img = aloca(sizeof(imagem_t) + (size_t)j->larg * (size_t)j->alt * 4);
    if (!img) FALHA("memória insuficiente para a imagem");
    img->larg = (uint32_t)j->larg;
    img->alt = (uint32_t)j->alt;
    img->px = (uint32_t *)(img + 1);
    for (int y = 0; y < j->alt; y++)
        for (int x = 0; x < j->larg; x++) {
            int v[3];
            for (int i = 0; i < j->ncomp; i++) {
                comp_t *c = &j->c[i];
                v[i] = amostra_esticada(planos[i], c->blocos_l * 8, c->blocos_a * 8, x, y, c->h, j->hmax, c->v, j->vmax);
            }
            int r, g, b;
            if (j->ncomp == 1) r = g = b = v[0];
            else if (j->adobe_rgb) { r = v[0]; g = v[1]; b = v[2]; }
            else {
                /* YCbCr -> RGB (coeficientes da norma, com 16 bits de fração). */
                int cb = v[1] - 128, cr = v[2] - 128;
                r = v[0] + ((91881 * cr + 32768) >> 16);
                g = v[0] - ((22554 * cb + 46802 * cr + 32768) >> 16);
                b = v[0] + ((116130 * cb + 32768) >> 16);
            }
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
            img->px[y * j->larg + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
fim:
    for (int i = 0; i < 4; i++) { if (planos[i]) libera(planos[i]); if (j->c[i].coef) libera(j->c[i].coef); }
    libera(j);
    return img;
    #undef FALHA
}

imagem_t *imagem_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro)
{
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P') return png_ler(d, n, erro, max_erro);
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return jpeg_ler(d, n, erro, max_erro);
    copia_texto(erro, "formato de imagem não suportado (só PNG e JPEG)", (size_t)max_erro);
    return NULL;
}
