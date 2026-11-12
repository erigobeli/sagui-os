/* fpu.c - dois processos fazem contas em x87 e SSE ao mesmo tempo. Se o kernel
 * não trocar o estado da FPU, os valores de um vazam para o outro. */
#include "sagui.h"

static int contar(int escala)
{
    double s = 0;
    unsigned marca = 0x1000u + (unsigned)escala, lido;
    __asm__ volatile("movd %0, %%xmm3" :: "r"(marca) : "xmm3");
    for (int i = 1; i <= 6000000; i++) s += (double)escala / i;
    __asm__ volatile("movd %%xmm3, %0" : "=r"(lido));
    if (lido != marca) return -1;
    return (int)(s * 1000);
}

int principal(int argc, char **argv)
{
    int pid = -1;
    if (argc < 2) {
        char *a[] = { "/testes/fpu", "filho", NULL };
        pid = executar("/testes/fpu", a);
    }
    int escala = argc < 2 ? 1 : 2;
    imprimir("fpu %d: %d\n", escala, contar(escala));
    registrar("fpu %d: %d\n", escala, contar(escala));
    if (pid > 0) esperar(pid, NULL);
    return 0;
}
