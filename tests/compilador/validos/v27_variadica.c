/* Funções com número variável de argumentos (va_list), como a imprimir. */
#include "sagui.h"

static int soma(int quantos, ...)
{
    va_list ap;
    int s = 0;
    va_start(ap, quantos);
    for (int i = 0; i < quantos; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

static int maior_texto(const char *primeiro, ...)
{
    va_list ap;
    int m = (int)tam_texto(primeiro);
    const char *s;
    va_start(ap, primeiro);
    while ((s = va_arg(ap, const char *)) != NULL)
        if ((int)tam_texto(s) > m) m = (int)tam_texto(s);
    va_end(ap);
    return m;
}

/* Repassa a lista para a função da libsagui que recebe va_list. */
static void registro(const char *nivel, const char *fmt, ...)
{
    char linha[100];
    va_list ap;
    va_start(ap, fmt);
    formatar_lista(linha, sizeof linha, fmt, ap);
    va_end(ap);
    imprimir("[%s] %s\n", nivel, linha);
}

static long long soma_longos(int n, ...)
{
    va_list ap;
    long long s = 0;
    va_start(ap, n);
    while (n--) s += va_arg(ap, long long);
    va_end(ap);
    return s;
}

int principal(int argc, char **argv)
{
    char c = 'x';
    short s = -3;
    (void)argc; (void)argv;
    imprimir("%d %d %d\n", soma(0), soma(3, 10, 20, 30), soma(5, 1, -1, 1, -1, 100));
    imprimir("%d\n", maior_texto("um", "quatro", "tres", NULL));
    registro("aviso", "%d macacos em %s", 3, "galhos");
    registro("erro", "codigo %x, letra %c", 255, 'Z');
    imprimir("%c %d %s %u %x %5d|%-5d|%05d\n", c, s, "texto", 3000000000u, 48879, 42, 42, 42);
    imprimir("%d\n", (int)(soma_longos(3, 10000000000LL, 20000000000LL, 12LL) >> 20));
    return 0;
}
