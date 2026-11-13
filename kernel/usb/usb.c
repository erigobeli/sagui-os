/* usb.c - dispositivos USB: enumeração, hubs e conexão a quente (H30).
 *
 * Enumerar um dispositivo novo, como manda a especificação USB 2.0:
 *   1. resetar a porta (do controlador ou do hub) onde ele apareceu;
 *   2. no endereço 0, ler os 8 primeiros bytes do descritor de dispositivo
 *      para saber o tamanho de pacote do endpoint 0;
 *   3. dar a ele um endereço próprio (SET_ADDRESS);
 *   4. ler o descritor de dispositivo inteiro, os textos (fabricante e
 *      produto) e o descritor de configuração com as interfaces e endpoints;
 *   5. escolher a configuração (SET_CONFIGURATION).
 * Se for um hub (classe 9), liga as portas dele e enumera o que estiver
 * em cada uma. Se for armazenamento (classe 8, SCSI, Bulk-Only), passa para
 * o armazenamento.c.
 *
 * Conexão e remoção a quente: uma tarefa do kernel consulta, duas vezes
 * por segundo, o bit "a conexão mudou" das portas do controlador e de cada
 * hub (pedido GET_STATUS de porta). Ver D025. */
#include "usb.h"
#include "console.h"
#include "cpu.h"
#include "processo.h"
#include "relogio.h"
#include "texto.h"

static usb_disp_t disps[USB_MAX_DISP];

usb_disp_t *usb_disp(int i) { return (i >= 0 && i < USB_MAX_DISP && disps[i].usado) ? &disps[i] : NULL; }

int usb_controle(usb_disp_t *d, uint8_t tipo, uint8_t pedido, uint16_t valor, uint16_t indice, void *dados, uint16_t tam)
{
    uint8_t setup[8] = { tipo, pedido, (uint8_t)valor, (uint8_t)(valor >> 8),
                         (uint8_t)indice, (uint8_t)(indice >> 8), (uint8_t)tam, (uint8_t)(tam >> 8) };
    return ehci_controle(d->ctl, d->endereco, d->mps0, setup, dados, tam, (tipo & 0x80) != 0);
}

int usb_descritor(usb_disp_t *d, uint8_t tipo, uint8_t indice, uint16_t idioma, void *buf, uint16_t tam)
{
    int r = USB_ERRO;
    for (int t = 0; t < 3 && r < 0; t++)
        r = usb_controle(d, 0x80, 6, (uint16_t)((tipo << 8) | indice), idioma, buf, tam);
    return r;
}

/* Texto USB (UTF-16LE) para Latin-1: o que não couber vira '?'. */
void usb_ler_texto(usb_disp_t *d, uint8_t indice, uint16_t idioma, char *saida, int max)
{
    saida[0] = 0;
    if (!indice) return;
    uint8_t b[128];
    int n = usb_descritor(d, 3, indice, idioma, b, sizeof b);
    if (n < 2 || b[1] != 3) return;
    int k = 0;
    for (int i = 2; i + 1 < n && i + 1 < b[0] && k < max - 1; i += 2) {
        uint16_t c = (uint16_t)(b[i] | (b[i + 1] << 8));
        saida[k++] = c < 0x100 ? (char)c : '?';
    }
    while (k > 0 && saida[k - 1] == ' ') k--;   /* muitos dispositivos completam com espaços */
    saida[k] = 0;
}

static usb_disp_t *novo(void)
{
    for (int i = 0; i < USB_MAX_DISP; i++)
        if (!disps[i].usado) {
            memset(&disps[i], 0, sizeof disps[i]);
            disps[i].usado = true;
            return &disps[i];
        }
    return NULL;
}

static void remover(usb_disp_t *d);
static void configurar_hub(usb_disp_t *h);

static const char *nome_classe(uint8_t c)
{
    switch (c) {
    case 0x03: return "HID";
    case 0x08: return "armazenamento";
    case 0x09: return "hub";
    case 0x0E: return "video";
    case 0xE0: return "sem fio";
    default: return "outra";
    }
}

static int ultimo_passo;   /* em que passo a última enumeração parou (diagnóstico) */

static usb_disp_t *enumerar(ehci_t *c, usb_disp_t *hub, uint8_t porta)
{
    ultimo_passo = 1;
    usb_disp_t *d = novo();
    if (!d) return NULL;
    d->ctl = c;
    d->hub = hub;
    d->porta = porta;
    d->endereco = 0;
    d->mps0 = 64;
    uint8_t b[256];
    if (usb_descritor(d, 1, 0, 0, b, 8) < 8) goto falhou;
    ultimo_passo = 2;
    d->mps0 = b[7] ? b[7] : 64;
    uint8_t endereco = c->proximo_endereco++;
    if (c->proximo_endereco > 127) c->proximo_endereco = 1;
    if (usb_controle(d, 0x00, 5, endereco, 0, NULL, 0) < 0) goto falhou;
    d->endereco = endereco;
    ehci_esperar_ms(c, 10);
    ultimo_passo = 3;
    if (usb_descritor(d, 1, 0, 0, b, 18) < 18) goto falhou;
    ultimo_passo = 4;
    d->id_fabricante = (uint16_t)(b[8] | (b[9] << 8));
    d->id_produto = (uint16_t)(b[10] | (b[11] << 8));
    d->classe = b[4]; d->subclasse = b[5]; d->protocolo = b[6];
    int configuracoes = b[17];
    uint8_t i_fab = b[14], i_prod = b[15];
    uint16_t idioma = 0x0409;
    uint8_t l[8];
    if (usb_descritor(d, 3, 0, 0, l, 4) >= 4) idioma = (uint16_t)(l[2] | (l[3] << 8));
    usb_ler_texto(d, i_fab, idioma, d->fabricante, sizeof d->fabricante);
    usb_ler_texto(d, i_prod, idioma, d->produto, sizeof d->produto);
    if (!d->fabricante[0]) snformatar(d->fabricante, sizeof d->fabricante, "%04x", d->id_fabricante);
    if (!d->produto[0]) snformatar(d->produto, sizeof d->produto, "%04x", d->id_produto);

    /* Configuração 0: cabeçalho de 9 bytes, depois inteira. */
    if (usb_descritor(d, 2, 0, 0, b, 9) < 9) goto falhou;
    uint16_t total = (uint16_t)(b[2] | (b[3] << 8));
    if (total > sizeof b) total = sizeof b;
    int n = usb_descritor(d, 2, 0, 0, b, total);
    if (n < 9) goto falhou;
    d->config = b[5];
    bool achou_interface = false;
    for (int i = 0; i + 1 < n && b[i] >= 2; i += b[i]) {
        if (b[i + 1] == 4 && !achou_interface) {          /* interface */
            achou_interface = true;
            d->interface = b[i + 2];
            if (d->classe == 0) { d->classe = b[i + 5]; d->subclasse = b[i + 6]; d->protocolo = b[i + 7]; }
        } else if (b[i + 1] == 5 && achou_interface && (b[i + 3] & 3) == 2) {   /* endpoint bulk */
            uint16_t mps = (uint16_t)((b[i + 4] | (b[i + 5] << 8)) & 0x7FF);
            if (b[i + 2] & 0x80) { if (!d->ep_entrada) { d->ep_entrada = b[i + 2] & 0x0F; d->mps_entrada = mps; } }
            else if (!d->ep_saida) { d->ep_saida = b[i + 2] & 0x0F; d->mps_saida = mps; }
        }
    }
    ultimo_passo = 5;
    /* Adaptador de rede (H46)? Ele escolhe a própria configuração (a ECM). */
    if ((configuracoes > 1 || d->classe == 0x02 || d->classe == 0xFF) && rede_usb_conectar(d, configuracoes)) {
        ultimo_passo = 0;
        if (!hub) c->resultado[porta] = 0;
        klog("[USB] conectado: %s %s (%04x:%04x) como adaptador de rede, endereco %u\n",
             d->fabricante, d->produto, d->id_fabricante, d->id_produto, d->endereco);
        return d;
    }
    if (usb_controle(d, 0x00, 9, d->config, 0, NULL, 0) < 0) goto falhou;
    ultimo_passo = 0;
    if (!hub) c->resultado[porta] = 0;
    klog("[USB] conectado: %s %s (%04x:%04x) classe %x (%s), endereco %u, %s %u porta %u\n",
         d->fabricante, d->produto, d->id_fabricante, d->id_produto, d->classe, nome_classe(d->classe),
         d->endereco, hub ? "hub" : "raiz", hub ? hub->endereco : 0, porta);
    if (d->classe == 0x09) configurar_hub(d);
    else if (d->classe == 0x08 && d->subclasse == 0x06 && d->protocolo == 0x50) armaz_conectar(d);
    else if (d->classe == 0x08) klog("[USB] armazenamento com protocolo nao suportado (%x/%x)\n", d->subclasse, d->protocolo);
    return d;
falhou:
    klog("[USB] falha ao enumerar o dispositivo da porta %u (%s), passo %d\n", porta, hub ? "hub" : "raiz", ultimo_passo);
    if (!hub) c->resultado[porta] = ultimo_passo;
    d->usado = false;
    return NULL;
}

/* ------------------------------------------------------------ hubs (USB 2.0, capítulo 11) */

#define PORTA_CONEXAO      0
#define PORTA_RESET        4
#define PORTA_ENERGIA      8
#define C_PORTA_CONEXAO    16
#define C_PORTA_RESET      20

static int hub_estado(usb_disp_t *h, uint8_t p, uint16_t *estado, uint16_t *mudou)
{
    uint8_t b[4];
    int r = usb_controle(h, 0xA3, 0, 0, p, b, 4);
    if (r < 4) return USB_ERRO;
    *estado = (uint16_t)(b[0] | (b[1] << 8));
    *mudou = (uint16_t)(b[2] | (b[3] << 8));
    return 0;
}

static usb_disp_t *filho(usb_disp_t *h, uint8_t p)
{
    for (int i = 0; i < USB_MAX_DISP; i++)
        if (disps[i].usado && disps[i].hub == h && disps[i].porta == p) return &disps[i];
    return NULL;
}

static void hub_porta_conectou(usb_disp_t *h, uint8_t p)
{
    uint16_t estado, mudou;
    usb_controle(h, 0x23, 3, PORTA_RESET, p, NULL, 0);
    for (int i = 0; i < 50; i++) {
        ehci_esperar_ms(h->ctl, 10);
        if (hub_estado(h, p, &estado, &mudou) == 0 && (mudou & (1u << 4))) break;
    }
    usb_controle(h, 0x23, 1, C_PORTA_RESET, p, NULL, 0);
    ehci_esperar_ms(h->ctl, 10);
    if (hub_estado(h, p, &estado, &mudou) < 0 || !(estado & 1) || !(estado & 2)) return;
    if (!(estado & (1u << 10))) {
        klog("[USB] hub %u porta %u: dispositivo de velocidade %s, nao suportado atras de hub\n",
             h->endereco, p, (estado & (1u << 9)) ? "baixa" : "plena");
        return;
    }
    enumerar(h->ctl, h, p);
}

static void configurar_hub(usb_disp_t *h)
{
    uint8_t b[16];
    int n = USB_ERRO;
    for (int t = 0; t < 3 && n < 0; t++) n = usb_controle(h, 0xA0, 6, 0x2900, 0, b, 9);
    if (n < 7) { klog("[USB] hub sem descritor\n"); return; }
    h->hub_portas = b[2];
    uint32_t espera = (uint32_t)b[5] * 2 + 20;
    for (uint8_t p = 1; p <= h->hub_portas; p++)
        usb_controle(h, 0x23, 3, PORTA_ENERGIA, p, NULL, 0);
    ehci_esperar_ms(h->ctl, espera);
    klog("[USB] hub %u com %u portas\n", h->endereco, h->hub_portas);
    for (uint8_t p = 1; p <= h->hub_portas; p++) {
        uint16_t estado, mudou;
        if (hub_estado(h, p, &estado, &mudou) < 0) continue;
        if (mudou & 1) usb_controle(h, 0x23, 1, C_PORTA_CONEXAO, p, NULL, 0);
        if (estado & 1) hub_porta_conectou(h, p);
    }
}

/* ------------------------------------------------------------ remoção */

static void remover(usb_disp_t *d)
{
    for (int i = 0; i < USB_MAX_DISP; i++)
        if (disps[i].usado && disps[i].hub == d) remover(&disps[i]);   /* primeiro o que estava atrás dele */
    if (d->disco) armaz_desconectar(d);
    if (d->rede) rede_usb_desconectar(d);
    klog("[USB] removido: %s %s (endereco %u)\n", d->fabricante, d->produto, d->endereco);
    d->usado = false;
}

/* ------------------------------------------------------------ conexão a quente */

void usb_verificar_conexoes(void)
{
    bool i0 = int_salva_desliga();
    for (int k = 0; k < ehci_quantidade(); k++) {
        ehci_t *c = ehci_controlador(k);
        for (int p = 0; p < c->portas; p++) {
            if (!ehci_porta_mudou(c, p)) continue;
            usb_disp_t *antes = NULL;
            for (int i = 0; i < USB_MAX_DISP; i++)
                if (disps[i].usado && disps[i].ctl == c && !disps[i].hub && disps[i].porta == p) antes = &disps[i];
            if (antes) remover(antes);
            if (ehci_porta_estado(c, p) & 1) {
                ehci_esperar_ms(c, 100);   /* deixa o contato estabilizar */
                if (ehci_porta_resetar(c, p) == 0) enumerar(c, NULL, (uint8_t)p);
                else c->resultado[p] = 9;
            }
        }
    }
    for (int i = 0; i < USB_MAX_DISP; i++) {
        usb_disp_t *h = &disps[i];
        if (!h->usado || h->classe != 0x09) continue;
        for (uint8_t p = 1; p <= h->hub_portas; p++) {
            uint16_t estado, mudou;
            if (hub_estado(h, p, &estado, &mudou) < 0) break;
            if (!(mudou & 1)) continue;
            usb_controle(h, 0x23, 1, C_PORTA_CONEXAO, p, NULL, 0);
            usb_disp_t *f = filho(h, p);
            if (f) remover(f);
            if (estado & 1) { ehci_esperar_ms(h->ctl, 100); hub_porta_conectou(h, p); }
        }
    }
    int_restaura(i0);
}

static void tarefa_usb(void *arg)
{
    (void)arg;
    static const char canal = 0;
    for (;;) {
        usb_verificar_conexoes();
        atual->acordar_em = relogio_tiques() + TIQUES_POR_SEGUNDO / 2;
        int_desliga();
        bloquear(&canal);
        ceder();
        int_liga();
    }
}

void usb_iniciar(void)
{
    int n = ehci_iniciar_todos();
    klog("[USB] %d controlador(es) EHCI\n", n);
    if (!n) return;
    bool i0 = int_salva_desliga();
    for (int k = 0; k < n; k++) {
        ehci_t *c = ehci_controlador(k);
        for (int p = 0; p < c->portas; p++) {
            ehci_porta_mudou(c, p);   /* o que já está ligado entra agora, não como "mudou" */
            if (ehci_porta_estado(c, p) & 1) {
                if (ehci_porta_resetar(c, p) == 0) enumerar(c, NULL, (uint8_t)p);
                else c->resultado[p] = 9;   /* não habilitou depois do reset */
            }
        }
    }
    int_restaura(i0);
    tarefa_kernel("usb", tarefa_usb, NULL);
}

/* Relatório de diagnóstico (comando "usb"): controladores, portas e dispositivos. */
int usb_relatorio(char *s, int max)
{
    int k = 0;
    k += snformatar(s + k, (size_t)(max - k), "Controladores USB 2.0 (EHCI): %d\n", ehci_quantidade());
    for (int c = 0; c < ehci_quantidade() && k < max; c++) {
        ehci_t *e = ehci_controlador(c);
        static const char *bios[] = { "não soltou", "não usava", "soltou" };
        k += snformatar(s + k, (size_t)(max - k), "EHCI %d: PCI %02x:%02x.%x, %04x:%04x, %d portas, %s, BIOS %s, %d companheiros\n", c,
                        e->pci.barramento, e->pci.dispositivo, e->pci.funcao, e->pci.fabricante, e->pci.modelo, e->portas,
                        e->enderecos64 ? "64 bits" : "32 bits", bios[e->bios + 1], e->companheiros);
        for (int p = 0; p < e->portas && k < max; p++) {
            uint32_t v = ehci_porta_estado(e, p);
            if (!(v & 1)) continue;
            static const char *passo[] = { "reconhecido", "não respondeu ao primeiro descritor", "não aceitou o endereço",
                                           "não deu o descritor completo", "não deu a configuração",
                                           "não aceitou a configuração", "?", "?", "?", "não habilitou depois do reset" };
            int r = e->resultado[p];
            k += snformatar(s + k, (size_t)(max - k), "  porta %d: dispositivo conectado, %s (PORTSC %x)\n", p,
                            r < 0 ? "ainda não enumerado" : r <= 9 ? passo[r] : "?", v);
        }
    }
    for (int i = 0; i < USB_MAX_DISP && k < max; i++) {
        usb_disp_t *d = &disps[i];
        if (!d->usado) continue;
        k += snformatar(s + k, (size_t)(max - k), "Dispositivo %u: %s %s (%04x:%04x), classe %s%s\n", d->endereco,
                        d->fabricante, d->produto, d->id_fabricante, d->id_produto, nome_classe(d->classe),
                        d->hub ? ", atrás de um hub" : "");
    }
    return k;
}
