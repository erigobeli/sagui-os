/* pacotes.c - a camada de pacotes do SSH (RFC 4253, seção 6) e as miudezas
 * que todas as outras partes usam.
 *
 * Um pacote na rede é:
 *
 *     uint32  tamanho         (do que vem depois, sem contar o MAC)
 *     byte    enchimento      (quantos bytes de enchimento há no fim)
 *     ...     carga           (o primeiro byte é o tipo da mensagem)
 *     ...     enchimento      (aleatório, de 4 a 255 bytes)
 *     ...     MAC             (só depois que há chaves)
 *
 * Tudo menos o MAC é cifrado, inclusive o tamanho. Por isso a chegada é em
 * dois tempos: primeiro um bloco da cifra, para descobrir o tamanho; depois
 * o resto e o MAC. O MAC é calculado sobre o número de sequência seguido do
 * pacote em claro, e NADA do conteúdo é usado antes de ele conferir. A única
 * coisa usada antes é o tamanho (não há outro jeito neste modo de MAC), e
 * por isso ele passa por um crivo apertado. */
#include "ssh_interno.h"

/* ------------------------------------------------------------ miudezas */

void ssh_i_por_u32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

u32 ssh_i_pegar_u32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

u32 ssh_i_tam(const char *t)
{
    u32 n = 0;
    while (t[n]) n++;
    return n;
}

int ssh_i_igual(const u8 *p, u32 n, const char *t)
{
    u32 i;
    for (i = 0; i < n; i++)
        if (t[i] == 0 || (u8)t[i] != p[i]) return 0;
    return t[n] == 0;
}

void ssh_i_juntar(char *destino, u32 max, const char *texto)
{
    u32 i = ssh_i_tam(destino);
    while (*texto && i + 1 < max) destino[i++] = *texto++;
    destino[i] = 0;
}

/* O que vem do servidor pode trazer sequências de escape capazes de mexer
 * no terminal de quem lê (RFC 4252, 5.4, manda filtrar). Passam as letras
 * visíveis do ASCII e os bytes de UTF-8; a quebra de linha só se pedida. */
void ssh_i_juntar_bytes(char *destino, u32 max, const u8 *p, u32 n, int linhas)
{
    u32 i = ssh_i_tam(destino), k;
    for (k = 0; k < n && i + 1 < max; k++) {
        u8 c = p[k];
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') { if (!linhas) c = ' '; }
        else if (c < 32 || c == 127) c = '?';
        destino[i++] = (char)c;
    }
    destino[i] = 0;
}

void ssh_i_juntar_numero(char *destino, u32 max, u32 v)
{
    char t[12];
    u32 i = sizeof t;
    t[--i] = 0;
    do { t[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    ssh_i_juntar(destino, max, t + i);
}

/* ------------------------------------------------------------ leitor */

void ssh_i_ler_iniciar(Leitor *l, const u8 *p, u32 n)
{
    l->p = p; l->n = n; l->pos = 0; l->erro = 0;
}

u8 ssh_i_ler_byte(Leitor *l)
{
    if (l->erro || l->pos >= l->n) { l->erro = 1; return 0; }
    return l->p[l->pos++];
}

u32 ssh_i_ler_u32(Leitor *l)
{
    u32 v;
    if (l->erro || l->n - l->pos < 4) { l->erro = 1; return 0; }
    v = ssh_i_pegar_u32(l->p + l->pos);
    l->pos += 4;
    return v;
}

void ssh_i_ler_cadeia(Leitor *l, const u8 **p, u32 *n)
{
    u32 t = ssh_i_ler_u32(l);
    if (l->erro || t > l->n - l->pos) {
        l->erro = 1;
        *p = l->p; *n = 0;
        return;
    }
    *p = l->p + l->pos; *n = t;
    l->pos += t;
}

int ssh_i_ler_ok(const Leitor *l)  { return !l->erro; }
int ssh_i_ler_fim(const Leitor *l) { return !l->erro && l->pos == l->n; }

/* ------------------------------------------------------------ escritor */

/* A carga começa no byte 5 do buffer de saída (depois do tamanho e do byte
 * de enchimento) e deixa lugar no fim para o enchimento e o MAC. */
#define SOBRA_SAIDA 64

void ssh_i_esc_iniciar(Escritor *e, Ssh *s, u8 tipo)
{
    e->p = s->saida + 5;
    e->max = SSH_SAIDA_MAX - 5 - SOBRA_SAIDA;
    e->n = 0; e->erro = 0;
    ssh_i_esc_byte(e, tipo);
}

void ssh_i_esc_byte(Escritor *e, u8 v)
{
    if (e->erro || e->n >= e->max) { e->erro = 1; return; }
    e->p[e->n++] = v;
}

void ssh_i_esc_u32(Escritor *e, u32 v)
{
    if (e->erro || e->max - e->n < 4) { e->erro = 1; return; }
    ssh_i_por_u32(e->p + e->n, v);
    e->n += 4;
}

void ssh_i_esc_cadeia(Escritor *e, const void *p, u32 n)
{
    ssh_i_esc_u32(e, n);
    if (e->erro || n > e->max - e->n) { e->erro = 1; return; }
    cri_copiar(e->p + e->n, p, n);
    e->n += n;
}

void ssh_i_esc_texto(Escritor *e, const char *t)
{
    ssh_i_esc_cadeia(e, t, ssh_i_tam(t));
}

/* ------------------------------------------------------------ tempo */

void ssh_i_marcar_inicio(Ssh *s)
{
    s->inicio_ms = s->amb.agora_ms(s->amb.ctx);
}

static int passou_do_limite(Ssh *s, u32 desde)
{
    /* A conta sem sinal continua certa quando o relógio dá a volta. */
    return s->amb.agora_ms(s->amb.ctx) - desde > s->limite_ms;
}

/* ------------------------------------------------------------ saída */

/* Manda tudo ou diz por que não deu: 0, -1 (caiu) ou -2 (parou de andar). */
static int mandar_tudo(Ssh *s, const u8 *p, u32 n)
{
    u32 feito = 0, desde = s->amb.agora_ms(s->amb.ctx);
    while (feito < n) {
        int r = s->amb.enviar(s->amb.ctx, p + feito, n - feito);
        if (r < 0 || (u32)r > n - feito) return -1;
        if (r == 0) {
            if (passou_do_limite(s, desde)) return -2;
            s->amb.esperar(s->amb.ctx, 5);
            continue;
        }
        feito += (u32)r;
        desde = s->amb.agora_ms(s->amb.ctx);
    }
    return 0;
}

/* Fecha o pacote cuja carga (n bytes) já está em saida+5 e o manda. */
static int despachar(Ssh *s, u32 n)
{
    u32 bloco = s->tx.ativa ? 16 : 8;
    u32 base = 5 + n;
    u32 ench = bloco - base % bloco;
    u32 total;

    /* O pacote inteiro (com o campo do tamanho) tem de ser múltiplo do bloco
     * da cifra e levar pelo menos 4 bytes de enchimento. */
    if (ench < 4) ench += bloco;
    total = base + ench;
    ssh_i_por_u32(s->saida, total - 4);
    s->saida[4] = (u8)ench;
    s->amb.aleatorio(s->amb.ctx, s->saida + base, ench);

    if (s->tx.ativa) {
        Hmac h;
        u8 seq[4];
        ssh_i_por_u32(seq, s->tx.sequencia);
        cri_copiar(&h, &s->tx.mac, sizeof h);
        hmac_atualizar(&h, seq, 4);
        hmac_atualizar(&h, s->saida, total);
        hmac_finalizar(&h, s->saida + total);
        aes_ctr_aplicar(&s->tx.cifra, s->saida, s->saida, total);
        total += 32;
    }
    s->tx.sequencia++;
    s->tx.bytes += total;
    return mandar_tudo(s, s->saida, total);
}

int ssh_i_enviar_cru(Ssh *s, const u8 *p, u32 n)
{
    int r = mandar_tudo(s, p, n);
    if (r == -1) return ssh_i_caiu(s);
    if (r == -2) return ssh_i_falhar(s, MOTIVO_NENHUM, "o servidor parou de receber (tempo esgotado no envio)");
    return SSH_OK;
}

int ssh_i_enviar(Ssh *s, Escritor *e)
{
    int r;
    if (s->estado == EST_MORTO) return s->codigo;
    if (e->erro) return ssh_i_falhar(s, MOTIVO_APLICACAO, "erro interno: pacote maior que o buffer de saída");
    r = despachar(s, e->n);
    if (r == -1) return ssh_i_caiu(s);
    if (r == -2) return ssh_i_falhar(s, MOTIVO_NENHUM, "o servidor parou de receber (tempo esgotado no envio)");
    return SSH_OK;
}

/* ------------------------------------------------------------ erros */

int ssh_i_caiu(Ssh *s)
{
    if (s->estado != EST_MORTO) {
        s->estado = EST_MORTO;
        s->codigo = SSH_CORTADO;
        s->erro[0] = 0;
        ssh_i_juntar(s->erro, SSH_ERRO_MAX, "a conexão caiu");
    }
    return s->codigo;
}

int ssh_i_falhar_com(Ssh *s, u32 motivo, const char *texto, const u8 *detalhe, u32 n)
{
    int avisar;
    if (s->estado == EST_MORTO) return s->codigo;
    /* Antes de trocadas as identificações o servidor não entenderia um
     * pacote: aí só anotamos o erro. */
    avisar = motivo != MOTIVO_NENHUM && s->versao_servidor[0] != 0;
    s->estado = EST_MORTO;
    s->codigo = SSH_ERRO;
    s->erro[0] = 0;
    ssh_i_juntar(s->erro, SSH_ERRO_MAX, texto);
    if (n) {
        ssh_i_juntar(s->erro, SSH_ERRO_MAX, ": ");
        ssh_i_juntar_bytes(s->erro, SSH_ERRO_MAX, detalhe, n, 0);
    }
    if (avisar) {
        Escritor e;
        ssh_i_esc_iniciar(&e, s, MSG_DESCONECTAR);
        ssh_i_esc_u32(&e, motivo);
        ssh_i_esc_texto(&e, s->erro);
        ssh_i_esc_texto(&e, "pt-BR");
        /* Se o aviso não sair, paciência: a conexão já está condenada. */
        if (!e.erro) (void)despachar(s, e.n);
    }
    return SSH_ERRO;
}

int ssh_i_falhar(Ssh *s, u32 motivo, const char *texto)
{
    return ssh_i_falhar_com(s, motivo, texto, 0, 0);
}

int ssh_i_uso_errado(Ssh *s, const char *texto)
{
    if (s->estado == EST_MORTO) return s->codigo;
    s->erro[0] = 0;
    ssh_i_juntar(s->erro, SSH_ERRO_MAX, texto);
    return SSH_ERRO;
}

/* ------------------------------------------------------------ entrada */

int ssh_i_receber(Ssh *s)
{
    u32 bloco = s->rx.ativa ? 16 : 8;
    u32 mac = s->rx.ativa ? 32 : 0;

    if (s->estado == EST_MORTO) return s->codigo;
    for (;;) {
        /* Pedimos à rede só o que falta do pedaço atual: assim nunca sobra
         * na mão um começo de pacote cifrado com chaves que vão mudar. */
        u32 alvo = s->tem_cabecalho ? 4 + s->tam_pacote + mac : bloco;
        if (s->nentrada < alvo) {
            int r = s->amb.receber(s->amb.ctx, s->entrada + s->nentrada, alvo - s->nentrada);
            if (r < 0 || (u32)r > alvo - s->nentrada) return ssh_i_caiu(s);
            if (r == 0) return 0;
            s->nentrada += (u32)r;
            s->rx.bytes += (u32)r;
            continue;
        }
        if (!s->tem_cabecalho) {
            u32 tam;
            if (s->rx.ativa) aes_ctr_aplicar(&s->rx.cifra, s->entrada, s->entrada, bloco);
            tam = ssh_i_pegar_u32(s->entrada);
            /* O crivo do tamanho: pacote inteiro de 16 bytes no mínimo,
             * múltiplo do bloco, dentro do limite. Um tamanho absurdo é
             * quase sempre sinal de bytes estragados no caminho (ou de
             * chaves diferentes dos dois lados). */
            if (tam < 12 || tam > SSH_PACOTE_MAX - 4 || (tam + 4) % bloco != 0)
                return ssh_i_falhar(s, MOTIVO_PROTOCOLO,
                                    "pacote com tamanho inválido (dados corrompidos no caminho?)");
            s->tam_pacote = tam;
            s->tem_cabecalho = 1;
            continue;
        }
        break;
    }

    {
        u32 total = 4 + s->tam_pacote;
        u32 ench;
        if (s->rx.ativa) {
            Hmac h;
            u8 seq[4], conta[32];
            aes_ctr_aplicar(&s->rx.cifra, s->entrada + bloco, s->entrada + bloco, total - bloco);
            ssh_i_por_u32(seq, s->rx.sequencia);
            cri_copiar(&h, &s->rx.mac, sizeof h);
            hmac_atualizar(&h, seq, 4);
            hmac_atualizar(&h, s->entrada, total);
            hmac_finalizar(&h, conta);
            if (cri_diferentes(conta, s->entrada + total, 32))
                return ssh_i_falhar(s, MOTIVO_MAC,
                                    "um pacote chegou adulterado ou corrompido (o MAC não confere)");
        }
        s->rx.sequencia++;
        s->nentrada = 0;
        s->tem_cabecalho = 0;
        ench = s->entrada[4];
        /* Tem de sobrar pelo menos o byte do tipo da mensagem. */
        if (ench < 4 || ench + 2 > s->tam_pacote)
            return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "pacote com enchimento inválido");
        s->ncarga = s->tam_pacote - ench - 1;
    }
    return 1;
}

int ssh_i_receber_esperando(Ssh *s)
{
    for (;;) {
        int r = ssh_i_receber(s);
        if (r != 0) return r;
        if (passou_do_limite(s, s->inicio_ms))
            return ssh_i_falhar(s, MOTIVO_APLICACAO, "tempo esgotado esperando o servidor");
        s->amb.esperar(s->amb.ctx, 10);
    }
}

int ssh_i_basico(Ssh *s)
{
    const u8 *c = ssh_i_carga(s);
    switch (c[0]) {
    case MSG_DESCONECTAR: {
        Leitor l;
        const u8 *texto; u32 ntexto, motivo;
        ssh_i_ler_iniciar(&l, c + 1, s->ncarga - 1);
        motivo = ssh_i_ler_u32(&l);
        ssh_i_ler_cadeia(&l, &texto, &ntexto);
        if (!ssh_i_ler_ok(&l)) { motivo = 0; ntexto = 0; }
        s->estado = EST_MORTO;
        s->codigo = SSH_ERRO;
        s->erro[0] = 0;
        ssh_i_juntar(s->erro, SSH_ERRO_MAX, "o servidor desconectou");
        if (ntexto) {
            ssh_i_juntar(s->erro, SSH_ERRO_MAX, ": ");
            ssh_i_juntar_bytes(s->erro, SSH_ERRO_MAX, texto, ntexto, 0);
        }
        ssh_i_juntar(s->erro, SSH_ERRO_MAX, " (motivo ");
        ssh_i_juntar_numero(s->erro, SSH_ERRO_MAX, motivo);
        ssh_i_juntar(s->erro, SSH_ERRO_MAX, ")");
        return SSH_ERRO;
    }
    case MSG_IGNORAR:
    case MSG_DEPURACAO:
    case MSG_NAO_IMPLEMENTADO:
        return 1;
    }
    return 0;
}

static int transporte(Ssh *s)
{
    int r = ssh_i_basico(s);
    if (r != 0) return r;
    if (ssh_i_carga(s)[0] == MSG_KEXINIT) {
        r = ssh_i_trocar(s);
        return r < 0 ? r : 1;
    }
    return 0;
}

int ssh_i_proximo(Ssh *s)
{
    for (;;) {
        int r = ssh_i_receber_esperando(s);
        if (r < 0) return r;
        r = transporte(s);
        if (r < 0) return r;
        if (r == 0) return ssh_i_carga(s)[0];
    }
}

int ssh_i_proximo_se_houver(Ssh *s)
{
    for (;;) {
        int r = ssh_i_receber(s);
        if (r <= 0) return r;
        r = transporte(s);
        if (r < 0) return r;
        if (r == 0) return ssh_i_carga(s)[0];
    }
}

/* RFC 4253, 11.4: a resposta a uma mensagem desconhecida leva o número de
 * sequência dela. Pode sair até no meio de uma troca de chaves. */
int ssh_i_nao_implementado(Ssh *s)
{
    Escritor e;
    ssh_i_esc_iniciar(&e, s, MSG_NAO_IMPLEMENTADO);
    ssh_i_esc_u32(&e, s->rx.sequencia - 1);
    return ssh_i_enviar(s, &e);
}
