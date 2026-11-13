/* numeros.c - números grandes, potência modular e conferência de assinatura
 * RSA (RFC 8017).
 *
 * Uma assinatura RSA é um número s tal que s^e mod n (com (n, e) a chave
 * pública) devolve um bloco montado a partir do resumo da mensagem.
 * Conferir é fazer essa potência e ver se o bloco é o esperado. Só há
 * conferência aqui: o SaguiOS não tem chave RSA própria. Como tudo o que
 * entra é público (chave, assinatura, resumo), estas rotinas NÃO são em
 * tempo constante, e isso está certo; não as use com segredos.
 *
 * A MULTIPLICAÇÃO DE MONTGOMERY. Calcular (a*b) mod n do jeito direto pede
 * uma divisão de números enormes a cada passo. Montgomery troca a divisão
 * por n por uma divisão por R = 2^(32k), que é só descartar palavras. Cada
 * número x é guardado como x*R mod n (a "forma de Montgomery"). A operação
 * básica devolve a*b/R mod n; aplicada a dois números na forma, o resultado
 * continua na forma: (xR)(yR)/R = xyR.
 *
 * Para dividir por 2^32 sem resto, soma-se antes um múltiplo de n que zere
 * a palavra de baixo: m = t[0] * n0, onde n0 = -1/n mod 2^32. Então
 * t + m*n termina em 32 zeros e pode ser deslocado uma palavra. Fazendo
 * isso uma vez para cada palavra de b, intercalado com a soma de a*b[i], o
 * resultado nunca passa de 2n e no fim basta uma subtração.
 *
 * n0 sai pelo método de Newton, que dobra os bits certos a cada volta. E
 * R^2 mod n (necessário para entrar na forma) sai sem divisão nenhuma:
 * começa em 1 e dobra módulo n 64k vezes. */
#include "interno.h"

/* ------------------------------------------------------------ contas simples */

/* Bytes (o mais significativo primeiro) para palavras. Bytes que sobrarem à
 * esquerda são ignorados: quem chama garante antes que são zeros. */
void num_de_bytes(u32 *r, u32 k, const u8 *b, u32 nb)
{
    for (u32 i = 0; i < k; i++) r[i] = 0;
    for (u32 i = 0; i < nb && i < 4 * k; i++)
        r[i >> 2] |= (u32)b[nb - 1 - i] << (8 * (i & 3));
}

void num_para_bytes(u8 *b, u32 nb, const u32 *a, u32 k)
{
    for (u32 i = 0; i < nb; i++)
        b[nb - 1 - i] = i < 4 * k ? (u8)(a[i >> 2] >> (8 * (i & 3))) : 0;
}

int num_comparar(const u32 *a, const u32 *b, u32 k)
{
    for (u32 i = k; i-- > 0; ) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

int num_e_zero(const u32 *a, u32 k)
{
    u32 junta = 0;
    for (u32 i = 0; i < k; i++) junta |= a[i];
    return junta == 0;
}

void num_copiar(u32 *r, const u32 *a, u32 k)
{
    for (u32 i = 0; i < k; i++) r[i] = a[i];
}

u32 num_somar(u32 *r, const u32 *a, const u32 *b, u32 k)
{
    u32 vai = 0;
    for (u32 i = 0; i < k; i++) {
        u64 s = (u64)a[i] + b[i] + vai;
        r[i] = (u32)s;
        vai = (u32)(s >> 32);
    }
    return vai;
}

u32 num_subtrair(u32 *r, const u32 *a, const u32 *b, u32 k)
{
    u32 emprestimo = 0;
    for (u32 i = 0; i < k; i++) {
        u64 d = (u64)a[i] - b[i] - emprestimo;
        r[i] = (u32)d;
        emprestimo = (u32)(d >> 63);
    }
    return emprestimo;
}

/* ------------------------------------------------------------ Montgomery */

void mont_somar(const Montgomery *m, u32 *r, const u32 *a, const u32 *b)
{
    u32 vai = num_somar(r, a, b, m->k);
    if (vai || num_comparar(r, m->n, m->k) >= 0) num_subtrair(r, r, m->n, m->k);
}

void mont_subtrair(const Montgomery *m, u32 *r, const u32 *a, const u32 *b)
{
    if (num_subtrair(r, a, b, m->k)) num_somar(r, r, m->n, m->k);
}

int mont_iniciar(Montgomery *m, const u32 *modulo, u32 k)
{
    if (k == 0 || k > NUM_PALAVRAS) return CRIPTO_ERRO;
    if ((modulo[0] & 1) == 0) return CRIPTO_ERRO;       /* só módulo ímpar tem 1/n mod 2^32 */
    if (num_e_zero(modulo + 1, k - 1) && modulo[0] == 1) return CRIPTO_ERRO;
    m->k = k;
    num_copiar(m->n, modulo, k);

    /* Inverso de n[0] módulo 2^32. Para n ímpar, n*n = 1 (mod 8), então o
     * próprio n já é o inverso com 3 bits certos; cada volta de
     * x = x*(2 - n*x) dobra esse número: 6, 12, 24, 48. */
    u32 x = modulo[0];
    for (int i = 0; i < 4; i++) x *= 2 - modulo[0] * x;
    m->n0 = 0u - x;

    /* R mod n e R^2 mod n por dobras: 32k dobras a partir de 1 dão R, mais
     * 32k dão R^2. Dobrar módulo n: desloca um bit e, se passou de n (ou
     * caiu um bit pela ponta), tira n. */
    u32 *r = m->r2;
    for (u32 i = 0; i < k; i++) r[i] = 0;
    r[0] = 1;
    for (u32 volta = 0; volta < 64 * k; volta++) {
        u32 caiu = 0;
        for (u32 i = 0; i < k; i++) {
            u32 novo = (r[i] << 1) | caiu;
            caiu = r[i] >> 31;
            r[i] = novo;
        }
        if (caiu || num_comparar(r, m->n, k) >= 0) num_subtrair(r, r, m->n, k);
        if (volta == 32 * k - 1) num_copiar(m->um, r, k);
    }
    return CRIPTO_OK;
}

/* r = a*b/R mod n. Pode ter r igual a "a" ou a "b". */
void mont_multiplicar(const Montgomery *m, u32 *r, const u32 *a, const u32 *b)
{
    u32 k = m->k;
    u32 t[NUM_PALAVRAS + 2];
    for (u32 i = 0; i < k + 2; i++) t[i] = 0;

    for (u32 i = 0; i < k; i++) {
        /* t += a * b[i] */
        u32 bi = b[i], vai = 0;
        for (u32 j = 0; j < k; j++) {
            u64 p = (u64)a[j] * bi + t[j] + vai;
            t[j] = (u32)p;
            vai = (u32)(p >> 32);
        }
        u64 s = (u64)t[k] + vai;
        t[k] = (u32)s;
        t[k + 1] = (u32)(s >> 32);

        /* t += q*n, com q escolhido para zerar t[0]; e desloca uma palavra. */
        u32 q = t[0] * m->n0;
        u64 p = (u64)q * m->n[0] + t[0];
        vai = (u32)(p >> 32);
        for (u32 j = 1; j < k; j++) {
            p = (u64)q * m->n[j] + t[j] + vai;
            t[j - 1] = (u32)p;
            vai = (u32)(p >> 32);
        }
        s = (u64)t[k] + vai;
        t[k - 1] = (u32)s;
        t[k] = t[k + 1] + (u32)(s >> 32);
    }
    if (t[k] || num_comparar(t, m->n, k) >= 0) num_subtrair(r, t, m->n, k);
    else num_copiar(r, t, k);
}

void mont_entrar(const Montgomery *m, u32 *r, const u32 *a)
{
    mont_multiplicar(m, r, a, m->r2);
}

void mont_sair(const Montgomery *m, u32 *r, const u32 *a)
{
    u32 um[NUM_PALAVRAS];
    for (u32 i = 0; i < m->k; i++) um[i] = 0;
    um[0] = 1;
    mont_multiplicar(m, r, a, um);
}

/* r = base^expoente, tudo na forma de Montgomery (menos o expoente, que é
 * um número comum de kexp palavras). Quadrados e multiplicações, do bit
 * mais alto do expoente para o mais baixo. */
void mont_potencia(const Montgomery *m, u32 *r, const u32 *base, const u32 *expoente, u32 kexp)
{
    u32 acum[NUM_PALAVRAS];
    num_copiar(acum, m->um, m->k);
    int comecou = 0;
    for (u32 i = 32 * kexp; i-- > 0; ) {
        u32 bit = (expoente[i >> 5] >> (i & 31)) & 1;
        if (comecou) mont_multiplicar(m, acum, acum, acum);
        if (bit) {
            mont_multiplicar(m, acum, acum, base);
            comecou = 1;
        }
    }
    num_copiar(r, acum, m->k);
}

/* ------------------------------------------------------------ potência com bytes */

/* Pula os zeros à esquerda de um número em bytes. */
static void sem_zeros(const u8 **b, u32 *n)
{
    while (*n && **b == 0) { (*b)++; (*n)--; }
}

int exp_modular(const u8 *base, u32 nbase, const u8 *expoente, u32 nexpoente,
                const u8 *modulo, u32 nmodulo, u8 *saida)
{
    const u8 *mod = modulo;
    u32 nmod = nmodulo;
    sem_zeros(&mod, &nmod);
    sem_zeros(&base, &nbase);
    sem_zeros(&expoente, &nexpoente);
    if (nmod == 0 || nmod > RSA_BITS_MAX / 8) return CRIPTO_ERRO;
    if (nbase > nmod || nexpoente > RSA_BITS_MAX / 8) return CRIPTO_ERRO;

    /* O contexto e os números somam uns 4 KB: cabem na pilha. */
    Montgomery m;
    u32 k = (nmod + 3) / 4, kexp = (nexpoente + 3) / 4;
    u32 x[NUM_PALAVRAS], e[NUM_PALAVRAS];
    num_de_bytes(x, k, mod, nmod);
    if (mont_iniciar(&m, x, k) != CRIPTO_OK) return CRIPTO_ERRO;
    num_de_bytes(x, k, base, nbase);
    if (num_comparar(x, m.n, k) >= 0) return CRIPTO_ERRO;
    num_de_bytes(e, kexp ? kexp : 1, expoente, nexpoente);

    mont_entrar(&m, x, x);
    mont_potencia(&m, x, x, e, kexp);
    mont_sair(&m, x, x);
    num_para_bytes(saida, nmodulo, x, k);
    return CRIPTO_OK;
}

/* ------------------------------------------------------------ RSA */

#define RSA_BYTES_MAX (RSA_BITS_MAX / 8)

/* A operação pública: bloco = assinatura^e mod n. Devolve em *nbloco o
 * tamanho do módulo em bytes e em *bits o tamanho dele em bits. */
static int rsa_abrir(const u8 *n, u32 nn, const u8 *e, u32 ne,
                     const u8 *assinatura, u32 nassinatura, u8 *bloco, u32 *nbloco, u32 *bits)
{
    sem_zeros(&n, &nn);
    /* Abaixo de 1024 bits não é chave que se aceite hoje. */
    if (nn < 128 || nn > RSA_BYTES_MAX) return CRIPTO_ERRO;
    /* A assinatura tem exatamente o tamanho do módulo (RFC 8017, 8.2.2). */
    if (nassinatura != nn) return CRIPTO_ERRO;
    const u8 *ee = e;
    u32 nee = ne;
    sem_zeros(&ee, &nee);
    /* Expoente 1 devolveria a própria assinatura: quem forja escolheria o
     * bloco que quisesse. Expoente par não existe em RSA. */
    if (nee == 0 || (ee[nee - 1] & 1) == 0 || (nee == 1 && ee[0] == 1)) return CRIPTO_ERRO;
    if (exp_modular(assinatura, nassinatura, ee, nee, n, nn, bloco) != CRIPTO_OK) return CRIPTO_ERRO;
    *nbloco = nn;
    u32 b = 8 * nn;
    for (u8 topo = n[0]; (topo & 0x80) == 0; topo <<= 1) b--;
    *bits = b;
    return CRIPTO_OK;
}

/* O cabeçalho DER que diz qual resumo foi usado (as "DigestInfo" da nota 1
 * da seção 9.2 da RFC 8017). Só muda o byte do algoritmo e os tamanhos. */
static u32 rsa_cabecalho_resumo(int tipo, u8 cab[19])
{
    static const u8 modelo[19] = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
        0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20,
    };
    u32 tam = resumo_tamanho(tipo);
    if (tam == 0) return 0;
    cri_copiar(cab, modelo, 19);
    cab[1] = (u8)(17 + tam);
    cab[14] = (u8)tipo;             /* 1, 2, 3: SHA-256, SHA-384, SHA-512 */
    cab[18] = (u8)tam;
    return 19;
}

/* PKCS#1 v1.5: o bloco tem de ser exatamente
 *     00 01 FF ... FF 00 || cabeçalho || resumo
 * com pelo menos 8 bytes FF. Em vez de interpretar o bloco recebido (onde
 * moram os erros clássicos que deixam forjar assinaturas), montamos o bloco
 * esperado e comparamos tudo. */
int rsa_verificar_pkcs1(const u8 *n, u32 nn, const u8 *e, u32 ne, int tipo,
                        const u8 *resumo, const u8 *assinatura, u32 nassinatura)
{
    u8 bloco[RSA_BYTES_MAX], esperado[RSA_BYTES_MAX], cab[19];
    u32 k, bits;
    u32 tam = resumo_tamanho(tipo);
    u32 ncab = rsa_cabecalho_resumo(tipo, cab);
    if (ncab == 0) return CRIPTO_ERRO;
    if (rsa_abrir(n, nn, e, ne, assinatura, nassinatura, bloco, &k, &bits) != CRIPTO_OK)
        return CRIPTO_ERRO;
    if (k < ncab + tam + 11) return CRIPTO_ERRO;

    u32 enchimento = k - 3 - ncab - tam;
    esperado[0] = 0;
    esperado[1] = 1;
    for (u32 i = 0; i < enchimento; i++) esperado[2 + i] = 0xff;
    esperado[2 + enchimento] = 0;
    cri_copiar(esperado + 3 + enchimento, cab, ncab);
    cri_copiar(esperado + 3 + enchimento + ncab, resumo, tam);
    return cri_diferentes(bloco, esperado, k) ? CRIPTO_ERRO : CRIPTO_OK;
}

/* MGF1: estica uma semente em "n" bytes de máscara, concatenando
 * H(semente || contador de 4 bytes) para contador = 0, 1, 2... e já
 * aplicando a máscara (OU-exclusivo) sobre "alvo". */
static void mgf1_aplicar(int tipo, const u8 *semente, u32 nsemente, u8 *alvo, u32 n)
{
    u8 h[RESUMO_TAM_MAX], contador[4];
    u32 tam = resumo_tamanho(tipo);
    for (u32 c = 0, feito = 0; feito < n; c++) {
        Resumo r;
        gravar32_be(contador, c);
        resumo_iniciar(&r, tipo);
        resumo_atualizar(&r, semente, nsemente);
        resumo_atualizar(&r, contador, 4);
        resumo_finalizar(&r, h);
        for (u32 i = 0; i < tam && feito < n; i++) alvo[feito++] ^= h[i];
    }
}

/* PSS (RFC 8017, 9.1.2). O bloco EM tem emBits = bits do módulo - 1 e é
 *     DB mascarado || H || BC
 * onde DB = zeros || 01 || sal, a máscara é MGF1(H), e
 * H = resumo(8 bytes zero || resumo da mensagem || sal). Aqui o sal tem
 * sempre o tamanho do resumo, que é o que o TLS 1.3 usa. */
int rsa_verificar_pss(const u8 *n, u32 nn, const u8 *e, u32 ne, int tipo,
                      const u8 *resumo, const u8 *assinatura, u32 nassinatura)
{
    u8 bloco[RSA_BYTES_MAX], h2[RESUMO_TAM_MAX];
    u32 k, bits;
    u32 tam = resumo_tamanho(tipo);
    if (tam == 0) return CRIPTO_ERRO;
    if (rsa_abrir(n, nn, e, ne, assinatura, nassinatura, bloco, &k, &bits) != CRIPTO_OK)
        return CRIPTO_ERRO;

    u32 em_bits = bits - 1;
    u32 em_tam = (em_bits + 7) / 8;
    u8 *em = bloco;
    /* Quando o módulo tem 8j+1 bits, EM tem um byte a menos que o módulo, e
     * o byte que sobra na frente tem de ser zero. */
    if (em_tam < k) {
        if (bloco[0] != 0) return CRIPTO_ERRO;
        em = bloco + 1;
    }
    if (em_tam < 2 * tam + 2) return CRIPTO_ERRO;
    if (em[em_tam - 1] != 0xbc) return CRIPTO_ERRO;

    u32 ndb = em_tam - tam - 1;
    u8 *db = em, *h = em + ndb;
    /* Os bits que sobram à esquerda (EM tem emBits, não 8*em_tam) são zero. */
    u8 sobra = (u8)(0xff << (8 - (8 * em_tam - em_bits)));
    if (8 * em_tam == em_bits) sobra = 0;
    if (db[0] & sobra) return CRIPTO_ERRO;

    mgf1_aplicar(tipo, h, tam, db, ndb);
    db[0] &= (u8)~sobra;

    u32 zeros = ndb - tam - 1;
    u32 ruim = 0;
    for (u32 i = 0; i < zeros; i++) ruim |= db[i];
    ruim |= (u32)(db[zeros] ^ 1);
    if (ruim) return CRIPTO_ERRO;

    static const u8 oito_zeros[8] = { 0 };
    Resumo r;
    resumo_iniciar(&r, tipo);
    resumo_atualizar(&r, oito_zeros, 8);
    resumo_atualizar(&r, resumo, tam);
    resumo_atualizar(&r, db + zeros + 1, tam);
    resumo_finalizar(&r, h2);
    return cri_diferentes(h, h2, tam) ? CRIPTO_ERRO : CRIPTO_OK;
}
