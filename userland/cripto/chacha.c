/* chacha.c - ChaCha20, Poly1305 e o AEAD que junta os dois (RFC 8439).
 *
 * O ChaCha20 é uma cifra de fluxo feita só de somas, giros e OU-exclusivos
 * de 32 bits: não tem tabela nem desvio, então é em tempo constante por
 * construção. O estado são 16 palavras: 4 constantes, 8 da chave, 1 de
 * contador e 3 de nonce. Vinte rodadas embaralham o estado, o estado
 * original é somado de volta e o resultado são 64 bytes de fluxo.
 *
 * O Poly1305 é a marca de autenticação: a mensagem, em pedaços de 16 bytes,
 * forma um polinômio que é avaliado no ponto r, módulo o primo 2^130 - 5; ao
 * resultado soma-se s. (r, s) é uma chave de uso único. */
#include "interno.h"

/* ------------------------------------------------------------ ChaCha20 */

static inline u32 girar_esq(u32 x, int n) { return (x << n) | (x >> (32 - n)); }

#define QUARTO(a, b, c, d)                       \
    do {                                         \
        a += b; d ^= a; d = girar_esq(d, 16);    \
        c += d; b ^= c; b = girar_esq(b, 12);    \
        a += b; d ^= a; d = girar_esq(d, 8);     \
        c += d; b ^= c; b = girar_esq(b, 7);     \
    } while (0)

static void chacha20_estado(u32 e[16], const u8 chave[32], u32 contador, const u8 nonce[12])
{
    /* "expand 32-byte k" lido como quatro números little-endian. */
    static const u8 frase[16] = {
        'e', 'x', 'p', 'a', 'n', 'd', ' ', '3', '2', '-', 'b', 'y', 't', 'e', ' ', 'k',
    };
    for (int i = 0; i < 4; i++) e[i] = ler32_le(frase + 4 * i);
    for (int i = 0; i < 8; i++) e[4 + i] = ler32_le(chave + 4 * i);
    e[12] = contador;
    for (int i = 0; i < 3; i++) e[13 + i] = ler32_le(nonce + 4 * i);
}

static void chacha20_misturar(const u32 e[16], u8 saida[64])
{
    u32 x[16];
    for (int i = 0; i < 16; i++) x[i] = e[i];
    /* Dez voltas duplas: uma nas colunas, uma nas diagonais. */
    for (int volta = 0; volta < 10; volta++) {
        QUARTO(x[0], x[4], x[8],  x[12]);
        QUARTO(x[1], x[5], x[9],  x[13]);
        QUARTO(x[2], x[6], x[10], x[14]);
        QUARTO(x[3], x[7], x[11], x[15]);
        QUARTO(x[0], x[5], x[10], x[15]);
        QUARTO(x[1], x[6], x[11], x[12]);
        QUARTO(x[2], x[7], x[8],  x[13]);
        QUARTO(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; i++) gravar32_le(saida + 4 * i, x[i] + e[i]);
    cri_zerar(x, sizeof x);
}

void chacha20_bloco(const u8 chave[32], u32 contador, const u8 nonce[12], u8 saida[64])
{
    u32 e[16];
    chacha20_estado(e, chave, contador, nonce);
    chacha20_misturar(e, saida);
    cri_zerar(e, sizeof e);
}

void chacha20_aplicar(const u8 chave[32], u32 contador, const u8 nonce[12],
                      const u8 *entrada, u8 *saida, u32 n)
{
    u32 e[16];
    u8 fluxo[64];
    chacha20_estado(e, chave, contador, nonce);
    while (n) {
        chacha20_misturar(e, fluxo);
        e[12]++;
        u32 parte = n < 64 ? n : 64;
        for (u32 i = 0; i < parte; i++) saida[i] = entrada[i] ^ fluxo[i];
        entrada += parte; saida += parte; n -= parte;
    }
    cri_zerar(e, sizeof e);
    cri_zerar(fluxo, sizeof fluxo);
}

/* ------------------------------------------------------------ Poly1305
 *
 * As contas são com números de 130 bits. Eles ficam em 5 pedaços de 26
 * bits: o produto de dois pedaços cabe folgado em 64 bits, e dá para somar
 * vários produtos antes de acertar os vai-uns. A redução é barata porque
 * 2^130 = 5 (mod p): o que passar do quinto pedaço volta para o começo
 * multiplicado por 5.
 *
 * Tudo é feito com as mesmas operações para qualquer valor: não há desvio
 * que dependa da chave ou da mensagem. */

#define M26 0x3ffffffu

void poly1305_iniciar(Poly1305 *p, const u8 chave[32])
{
    /* A RFC manda zerar alguns bits de r ("clamp"): os 4 de cima dos bytes
     * 3, 7, 11 e 15 e os 2 de baixo dos bytes 4, 8 e 12. */
    u32 t0 = ler32_le(chave) & 0x0fffffff;
    u32 t1 = ler32_le(chave + 4) & 0x0ffffffc;
    u32 t2 = ler32_le(chave + 8) & 0x0ffffffc;
    u32 t3 = ler32_le(chave + 12) & 0x0ffffffc;
    p->r[0] = t0 & M26;
    p->r[1] = ((t0 >> 26) | (t1 << 6)) & M26;
    p->r[2] = ((t1 >> 20) | (t2 << 12)) & M26;
    p->r[3] = ((t2 >> 14) | (t3 << 18)) & M26;
    p->r[4] = t3 >> 8;
    for (int i = 0; i < 5; i++) p->h[i] = 0;
    for (int i = 0; i < 4; i++) p->s[i] = ler32_le(chave + 16 + 4 * i);
    p->usado = 0;
}

/* h = (h + bloco + alto * 2^128) * r mod p. "alto" é 1 nos blocos cheios (o
 * bit que a RFC acrescenta depois do 16º byte) e 0 no bloco final, que já
 * traz o seu byte 01 embutido. */
static void poly1305_bloco(Poly1305 *p, const u8 b[16], u32 alto)
{
    u32 t0 = ler32_le(b), t1 = ler32_le(b + 4), t2 = ler32_le(b + 8), t3 = ler32_le(b + 12);
    u32 h0 = p->h[0] + (t0 & M26);
    u32 h1 = p->h[1] + (((t0 >> 26) | (t1 << 6)) & M26);
    u32 h2 = p->h[2] + (((t1 >> 20) | (t2 << 12)) & M26);
    u32 h3 = p->h[3] + (((t2 >> 14) | (t3 << 18)) & M26);
    u32 h4 = p->h[4] + ((t3 >> 8) | (alto << 24));
    u32 r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], r3 = p->r[3], r4 = p->r[4];
    /* Os termos que passariam de 2^130 já entram multiplicados por 5. */
    u32 q1 = r1 * 5, q2 = r2 * 5, q3 = r3 * 5, q4 = r4 * 5;

    u64 d0 = (u64)h0 * r0 + (u64)h1 * q4 + (u64)h2 * q3 + (u64)h3 * q2 + (u64)h4 * q1;
    u64 d1 = (u64)h0 * r1 + (u64)h1 * r0 + (u64)h2 * q4 + (u64)h3 * q3 + (u64)h4 * q2;
    u64 d2 = (u64)h0 * r2 + (u64)h1 * r1 + (u64)h2 * r0 + (u64)h3 * q4 + (u64)h4 * q3;
    u64 d3 = (u64)h0 * r3 + (u64)h1 * r2 + (u64)h2 * r1 + (u64)h3 * r0 + (u64)h4 * q4;
    u64 d4 = (u64)h0 * r4 + (u64)h1 * r3 + (u64)h2 * r2 + (u64)h3 * r1 + (u64)h4 * r0;

    /* Acerta os vai-uns; o que sobra no topo volta vezes 5. */
    d1 += d0 >> 26; d0 &= M26;
    d2 += d1 >> 26; d1 &= M26;
    d3 += d2 >> 26; d2 &= M26;
    d4 += d3 >> 26; d3 &= M26;
    d0 += (d4 >> 26) * 5; d4 &= M26;
    d1 += d0 >> 26; d0 &= M26;

    p->h[0] = (u32)d0; p->h[1] = (u32)d1; p->h[2] = (u32)d2; p->h[3] = (u32)d3; p->h[4] = (u32)d4;
}

void poly1305_atualizar(Poly1305 *p, const void *dados, u32 n)
{
    const u8 *d = dados;
    if (p->usado) {
        while (n && p->usado < 16) { p->bloco[p->usado++] = *d++; n--; }
        if (p->usado < 16) return;
        poly1305_bloco(p, p->bloco, 1);
        p->usado = 0;
    }
    while (n >= 16) { poly1305_bloco(p, d, 1); d += 16; n -= 16; }
    while (n) { p->bloco[p->usado++] = *d++; n--; }
}

void poly1305_finalizar(Poly1305 *p, u8 marca[POLY1305_MARCA])
{
    if (p->usado) {
        /* Bloco incompleto: um byte 01 logo depois dos dados e zeros. */
        p->bloco[p->usado++] = 1;
        while (p->usado < 16) p->bloco[p->usado++] = 0;
        poly1305_bloco(p, p->bloco, 0);
    }
    u32 h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4];
    /* Vai-uns até o fim: agora cada pedaço tem no máximo 26 bits. */
    h2 += h1 >> 26; h1 &= M26;
    h3 += h2 >> 26; h2 &= M26;
    h4 += h3 >> 26; h3 &= M26;
    h0 += (h4 >> 26) * 5; h4 &= M26;
    h1 += h0 >> 26; h0 &= M26;
    h2 += h1 >> 26; h1 &= M26;
    h3 += h2 >> 26; h2 &= M26;
    h4 += h3 >> 26; h3 &= M26;
    /* h está abaixo de 2^130, mas pode estar entre p e 2^130. Calcula
     * g = h + 5: se g passar de 2^130, então h >= p e o valor certo é
     * g - 2^130. A escolha é por máscara, sem desvio. */
    u32 g0 = h0 + 5;
    u32 g1 = h1 + (g0 >> 26); g0 &= M26;
    u32 g2 = h2 + (g1 >> 26); g1 &= M26;
    u32 g3 = h3 + (g2 >> 26); g2 &= M26;
    u32 g4 = h4 + (g3 >> 26); g3 &= M26;
    u32 m = 0u - (g4 >> 26);            /* tudo 1 se h >= p */
    g4 &= M26;
    h0 = (h0 & ~m) | (g0 & m);
    h1 = (h1 & ~m) | (g1 & m);
    h2 = (h2 & ~m) | (g2 & m);
    h3 = (h3 & ~m) | (g3 & m);
    h4 = (h4 & ~m) | (g4 & m);
    /* Remonta em 4 palavras de 32 bits e soma s, módulo 2^128. */
    u32 w0 = h0 | (h1 << 26);
    u32 w1 = (h1 >> 6) | (h2 << 20);
    u32 w2 = (h2 >> 12) | (h3 << 14);
    u32 w3 = (h3 >> 18) | (h4 << 8);
    u64 soma = (u64)w0 + p->s[0];
    gravar32_le(marca, (u32)soma);
    soma = (u64)w1 + p->s[1] + (soma >> 32);
    gravar32_le(marca + 4, (u32)soma);
    soma = (u64)w2 + p->s[2] + (soma >> 32);
    gravar32_le(marca + 8, (u32)soma);
    soma = (u64)w3 + p->s[3] + (soma >> 32);
    gravar32_le(marca + 12, (u32)soma);
    cri_zerar(p, sizeof *p);
}

void poly1305_calcular(const u8 chave[32], const void *dados, u32 n, u8 marca[POLY1305_MARCA])
{
    Poly1305 p;
    poly1305_iniciar(&p, chave);
    poly1305_atualizar(&p, dados, n);
    poly1305_finalizar(&p, marca);
}

/* ------------------------------------------------------------ AEAD
 *
 * A chave do Poly1305 é tirada do próprio ChaCha20: os primeiros 32 bytes do
 * bloco de contador 0. O texto é cifrado a partir do contador 1. A marca
 * cobre: dados extras, zeros até múltiplo de 16, texto cifrado, zeros até
 * múltiplo de 16, e os dois tamanhos em 8 bytes little-endian cada. */

static void aead_marca(const u8 chave[32], const u8 nonce[12], const u8 *extra, u32 nextra,
                       const u8 *cifrado, u32 n, u8 marca[16])
{
    static const u8 zeros[16] = { 0 };
    u8 bloco[64], tamanhos[16];
    Poly1305 p;
    chacha20_bloco(chave, 0, nonce, bloco);
    poly1305_iniciar(&p, bloco);
    poly1305_atualizar(&p, extra, nextra);
    poly1305_atualizar(&p, zeros, (16 - (nextra & 15)) & 15);
    poly1305_atualizar(&p, cifrado, n);
    poly1305_atualizar(&p, zeros, (16 - (n & 15)) & 15);
    gravar32_le(tamanhos, nextra); gravar32_le(tamanhos + 4, 0);
    gravar32_le(tamanhos + 8, n);  gravar32_le(tamanhos + 12, 0);
    poly1305_atualizar(&p, tamanhos, 16);
    poly1305_finalizar(&p, marca);
    cri_zerar(bloco, sizeof bloco);
}

void chacha20_poly1305_cifrar(const u8 chave[32], const u8 nonce[12], const u8 *extra, u32 nextra,
                              const u8 *claro, u32 n, u8 *cifrado, u8 marca[16])
{
    chacha20_aplicar(chave, 1, nonce, claro, cifrado, n);
    aead_marca(chave, nonce, extra, nextra, cifrado, n, marca);
}

int chacha20_poly1305_decifrar(const u8 chave[32], const u8 nonce[12], const u8 *extra, u32 nextra,
                               const u8 *cifrado, u32 n, const u8 marca[16], u8 *claro)
{
    u8 esperada[16];
    aead_marca(chave, nonce, extra, nextra, cifrado, n, esperada);
    int ruim = cri_diferentes(esperada, marca, 16);
    cri_zerar(esperada, sizeof esperada);
    if (ruim) {
        cri_zerar(claro, n);
        return CRIPTO_ERRO;
    }
    chacha20_aplicar(chave, 1, nonce, cifrado, claro, n);
    return CRIPTO_OK;
}
