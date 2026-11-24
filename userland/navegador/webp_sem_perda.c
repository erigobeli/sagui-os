/* webp_sem_perda.c - WebP sem perdas (o chunk "VP8L"), escrito a partir da
 * especificação (RFC 9649, "WebP Image Format", seção 3: "Specification
 * for WebP Lossless Bitstream").
 *
 * O fluxo é lido bit a bit, do bit menos significativo para o mais. A
 * imagem principal vem com transformações (preditor, cor, "subtrai o
 * verde", paleta), que são desfeitas no fim na ordem contrária à de
 * leitura. Cada pixel é um literal ARGB (códigos de prefixo/Huffman
 * separados para verde, vermelho, azul e alfa), uma cópia de trecho já
 * decodificado (tamanho e distância, como no LZ77) ou um índice no cache de
 * cores. Subimagens (os dados das transformações e a "imagem de entropia",
 * que escolhe o grupo de códigos por bloco) usam o mesmo formato, sem
 * transformações.
 *
 * Saída: 0xAARRGGBB, como o resto do Cipó (é o mesmo arranjo do VP8L). */
#include "sagui.h"

/* Imagens grandes passam a vez no meio (sagui.h); fraca porque este arquivo
 * também é compilado nos testes no computador, onde ela não existe. */
extern void (*ceder_vez)(void) __attribute__((weak));
#define CEDER() do { if (&ceder_vez && ceder_vez) ceder_vez(); } while (0)

#define MAX_LADO 16384

typedef struct {
    const uint8_t *d;
    uint32_t n, pos;      /* próximo byte a entrar no acumulador */
    uint64_t acc;         /* bits ainda não usados (os de baixo primeiro) */
    int qtd;
    bool erro;
} bits_t;

static uint32_t bits_ler(bits_t *b, int n)
{
    if (n == 0) return 0;
    while (b->qtd < n) {
        if (b->pos >= b->n) { b->erro = true; return 0; }
        b->acc |= (uint64_t)b->d[b->pos++] << b->qtd;
        b->qtd += 8;
    }
    uint32_t v = (uint32_t)(b->acc & ((1ull << n) - 1));
    b->acc >>= n;
    b->qtd -= n;
    return v;
}

/* ------------------------------------------------------------ códigos de prefixo */

#define RAP 8   /* bits da tabela rápida */
typedef struct {
    uint16_t quantos[16];
    uint16_t *simbolos;
    int unico;             /* >= 0: só um símbolo, código de 0 bits */
    uint16_t rapido[1 << RAP];   /* símbolo << 4 | tamanho; 0 = mais longo */
} prefixo_t;

static bool pref_montar(prefixo_t *h, const uint8_t *tam, int n)
{
    uint16_t inicio[16];
    memset(h->quantos, 0, sizeof h->quantos);
    memset(h->rapido, 0, sizeof h->rapido);
    h->unico = -1;
    int nao_zero = 0, ultimo = 0;
    for (int i = 0; i < n; i++) if (tam[i]) { h->quantos[tam[i]]++; nao_zero++; ultimo = i; }
    if (nao_zero == 0) return false;
    if (nao_zero == 1) { h->unico = ultimo; return true; }
    int sobra = 1;
    for (int t = 1; t < 16; t++) { sobra = sobra * 2 - h->quantos[t]; if (sobra < 0) return false; }
    if (sobra != 0) return false;   /* incompleto */
    h->simbolos = aloca((size_t)n * sizeof(uint16_t));
    if (!h->simbolos) return false;
    inicio[1] = 0;
    for (int t = 1; t < 15; t++) inicio[t + 1] = (uint16_t)(inicio[t] + h->quantos[t]);
    for (int i = 0; i < n; i++) if (tam[i]) h->simbolos[inicio[tam[i]]++] = (uint16_t)i;
    uint32_t codigo = 0;
    int k = 0;
    for (int t = 1; t < 16; t++) {
        for (int j = 0; j < h->quantos[t]; j++, codigo++, k++) {
            if (t > RAP) continue;
            uint32_t inv = 0;
            for (int b = 0; b < t; b++) if (codigo & (1u << b)) inv |= 1u << (t - 1 - b);
            for (uint32_t x = inv; x < (1u << RAP); x += 1u << t) h->rapido[x] = (uint16_t)(h->simbolos[k] << 4 | t);
        }
        codigo <<= 1;
    }
    return true;
}

static void pref_soltar(prefixo_t *h) { libera(h->simbolos); h->simbolos = NULL; }

static int simbolo(bits_t *b, const prefixo_t *h)
{
    if (h->unico >= 0) return h->unico;
    while (b->qtd < RAP && b->pos < b->n) { b->acc |= (uint64_t)b->d[b->pos++] << b->qtd; b->qtd += 8; }
    uint16_t r = h->rapido[b->acc & ((1u << RAP) - 1)];
    if (r && (r & 15) <= b->qtd) { b->acc >>= r & 15; b->qtd -= r & 15; return r >> 4; }
    int codigo = 0, primeiro = 0, indice = 0;
    for (int t = 1; t < 16; t++) {
        codigo |= (int)bits_ler(b, 1);
        if (b->erro) return -1;
        int q = h->quantos[t];
        if (codigo - primeiro < q) return h->simbolos[indice + codigo - primeiro];
        indice += q;
        primeiro = (primeiro + q) << 1;
        codigo <<= 1;
    }
    b->erro = true;
    return -1;
}

/* Lê um código de prefixo de "alfabeto" símbolos (RFC 9649, 3.7.2.1). */
static bool ler_prefixo(bits_t *b, prefixo_t *h, int alfabeto)
{
    uint8_t *tam = aloca_zerado((size_t)alfabeto);
    if (!tam) return false;
    bool ok = false;
    if (bits_ler(b, 1)) {   /* código simples: 1 ou 2 símbolos */
        int qtd = (int)bits_ler(b, 1) + 1;
        int s0 = (int)bits_ler(b, bits_ler(b, 1) ? 8 : 1);
        if (s0 < alfabeto) tam[s0] = 1;
        if (qtd == 2) { int s1 = (int)bits_ler(b, 8); if (s1 < alfabeto) tam[s1] = 1; }
        if (qtd == 1) { libera(tam); h->unico = s0 < alfabeto ? s0 : 0; h->simbolos = NULL; return !b->erro; }
    } else {
        static const uint8_t ordem[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        uint8_t tcl[19] = { 0 };
        int ncl = 4 + (int)bits_ler(b, 4);
        for (int i = 0; i < ncl && i < 19; i++) tcl[ordem[i]] = (uint8_t)bits_ler(b, 3);
        prefixo_t cl = { 0 };
        if (b->erro || !pref_montar(&cl, tcl, 19)) goto fim;
        int max_sim = alfabeto;
        if (bits_ler(b, 1)) { int nb = 2 + 2 * (int)bits_ler(b, 3); max_sim = 2 + (int)bits_ler(b, nb); }
        int s = 0, ant = 8;
        while (s < alfabeto && !b->erro) {
            if (max_sim-- == 0) break;
            int c = simbolo(b, &cl);
            if (c < 0) break;
            if (c < 16) { tam[s++] = (uint8_t)c; if (c) ant = c; continue; }
            int rep, val = 0;
            if (c == 16) { rep = 3 + (int)bits_ler(b, 2); val = ant; }
            else if (c == 17) rep = 3 + (int)bits_ler(b, 3);
            else rep = 11 + (int)bits_ler(b, 7);
            if (s + rep > alfabeto) { b->erro = true; break; }
            while (rep--) tam[s++] = (uint8_t)val;
        }
        pref_soltar(&cl);
        if (b->erro) goto fim;
    }
    ok = pref_montar(h, tam, alfabeto);
fim:
    libera(tam);
    return ok && !b->erro;
}

/* ------------------------------------------------------------ dados da imagem */

#define GRUPO 5
typedef struct { prefixo_t p[GRUPO]; } grupo_t;

static const uint8_t mapa_dist[120][2] = {
    {0,1},{1,0},{1,1},{-1+256,1},{0,2},{2,0},{1,2},{-1+256,2},{2,1},{-2+256,1},{2,2},{-2+256,2},{0,3},{3,0},{1,3},{-1+256,3},
    {3,1},{-3+256,1},{2,3},{-2+256,3},{3,2},{-3+256,2},{0,4},{4,0},{1,4},{-1+256,4},{4,1},{-4+256,1},{3,3},{-3+256,3},{2,4},{-2+256,4},
    {4,2},{-4+256,2},{0,5},{3,4},{-3+256,4},{4,3},{-4+256,3},{5,0},{1,5},{-1+256,5},{5,1},{-5+256,1},{2,5},{-2+256,5},{5,2},{-5+256,2},
    {4,4},{-4+256,4},{3,5},{-3+256,5},{5,3},{-5+256,3},{0,6},{6,0},{1,6},{-1+256,6},{6,1},{-6+256,1},{2,6},{-2+256,6},{6,2},{-6+256,2},
    {4,5},{-4+256,5},{5,4},{-5+256,4},{3,6},{-3+256,6},{6,3},{-6+256,3},{0,7},{7,0},{1,7},{-1+256,7},{5,5},{-5+256,5},{7,1},{-7+256,1},
    {4,6},{-4+256,6},{6,4},{-6+256,4},{2,7},{-2+256,7},{7,2},{-7+256,2},{3,7},{-3+256,7},{7,3},{-7+256,3},{5,6},{-5+256,6},{6,5},{-6+256,5},
    {8,0},{4,7},{-4+256,7},{7,4},{-7+256,4},{8,1},{8,2},{6,6},{-6+256,6},{8,3},{5,7},{-5+256,7},{7,5},{-7+256,5},{8,4},{6,7},
    {-6+256,7},{7,6},{-7+256,6},{8,5},{7,7},{-7+256,7},{8,6},{8,7}
};

static uint32_t valor_prefixo(bits_t *b, int p)
{
    if (p < 4) return (uint32_t)p + 1;
    int extra = (p - 2) >> 1;
    uint32_t base = (uint32_t)(2 + (p & 1)) << extra;
    return base + bits_ler(b, extra) + 1;
}

static uint32_t *ler_imagem(bits_t *b, int l, int a, bool principal);

/* Os pixels de uma imagem l x a (já com o cache e os grupos lidos). */
static bool ler_pixels(bits_t *b, uint32_t *px, int l, int a, grupo_t *g, int ngrupos,
                       const uint32_t *entropia, int bits_ent, int le, int cache_bits)
{
    uint32_t *cache = NULL;
    if (cache_bits) { cache = aloca_zerado(sizeof(uint32_t) << cache_bits); if (!cache) return false; }
    uint32_t total = (uint32_t)l * (uint32_t)a, i = 0;
    int vez = 0;
    while (i < total && !b->erro) {
        if (!(++vez & 4095)) CEDER();
        int x = (int)(i % (uint32_t)l), y = (int)(i / (uint32_t)l);
        grupo_t *G = g;
        if (entropia) {
            int k = (int)((entropia[(y >> bits_ent) * le + (x >> bits_ent)] >> 8) & 0xFFFF);
            if (k >= ngrupos) { b->erro = true; break; }
            G = &g[k];
        }
        int s = simbolo(b, &G->p[0]);
        if (s < 0) break;
        if (s < 256) {
            int r = simbolo(b, &G->p[1]), bl = simbolo(b, &G->p[2]), al = simbolo(b, &G->p[3]);
            if (r < 0 || bl < 0 || al < 0) { b->erro = true; break; }
            uint32_t c = (uint32_t)al << 24 | (uint32_t)r << 16 | (uint32_t)s << 8 | (uint32_t)bl;
            px[i++] = c;
            if (cache) cache[(0x1e35a7bdu * c) >> (32 - cache_bits)] = c;
        } else if (s < 256 + 24) {
            uint32_t tam = valor_prefixo(b, s - 256);
            int ds = simbolo(b, &G->p[4]);
            if (ds < 0) { b->erro = true; break; }
            uint32_t cod = valor_prefixo(b, ds);
            int64_t dist;
            if (cod > 120) dist = (int64_t)cod - 120;
            else {
                int xi = (int8_t)mapa_dist[cod - 1][0], yi = mapa_dist[cod - 1][1];
                dist = (int64_t)yi * l + xi;
                if (dist < 1) dist = 1;
            }
            if (dist > i || tam > total - i) { b->erro = true; break; }
            for (uint32_t k = 0; k < tam; k++, i++) {
                uint32_t c = px[i - (uint32_t)dist];
                px[i] = c;
                if (cache) cache[(0x1e35a7bdu * c) >> (32 - cache_bits)] = c;
            }
        } else {
            int k = s - 280;
            if (!cache || k >= (1 << cache_bits)) { b->erro = true; break; }
            px[i++] = cache[k];
        }
    }
    libera(cache);
    return !b->erro;
}

/* Imagem codificada (3.7.2.2): cache de cores, grupos de códigos (com a
 * imagem de entropia só na principal) e os pixels. */
static uint32_t *ler_imagem(bits_t *b, int l, int a, bool principal)
{
    int cache_bits = 0;
    if (bits_ler(b, 1)) { cache_bits = (int)bits_ler(b, 4); if (cache_bits < 1 || cache_bits > 11) { b->erro = true; return NULL; } }
    uint32_t *entropia = NULL;
    int bits_ent = 0, le = 0, ngrupos = 1;
    if (principal && bits_ler(b, 1)) {
        bits_ent = (int)bits_ler(b, 3) + 2;
        le = (l + (1 << bits_ent) - 1) >> bits_ent;
        int ae = (a + (1 << bits_ent) - 1) >> bits_ent;
        entropia = ler_imagem(b, le, ae, false);
        if (!entropia) return NULL;
        for (int i = 0; i < le * ae; i++) { int k = (int)((entropia[i] >> 8) & 0xFFFF); if (k + 1 > ngrupos) ngrupos = k + 1; }
    }
    if (b->erro) { libera(entropia); return NULL; }
    grupo_t *g = aloca_zerado((size_t)ngrupos * sizeof(grupo_t));
    uint32_t *px = aloca((size_t)l * (size_t)a * 4);
    bool ok = g && px;
    for (int k = 0; ok && k < ngrupos; k++) {
        static const int alfabetos[GRUPO] = { 256 + 24, 256, 256, 256, 40 };
        for (int j = 0; j < GRUPO && ok; j++) {
            g[k].p[j].unico = -1;
            ok = ler_prefixo(b, &g[k].p[j], alfabetos[j] + (j == 0 && cache_bits ? 1 << cache_bits : 0));
        }
    }
    if (ok) ok = ler_pixels(b, px, l, a, g, ngrupos, entropia, bits_ent, le, cache_bits);
    if (g) for (int k = 0; k < ngrupos; k++) for (int j = 0; j < GRUPO; j++) pref_soltar(&g[k].p[j]);
    libera(g);
    libera(entropia);
    if (!ok) { libera(px); return NULL; }
    return px;
}

/* ------------------------------------------------------------ transformações */

static uint32_t media2(uint32_t a, uint32_t b) { return (((a ^ b) & 0xFEFEFEFEu) >> 1) + (a & b); }
static int canal(uint32_t c, int s) { return (int)((c >> s) & 0xFF); }
static int abs_i(int v) { return v < 0 ? -v : v; }
static int limita(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static uint32_t selecionar(uint32_t L, uint32_t T, uint32_t TL)
{
    int pl = 0, pt = 0;
    for (int s = 0; s < 32; s += 8) {
        int p = canal(L, s) + canal(T, s) - canal(TL, s);
        pl += abs_i(p - canal(L, s));
        pt += abs_i(p - canal(T, s));
    }
    return pl < pt ? L : T;
}
static uint32_t soma_sub_cheia(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) r |= (uint32_t)limita(canal(a, s) + canal(b, s) - canal(c, s)) << s;
    return r;
}
static uint32_t soma_sub_meia(uint32_t a, uint32_t b)
{
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) r |= (uint32_t)limita(canal(a, s) + (canal(a, s) - canal(b, s)) / 2) << s;
    return r;
}
static uint32_t somar_px(uint32_t a, uint32_t b)
{
    return (((a & 0xFF00FF00u) + (b & 0xFF00FF00u)) & 0xFF00FF00u) | (((a & 0x00FF00FFu) + (b & 0x00FF00FFu)) & 0x00FF00FFu);
}

static void desfazer_preditor(uint32_t *px, int l, int a, const uint32_t *t, int bits)
{
    int lt = (l + (1 << bits) - 1) >> bits;
    for (int y = 0; y < a; y++) {
        if ((y & 15) == 0) CEDER();
        for (int x = 0; x < l; x++) {
            uint32_t *p = &px[y * l + x], pred;
            if (y == 0) pred = x == 0 ? 0xFF000000u : p[-1];
            else if (x == 0) pred = p[-l];
            else {
                uint32_t L = p[-1], T = p[-l], TL = p[-l - 1], TR = p[-l + 1];   /* na última coluna, TR é o 1.º da linha atual */
                switch ((t[(y >> bits) * lt + (x >> bits)] >> 8) & 15) {
                case 0: pred = 0xFF000000u; break;
                case 1: pred = L; break;
                case 2: pred = T; break;
                case 3: pred = TR; break;
                case 4: pred = TL; break;
                case 5: pred = media2(media2(L, TR), T); break;
                case 6: pred = media2(L, TL); break;
                case 7: pred = media2(L, T); break;
                case 8: pred = media2(TL, T); break;
                case 9: pred = media2(T, TR); break;
                case 10: pred = media2(media2(L, TL), media2(T, TR)); break;
                case 11: pred = selecionar(L, T, TL); break;
                case 12: pred = soma_sub_cheia(L, T, TL); break;
                case 13: pred = soma_sub_meia(media2(L, T), TL); break;
                default: pred = 0xFF000000u; break;
                }
            }
            *p = somar_px(*p, pred);
        }
    }
}

static int delta_cor(int8_t t, int8_t c) { return ((int)t * (int)c) >> 5; }

static void desfazer_cor(uint32_t *px, int l, int a, const uint32_t *t, int bits)
{
    int lt = (l + (1 << bits) - 1) >> bits;
    for (int y = 0; y < a; y++)
        for (int x = 0; x < l; x++) {
            uint32_t e = t[(y >> bits) * lt + (x >> bits)];
            int8_t g2r = (int8_t)(e & 0xFF), g2b = (int8_t)((e >> 8) & 0xFF), r2b = (int8_t)((e >> 16) & 0xFF);
            uint32_t c = px[y * l + x];
            int8_t g = (int8_t)((c >> 8) & 0xFF);
            int r = (int)((c >> 16) & 0xFF) + delta_cor(g2r, g);
            r &= 0xFF;
            int bl = (int)(c & 0xFF) + delta_cor(g2b, g) + delta_cor(r2b, (int8_t)r);
            px[y * l + x] = (c & 0xFF00FF00u) | (uint32_t)r << 16 | (uint32_t)(bl & 0xFF);
        }
}

static void desfazer_verde(uint32_t *px, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = px[i], g = (c >> 8) & 0xFF;
        px[i] = (c & 0xFF00FF00u) | ((((c >> 16) + g) & 0xFF) << 16) | (((c & 0xFF) + g) & 0xFF);
    }
}

/* Paleta: cada pixel compactado guarda 1, 2, 4 ou 8 índices no verde. */
static uint32_t *desfazer_paleta(const uint32_t *px, int lc, int l, int a, const uint32_t *pal, int npal, int bits)
{
    uint32_t *s = aloca((size_t)l * (size_t)a * 4);
    if (!s) return NULL;
    int bpp = 8 >> bits, mask = (1 << bits) - 1, m = (1 << bpp) - 1;
    for (int y = 0; y < a; y++)
        for (int x = 0; x < l; x++) {
            uint32_t g = (px[y * lc + (x >> bits)] >> 8) & 0xFF;
            int k = (int)((g >> ((x & mask) * bpp)) & (uint32_t)m);
            s[y * l + x] = k < npal ? pal[k] : 0;
        }
    return s;
}

/* ------------------------------------------------------------ entrada */

bool vp8l_ler(const uint8_t *d, unsigned n, uint32_t **saida, int *pl, int *pa)
{
    bits_t b = { d, n, 0, 0, 0, false };
    if (n < 5 || bits_ler(&b, 8) != 0x2F) return false;
    int l = (int)bits_ler(&b, 14) + 1, a = (int)bits_ler(&b, 14) + 1;
    bits_ler(&b, 1);   /* alfa usado: só uma dica */
    if (bits_ler(&b, 3) != 0 || l > MAX_LADO || a > MAX_LADO || (uint64_t)l * (uint64_t)a > 64u * 1024 * 1024) return false;
    struct { int tipo, bits, npal; uint32_t *dados; } tr[4];
    int ntr = 0, lc = l;   /* lc: largura codificada (a paleta compacta pixels) */
    bool ok = true;
    unsigned vistos = 0;
    while (ok && bits_ler(&b, 1)) {
        int tipo = (int)bits_ler(&b, 2);
        if (vistos & (1u << tipo) || ntr == 4) { ok = false; break; }
        vistos |= 1u << tipo;
        tr[ntr].tipo = tipo; tr[ntr].dados = NULL; tr[ntr].bits = 0; tr[ntr].npal = 0;
        if (tipo == 0 || tipo == 1) {
            tr[ntr].bits = (int)bits_ler(&b, 3) + 2;
            int s = 1 << tr[ntr].bits;
            tr[ntr].dados = ler_imagem(&b, (lc + s - 1) / s, (a + s - 1) / s, false);
            ok = tr[ntr].dados != NULL;
        } else if (tipo == 3) {
            int np = (int)bits_ler(&b, 8) + 1;
            uint32_t *pal = ler_imagem(&b, np, 1, false);
            if (!pal) { ok = false; break; }
            for (int i = 1; i < np; i++) pal[i] = somar_px(pal[i], pal[i - 1]);
            tr[ntr].dados = pal; tr[ntr].npal = np;
            tr[ntr].bits = np <= 2 ? 3 : np <= 4 ? 2 : np <= 16 ? 1 : 0;
            tr[ntr].tipo = 3;
            int lc_antes = lc;
            lc = (lc + (1 << tr[ntr].bits) - 1) >> tr[ntr].bits;
            tr[ntr].npal = np | (lc_antes << 16);   /* guarda a largura de antes da paleta */
        }
        ntr++;
        if (b.erro) ok = false;
    }
    uint32_t *px = ok ? ler_imagem(&b, lc, a, true) : NULL;
    ok = px != NULL;
    /* desfaz na ordem contrária */
    for (int i = ntr - 1; ok && i >= 0; i--) {
        if (tr[i].tipo == 0) desfazer_preditor(px, lc, a, tr[i].dados, tr[i].bits);
        else if (tr[i].tipo == 1) desfazer_cor(px, lc, a, tr[i].dados, tr[i].bits);
        else if (tr[i].tipo == 2) desfazer_verde(px, (uint32_t)lc * (uint32_t)a);
        else {
            int largura = tr[i].npal >> 16, np = tr[i].npal & 0xFFFF;
            uint32_t *s = desfazer_paleta(px, lc, largura, a, tr[i].dados, np, tr[i].bits);
            libera(px);
            px = s;
            lc = largura;
            ok = px != NULL;
        }
    }
    for (int i = 0; i < ntr; i++) libera(tr[i].dados);
    if (!ok) { libera(px); return false; }
    *saida = px; *pl = l; *pa = a;
    return true;
}
