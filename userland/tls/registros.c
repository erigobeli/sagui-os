/* registros.c - a camada de registros do TLS 1.3 (RFC 8446, seção 5), os
 * alertas e a remontagem das mensagens do aperto de mão.
 *
 * Tudo no TLS viaja em registros: 5 bytes de cabeçalho (tipo, versão,
 * tamanho) e até 16 KB de conteúdo. Antes das chaves, o conteúdo vai às
 * claras. Depois, todo registro se apresenta como "dados de aplicação" e o
 * conteúdo é cifrado com um AEAD: lá dentro vão os dados, um byte com o tipo
 * verdadeiro e, se o outro lado quiser esconder o tamanho, zeros de
 * enchimento. */
#include "tls_interno.h"

/* ------------------------------------------------------------ cifra dos registros */

/* O nonce de cada registro é o IV do sentido com o número do registro
 * (64 bits, o byte mais alto primeiro) misturado por OU-exclusivo nos
 * últimos 8 bytes. Como o número nunca se repete, o nonce também não. */
static void montar_nonce(const TlsSentido *s, u8 nonce[12])
{
    cri_copiar(nonce, s->iv, 12);
    for (u32 i = 0; i < 8; i++)
        nonce[11 - i] ^= (u8)(s->sequencia >> (8 * i));
}

/* Cifra "n" bytes no lugar; a marca vai logo depois. O cabeçalho do
 * registro entra como dado extra: é autenticado sem ser cifrado. */
static void selar(Tls *t, const u8 cabecalho[5], u8 *dados, u32 n)
{
    u8 nonce[12];
    montar_nonce(&t->tx, nonce);
    if (t->conjunto == 0x1303)
        chacha20_poly1305_cifrar(t->tx.chave, nonce, cabecalho, 5, dados, n, dados, dados + n);
    else
        aes_gcm_cifrar(&t->tx.gcm, nonce, 12, cabecalho, 5, dados, n, dados, dados + n);
    t->tx.sequencia++;
}

static int abrir(Tls *t, const u8 cabecalho[5], u8 *dados, u32 n)
{
    u8 nonce[12];
    int r;
    montar_nonce(&t->rx, nonce);
    if (t->conjunto == 0x1303)
        r = chacha20_poly1305_decifrar(t->rx.chave, nonce, cabecalho, 5, dados, n, dados + n, dados);
    else
        r = aes_gcm_decifrar(&t->rx.gcm, nonce, 12, cabecalho, 5, dados, n, dados + n, dados);
    t->rx.sequencia++;
    return r == CRIPTO_OK ? 0 : -1;
}

/* ------------------------------------------------------------ envio */

/* Monta em t->saida o registro pronto para a rede; devolve o tamanho. */
static u32 montar_registro(Tls *t, u8 tipo, const u8 *dados, u32 n)
{
    u8 *s = t->saida;
    u32 corpo = n;

    cri_copiar(s + 5, dados, n);
    s[1] = 3;
    s[2] = 3;                               /* a versão do registro está congelada em "TLS 1.2" */
    if (t->tx.ativa) {
        s[0] = REG_DADOS;
        s[5 + n] = tipo;                    /* o tipo verdadeiro vai escondido no fim */
        corpo = n + 1 + 16;
        s[3] = (u8)(corpo >> 8);
        s[4] = (u8)corpo;
        selar(t, s, s + 5, n + 1);
    } else {
        s[0] = tipo;
        /* O primeiro registro (o ClientHello) diz "TLS 1.0" para não
         * assustar equipamentos antigos no caminho (RFC 8446, 5.1). */
        if (tipo == REG_APERTO) s[2] = 1;
        s[3] = (u8)(n >> 8);
        s[4] = (u8)n;
    }
    return 5 + corpo;
}

static int enviar_tudo(Tls *t, const u8 *p, u32 n)
{
    u32 ultimo = t->amb.agora_ms(t->amb.ctx);
    while (n) {
        int r = t->amb.enviar(t->amb.ctx, p, n);
        if (r < 0) return tls_falhar(t, -1, "a conexão caiu durante o envio", 0, 0);
        if (r == 0) {
            /* Não coube nada agora. Só desistimos se ficar parado demais. */
            if (t->amb.agora_ms(t->amb.ctx) - ultimo > t->limite_ms)
                return tls_falhar(t, -1, "tempo esgotado: o servidor parou de receber", 0, 0);
            t->amb.esperar(t->amb.ctx, 2);
            continue;
        }
        if ((u32)r > n) r = (int)n;
        p += r;
        n -= (u32)r;
        ultimo = t->amb.agora_ms(t->amb.ctx);
    }
    return 0;
}

int tls_registro_enviar(Tls *t, u8 tipo, const u8 *dados, u32 n)
{
    if (n > TLS_DADOS_MAX) return tls_falhar(t, ALERTA_INTERNO, "registro grande demais para enviar", 0, 0);
    return enviar_tudo(t, t->saida, montar_registro(t, tipo, dados, n));
}

int tls_falhar(Tls *t, int alerta, const char *a, const char *b, const char *c)
{
    int antes = t->estado;
    if (antes == EST_ERRO) return TLS_ERRO;     /* a primeira explicação é a que vale */
    txt_juntar(t->erro, sizeof t->erro, a, b, c, 0, 0, 0);
    t->estado = EST_ERRO;
    if (alerta >= 0 && (antes == EST_APERTO || antes == EST_ABERTO)) {
        /* Aviso de cortesia ao outro lado. Uma tentativa só: já estamos
         * desistindo e não vale ficar esperando por isso. */
        u8 aviso[2] = { 2, (u8)alerta };
        u32 n = montar_registro(t, REG_ALERTA, aviso, 2);
        t->amb.enviar(t->amb.ctx, t->saida, n);
    }
    return TLS_ERRO;
}

/* ------------------------------------------------------------ recepção */

/* Recebe até completar "alvo" bytes em t->entrada. 1 = completou. */
static int encher_entrada(Tls *t, u32 alvo)
{
    while (t->nentrada < alvo) {
        int r = t->amb.receber(t->amb.ctx, t->entrada + t->nentrada, alvo - t->nentrada);
        if (r == 0) return 0;
        if (r < 0) {
            if (t->estado == EST_APERTO)
                return tls_falhar(t, -1, "o servidor fechou a conexão no meio do aperto de mão", 0, 0);
            /* Sem close_notify não dá para saber se os dados vieram até o
             * fim ou se alguém cortou a conversa: quem chama decide. */
            t->cortado = 1;
            txt_juntar(t->erro, sizeof t->erro, "a conexão caiu sem o aviso de fim do TLS",
                       t->nentrada ? " (no meio de um registro)" : 0, 0, 0, 0, 0);
            return TLS_CORTADO;
        }
        if ((u32)r > alvo - t->nentrada) r = (int)(alvo - t->nentrada);
        t->nentrada += (u32)r;
    }
    return 1;
}

int tls_registro_tentar(Tls *t)
{
    for (;;) {
        u8 *e = t->entrada;
        u32 n, tipo;
        int r;

        /* Pedimos à rede exatamente o que falta do registro atual (primeiro
         * o cabeçalho, depois o corpo): nunca sobra byte do registro
         * seguinte para guardar em outro lugar. */
        if ((r = encher_entrada(t, 5)) <= 0) return r;
        tipo = e[0];
        n = ((u32)e[3] << 8) | e[4];
        if (tipo < REG_TROCA_CIFRA || tipo > REG_DADOS || e[1] != 3)
            return tls_falhar(t, ALERTA_INESPERADO, "o servidor não respondeu em TLS", 0, 0);
        /* Às claras cabem 2^14 bytes; cifrado, mais 256 (RFC 8446, 5.2). */
        if (n > (t->rx.ativa ? TLS_DADOS_MAX + 256u : TLS_DADOS_MAX))
            return tls_falhar(t, ALERTA_REGISTRO_GRANDE, "registro maior que o permitido", 0, 0);
        if ((r = encher_entrada(t, 5 + n)) <= 0) return r;
        t->nentrada = 0;                    /* o próximo "encher" começa um registro novo */

        if (tipo == REG_TROCA_CIFRA) {
            /* Sobra do TLS 1.2 que os servidores mandam para agradar
             * equipamentos do caminho. Durante o aperto de mão, o registro
             * de um byte 01 é ignorado; qualquer outro é erro. */
            if (t->estado == EST_APERTO && n == 1 && e[5] == 1) continue;
            return tls_falhar(t, ALERTA_INESPERADO, "registro de troca de cifra fora de hora", 0, 0);
        }

        if (t->rx.ativa) {
            if (tipo != REG_DADOS)
                return tls_falhar(t, ALERTA_INESPERADO, "registro sem cifra depois de combinadas as chaves", 0, 0);
            if (n < 17 || abrir(t, e, e + 5, n - 16))
                return tls_falhar(t, ALERTA_MARCA_RUIM,
                                  "registro adulterado ou chave errada (a marca de autenticação não confere)", 0, 0);
            n -= 16;
            /* Tira o enchimento: zeros no fim; o último byte que não é zero
             * é o tipo verdadeiro. */
            while (n && e[5 + n - 1] == 0) n--;
            if (n == 0) return tls_falhar(t, ALERTA_INESPERADO, "registro cifrado sem tipo", 0, 0);
            tipo = e[5 + --n];
            if (n > TLS_DADOS_MAX)
                return tls_falhar(t, ALERTA_REGISTRO_GRANDE, "registro maior que o permitido", 0, 0);
            if (tipo < REG_ALERTA || tipo > REG_DADOS)
                return tls_falhar(t, ALERTA_INESPERADO, "registro de tipo desconhecido", 0, 0);
        } else if (tipo == REG_DADOS) {
            return tls_falhar(t, ALERTA_INESPERADO, "dados cifrados antes de combinadas as chaves", 0, 0);
        }
        /* Registro vazio só é permitido para dados de aplicação. */
        if (n == 0 && tipo != REG_DADOS)
            return tls_falhar(t, ALERTA_INESPERADO, "registro vazio", 0, 0);

        t->reg_tipo = (u8)tipo;
        t->reg_ini = 5;
        t->reg_fim = 5 + n;
        return 1;
    }
}

int tls_registro_esperar(Tls *t)
{
    for (;;) {
        int r = tls_registro_tentar(t);
        if (r != 0) return r;
        if (t->amb.agora_ms(t->amb.ctx) - t->inicio_ms > t->limite_ms)
            return tls_falhar(t, -1, "tempo esgotado esperando o servidor", 0, 0);
        t->amb.esperar(t->amb.ctx, 5);
    }
}

/* ------------------------------------------------------------ alertas */

static const char *texto_do_alerta(u32 codigo)
{
    switch (codigo) {
    case 10:  return "mensagem inesperada";
    case 20:  return "registro com marca de autenticação errada";
    case 22:  return "registro grande demais";
    case 40:  return "falha na negociação (nenhuma combinação de cifras ou de troca de chaves em comum)";
    case 42:  return "certificado ruim";
    case 43:  return "tipo de certificado não suportado";
    case 44:  return "certificado revogado";
    case 45:  return "certificado vencido";
    case 46:  return "problema no certificado";
    case 47:  return "parâmetro ilegal";
    case 48:  return "autoridade certificadora desconhecida";
    case 49:  return "acesso negado";
    case 50:  return "mensagem malformada";
    case 51:  return "falha ao conferir assinatura ou Finished";
    case 70:  return "versão do protocolo não suportada (o servidor não aceita TLS 1.3?)";
    case 71:  return "segurança insuficiente";
    case 80:  return "erro interno do servidor";
    case 86:  return "tentativa de rebaixar a versão";
    case 109: return "extensão obrigatória ausente";
    case 110: return "extensão não suportada";
    case 112: return "nome de servidor não reconhecido";
    case 113: return "resposta de estado do certificado ruim";
    case 115: return "identidade de chave pré-combinada desconhecida";
    case 116: return "o servidor exige certificado de cliente";
    case 120: return "nenhum protocolo de aplicação em comum";
    }
    return "alerta desconhecido";
}

int tls_alerta_recebido(Tls *t)
{
    char numero[12];
    u32 codigo;

    if (t->reg_fim - t->reg_ini != 2)
        return tls_falhar(t, ALERTA_FORMATO, "alerta malformado", 0, 0);
    codigo = t->entrada[t->reg_ini + 1];
    if (codigo == ALERTA_FIM) {
        t->fim_remoto = 1;
        return TLS_FECHADO;
    }
    if (codigo == 90) return 0;             /* user_canceled: vem seguido de um close_notify */
    /* No TLS 1.3 todos os outros alertas são fatais, diga o nível o que
     * disser. Não se responde a um alerta com outro. */
    txt_juntar(t->erro, sizeof t->erro, "o servidor encerrou com um alerta: ", texto_do_alerta(codigo),
               " (código ", txt_numero(numero, codigo), ")", 0);
    t->estado = EST_ERRO;
    return TLS_ERRO;
}

/* ------------------------------------------------------------ mensagens do aperto
 *
 * As mensagens do aperto de mão formam um fluxo de bytes que os registros
 * cortam onde bem entendem: uma mensagem pode vir em vários registros (a
 * cadeia de certificados quase sempre vem) e um registro pode trazer várias
 * mensagens. Os registros de aperto são juntados em t->msg, e as mensagens
 * inteiras são tiradas dali. */

int tls_msg_anexar(Tls *t)
{
    u32 n = t->reg_fim - t->reg_ini;

    if (t->msg_ini == t->msg_fim) {
        t->msg_ini = t->msg_fim = 0;
    } else if (t->msg_ini) {
        /* Puxa o resto para o começo, para sempre haver espaço no fim. */
        mem_descer(t->msg, t->msg + t->msg_ini, t->msg_fim - t->msg_ini);
        t->msg_fim -= t->msg_ini;
        t->msg_ini = 0;
    }
    if (n > TLS_MENSAGEM_MAX - t->msg_fim)
        return tls_falhar(t, ALERTA_INTERNO, "mensagem do aperto de mão grande demais para este sistema", 0, 0);
    cri_copiar(t->msg + t->msg_fim, t->entrada + t->reg_ini, n);
    t->msg_fim += n;
    return 0;
}

int tls_msg_tirar(Tls *t, u32 *tipo, Fatia *corpo, Fatia *inteira)
{
    Fatia f = { t->msg + t->msg_ini, t->msg_fim - t->msg_ini };
    u32 n;

    /* Cabeçalho da mensagem: 1 byte de tipo e 3 de tamanho. */
    if (f.n < 4) return 0;
    inteira->p = f.p;
    fatia_ler8(&f, tipo);
    fatia_ler24(&f, &n);
    if (n > TLS_MENSAGEM_MAX - 4)
        return tls_falhar(t, ALERTA_INTERNO,
                          "mensagem do aperto de mão grande demais para este sistema (cadeia de certificados enorme?)", 0, 0);
    if (fatia_cortar(&f, n, corpo)) return 0;
    inteira->n = 4 + n;
    t->msg_ini += 4 + n;
    return 1;
}
