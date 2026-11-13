/* descompactar.c - DEFLATE, zlib e gzip (H59).
 *
 * O DEFLATE (RFC 1951) guarda os dados em blocos. Cada bloco é:
 *   - guardado (tipo 0): os bytes como estão;
 *   - com códigos fixos (tipo 1) ou próprios (tipo 2): uma sequência de
 *     símbolos codificados por Huffman, em que cada símbolo é um byte literal
 *     (0 a 255), o fim do bloco (256) ou um "tamanho" (257 a 285) seguido de
 *     uma "distância": copie tantos bytes que estão tantas posições atrás.
 * Os bits vêm do byte menos significativo para o mais; os códigos de
 * Huffman são "canônicos": basta saber quantos símbolos têm cada tamanho de
 * código para reconstruir todos os códigos, em ordem. A decodificação aqui
 * lê um bit por vez e compara com o primeiro código de cada tamanho, o que é
 * simples e rápido o bastante para páginas e imagens da web.
 *
 * zlib (RFC 1950) é o DEFLATE com 2 bytes na frente e a soma Adler-32 no fim
 * (é o que o PNG usa); gzip (RFC 1952) tem um cabeçalho maior e CRC-32. */
#include "navegador.h"

typedef struct {
    const uint8_t *d;
    uint32_t n, pos;
    uint32_t bits, qtd;          /* bits guardados, ainda não usados */
    uint8_t *saida;
    uint32_t max, feito;
    bool erro;
} leitor_t;

typedef struct {
    uint16_t quantos[16];        /* quantos códigos de cada tamanho */
    uint16_t simbolos[320];      /* símbolos em ordem de código */
} huffman_t;

static uint32_t bits(leitor_t *l, int n)
{
    while (l->qtd < (uint32_t)n) {
        if (l->pos >= l->n) { l->erro = true; return 0; }
        l->bits |= (uint32_t)l->d[l->pos++] << l->qtd;
        l->qtd += 8;
    }
    uint32_t v = l->bits & ((1u << n) - 1);
    l->bits >>= n;
    l->qtd -= (uint32_t)n;
    return v;
}

static void montar_huffman(huffman_t *h, const uint8_t *tamanhos, int n)
{
    uint16_t inicio[16];
    memset(h->quantos, 0, sizeof h->quantos);
    for (int i = 0; i < n; i++) h->quantos[tamanhos[i]]++;
    h->quantos[0] = 0;
    inicio[1] = 0;
    for (int t = 1; t < 15; t++) inicio[t + 1] = (uint16_t)(inicio[t] + h->quantos[t]);
    for (int i = 0; i < n; i++)
        if (tamanhos[i]) h->simbolos[inicio[tamanhos[i]]++] = (uint16_t)i;
}

/* Lê um símbolo: o código cresce um bit por vez; quando cai na faixa dos
 * códigos daquele tamanho, achamos o símbolo. */
static int decodificar(leitor_t *l, const huffman_t *h)
{
    int codigo = 0, primeiro = 0, indice = 0;
    for (int t = 1; t < 16; t++) {
        codigo |= (int)bits(l, 1);
        int n = h->quantos[t];
        if (codigo - n < primeiro) return h->simbolos[indice + (codigo - primeiro)];
        indice += n;
        primeiro += n;
        primeiro <<= 1;
        codigo <<= 1;
        if (l->erro) return -1;
    }
    l->erro = true;
    return -1;
}

static const uint16_t base_tam[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t extra_tam[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t base_dist[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t extra_dist[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void emitir(leitor_t *l, uint8_t b)
{
    if (l->feito >= l->max) { l->erro = true; return; }
    l->saida[l->feito++] = b;
}

static void bloco_codificado(leitor_t *l, const huffman_t *lit, const huffman_t *dist)
{
    for (;;) {
        int s = decodificar(l, lit);
        if (s < 0 || l->erro) { l->erro = true; return; }
        if (s < 256) { emitir(l, (uint8_t)s); continue; }
        if (s == 256) return;
        s -= 257;
        if (s >= 29) { l->erro = true; return; }
        uint32_t tam = base_tam[s] + bits(l, extra_tam[s]);
        int d = decodificar(l, dist);
        if (d < 0 || d >= 30) { l->erro = true; return; }
        uint32_t distancia = base_dist[d] + bits(l, extra_dist[d]);
        if (distancia > l->feito) { l->erro = true; return; }
        for (uint32_t i = 0; i < tam && !l->erro; i++) emitir(l, l->saida[l->feito - distancia]);
    }
}

static void bloco_proprio(leitor_t *l)
{
    int nlit = (int)bits(l, 5) + 257, ndist = (int)bits(l, 5) + 1, ncod = (int)bits(l, 4) + 4;
    static const uint8_t ordem[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t tam_cod[19];
    memset(tam_cod, 0, sizeof tam_cod);
    for (int i = 0; i < ncod; i++) tam_cod[ordem[i]] = (uint8_t)bits(l, 3);
    huffman_t cod, lit, dist;
    montar_huffman(&cod, tam_cod, 19);
    uint8_t tams[320];
    int k = 0;
    while (k < nlit + ndist && !l->erro) {
        int s = decodificar(l, &cod);
        if (s < 0) { l->erro = true; return; }
        if (s < 16) tams[k++] = (uint8_t)s;
        else {
            int repetir, valor = 0;
            if (s == 16) { if (k == 0) { l->erro = true; return; } valor = tams[k - 1]; repetir = 3 + (int)bits(l, 2); }
            else if (s == 17) repetir = 3 + (int)bits(l, 3);
            else repetir = 11 + (int)bits(l, 7);
            if (k + repetir > nlit + ndist) { l->erro = true; return; }
            while (repetir--) tams[k++] = (uint8_t)valor;
        }
    }
    montar_huffman(&lit, tams, nlit);
    montar_huffman(&dist, tams + nlit, ndist);
    bloco_codificado(l, &lit, &dist);
}

/* DEFLATE puro. Devolve quantos bytes saíram, ou -1. */
int inflar(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max)
{
    leitor_t l = { dados, n, 0, 0, 0, saida, max, 0, false };
    static huffman_t fixo_lit, fixo_dist;
    static bool fixos = false;
    if (!fixos) {
        uint8_t t[288];
        for (int i = 0; i < 288; i++) t[i] = (uint8_t)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        montar_huffman(&fixo_lit, t, 288);
        for (int i = 0; i < 30; i++) t[i] = 5;
        montar_huffman(&fixo_dist, t, 30);
        fixos = true;
    }
    int ultimo;
    do {
        ultimo = (int)bits(&l, 1);
        int tipo = (int)bits(&l, 2);
        if (tipo == 0) {
            l.bits = 0; l.qtd = 0;                       /* alinha no byte */
            if (l.pos + 4 > l.n) return -1;
            uint32_t tam = (uint32_t)(l.d[l.pos] | (l.d[l.pos + 1] << 8));
            l.pos += 4;
            if (l.pos + tam > l.n) return -1;
            for (uint32_t i = 0; i < tam && !l.erro; i++) emitir(&l, l.d[l.pos + i]);
            l.pos += tam;
        } else if (tipo == 1) bloco_codificado(&l, &fixo_lit, &fixo_dist);
        else if (tipo == 2) bloco_proprio(&l);
        else return -1;
        if (l.erro) return -1;
    } while (!ultimo);
    return (int)l.feito;
}

int inflar_zlib(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max)
{
    if (n < 6 || (dados[0] & 0x0F) != 8 || ((dados[0] << 8) | dados[1]) % 31 != 0 || (dados[1] & 0x20)) return -1;
    return inflar(dados + 2, n - 6, saida, max);
}

int inflar_gzip(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max)
{
    if (n < 18 || dados[0] != 0x1F || dados[1] != 0x8B || dados[2] != 8) return -1;
    uint8_t flags = dados[3];
    uint32_t p = 10;
    if (flags & 4) { if (p + 2 > n) return -1; p += 2u + (uint32_t)(dados[p] | (dados[p + 1] << 8)); }
    if (flags & 8) while (p < n && dados[p++]) ;
    if (flags & 16) while (p < n && dados[p++]) ;
    if (flags & 2) p += 2;
    if (p + 8 > n) return -1;
    return inflar(dados + p, n - p - 8, saida, max);
}
