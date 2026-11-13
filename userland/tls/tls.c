/* tls.c - a conexão depois do aperto de mão: ler, escrever, fechar, e as
 * mensagens que o servidor ainda pode mandar (RFC 8446, 4.6). */
#include "tls_interno.h"

/* Mensagens do aperto de mão que chegam com a conexão já aberta. */
static int depois_do_aperto(Tls *t)
{
    Fatia corpo, inteira;
    u32 tipo;
    int r;

    if (tls_msg_anexar(t)) return TLS_ERRO;
    while ((r = tls_msg_tirar(t, &tipo, &corpo, &inteira)) > 0) {
        if (tipo == 4) {
            /* NewSessionTicket: um bilhete para retomar a sessão mais
             * tarde. Não fazemos retomada; vai fora. */
            continue;
        }
        if (tipo == 24) {
            /* KeyUpdate: o servidor trocou a chave com que nos escreve. O
             * segredo novo sai do antigo, sem conversa nenhuma. Se o byte
             * for 1, ele pede que troquemos a nossa também. */
            static const u8 resposta[5] = { 24, 0, 0, 1, 0 };
            if (corpo.n != 1 || corpo.p[0] > 1)
                return tls_falhar(t, ALERTA_PARAMETRO, "pedido de troca de chaves malformado", 0, 0);
            if (t->msg_ini != t->msg_fim)
                return tls_falhar(t, ALERTA_INESPERADO, "mensagem atravessada numa troca de chaves", 0, 0);
            int pede = corpo.p[0];
            tls_sentido_avancar(t, &t->rx);
            if (pede && t->estado == EST_ABERTO) {
                /* A resposta ainda vai com a chave antiga; a nova vale do
                 * registro seguinte em diante. */
                if (tls_registro_enviar(t, REG_APERTO, resposta, sizeof resposta)) return TLS_ERRO;
                tls_sentido_avancar(t, &t->tx);
            }
            continue;
        }
        /* Pedido de certificado depois do aperto e qualquer outra coisa:
         * não anunciamos suporte, então é erro do servidor. */
        return tls_falhar(t, ALERTA_INESPERADO, "mensagem inesperada do servidor depois do aperto de mão", 0, 0);
    }
    return r;
}

int tls_ler(Tls *t, u8 *dados, u32 max)
{
    if (max == 0) return 0;
    for (;;) {
        int r;

        /* Primeiro o que já está decifrado e esperando. */
        if (t->claro_ini < t->claro_fim) {
            u32 n = t->claro_fim - t->claro_ini;
            if (n > max) n = max;
            cri_copiar(dados, t->entrada + t->claro_ini, n);
            t->claro_ini += n;
            return (int)n;
        }
        if (t->estado != EST_ABERTO) return TLS_ERRO;
        if (t->fim_remoto) return TLS_FECHADO;
        if (t->cortado) return TLS_CORTADO;

        if ((r = tls_registro_tentar(t)) <= 0) return r;
        switch (t->reg_tipo) {
        case REG_DADOS:
            t->claro_ini = t->reg_ini;
            t->claro_fim = t->reg_fim;
            break;
        case REG_ALERTA:
            if ((r = tls_alerta_recebido(t)) < 0) return r;
            break;
        default:
            if ((r = depois_do_aperto(t)) < 0) return r;
        }
    }
}

int tls_escrever(Tls *t, const u8 *dados, u32 n)
{
    u32 feito = 0;
    if (t->estado != EST_ABERTO) return TLS_ERRO;
    while (feito < n) {
        u32 parte = n - feito;
        if (parte > TLS_DADOS_MAX) parte = TLS_DADOS_MAX;
        if (tls_registro_enviar(t, REG_DADOS, dados + feito, parte)) return TLS_ERRO;
        feito += parte;
    }
    return (int)n;
}

void tls_fechar(Tls *t)
{
    if (t->estado == EST_ABERTO && !t->cortado) {
        /* close_notify: sem ele o outro lado não sabe se a conversa acabou
         * ou se foi cortada. Não esperamos o dele. */
        static const u8 aviso[2] = { 1, ALERTA_FIM };
        tls_registro_enviar(t, REG_ALERTA, aviso, 2);
    }
    if (t->estado != EST_ERRO) t->estado = EST_FECHADO;
    cri_zerar(&t->rx, sizeof t->rx);
    cri_zerar(&t->tx, sizeof t->tx);
    cri_zerar(t->entrada, sizeof t->entrada);
    cri_zerar(t->saida, sizeof t->saida);
    t->claro_ini = t->claro_fim = 0;
}

const char *tls_erro(const Tls *t)
{
    return t->erro;
}

const char *tls_cifra(const Tls *t)
{
    switch (t->conjunto) {
    case 0x1301: return "TLS_AES_128_GCM_SHA256";
    case 0x1302: return "TLS_AES_256_GCM_SHA384";
    case 0x1303: return "TLS_CHACHA20_POLY1305_SHA256";
    }
    return "";
}
