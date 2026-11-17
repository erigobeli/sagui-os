/* setores - teste da trava dos setores crus (D063, h109).
 *
 *   setores <disco> <lba> ler|gravar [liberar]
 *
 * Com "liberar", pede antes a confirmação do kernel (a pessoa digita o nome
 * do disco). Mostra o código que o kernel devolveu, para o teste conferir
 * que sem a confirmação não dá para gravar nem ler um disco interno. */
#include "sagui.h"

static unsigned numero(const char *s)
{
    unsigned n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (unsigned)(*s++ - '0');
    return n;
}

int principal(int argc, char **argv)
{
    if (argc < 4) { imprimir("uso: setores <disco> <lba> ler|gravar [liberar]\n"); return 1; }
    if (argc >= 5 && compara_texto(argv[4], "liberar") == 0) {
        int r = disco_liberar(argv[1]);
        imprimir("setores: liberar %s -> %d\n", argv[1], r);
        registrar("setores: liberar %s -> %d", argv[1], r);
    }
    static unsigned char b[512];
    bool gravar = compara_texto(argv[3], "gravar") == 0;
    if (gravar) for (int i = 0; i < 512; i++) b[i] = (unsigned char)(0xA5 ^ i);
    pedido_setores_t p;
    memset(&p, 0, sizeof p);
    copia_texto(p.nome, argv[1], sizeof p.nome);
    p.lba = numero(argv[2]);
    p.n = 1;
    p.escrever = gravar;
    p.buf = b;
    int r = disco_setores(&p);
    imprimir("setores: %s %s %u -> %d\n", argv[3], argv[1], p.lba, r);
    registrar("setores: %s %s %u -> %d", argv[3], argv[1], p.lba, r);
    return 0;
}
