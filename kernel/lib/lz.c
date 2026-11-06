/* lz.c - desfaz a compressão do ramdisk. O formato está em tools/lz.py. */
#include "tipos.h"
#include "lz.h"

static uint32_t tamanho(const uint8_t **s, uint32_t n)
{
    uint8_t b;
    do { b = *(*s)++; n += b; } while (b == 255);
    return n;
}

void lz_expandir(const uint8_t *s, uint32_t n, uint8_t *d)
{
    const uint8_t *fim = s + n;
    while (s < fim) {
        uint8_t t = *s++;
        uint32_t lit = t >> 4;
        if (lit == 15) lit = tamanho(&s, lit);
        while (lit--) *d++ = *s++;
        if (s >= fim) break;
        uint32_t dist = (uint32_t)s[0] | (uint32_t)s[1] << 8;
        s += 2;
        uint32_t m = t & 15;
        if (m == 15) m = tamanho(&s, m);
        m += 4;
        const uint8_t *o = d - dist;
        while (m--) *d++ = *o++;
    }
}
