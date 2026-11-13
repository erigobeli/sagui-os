/* resumos.c - SHA-256, SHA-384, SHA-512 (FIPS 180-4), HMAC (RFC 2104) e
 * HKDF (RFC 5869).
 *
 * Um resumo transforma qualquer quantidade de bytes num número de tamanho
 * fixo, de modo que ninguém consiga achar duas entradas com o mesmo
 * resultado. Os SHA-2 trabalham em blocos (64 bytes no SHA-256, 128 no
 * SHA-512): cada bloco é misturado ao estado por uma "compressão" de 64 ou
 * 80 rodadas. No fim entra o enchimento: um bit 1, zeros, e o tamanho da
 * mensagem em bits.
 *
 * As constantes das rodadas não são mágicas: são os primeiros bits da parte
 * fracionária das raízes cúbicas dos primeiros números primos, e os valores
 * iniciais, das raízes quadradas. As tabelas abaixo foram geradas por um
 * script com essa conta (com inteiros grandes) e conferem com a FIPS 180-4.
 *
 * Nada aqui depende de segredo para desviar ou indexar memória, então os
 * resumos (e o HMAC) correm em tempo que só depende do tamanho da entrada. */
#include "interno.h"

/* ------------------------------------------------------------ SHA-256 */

static const u32 K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline u32 girar32(u32 x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_comprimir(u32 h[8], const u8 *bloco)
{
    u32 w[64];
    for (int t = 0; t < 16; t++) w[t] = ler32_be(bloco + 4 * t);
    for (int t = 16; t < 64; t++) {
        u32 s0 = girar32(w[t - 15], 7) ^ girar32(w[t - 15], 18) ^ (w[t - 15] >> 3);
        u32 s1 = girar32(w[t - 2], 17) ^ girar32(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; t++) {
        u32 S1 = girar32(e, 6) ^ girar32(e, 11) ^ girar32(e, 25);
        u32 escolhe = (e & f) ^ (~e & g);
        u32 t1 = hh + S1 + escolhe + K256[t] + w[t];
        u32 S0 = girar32(a, 2) ^ girar32(a, 13) ^ girar32(a, 22);
        u32 maioria = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = S0 + maioria;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_iniciar(Sha256 *c)
{
    static const u32 inicio[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    for (int i = 0; i < 8; i++) c->h[i] = inicio[i];
    c->usado = 0;
    c->total = 0;
}

void sha256_atualizar(Sha256 *c, const void *dados, u32 n)
{
    const u8 *p = dados;
    c->total += n;
    /* Primeiro completa o bloco que estava pela metade, se houver. */
    if (c->usado) {
        while (n && c->usado < 64) { c->bloco[c->usado++] = *p++; n--; }
        if (c->usado < 64) return;
        sha256_comprimir(c->h, c->bloco);
        c->usado = 0;
    }
    /* Blocos inteiros vão direto da entrada, sem cópia. */
    while (n >= 64) { sha256_comprimir(c->h, p); p += 64; n -= 64; }
    while (n) { c->bloco[c->usado++] = *p++; n--; }
}

void sha256_finalizar(Sha256 *c, u8 saida[SHA256_TAM])
{
    u64 bits = c->total << 3;
    /* O bit 1, depois zeros até sobrarem exatamente 8 bytes para o tamanho.
     * Se não couber neste bloco, o enchimento transborda para mais um. */
    c->bloco[c->usado++] = 0x80;
    if (c->usado > 56) {
        while (c->usado < 64) c->bloco[c->usado++] = 0;
        sha256_comprimir(c->h, c->bloco);
        c->usado = 0;
    }
    while (c->usado < 56) c->bloco[c->usado++] = 0;
    gravar32_be(c->bloco + 56, (u32)(bits >> 32));
    gravar32_be(c->bloco + 60, (u32)bits);
    sha256_comprimir(c->h, c->bloco);
    for (int i = 0; i < 8; i++) gravar32_be(saida + 4 * i, c->h[i]);
    cri_zerar(c, sizeof *c);
}

void sha256(const void *dados, u32 n, u8 saida[SHA256_TAM])
{
    Sha256 c;
    sha256_iniciar(&c);
    sha256_atualizar(&c, dados, n);
    sha256_finalizar(&c, saida);
}

/* ------------------------------------------------------------ SHA-512 e SHA-384
 *
 * A mesma ideia do SHA-256 com palavras de 64 bits, 80 rodadas e outras
 * quantidades de giro. Num processador de 32 bits cada palavra ocupa dois
 * registradores; somar, girar e deslocar 64 bits o compilador faz sozinho,
 * sem pedir ajuda à biblioteca dele (só a divisão pediria). */

static const u64 K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static inline u64 girar64(u64 x, int n) { return (x >> n) | (x << (64 - n)); }

static void sha512_comprimir(u64 h[8], const u8 *bloco)
{
    u64 w[80];
    for (int t = 0; t < 16; t++)
        w[t] = ((u64)ler32_be(bloco + 8 * t) << 32) | ler32_be(bloco + 8 * t + 4);
    for (int t = 16; t < 80; t++) {
        u64 s0 = girar64(w[t - 15], 1) ^ girar64(w[t - 15], 8) ^ (w[t - 15] >> 7);
        u64 s1 = girar64(w[t - 2], 19) ^ girar64(w[t - 2], 61) ^ (w[t - 2] >> 6);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    u64 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 80; t++) {
        u64 S1 = girar64(e, 14) ^ girar64(e, 18) ^ girar64(e, 41);
        u64 escolhe = (e & f) ^ (~e & g);
        u64 t1 = hh + S1 + escolhe + K512[t] + w[t];
        u64 S0 = girar64(a, 28) ^ girar64(a, 34) ^ girar64(a, 39);
        u64 maioria = (a & b) ^ (a & c) ^ (b & c);
        u64 t2 = S0 + maioria;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha512_iniciar(Sha512 *c)
{
    static const u64 inicio[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
    };
    for (int i = 0; i < 8; i++) c->h[i] = inicio[i];
    c->usado = 0;
    c->total = 0;
    c->tam_saida = SHA512_TAM;
}

void sha384_iniciar(Sha512 *c)
{
    /* Raízes quadradas do nono ao décimo sexto primo. */
    static const u64 inicio[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL,
        0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
        0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
    };
    for (int i = 0; i < 8; i++) c->h[i] = inicio[i];
    c->usado = 0;
    c->total = 0;
    c->tam_saida = SHA384_TAM;
}

void sha512_atualizar(Sha512 *c, const void *dados, u32 n)
{
    const u8 *p = dados;
    c->total += n;
    if (c->usado) {
        while (n && c->usado < 128) { c->bloco[c->usado++] = *p++; n--; }
        if (c->usado < 128) return;
        sha512_comprimir(c->h, c->bloco);
        c->usado = 0;
    }
    while (n >= 128) { sha512_comprimir(c->h, p); p += 128; n -= 128; }
    while (n) { c->bloco[c->usado++] = *p++; n--; }
}

void sha512_finalizar(Sha512 *c, u8 *saida)
{
    /* O tamanho aqui ocupa 16 bytes. Contamos bytes em 64 bits, então em
     * bits o número tem até 67: os 3 de cima vão para a metade alta. */
    u64 bits_alto = c->total >> 61, bits = c->total << 3;
    c->bloco[c->usado++] = 0x80;
    if (c->usado > 112) {
        while (c->usado < 128) c->bloco[c->usado++] = 0;
        sha512_comprimir(c->h, c->bloco);
        c->usado = 0;
    }
    while (c->usado < 112) c->bloco[c->usado++] = 0;
    gravar32_be(c->bloco + 112, (u32)(bits_alto >> 32));
    gravar32_be(c->bloco + 116, (u32)bits_alto);
    gravar32_be(c->bloco + 120, (u32)(bits >> 32));
    gravar32_be(c->bloco + 124, (u32)bits);
    sha512_comprimir(c->h, c->bloco);
    u8 tudo[SHA512_TAM];
    for (int i = 0; i < 8; i++) {
        gravar32_be(tudo + 8 * i, (u32)(c->h[i] >> 32));
        gravar32_be(tudo + 8 * i + 4, (u32)c->h[i]);
    }
    cri_copiar(saida, tudo, c->tam_saida);
    cri_zerar(tudo, sizeof tudo);
    cri_zerar(c, sizeof *c);
}

void sha512(const void *dados, u32 n, u8 saida[SHA512_TAM])
{
    Sha512 c;
    sha512_iniciar(&c);
    sha512_atualizar(&c, dados, n);
    sha512_finalizar(&c, saida);
}

void sha384(const void *dados, u32 n, u8 saida[SHA384_TAM])
{
    Sha512 c;
    sha384_iniciar(&c);
    sha512_atualizar(&c, dados, n);
    sha512_finalizar(&c, saida);
}

/* ------------------------------------------------------------ resumo por número */

u32 resumo_tamanho(int tipo)
{
    switch (tipo) {
    case RESUMO_SHA256: return SHA256_TAM;
    case RESUMO_SHA384: return SHA384_TAM;
    case RESUMO_SHA512: return SHA512_TAM;
    }
    return 0;
}

u32 resumo_tam_bloco(int tipo)
{
    switch (tipo) {
    case RESUMO_SHA256: return 64;
    case RESUMO_SHA384:
    case RESUMO_SHA512: return 128;
    }
    return 0;
}

void resumo_iniciar(Resumo *c, int tipo)
{
    c->tipo = tipo;
    if (tipo == RESUMO_SHA256) sha256_iniciar(&c->u.s256);
    else if (tipo == RESUMO_SHA384) sha384_iniciar(&c->u.s512);
    else sha512_iniciar(&c->u.s512);
}

void resumo_atualizar(Resumo *c, const void *dados, u32 n)
{
    if (c->tipo == RESUMO_SHA256) sha256_atualizar(&c->u.s256, dados, n);
    else sha512_atualizar(&c->u.s512, dados, n);
}

void resumo_finalizar(Resumo *c, u8 *saida)
{
    if (c->tipo == RESUMO_SHA256) sha256_finalizar(&c->u.s256, saida);
    else sha512_finalizar(&c->u.s512, saida);
}

void resumo_calcular(int tipo, const void *dados, u32 n, u8 *saida)
{
    Resumo c;
    resumo_iniciar(&c, tipo);
    resumo_atualizar(&c, dados, n);
    resumo_finalizar(&c, saida);
}

/* ------------------------------------------------------------ HMAC
 *
 * HMAC(K, m) = H((K ^ opad) || H((K ^ ipad) || m)): o resumo aplicado duas
 * vezes, com a chave misturada de dois jeitos diferentes. A chave é antes
 * ajustada ao tamanho do bloco: se for maior, troca-se pelo resumo dela; se
 * for menor, completa-se com zeros.
 *
 * O contexto guarda os dois resumos já "abertos" com a chave: o de dentro
 * recebe a mensagem e o de fora só espera o resultado do de dentro. Assim a
 * chave em si não fica guardada em lugar nenhum. */

void hmac_iniciar(Hmac *c, int tipo, const u8 *chave, u32 nchave)
{
    u8 k[128], mistura[128];
    u32 bloco = resumo_tam_bloco(tipo);
    for (u32 i = 0; i < bloco; i++) k[i] = 0;
    if (nchave > bloco) resumo_calcular(tipo, chave, nchave, k);
    else cri_copiar(k, chave, nchave);

    for (u32 i = 0; i < bloco; i++) mistura[i] = k[i] ^ 0x36;
    resumo_iniciar(&c->dentro, tipo);
    resumo_atualizar(&c->dentro, mistura, bloco);

    for (u32 i = 0; i < bloco; i++) mistura[i] = k[i] ^ 0x5c;
    resumo_iniciar(&c->fora, tipo);
    resumo_atualizar(&c->fora, mistura, bloco);

    cri_zerar(k, sizeof k);
    cri_zerar(mistura, sizeof mistura);
}

void hmac_atualizar(Hmac *c, const void *dados, u32 n)
{
    resumo_atualizar(&c->dentro, dados, n);
}

void hmac_finalizar(Hmac *c, u8 *saida)
{
    u8 meio[RESUMO_TAM_MAX];
    u32 tam = resumo_tamanho(c->fora.tipo);
    resumo_finalizar(&c->dentro, meio);
    resumo_atualizar(&c->fora, meio, tam);
    resumo_finalizar(&c->fora, saida);
    cri_zerar(meio, sizeof meio);
}

void hmac_calcular(int tipo, const u8 *chave, u32 nchave, const void *dados, u32 n, u8 *saida)
{
    Hmac c;
    hmac_iniciar(&c, tipo, chave, nchave);
    hmac_atualizar(&c, dados, n);
    hmac_finalizar(&c, saida);
}

void hmac_sha256(const u8 *chave, u32 nchave, const void *dados, u32 n, u8 saida[32])
{
    hmac_calcular(RESUMO_SHA256, chave, nchave, dados, n, saida);
}

void hmac_sha384(const u8 *chave, u32 nchave, const void *dados, u32 n, u8 saida[48])
{
    hmac_calcular(RESUMO_SHA384, chave, nchave, dados, n, saida);
}

/* ------------------------------------------------------------ HKDF
 *
 * Deriva chaves em dois passos. "Extrair" concentra um segredo de qualidade
 * irregular (o resultado do X25519, por exemplo) numa chave uniforme:
 * PRK = HMAC(sal, material). "Expandir" estica essa chave no tamanho que se
 * queira, encadeando HMACs:
 *     T(1) = HMAC(PRK, info || 01)
 *     T(i) = HMAC(PRK, T(i-1) || info || i)
 * e a saída é T(1) || T(2) || ... cortada no tamanho pedido. */

void hkdf_extrair(int tipo, const u8 *sal, u32 nsal, const u8 *material, u32 nmaterial, u8 *prk)
{
    /* Sem sal, a RFC manda usar um bloco de zeros do tamanho do resumo; no
     * HMAC isso dá no mesmo que a chave vazia, que é completada com zeros. */
    hmac_calcular(tipo, sal, nsal, material, nmaterial, prk);
}

int hkdf_expandir(int tipo, const u8 *prk, u32 nprk, const u8 *info, u32 ninfo, u8 *saida, u32 nsaida)
{
    u32 tam = resumo_tamanho(tipo);
    if (tam == 0 || nsaida > 255 * tam) return CRIPTO_ERRO;

    Hmac base, c;
    u8 t[RESUMO_TAM_MAX];
    u8 indice = 0;
    u32 feito = 0;
    /* A chave é a mesma em todas as voltas: abre o HMAC uma vez e copia. */
    hmac_iniciar(&base, tipo, prk, nprk);
    while (feito < nsaida) {
        indice++;
        cri_copiar(&c, &base, sizeof c);
        if (indice > 1) hmac_atualizar(&c, t, tam);
        hmac_atualizar(&c, info, ninfo);
        hmac_atualizar(&c, &indice, 1);
        hmac_finalizar(&c, t);
        u32 parte = nsaida - feito < tam ? nsaida - feito : tam;
        cri_copiar(saida + feito, t, parte);
        feito += parte;
    }
    cri_zerar(&base, sizeof base);
    cri_zerar(t, sizeof t);
    return CRIPTO_OK;
}
