/* tcp.c - TCP só do lado cliente: o SaguiOS conecta, nunca escuta. Ver D032.
 *
 * Uma conexão vira um descritor de arquivo comum (ler, escrever, fechar),
 * como um pipe. O essencial do TCP está aqui:
 *   - abertura em três passos (SYN, SYN+ACK, ACK) com reenvio do SYN;
 *   - números de sequência: tudo que foi enviado fica guardado até o outro
 *     lado confirmar (ACK); sem confirmação no prazo, reenvia a partir do
 *     primeiro byte não confirmado e dobra o prazo;
 *   - recepção em ordem num anel de 32 KB; a janela anunciada é o espaço
 *     livre do anel, então quem manda rápido demais espera o programa ler;
 *   - fechamento pelos dois lados (FIN) e RST.
 * shortcut: segmento fora de ordem é descartado (o ACK repetido faz o outro
 * lado reenviar), não há "time-wait" nem sonda de janela zero. Basta para
 * baixar páginas; se a rede for ruim e lenta, guardar os fora de ordem. */
#include "rede.h"
#include "abi.h"
#include "console.h"
#include "memoria.h"
#include "processo.h"
#include "relogio.h"
#include "sistema.h"
#include "texto.h"

#define CONEXOES   8
#define RX_TAM     32768
#define TX_TAM     8192
#define MEU_MSS    1460

#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

enum { T_LIVRE, T_SYN_ENVIADO, T_ESTABELECIDA, T_FECHADA };

typedef struct tcp_conexao {
    int      estado;
    int      erro;                /* <0: a conexão caiu (recusada, sem resposta...) */
    int      dono;                /* pid que está em "conectar" */
    bool     aberta;              /* ainda tem descritor apontando para ela */
    uint32_t ip;
    uint16_t porta_local, porta_remota;
    uint32_t iss, snd_una, snd_nxt, rcv_nxt;
    uint32_t janela_remota, mss, janela_anunciada;
    uint8_t *rx; uint32_t rx_ini, rx_qtd;
    uint8_t *tx; uint32_t tx_qtd;           /* bytes a partir de snd_una (não confirmados + não enviados) */
    bool     fin_recebido, fechar_pedido, fin_enviado;
    uint32_t rto, reenviar_em, desistir_em;
    int      tentativas;
} tcp_t;

static tcp_t conexoes[CONEXOES];

static uint32_t agora(void) { return relogio_tiques(); }
static bool antes(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }   /* comparação de sequência com volta */

/* Monta e manda um segmento com "n" bytes do anel de envio a partir de "desloc". */
static void segmento(tcp_t *c, uint32_t seq, uint8_t flags, uint32_t desloc, uint32_t n)
{
    uint8_t *t = rede_carga();
    uint32_t cab = (flags & F_SYN) ? 24 : 20;
    poe16(t, c->porta_local);
    poe16(t + 2, c->porta_remota);
    poe32(t + 4, seq);
    poe32(t + 8, (flags & F_ACK) ? c->rcv_nxt : 0);
    t[12] = (uint8_t)((cab / 4) << 4);
    t[13] = flags;
    uint32_t livre = RX_TAM - c->rx_qtd;
    c->janela_anunciada = livre > 65535 ? 65535 : livre;
    poe16(t + 14, (uint16_t)c->janela_anunciada);
    poe16(t + 16, 0);
    poe16(t + 18, 0);
    if (flags & F_SYN) { t[20] = 2; t[21] = 4; poe16(t + 22, MEU_MSS); }   /* opção MSS */
    for (uint32_t i = 0; i < n; i++) t[cab + i] = c->tx[(desloc + i) % TX_TAM];
    uint8_t pseudo[12];
    poe32(pseudo, rede_meu_ip());
    poe32(pseudo + 4, c->ip);
    pseudo[8] = 0; pseudo[9] = 6;
    poe16(pseudo + 10, (uint16_t)(cab + n));
    poe16(t + 16, soma_final(soma_parcial(soma_parcial(0, pseudo, 12), t, cab + n)));
    ip_enviar(c->ip, 6, cab + n);
}

static void armar_reenvio(tcp_t *c)
{
    c->reenviar_em = agora() + c->rto;
}

/* Manda o que cabe na janela do outro lado; depois dos dados, o FIN. */
static void enviar_pendente(tcp_t *c)
{
    if (c->estado != T_ESTABELECIDA) return;
    for (;;) {
        uint32_t em_voo = c->snd_nxt - c->snd_una - (c->fin_enviado ? 1 : 0);
        if (em_voo >= c->tx_qtd) break;
        uint32_t janela = c->janela_remota > em_voo ? c->janela_remota - em_voo : 0;
        uint32_t n = c->tx_qtd - em_voo;
        if (n > c->mss) n = c->mss;
        if (n > janela) n = janela;
        if (!n) break;
        segmento(c, c->snd_nxt, F_ACK | F_PSH, em_voo, n);
        if (c->snd_nxt == c->snd_una) armar_reenvio(c);
        c->snd_nxt += n;
    }
    uint32_t em_voo = c->snd_nxt - c->snd_una;
    if (c->fechar_pedido && !c->fin_enviado && em_voo == c->tx_qtd) {
        segmento(c, c->snd_nxt, F_FIN | F_ACK, 0, 0);
        if (c->snd_nxt == c->snd_una) armar_reenvio(c);
        c->snd_nxt++;
        c->fin_enviado = true;
    }
}

static void liberar(tcp_t *c)
{
    kfree(c->rx);
    kfree(c->tx);
    memset(c, 0, sizeof *c);
}

static void cair(tcp_t *c, int erro)
{
    if (!c->erro) c->erro = erro;
    c->estado = T_FECHADA;
    acordar(c);
    if (!c->aberta && !c->dono) liberar(c);
}

void tcp_receber(uint32_t origem, const uint8_t *t, uint32_t n)
{
    if (n < 20) return;
    uint32_t cab = (t[12] >> 4) * 4u;
    if (cab < 20 || cab > n) return;
    uint8_t pseudo[12];
    poe32(pseudo, origem);
    poe32(pseudo + 4, rede_meu_ip());
    pseudo[8] = 0; pseudo[9] = 6;
    poe16(pseudo + 10, (uint16_t)n);
    if (soma_final(soma_parcial(soma_parcial(0, pseudo, 12), t, n)) != 0) return;

    uint16_t po = be16(t), pd = be16(t + 2);
    tcp_t *c = NULL;
    for (int i = 0; i < CONEXOES; i++)
        if (conexoes[i].estado != T_LIVRE && conexoes[i].ip == origem &&
            conexoes[i].porta_remota == po && conexoes[i].porta_local == pd) c = &conexoes[i];
    if (!c || c->estado == T_FECHADA) return;   /* shortcut: sem RST para portas desconhecidas */

    uint32_t seq = be32(t + 4), ack = be32(t + 8);
    uint8_t flags = t[13];
    const uint8_t *dados = t + cab;
    uint32_t tam = n - cab;

    if (c->estado == T_SYN_ENVIADO) {
        if ((flags & F_ACK) && ack != c->iss + 1) return;
        if (flags & F_RST) { if (flags & F_ACK) cair(c, ERRO_RECUSADA); return; }
        if (!(flags & F_SYN) || !(flags & F_ACK)) return;
        c->rcv_nxt = seq + 1;
        c->snd_una = c->snd_nxt = ack;
        c->janela_remota = be16(t + 14);
        c->mss = 536;
        for (uint32_t i = 20; i + 1 < cab;) {          /* procura a opção MSS */
            if (t[i] == 0) break;
            if (t[i] == 1) { i++; continue; }
            if (t[i] == 2 && t[i + 1] == 4 && i + 4 <= cab) c->mss = be16(t + i + 2);
            if (t[i + 1] < 2) break;
            i += t[i + 1];
        }
        if (c->mss > MEU_MSS) c->mss = MEU_MSS;
        c->estado = T_ESTABELECIDA;
        c->tentativas = 0;
        c->rto = TIQUES_POR_SEGUNDO;
        c->reenviar_em = 0;
        segmento(c, c->snd_nxt, F_ACK, 0, 0);
        acordar(c);
        return;
    }

    if (flags & F_RST) { cair(c, ERRO_RECUSADA); return; }
    if (flags & F_ACK) {
        if (antes(c->snd_una, ack) && !antes(c->snd_nxt, ack)) {   /* confirma algo novo */
            uint32_t conf = ack - c->snd_una;
            if (c->fin_enviado && ack == c->snd_nxt) conf--;      /* o FIN ocupa um número, não um byte */
            if (conf > c->tx_qtd) conf = c->tx_qtd;
            c->tx_qtd -= conf;
            memmove(c->tx, c->tx + conf, c->tx_qtd);
            c->snd_una = ack;
            c->tentativas = 0;
            c->rto = TIQUES_POR_SEGUNDO;
            c->reenviar_em = c->snd_una == c->snd_nxt ? 0 : agora() + c->rto;
            acordar(c);                                            /* abriu espaço para escrever */
        }
        c->janela_remota = be16(t + 14);
    }
    bool responder = false;
    if (tam || (flags & F_FIN)) {
        if (seq == c->rcv_nxt && !c->fin_recebido) {
            uint32_t livre = RX_TAM - c->rx_qtd, k = tam < livre ? tam : livre;
            for (uint32_t i = 0; i < k; i++) c->rx[(c->rx_ini + c->rx_qtd + i) % RX_TAM] = dados[i];
            c->rx_qtd += k;
            c->rcv_nxt += k;
            if ((flags & F_FIN) && k == tam) { c->rcv_nxt++; c->fin_recebido = true; }
            if (k || c->fin_recebido) acordar(c);
        }
        responder = true;   /* em ordem ou não, confirma até onde temos */
    }
    if (flags & F_SYN) responder = true;   /* o SYN+ACK voltou: nosso ACK se perdeu */
    if (responder) segmento(c, c->snd_nxt, F_ACK, 0, 0);
    enviar_pendente(c);
    /* Os dois lados já fecharam e nosso FIN foi confirmado: acabou. */
    if (c->fin_recebido && c->fin_enviado && c->snd_una == c->snd_nxt && !c->aberta) liberar(c);
}

void tcp_tique(void)
{
    for (int i = 0; i < CONEXOES; i++) {
        tcp_t *c = &conexoes[i];
        if (c->estado == T_LIVRE) continue;
        if (c->dono && !processo_por_pid(c->dono)) {   /* morreu esperando o conectar */
            c->dono = 0;
            c->aberta = true;
            tcp_fechar(c);
            continue;
        }
        if (!c->aberta && !c->dono && c->desistir_em && antes(c->desistir_em, agora())) { liberar(c); continue; }
        if (!c->reenviar_em || antes(agora(), c->reenviar_em)) continue;
        if (++c->tentativas > 6) { cair(c, ERRO_TEMPO); continue; }
        c->rto *= 2;
        if (c->estado == T_SYN_ENVIADO) {
            segmento(c, c->iss, F_SYN, 0, 0);
            armar_reenvio(c);
        } else if (c->estado == T_ESTABELECIDA) {
            /* Volta ao primeiro byte não confirmado e manda de novo. */
            c->snd_nxt = c->snd_una;
            c->fin_enviado = false;
            c->reenviar_em = 0;
            enviar_pendente(c);
            if (!c->reenviar_em) armar_reenvio(c);
        } else {
            c->reenviar_em = 0;
        }
    }
}

/* SIS_CONECTAR (ip, porta) -> descritor. A primeira execução manda o SYN e
 * bloqueia; a chamada roda de novo a cada mudança da conexão (D010). */
int32_t sis_conectar(uint32_t ip, uint32_t porta, uint32_t c_, uint32_t d, uint32_t e)
{
    (void)c_; (void)d; (void)e;
    tcp_t *c = NULL;
    for (int i = 0; i < CONEXOES; i++)
        if (conexoes[i].estado != T_LIVRE && conexoes[i].dono == atual->pid) c = &conexoes[i];
    if (!c) {
        if (!rede_pronta()) return ERRO_SEM_REDE;
        if (porta == 0 || porta > 65535 || ip == 0) return ERRO_INVALIDO;
        for (int i = 0; i < CONEXOES && !c; i++)
            if (conexoes[i].estado == T_LIVRE) c = &conexoes[i];
        if (!c) return ERRO_LIMITE;
        memset(c, 0, sizeof *c);
        c->rx = kmalloc(RX_TAM);
        c->tx = kmalloc(TX_TAM);
        if (!c->rx || !c->tx) { kfree(c->rx); kfree(c->tx); c->rx = c->tx = NULL; return ERRO_SEM_MEMORIA; }
        static uint16_t proxima_porta;
        c->estado = T_SYN_ENVIADO;
        c->dono = atual->pid;
        c->ip = ip;
        c->porta_remota = (uint16_t)porta;
        c->porta_local = (uint16_t)(49152 + (proxima_porta++ + agora()) % 16000);
        c->iss = agora() * 2654435761u + proxima_porta * 7919u;
        c->snd_una = c->snd_nxt = c->iss + 1;
        c->rto = TIQUES_POR_SEGUNDO;
        c->mss = 536;
        segmento(c, c->iss, F_SYN, 0, 0);
        armar_reenvio(c);
    }
    if (c->estado == T_ESTABELECIDA) {
        int fd = arquivos_novo_tcp(c);
        if (fd < 0) { c->dono = 0; c->aberta = true; tcp_fechar(c); return fd; }
        c->dono = 0;
        c->aberta = true;
        return fd;
    }
    if (c->estado == T_FECHADA) {
        int r = c->erro ? c->erro : ERRO_TEMPO;
        liberar(c);
        return r;
    }
    bloquear(c);
    atual->acordar_em = 0;
    return SIS_REPETIR;
}

int32_t tcp_ler(tcp_t *c, char *destino, uint32_t n)
{
    if (n == 0) return 0;
    if (c->rx_qtd == 0) {
        if (c->fin_recebido) return 0;              /* fim do arquivo */
        if (c->estado == T_FECHADA) return c->erro ? c->erro : 0;
        bloquear(c);
        return SIS_REPETIR;
    }
    uint32_t k = n < c->rx_qtd ? n : c->rx_qtd;
    for (uint32_t i = 0; i < k; i++) destino[i] = (char)c->rx[(c->rx_ini + i) % RX_TAM];
    c->rx_ini = (c->rx_ini + k) % RX_TAM;
    c->rx_qtd -= k;
    /* A janela estava quase fechada: avisa o outro lado que abriu espaço. */
    if (c->estado == T_ESTABELECIDA && c->janela_anunciada < RX_TAM / 2) segmento(c, c->snd_nxt, F_ACK, 0, 0);
    return (int32_t)k;
}

int32_t tcp_escrever(tcp_t *c, const char *origem, uint32_t n)
{
    if (c->estado == T_FECHADA) return c->erro ? c->erro : ERRO_PIPE_QUEBRADO;
    if (c->fechar_pedido) return ERRO_PIPE_QUEBRADO;
    if (n == 0) return 0;
    uint32_t livre = TX_TAM - c->tx_qtd;
    if (!livre) { bloquear(c); return SIS_REPETIR; }
    uint32_t k = n < livre ? n : livre;
    memcpy(c->tx + c->tx_qtd, origem, k);
    c->tx_qtd += k;
    enviar_pendente(c);
    return (int32_t)k;
}

/* O último descritor fechou: manda o FIN e deixa a conexão terminar sozinha
 * (no máximo 10 s depois, ela é liberada mesmo sem resposta). */
void tcp_fechar(tcp_t *c)
{
    c->aberta = false;
    if (c->estado == T_FECHADA) { liberar(c); return; }
    c->fechar_pedido = true;
    c->desistir_em = agora() + 10 * TIQUES_POR_SEGUNDO;
    enviar_pendente(c);
}
