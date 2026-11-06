/* cripto - confere as primitivas do TLS com os vetores das especificações. */
#include "sagui.h"
#include "cripto.h"

static int falhas;

static void hex(const char *s, uint8_t *d, int n)
{
    for (int i = 0; i < n; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[2 * i + k];
            v = v * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        d[i] = (uint8_t)v;
    }
}

static void confere(const char *nome, const uint8_t *a, const char *esperado, int n)
{
    uint8_t e[64];
    hex(esperado, e, n);
    bool ok = memcmp(a, e, (size_t)n) == 0;
    if (!ok) falhas++;
    registrar("cripto: %s %s\n", nome, ok ? "ok" : "FALHOU");
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint8_t s[64], a[32], b[32], k[16], iv[12], d[64], m[16];
    sha256("abc", 3, s);
    confere("sha256", s, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);
    uint8_t ch[20];
    memset(ch, 0x0b, 20);
    hmac_sha256(ch, 20, "Hi There", 8, s);
    confere("hmac", s, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32);
    hex("000102030405060708090a0b0c0d0e0f", k, 16);
    hex("00112233445566778899aabbccddeeff", d, 16);
    aes_t aes;
    aes_chave(&aes, k);
    aes_bloco(&aes, d, s);
    confere("aes", s, "69c4e0d86a7b0430d8cdb78070b4c55a", 16);
    /* NIST GCM, caso 3 reduzido ao caso 4: com dados adicionais. */
    hex("feffe9928665731c6d6a8f9467308308", k, 16);
    hex("cafebabefacedbaddecaf888", iv, 12);
    hex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39", d, 60);
    uint8_t aad[20];
    hex("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad, 20);
    aes_chave(&aes, k);
    gcm_cifrar(&aes, iv, aad, 20, d, 60, m);
    confere("gcm-texto", d, "42831ec2217774244b7221b7", 12);
    confere("gcm-marca", m, "5bc94fbc3221a5db94fae95ae7121a47", 16);
    bool volta = gcm_decifrar(&aes, iv, aad, 20, d, 60, m);
    if (!volta) falhas++;
    d[0] ^= 1;
    if (gcm_decifrar(&aes, iv, aad, 20, d, 60, m)) falhas++;
    registrar("cripto: gcm-adulterado %s\n", volta ? "ok" : "FALHOU");
    hex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", a, 32);
    hex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", b, 32);
    x25519(s, a, b);
    confere("x25519", s, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", 32);
    registrar("cripto: %s\n", falhas ? "FALHOU" : "tudo certo");
    return falhas;
}
