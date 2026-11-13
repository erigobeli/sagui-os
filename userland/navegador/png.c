/* png.c - imagens PNG (H59).
 *
 * Um PNG é a assinatura de 8 bytes e uma sequência de pedaços ("chunks"):
 * tamanho, tipo de 4 letras, dados e CRC. Os que importam:
 *   IHDR  largura, altura, profundidade (bits por amostra), tipo de cor,
 *         entrelaçamento;
 *   PLTE  a paleta (tipo de cor 3);
 *   tRNS  transparência (paleta ou cor transparente);
 *   IDAT  os dados (podem vir em vários pedaços: juntam-se e descompactam-se
 *         com o zlib).
 * Depois de descompactar, cada linha começa com um byte de "filtro": a linha
 * foi guardada como diferença em relação ao pixel da esquerda, ao de cima, à
 * média dos dois ou ao "preditor de Paeth" (o dos três vizinhos mais parecido
 * com a estimativa esquerda + cima - diagonal). Desfazer o filtro devolve os
 * bytes originais, que viram cores conforme o tipo e a profundidade. Imagens
 * entrelaçadas (Adam7) chegam em 7 passadas de pixels espalhados.
 *
 * A saída é sempre 0xAARRGGBB, 4 bytes por pixel. */
#include "navegador.h"

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

typedef struct {
    uint32_t larg, alt;
    int prof, tipo, canais;
    uint32_t paleta[256];
    int npaleta;
    bool tem_chave;
    uint16_t chave[3];            /* cor transparente (tipos 0 e 2) */
} cabecalho_t;

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

/* Desfaz os filtros de uma imagem (ou de uma passada) de l x a pixels. */
static bool desfiltrar(uint8_t *d, uint32_t l, uint32_t a, const cabecalho_t *h, uint32_t *bytes_linha)
{
    uint32_t bpp = (uint32_t)((h->prof * h->canais + 7) / 8);   /* bytes por pixel (mínimo 1) */
    uint32_t linha = (l * (uint32_t)h->prof * (uint32_t)h->canais + 7) / 8;
    *bytes_linha = linha;
    uint8_t *anterior = NULL;
    for (uint32_t y = 0; y < a; y++) {
        uint8_t *f = d + y * (linha + 1);
        uint8_t filtro = f[0];
        uint8_t *x = f + 1;
        for (uint32_t i = 0; i < linha; i++) {
            int esq = i >= bpp ? x[i - bpp] : 0, cima = anterior ? anterior[i] : 0, diag = anterior && i >= bpp ? anterior[i - bpp] : 0;
            switch (filtro) {
            case 0: break;
            case 1: x[i] = (uint8_t)(x[i] + esq); break;
            case 2: x[i] = (uint8_t)(x[i] + cima); break;
            case 3: x[i] = (uint8_t)(x[i] + ((esq + cima) >> 1)); break;
            case 4: x[i] = (uint8_t)(x[i] + paeth(esq, cima, diag)); break;
            default: return false;
            }
        }
        anterior = x;
    }
    return true;
}

/* Lê a amostra número k (de "prof" bits) de uma linha. */
static uint32_t amostra(const uint8_t *linha, uint32_t k, int prof)
{
    if (prof == 8) return linha[k];
    if (prof == 16) return (uint32_t)((linha[2 * k] << 8) | linha[2 * k + 1]);
    uint32_t bit = k * (uint32_t)prof;
    return (uint32_t)(linha[bit / 8] >> (8 - prof - (int)(bit % 8))) & ((1u << prof) - 1);
}

static uint32_t cor_do_pixel(const uint8_t *linha, uint32_t x, const cabecalho_t *h)
{
    int p = h->prof;
    uint32_t maximo = (1u << p) - 1;
    #define ESCALA(v) ((uint32_t)((v) * 255 / maximo))
    uint32_t r, g, b, a = 255;
    switch (h->tipo) {
    case 0: {
        uint32_t v = amostra(linha, x, p);
        r = g = b = ESCALA(v);
        if (h->tem_chave && v == h->chave[0]) a = 0;
        break;
    }
    case 2: {
        uint32_t vr = amostra(linha, x * 3, p), vg = amostra(linha, x * 3 + 1, p), vb = amostra(linha, x * 3 + 2, p);
        r = ESCALA(vr); g = ESCALA(vg); b = ESCALA(vb);
        if (h->tem_chave && vr == h->chave[0] && vg == h->chave[1] && vb == h->chave[2]) a = 0;
        break;
    }
    case 3: {
        uint32_t i = amostra(linha, x, p);
        return i < (uint32_t)h->npaleta ? h->paleta[i] : 0xFF000000u;
    }
    case 4: r = g = b = ESCALA(amostra(linha, x * 2, p)); a = ESCALA(amostra(linha, x * 2 + 1, p)); break;
    default: /* 6 */
        r = ESCALA(amostra(linha, x * 4, p)); g = ESCALA(amostra(linha, x * 4 + 1, p));
        b = ESCALA(amostra(linha, x * 4 + 2, p)); a = ESCALA(amostra(linha, x * 4 + 3, p));
    }
    #undef ESCALA
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Decodifica. Devolve a imagem (alocada com aloca) ou NULL com o motivo em erro. */
imagem_t *png_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro)
{
    static const uint8_t assinatura[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (n < 33 || memcmp(d, assinatura, 8) != 0) { copia_texto(erro, "não é PNG", (size_t)max_erro); return NULL; }
    cabecalho_t h;
    memset(&h, 0, sizeof h);
    int entrelacado = 0;
    uint32_t total_idat = 0;
    for (uint32_t p = 8; p + 12 <= n;) {   /* primeira volta: cabeçalho, paleta e o tamanho dos dados */
        uint32_t t = be32(d + p);
        if (t > n - p - 12) break;
        const uint8_t *tipo = d + p + 4, *c = d + p + 8;
        if (!memcmp(tipo, "IHDR", 4) && t >= 13) {
            h.larg = be32(c); h.alt = be32(c + 4); h.prof = c[8]; h.tipo = c[9]; entrelacado = c[12];
        } else if (!memcmp(tipo, "PLTE", 4)) {
            h.npaleta = (int)(t / 3 > 256 ? 256 : t / 3);
            for (int i = 0; i < h.npaleta; i++) h.paleta[i] = 0xFF000000u | ((uint32_t)c[3 * i] << 16) | ((uint32_t)c[3 * i + 1] << 8) | c[3 * i + 2];
        } else if (!memcmp(tipo, "tRNS", 4)) {
            if (h.tipo == 3) for (uint32_t i = 0; i < t && i < 256; i++) h.paleta[i] = (h.paleta[i] & 0xFFFFFF) | ((uint32_t)c[i] << 24);
            else if (h.tipo == 0 && t >= 2) { h.tem_chave = true; h.chave[0] = (uint16_t)((c[0] << 8) | c[1]); }
            else if (h.tipo == 2 && t >= 6) { h.tem_chave = true; for (int k = 0; k < 3; k++) h.chave[k] = (uint16_t)((c[2 * k] << 8) | c[2 * k + 1]); }
        } else if (!memcmp(tipo, "IDAT", 4)) total_idat += t;
        else if (!memcmp(tipo, "IEND", 4)) break;
        p += t + 12;
    }
    static const int canais[7] = { 1, 0, 3, 1, 2, 0, 4 };
    if (h.tipo > 6 || !canais[h.tipo] || !h.larg || !h.alt || h.larg > 4096 || h.alt > 4096 || !total_idat) {
        copia_texto(erro, "PNG inválido ou grande demais", (size_t)max_erro);
        return NULL;
    }
    h.canais = canais[h.tipo];
    /* Junta os IDAT e descompacta. */
    uint8_t *z = aloca(total_idat);
    uint32_t linha_max = (h.larg * (uint32_t)h.prof * (uint32_t)h.canais + 7) / 8 + 1;
    uint32_t tam_bruto = linha_max * h.alt + (entrelacado ? 7 * h.alt + 64 : 0);
    uint8_t *bruto = aloca(tam_bruto);
    imagem_t *img = aloca(sizeof(imagem_t) + h.larg * h.alt * 4);
    if (!z || !bruto || !img) { copia_texto(erro, "memória insuficiente para a imagem", (size_t)max_erro); return NULL; }
    uint32_t k = 0;
    for (uint32_t p = 8; p + 12 <= n;) {
        uint32_t t = be32(d + p);
        if (t > n - p - 12) break;
        if (!memcmp(d + p + 4, "IDAT", 4)) { memcpy(z + k, d + p + 8, t); k += t; }
        p += t + 12;
    }
    int r = inflar_zlib(z, k, bruto, tam_bruto);
    libera(z);
    if (r < 0) { libera(bruto); libera(img); copia_texto(erro, "dados do PNG corrompidos", (size_t)max_erro); return NULL; }
    img->larg = h.larg;
    img->alt = h.alt;
    img->px = (uint32_t *)(img + 1);
    if (!entrelacado) {
        uint32_t bl;
        if ((uint32_t)r < linha_max * h.alt || !desfiltrar(bruto, h.larg, h.alt, &h, &bl)) {
            libera(bruto); libera(img);
            copia_texto(erro, "PNG truncado", (size_t)max_erro);
            return NULL;
        }
        for (uint32_t y = 0; y < h.alt; y++)
            for (uint32_t x = 0; x < h.larg; x++) img->px[y * h.larg + x] = cor_do_pixel(bruto + y * (bl + 1) + 1, x, &h);
    } else {
        /* Adam7: sete passadas, cada uma uma imagem menor com seus filtros. */
        static const int x0[7] = { 0, 4, 0, 2, 0, 1, 0 }, y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
        static const int dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
        uint32_t pos = 0;
        for (int pa = 0; pa < 7; pa++) {
            uint32_t l = (h.larg - (uint32_t)x0[pa] + (uint32_t)dx[pa] - 1) / (uint32_t)dx[pa];
            uint32_t a = (h.alt - (uint32_t)y0[pa] + (uint32_t)dy[pa] - 1) / (uint32_t)dy[pa];
            if (h.larg <= (uint32_t)x0[pa] || h.alt <= (uint32_t)y0[pa]) continue;
            uint32_t bl;
            if (!desfiltrar(bruto + pos, l, a, &h, &bl)) { libera(bruto); libera(img); copia_texto(erro, "PNG truncado", (size_t)max_erro); return NULL; }
            for (uint32_t y = 0; y < a; y++)
                for (uint32_t x = 0; x < l; x++)
                    img->px[((uint32_t)y0[pa] + y * (uint32_t)dy[pa]) * h.larg + (uint32_t)x0[pa] + x * (uint32_t)dx[pa]] =
                        cor_do_pixel(bruto + pos + y * (bl + 1) + 1, x, &h);
            pos += a * (bl + 1);
        }
    }
    libera(bruto);
    return img;
}
