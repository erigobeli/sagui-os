/* tcpreceber <ip> <porta> - teste do TCP (H48): conecta, lê tudo até o
 * outro lado fechar e mostra quantos bytes chegaram e a soma Adler-32 deles. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    if (argc < 3) { imprimir_em(ERROS, "uso: tcpreceber <ip> <porta>\n"); return 1; }
    unsigned ip;
    if (dns_resolver(argv[1], &ip) < 0) { imprimir_em(ERROS, "tcpreceber: nome não encontrado\n"); return 1; }
    unsigned inicio = tempo_ms();
    int s = tcp_conectar(ip, texto_para_int(argv[2]), 10000);
    if (s < 0) { imprimir_em(ERROS, "tcpreceber: não conectou (%s)\n", nome_erro(s)); return 1; }
    tcp_enviar_tudo(s, "ola\n", 4);
    static unsigned char buf[16384];
    unsigned a = 1, b = 0, total = 0, parado = tempo_ms();
    for (;;) {
        int n = rede_receber(s, buf, sizeof buf, NULL);
        if (n == ERRO_FIM) break;
        if (n < 0) { imprimir_em(ERROS, "tcpreceber: %s depois de %u bytes\n", nome_erro(n), total); rede_fechar(s); return 1; }
        if (n == 0) {
            if (tempo_ms() - parado > 20000) { imprimir_em(ERROS, "tcpreceber: parou de chegar\n"); return 1; }
            dormir(5);
            continue;
        }
        parado = tempo_ms();
        for (int i = 0; i < n; i++) { a = (a + buf[i]) % 65521; b = (b + a) % 65521; }
        total += (unsigned)n;
    }
    rede_fechar(s);
    unsigned ms = tempo_ms() - inicio;
    imprimir("recebidos %u bytes, adler32 %08x, %u ms\n", total, (b << 16) | a, ms);
    registrar("tcpreceber: %u bytes adler32 %08x em %u ms", total, (b << 16) | a, ms);
    return 0;
}
