/* uptime - mostra há quanto tempo o sistema está ligado.
 *
 *   uptime
 *
 * Mostra dias, horas, minutos e segundos desde o boot, lendo os milissegundos
 * do relógio do sistema (SIS_TEMPO via tempo_ms da libsagui). Tudo do zero,
 * sem libc. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    unsigned ms = tempo_ms();
    unsigned segundos = ms / 1000;
    unsigned minutos = segundos / 60;
    unsigned horas = minutos / 60;
    unsigned dias = horas / 24;

    segundos %= 60;
    minutos %= 60;
    horas %= 24;

    if (dias > 0)
        imprimir("%u dia%s, %02u:%02u:%02u\n", dias, dias > 1 ? "s" : "", horas, minutos, segundos);
    else
        imprimir("%02u:%02u:%02u\n", horas, minutos, segundos);
    return 0;
}
