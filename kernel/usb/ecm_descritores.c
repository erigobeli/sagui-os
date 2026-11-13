/* ecm_descritores.c - acha o modo "CDC Ethernet" (ECM) nos descritores de
 * configuração de um adaptador USB de rede (H46). Só lê bytes: não depende
 * do resto do kernel, e por isso é testado também no computador
 * (tests/usb/teste_ecm.c).
 *
 * Pela especificação USB CDC 1.2 (classe de comunicação) e ECM 1.2:
 *   - a interface de CONTROLE é de classe 2 (comunicação), subclasse 6 (ECM);
 *     dentro dela vêm descritores "funcionais" (tipo 0x24):
 *       subtipo 0x06 (União): qual interface é a de DADOS;
 *       subtipo 0x0F (Ethernet): o índice do texto com o endereço MAC e o
 *       maior quadro aceito;
 *     e um endpoint de interrupção (avisos de cabo ligado/desligado);
 *   - a interface de DADOS é de classe 0x0A; a configuração alternativa 0
 *     não tem endpoints (rede parada) e a 1 tem dois endpoints bulk, um de
 *     entrada e um de saída, por onde passam os quadros Ethernet inteiros.
 * Adaptadores como o RTL8153 (TP-Link UE300) têm mais de uma configuração:
 * a primeira é a do fabricante e uma das seguintes é a ECM. */
#include "ecm.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool ecm_analisar(const uint8_t *c, int n, ecm_info_t *e)
{
    for (int i = 0; i < (int)sizeof *e; i++) ((uint8_t *)e)[i] = 0;
    if (n < 9 || c[1] != 2) return false;
    e->config = c[5];
    e->iface_controle = e->iface_dados = 0xFF;
    int iface = -1, alt = 0, classe = 0, subclasse = 0;
    uint8_t ep_in = 0, ep_out = 0;
    uint16_t mps_in = 0, mps_out = 0;
    for (int i = 0; i + 2 <= n && c[i] >= 2 && i + c[i] <= n; i += c[i]) {
        const uint8_t *d = c + i;
        if (d[1] == 4 && d[0] >= 9) {                         /* interface */
            /* Fechou uma interface de dados com os dois endpoints: guarda. */
            if (classe == 0x0A && ep_in && ep_out && iface == e->iface_dados && !e->ep_entrada) {
                e->alt_dados = (uint8_t)alt; e->ep_entrada = ep_in; e->ep_saida = ep_out;
                e->mps_entrada = mps_in; e->mps_saida = mps_out;
            }
            iface = d[2]; alt = d[3]; classe = d[5]; subclasse = d[6];
            ep_in = ep_out = 0;
            if (classe == 2 && subclasse == 6 && e->iface_controle == 0xFF) e->iface_controle = (uint8_t)iface;
            if (classe == 2 && subclasse == 0x0D) e->tem_ncm = true;
        } else if (d[1] == 0x24 && d[0] >= 3 && classe == 2 && subclasse == 6 && iface == e->iface_controle) {
            if (d[2] == 0x06 && d[0] >= 5) e->iface_dados = d[4];                 /* União */
            else if (d[2] == 0x0F && d[0] >= 13) { e->i_mac = d[3]; e->max_quadro = le16(d + 8); }   /* Ethernet */
        } else if (d[1] == 5 && d[0] >= 7) {                  /* endpoint */
            uint8_t tipo = d[3] & 3;
            uint16_t mps = (uint16_t)(le16(d + 4) & 0x7FF);
            if (classe == 2 && iface == e->iface_controle && tipo == 3) e->ep_interrupcao = d[2] & 0x0F;
            if (classe == 0x0A && tipo == 2) {
                if (d[2] & 0x80) { ep_in = d[2] & 0x0F; mps_in = mps; }
                else { ep_out = d[2] & 0x0F; mps_out = mps; }
            }
        }
    }
    if (classe == 0x0A && ep_in && ep_out && iface == e->iface_dados && !e->ep_entrada) {
        e->alt_dados = (uint8_t)alt; e->ep_entrada = ep_in; e->ep_saida = ep_out;
        e->mps_entrada = mps_in; e->mps_saida = mps_out;
    }
    return e->iface_controle != 0xFF && e->iface_dados != 0xFF && e->ep_entrada && e->ep_saida;
}

/* O endereço MAC vem como texto: 12 dígitos hexadecimais ("00E04C680001"). */
bool ecm_mac_de_texto(const char *t, uint8_t mac[6])
{
    for (int i = 0; i < 12; i++) {
        char ch = t[i];
        int v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
        if (v < 0) return false;
        if (i % 2 == 0) mac[i / 2] = (uint8_t)(v << 4);
        else mac[i / 2] |= (uint8_t)v;
    }
    return true;
}
