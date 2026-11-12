/* ping - manda "eco" (ICMP) para um computador e mede a volta (D032). */
#include "sagui.h"

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: ping <endereço> [vezes]\n"); return 1; }
    int vezes = argc > 2 ? texto_para_int(argv[2]) : 4;
    if (vezes < 1) vezes = 1;
    uint32_t ip;
    int r = resolver(argv[1], &ip);
    if (r < 0) { imprimir_em(ERROS, "ping: %s: %s\n", argv[1], nome_erro(r)); return 1; }
    imprimir("PING %s (%u.%u.%u.%u)\n", argv[1], IP_PARTES(ip));
    int ok = 0;
    for (int i = 1; i <= vezes; i++) {
        unsigned ini = tempo_ms();
        r = ping(ip, 2000);
        if (r >= 0) {
            ok++;
            if (r == 0) imprimir("resposta de %u.%u.%u.%u: %d, tempo < 10 ms\n", IP_PARTES(ip), i);
            else imprimir("resposta de %u.%u.%u.%u: %d, tempo %d ms\n", IP_PARTES(ip), i, r);
        } else {
            imprimir("%d: %s\n", i, nome_erro(r));
        }
        unsigned gasto = tempo_ms() - ini;
        if (i < vezes && gasto < 1000) dormir(1000 - gasto);
    }
    imprimir("%d enviados, %d respondidos\n", vezes, ok);
    return ok ? 0 : 1;
}
