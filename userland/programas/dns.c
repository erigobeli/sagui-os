/* dns <nome> - descobre o endereço IP de um nome pelo DNS (H45). */
#include "sagui.h"

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: dns <nome>\n"); return 1; }
    unsigned ip;
    int r = dns_resolver(argv[1], &ip);
    if (r < 0) {
        imprimir_em(ERROS, "dns: %s: %s\n", argv[1], r == ERRO_NAO_EXISTE ? "nome não encontrado" : nome_erro(r));
        return 1;
    }
    char t[16];
    ip_para_texto(ip, t);
    imprimir("%s = %s\n", argv[1], t);
    registrar("dns: %s = %s", argv[1], t);
    return 0;
}
