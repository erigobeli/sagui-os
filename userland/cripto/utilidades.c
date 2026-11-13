/* utilidades.c - copiar, zerar e comparar, do jeito que a criptografia pede.
 *
 * Parecem as rotinas de sempre, mas duas delas têm um motivo a mais:
 *   - zerar serve para apagar chaves e valores intermediários da pilha. Um
 *     compilador esperto percebe que ninguém lê a memória depois e remove a
 *     limpeza; escrever por um ponteiro "volatile" proíbe isso.
 *   - comparar não pode sair no primeiro byte diferente: quem mede o tempo
 *     descobriria quantos bytes de uma marca falsa já estão certos e
 *     acertaria o resto um por um. Aqui todos os bytes são sempre lidos. */
#include "interno.h"

void cri_copiar(void *destino, const void *origem, u32 n)
{
    u8 *d = destino;
    const u8 *o = origem;
    for (u32 i = 0; i < n; i++) d[i] = o[i];
}

void cri_zerar(void *p, u32 n)
{
    volatile u8 *v = p;
    for (u32 i = 0; i < n; i++) v[i] = 0;
}

int cri_diferentes(const void *a, const void *b, u32 n)
{
    const u8 *x = a, *y = b;
    u32 soma = 0;
    for (u32 i = 0; i < n; i++) soma |= (u32)(x[i] ^ y[i]);
    /* Junta tudo num só bit sem desvio: 1 se algum byte diferiu. */
    return (int)((soma | (0u - soma)) >> 31);
}
