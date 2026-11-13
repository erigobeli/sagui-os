/* rede.h - a rede do SaguiOS (fase 3 da versão 2.0).
 *
 * Camadas, de baixo para cima:
 *   placa (e1000.c, ...)   manda e recebe quadros Ethernet
 *   rede.c                 Ethernet, ARP, IPv4, ICMP, UDP, DHCP e sockets
 *   tcp.c                  TCP (fase 4)
 * Os programas usam a rede por chamadas de sistema de sockets; DNS, HTTP,
 * TLS e SSH são bibliotecas e programas em ring 3. */
#ifndef SAGUI_REDE_H
#define SAGUI_REDE_H
#include "tipos.h"
#include "abi.h"

#define ETH_MAX 1518

/* Uma placa de rede, vista pela pilha. */
typedef struct placa {
    char     nome[32];
    uint8_t  mac[6];
    bool     (*enviar)(struct placa *p, const void *quadro, uint32_t tam);
    int      (*receber)(struct placa *p, void *quadro, uint32_t max);   /* 0 = nada chegou */
    bool     (*cabo)(struct placa *p);                                  /* cabo ligado? */
    void    *dados;
} placa_t;

/* Endereços IPv4 guardados na ordem da rede (o primeiro byte é o primeiro número). */
typedef uint32_t ip4_t;
#define IP4(a, b, c, d) ((ip4_t)(a) | ((ip4_t)(b) << 8) | ((ip4_t)(c) << 16) | ((ip4_t)(d) << 24))

static inline uint16_t troca16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint32_t troca32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

placa_t *e1000_iniciar(void);
void     rede_placa_usb(placa_t *p, bool conectou);   /* adaptador USB de rede (H46) */
void     rede_iniciar(void);
bool     rede_tem_placa(void);
void     rede_relatorio(char *s, int max);
int      rede_estado(void);   /* 0 sem placa, 1 sem cabo, 2 pedindo endereço, 3 conectado */
uint16_t soma_verificacao(const void *dados, uint32_t tam, uint32_t inicial);

/* Interno da pilha (rede.c e tcp.c). */
bool     ip_enviar(ip4_t destino, uint8_t protocolo, const void *dados, uint32_t tam);
ip4_t    rede_meu_ip(void);
void     tcp_receber(ip4_t origem, const uint8_t *segmento, uint32_t tam);
void     tcp_tique(void);
int      tcp_abrir(ip4_t ip, uint16_t porta);
int      tcp_estado(int id);
int      tcp_enviar(int id, const uint8_t *dados, uint32_t n);
int      tcp_receber_dados(int id, uint8_t *dados, uint32_t max);
int      tcp_fechar(int id);
void     tcp_relatorio(char *s, int max);
int32_t  sis_rede_estado(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

/* Sockets (chamadas de sistema). */
int32_t sis_rede_info(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_rede_socket(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_rede_conectar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_rede_enviar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_rede_receber(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_rede_fechar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#endif
