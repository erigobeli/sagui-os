/* interno.h - o que os arquivos da biblioteca dividem entre si e não é para
 * os programas usarem. */
#ifndef CRIPTO_INTERNO_H
#define CRIPTO_INTERNO_H
#include "cripto.h"

/* O GCC, quando vê um laço que copia ou zera bytes, gosta de trocá-lo por
 * uma chamada a memcpy/memset, mesmo sem biblioteca C. Como a biblioteca
 * não pode depender de nada, essa troca fica desligada aqui. */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")

/* ------------------------------------------------------------ bytes e palavras */

static inline u32 ler32_be(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static inline void gravar32_be(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

static inline u32 ler32_le(const u8 *p)
{
    return ((u32)p[3] << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0];
}

static inline void gravar32_le(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* ------------------------------------------------------------ números grandes
 *
 * Um número é um vetor de "k" palavras de 32 bits, a menos significativa
 * primeiro. As rotinas abaixo estão em numeros.c e são usadas pelo RSA e
 * pelo ECDSA. Nenhuma é em tempo constante: só veem dados públicos. */

#define NUM_PALAVRAS 128            /* 4096 bits */

typedef struct {
    u32 k;                          /* palavras em uso */
    u32 n0;                         /* -1/n mod 2^32 */
    u32 n[NUM_PALAVRAS];            /* o módulo */
    u32 um[NUM_PALAVRAS];           /* 1 na forma de Montgomery (R mod n) */
    u32 r2[NUM_PALAVRAS];           /* R^2 mod n, para entrar na forma */
} Montgomery;

void num_de_bytes(u32 *r, u32 k, const u8 *b, u32 nb);
void num_para_bytes(u8 *b, u32 nb, const u32 *a, u32 k);
int  num_comparar(const u32 *a, const u32 *b, u32 k);       /* -1, 0, 1 */
int  num_e_zero(const u32 *a, u32 k);
u32  num_somar(u32 *r, const u32 *a, const u32 *b, u32 k);  /* devolve o vai-um */
u32  num_subtrair(u32 *r, const u32 *a, const u32 *b, u32 k);   /* devolve o empréstimo */
void num_copiar(u32 *r, const u32 *a, u32 k);

int  mont_iniciar(Montgomery *m, const u32 *modulo, u32 k);
void mont_multiplicar(const Montgomery *m, u32 *r, const u32 *a, const u32 *b);
void mont_entrar(const Montgomery *m, u32 *r, const u32 *a);
void mont_sair(const Montgomery *m, u32 *r, const u32 *a);
void mont_potencia(const Montgomery *m, u32 *r, const u32 *base, const u32 *expoente, u32 kexp);
void mont_somar(const Montgomery *m, u32 *r, const u32 *a, const u32 *b);
void mont_subtrair(const Montgomery *m, u32 *r, const u32 *a, const u32 *b);

#endif
