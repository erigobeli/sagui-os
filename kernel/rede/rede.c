/* rede.c - a pilha de rede: Ethernet, ARP, IPv4, ICMP (ping), UDP, DHCP e
 * DNS. O TCP fica em tcp.c. Ver D032.
 *
 * Tudo roda com as interrupções desligadas: na tarefa da rede (que consulta
 * a placa a cada tique) e dentro das chamadas de sistema (que já entram com
 * elas desligadas). Assim, como no USB, não há duas partes mexendo no mesmo
 * estado ao mesmo tempo, e não precisa de travas.
 *
 * Os endereços IP ficam como uint32_t na ordem "humana" (10.0.2.15 =
 * 0x0A00020F); a conversão para a ordem da rede é feita byte a byte, com
 * be16/be32/poe16/poe32, na hora de ler ou montar um pacote. */
#include "rede.h"
#include "abi.h"
#include "console.h"
#include "cpu.h"
#include "pci.h"
#include "processo.h"
#include "relogio.h"
#include "sistema.h"
#include "texto.h"

#define ETH_IP   0x0800
#define ETH_ARP  0x0806
#define IP_ICMP  1
#define IP_TCP   6
#define IP_UDP   17
#define TODOS    0xFFFFFFFFu

#define PORTA_DHCP_SERVIDOR 67
#define PORTA_DHCP_CLIENTE  68

static placa_t placa;
static bool tem_placa;
static uint32_t meu_ip, mascara, roteador, servidor_dns, servidor_dhcp;
static uint32_t recebidos, enviados, descartados;

static uint8_t quadro[ETH_MAX];       /* o pacote sendo montado para envio */
static uint8_t chegada[ETH_MAX + 2];  /* o quadro que acabou de chegar */
static const uint8_t difusao[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

bool rede_pronta(void) { return tem_placa && meu_ip; }
uint32_t rede_meu_ip(void) { return meu_ip; }
uint8_t *rede_carga(void) { return quadro + 14 + 20; }

uint32_t soma_parcial(uint32_t s, const uint8_t *p, uint32_t n)
{
    for (; n > 1; p += 2, n -= 2) s += (uint32_t)(p[0] << 8 | p[1]);
    if (n) s += (uint32_t)p[0] << 8;
    return s;
}

uint16_t soma_final(uint32_t s)
{
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t)~s;
}

static void eth_enviar(const uint8_t *destino, uint16_t tipo, uint8_t *q, uint32_t n)
{
    memcpy(q, destino, 6);
    memcpy(q + 6, placa.mac, 6);
    poe16(q + 12, tipo);
    if (placa.enviar(q, 14 + n)) enviados++;
}

/* ------------------------------------------------------------ ARP */

#define ARP_MAX 16
static struct { uint32_t ip; uint8_t mac[6]; uint32_t quando; } arp[ARP_MAX];

/* Um pacote espera a resposta do ARP (quase sempre é o do roteador, e só na
 * primeira vez). shortcut: só um na fila; um segundo substitui o primeiro,
 * que o TCP ou o ping mandam de novo. Se precisar, uma fila por endereço. */
static struct { uint32_t ip; uint32_t n; uint8_t q[ETH_MAX]; } pendente;

static const uint8_t *arp_achar(uint32_t ip)
{
    for (int i = 0; i < ARP_MAX; i++)
        if (arp[i].quando && arp[i].ip == ip) return arp[i].mac;
    return NULL;
}

static void arp_guardar(uint32_t ip, const uint8_t *mac)
{
    int k = 0;
    for (int i = 0; i < ARP_MAX; i++) {
        if (arp[i].quando && arp[i].ip == ip) { k = i; break; }
        if (arp[i].quando < arp[k].quando) k = i;   /* o mais antigo (ou vazio) dá lugar */
    }
    arp[k].ip = ip;
    memcpy(arp[k].mac, mac, 6);
    arp[k].quando = relogio_tiques() | 1;
    if (pendente.n && pendente.ip == ip) {
        memcpy(pendente.q, mac, 6);
        if (placa.enviar(pendente.q, pendente.n)) enviados++;
        pendente.n = 0;
    }
}

static void arp_enviar(uint16_t operacao, const uint8_t *mac_alvo, uint32_t ip_alvo)
{
    uint8_t q[14 + 28];
    uint8_t *a = q + 14;
    poe16(a, 1);                 /* Ethernet */
    poe16(a + 2, ETH_IP);
    a[4] = 6; a[5] = 4;
    poe16(a + 6, operacao);      /* 1 pergunta, 2 resposta */
    memcpy(a + 8, placa.mac, 6);
    poe32(a + 14, meu_ip);
    memcpy(a + 18, operacao == 1 ? (const uint8_t *)"\0\0\0\0\0\0" : mac_alvo, 6);
    poe32(a + 24, ip_alvo);
    eth_enviar(operacao == 1 ? difusao : mac_alvo, ETH_ARP, q, 28);
}

static void arp_chegou(const uint8_t *a, uint32_t n)
{
    if (n < 28 || be16(a) != 1 || be16(a + 2) != ETH_IP) return;
    uint32_t ip_origem = be32(a + 14), ip_alvo = be32(a + 24);
    if (!meu_ip || ip_alvo != meu_ip) {
        if (arp_achar(ip_origem)) arp_guardar(ip_origem, a + 8);   /* atualiza quem já conhecemos */
        return;
    }
    arp_guardar(ip_origem, a + 8);
    if (be16(a + 6) == 1) arp_enviar(2, a + 8, ip_origem);
}

/* ------------------------------------------------------------ IPv4 */

/* O conteúdo (n bytes) já está em rede_carga(). */
bool ip_enviar(uint32_t destino, uint8_t protocolo, uint32_t n)
{
    static uint16_t ident;
    if (!tem_placa || n > ETH_MAX - 34) return false;
    uint8_t *h = quadro + 14;
    h[0] = 0x45; h[1] = 0;
    poe16(h + 2, (uint16_t)(20 + n));
    poe16(h + 4, ident++);
    poe16(h + 6, 0x4000);        /* não fragmentar */
    h[8] = 64; h[9] = protocolo;
    poe16(h + 10, 0);
    poe32(h + 12, meu_ip);
    poe32(h + 16, destino);
    poe16(h + 10, soma_final(soma_parcial(0, h, 20)));

    if (destino == TODOS) { eth_enviar(difusao, ETH_IP, quadro, 20 + n); return true; }
    uint32_t salto = ((destino ^ meu_ip) & mascara) == 0 ? destino : roteador;
    const uint8_t *mac = arp_achar(salto);
    if (mac) { eth_enviar(mac, ETH_IP, quadro, 20 + n); return true; }
    /* Ainda não sabemos o MAC: guarda o pacote e pergunta. */
    memcpy(quadro + 6, placa.mac, 6);
    poe16(quadro + 12, ETH_IP);
    pendente.ip = salto;
    pendente.n = 14 + 20 + n;
    memcpy(pendente.q, quadro, pendente.n);
    arp_enviar(1, NULL, salto);
    return true;
}

/* ------------------------------------------------------------ esperas
 * Ping e DNS seguem o padrão das outras chamadas que bloqueiam (D010): a
 * primeira execução manda o pedido e bloqueia com prazo; a chamada roda de
 * novo ao acordar e então vê se a resposta chegou. Cada processo tem no
 * máximo uma espera de cada tipo. */

enum { ESP_PING = 1, ESP_DNS };
#define ESPERAS 8
typedef struct {
    int      pid, tipo;
    uint16_t id;
    uint32_t inicio, prazo, reenviar;
    uint32_t ip;                  /* ping: destino; DNS: resposta */
    uint32_t tentativas_dns;
    int      resultado;           /* 1 = chegou; <0 = erro */
    char     nome[128];
} espera_t;
static espera_t esperas[ESPERAS];

static espera_t *espera_minha(int tipo)
{
    for (int i = 0; i < ESPERAS; i++)
        if (esperas[i].tipo == tipo && esperas[i].pid == atual->pid) return &esperas[i];
    return NULL;
}

static espera_t *espera_nova(int tipo, uint32_t prazo_ms)
{
    for (int i = 0; i < ESPERAS; i++) {
        espera_t *e = &esperas[i];
        if (e->tipo && processo_por_pid(e->pid)) continue;   /* em uso (ou de um processo que já morreu) */
        static uint16_t proximo_id = 0x5A00;
        memset(e, 0, sizeof *e);
        e->pid = atual->pid;
        e->tipo = tipo;
        e->id = ++proximo_id;
        e->inicio = relogio_tiques();
        e->prazo = e->inicio + (prazo_ms * TIQUES_POR_SEGUNDO + 999) / 1000;
        return e;
    }
    return NULL;
}

/* Bloqueia até a resposta ou o prazo; devolve SIS_REPETIR, ou o resultado. */
static int32_t espera_continuar(espera_t *e, int32_t ok)
{
    if (e->resultado || (int32_t)(relogio_tiques() - e->prazo) >= 0) {
        int32_t r = e->resultado > 0 ? ok : e->resultado ? e->resultado : ERRO_TEMPO;
        e->tipo = 0;
        atual->acordar_em = 0;
        return r;
    }
    uint32_t ate = e->reenviar && (int32_t)(e->reenviar - e->prazo) < 0 ? e->reenviar : e->prazo;
    bloquear(e);
    atual->acordar_em = ate;
    return SIS_REPETIR;
}

/* ------------------------------------------------------------ ICMP */

static void eco_enviar(uint32_t destino, uint16_t id, uint16_t seq)
{
    uint8_t *c = rede_carga();
    c[0] = 8; c[1] = 0;
    poe16(c + 2, 0);
    poe16(c + 4, id);
    poe16(c + 6, seq);
    for (int i = 0; i < 32; i++) c[8 + i] = (uint8_t)('a' + i % 23);   /* conteúdo qualquer */
    poe16(c + 2, soma_final(soma_parcial(0, c, 40)));
    ip_enviar(destino, IP_ICMP, 40);
}

static void icmp_chegou(uint32_t origem, const uint8_t *c, uint32_t n)
{
    if (n < 8 || soma_final(soma_parcial(0, c, n)) != 0) return;
    if (c[0] == 8 && n <= ETH_MAX - 34) {           /* alguém pingou o SaguiOS: responde */
        uint8_t *r = rede_carga();
        memcpy(r, c, n);
        r[0] = 0;
        poe16(r + 2, 0);
        poe16(r + 2, soma_final(soma_parcial(0, r, n)));
        ip_enviar(origem, IP_ICMP, n);
    } else if (c[0] == 0) {
        for (int i = 0; i < ESPERAS; i++) {
            espera_t *e = &esperas[i];
            if (e->tipo == ESP_PING && e->id == be16(c + 4) && e->ip == origem && !e->resultado) {
                e->resultado = 1;
                e->prazo = relogio_tiques();   /* guarda a hora da chegada */
                acordar(e);
            }
        }
    }
}

/* SIS_PING (ip, prazo_ms) -> milissegundos até a resposta. */
int32_t sis_ping(uint32_t ip, uint32_t prazo_ms, uint32_t c, uint32_t d, uint32_t e_)
{
    (void)c; (void)d; (void)e_;
    espera_t *e = espera_minha(ESP_PING);
    if (!e) {
        if (!rede_pronta()) return ERRO_SEM_REDE;
        if (prazo_ms < 100 || prazo_ms > 30000) prazo_ms = 2000;
        e = espera_nova(ESP_PING, prazo_ms);
        if (!e) return ERRO_LIMITE;
        e->ip = ip;
        eco_enviar(ip, e->id, 1);
    }
    uint32_t inicio = e->inicio, chegou = e->prazo;
    int32_t r = espera_continuar(e, 1);
    if (r == 1) r = (int32_t)((chegou - inicio) * (1000 / TIQUES_POR_SEGUNDO));
    return r;
}

/* ------------------------------------------------------------ UDP */

static void udp_enviar(uint32_t destino, uint16_t porta_origem, uint16_t porta_destino, uint32_t n)
{
    /* Os dados já estão em rede_carga() + 8. Soma de verificação 0 = não
     * calculada, o que o UDP sobre IPv4 permite. */
    uint8_t *u = rede_carga();
    poe16(u, porta_origem);
    poe16(u + 2, porta_destino);
    poe16(u + 4, (uint16_t)(8 + n));
    poe16(u + 6, 0);
    ip_enviar(destino, IP_UDP, 8 + n);
}

/* ------------------------------------------------------------ DHCP */

enum { DHCP_PARADO, DHCP_DESCOBRINDO, DHCP_PEDINDO, DHCP_PRONTO };
static int dhcp_estado;
static uint32_t dhcp_xid, dhcp_oferta, dhcp_proxima;

static uint8_t *dhcp_montar(uint8_t tipo)
{
    uint8_t *b = rede_carga() + 8;
    memset(b, 0, 240);
    b[0] = 1; b[1] = 1; b[2] = 6;             /* pedido, Ethernet, MAC de 6 bytes */
    poe32(b + 4, dhcp_xid);
    poe16(b + 10, 0x8000);                    /* responda por difusão: ainda não temos IP */
    memcpy(b + 28, placa.mac, 6);
    poe32(b + 236, 0x63825363);               /* "magic cookie" */
    uint8_t *o = b + 240;
    *o++ = 53; *o++ = 1; *o++ = tipo;
    return o;
}

static void dhcp_enviar(uint8_t *fim)
{
    *fim++ = 55; *fim++ = 3; *fim++ = 1; *fim++ = 3; *fim++ = 6;   /* queremos máscara, roteador e DNS */
    *fim++ = 255;
    uint32_t n = (uint32_t)(fim - (rede_carga() + 8));
    if (n < 300) { memset(fim, 0, 300 - n); n = 300; }   /* alguns servidores ignoram pedidos curtos */
    udp_enviar(TODOS, PORTA_DHCP_CLIENTE, PORTA_DHCP_SERVIDOR, n);
}

static void dhcp_descobrir(void)
{
    dhcp_xid = 0x5A600000u ^ (relogio_tiques() * 2654435761u);
    dhcp_estado = DHCP_DESCOBRINDO;
    dhcp_proxima = relogio_tiques() + 3 * TIQUES_POR_SEGUNDO;
    dhcp_enviar(dhcp_montar(1));
}

static void dhcp_chegou(const uint8_t *b, uint32_t n)
{
    if (n < 240 || b[0] != 2 || be32(b + 4) != dhcp_xid || be32(b + 236) != 0x63825363) return;
    uint8_t tipo = 0;
    uint32_t msk = 0, rot = 0, dns = 0, srv = 0;
    for (uint32_t i = 240; i < n && b[i] != 255;) {
        if (b[i] == 0) { i++; continue; }
        if (i + 1 >= n || i + 2 + b[i + 1] > n) break;
        const uint8_t *v = b + i + 2;
        uint8_t t = b[i + 1];
        if (b[i] == 53 && t >= 1) tipo = v[0];
        if (b[i] == 1 && t >= 4) msk = be32(v);
        if (b[i] == 3 && t >= 4) rot = be32(v);
        if (b[i] == 6 && t >= 4) dns = be32(v);
        if (b[i] == 54 && t >= 4) srv = be32(v);
        i += 2u + t;
    }
    if (tipo == 2 && dhcp_estado == DHCP_DESCOBRINDO) {       /* oferta: pede esse endereço */
        dhcp_oferta = be32(b + 16);
        servidor_dhcp = srv;
        dhcp_estado = DHCP_PEDINDO;
        dhcp_proxima = relogio_tiques() + 3 * TIQUES_POR_SEGUNDO;
        uint8_t *o = dhcp_montar(3);
        *o++ = 50; *o++ = 4; poe32(o, dhcp_oferta); o += 4;
        *o++ = 54; *o++ = 4; poe32(o, servidor_dhcp); o += 4;
        dhcp_enviar(o);
    } else if (tipo == 5 && dhcp_estado == DHCP_PEDINDO) {    /* confirmado */
        meu_ip = be32(b + 16);
        mascara = msk ? msk : 0xFFFFFF00u;
        roteador = rot;
        servidor_dns = dns ? dns : rot;
        dhcp_estado = DHCP_PRONTO;
        klog("[REDE] DHCP: ip %u.%u.%u.%u roteador %u.%u.%u.%u dns %u.%u.%u.%u\n",
             meu_ip >> 24, (meu_ip >> 16) & 255, (meu_ip >> 8) & 255, meu_ip & 255,
             roteador >> 24, (roteador >> 16) & 255, (roteador >> 8) & 255, roteador & 255,
             servidor_dns >> 24, (servidor_dns >> 16) & 255, (servidor_dns >> 8) & 255, servidor_dns & 255);
        if (roteador) arp_enviar(1, NULL, roteador);   /* já descobre o MAC do roteador */
    } else if (tipo == 6) {                                    /* recusado: começa de novo */
        dhcp_estado = DHCP_PARADO;
    }
}

/* shortcut: o endereço não é renovado no fim do prazo do DHCP; uma rede
 * que troque o IP do SaguiOS depois de horas para de responder. Se
 * precisar, guardar a opção 51 e pedir de novo na metade do prazo. */
static void dhcp_tique(void)
{
    if (dhcp_estado == DHCP_PRONTO || !placa.cabo()) return;
    if (dhcp_estado == DHCP_PARADO || (int32_t)(relogio_tiques() - dhcp_proxima) >= 0) dhcp_descobrir();
}

/* ------------------------------------------------------------ DNS */

#define PORTA_DNS 53
#define DNS_RESERVA_1 0x08080808   /* 8.8.8.8 */
#define DNS_RESERVA_2 0x01010101   /* 1.1.1.1 */

static uint16_t dns_porta(const espera_t *e) { return (uint16_t)(50000 + (e->id & 0x3FF)); }

static void dns_perguntar(espera_t *e)
{
    uint8_t *m = rede_carga() + 8;
    poe16(m, e->id);
    poe16(m + 2, 0x0100);        /* pergunta recursiva */
    poe16(m + 4, 1);
    poe16(m + 6, 0); poe16(m + 8, 0); poe16(m + 10, 0);
    uint8_t *p = m + 12;
    for (const char *s = e->nome; *s;) {     /* "exemplo.com" -> 7 exemplo 3 com 0 */
        const char *ponto = s;
        while (*ponto && *ponto != '.') ponto++;
        uint32_t t = (uint32_t)(ponto - s);
        *p++ = (uint8_t)t;
        memcpy(p, s, t);
        p += t;
        s = *ponto ? ponto + 1 : ponto;
    }
    *p++ = 0;
    poe16(p, 1); poe16(p + 2, 1);            /* tipo A (IPv4), classe IN */
    p += 4;
    /* Se o servidor do DHCP não responder (o DNS interno do QEMU falha em alguns
     * Macs), cada nova tentativa vai para outro servidor público. */
    static const uint32_t reserva[2] = { DNS_RESERVA_1, DNS_RESERVA_2 };
    uint32_t alvo = e->tentativas_dns % 3 == 0 ? servidor_dns : reserva[e->tentativas_dns % 3 - 1];
    e->tentativas_dns++;
    udp_enviar(alvo, dns_porta(e), PORTA_DNS, (uint32_t)(p - m));
    e->reenviar = relogio_tiques() + TIQUES_POR_SEGUNDO;
}

/* Pula um nome comprimido ou não; devolve a posição depois dele. */
static uint32_t dns_pular_nome(const uint8_t *m, uint32_t i, uint32_t n)
{
    while (i < n) {
        if (m[i] == 0) return i + 1;
        if ((m[i] & 0xC0) == 0xC0) return i + 2;   /* ponteiro: o nome termina aqui */
        i += 1u + m[i];
    }
    return n;
}

static void dns_chegou(uint16_t porta, const uint8_t *m, uint32_t n)
{
    if (n < 12) return;
    espera_t *e = NULL;
    for (int i = 0; i < ESPERAS; i++)
        if (esperas[i].tipo == ESP_DNS && esperas[i].id == be16(m) && dns_porta(&esperas[i]) == porta) e = &esperas[i];
    if (!e || e->resultado || !(m[2] & 0x80)) return;
    uint32_t i = 12;
    for (uint16_t q = be16(m + 4); q; q--) i = dns_pular_nome(m, i, n) + 4;
    for (uint16_t a = be16(m + 6); a && i < n; a--) {
        i = dns_pular_nome(m, i, n);
        if (i + 10 > n) break;
        uint16_t tipo = be16(m + i), classe = be16(m + i + 2), t = be16(m + i + 8);
        i += 10;
        if (tipo == 1 && classe == 1 && t == 4 && i + 4 <= n) {
            e->ip = be32(m + i);
            e->resultado = 1;
            break;
        }
        i += t;                      /* CNAME e outros: pula */
    }
    if (!e->resultado) e->resultado = ERRO_NAO_EXISTE;   /* o nome não existe ou não tem IPv4 */
    acordar(e);
}

/* SIS_RESOLVER (nome, &ip) */
int32_t sis_resolver(uint32_t pnome, uint32_t pip, uint32_t c, uint32_t d, uint32_t e_)
{
    (void)c; (void)d; (void)e_;
    if (!usuario_ok((void *)pip, 4, true)) return ERRO_ENDERECO;
    espera_t *e = espera_minha(ESP_DNS);
    if (!e) {
        char nome[128];
        int r = usuario_texto((const char *)pnome, nome, sizeof nome);
        if (r < 0) return r;
        if (r == 0 || r > 120) return ERRO_INVALIDO;
        if (!rede_pronta()) return ERRO_SEM_REDE;
        if (!servidor_dns) return ERRO_NAO_EXISTE;
        e = espera_nova(ESP_DNS, 6000);
        if (!e) return ERRO_LIMITE;
        txt_copia_n(e->nome, nome, sizeof e->nome);
        dns_perguntar(e);
    } else if (!e->resultado && (int32_t)(relogio_tiques() - e->reenviar) >= 0) {
        dns_perguntar(e);            /* UDP pode se perder: pergunta de novo a cada segundo */
    }
    uint32_t ip = e->ip;
    int32_t r = espera_continuar(e, 0);
    if (r == 0) *(uint32_t *)pip = ip;
    return r;
}

/* ------------------------------------------------------------ chegada */

static void udp_chegou(uint32_t origem, uint32_t destino, const uint8_t *u, uint32_t n)
{
    (void)destino;
    if (n < 8 || be16(u + 4) > n || be16(u + 4) < 8) return;
    uint16_t po = be16(u), pd = be16(u + 2);
    n = be16(u + 4);
    if (pd == PORTA_DHCP_CLIENTE && po == PORTA_DHCP_SERVIDOR) dhcp_chegou(u + 8, n - 8);
    else if (po == PORTA_DNS && (origem == servidor_dns || origem == DNS_RESERVA_1 || origem == DNS_RESERVA_2)) dns_chegou(pd, u + 8, n - 8);
}

static void ip_chegou(const uint8_t *h, uint32_t n)
{
    if (n < 20 || (h[0] >> 4) != 4) return;
    uint32_t cab = (h[0] & 15) * 4u, total = be16(h + 2);
    if (cab < 20 || total < cab || total > n || soma_final(soma_parcial(0, h, cab)) != 0) return;
    /* shortcut: pedaços de pacotes fragmentados são descartados; quem manda
     * TCP com MSS de 1460 não fragmenta. Se precisar, remontar aqui. */
    if (be16(h + 6) & 0x3FFF) return;
    uint32_t origem = be32(h + 12), destino = be32(h + 16);
    if (meu_ip && destino != meu_ip && destino != TODOS && (destino | ~mascara) != TODOS) return;
    const uint8_t *c = h + cab;
    uint32_t t = total - cab;
    if (h[9] == IP_ICMP && meu_ip) icmp_chegou(origem, c, t);
    else if (h[9] == IP_UDP) udp_chegou(origem, destino, c, t);
    else if (h[9] == IP_TCP && meu_ip && destino == meu_ip) tcp_receber(origem, c, t);
}

static void receber_tudo(void)
{
    for (int k = 0; k < 64; k++) {     /* no máximo um anel por tique, para não segurar o sistema */
        int n = placa.receber(chegada);
        if (n == 0) break;
        if (n < 14) { descartados++; continue; }
        recebidos++;
        uint16_t tipo = be16(chegada + 12);
        if (tipo == ETH_ARP) arp_chegou(chegada + 14, (uint32_t)n - 14);
        else if (tipo == ETH_IP) ip_chegou(chegada + 14, (uint32_t)n - 14);
    }
}

static void tarefa_rede(void *arg)
{
    (void)arg;
    static const char canal = 0;
    for (;;) {
        bool i0 = int_salva_desliga();
        receber_tudo();
        dhcp_tique();
        tcp_tique();
        int_restaura(i0);
        atual->acordar_em = relogio_tiques() + 1;
        int_desliga();
        bloquear(&canal);
        ceder();
        int_liga();
    }
}

const char *rede_iniciar(void)
{
    tem_placa = e1000_procurar(&placa);
    if (!tem_placa) return NULL;
    tarefa_kernel("rede", tarefa_rede, NULL);
    return placa.nome;
}

/* ------------------------------------------------------------ relatório */

void rede_resumo(char *s, int max)
{
    if (!tem_placa) { s[0] = 0; return; }
    if (!placa.cabo()) { txt_copia_n(s, "sem cabo", (size_t)max); return; }
    if (!meu_ip) { txt_copia_n(s, "pedindo IP...", (size_t)max); return; }
    snformatar(s, (size_t)max, "%u.%u.%u.%u", meu_ip >> 24, (meu_ip >> 16) & 255, (meu_ip >> 8) & 255, meu_ip & 255);
}

static int ip_texto(char *s, int max, uint32_t ip)
{
    return snformatar(s, (size_t)max, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

int rede_relatorio(char *s, int max)
{
    int k = 0;
#define MAIS(...) do { if (k < max) k += snformatar(s + k, (size_t)(max - k), __VA_ARGS__); } while (0)
    if (tem_placa) {
        MAIS("Placa: %s\n", placa.nome);
        MAIS("MAC: %02x:%02x:%02x:%02x:%02x:%02x | cabo: %s\n", placa.mac[0], placa.mac[1], placa.mac[2],
             placa.mac[3], placa.mac[4], placa.mac[5], placa.cabo() ? "conectado" : "desconectado");
        char a[16], b[16], c[16], d[16];
        if (meu_ip) {
            ip_texto(a, sizeof a, meu_ip); ip_texto(b, sizeof b, mascara);
            ip_texto(c, sizeof c, roteador); ip_texto(d, sizeof d, servidor_dns);
            MAIS("IP: %s | máscara %s (DHCP)\nRoteador: %s | DNS: %s\n", a, b, c, d);
        } else {
            MAIS("IP: ainda sem endereço (pedindo por DHCP%s)\n", placa.cabo() ? "" : "; ligue o cabo");
        }
        MAIS("Pacotes: %u recebidos, %u enviados, %u descartados\n", recebidos, enviados, descartados);
    } else {
        MAIS("Nenhuma placa de rede suportada.\n");
    }
    /* Lista as placas de rede do PCI, conhecidas ou não: com os números
     * dá para saber qual driver falta (ex.: o do G470). */
    static const uint8_t subclasses[] = { 0x00, 0x80 };
    for (int j = 0; j < 2; j++) {
        pci_disp_t v[4];
        int n = pci_procurar(0x02, subclasses[j], 0x00, v, 4);
        for (int i = 0; i < n; i++)
            MAIS("PCI %02x:%02x.%x: %s %04x:%04x\n", v[i].barramento, v[i].dispositivo, v[i].funcao,
                 j == 0 ? "Ethernet" : "rede (sem fio?)", v[i].fabricante, v[i].modelo);
    }
#undef MAIS
    return k < max ? k : max - 1;
}
