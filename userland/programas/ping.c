/* ping <nome ou IP> [vezes] - pergunta "você está aí?" a outro computador
 * (pedido de eco do ICMP) e mede quanto a resposta demora (H44, H45). */
#include "sagui.h"

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: ping <nome ou IP> [vezes]\n"); return 1; }
    int vezes = argc > 2 ? texto_para_int(argv[2]) : 4;
    if (vezes < 1) vezes = 1;
    if (vezes > 100) vezes = 100;
    unsigned ip;
    int r = dns_resolver(argv[1], &ip);
    if (r < 0) {
        imprimir_em(ERROS, "ping: %s: %s\n", argv[1], r == ERRO_NAO_EXISTE ? "nome não encontrado" : nome_erro(r));
        return 1;
    }
    char t[16];
    ip_para_texto(ip, t);
    int s = rede_socket(REDE_ICMP);
    if (s < 0) { imprimir_em(ERROS, "ping: %s\n", nome_erro(s)); return 1; }
    rede_conectar(s, ip, 0);
    imprimir("PING %s (%s)\n", argv[1], t);
    registrar("ping: %s = %s", argv[1], t);
    unsigned id = (unsigned)meu_pid() & 0xFFFF;
    int respostas = 0;
    unsigned soma = 0;
    for (int seq = 1; seq <= vezes; seq++) {
        unsigned char m[40];
        memset(m, 0, sizeof m);
        m[0] = 8;                                   /* pedido de eco */
        m[4] = (unsigned char)(id >> 8); m[5] = (unsigned char)id;
        m[6] = (unsigned char)(seq >> 8); m[7] = (unsigned char)seq;
        copia_texto((char *)m + 8, "SaguiOS ping", 32);
        unsigned inicio = tempo_ms();
        if (rede_enviar(s, m, sizeof m) < 0) { imprimir("sem rede\n"); break; }
        bool chegou = false;
        while (tempo_ms() - inicio < 2000) {
            unsigned char resp[64];
            unsigned de;
            int n = rede_receber(s, resp, sizeof resp, &de);
            if (n >= 8 && resp[0] == 0 && de == ip && ((resp[4] << 8) | resp[5]) == (int)id && ((resp[6] << 8) | resp[7]) == seq) {
                unsigned ms = tempo_ms() - inicio;
                imprimir("resposta de %s: seq=%d tempo=%u ms\n", t, seq, ms);
                respostas++;
                soma += ms;
                chegou = true;
                break;
            }
            dormir(5);
        }
        if (!chegou) imprimir("seq=%d: sem resposta\n", seq);
        if (seq < vezes) dormir(500);
    }
    rede_fechar(s);
    imprimir("%d enviados, %d respostas, %d%% de perda", vezes, respostas, (vezes - respostas) * 100 / vezes);
    if (respostas) imprimir(", média %u ms", soma / (unsigned)respostas);
    imprimir("\n");
    registrar("ping: %d de %d respostas", respostas, vezes);
    return respostas ? 0 : 1;
}
