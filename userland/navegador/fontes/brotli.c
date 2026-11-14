/* brotli.c - descompressor Brotli (RFC 7932), usado pelas fontes WOFF2.
 *
 * Guarda a saída inteira na memória (não usa janela circular): as fontes
 * cabem folgadas. Códigos de prefixo decodificados bit a bit pela forma
 * canônica (contagem por comprimento), como no inflate.
 * Dados da RFC (dicionário, transformações, tabelas de contexto) em brotli_tab.h. */
#include "sagui.h"
#include "cipo.h"
#include "brotli_tab.h"

int brotli_linha;   /* linha da última falha (diagnóstico) */
#define FALHA() (brotli_linha = __LINE__)

typedef struct {
    const uint8_t *p; unsigned n, pos;   /* pos em bits */
    int erro;
} br_bits;

static unsigned br_ler(br_bits *b, int k)
{
    unsigned v = 0;
    for (int i = 0; i < k; i++) {
        unsigned byte = b->pos >> 3;
        if (byte >= b->n) { b->erro = 1, FALHA(); return 0; }
        v |= (unsigned)((b->p[byte] >> (b->pos & 7)) & 1) << i;
        b->pos++;
    }
    return v;
}

/* código de prefixo canônico */
#define BR_MAXSIM 704
typedef struct { uint16_t cont[16]; uint16_t sim[BR_MAXSIM]; int unico; } br_cod;

static int br_montar(br_cod *c, const uint8_t *comp, int n)
{
    uint16_t desl[16];
    memset(c->cont, 0, sizeof c->cont);
    for (int i = 0; i < n; i++) c->cont[comp[i]]++;
    c->cont[0] = 0;
    int usados = 0, ult = 0;
    for (int i = 0; i < n; i++) if (comp[i]) { usados++; ult = i; }
    c->unico = usados == 1 ? ult : -1;
    desl[1] = 0;
    for (int l = 1; l < 15; l++) desl[l + 1] = (uint16_t)(desl[l] + c->cont[l]);
    for (int i = 0; i < n; i++) if (comp[i]) c->sim[desl[comp[i]]++] = (uint16_t)i;
    return usados;
}

static int br_sim(br_bits *b, const br_cod *c)
{
    if (c->unico >= 0) return c->unico;    /* código de um símbolo só: 0 bits */
    int cod = 0, prim = 0, ind = 0;
    for (int l = 1; l < 16; l++) {
        cod |= (int)br_ler(b, 1);
        int k = c->cont[l];
        if (cod - k < prim) return c->sim[ind + (cod - prim)];
        ind += k; prim += k; prim <<= 1; cod <<= 1;
        if (b->erro) return 0;
    }
    b->erro = 1, FALHA();
    return 0;
}

/* Lê um código de prefixo (seção 3.4/3.5) para um alfabeto de tam símbolos. */
static void br_ler_cod(br_bits *b, br_cod *c, int tam)
{
    uint8_t comp[BR_MAXSIM];
    memset(comp, 0, (size_t)tam);
    int bits_alf = 0; while ((1 << bits_alf) < tam) bits_alf++;
    unsigned hskip = br_ler(b, 2);
    if (hskip == 1) {   /* código simples */
        int nsim = (int)br_ler(b, 2) + 1, s[4];
        for (int i = 0; i < nsim; i++) { s[i] = (int)br_ler(b, bits_alf); if (s[i] >= tam) { b->erro = 1, FALHA(); return; } }
        if (nsim == 1) { comp[s[0]] = 1; br_montar(c, comp, tam); c->unico = s[0]; return; }
        if (nsim == 2) { comp[s[0]] = 1; comp[s[1]] = 1; }
        else if (nsim == 3) { comp[s[0]] = 1; comp[s[1]] = 2; comp[s[2]] = 2; }
        else if (!br_ler(b, 1)) { for (int i = 0; i < 4; i++) comp[s[i]] = 2; }
        else { comp[s[0]] = 1; comp[s[1]] = 2; comp[s[2]] = 3; comp[s[3]] = 3; }
        br_montar(c, comp, tam);
        return;
    }
    /* comprimentos do código dos comprimentos */
    static const uint8_t ordem[18] = { 1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    static const uint8_t pl_comp[16] = { 2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4 };
    static const uint8_t pl_val[16] = { 0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5 };
    uint8_t cc[18]; memset(cc, 0, sizeof cc);
    int espaco = 32, num = 0;
    for (int i = (int)hskip; i < 18 && espaco > 0; i++) {
        unsigned salvo = b->pos;
        unsigned v4 = br_ler(b, 4);
        int l = pl_comp[v4 & 15];
        b->pos = salvo + (unsigned)l;
        int v = pl_val[v4 & 15];
        cc[ordem[i]] = (uint8_t)v;
        if (v) { espaco -= 32 >> v; num++; }
    }
    if (num != 1 && espaco != 0) { b->erro = 1, FALHA(); return; }
    br_cod ccod;
    br_montar(&ccod, cc, 18);
    int anterior = 8, rep = 0, rep_comp = 0, sim = 0;
    int esp = 32768;
    while (sim < tam && esp > 0) {
        int v = br_sim(b, &ccod);
        if (b->erro) return;
        if (v < 16) {
            rep = 0;
            comp[sim++] = (uint8_t)v;
            if (v) { anterior = v; esp -= 32768 >> v; }
        } else {
            int extra = v == 16 ? 2 : 3, novo = v == 16 ? anterior : 0;
            if (rep_comp != novo) { rep = 0; rep_comp = novo; }
            int velho = rep;
            if (rep > 0) { rep -= 2; rep <<= extra; }
            rep += (int)br_ler(b, extra) + 3;
            int delta = rep - velho;
            if (sim + delta > tam) { b->erro = 1, FALHA(); return; }
            for (int k = 0; k < delta; k++) comp[sim++] = (uint8_t)novo;
            if (novo) esp -= delta * (32768 >> novo);
        }
    }
    if (esp != 0) { b->erro = 1, FALHA(); return; }
    br_montar(c, comp, tam);
}

static unsigned br_var8(br_bits *b)
{
    if (!br_ler(b, 1)) return 0;
    unsigned n = br_ler(b, 3);
    if (!n) return 1;
    return (1u << n) + br_ler(b, (int)n);
}

static const uint16_t blen_base[26] = { 1, 5, 9, 13, 17, 25, 33, 41, 49, 65, 81, 97, 113, 145, 177, 209, 241, 305, 369, 497, 753, 1265, 2289, 4337, 8433, 16625 };
static const uint8_t blen_extra[26] = { 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 24 };
static const uint32_t ins_base[24] = { 0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98, 130, 194, 322, 578, 1090, 2114, 6210, 22594 };
static const uint8_t ins_extra[24] = { 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 12, 14, 24 };
static const uint32_t cop_base[24] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30, 38, 54, 70, 102, 134, 198, 326, 582, 1094, 2118 };
static const uint8_t cop_extra[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 24 };

/* tipos de bloco de uma categoria (literais, comandos, distâncias) */
typedef struct { unsigned n, tipo, ult, cont; br_cod *ct, *cc; } br_blocos;

static unsigned br_cont_bloco(br_bits *b, br_cod *cc)
{
    int s = br_sim(b, cc);
    if (s >= 26) { b->erro = 1, FALHA(); return 1; }
    return blen_base[s] + br_ler(b, blen_extra[s]);
}
static void br_ler_blocos(br_bits *b, br_blocos *k)
{
    k->n = br_var8(b) + 1; k->tipo = 0; k->ult = 1; k->cont = 1u << 24;
    if (k->n >= 2) {
        br_ler_cod(b, k->ct, (int)k->n + 2);
        br_ler_cod(b, k->cc, 26);
        k->cont = br_cont_bloco(b, k->cc);
    }
}
static void br_trocar(br_bits *b, br_blocos *k)
{
    int s = br_sim(b, k->ct);
    unsigned t = s == 0 ? k->ult : s == 1 ? k->tipo + 1 : (unsigned)s - 2;
    if (t >= k->n) t -= k->n;
    k->ult = k->tipo; k->tipo = t;
    k->cont = br_cont_bloco(b, k->cc);
}

/* mapa de contexto (seção 7.3) */
static int br_mapa(br_bits *b, uint8_t *mapa, int tam, unsigned *ntrees, br_cod *tmp)
{
    *ntrees = br_var8(b) + 1;
    if (*ntrees < 2) { memset(mapa, 0, (size_t)tam); return 1; }
    unsigned rlemax = br_ler(b, 1) ? br_ler(b, 4) + 1 : 0;
    br_ler_cod(b, tmp, (int)(*ntrees + rlemax));
    for (int i = 0; i < tam;) {
        int s = br_sim(b, tmp);
        if (b->erro) return 0;
        if (s == 0) mapa[i++] = 0;
        else if ((unsigned)s <= rlemax) {
            unsigned r = (1u << s) + br_ler(b, s);
            if (i + (int)r > tam) return 0;
            while (r--) mapa[i++] = 0;
        } else mapa[i++] = (uint8_t)(s - (int)rlemax);
    }
    if (br_ler(b, 1)) {   /* move-to-front inverso */
        uint8_t mtf[256];
        for (int i = 0; i < 256; i++) mtf[i] = (uint8_t)i;
        for (int i = 0; i < tam; i++) {
            uint8_t ix = mapa[i], v = mtf[ix];
            mapa[i] = v;
            for (; ix; ix--) mtf[ix] = mtf[ix - 1];
            mtf[0] = v;
        }
    }
    return !b->erro;
}

typedef struct { uint8_t *d; unsigned n, cap, max; } br_saida;
static int br_por(br_saida *s, const uint8_t *p, unsigned k)
{
    if (s->n + k > s->max) return 0;
    if (s->n + k > s->cap) {
        unsigned c = s->cap ? s->cap : 65536;
        while (c < s->n + k) c *= 2;
        if (c > s->max) c = s->max;
        uint8_t *nd = aloca(c);
        if (!nd) return 0;
        if (s->n) memcpy(nd, s->d, s->n);
        if (s->d) libera(s->d);
        s->d = nd; s->cap = c;
    }
    memcpy(s->d + s->n, p, k);
    s->n += k;
    return 1;
}

static int fermentar(uint8_t *p, int n)
{
    if (p[0] < 192) { if (p[0] >= 'a' && p[0] <= 'z') p[0] ^= 32; return 1; }
    if (p[0] < 224) { if (n > 1) p[1] ^= 32; return 2; }
    if (n > 2) p[2] ^= 5;
    return 3;
}

/* palavra do dicionário com transformação */
static int br_palavra(br_saida *s, int comp, unsigned id)
{
    if (comp < 4 || comp > 24) return 0;
    int nb = br_ndbits[comp];
    unsigned desl = 0;
    for (int l = 4; l < comp; l++) desl += (unsigned)l << br_ndbits[l];
    unsigned ix = id & ((1u << nb) - 1), tf = id >> nb;
    if (tf >= 121) return 0;
    const uint8_t *w = br_dic + desl + ix * (unsigned)comp;
    uint8_t buf[64];
    int k = 0;
    for (const char *p = br_tf[tf].pre; *p; p++) buf[k++] = (uint8_t)*p;
    int t = br_tf[tf].tipo, ini = 0, fim = comp;
    if (t >= 3 && t <= 11) ini = t - 2;
    if (t >= 12 && t <= 20) fim = comp - (t - 11);
    if (ini > comp) ini = comp;
    if (fim < 0) fim = 0;
    int w0 = k;
    for (int i = ini; i < fim; i++) buf[k++] = w[i];
    if (t == 1 && k > w0) fermentar(buf + w0, k - w0);
    else if (t == 2) for (int i = w0; i < k;) i += fermentar(buf + i, k - i);
    for (const char *p = br_tf[tf].suf; *p; p++) buf[k++] = (uint8_t)*p;
    return br_por(s, buf, (unsigned)k);
}

/* Descomprime; devolve bloco de aloca (n em *saida_n) ou NULL. */
uint8_t *brotli_descomprimir(const uint8_t *in, unsigned n, unsigned *saida_n, unsigned max)
{
    br_bits b = { in, n, 0, 0 };
    br_saida s = { NULL, 0, 0, max };
    int wbits = 16;
    if (br_ler(&b, 1)) {
        unsigned k = br_ler(&b, 3);
        if (k) wbits = 17 + (int)k;
        else { k = br_ler(&b, 3); if (k == 1) return NULL; wbits = k ? 8 + (int)k : 17; }
    }
    unsigned janela = (1u << wbits) - 16;
    unsigned dist[4] = { 4, 11, 15, 16 };   /* dist[0] = última */
    /* memória de trabalho: códigos e mapas */
    br_cod *cods = aloca(sizeof(br_cod) * 8);
    uint8_t *mapa_l = NULL, *mapa_d = NULL;
    br_cod *arv_l = NULL, *arv_i = NULL, *arv_d = NULL;
    if (!cods) return NULL;
    int ok = 0;
    for (;;) {
        unsigned ultimo = br_ler(&b, 1);
        if (ultimo && br_ler(&b, 1)) { ok = 1; break; }
        unsigned nib = br_ler(&b, 2);
        if (nib == 3) {   /* metadados: pula */
            if (br_ler(&b, 1)) break;
            unsigned nb = br_ler(&b, 2), len = 0;
            for (unsigned i = 0; i < nb; i++) len |= br_ler(&b, 8) << (8 * i);
            if (nb) len++;
            b.pos = (b.pos + 7) & ~7u;
            b.pos += len * 8;
            if (b.erro || (b.pos >> 3) > b.n) break;
            if (ultimo) { ok = 1; break; }
            continue;
        }
        unsigned mlen = br_ler(&b, (int)(nib + 4) * 4) + 1;
        if (!ultimo && br_ler(&b, 1)) {   /* sem compressão */
            b.pos = (b.pos + 7) & ~7u;
            if ((b.pos >> 3) + mlen > b.n || !br_por(&s, in + (b.pos >> 3), mlen)) break;
            b.pos += mlen * 8;
            continue;
        }
        if (b.erro) break;
        br_blocos bl = { 0, 0, 0, 0, &cods[0], &cods[1] }, bi = { 0, 0, 0, 0, &cods[2], &cods[3] }, bd = { 0, 0, 0, 0, &cods[4], &cods[5] };
        br_ler_blocos(&b, &bl); br_ler_blocos(&b, &bi); br_ler_blocos(&b, &bd);
        unsigned npost = br_ler(&b, 2), ndir = br_ler(&b, 4) << npost;
        uint8_t modos[256];
        for (unsigned i = 0; i < bl.n && i < 256; i++) modos[i] = (uint8_t)br_ler(&b, 2);
        if (b.erro || bl.n > 256 || bi.n > 256 || bd.n > 256) break;
        unsigned ntl, ntd;
        if (mapa_l) libera(mapa_l);
        if (mapa_d) libera(mapa_d);
        mapa_l = aloca(64 * bl.n); mapa_d = aloca(4 * bd.n);
        if (!mapa_l || !mapa_d) break;
        if (!br_mapa(&b, mapa_l, (int)(64 * bl.n), &ntl, &cods[6])) break;
        if (!br_mapa(&b, mapa_d, (int)(4 * bd.n), &ntd, &cods[6])) break;
        if (arv_l) libera(arv_l);
        if (arv_i) libera(arv_i);
        if (arv_d) libera(arv_d);
        arv_l = aloca(sizeof(br_cod) * ntl); arv_i = aloca(sizeof(br_cod) * bi.n); arv_d = aloca(sizeof(br_cod) * ntd);
        if (!arv_l || !arv_i || !arv_d) break;
        int nsd = 16 + (int)ndir + (48 << npost);
        if (nsd > BR_MAXSIM) break;
        for (unsigned i = 0; i < ntl; i++) br_ler_cod(&b, &arv_l[i], 256);
        for (unsigned i = 0; i < bi.n; i++) br_ler_cod(&b, &arv_i[i], 704);
        for (unsigned i = 0; i < ntd; i++) br_ler_cod(&b, &arv_d[i], nsd);
        if (b.erro) break;
        unsigned feito = 0;
        int falha = 0;
        while (feito < mlen) {
            if (bi.cont == 0) br_trocar(&b, &bi);
            bi.cont--;
            int cmd = br_sim(&b, &arv_i[bi.tipo]);
            if (b.erro) { falha = 1, FALHA(); break; }
            int cel = cmd >> 6;
            static const uint8_t ib[11] = { 0, 0, 0, 0, 8, 8, 0, 16, 8, 16, 16 }, cb[11] = { 0, 8, 0, 8, 0, 8, 16, 0, 16, 8, 16 };
            int ic = ib[cel] + ((cmd >> 3) & 7), cc = cb[cel] + (cmd & 7);
            unsigned ins = ins_base[ic] + br_ler(&b, ins_extra[ic]);
            unsigned cop = cop_base[cc] + br_ler(&b, cop_extra[cc]);
            for (unsigned i = 0; i < ins && feito < mlen; i++) {
                if (bl.cont == 0) br_trocar(&b, &bl);
                bl.cont--;
                uint8_t p1 = s.n ? s.d[s.n - 1] : 0, p2 = s.n > 1 ? s.d[s.n - 2] : 0;
                int ctx;
                switch (modos[bl.tipo]) {
                case 0: ctx = p1 & 0x3f; break;
                case 1: ctx = p1 >> 2; break;
                case 2: ctx = br_lut0[p1] | br_lut1[p2]; break;
                default: ctx = (br_lut2[p1] << 3) | br_lut2[p2]; break;
                }
                uint8_t v = (uint8_t)br_sim(&b, &arv_l[mapa_l[bl.tipo * 64 + (unsigned)ctx]]);
                if (!br_por(&s, &v, 1)) { falha = 1, FALHA(); break; }
                feito++;
            }
            if (falha || b.erro) { falha = 1, FALHA(); break; }
            if (feito >= mlen) break;
            unsigned d; int dcod = 0;
            if (cmd < 128) d = dist[0];
            else {
                if (bd.cont == 0) br_trocar(&b, &bd);
                bd.cont--;
                int ctx = cop > 4 ? 3 : (int)cop - 2;
                dcod = br_sim(&b, &arv_d[mapa_d[bd.tipo * 4 + (unsigned)ctx]]);
                if (dcod < 16) {
                    static const uint8_t qual[16] = { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1 };
                    static const int8_t delta[16] = { 0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3 };
                    int v = (int)dist[qual[dcod]] + delta[dcod];
                    if (v <= 0) { falha = 1, FALHA(); break; }
                    d = (unsigned)v;
                } else if (dcod < 16 + (int)ndir) d = (unsigned)dcod - 15;
                else {
                    unsigned x = (unsigned)dcod - ndir - 16;
                    int nbits = 1 + (int)(x >> (npost + 1));
                    unsigned extra = br_ler(&b, nbits);
                    unsigned hc = x >> npost, lc = x & ((1u << npost) - 1);
                    unsigned off = ((2 + (hc & 1)) << nbits) - 4;
                    d = ((off + extra) << npost) + lc + ndir + 1;
                }
            }
            unsigned maxd = s.n < janela ? s.n : janela;
            if (d > maxd) {
                unsigned antes = s.n;
                if (!br_palavra(&s, (int)cop, d - maxd - 1)) { falha = 1, FALHA(); break; }
                feito += s.n - antes;
            } else {
                if (dcod != 0) { dist[3] = dist[2]; dist[2] = dist[1]; dist[1] = dist[0]; dist[0] = d; }
                for (unsigned i = 0; i < cop; i++) {
                    uint8_t v = s.d[s.n - d];
                    if (!br_por(&s, &v, 1)) { falha = 1, FALHA(); break; }
                }
                if (falha) break;
                feito += cop;
            }
        }
        if (falha || b.erro) break;
        if (ultimo) { ok = 1; break; }
    }
    if (mapa_l) libera(mapa_l);
    if (mapa_d) libera(mapa_d);
    if (arv_l) libera(arv_l);
    if (arv_i) libera(arv_i);
    if (arv_d) libera(arv_d);
    libera(cods);
    if (!ok) { if (!brotli_linha) FALHA(); if (s.d) libera(s.d); return NULL; }
    if (!s.d) s.d = aloca(1);
    *saida_n = s.n;
    return s.d;
}
