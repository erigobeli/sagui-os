/* cripto.h - a biblioteca de criptografia do SaguiOS.
 *
 * Tudo aqui foi escrito neste projeto, a partir das especificações (FIPS
 * 180-4, FIPS 197, NIST SP 800-38D, RFC 2104, 5869, 8439, 7748, 8032, 8017 e
 * FIPS 186). Serve ao cliente TLS 1.3 e ao cliente SSH do sistema.
 *
 * AVISO: criptografia própria não é auditada. Ela é conferida com os vetores
 * oficiais e faz em tempo constante as contas que mexem com segredos, mas
 * serve para o projeto (abrir páginas, SSH), não para banco.
 *
 * A biblioteca não depende de nada: nem da libsagui, nem do núcleo, nem da
 * biblioteca do compilador. Por isso ela define os próprios tipos inteiros
 * e as próprias rotinas de copiar e zerar. Também não reserva memória: quem
 * chama passa os contextos e os buffers.
 *
 * Convenção de retorno: as funções que podem falhar devolvem CRIPTO_OK (0)
 * quando deu certo e CRIPTO_ERRO (-1) quando não deu. Isso vale também para
 * as de "verificar": 0 quer dizer assinatura válida. Compare sempre com
 * CRIPTO_OK, nunca use o valor direto num "if".
 *
 * Os números grandes entram e saem como bytes na ordem da rede (o mais
 * significativo primeiro), que é como chegam nos certificados; as curvas
 * 25519 usam a ordem inversa, como mandam as RFCs delas. */
#ifndef CRIPTO_H
#define CRIPTO_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

#define CRIPTO_OK    0
#define CRIPTO_ERRO  (-1)

/* ------------------------------------------------------------ utilidades */

void cri_copiar(void *destino, const void *origem, u32 n);
/* Zera de um jeito que o compilador não pode jogar fora (para apagar
 * segredos da pilha antes de voltar). */
void cri_zerar(void *p, u32 n);
/* Compara sem parar no primeiro byte diferente: o tempo não revela onde
 * está a diferença. Devolve 0 se são iguais, outro valor se não são. */
int  cri_diferentes(const void *a, const void *b, u32 n);

/* ------------------------------------------------------------ resumos */

#define SHA256_TAM  32
#define SHA384_TAM  48
#define SHA512_TAM  64

typedef struct {
    u32 h[8];
    u8  bloco[64];
    u32 usado;          /* bytes esperando no bloco */
    u64 total;          /* bytes já recebidos */
} Sha256;

void sha256_iniciar(Sha256 *c);
void sha256_atualizar(Sha256 *c, const void *dados, u32 n);
void sha256_finalizar(Sha256 *c, u8 saida[SHA256_TAM]);
void sha256(const void *dados, u32 n, u8 saida[SHA256_TAM]);

/* O SHA-384 é o SHA-512 com outros valores iniciais e a saída cortada: os
 * dois usam o mesmo contexto. */
typedef struct {
    u64 h[8];
    u8  bloco[128];
    u32 usado;
    u32 tam_saida;      /* 48 ou 64 */
    u64 total;
} Sha512;

void sha512_iniciar(Sha512 *c);
void sha384_iniciar(Sha512 *c);
void sha512_atualizar(Sha512 *c, const void *dados, u32 n);
void sha512_finalizar(Sha512 *c, u8 *saida);      /* 64 bytes (ou 48 no SHA-384) */
#define sha384_atualizar sha512_atualizar
#define sha384_finalizar sha512_finalizar
void sha512(const void *dados, u32 n, u8 saida[SHA512_TAM]);
void sha384(const void *dados, u32 n, u8 saida[SHA384_TAM]);

/* Resumo "genérico": o TLS escolhe o resumo na hora, conforme o conjunto de
 * cifras, então o HMAC, o HKDF e as assinaturas recebem o tipo por número. */
#define RESUMO_SHA256   1
#define RESUMO_SHA384   2
#define RESUMO_SHA512   3
#define RESUMO_TAM_MAX  64

typedef struct {
    int tipo;
    union { Sha256 s256; Sha512 s512; } u;
} Resumo;

u32  resumo_tamanho(int tipo);          /* bytes da saída; 0 se o tipo não existe */
u32  resumo_tam_bloco(int tipo);        /* bytes do bloco interno (64 ou 128) */
void resumo_iniciar(Resumo *c, int tipo);
void resumo_atualizar(Resumo *c, const void *dados, u32 n);
void resumo_finalizar(Resumo *c, u8 *saida);
void resumo_calcular(int tipo, const void *dados, u32 n, u8 *saida);

/* ------------------------------------------------------------ HMAC e HKDF */

typedef struct {
    Resumo dentro;      /* já com a chave^ipad absorvida */
    Resumo fora;        /* já com a chave^opad absorvida */
} Hmac;

void hmac_iniciar(Hmac *c, int tipo, const u8 *chave, u32 nchave);
void hmac_atualizar(Hmac *c, const void *dados, u32 n);
void hmac_finalizar(Hmac *c, u8 *saida);
void hmac_calcular(int tipo, const u8 *chave, u32 nchave, const void *dados, u32 n, u8 *saida);
void hmac_sha256(const u8 *chave, u32 nchave, const void *dados, u32 n, u8 saida[32]);
void hmac_sha384(const u8 *chave, u32 nchave, const void *dados, u32 n, u8 saida[48]);

/* HKDF (RFC 5869). "prk" tem o tamanho do resumo. O expandir falha se pedirem
 * mais que 255 vezes o tamanho do resumo. */
void hkdf_extrair(int tipo, const u8 *sal, u32 nsal, const u8 *material, u32 nmaterial, u8 *prk);
int  hkdf_expandir(int tipo, const u8 *prk, u32 nprk, const u8 *info, u32 ninfo, u8 *saida, u32 nsaida);

/* ------------------------------------------------------------ AES */

/* As chaves de rodada ficam guardadas "fatiadas em bits" (ver aes.c): oito
 * palavras por rodada, uma para cada bit dos bytes do estado. */
typedef struct {
    u32 rodadas;        /* 10 (AES-128) ou 14 (AES-256) */
    u32 chaves[15][8];
} Aes;

int  aes_iniciar(Aes *a, const u8 *chave, u32 nchave);     /* nchave: 16 ou 32 */
void aes_cifrar_bloco(const Aes *a, const u8 entrada[16], u8 saida[16]);

/* Modo contador (SSH aes128-ctr/aes256-ctr): o bloco contador inteiro é um
 * número de 128 bits que cresce de um em um. O contexto lembra onde parou,
 * então dá para cifrar aos pedaços. Cifrar e decifrar são a mesma conta. */
typedef struct {
    Aes aes;
    u8  contador[16];
    u8  fluxo[16];
    u32 usado;          /* bytes do fluxo já gastos (16 = nenhum sobrando) */
} AesCtr;

int  aes_ctr_iniciar(AesCtr *c, const u8 *chave, u32 nchave, const u8 contador[16]);
void aes_ctr_aplicar(AesCtr *c, const u8 *entrada, u8 *saida, u32 n);

/* AES-GCM (SP 800-38D), marca de 16 bytes. "saída" pode ser o mesmo buffer
 * da entrada. O decifrar confere a marca antes de entregar qualquer coisa:
 * se ela não bate, devolve CRIPTO_ERRO e zera a saída. */
#define GCM_MARCA 16

typedef struct {
    Aes aes;
    u32 h[4];           /* a chave do GHASH: o bloco zero cifrado */
} AesGcm;

int  aes_gcm_iniciar(AesGcm *g, const u8 *chave, u32 nchave);
void aes_gcm_cifrar(const AesGcm *g, const u8 *nonce, u32 nnonce, const u8 *extra, u32 nextra,
                    const u8 *claro, u32 n, u8 *cifrado, u8 marca[GCM_MARCA]);
int  aes_gcm_decifrar(const AesGcm *g, const u8 *nonce, u32 nnonce, const u8 *extra, u32 nextra,
                      const u8 *cifrado, u32 n, const u8 marca[GCM_MARCA], u8 *claro);

/* ------------------------------------------------------------ ChaCha20 e Poly1305 */

void chacha20_bloco(const u8 chave[32], u32 contador, const u8 nonce[12], u8 saida[64]);
void chacha20_aplicar(const u8 chave[32], u32 contador, const u8 nonce[12],
                      const u8 *entrada, u8 *saida, u32 n);

typedef struct {
    u32 r[5];           /* a parte "r" da chave, em pedaços de 26 bits */
    u32 h[5];           /* o acumulador */
    u32 s[4];           /* a parte "s" da chave, somada no fim */
    u8  bloco[16];
    u32 usado;
} Poly1305;

#define POLY1305_MARCA 16

void poly1305_iniciar(Poly1305 *p, const u8 chave[32]);
void poly1305_atualizar(Poly1305 *p, const void *dados, u32 n);
void poly1305_finalizar(Poly1305 *p, u8 marca[POLY1305_MARCA]);
void poly1305_calcular(const u8 chave[32], const void *dados, u32 n, u8 marca[POLY1305_MARCA]);

/* AEAD ChaCha20-Poly1305 da RFC 8439 (nonce de 12 bytes, marca de 16). */
void chacha20_poly1305_cifrar(const u8 chave[32], const u8 nonce[12], const u8 *extra, u32 nextra,
                              const u8 *claro, u32 n, u8 *cifrado, u8 marca[16]);
int  chacha20_poly1305_decifrar(const u8 chave[32], const u8 nonce[12], const u8 *extra, u32 nextra,
                                const u8 *cifrado, u32 n, const u8 marca[16], u8 *claro);

/* ------------------------------------------------------------ Curve25519 */

/* X25519 (RFC 7748). Devolve CRIPTO_ERRO se o resultado for todo zero (o
 * outro lado mandou um ponto de ordem pequena): o TLS 1.3 manda recusar. */
int  x25519(u8 saida[32], const u8 escalar[32], const u8 ponto[32]);
void x25519_base(u8 publica[32], const u8 escalar[32]);

/* Ed25519 (RFC 8032, a variante pura). A chave privada é a semente de 32
 * bytes; a assinatura tem 64 bytes. */
void ed25519_chave_publica(u8 publica[32], const u8 semente[32]);
void ed25519_assinar(u8 assinatura[64], const void *mensagem, u32 n, const u8 semente[32]);
int  ed25519_verificar(const u8 assinatura[64], const void *mensagem, u32 n, const u8 publica[32]);

/* ------------------------------------------------------------ RSA (só conferir) */

#define RSA_BITS_MAX 4096

/* base^expoente mod modulo, tudo em bytes com o mais significativo primeiro;
 * a saída tem nmodulo bytes. O módulo tem de ser ímpar e a base menor que
 * ele. NÃO é em tempo constante: só serve para dados públicos. */
int exp_modular(const u8 *base, u32 nbase, const u8 *expoente, u32 nexpoente,
                const u8 *modulo, u32 nmodulo, u8 *saida);

/* Confere uma assinatura RSA sobre um resumo já calculado. "tipo" diz qual
 * resumo foi usado (e, no PSS, é também o resumo do MGF1; o sal tem o
 * tamanho do resumo, como no TLS 1.3). (n, e) é a chave pública. */
int rsa_verificar_pkcs1(const u8 *n, u32 nn, const u8 *e, u32 ne, int tipo,
                        const u8 *resumo, const u8 *assinatura, u32 nassinatura);
int rsa_verificar_pss(const u8 *n, u32 nn, const u8 *e, u32 ne, int tipo,
                      const u8 *resumo, const u8 *assinatura, u32 nassinatura);

/* ------------------------------------------------------------ ECDSA (só conferir) */

#define CURVA_P256  1
#define CURVA_P384  2

/* "ponto" é a chave pública no formato sem compressão: 04 || X || Y. O
 * resumo pode ter qualquer tamanho (se for maior que a curva, valem os bytes
 * da esquerda, como manda a FIPS 186). r e s são inteiros sem sinal. */
int ecdsa_verificar(int curva, const u8 *ponto, u32 nponto, const u8 *resumo, u32 nresumo,
                    const u8 *r, u32 nr, const u8 *s, u32 ns);
/* Separa r e s de uma assinatura no formato DER (SEQUENCE de dois INTEGER),
 * que é como ela vem no TLS e nos certificados. Os ponteiros devolvidos
 * apontam para dentro de "der". */
int ecdsa_ler_der(const u8 *der, u32 nder, const u8 **r, u32 *nr, const u8 **s, u32 *ns);
int ecdsa_verificar_der(int curva, const u8 *ponto, u32 nponto, const u8 *resumo, u32 nresumo,
                        const u8 *der, u32 nder);

#endif
