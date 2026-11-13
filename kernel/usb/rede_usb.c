/* rede_usb.c - adaptadores USB de rede da classe CDC Ethernet (H46).
 *
 * É o caminho de rede do G470: a placa interna dele (Atheros AR8152) não tem
 * manual público, e o adaptador escolhido (TP-Link UE300, chip Realtek
 * RTL8153) também fala o modo padrão "CDC Ethernet" (ECM), descrito em
 * especificações públicas do USB-IF.
 *
 * Como funciona:
 *   1. na conexão, o usb.c pergunta aqui se o dispositivo é um adaptador de
 *      rede: lemos todas as configurações dele e procuramos a de ECM
 *      (ecm_descritores.c). O RTL8153 começa na configuração do fabricante;
 *      escolher a ECM é só um SET_CONFIGURATION com o número dela;
 *   2. a interface de dados vai para a configuração alternativa com os
 *      endpoints (SET_INTERFACE): é isso que "liga" a rede no adaptador;
 *   3. o MAC é um texto do dispositivo (12 dígitos hexadecimais);
 *   4. SET_ETHERNET_PACKET_FILTER: quadros para nós, difusão e multicast;
 *   5. cada quadro Ethernet vai inteiro num envio bulk de saída (sem o CRC,
 *      que o adaptador calcula); se o tamanho for múltiplo do pacote USB,
 *      manda um pacote vazio no fim, que marca o fim do quadro;
 *   6. receber: um pedido bulk de entrada que desiste em ~1 ms se não houver
 *      quadro (ehci_bulk_tentar), chamado pela tarefa da rede a cada tique.
 * O aviso de cabo ligado (endpoint de interrupção) não é lido: o EHCI do
 * SaguiOS não tem a lista periódica. O cabo é dado como ligado; sem cabo, o
 * DHCP simplesmente não recebe resposta (e o registro mostra).
 *
 * Tudo vai para o diário: "registro USB" e "registro REDE" no G470. */
#include "usb.h"
#include "ecm.h"
#include "rede.h"
#include "console.h"
#include "texto.h"
#include "memoria.h"

typedef struct {
    bool        usado;
    usb_disp_t *d;
    ecm_info_t  e;
    placa_t     placa;
    uint8_t     alterna_in, alterna_out;
    uint32_t    quadros_in, quadros_out, erros;
} adaptador_t;

static adaptador_t adaptadores[2];

static bool enviar(placa_t *p, const void *quadro, uint32_t tam)
{
    adaptador_t *a = p->dados;
    if (!a->usado) return false;
    int r = ehci_bulk(a->d->ctl, a->d->endereco, a->e.ep_saida, a->e.mps_saida, false, &a->alterna_out, (void *)quadro, tam);
    if (r >= 0 && tam % a->e.mps_saida == 0)       /* pacote vazio: fim do quadro */
        ehci_bulk(a->d->ctl, a->d->endereco, a->e.ep_saida, a->e.mps_saida, false, &a->alterna_out, (void *)quadro, 0);
    if (r < 0) {
        if (a->erros++ < 5) klog("[REDE-USB] envio de %u bytes falhou (%d)\n", tam, r);
        return false;
    }
    if (a->quadros_out++ == 0) klog("[REDE-USB] primeiro quadro enviado (%u bytes)\n", tam);
    return true;
}

static int receber(placa_t *p, void *quadro, uint32_t max)
{
    adaptador_t *a = p->dados;
    if (!a->usado) return 0;
    static uint8_t buf[2048];
    int r = ehci_bulk_tentar(a->d->ctl, a->d->endereco, a->e.ep_entrada, a->e.mps_entrada, &a->alterna_in, buf, sizeof buf);
    if (r < 0) {
        if (a->erros++ < 5) klog("[REDE-USB] recepcao falhou (%d)\n", r);
        return 0;
    }
    if (r == 0) return 0;
    if ((uint32_t)r > max) r = (int)max;
    memcpy(quadro, buf, (uint32_t)r);
    if (a->quadros_in++ == 0) klog("[REDE-USB] primeiro quadro recebido (%d bytes)\n", r);
    return r;
}

static bool cabo(placa_t *p) { return ((adaptador_t *)p->dados)->usado; }

/* Chamado pelo usb.c antes de escolher a configuração padrão. Devolve true
 * se o dispositivo é um adaptador de rede ECM e foi configurado aqui. */
bool rede_usb_conectar(usb_disp_t *d, int configuracoes)
{
    static uint8_t cfg[512];
    ecm_info_t e;
    bool achou = false, viu_ncm = false;
    for (int i = 0; i < configuracoes && i < 4 && !achou; i++) {
        int r = usb_descritor(d, 2, (uint8_t)i, 0, cfg, 9);
        if (r < 9) continue;
        uint16_t total = (uint16_t)(cfg[2] | (cfg[3] << 8));
        if (total > sizeof cfg) total = sizeof cfg;
        int n = usb_descritor(d, 2, (uint8_t)i, 0, cfg, total);
        if (n < 9) continue;
        achou = ecm_analisar(cfg, n, &e);
        viu_ncm |= e.tem_ncm;
        klog("[REDE-USB] %04x:%04x configuracao %d (valor %u, %d bytes): %s\n", d->id_fabricante, d->id_produto, i, cfg[5], n,
             achou ? "CDC Ethernet (ECM)" : e.tem_ncm ? "NCM (ainda sem suporte)" : "outra");
    }
    if (!achou) {
        if (viu_ncm || d->classe == 0x02) klog("[REDE-USB] %s: adaptador de rede sem o modo ECM; nao suportado ainda\n", d->produto);
        return false;
    }
    adaptador_t *a = NULL;
    for (int i = 0; i < 2 && !a; i++) if (!adaptadores[i].usado) a = &adaptadores[i];
    if (!a) return false;
    memset(a, 0, sizeof *a);
    a->d = d;
    a->e = e;
    if (usb_controle(d, 0x00, 9, e.config, 0, NULL, 0) < 0) { klog("[REDE-USB] SET_CONFIGURATION %u falhou\n", e.config); return false; }
    d->config = e.config;
    ehci_esperar_ms(d->ctl, 20);
    if (usb_controle(d, 0x01, 11, e.alt_dados, e.iface_dados, NULL, 0) < 0)   /* SET_INTERFACE: liga os endpoints */
        klog("[REDE-USB] SET_INTERFACE %u/%u falhou (seguindo assim mesmo)\n", e.iface_dados, e.alt_dados);
    char mac_texto[16];
    usb_ler_texto(d, e.i_mac, 0x0409, mac_texto, sizeof mac_texto);
    if (!ecm_mac_de_texto(mac_texto, a->placa.mac)) {
        /* Sem MAC legível: um endereço "local" (bit 1 do primeiro byte) fixo. */
        uint8_t local[6] = { 0x02, 0x53, 0x47, 0x00, (uint8_t)d->id_produto, (uint8_t)d->endereco };
        memcpy(a->placa.mac, local, 6);
        klog("[REDE-USB] MAC ilegivel ('%s'): usando um endereco local\n", mac_texto);
    }
    /* Filtro: multicast (2), para nós (4), difusão (8). */
    if (usb_controle(d, 0x21, 0x43, 0x000E, e.iface_controle, NULL, 0) < 0) klog("[REDE-USB] filtro de pacotes recusado\n");
    snformatar(a->placa.nome, sizeof a->placa.nome, "USB: %s", d->produto[0] ? d->produto : "adaptador");
    a->placa.enviar = enviar;
    a->placa.receber = receber;
    a->placa.cabo = cabo;
    a->placa.dados = a;
    a->usado = true;
    d->rede = a;
    klog("[REDE-USB] %s pronto: MAC %02x:%02x:%02x:%02x:%02x:%02x, dados na interface %u (alt %u), entrada ep %u (%u), saida ep %u (%u)\n",
         d->produto, a->placa.mac[0], a->placa.mac[1], a->placa.mac[2], a->placa.mac[3], a->placa.mac[4], a->placa.mac[5],
         e.iface_dados, e.alt_dados, e.ep_entrada, e.mps_entrada, e.ep_saida, e.mps_saida);
    rede_placa_usb(&a->placa, true);
    return true;
}

void rede_usb_desconectar(usb_disp_t *d)
{
    adaptador_t *a = d->rede;
    if (!a) return;
    rede_placa_usb(&a->placa, false);
    a->usado = false;
    d->rede = NULL;
    klog("[REDE-USB] adaptador removido (%u quadros recebidos, %u enviados)\n", a->quadros_in, a->quadros_out);
}
