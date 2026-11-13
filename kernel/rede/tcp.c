/* tcp.c - TCP: conexões confiáveis sobre o IP (H48).
 *
 * O IP entrega pacotes soltos, que podem se perder, chegar repetidos ou fora
 * de ordem. O TCP numera cada byte enviado: o outro lado confirma ("ACK") até
 * onde já recebeu tudo em ordem, e o que não for confirmado a tempo é mandado
 * de novo. Cada lado também diz quanto espaço tem para receber (a "janela"),
 * para o outro não mandar mais do que cabe.
 *
 * Esta implementação é a de um cliente (o SaguiOS abre conexões para fora:
 * HTTP, TLS, SSH, IA), pensada para ser simples e segura:
 *   - abertura em três passos: SYN, SYN+ACK, ACK;
 *   - cada conexão tem um anel de envio (o que o programa mandou e ainda não
 *     foi confirmado) e um de recepção (o que chegou e o programa não leu);
 *   - o que chega fora de ordem é descartado: o outro lado manda de novo, e
 *     o ACK repetido avisa que falta um pedaço;
 *   - sem confirmação a tempo, manda de novo a partir do primeiro byte não
 *     confirmado, dobrando a espera a cada falha (de 0,5 s até 8 s);
 *   - três ACKs repetidos seguidos também disparam o reenvio, sem esperar;
 *   - fechamento com FIN dos dois lados.
 *
 * A tarefa da rede chama tcp_tique() a cada 10 ms (reenvios e envio do que
 * está pendente) e tcp_receber() para cada segmento que chega. */
#include "rede.h"
#include "console.h"
#include "cpu.h"
#include "memoria.h"
#include "processo.h"
#include "relogio.h"
#include "sistema.h"
#include "texto.h"

#define MAX_TCP   8
#define ANEL      (64 * 1024)
#define MSS       1400
#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

enum { T_FECHADO, T_SYN_ENVIADO, T_ESTABELECIDO, T_FIN_ESPERA1, T_FIN_ESPERA2, T_ESPERA_FECHAR, T_ULTIMO_ACK,
       T_ESPERA_TEMPO, T_FALHOU };

typedef struct {
    bool     usado;
    int      pid;
    int      estado;
    ip4_t    ip;
    uint16_t porta_local, porta_remota;
    uint32_t snd_una, snd_nxt, snd_janela, iss;   /* envio */
    uint32_t rcv_nxt;                             /* recepção */
    uint8_t *env, *rec;                           /* anéis */
    uint32_t env_ini, env_qtd;                    /* env_ini = byte snd_una */
    uint32_t rec_ini, rec_qtd;
    bool     fin_pedido, fin_enviado, fin_recebido;
    uint32_t rto, reenvio_em, tentativas;
    int      acks_repetidos;
    uint32_t aberta_em;
    uint32_t bytes_rx, bytes_tx, reenvios;
} conexao_t;

static conexao_t conexoes[MAX_TCP];
static uint16_t proxima_porta = 50000;

/* ------------------------------------------------------------ envio de segmentos */

static void segmento(conexao_t *c, uint8_t flags, uint32_t seq, const uint8_t *dados, uint32_t tam)
{
    uint8_t s[20 + 4 + MSS];
    uint32_t cab = (flags & F_SYN) ? 24 : 20;
    s[0] = (uint8_t)(c->porta_local >> 8); s[1] = (uint8_t)c->porta_local;
    s[2] = (uint8_t)(c->porta_remota >> 8); s[3] = (uint8_t)c->porta_remota;
    uint32_t sq = troca32(seq), ack = troca32(c->rcv_nxt);
    memcpy(s + 4, &sq, 4);
    memcpy(s + 8, &ack, 4);
    s[12] = (uint8_t)((cab / 4) << 4);
    s[13] = flags;
    uint32_t livre = ANEL - c->rec_qtd;
    uint16_t janela = (uint16_t)(livre > 65535 ? 65535 : livre);
    s[14] = (uint8_t)(janela >> 8); s[15] = (uint8_t)janela;
    s[16] = s[17] = s[18] = s[19] = 0;
    if (flags & F_SYN) { s[20] = 2; s[21] = 4; s[22] = (uint8_t)(MSS >> 8); s[23] = (uint8_t)MSS; }   /* opção MSS */
    if (tam) memcpy(s + cab, dados, tam);
    uint32_t total = cab + tam;
    ip4_t eu = rede_meu_ip();
    uint32_t inicial = ((eu & 0xFF) << 8 | ((eu >> 8) & 0xFF)) + (((eu >> 16) & 0xFF) << 8 | (eu >> 24)) +
                       ((c->ip & 0xFF) << 8 | ((c->ip >> 8) & 0xFF)) + (((c->ip >> 16) & 0xFF) << 8 | (c->ip >> 24)) +
                       6 + total;
    uint16_t soma = soma_verificacao(s, total, inicial);
    memcpy(s + 16, &soma, 2);
    ip_enviar(c->ip, 6, s, total);
}

static void enviar_ack(conexao_t *c) { segmento(c, F_ACK, c->snd_nxt, NULL, 0); }

/* Manda o que está no anel e ainda não foi mandado, até o tamanho da janela do outro lado. */
static void enviar_pendente(conexao_t *c)
{
    if (c->estado != T_ESTABELECIDO && c->estado != T_ESPERA_FECHAR) return;
    for (int voltas = 0; voltas < 16; voltas++) {
        uint32_t no_ar = c->snd_nxt - c->snd_una;
        uint32_t ja_no_anel = no_ar - (c->fin_enviado ? 1 : 0);
        if (ja_no_anel >= c->env_qtd) break;
        uint32_t janela = c->snd_janela ? c->snd_janela : MSS;
        if (no_ar >= janela) break;
        uint32_t n = c->env_qtd - ja_no_anel;
        if (n > MSS) n = MSS;
        if (n > janela - no_ar) n = janela - no_ar;
        uint8_t pedaco[MSS];
        for (uint32_t i = 0; i < n; i++) pedaco[i] = c->env[(c->env_ini + ja_no_anel + i) % ANEL];
        segmento(c, F_ACK | F_PSH, c->snd_nxt, pedaco, n);
        if (c->snd_una == c->snd_nxt) c->reenvio_em = relogio_tiques() + c->rto;
        c->snd_nxt += n;
        c->bytes_tx += n;
    }
    /* Tudo mandado e o programa pediu para fechar: manda o FIN. */
    if (c->fin_pedido && !c->fin_enviado && c->snd_nxt - c->snd_una == c->env_qtd) {
        segmento(c, F_FIN | F_ACK, c->snd_nxt, NULL, 0);
        c->snd_nxt++;
        c->fin_enviado = true;
        c->estado = c->estado == T_ESPERA_FECHAR ? T_ULTIMO_ACK : T_FIN_ESPERA1;
        c->reenvio_em = relogio_tiques() + c->rto;
    }
}

/* Reenvio: volta a mandar a partir do primeiro byte não confirmado. */
static void reenviar(conexao_t *c)
{
    c->reenvios++;
    if (c->estado == T_SYN_ENVIADO) {
        segmento(c, F_SYN, c->iss, NULL, 0);
    } else {
        bool fin = c->fin_enviado && c->snd_nxt - c->snd_una == c->env_qtd + 1;
        c->snd_nxt = c->snd_una;
        if (fin) c->fin_enviado = false;
        if (c->estado == T_FIN_ESPERA1) c->estado = T_ESTABELECIDO;
        if (c->estado == T_ULTIMO_ACK) c->estado = T_ESPERA_FECHAR;
        enviar_pendente(c);
    }
    c->rto = c->rto * 2 > 8 * TIQUES_POR_SEGUNDO ? 8 * TIQUES_POR_SEGUNDO : c->rto * 2;
    c->reenvio_em = relogio_tiques() + c->rto;
}

void tcp_tique(void)
{
    for (int i = 0; i < MAX_TCP; i++) {
        conexao_t *c = &conexoes[i];
        if (!c->usado || c->estado == T_FECHADO || c->estado == T_FALHOU) continue;
        if (c->estado == T_ESPERA_TEMPO) {
            if (relogio_tiques() - c->reenvio_em < 2 * TIQUES_POR_SEGUNDO) continue;
            c->estado = T_FECHADO;
            continue;
        }
        bool algo_no_ar = c->snd_nxt != c->snd_una;
        if (algo_no_ar && (int32_t)(relogio_tiques() - c->reenvio_em) >= 0) {
            if (++c->tentativas > 8) {             /* o outro lado sumiu */
                c->estado = T_FALHOU;
                klog("[TCP] conexao %d desistiu depois de 8 reenvios\n", i);
                continue;
            }
            reenviar(c);
        }
        enviar_pendente(c);
    }
}

/* ------------------------------------------------------------ recepção */

void tcp_receber(ip4_t origem, const uint8_t *s, uint32_t tam)
{
    if (tam < 20) return;
    uint16_t de = (uint16_t)((s[0] << 8) | s[1]), para = (uint16_t)((s[2] << 8) | s[3]);
    uint32_t seq, ack;
    memcpy(&seq, s + 4, 4); memcpy(&ack, s + 8, 4);
    seq = troca32(seq); ack = troca32(ack);
    uint32_t cab = (uint32_t)(s[12] >> 4) * 4;
    uint8_t flags = s[13];
    uint32_t janela = (uint32_t)((s[14] << 8) | s[15]);
    if (cab < 20 || cab > tam) return;
    conexao_t *c = NULL;
    for (int i = 0; i < MAX_TCP && !c; i++)
        if (conexoes[i].usado && conexoes[i].estado != T_FECHADO && conexoes[i].ip == origem &&
            conexoes[i].porta_local == para && conexoes[i].porta_remota == de)
            c = &conexoes[i];
    if (!c) return;   /* ninguém esperando: o outro lado vai desistir (não mandamos RST) */
    const uint8_t *dados = s + cab;
    uint32_t n = tam - cab;

    if (flags & F_RST) { c->estado = T_FALHOU; klog("[TCP] conexao recusada ou derrubada pelo outro lado\n"); return; }

    if (c->estado == T_SYN_ENVIADO) {
        if ((flags & (F_SYN | F_ACK)) == (F_SYN | F_ACK) && ack == c->iss + 1) {
            c->rcv_nxt = seq + 1;
            c->snd_una = c->snd_nxt = ack;
            c->snd_janela = janela;
            c->estado = T_ESTABELECIDO;
            c->rto = TIQUES_POR_SEGUNDO / 2;
            c->tentativas = 0;
            enviar_ack(c);
            klog("[TCP] conectado em %u ms\n", (relogio_tiques() - c->aberta_em) * 10);
        }
        return;
    }

    /* Confirmações: libera do anel de envio o que o outro lado já recebeu. */
    if (flags & F_ACK) {
        uint32_t confirmados = ack - c->snd_una;
        if (confirmados > 0 && confirmados <= c->snd_nxt - c->snd_una) {
            uint32_t do_anel = confirmados;
            if (c->fin_enviado && ack == c->snd_nxt) do_anel--;   /* o FIN ocupa um número, mas não está no anel */
            if (do_anel > c->env_qtd) do_anel = c->env_qtd;
            c->env_ini = (c->env_ini + do_anel) % ANEL;
            c->env_qtd -= do_anel;
            c->snd_una = ack;
            c->tentativas = 0;
            c->acks_repetidos = 0;
            c->rto = TIQUES_POR_SEGUNDO / 2;
            c->reenvio_em = relogio_tiques() + c->rto;
            if (c->fin_enviado && ack == c->snd_nxt) {
                if (c->estado == T_FIN_ESPERA1) c->estado = T_FIN_ESPERA2;
                else if (c->estado == T_ULTIMO_ACK) c->estado = T_FECHADO;
            }
        } else if (confirmados == 0 && n == 0 && c->snd_nxt != c->snd_una && ++c->acks_repetidos == 3) {
            reenviar(c);                          /* três ACKs iguais: um pedaço se perdeu */
        }
        c->snd_janela = janela;
    }

    /* Dados: só aceita o que vem exatamente na ordem esperada. */
    bool responder = false;
    if (n > 0) {
        if (seq == c->rcv_nxt) {
            uint32_t cabe = ANEL - c->rec_qtd;
            if (n > cabe) n = cabe;
            for (uint32_t i = 0; i < n; i++) c->rec[(c->rec_ini + c->rec_qtd + i) % ANEL] = dados[i];
            c->rec_qtd += n;
            c->rcv_nxt += n;
            c->bytes_rx += n;
        }
        responder = true;                         /* fora de ordem também: o ACK repetido avisa o que falta */
    }
    if ((flags & F_FIN) && seq + (tam - cab) == c->rcv_nxt && !c->fin_recebido) {
        c->rcv_nxt++;
        c->fin_recebido = true;
        responder = true;
        if (c->estado == T_ESTABELECIDO) c->estado = T_ESPERA_FECHAR;
        else if (c->estado == T_FIN_ESPERA1 || c->estado == T_FIN_ESPERA2) { c->estado = T_ESPERA_TEMPO; c->reenvio_em = relogio_tiques(); }
    }
    if (responder) enviar_ack(c);
    enviar_pendente(c);
}

/* ------------------------------------------------------------ para os sockets */

static conexao_t *do_processo(int id)
{
    if (id < 0 || id >= MAX_TCP || !conexoes[id].usado || conexoes[id].pid != atual->pid) return NULL;
    return &conexoes[id];
}

/* Abre uma conexão (não espera: o estado diz quando ficou pronta). */
int tcp_abrir(ip4_t ip, uint16_t porta)
{
    for (int i = 0; i < MAX_TCP; i++) {
        conexao_t *c = &conexoes[i];
        if (c->usado) {
            processo_t *p = processo_por_pid(c->pid);
            bool dono_vivo = p && p->estado != P_ZUMBI && p->estado != P_LIVRE;
            if (dono_vivo || (c->estado != T_FECHADO && c->estado != T_FALHOU && c->estado != T_ESPERA_TEMPO)) continue;
        }
        uint8_t *env = c->env, *rec = c->rec;
        memset(c, 0, sizeof *c);
        c->env = env ? env : kmalloc(ANEL);
        c->rec = rec ? rec : kmalloc(ANEL);
        if (!c->env || !c->rec) return ERRO_SEM_MEMORIA;
        c->usado = true;
        c->pid = atual->pid;
        c->ip = ip;
        c->porta_remota = porta;
        c->porta_local = proxima_porta++;
        if (proxima_porta < 50000) proxima_porta = 50000;
        c->iss = 0x5A600000u ^ (relogio_tiques() * 2654435761u);
        c->snd_una = c->snd_nxt = c->iss + 1;
        c->estado = T_SYN_ENVIADO;
        c->rto = TIQUES_POR_SEGUNDO;
        c->aberta_em = relogio_tiques();
        c->reenvio_em = relogio_tiques() + c->rto;
        segmento(c, F_SYN, c->iss, NULL, 0);
        c->snd_nxt = c->iss + 1;
        c->snd_una = c->iss;                      /* o SYN fica "no ar" até o SYN+ACK */
        return i;
    }
    return ERRO_LIMITE;
}

/* 0 conectando, 1 conectado, 2 o outro lado terminou de mandar, <0 falhou ou fechada. */
int tcp_estado(int id)
{
    conexao_t *c = do_processo(id);
    if (!c) return ERRO_DESCRITOR;
    switch (c->estado) {
    case T_SYN_ENVIADO: return 0;
    case T_ESTABELECIDO: case T_FIN_ESPERA1: case T_FIN_ESPERA2: return 1;
    case T_ESPERA_FECHAR: case T_ULTIMO_ACK: case T_ESPERA_TEMPO: return c->rec_qtd ? 1 : 2;
    default: return c->rec_qtd ? 1 : ERRO_REDE;
    }
}

int tcp_enviar(int id, const uint8_t *dados, uint32_t n)
{
    conexao_t *c = do_processo(id);
    if (!c) return ERRO_DESCRITOR;
    if (c->estado == T_SYN_ENVIADO) return 0;
    if (c->estado != T_ESTABELECIDO && c->estado != T_ESPERA_FECHAR) return ERRO_REDE;
    if (c->fin_pedido) return ERRO_REDE;
    uint32_t cabe = ANEL - c->env_qtd;
    if (n > cabe) n = cabe;
    for (uint32_t i = 0; i < n; i++) c->env[(c->env_ini + c->env_qtd + i) % ANEL] = dados[i];
    c->env_qtd += n;
    enviar_pendente(c);
    return (int)n;
}

int tcp_receber_dados(int id, uint8_t *dados, uint32_t max)
{
    conexao_t *c = do_processo(id);
    if (!c) return ERRO_DESCRITOR;
    if (c->rec_qtd == 0) {
        if (c->fin_recebido) return ERRO_FIM;
        if (c->estado == T_FALHOU) return ERRO_REDE;
        return 0;
    }
    uint32_t janela_antes = ANEL - c->rec_qtd;
    uint32_t n = c->rec_qtd < max ? c->rec_qtd : max;
    for (uint32_t i = 0; i < n; i++) dados[i] = c->rec[(c->rec_ini + i) % ANEL];
    c->rec_ini = (c->rec_ini + n) % ANEL;
    c->rec_qtd -= n;
    /* A janela estava quase fechada: avisa o outro lado que abriu espaço. */
    if (janela_antes < 2 * MSS && c->estado == T_ESTABELECIDO) enviar_ack(c);
    return (int)n;
}

int tcp_fechar(int id)
{
    conexao_t *c = do_processo(id);
    if (!c) return ERRO_DESCRITOR;
    if (c->estado == T_ESTABELECIDO || c->estado == T_ESPERA_FECHAR) {
        c->fin_pedido = true;
        enviar_pendente(c);
    } else if (c->estado == T_SYN_ENVIADO || c->estado == T_FALHOU) {
        c->estado = T_FECHADO;
    }
    c->pid = -1;                                  /* a conexão termina sozinha; o socket fica livre */
    return 0;
}

void tcp_relatorio(char *s, int max)
{
    static const char *const nomes[] = { "fechada", "conectando", "aberta", "fechando", "fechando", "fechando", "fechando",
                                         "terminando", "falhou" };
    int k = 0;
    for (int i = 0; i < MAX_TCP && k < max - 80; i++) {
        conexao_t *c = &conexoes[i];
        if (!c->usado || c->estado == T_FECHADO) continue;
        k += snformatar(s + k, (size_t)(max - k), "tcp %d: porta %u -> %u.%u.%u.%u:%u, %s, %u bytes recebidos, %u enviados, %u reenvios\n",
                        i, c->porta_local, c->ip & 0xFF, (c->ip >> 8) & 0xFF, (c->ip >> 16) & 0xFF, c->ip >> 24, c->porta_remota,
                        nomes[c->estado], c->bytes_rx, c->bytes_tx, c->reenvios);
    }
    if (k == 0) snformatar(s, (size_t)max, "nenhuma conexão TCP aberta\n");
}
