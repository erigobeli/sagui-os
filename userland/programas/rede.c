/* rede - mostra a rede (fase 3 da 2.0): placa, cabo, endereço, máscara,
 * roteador, DNS e quantos pacotes passaram. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    info_rede_t r;
    if (rede_info(&r) < 0) { imprimir("Nenhuma placa de rede com driver neste computador.\n"); return 1; }
    char ip[16], m[16], g[16], d[16];
    ip_para_texto(r.ip, ip); ip_para_texto(r.mascara, m); ip_para_texto(r.roteador, g); ip_para_texto(r.dns, d);
    imprimir("Placa:     %s\n", r.placa);
    imprimir("Físico:    %02x:%02x:%02x:%02x:%02x:%02x\n", r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5]);
    imprimir("Cabo:      %s\n", r.cabo ? "ligado" : "desligado");
    imprimir("Modo:      %s\n", r.dhcp ? "automático (DHCP)" : "manual");
    if (!r.configurado) { imprimir("Endereço:  %s\n", r.cabo ? "pedindo ao roteador..." : "sem cabo"); }
    else {
        imprimir("Endereço:  %s\n", ip);
        imprimir("Máscara:   %s\n", m);
        imprimir("Roteador:  %s\n", g);
        imprimir("DNS:       %s\n", d);
    }
    imprimir("Pacotes:   %u recebidos, %u enviados\n", r.pacotes_rx, r.pacotes_tx);
    return 0;
}
