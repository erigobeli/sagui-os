/* rede.h - placa de rede, IPv4, ICMP, UDP (DHCP, DNS) e TCP. Ver D032. */
#ifndef SAGUI_REDE_H
#define SAGUI_REDE_H
#include "tipos.h"

#define ETH_MAX   1514   /* quadro Ethernet sem o CRC */

/* O que um driver de placa entrega para a pilha. Os drivers não usam
 * interrupções: a tarefa da rede consulta a placa a cada tique (10 ms). */
typedef struct {
    const char *nome;
    uint8_t mac[6];
    bool (*enviar)(const void *quadro, uint32_t n);   /* false = fila de envio cheia */
    int  (*receber)(void *destino);                   /* bytes; 0 = nada chegou; <0 = quadro descartado */
    bool (*cabo)(void);                               /* link ativo */
} placa_t;

bool e1000_procurar(placa_t *p);

/* ---- rede.c ---- */
const char *rede_iniciar(void);      /* nome da placa, ou NULL se não há placa conhecida */
int  rede_relatorio(char *s, int max);
void rede_resumo(char *s, int max);  /* para a barra: "" (sem placa), "sem cabo", "pedindo IP...", ou o IP */
bool rede_pronta(void);              /* tem placa e IP */
uint8_t *rede_carga(void);           /* onde escrever o conteúdo do próximo pacote IP */
bool ip_enviar(uint32_t destino, uint8_t protocolo, uint32_t n);
uint32_t rede_meu_ip(void);
uint32_t soma_parcial(uint32_t s, const uint8_t *p, uint32_t n);
uint16_t soma_final(uint32_t s);

/* Números em ordem de rede (big-endian) dentro dos pacotes. */
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline void poe16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void poe32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* ---- tcp.c ---- */
struct tcp_conexao;
void    tcp_receber(uint32_t origem, const uint8_t *seg, uint32_t n);
void    tcp_tique(void);
int32_t tcp_ler(struct tcp_conexao *c, char *destino, uint32_t n);
int32_t tcp_escrever(struct tcp_conexao *c, const char *origem, uint32_t n);
void    tcp_fechar(struct tcp_conexao *c);

/* ---- arquivos.c ---- */
int arquivos_novo_tcp(struct tcp_conexao *c);   /* fd do processo atual, ou erro */

#endif
