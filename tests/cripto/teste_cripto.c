/* teste_cripto.c - programa de teste da biblioteca de criptografia (H50).
 *
 * Roda no computador de desenvolvimento, não no SaguiOS: é compilado com o
 * gcc da máquina e ligado aos mesmos arquivos de userland/cripto/. Ele não
 * sabe resposta nenhuma. Lê comandos da entrada padrão, um por linha, com os
 * argumentos em hexadecimal ("-" é o vazio), chama a biblioteca e escreve o
 * resultado em hexadecimal. Quem tem os vetores oficiais e decide se está
 * certo é o tests/h50_cripto.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cripto.h"

#define MAX_ARGS   10
#define MAX_LINHA  (1 << 20)

static char linha[MAX_LINHA];
static u8 *arg[MAX_ARGS];
static u32 narg[MAX_ARGS];
static char *texto[MAX_ARGS];
static int nargs;

static int digito(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Converte o argumento i de hexadecimal para bytes (se não for hexadecimal,
 * fica só o texto, para os argumentos que são números ou nomes). */
static void converter(int i)
{
    const char *t = texto[i];
    size_t n = strlen(t);
    arg[i] = malloc(n / 2 + 1);
    narg[i] = 0;
    if (strcmp(t, "-") == 0 || n % 2) return;
    for (size_t j = 0; j < n; j += 2) {
        int a = digito(t[j]), b = digito(t[j + 1]);
        if (a < 0 || b < 0) { narg[i] = 0; return; }
        arg[i][narg[i]++] = (u8)(a * 16 + b);
    }
}

static void hex(const u8 *b, u32 n)
{
    if (n == 0) printf("-");
    for (u32 i = 0; i < n; i++) printf("%02x", b[i]);
}

static void fim(void) { printf("\n"); }

static int tipo_resumo(const char *t)
{
    if (strcmp(t, "sha256") == 0) return RESUMO_SHA256;
    if (strcmp(t, "sha384") == 0) return RESUMO_SHA384;
    if (strcmp(t, "sha512") == 0) return RESUMO_SHA512;
    return 0;
}

static int curva(const char *t)
{
    if (strcmp(t, "p256") == 0) return CURVA_P256;
    if (strcmp(t, "p384") == 0) return CURVA_P384;
    return 0;
}

static void veredito(int r) { printf(r == CRIPTO_OK ? "OK" : "FALHA"); fim(); }

static void executar(const char *cmd)
{
    u8 saida[64];

    /* resumo TIPO DADOS: de uma vez só. */
    if (strcmp(cmd, "resumo") == 0) {
        int t = tipo_resumo(texto[0]);
        if (t == RESUMO_SHA256) sha256(arg[1], narg[1], saida);
        else if (t == RESUMO_SHA384) sha384(arg[1], narg[1], saida);
        else sha512(arg[1], narg[1], saida);
        hex(saida, resumo_tamanho(t)); fim();
    }
    /* resumo_pedacos TIPO DADOS: aos pedaços de 1, 2, 3... bytes, para
     * exercitar o bloco que fica pela metade entre uma chamada e outra. */
    else if (strcmp(cmd, "resumo_pedacos") == 0) {
        Resumo r;
        int t = tipo_resumo(texto[0]);
        resumo_iniciar(&r, t);
        for (u32 pos = 0, passo = 1; pos < narg[1]; passo++) {
            u32 parte = narg[1] - pos < passo ? narg[1] - pos : passo;
            resumo_atualizar(&r, arg[1] + pos, parte);
            pos += parte;
        }
        resumo_finalizar(&r, saida);
        hex(saida, resumo_tamanho(t)); fim();
    }
    /* resumo_repetido TIPO BYTE VEZES: o teste do milhão de letras "a". */
    else if (strcmp(cmd, "resumo_repetido") == 0) {
        Resumo r;
        int t = tipo_resumo(texto[0]);
        u8 bloco[1000];
        long vezes = atol(texto[2]);
        memset(bloco, arg[1][0], sizeof bloco);
        resumo_iniciar(&r, t);
        while (vezes > 0) {
            u32 parte = vezes < 1000 ? (u32)vezes : 1000;
            resumo_atualizar(&r, bloco, parte);
            vezes -= parte;
        }
        resumo_finalizar(&r, saida);
        hex(saida, resumo_tamanho(t)); fim();
    }
    /* hmac TIPO CHAVE DADOS: de uma vez e aos pedaços; as duas têm de bater. */
    else if (strcmp(cmd, "hmac") == 0) {
        int t = tipo_resumo(texto[0]);
        u8 outra[64];
        Hmac h;
        if (t == RESUMO_SHA256) hmac_sha256(arg[1], narg[1], arg[2], narg[2], saida);
        else if (t == RESUMO_SHA384) hmac_sha384(arg[1], narg[1], arg[2], narg[2], saida);
        else hmac_calcular(t, arg[1], narg[1], arg[2], narg[2], saida);
        hmac_iniciar(&h, t, arg[1], narg[1]);
        for (u32 pos = 0, passo = 1; pos < narg[2]; passo += 3) {
            u32 parte = narg[2] - pos < passo ? narg[2] - pos : passo;
            hmac_atualizar(&h, arg[2] + pos, parte);
            pos += parte;
        }
        hmac_finalizar(&h, outra);
        if (memcmp(saida, outra, resumo_tamanho(t)) != 0) printf("PEDACOS-DIFERENTES ");
        hex(saida, resumo_tamanho(t)); fim();
    }
    /* hkdf TIPO SAL MATERIAL INFO TAMANHO: escreve "PRK OKM". */
    else if (strcmp(cmd, "hkdf") == 0) {
        int t = tipo_resumo(texto[0]);
        u32 n = (u32)atol(texto[4]);
        u8 prk[64], *okm = malloc(n + 1);
        hkdf_extrair(t, arg[1], narg[1], arg[2], narg[2], prk);
        hex(prk, resumo_tamanho(t));
        printf(" ");
        if (hkdf_expandir(t, prk, resumo_tamanho(t), arg[3], narg[3], okm, n) != CRIPTO_OK) printf("FALHA");
        else hex(okm, n);
        fim();
        free(okm);
    }
    /* aes CHAVE BLOCO */
    else if (strcmp(cmd, "aes") == 0) {
        Aes a;
        if (aes_iniciar(&a, arg[0], narg[0]) != CRIPTO_OK) { printf("FALHA"); fim(); return; }
        aes_cifrar_bloco(&a, arg[1], saida);
        hex(saida, 16); fim();
    }
    /* aes_ctr CHAVE CONTADOR DADOS: de uma vez e aos pedaços. */
    else if (strcmp(cmd, "aes_ctr") == 0) {
        AesCtr c;
        u32 n = narg[2];
        u8 *s1 = malloc(n + 1), *s2 = malloc(n + 1);
        if (aes_ctr_iniciar(&c, arg[0], narg[0], arg[1]) != CRIPTO_OK) { printf("FALHA"); fim(); return; }
        aes_ctr_aplicar(&c, arg[2], s1, n);
        aes_ctr_iniciar(&c, arg[0], narg[0], arg[1]);
        for (u32 pos = 0, passo = 1; pos < n; passo += 5) {
            u32 parte = n - pos < passo ? n - pos : passo;
            aes_ctr_aplicar(&c, arg[2] + pos, s2 + pos, parte);
            pos += parte;
        }
        if (memcmp(s1, s2, n) != 0) printf("PEDACOS-DIFERENTES ");
        hex(s1, n); fim();
        free(s1); free(s2);
    }
    /* gcm_cifrar CHAVE NONCE EXTRA CLARO: escreve "CIFRADO MARCA". */
    else if (strcmp(cmd, "gcm_cifrar") == 0) {
        AesGcm g;
        u32 n = narg[3];
        u8 *s = malloc(n + 1), marca[16];
        if (aes_gcm_iniciar(&g, arg[0], narg[0]) != CRIPTO_OK) { printf("FALHA"); fim(); return; }
        aes_gcm_cifrar(&g, arg[1], narg[1], arg[2], narg[2], arg[3], n, s, marca);
        hex(s, n); printf(" "); hex(marca, 16); fim();
        free(s);
    }
    /* gcm_decifrar CHAVE NONCE EXTRA CIFRADO MARCA: o claro, ou FALHA. No
     * caso de falha confere também que a saída foi zerada. */
    else if (strcmp(cmd, "gcm_decifrar") == 0) {
        AesGcm g;
        u32 n = narg[3];
        u8 *s = malloc(n + 1);
        memset(s, 0xAA, n + 1);
        if (aes_gcm_iniciar(&g, arg[0], narg[0]) != CRIPTO_OK) { printf("FALHA"); fim(); return; }
        if (aes_gcm_decifrar(&g, arg[1], narg[1], arg[2], narg[2], arg[3], n, arg[4], s) != CRIPTO_OK) {
            int zerada = 1;
            for (u32 i = 0; i < n; i++) if (s[i]) zerada = 0;
            printf(zerada ? "FALHA" : "FALHA-SEM-ZERAR");
        } else hex(s, n);
        fim();
        free(s);
    }
    /* chacha_bloco CHAVE CONTADOR NONCE */
    else if (strcmp(cmd, "chacha_bloco") == 0) {
        u8 b[64];
        chacha20_bloco(arg[0], (u32)atol(texto[1]), arg[2], b);
        hex(b, 64); fim();
    }
    /* chacha CHAVE CONTADOR NONCE DADOS */
    else if (strcmp(cmd, "chacha") == 0) {
        u32 n = narg[3];
        u8 *s = malloc(n + 1);
        chacha20_aplicar(arg[0], (u32)atol(texto[1]), arg[2], arg[3], s, n);
        hex(s, n); fim();
        free(s);
    }
    /* poly1305 CHAVE DADOS: de uma vez e aos pedaços. */
    else if (strcmp(cmd, "poly1305") == 0) {
        Poly1305 p;
        u8 m1[16], m2[16];
        poly1305_calcular(arg[0], arg[1], narg[1], m1);
        poly1305_iniciar(&p, arg[0]);
        for (u32 pos = 0, passo = 1; pos < narg[1]; passo += 2) {
            u32 parte = narg[1] - pos < passo ? narg[1] - pos : passo;
            poly1305_atualizar(&p, arg[1] + pos, parte);
            pos += parte;
        }
        poly1305_finalizar(&p, m2);
        if (memcmp(m1, m2, 16) != 0) printf("PEDACOS-DIFERENTES ");
        hex(m1, 16); fim();
    }
    /* cp_cifrar CHAVE NONCE EXTRA CLARO: escreve "CIFRADO MARCA". */
    else if (strcmp(cmd, "cp_cifrar") == 0) {
        u32 n = narg[3];
        u8 *s = malloc(n + 1), marca[16];
        chacha20_poly1305_cifrar(arg[0], arg[1], arg[2], narg[2], arg[3], n, s, marca);
        hex(s, n); printf(" "); hex(marca, 16); fim();
        free(s);
    }
    /* cp_decifrar CHAVE NONCE EXTRA CIFRADO MARCA */
    else if (strcmp(cmd, "cp_decifrar") == 0) {
        u32 n = narg[3];
        u8 *s = malloc(n + 1);
        memset(s, 0xAA, n + 1);
        if (chacha20_poly1305_decifrar(arg[0], arg[1], arg[2], narg[2], arg[3], n, arg[4], s) != CRIPTO_OK) {
            int zerada = 1;
            for (u32 i = 0; i < n; i++) if (s[i]) zerada = 0;
            printf(zerada ? "FALHA" : "FALHA-SEM-ZERAR");
        } else hex(s, n);
        fim();
        free(s);
    }
    /* x25519 ESCALAR PONTO */
    else if (strcmp(cmd, "x25519") == 0) {
        if (x25519(saida, arg[0], arg[1]) != CRIPTO_OK) printf("FALHA");
        else hex(saida, 32);
        fim();
    }
    /* x25519_base ESCALAR */
    else if (strcmp(cmd, "x25519_base") == 0) {
        x25519_base(saida, arg[0]);
        hex(saida, 32); fim();
    }
    /* x25519_repetido VEZES: o teste em laço da seção 5.2 da RFC 7748. */
    else if (strcmp(cmd, "x25519_repetido") == 0) {
        u8 k[32] = { 9 }, u[32] = { 9 }, r[32];
        long vezes = atol(texto[0]);
        for (long i = 0; i < vezes; i++) {
            x25519(r, k, u);
            memcpy(u, k, 32);
            memcpy(k, r, 32);
        }
        hex(k, 32); fim();
    }
    /* ed_publica SEMENTE */
    else if (strcmp(cmd, "ed_publica") == 0) {
        ed25519_chave_publica(saida, arg[0]);
        hex(saida, 32); fim();
    }
    /* ed_assinar SEMENTE MENSAGEM */
    else if (strcmp(cmd, "ed_assinar") == 0) {
        ed25519_assinar(saida, arg[1], narg[1], arg[0]);
        hex(saida, 64); fim();
    }
    /* ed_verificar PUBLICA MENSAGEM ASSINATURA */
    else if (strcmp(cmd, "ed_verificar") == 0) {
        if (narg[0] != 32 || narg[2] != 64) { printf("FALHA"); fim(); return; }
        veredito(ed25519_verificar(arg[2], arg[1], narg[1], arg[0]));
    }
    /* exp_modular BASE EXPOENTE MODULO */
    else if (strcmp(cmd, "exp_modular") == 0) {
        u8 *s = malloc(narg[2] + 1);
        if (exp_modular(arg[0], narg[0], arg[1], narg[1], arg[2], narg[2], s) != CRIPTO_OK) printf("FALHA");
        else hex(s, narg[2]);
        fim();
        free(s);
    }
    /* rsa_pkcs1 / rsa_pss TIPO N E RESUMO ASSINATURA */
    else if (strcmp(cmd, "rsa_pkcs1") == 0) {
        veredito(rsa_verificar_pkcs1(arg[1], narg[1], arg[2], narg[2], tipo_resumo(texto[0]),
                                     arg[3], arg[4], narg[4]));
    }
    else if (strcmp(cmd, "rsa_pss") == 0) {
        veredito(rsa_verificar_pss(arg[1], narg[1], arg[2], narg[2], tipo_resumo(texto[0]),
                                   arg[3], arg[4], narg[4]));
    }
    /* ecdsa_der CURVA PONTO RESUMO DER */
    else if (strcmp(cmd, "ecdsa_der") == 0) {
        veredito(ecdsa_verificar_der(curva(texto[0]), arg[1], narg[1], arg[2], narg[2], arg[3], narg[3]));
    }
    /* ecdsa CURVA PONTO RESUMO R S */
    else if (strcmp(cmd, "ecdsa") == 0) {
        veredito(ecdsa_verificar(curva(texto[0]), arg[1], narg[1], arg[2], narg[2],
                                 arg[3], narg[3], arg[4], narg[4]));
    }
    /* diferentes A B: a comparação em tempo constante. */
    else if (strcmp(cmd, "diferentes") == 0) {
        int r = narg[0] != narg[1] ? 1 : cri_diferentes(arg[0], arg[1], narg[0]);
        printf("%d", r); fim();
    }
    /* zerar DADOS: copia, zera e mostra. */
    else if (strcmp(cmd, "zerar") == 0) {
        u8 *s = malloc(narg[0] + 1);
        cri_copiar(s, arg[0], narg[0]);
        int igual = memcmp(s, arg[0], narg[0]) == 0;
        cri_zerar(s, narg[0]);
        printf(igual ? "copiou " : "nao-copiou ");
        hex(s, narg[0]); fim();
        free(s);
    }
    else {
        printf("COMANDO-DESCONHECIDO %s", cmd); fim();
    }
}

int main(void)
{
    while (fgets(linha, sizeof linha, stdin)) {
        char *cmd = strtok(linha, " \r\n");
        if (!cmd || cmd[0] == '#') continue;
        nargs = 0;
        char *t;
        while (nargs < MAX_ARGS && (t = strtok(NULL, " \r\n")) != NULL) {
            texto[nargs] = t;
            converter(nargs);
            nargs++;
        }
        for (int i = nargs; i < MAX_ARGS; i++) { texto[i] = "-"; arg[i] = malloc(1); narg[i] = 0; }
        executar(cmd);
        for (int i = 0; i < MAX_ARGS; i++) free(arg[i]);
    }
    return 0;
}
