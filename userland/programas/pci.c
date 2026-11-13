/* pci - lista o hardware do barramento PCI (H35): placa de rede, som, USB,
 * vídeo... e diz quais o SaguiOS já sabe usar. Com um pendrive montado, grava
 * também o relatório em /usb/hardware.txt, para mandar ou fotografar. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    static char texto[8192];
    int n = info_sistema(INFO_PCI, texto, sizeof texto - 1);
    if (n < 0) { imprimir_em(ERROS, "pci: %s\n", nome_erro(n)); return 1; }
    texto[n] = 0;
    escrever_texto(SAIDA, texto);
    int fd = abrir("/usb/hardware.txt", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd >= 0) {
        escrever(fd, texto, (unsigned)n);
        fechar(fd);
        imprimir("Relatório gravado em /usb/hardware.txt\n");
    }
    return 0;
}
