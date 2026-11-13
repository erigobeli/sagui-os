/* relogio [internet [servidor]] - mostra a hora ou acerta pela internet (H49).
 *
 * O acerto usa o NTP: uma pergunta UDP de 48 bytes para um servidor de hora
 * (porta 123). A resposta traz, a partir do byte 40, a hora em segundos desde
 * 1º de janeiro de 1900 (UTC). Tira-se a diferença até 1970, converte-se em
 * data (contando anos e meses, com os bissextos), soma-se o fuso das
 * Configurações e grava-se no relógio da placa-mãe. */
#include "sagui.h"

static bool bissexto(unsigned a) { return (a % 4 == 0 && a % 100 != 0) || a % 400 == 0; }

static void segundos_para_data(unsigned s, data_t *d)
{
    static const unsigned char dias_mes[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    unsigned dias = s / 86400, resto = s % 86400;
    d->hora = (unsigned char)(resto / 3600);
    d->minuto = (unsigned char)(resto / 60 % 60);
    d->segundo = (unsigned char)(resto % 60);
    unsigned ano = 1970;
    while (dias >= (bissexto(ano) ? 366u : 365u)) { dias -= bissexto(ano) ? 366 : 365; ano++; }
    unsigned mes = 0;
    for (;;) {
        unsigned n = dias_mes[mes] + (mes == 1 && bissexto(ano) ? 1 : 0);
        if (dias < n) break;
        dias -= n;
        mes++;
    }
    d->ano = (unsigned short)ano;
    d->mes = (unsigned char)(mes + 1);
    d->dia = (unsigned char)(dias + 1);
}

static int acertar(const char *endereco)
{
    /* "servidor" ou "servidor:porta" (a porta padrão do NTP é 123). */
    char servidor[96];
    copia_texto(servidor, endereco, sizeof servidor);
    int porta = 123;
    for (char *p = servidor; *p; p++)
        if (*p == ':') { *p = 0; porta = texto_para_int(p + 1); break; }
    unsigned ip;
    if (dns_resolver(servidor, &ip) < 0) { imprimir_em(ERROS, "relogio: %s não encontrado\n", servidor); return 1; }
    int s = rede_socket(REDE_UDP);
    if (s < 0) { imprimir_em(ERROS, "relogio: sem rede\n"); return 1; }
    rede_conectar(s, ip, porta);
    unsigned char m[48];
    memset(m, 0, sizeof m);
    m[0] = 0x23;                                     /* versão 4, modo "cliente" */
    unsigned segundos = 0;
    for (int tentativa = 0; tentativa < 3 && !segundos; tentativa++) {
        rede_enviar(s, m, sizeof m);
        for (int k = 0; k < 100 && !segundos; k++) {
            unsigned char r[64];
            int n = rede_receber(s, r, sizeof r, NULL);
            if (n >= 48 && (r[0] & 7) == 4) {      /* modo "servidor" */
                unsigned t = ((unsigned)r[40] << 24) | ((unsigned)r[41] << 16) | ((unsigned)r[42] << 8) | r[43];
                segundos = t - 2208988800u;         /* de 1900 para 1970 */
            } else dormir(20);
        }
    }
    rede_fechar(s);
    if (!segundos) { imprimir_em(ERROS, "relogio: o servidor de hora não respondeu\n"); return 1; }
    char fuso_t[8];
    int fuso = config_ler("relogio.fuso", fuso_t, sizeof fuso_t) >= 0 ? texto_para_int(fuso_t) : -3;
    segundos = (unsigned)((int)segundos + fuso * 3600);
    data_t d;
    segundos_para_data(segundos, &d);
    int r = ajustar_data(&d);
    if (r < 0) { imprimir_em(ERROS, "relogio: %s\n", nome_erro(r)); return 1; }
    imprimir("Relógio acertado pela internet: %02u/%02u/%u %02u:%02u:%02u (fuso %d)\n", d.dia, d.mes, d.ano, d.hora, d.minuto, d.segundo, fuso);
    registrar("relogio: acertado por %s: %02u/%02u/%u %02u:%02u:%02u", servidor, d.dia, d.mes, d.ano, d.hora, d.minuto, d.segundo);
    return 0;
}

int principal(int argc, char **argv)
{
    if (argc > 1 && compara_texto(argv[1], "internet") == 0)
        return acertar(argc > 2 ? argv[2] : "pool.ntp.org");
    data_t d;
    ler_data(&d);
    imprimir("%02u/%02u/%u %02u:%02u:%02u\n", d.dia, d.mes, d.ano, d.hora, d.minuto, d.segundo);
    imprimir("Para acertar pela internet: relogio internet [servidor]\n");
    return 0;
}
