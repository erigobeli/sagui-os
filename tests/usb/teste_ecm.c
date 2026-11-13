/* teste_ecm.c - confere ecm_analisar e ecm_mac_de_texto (kernel/usb/ecm_descritores.c)
 * com descritores montados pela especificação USB CDC 1.2 / ECM 1.2. */
#include <stdio.h>
#include <string.h>
#include "ecm.h"

static int falhas;
#define CONFERE(c, msg) do { if (!(c)) { printf("FALHA: %s\n", msg); falhas++; } else printf("ok: %s\n", msg); } while (0)

/* Configuração do fabricante: uma interface 0xFF com três endpoints. */
static const unsigned char vendor[] = {
    9, 2, 39, 0, 1, 1, 0, 0xA0, 50,
    9, 4, 0, 0, 3, 0xFF, 0xFF, 0, 0,
    7, 5, 0x81, 2, 0x00, 0x02, 0,
    7, 5, 0x02, 2, 0x00, 0x02, 0,
    7, 5, 0x83, 3, 0x02, 0x00, 8,
};
/* Configuração ECM: controle (interface 0) com cabeçalho, união e Ethernet,
 * e um endpoint de interrupção; dados (interface 1) com alt 0 vazia e alt 1
 * com dois endpoints bulk. */
static const unsigned char ecm[] = {
    9, 2, 80, 0, 2, 2, 0, 0xA0, 50,
    9, 4, 0, 0, 1, 0x02, 0x06, 0, 5,
    5, 0x24, 0x00, 0x10, 0x01,                      /* cabeçalho CDC 1.10 */
    5, 0x24, 0x06, 0, 1,                            /* união: controle 0, dados 1 */
    13, 0x24, 0x0F, 3, 0, 0, 0, 0, 0xEA, 0x05, 0, 0, 0,   /* Ethernet: MAC no texto 3, 1514 */
    7, 5, 0x83, 3, 0x10, 0x00, 8,
    9, 4, 1, 0, 0, 0x0A, 0, 0, 0,
    9, 4, 1, 1, 2, 0x0A, 0, 0, 0,
    7, 5, 0x81, 2, 0x00, 0x02, 0,
    7, 5, 0x02, 2, 0x00, 0x02, 0,
};
static const unsigned char ncm[] = {
    9, 2, 27, 0, 2, 1, 0, 0xA0, 50,
    9, 4, 0, 0, 1, 0x02, 0x0D, 0, 0,
    9, 4, 1, 0, 0, 0x0A, 0, 1, 0,
};

int main(void)
{
    ecm_info_t e;
    CONFERE(!ecm_analisar(vendor, sizeof vendor, &e), "configuração do fabricante não é ECM");
    CONFERE(ecm_analisar(ecm, sizeof ecm, &e), "configuração ECM reconhecida");
    CONFERE(e.config == 2, "valor da configuração = 2");
    CONFERE(e.iface_controle == 0 && e.iface_dados == 1 && e.alt_dados == 1, "interfaces: controle 0, dados 1 (alternativa 1)");
    CONFERE(e.ep_entrada == 1 && e.ep_saida == 2 && e.mps_entrada == 512 && e.mps_saida == 512, "endpoints bulk 1 (entrada) e 2 (saída), 512 bytes");
    CONFERE(e.ep_interrupcao == 3, "endpoint de interrupção 3");
    CONFERE(e.i_mac == 3 && e.max_quadro == 1514, "MAC no texto 3, quadro máximo 1514");
    CONFERE(!ecm_analisar(ncm, sizeof ncm, &e) && e.tem_ncm, "configuração só NCM: não é ECM, mas é reconhecida como NCM");
    unsigned char quebrado[sizeof ecm];
    memcpy(quebrado, ecm, sizeof ecm);
    quebrado[9 + 9 + 5 + 5 + 13 + 7 + 9 + 9] = 0;   /* um descritor com tamanho zero */
    CONFERE(!ecm_analisar(quebrado, sizeof quebrado, &e), "descritor quebrado (tamanho 0) não trava nem engana");
    CONFERE(!ecm_analisar(ecm, 30, &e), "configuração cortada no meio não engana");
    unsigned char mac[6];
    CONFERE(ecm_mac_de_texto("00E04C680001", mac) && mac[0] == 0x00 && mac[1] == 0xE0 && mac[5] == 0x01, "MAC 00E04C680001");
    CONFERE(ecm_mac_de_texto("a0b1c2d3e4f5", mac) && mac[0] == 0xA0 && mac[5] == 0xF5, "MAC em minúsculas");
    CONFERE(!ecm_mac_de_texto("00E04C6800", mac), "MAC curto recusado");
    return falhas != 0;
}
