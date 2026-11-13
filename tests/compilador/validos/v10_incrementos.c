/* ++, -- e as atribuições com conta (+=, <<= ...). */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int i = 5, j;
    (void)argc; (void)argv;
    j = i++; imprimir("%d %d\n", i, j);
    j = ++i; imprimir("%d %d\n", i, j);
    j = i--; imprimir("%d %d\n", i, j);
    j = --i; imprimir("%d %d\n", i, j);
    int x = 10;
    x += 5; imprimir("%d ", x);
    x -= 3; imprimir("%d ", x);
    x *= 4; imprimir("%d ", x);
    x /= 5; imprimir("%d ", x);
    x %= 7; imprimir("%d ", x);
    x <<= 6; imprimir("%d ", x);
    x >>= 2; imprimir("%d ", x);
    x |= 0x0F; imprimir("%d ", x);
    x &= 0x3C; imprimir("%d ", x);
    x ^= 0xFF; imprimir("%d\n", x);
    unsigned char c = 254;
    c++; c++; c++;
    imprimir("%d\n", c);
    char t[] = "abc";
    char *p = t;
    *p++ = 'X';
    (*p)++;
    imprimir("%s %d\n", t, (int)(p - t));
    int v[3] = { 10, 20, 30 }, k = 0;
    v[k++] += 1;
    v[k] *= 2;
    imprimir("%d %d %d %d\n", v[0], v[1], v[2], k);
    short s = 32767;
    s++;
    imprimir("%d\n", s);
    int a = 1, b = 2, r;
    r = a = b = 7;
    imprimir("%d %d %d\n", a, b, r);
    return 0;
}
