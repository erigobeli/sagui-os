/* lzma.c - desfaz LZMA (o "LZMA1" do 7-Zip/xz) e o filtro BCJ x86 do ramdisk.
 *
 * Formato no disco (tools/mksgfs.py): "SGXZ", tamanho real (u32), byte de
 * propriedades (lc, lp, pb), tamanho do dicionário (u32) e o fluxo LZMA cru,
 * comprimido depois do filtro x86 (que troca endereços relativos de CALL/JMP
 * por absolutos, deixando o código mais repetitivo). Aqui: LZMA e depois o
 * filtro ao contrário. A saída inteira está na memória, então o "dicionário"
 * é a própria saída.
 *
 * Decodificador escrito a partir da descrição do formato (decodificador de
 * faixa binário, literais com contexto, comprimentos e distâncias com árvores
 * de bits e alinhamento), sem ponto flutuante e sem divisão de 64 bits. */
#include "tipos.h"
#include "lzma.h"
#include "memoria.h"

#define N_ESTADOS 12
#define BITS_PROB 11
#define UM (1u << BITS_PROB)
#define TOPO (1u << 24)

typedef struct {
    const uint8_t *in, *fim;
    uint32_t faixa, cod;
    int erro;
} rc_t;

static uint8_t rc_byte(rc_t *r) { if (r->in >= r->fim) { r->erro = 1; return 0; } return *r->in++; }
static void rc_norm(rc_t *r) { if (r->faixa < TOPO) { r->faixa <<= 8; r->cod = (r->cod << 8) | rc_byte(r); } }
static int rc_bit(rc_t *r, uint16_t *p)
{
    uint32_t limite = (r->faixa >> BITS_PROB) * *p;
    int b;
    if (r->cod < limite) { r->faixa = limite; *p += (uint16_t)((UM - *p) >> 5); b = 0; }
    else { r->faixa -= limite; r->cod -= limite; *p -= (uint16_t)(*p >> 5); b = 1; }
    rc_norm(r);
    return b;
}
static uint32_t rc_direto(rc_t *r, int n)
{
    uint32_t v = 0;
    while (n--) {
        r->faixa >>= 1;
        uint32_t t = (r->cod - r->faixa) >> 31;   /* 1 se cod < faixa */
        r->cod -= r->faixa & (t - 1);
        v = (v << 1) | (1 - t);
        rc_norm(r);
    }
    return v;
}
static uint32_t arvore(rc_t *r, uint16_t *p, int bits)
{
    uint32_t m = 1;
    for (int i = 0; i < bits; i++) m = (m << 1) | (uint32_t)rc_bit(r, &p[m]);
    return m - (1u << bits);
}
static uint32_t arvore_inv(rc_t *r, uint16_t *p, int bits)
{
    uint32_t m = 1, s = 0;
    for (int i = 0; i < bits; i++) { uint32_t b = (uint32_t)rc_bit(r, &p[m]); m = (m << 1) | b; s |= b << i; }
    return s;
}

/* codificador de comprimentos */
typedef struct { uint16_t esc, esc2, baixo[16][8], medio[16][8], alto[256]; } comp_t;
static uint32_t ler_comp(rc_t *r, comp_t *c, uint32_t pos)
{
    if (!rc_bit(r, &c->esc)) return arvore(r, c->baixo[pos], 3);
    if (!rc_bit(r, &c->esc2)) return 8 + arvore(r, c->medio[pos], 3);
    return 16 + arvore(r, c->alto, 8);
}

/* probabilidades: tudo num bloco só (o kernel tem kmalloc) */
typedef struct {
    uint16_t eh_par[N_ESTADOS][16], eh_rep[N_ESTADOS], eh_rep0[N_ESTADOS], eh_rep1[N_ESTADOS], eh_rep2[N_ESTADOS], eh_rep0_longo[N_ESTADOS][16];
    uint16_t slot[4][64], especial[114], alinha[16];
    comp_t comp, rep_comp;
    uint16_t lit[1];   /* 0x300 << (lc + lp) */
} probs_t;


static void preencher(uint16_t *p, uint32_t n) { for (uint32_t i = 0; i < n; i++) p[i] = UM / 2; }

/* devolve quantos bytes saíram (== tam se deu certo) */
uint32_t lzma_expandir(const uint8_t *s, uint32_t n, uint8_t *d, uint32_t tam, uint8_t props)
{
    uint32_t lc = props % 9, lp = (props / 9) % 5, pb = props / 45;
    if (pb > 4 || lc + lp > 12) return 0;
    uint32_t n_lit = 0x300u << (lc + lp);
    uint32_t bytes = sizeof(probs_t) + n_lit * 2;
    probs_t *P = kmalloc(bytes);
    if (!P) return 0;
    preencher((uint16_t *)P, bytes / 2);
    rc_t r = { s, s + n, 0xFFFFFFFFu, 0, 0 };
    rc_byte(&r);   /* o primeiro byte é sempre 0 */
    for (int i = 0; i < 4; i++) r.cod = (r.cod << 8) | rc_byte(&r);
    uint32_t pos = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0, estado = 0;
    uint32_t mpb = (1u << pb) - 1, mlp = (1u << lp) - 1;
    while (pos < tam && !r.erro) {
        uint32_t ps = pos & mpb;
        if (!rc_bit(&r, &P->eh_par[estado][ps])) {   /* literal */
            uint32_t ant = pos ? d[pos - 1] : 0;
            uint16_t *pl = P->lit + 0x300u * (((pos & mlp) << lc) + (ant >> (8 - lc)));
            uint32_t sim = 1;
            if (estado >= 7) {   /* depois de um par: "casado" com o byte da distância rep0 */
                uint32_t casa = pos > rep0 ? d[pos - rep0 - 1] : 0;
                while (sim < 0x100) {
                    uint32_t bc = (casa >> 7) & 1; casa <<= 1;
                    uint32_t b = (uint32_t)rc_bit(&r, &pl[((1 + bc) << 8) + sim]);
                    sim = (sim << 1) | b;
                    if (b != bc) break;
                }
            }
            while (sim < 0x100) sim = (sim << 1) | (uint32_t)rc_bit(&r, &pl[sim]);
            d[pos++] = (uint8_t)sim;
            estado = estado < 4 ? 0 : estado < 10 ? estado - 3 : estado - 6;
            continue;
        }
        uint32_t comp;
        if (rc_bit(&r, &P->eh_rep[estado])) {
            if (!pos) break;
            if (!rc_bit(&r, &P->eh_rep0[estado])) {
                if (!rc_bit(&r, &P->eh_rep0_longo[estado][ps])) {   /* um byte só, de rep0 */
                    estado = estado < 7 ? 9 : 11;
                    d[pos] = d[pos - rep0 - 1]; pos++;
                    continue;
                }
            } else {
                uint32_t dist;
                if (!rc_bit(&r, &P->eh_rep1[estado])) dist = rep1;
                else {
                    if (!rc_bit(&r, &P->eh_rep2[estado])) dist = rep2;
                    else { dist = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0; rep0 = dist;
            }
            comp = ler_comp(&r, &P->rep_comp, ps);
            estado = estado < 7 ? 8 : 11;
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            comp = ler_comp(&r, &P->comp, ps);
            estado = estado < 7 ? 7 : 10;
            uint32_t est_comp = comp < 4 ? comp : 3;
            uint32_t sl = arvore(&r, P->slot[est_comp], 6);
            if (sl < 4) rep0 = sl;
            else {
                uint32_t nd = (sl >> 1) - 1;
                rep0 = (2 | (sl & 1)) << nd;
                if (sl < 14) rep0 += arvore_inv(&r, P->especial + rep0 - sl, (int)nd);
                else {
                    rep0 += rc_direto(&r, (int)nd - 4) << 4;
                    rep0 += arvore_inv(&r, P->alinha, 4);
                    if (rep0 == 0xFFFFFFFFu) break;   /* marcador de fim */
                }
            }
            if (rep0 >= pos) break;   /* distância impossível: fluxo corrompido */
        }
        comp += 2;
        while (comp-- && pos < tam) { d[pos] = d[pos - rep0 - 1]; pos++; }
    }
    kfree(P);
    return pos;
}

/* Filtro BCJ x86, decodificação (o mesmo algoritmo do xz, simple/x86.c):
 * E8/E9 com endereço absoluto (byte alto 00 ou FF) volta a ser relativo. */
#define MSB_OK(b) ((b) == 0 || (b) == 0xFF)
void bcj_x86_desfazer(uint8_t *b, uint32_t n)
{
    static const uint8_t permitido[8] = { 1, 1, 1, 0, 1, 0, 0, 0 };
    static const uint32_t bit_de[8] = { 0, 1, 2, 2, 3, 3, 3, 3 };
    uint32_t ant_mask = 0, ant_pos = (uint32_t)-5;
    if (n < 5) return;
    uint32_t lim = n - 5, i = 0;
    while (i <= lim) {
        uint8_t c = b[i];
        if (c != 0xE8 && c != 0xE9) { i++; continue; }
        uint32_t desl = i - ant_pos;
        ant_pos = i;
        if (desl > 5) ant_mask = 0;
        else for (uint32_t k = 0; k < desl; k++) { ant_mask &= 0x77; ant_mask <<= 1; }
        c = b[i + 4];
        if (MSB_OK(c) && permitido[(ant_mask >> 1) & 7] && (ant_mask >> 1) < 0x10) {
            uint32_t src = (uint32_t)c << 24 | (uint32_t)b[i + 3] << 16 | (uint32_t)b[i + 2] << 8 | b[i + 1];
            uint32_t dst;
            for (;;) {
                dst = src - (i + 5);
                if (!ant_mask) break;
                uint32_t k = bit_de[(ant_mask >> 1) & 7];
                c = (uint8_t)(dst >> (24 - k * 8));
                if (!MSB_OK(c)) break;
                src = dst ^ ((1u << (32 - k * 8)) - 1);
            }
            b[i + 4] = (uint8_t)~(((dst >> 24) & 1) - 1);
            b[i + 3] = (uint8_t)(dst >> 16); b[i + 2] = (uint8_t)(dst >> 8); b[i + 1] = (uint8_t)dst;
            i += 5;
            ant_mask = 0;
        } else {
            i++;
            ant_mask |= 1;
            if (MSB_OK(c)) ant_mask |= 0x10;
        }
    }
}
