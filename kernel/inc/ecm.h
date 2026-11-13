/* ecm.h - descritores CDC Ethernet (ECM) de adaptadores USB de rede (H46). */
#ifndef SAGUI_ECM_H
#define SAGUI_ECM_H
#include "tipos.h"

typedef struct {
    uint8_t  config;             /* bConfigurationValue */
    uint8_t  iface_controle, iface_dados, alt_dados;
    uint8_t  ep_entrada, ep_saida, ep_interrupcao;
    uint16_t mps_entrada, mps_saida;
    uint8_t  i_mac;              /* índice do texto com o MAC */
    uint16_t max_quadro;
    bool     tem_ncm;            /* a configuração tem NCM (não suportado ainda) */
} ecm_info_t;

bool ecm_analisar(const uint8_t *cfg, int n, ecm_info_t *e);
bool ecm_mac_de_texto(const char *t, uint8_t mac[6]);

#endif
