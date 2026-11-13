/* rede.c - a pilha de rede do SaguiOS: Ethernet, ARP, IPv4, ICMP, UDP,
 * DHCP e os sockets dos programas (H44, H45).
 *
 * Tudo roda numa tarefa do kernel ("rede") que acorda a cada tique: tira os
 * quadros que chegaram na placa, entrega cada um à camada certa e cuida dos
 * relógios (pedido de DHCP, pergunta de ARP repetida, retransmissão do TCP).
 *
 * Como um pacote IP chega ao destino:
 *   - se o destino está na mesma rede (mesma máscara), vai direto para ele;
 *     senão, vai para o roteador. Em qualquer caso é preciso o endereço
 *     físico (MAC) do próximo salto, que o ARP descobre perguntando "quem tem
 *     o IP x?" para todos. Enquanto a resposta não chega, o pacote espera
 *     numa fila pequena.
 *
 * Os programas usam a rede por sockets: um número que representa uma
 * "conversa" UDP, ICMP (ping) ou TCP. As chamadas não esperam: receber
 * devolve 0 se nada chegou ainda, e o programa tenta de novo depois. */
#include "rede.h"
#include "console.h"
#include "cpu.h"
#include "memoria.h"
#include "processo.h"
#include "relogio.h"
#include "sistema.h"
#include "texto.h"
#include "configuracoes.h"

#define ETH_IP  0x0800
#define ETH_ARP 0x0806
#define IP_ICMP 1
#define IP_TCP  6
#define IP_UDP  17

static void relogio_pela_internet(void);
static placa_t *placa;
static ip4_t meu_ip, mascara, roteador, dns;
static bool configurado, modo_dhcp = true;
static uint32_t pacotes_rx, pacotes_tx;
static uint16_t ident_ip;
static uint8_t quadro_rx[ETH_MAX], quadro_tx[ETH_MAX];
static const uint8_t difusao[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

ip4_t rede_meu_ip(void) { return meu_ip; }
bool rede_tem_placa(void) { return placa != NULL; }

/* Soma de verificação da Internet: soma de palavras de 16 bits em
 * complemento de um, dobrando o "vai um" de volta, e invertida no fim. */
uint16_t soma_verificacao(const void *dados, uint32_t tam, uint32_t inicial)
{
    const uint8_t *p = dados;
    uint32_t s = inicial;
    for (uint32_t i = 0; i + 1 < tam; i += 2) s += (uint32_t)((p[i] << 8) | p[i + 1]);
    if (tam & 1) s += (uint32_t)(p[tam - 1] << 8);
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return troca16((uint16_t)~s);
}

static void ip_texto(ip4_t ip, char *t, int max)
{
    snformatar(t, (size_t)max, "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
}

/* ------------------------------------------------------------ Ethernet */

static bool eth_enviar(const uint8_t *destino, uint16_t tipo, const void *dados, uint32_t tam)
{
    if (!placa || tam + 14 > ETH_MAX) return false;
    memcpy(quadro_tx, destino, 6);
    memcpy(quadro_tx + 6, placa->mac, 6);
    quadro_tx[12] = (uint8_t)(tipo >> 8);
    quadro_tx[13] = (uint8_t)tipo;
    memcpy(quadro_tx + 14, dados, tam);
    bool ok = placa->enviar(placa, quadro_tx, tam + 14);
    if (ok) pacotes_tx++;
    return ok;
}

/* ------------------------------------------------------------ ARP */

#define MAX_ARP 16
static struct { ip4_t ip; uint8_t mac[6]; bool usado; } tabela_arp[MAX_ARP];
static int arp_prox;

/* Pacotes esperando a resposta do ARP. */
#define MAX_ESPERA 4
static struct { bool usado; ip4_t salto; uint32_t tam, tentativas, quando; uint8_t dados[ETH_MAX]; } espera[MAX_ESPERA];

static bool arp_achar(ip4_t ip, uint8_t *mac)
{
    for (int i = 0; i < MAX_ARP; i++)
        if (tabela_arp[i].usado && tabela_arp[i].ip == ip) { memcpy(mac, tabela_arp[i].mac, 6); return true; }
    return false;
}

static void arp_guardar(ip4_t ip, const uint8_t *mac)
{
    for (int i = 0; i < MAX_ARP; i++)
        if (tabela_arp[i].usado && tabela_arp[i].ip == ip) { memcpy(tabela_arp[i].mac, mac, 6); return; }
    tabela_arp[arp_prox].usado = true;
    tabela_arp[arp_prox].ip = ip;
    memcpy(tabela_arp[arp_prox].mac, mac, 6);
    arp_prox = (arp_prox + 1) % MAX_ARP;
}

static void arp_enviar(uint16_t operacao, const uint8_t *mac_destino, ip4_t ip_destino)
{
    if (!placa) return;                   /* o adaptador USB pode ter saído */
    uint8_t p[28];
    p[0] = 0; p[1] = 1;                   /* Ethernet */
    p[2] = 0x08; p[3] = 0x00;             /* IPv4 */
    p[4] = 6; p[5] = 4;
    p[6] = 0; p[7] = (uint8_t)operacao;   /* 1 pergunta, 2 resposta */
    memcpy(p + 8, placa->mac, 6);
    memcpy(p + 14, &meu_ip, 4);
    memcpy(p + 18, operacao == 1 ? (const uint8_t *)"\0\0\0\0\0\0" : mac_destino, 6);
    memcpy(p + 24, &ip_destino, 4);
    eth_enviar(operacao == 1 ? difusao : mac_destino, ETH_ARP, p, 28);
}

static void arp_receber(const uint8_t *p, uint32_t tam)
{
    if (tam < 28 || p[2] != 0x08 || p[3] != 0x00) return;
    ip4_t quem, alvo;
    memcpy(&quem, p + 14, 4);
    memcpy(&alvo, p + 24, 4);
    if (quem) arp_guardar(quem, p + 8);
    if (p[7] == 1 && configurado && alvo == meu_ip) arp_enviar(2, p + 8, quem);
    /* A resposta pode liberar pacotes que esperavam. */
    for (int i = 0; i < MAX_ESPERA; i++)
        if (espera[i].usado && espera[i].salto == quem) {
            eth_enviar(p + 8, ETH_IP, espera[i].dados, espera[i].tam);
            espera[i].usado = false;
        }
}

static void arp_tique(void)
{
    for (int i = 0; i < MAX_ESPERA; i++) {
        if (!espera[i].usado || relogio_tiques() - espera[i].quando < TIQUES_POR_SEGUNDO / 2) continue;
        if (++espera[i].tentativas > 6) { espera[i].usado = false; continue; }   /* ninguém respondeu */
        espera[i].quando = relogio_tiques();
        arp_enviar(1, NULL, espera[i].salto);
    }
}

/* ------------------------------------------------------------ IPv4 */

bool ip_enviar(ip4_t destino, uint8_t protocolo, const void *dados, uint32_t tam)
{
    if (!placa || tam + 20 > ETH_MAX - 14) return false;
    uint8_t pacote[ETH_MAX];
    uint8_t *h = pacote;
    h[0] = 0x45; h[1] = 0;
    uint16_t total = (uint16_t)(20 + tam);
    h[2] = (uint8_t)(total >> 8); h[3] = (uint8_t)total;
    ident_ip++;
    h[4] = (uint8_t)(ident_ip >> 8); h[5] = (uint8_t)ident_ip;
    h[6] = 0x40; h[7] = 0;                /* não fragmentar */
    h[8] = 64; h[9] = protocolo;
    h[10] = h[11] = 0;
    memcpy(h + 12, &meu_ip, 4);
    memcpy(h + 16, &destino, 4);
    uint16_t s = soma_verificacao(h, 20, 0);
    memcpy(h + 10, &s, 2);
    memcpy(pacote + 20, dados, tam);

    uint8_t mac[6];
    if (destino == 0xFFFFFFFFu || (configurado && destino == (meu_ip | ~mascara))) {
        bool ok = eth_enviar(difusao, ETH_IP, pacote, total);
        return ok;
    }
    ip4_t salto = ((destino ^ meu_ip) & mascara) == 0 ? destino : roteador;
    if (arp_achar(salto, mac)) return eth_enviar(mac, ETH_IP, pacote, total);
    for (int i = 0; i < MAX_ESPERA; i++)
        if (!espera[i].usado) {
            espera[i].usado = true;
            espera[i].salto = salto;
            espera[i].tam = total;
            espera[i].tentativas = 0;
            espera[i].quando = relogio_tiques();
            memcpy(espera[i].dados, pacote, total);
            arp_enviar(1, NULL, salto);
            return true;
        }
    return false;
}

/* ------------------------------------------------------------ sockets */

#define MAX_SOCKETS 16
#define FILA 8
typedef struct {
    bool     usado;
    int      pid, tipo;
    uint16_t porta_local, porta_remota;
    ip4_t    ip_remoto;
    struct { uint32_t tam; ip4_t origem; uint16_t porta; uint8_t dados[1500]; } *fila;
    int      ini, qtd;
    int      tcp;                 /* conexão no tcp.c (-1 = ainda não conectado) */
} socket_t;
static socket_t sockets[MAX_SOCKETS];
static uint16_t proxima_porta = 49152;

static void entregar(socket_t *s, ip4_t origem, uint16_t porta, const uint8_t *dados, uint32_t tam)
{
    if (s->qtd >= FILA || !s->fila) return;   /* cheia: descarta (UDP e ping podem perder) */
    int i = (s->ini + s->qtd) % FILA;
    if (tam > 1500) tam = 1500;
    s->fila[i].tam = tam;
    s->fila[i].origem = origem;
    s->fila[i].porta = porta;
    memcpy(s->fila[i].dados, dados, tam);
    s->qtd++;
}

/* ------------------------------------------------------------ ICMP */

static void icmp_receber(ip4_t origem, const uint8_t *p, uint32_t tam)
{
    if (tam < 8 || soma_verificacao(p, tam, 0) != 0) return;
    if (p[0] == 8) {                      /* pedido de eco: responde com o mesmo conteúdo */
        uint8_t r[1480];
        if (tam > sizeof r) return;
        memcpy(r, p, tam);
        r[0] = 0;
        r[2] = r[3] = 0;
        uint16_t s = soma_verificacao(r, tam, 0);
        memcpy(r + 2, &s, 2);
        ip_enviar(origem, IP_ICMP, r, tam);
    } else if (p[0] == 0) {               /* resposta de eco: para os sockets de ping */
        for (int i = 0; i < MAX_SOCKETS; i++)
            if (sockets[i].usado && sockets[i].tipo == REDE_ICMP) entregar(&sockets[i], origem, 0, p, tam);
    }
}

/* ------------------------------------------------------------ UDP e DHCP */

static bool udp_enviar(ip4_t destino, uint16_t de, uint16_t para, const void *dados, uint32_t tam)
{
    uint8_t s[1480];
    if (tam + 8 > sizeof s) return false;
    s[0] = (uint8_t)(de >> 8); s[1] = (uint8_t)de;
    s[2] = (uint8_t)(para >> 8); s[3] = (uint8_t)para;
    uint16_t t = (uint16_t)(tam + 8);
    s[4] = (uint8_t)(t >> 8); s[5] = (uint8_t)t;
    s[6] = s[7] = 0;
    memcpy(s + 8, dados, tam);
    /* Soma com o "pseudocabeçalho" (origem, destino, protocolo, tamanho). */
    uint32_t inicial = ((meu_ip & 0xFF) << 8 | ((meu_ip >> 8) & 0xFF)) + (((meu_ip >> 16) & 0xFF) << 8 | (meu_ip >> 24)) +
                       ((destino & 0xFF) << 8 | ((destino >> 8) & 0xFF)) + (((destino >> 16) & 0xFF) << 8 | (destino >> 24)) +
                       IP_UDP + t;
    uint16_t soma = soma_verificacao(s, t, inicial);
    if (soma == 0) soma = 0xFFFF;
    memcpy(s + 6, &soma, 2);
    return ip_enviar(destino, IP_UDP, s, t);
}

/* DHCP: quatro mensagens. O SaguiOS pede um endereço para todos (DISCOVER),
 * o servidor oferece (OFFER), o SaguiOS aceita aquela oferta (REQUEST) e o
 * servidor confirma (ACK), junto com a máscara, o roteador e o DNS. */
static uint32_t dhcp_xid, dhcp_quando, dhcp_tentativas;
static ip4_t dhcp_oferta, dhcp_servidor;
static int dhcp_estado;   /* 0 parado, 1 esperando oferta, 2 esperando confirmação, 3 pronto */

static void dhcp_enviar(int tipo)
{
    if (!placa) return;
    uint8_t m[300];
    memset(m, 0, sizeof m);
    m[0] = 1; m[1] = 1; m[2] = 6;
    memcpy(m + 4, &dhcp_xid, 4);
    m[10] = 0x80;                          /* resposta por difusão: ainda não temos IP */
    memcpy(m + 28, placa->mac, 6);
    m[236] = 99; m[237] = 130; m[238] = 83; m[239] = 99;   /* "biscoito mágico" do DHCP */
    int k = 240;
    m[k++] = 53; m[k++] = 1; m[k++] = (uint8_t)tipo;
    if (tipo == 3) {
        m[k++] = 50; m[k++] = 4; memcpy(m + k, &dhcp_oferta, 4); k += 4;
        m[k++] = 54; m[k++] = 4; memcpy(m + k, &dhcp_servidor, 4); k += 4;
    }
    m[k++] = 12; m[k++] = 7; memcpy(m + k, "saguios", 7); k += 7;
    m[k++] = 55; m[k++] = 3; m[k++] = 1; m[k++] = 3; m[k++] = 6;
    m[k++] = 255;
    ip4_t guardado = meu_ip;
    meu_ip = 0;
    udp_enviar(0xFFFFFFFFu, 68, 67, m, (uint32_t)(k < 300 ? 300 : k));
    meu_ip = guardado;
    dhcp_quando = relogio_tiques();
}

static void dhcp_receber(const uint8_t *m, uint32_t tam)
{
    if (tam < 240 || m[0] != 2 || memcmp(m + 4, &dhcp_xid, 4) != 0) return;
    if (m[236] != 99 || m[237] != 130 || m[238] != 83 || m[239] != 99) return;
    int tipo = 0;
    ip4_t masc = 0, rot = 0, servidor_dns = 0, servidor = 0;
    for (uint32_t k = 240; k < tam && m[k] != 255;) {
        if (m[k] == 0) { k++; continue; }
        uint8_t op = m[k], n = m[k + 1];
        const uint8_t *v = m + k + 2;
        if (k + 2 + n > tam) break;
        if (op == 53 && n >= 1) tipo = v[0];
        else if (op == 1 && n >= 4) memcpy(&masc, v, 4);
        else if (op == 3 && n >= 4) memcpy(&rot, v, 4);
        else if (op == 6 && n >= 4) memcpy(&servidor_dns, v, 4);
        else if (op == 54 && n >= 4) memcpy(&servidor, v, 4);
        k += 2u + n;
    }
    ip4_t oferecido;
    memcpy(&oferecido, m + 16, 4);
    if (tipo == 2 && dhcp_estado == 1) {          /* OFFER */
        dhcp_oferta = oferecido;
        dhcp_servidor = servidor;
        dhcp_estado = 2;
        dhcp_tentativas = 0;
        dhcp_enviar(3);
    } else if (tipo == 5 && dhcp_estado == 2) {   /* ACK */
        meu_ip = oferecido;
        mascara = masc ? masc : IP4(255, 255, 255, 0);
        roteador = rot;
        dns = servidor_dns;
        configurado = true;
        dhcp_estado = 3;
        char a[16], b[16], c[16], d[16];
        ip_texto(meu_ip, a, 16); ip_texto(mascara, b, 16); ip_texto(roteador, c, 16); ip_texto(dns, d, 16);
        klog("[REDE] DHCP: endereco %s, mascara %s, roteador %s, DNS %s\n", a, b, c, d);
        relogio_pela_internet();
    }
}

static void dhcp_tique(void)
{
    if (!modo_dhcp || dhcp_estado == 3 || !placa || !placa->cabo(placa)) return;
    if (dhcp_estado != 0 && relogio_tiques() - dhcp_quando < 2 * TIQUES_POR_SEGUNDO) return;
    if (dhcp_estado == 2 && ++dhcp_tentativas > 3) dhcp_estado = 0;   /* a confirmação não veio: recomeça */
    if (dhcp_estado == 2) { dhcp_enviar(3); return; }
    dhcp_xid = 0x5A600000u ^ relogio_tiques() ^ ((uint32_t)placa->mac[5] << 8);
    dhcp_estado = 1;
    dhcp_enviar(1);
}

static void udp_receber(ip4_t origem, const uint8_t *s, uint32_t tam)
{
    if (tam < 8) return;
    uint16_t de = (uint16_t)((s[0] << 8) | s[1]), para = (uint16_t)((s[2] << 8) | s[3]);
    uint16_t t = (uint16_t)((s[4] << 8) | s[5]);
    if (t < 8 || t > tam) return;
    if (para == 68) { dhcp_receber(s + 8, t - 8u); return; }
    for (int i = 0; i < MAX_SOCKETS; i++) {
        socket_t *k = &sockets[i];
        if (k->usado && k->tipo == REDE_UDP && k->porta_local == para &&
            (!k->ip_remoto || (k->ip_remoto == origem && k->porta_remota == de)))
            entregar(k, origem, de, s + 8, t - 8u);
    }
}

/* Com "relógio automático" ligado (H49), assim que a rede fica pronta o
 * kernel roda o programa que acerta o relógio pelo NTP. */
static void relogio_pela_internet(void)
{
    const char *auto_ = config_obter("relogio.automatico");
    if (!auto_ || txt_compara(auto_, "sim") != 0) return;
    const char *servidor = config_obter("relogio.servidor");
    char *argv[] = { "relogio", "internet", (char *)(servidor ? servidor : "pool.ntp.org"), NULL };
    int pid = processo_executar("/bin/relogio", 3, argv, NULL);
    klog("[REDE] relogio automatico: processo %d\n", pid);
}

/* ------------------------------------------------------------ recepção */

static void ip_receber(const uint8_t *h, uint32_t tam)
{
    if (tam < 20 || (h[0] >> 4) != 4) return;
    uint32_t cab = (uint32_t)(h[0] & 0xF) * 4, total = (uint32_t)((h[2] << 8) | h[3]);
    if (cab < 20 || total > tam || total < cab || soma_verificacao(h, cab, 0) != 0) return;
    if ((h[6] & 0x20) || ((h[6] & 0x1F) | h[7])) return;   /* pedaços (fragmentos): não tratamos */
    ip4_t origem, destino;
    memcpy(&origem, h + 12, 4);
    memcpy(&destino, h + 16, 4);
    bool para_mim = destino == meu_ip || destino == 0xFFFFFFFFu || (configurado && destino == (meu_ip | ~mascara)) ||
                    (!configurado && h[9] == IP_UDP);
    if (!para_mim) return;
    const uint8_t *dados = h + cab;
    uint32_t n = total - cab;
    if (h[9] == IP_ICMP) icmp_receber(origem, dados, n);
    else if (h[9] == IP_UDP) udp_receber(origem, dados, n);
    else if (h[9] == IP_TCP) tcp_receber(origem, dados, n);
}

/* Só para os testes (H48): a configuração "rede.teste_perda" = N descarta um
 * em cada N quadros que chegam, para conferir que o TCP se recupera. */
static uint32_t perda_teste, contador_perda;

static void processar_quadros(void)
{
    for (int i = 0; i < 64; i++) {
        int n = placa->receber(placa, quadro_rx, sizeof quadro_rx);
        if (n == 0) break;
        if (n < 14) continue;
        if (perda_teste && ++contador_perda % perda_teste == 0) continue;
        pacotes_rx++;
        uint16_t tipo = (uint16_t)((quadro_rx[12] << 8) | quadro_rx[13]);
        if (tipo == ETH_ARP) arp_receber(quadro_rx + 14, (uint32_t)n - 14);
        else if (tipo == ETH_IP) ip_receber(quadro_rx + 14, (uint32_t)n - 14);
    }
}

/* ------------------------------------------------------------ configuração */

static ip4_t ler_ip(const char *t)
{
    uint32_t partes[4] = { 0, 0, 0, 0 };
    int k = 0;
    for (; *t && k < 4; t++) {
        if (*t == '.') k++;
        else if (*t >= '0' && *t <= '9') partes[k] = partes[k] * 10 + (uint32_t)(*t - '0');
        else return 0;
    }
    if (k != 3) return 0;
    return IP4(partes[0] & 255, partes[1] & 255, partes[2] & 255, partes[3] & 255);
}

/* Configurações "rede.modo" (dhcp ou manual), "rede.ip", "rede.mascara",
 * "rede.roteador" e "rede.dns" (H47). */
static void aplicar_configuracoes(void)
{
    static uint32_t vista = 0xFFFFFFFFu;
    if (config_geracao() == vista) return;
    vista = config_geracao();
    const char *perda = config_obter("rede.teste_perda");
    perda_teste = 0;
    for (const char *k = perda; k && *k >= '0' && *k <= '9'; k++) perda_teste = perda_teste * 10 + (uint32_t)(*k - '0');
    const char *modo = config_obter("rede.modo");
    bool manual = modo && txt_compara(modo, "manual") == 0;
    if (manual) {
        const char *ip = config_obter("rede.ip"), *m = config_obter("rede.mascara");
        const char *r = config_obter("rede.roteador"), *d = config_obter("rede.dns");
        ip4_t novo = ip ? ler_ip(ip) : 0;
        if (!novo) return;
        meu_ip = novo;
        mascara = m ? ler_ip(m) : IP4(255, 255, 255, 0);
        roteador = r ? ler_ip(r) : 0;
        dns = d ? ler_ip(d) : 0;
        configurado = true;
        modo_dhcp = false;
        klog("[REDE] configuracao manual aplicada\n");
    } else if (!modo_dhcp) {
        modo_dhcp = true;
        configurado = false;
        dhcp_estado = 0;
        meu_ip = 0;
    }
}

/* ------------------------------------------------------------ a tarefa */

static void tarefa_rede(void *arg)
{
    (void)arg;
    static const char canal = 0;
    bool cabo_antes = false;
    for (;;) {
        bool i0 = int_salva_desliga();
        if (!placa) {                             /* sem placa (ainda): espera um adaptador USB aparecer */
            cabo_antes = false;
            int_restaura(i0);
            atual->acordar_em = relogio_tiques() + 10;
            int_desliga();
            bloquear(&canal);
            ceder();
            int_liga();
            continue;
        }
        aplicar_configuracoes();
        bool cabo = placa->cabo(placa);
        if (cabo != cabo_antes) {
            klog("[REDE] cabo %s\n", cabo ? "ligado" : "desligado");
            if (!cabo && modo_dhcp) { configurado = false; dhcp_estado = 0; }
            cabo_antes = cabo;
        }
        processar_quadros();
        dhcp_tique();
        arp_tique();
        tcp_tique();
        int_restaura(i0);
        atual->acordar_em = relogio_tiques() + 1;
        int_desliga();
        bloquear(&canal);
        ceder();
        int_liga();
    }
}

void rede_iniciar(void)
{
    /* O USB começa antes: um adaptador USB pode já estar aqui. A placa
     * interna, se houver, tem preferência. */
    placa_t *interna = e1000_iniciar();
    if (interna) placa = interna;
    if (!placa) klog("[REDE] nenhuma placa de rede com driver (um adaptador USB pode aparecer depois)\n");
    tarefa_kernel("rede", tarefa_rede, NULL);
}

/* Adaptador USB de rede (H46): a placa aparece e some com o adaptador. A
 * placa interna, se houver, tem preferência. */
void rede_placa_usb(placa_t *p, bool conectou)
{
    bool i0 = int_salva_desliga();
    if (conectou && !placa) {
        placa = p;
        configurado = false;
        dhcp_estado = 0;
        if (modo_dhcp) meu_ip = 0;
        klog("[REDE] placa nova: %s\n", p->nome);
    } else if (!conectou && placa == p) {
        placa = NULL;
        configurado = false;
        dhcp_estado = 0;
        klog("[REDE] placa removida: %s\n", p->nome);
    }
    int_restaura(i0);
}

/* Para o ícone da barra (H47): 0 sem placa, 1 sem cabo, 2 pedindo endereço, 3 conectado. */
int rede_estado(void)
{
    if (!placa) return 0;
    if (!placa->cabo(placa)) return 1;
    return configurado ? 3 : 2;
}

void rede_relatorio(char *s, int max)
{
    if (!placa) { snformatar(s, (size_t)max, "sem placa de rede"); return; }
    char a[16];
    ip_texto(meu_ip, a, 16);
    snformatar(s, (size_t)max, "%s, %s", placa->nome, configurado ? a : (placa->cabo(placa) ? "pedindo endereço (DHCP)" : "cabo desligado"));
}

/* ------------------------------------------------------------ chamadas */

/* SIS_REDE_INFO (&info_rede_t). */
int32_t sis_rede_info(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    info_rede_t *r = (info_rede_t *)ptr;
    if (!usuario_ok(r, sizeof *r, true)) return ERRO_ENDERECO;
    memset(r, 0, sizeof *r);
    if (!placa) return ERRO_NAO_EXISTE;
    txt_copia_n(r->placa, placa->nome, sizeof r->placa);
    memcpy(r->mac, placa->mac, 6);
    r->cabo = placa->cabo(placa);
    r->configurado = configurado;
    r->dhcp = modo_dhcp;
    r->ip = meu_ip; r->mascara = mascara; r->roteador = roteador; r->dns = dns;
    r->pacotes_rx = pacotes_rx; r->pacotes_tx = pacotes_tx;
    return 0;
}

static socket_t *socket_do_processo(uint32_t id)
{
    if (id >= MAX_SOCKETS || !sockets[id].usado || sockets[id].pid != atual->pid) return NULL;
    return &sockets[id];
}

/* SIS_REDE_SOCKET (tipo) -> número do socket. */
int32_t sis_rede_socket(uint32_t tipo, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (!placa) return ERRO_NAO_EXISTE;
    if (tipo != REDE_UDP && tipo != REDE_ICMP && tipo != REDE_TCP) return ERRO_INVALIDO;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        socket_t *s = &sockets[i];
        if (s->usado && (processo_por_pid(s->pid) == NULL || processo_por_pid(s->pid)->estado == P_ZUMBI)) {
            kfree(s->fila);                /* o dono saiu sem fechar */
            s->fila = NULL;
            s->usado = false;
        }
        if (s->usado) continue;
        memset(s, 0, sizeof *s);
        if (tipo != REDE_TCP) {
            s->fila = kmalloc(sizeof *s->fila * FILA);
            if (!s->fila) return ERRO_SEM_MEMORIA;
        }
        s->usado = true;
        s->pid = atual->pid;
        s->tipo = (int)tipo;
        s->tcp = -1;
        s->porta_local = proxima_porta++;
        if (proxima_porta < 49152) proxima_porta = 49152;
        return i;
    }
    return ERRO_LIMITE;
}

/* SIS_REDE_CONECTAR (socket, ip, porta). */
int32_t sis_rede_conectar(uint32_t id, uint32_t ip, uint32_t porta, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    socket_t *s = socket_do_processo(id);
    if (!s) return ERRO_DESCRITOR;
    if (!configurado) return ERRO_REDE;
    s->ip_remoto = ip;
    s->porta_remota = (uint16_t)porta;
    if (s->tipo == REDE_TCP) {
        if (s->tcp >= 0) return ERRO_OCUPADO;
        int c = tcp_abrir(ip, (uint16_t)porta);
        if (c < 0) return c;
        s->tcp = c;
    }
    return 0;
}

/* SIS_REDE_ENVIAR (socket, &dados, tamanho) -> bytes enviados. */
int32_t sis_rede_enviar(uint32_t id, uint32_t ptr, uint32_t tam, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    socket_t *s = socket_do_processo(id);
    if (!s) return ERRO_DESCRITOR;
    if (!usuario_ok((const void *)ptr, tam, false)) return ERRO_ENDERECO;
    if (!configurado || !s->ip_remoto) return ERRO_REDE;
    if (s->tipo == REDE_TCP) return s->tcp < 0 ? ERRO_REDE : tcp_enviar(s->tcp, (const uint8_t *)ptr, tam);
    if (tam > 1400) tam = 1400;
    if (s->tipo == REDE_UDP)
        return udp_enviar(s->ip_remoto, s->porta_local, s->porta_remota, (const void *)ptr, tam) ? (int32_t)tam : ERRO_REDE;
    if (s->tipo == REDE_ICMP) {
        /* O programa manda a mensagem ICMP inteira; o kernel calcula a soma. */
        uint8_t m[1400];
        if (tam < 8) return ERRO_INVALIDO;
        memcpy(m, (const void *)ptr, tam);
        m[2] = m[3] = 0;
        uint16_t soma = soma_verificacao(m, tam, 0);
        memcpy(m + 2, &soma, 2);
        return ip_enviar(s->ip_remoto, IP_ICMP, m, tam) ? (int32_t)tam : ERRO_REDE;
    }
    return ERRO_INVALIDO;
}

/* SIS_REDE_RECEBER (socket, &dados, max, &origem) -> bytes (0 = nada ainda). */
int32_t sis_rede_receber(uint32_t id, uint32_t ptr, uint32_t max, uint32_t porigem, uint32_t e)
{
    (void)e;
    socket_t *s = socket_do_processo(id);
    if (!s) return ERRO_DESCRITOR;
    if (!usuario_ok((void *)ptr, max, true)) return ERRO_ENDERECO;
    if (porigem && !usuario_ok((void *)porigem, 4, true)) return ERRO_ENDERECO;
    if (s->tipo == REDE_TCP) {
        if (s->tcp < 0) return ERRO_REDE;
        if (porigem) memcpy((void *)porigem, &s->ip_remoto, 4);
        return tcp_receber_dados(s->tcp, (uint8_t *)ptr, max);
    }
    if (s->qtd == 0) return 0;
    uint32_t n = s->fila[s->ini].tam < max ? s->fila[s->ini].tam : max;
    memcpy((void *)ptr, s->fila[s->ini].dados, n);
    if (porigem) memcpy((void *)porigem, &s->fila[s->ini].origem, 4);
    s->ini = (s->ini + 1) % FILA;
    s->qtd--;
    return (int32_t)n;
}

int32_t sis_rede_fechar(uint32_t id, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    socket_t *s = socket_do_processo(id);
    if (!s) return ERRO_DESCRITOR;
    if (s->tipo == REDE_TCP && s->tcp >= 0) tcp_fechar(s->tcp);
    kfree(s->fila);
    s->fila = NULL;
    s->usado = false;
    return 0;
}

int32_t sis_rede_estado(uint32_t id, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    socket_t *s = socket_do_processo(id);
    if (!s || s->tipo != REDE_TCP) return ERRO_DESCRITOR;
    if (s->tcp < 0) return ERRO_REDE;
    return tcp_estado(s->tcp);
}
