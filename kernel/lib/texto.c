/* texto.c - rotinas de memória e de texto do kernel.
 *
 * Versões simples, byte a byte, mas com cópia de 4 em 4 bytes quando os dois
 * ponteiros estão alinhados: o kernel copia buffers de tela e páginas inteiras,
 * e isso dobra a velocidade sem complicar. */
#include "texto.h"

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0 && n >= 4) {
        /* Alinhados: a cópia de 4 em 4 bytes do próprio processador (rep
         * movsd), bem mais rápida que um laço (as telas são copiadas linha a
         * linha a cada quadro). A direção (DF) é sempre para a frente aqui:
         * a convenção do C garante DF = 0 na entrada de uma função. */
        size_t q = n / 4;
        __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(q) : : "memory");
        n &= 3;
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s)
        return memcpy(dst, src, n);
    /* Regiões sobrepostas com destino depois da origem: copia de trás para frente. */
    d += n; s += n;
    while (n--)
        *--d = *--s;
    return dst;
}

void *memset(void *dst, int v, size_t n)
{
    uint8_t *d = dst;
    uint32_t v4 = (uint8_t)v * 0x01010101u;
    while (n && ((uintptr_t)d & 3)) { *d++ = (uint8_t)v; n--; }
    while (n >= 4) { *(uint32_t *)d = v4; d += 4; n -= 4; }
    while (n--) *d++ = (uint8_t)v;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

size_t txt_tamanho(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int txt_compara(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

int txt_compara_n(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (uint8_t)*a - (uint8_t)*b;
        if (!*a) return 0;
    }
    return 0;
}

/* Copia no máximo n-1 caracteres e sempre termina com zero (n > 0). */
char *txt_copia_n(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (!n) return dst;
    for (; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return dst;
}

bool txt_comeca_com(const char *s, const char *prefixo)
{
    while (*prefixo)
        if (*s++ != *prefixo++)
            return false;
    return true;
}
