/* cripto.h - primitivas usadas pelo TLS (cripto.c). */
#ifndef SAGUI_CRIPTO_H
#define SAGUI_CRIPTO_H

typedef struct { uint32_t h[8]; uint8_t buf[64]; unsigned n; uint64_t total; } sha256_t;
typedef struct { uint8_t rk[176]; } aes_t;

void sha256_iniciar(sha256_t *c);
void sha256_somar(sha256_t *c, const void *dados, unsigned n);
void sha256_fim(const sha256_t *c, uint8_t saida[32]);
void sha256(const void *d, unsigned n, uint8_t saida[32]);
void sha384(const void *d, unsigned n, uint8_t saida[48]);
void sha512(const void *d, unsigned n, uint8_t saida[64]);
void hmac_sha256(const uint8_t *chave, unsigned nc, const void *d, unsigned n, uint8_t saida[32]);
void hkdf_extrair(const uint8_t *sal, unsigned ns, const uint8_t *ikm, unsigned ni, uint8_t prk[32]);
void hkdf_rotulo(const uint8_t segredo[32], const char *rotulo, const uint8_t *ctx, unsigned nctx, uint8_t *saida, unsigned n);
void aes_chave(aes_t *a, const uint8_t chave[16]);
void aes_bloco(const aes_t *a, const uint8_t e[16], uint8_t s[16]);
void gcm_cifrar(const aes_t *a, const uint8_t iv[12], const uint8_t *aad, unsigned na, uint8_t *d, unsigned n, uint8_t marca[16]);
bool gcm_decifrar(const aes_t *a, const uint8_t iv[12], const uint8_t *aad, unsigned na, uint8_t *d, unsigned n, const uint8_t marca[16]);
void x25519(uint8_t saida[32], const uint8_t escalar[32], const uint8_t ponto[32]);

/* certificado.c: devolvem NULL se está tudo certo, ou o motivo da recusa. */
const char *cert_cadeia(const uint8_t *msg, unsigned n, const char *host, bool v13);
const char *cert_prova12(const uint8_t *msg, unsigned n, const uint8_t *dados, unsigned nd);
void p256_publica(const uint8_t priv[32], uint8_t pub[65]);
bool p256_comum(const uint8_t priv[32], const uint8_t *q, unsigned nq, uint8_t saida[32]);
const char *cert_prova(const uint8_t *msg, unsigned n, const uint8_t transcricao[32]);

#endif
