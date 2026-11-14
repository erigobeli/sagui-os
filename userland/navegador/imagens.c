/* imagens.c - a porta única das imagens do Cipó: PNG e JPEG (png.c, jpeg.c,
 * da 2.0, inclusive JPEG progressivo), GIF (aqui, do pull request #6 de Robson
 * de Lima Barbosa) e WebP com perdas (terceiros/libwebp-vp8). Os pixels saem
 * em 0xAARRGGBB, num bloco de aloca que o motor libera.
 *
 * GIF: especificação GIF89a (CompuServe, 1990): tela lógica, paleta global ou
 * local, extensão de controle gráfico (cor transparente), LZW de tamanho
 * variável e entrelaçamento em 4 passadas. Só o primeiro quadro. */
#include "navegador.h"
#include "../../terceiros/libwebp-vp8/vp8.h"

#define MAX_PIXELS (4096u * 1024u)

static bool gif(const uint8_t *d, unsigned n, uint32_t **saida, int *pl, int *pa)
{
    static uint16_t prefixo[4096];
    static uint8_t sufixo[4096], pilha[4097], pal[768];
    if (n < 13) return false;
    int l = d[6] | d[7] << 8, a = d[8] | d[9] << 8, transp = -1;
    if (!l || !a || (unsigned)l * (unsigned)a > MAX_PIXELS) return false;
    unsigned k = 13;
    if (d[10] & 0x80) { unsigned t = 3u << ((d[10] & 7) + 1); if (k + t > n) return false; memcpy(pal, d + k, t); k += t; }
    while (k < n && d[k] != 0x2C) {
        if (d[k] != 0x21 || k + 2 > n) return false;
        if (d[k + 1] == 0xF9 && k + 7 < n && (d[k + 3] & 1)) transp = d[k + 6];
        k += 2;
        while (k < n && d[k]) k += 1u + d[k];
        k++;
    }
    if (k + 11 > n) return false;
    int fx = d[k + 1] | d[k + 2] << 8, fy = d[k + 3] | d[k + 4] << 8, fl = d[k + 5] | d[k + 6] << 8, fa = d[k + 7] | d[k + 8] << 8;
    int flags = d[k + 9];
    k += 10;
    if (flags & 0x80) { unsigned t = 3u << ((flags & 7) + 1); if (k + t > n) return false; memcpy(pal, d + k, t); k += t; }
    if (k >= n || fx + fl > l || fy + fa > a) return false;
    int minimo = d[k++];
    if (minimo < 2 || minimo > 8) return false;
    uint32_t *px = aloca((size_t)l * (size_t)a * 4);
    if (!px) return false;
    memset(px, 0, (size_t)l * (size_t)a * 4);   /* fora do quadro: transparente */
    int limpa = 1 << minimo, fim_c = limpa + 1, tam = minimo + 1, prox = limpa + 2, ant = -1, cod_bits = 0, passo = 0;
    uint32_t acc = 0;
    int x = 0, y = 0, resta = 0;
    uint8_t primeiro = 0;
    for (;;) {
        while (cod_bits < tam) {
            if (!resta) { if (k >= n || !d[k]) goto fim; resta = d[k++]; }
            if (k >= n) goto fim;
            acc |= (uint32_t)d[k++] << cod_bits;
            cod_bits += 8;
            resta--;
        }
        int c = (int)(acc & ((1u << tam) - 1));
        acc >>= tam; cod_bits -= tam;
        if (c == limpa) { tam = minimo + 1; prox = limpa + 2; ant = -1; continue; }
        if (c == fim_c) break;
        int t = 0, v = c;
        if (ant < 0) { if (c >= limpa) goto fim; primeiro = (uint8_t)c; pilha[t++] = primeiro; }
        else {
            if (c > prox || (c == prox && prox >= 4096)) goto fim;
            if (c == prox) { pilha[t++] = primeiro; v = ant; }
            while (v >= limpa && t < 4096) { pilha[t++] = sufixo[v]; v = prefixo[v]; }
            primeiro = (uint8_t)v;
            pilha[t++] = primeiro;
            if (prox < 4096) { prefixo[prox] = (uint16_t)ant; sufixo[prox] = primeiro; prox++; if (prox == 1 << tam && tam < 12) tam++; }
        }
        ant = c;
        while (t) {
            int i = pilha[--t];
            if (y < fa) {
                if (i != transp) px[(fy + y) * l + fx + x] = 0xFF000000u | (uint32_t)pal[i * 3] << 16 | (uint32_t)pal[i * 3 + 1] << 8 | pal[i * 3 + 2];
                if (++x == fl) {
                    x = 0;
                    if (flags & 0x40) {   /* entrelaçado: 4 passadas */
                        static const uint8_t ini[4] = { 0, 4, 2, 1 }, sal[4] = { 8, 8, 4, 2 };
                        y += sal[passo];
                        while (y >= fa && passo < 3) { passo++; y = ini[passo]; }
                    } else y++;
                }
            }
        }
    }
fim:
    *saida = px; *pl = l; *pa = a;
    return true;
}

bool imagem_decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a)
{
    if (n > 6 && !memcmp(d, "GIF8", 4)) return gif(d, n, px, l, a);
    if (n > 16 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return webp_ler(d, n, px, l, a);
    char erro[128];
    imagem_t *im = imagem_ler(d, n, erro, sizeof erro);
    if (!im) return false;
    /* a imagem_t da 2.0 tem os pixels no mesmo bloco: o motor quer só os pixels */
    uint32_t *p = aloca((size_t)im->larg * im->alt * 4);
    if (!p) { libera(im); return false; }
    memcpy(p, im->px, (size_t)im->larg * im->alt * 4);
    *px = p; *l = (int)im->larg; *a = (int)im->alt;
    libera(im);
    return true;
}
