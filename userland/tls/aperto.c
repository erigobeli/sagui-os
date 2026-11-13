/* aperto.c - o aperto de mão do TLS 1.3, lado do cliente (RFC 8446, 4).
 *
 * A conversa, do nosso ponto de vista:
 *
 *     nós:       ClientHello (nossa metade da chave x25519, o que sabemos falar)
 *     servidor:  ServerHello (a metade dele, o conjunto de cifras escolhido)
 *        -- daqui em diante tudo vai cifrado, com as chaves "de aperto" --
 *     servidor:  EncryptedExtensions
 *                [CertificateRequest]
 *                Certificate         (a cadeia de certificados)
 *                CertificateVerify   (assinatura da conversa com a chave do certificado)
 *                Finished            (HMAC da conversa)
 *     nós:       [Certificate vazio] Finished
 *        -- daqui em diante valem as chaves "de aplicação" --
 *
 * O "transcrito" é o resumo de todas as mensagens do aperto, na ordem. Ele
 * entra na derivação das chaves, na assinatura do servidor e nos dois
 * Finished: é o que garante que ninguém mexeu na negociação. */
#include "tls_interno.h"

/* Os segredos que só existem durante o aperto de mão. Ficam na pilha de
 * tls_conectar e são zerados no fim. */
typedef struct {
    u8  privada[32];                    /* nossa chave x25519 efêmera */
    u8  sessao[32];                     /* "id de sessão" de fachada, que o servidor ecoa */
    u8  aperto[RESUMO_TAM_MAX];         /* Handshake Secret */
    u8  cliente[RESUMO_TAM_MAX];        /* client_handshake_traffic_secret */
    u8  servidor[RESUMO_TAM_MAX];       /* server_handshake_traffic_secret */
    int pediu_certificado;
} Aperto;

/* ------------------------------------------------------------ transcrito */

/* Até o ServerHello não se sabe qual resumo o conjunto de cifras usa, e o
 * ClientHello já tem de entrar na conta. Em vez de guardar a mensagem,
 * mantemos os dois resumos possíveis andando juntos. */
static void transcrito_juntar(Tls *t, const u8 *p, u32 n)
{
    resumo_atualizar(&t->transcrito[0], p, n);
    resumo_atualizar(&t->transcrito[1], p, n);
}

/* O resumo do que foi dito até agora, sem encerrar a conta (finaliza uma
 * cópia). */
static void transcrito_agora(Tls *t, u8 *h)
{
    Resumo copia = t->transcrito[t->resumo == RESUMO_SHA384 ? 1 : 0];
    resumo_finalizar(&copia, h);
}

/* ------------------------------------------------------------ mensagens */

static int enviar_mensagem(Tls *t, const u8 *m, u32 n)
{
    transcrito_juntar(t, m, n);
    return tls_registro_enviar(t, REG_APERTO, m, n);
}

/* Espera a próxima mensagem do aperto de mão, juntando registros. */
static int esperar_mensagem(Tls *t, u32 *tipo, Fatia *corpo, Fatia *inteira)
{
    for (;;) {
        int r = tls_msg_tirar(t, tipo, corpo, inteira);
        if (r != 0) return r < 0 ? r : 0;
        if ((r = tls_registro_esperar(t)) < 0) return r;
        if (t->reg_tipo == REG_ALERTA) {
            r = tls_alerta_recebido(t);
            if (r == TLS_FECHADO)
                return tls_falhar(t, -1, "o servidor encerrou a conexão no meio do aperto de mão", 0, 0);
            if (r < 0) return r;
        } else if (t->reg_tipo == REG_APERTO) {
            if (tls_msg_anexar(t)) return TLS_ERRO;
        } else {
            return tls_falhar(t, ALERTA_INESPERADO, "dados de aplicação antes do fim do aperto de mão", 0, 0);
        }
    }
}

/* Uma mensagem não pode ficar atravessada numa troca de chaves (RFC 8446,
 * 5.1): quando as chaves mudam, o buffer de mensagens tem de estar vazio. */
static int exigir_buffer_vazio(Tls *t)
{
    if (t->msg_ini != t->msg_fim)
        return tls_falhar(t, ALERTA_INESPERADO, "mensagem atravessada numa troca de chaves", 0, 0);
    return 0;
}

static int malformada(Tls *t, const char *qual)
{
    return tls_falhar(t, ALERTA_FORMATO, "mensagem malformada do servidor: ", qual, 0);
}

/* ------------------------------------------------------------ ClientHello */

static u32 por16(u8 *m, u32 pos, u32 v)
{
    m[pos] = (u8)(v >> 8);
    m[pos + 1] = (u8)v;
    return pos + 2;
}

static u32 por_bytes(u8 *m, u32 pos, const void *p, u32 n)
{
    cri_copiar(m + pos, p, n);
    return pos + n;
}

/* Um nome só com números e pontos é um endereço, e endereço não vai no SNI
 * (RFC 6066, 3). */
static int parece_endereco(const char *s)
{
    for (; *s; s++)
        if ((*s < '0' || *s > '9') && *s != '.') return 0;
    return 1;
}

static int enviar_ola(Tls *t, Aperto *a, const TlsOpcoes *op)
{
    /* Os jeitos de assinar que aceitamos, em ordem de preferência. Os
     * "pkcs1" só valem para as assinaturas dentro dos certificados; o
     * servidor não pode usá-los no CertificateVerify. */
    static const u16 assinaturas[] = {
        0x0403,     /* ecdsa_secp256r1_sha256 */
        0x0503,     /* ecdsa_secp384r1_sha384 */
        0x0807,     /* ed25519 */
        0x0804,     /* rsa_pss_rsae_sha256 */
        0x0805,     /* rsa_pss_rsae_sha384 */
        0x0806,     /* rsa_pss_rsae_sha512 */
        0x0401,     /* rsa_pkcs1_sha256 */
        0x0501,     /* rsa_pkcs1_sha384 */
        0x0601,     /* rsa_pkcs1_sha512 */
    };
    u8 m[4 + 256 + 255 + 255];
    u8 publica[32];
    u32 nnome = txt_tamanho(op->nome), nalpn = op->alpn ? txt_tamanho(op->alpn) : 0;
    u32 pos = 4, ext, i;

    if (nnome == 0 || nnome > 255 || nalpn > 255)
        return tls_falhar(t, -1, "nome de servidor vazio ou comprido demais", 0, 0);

    t->amb.aleatorio(t->amb.ctx, a->privada, 32);
    t->amb.aleatorio(t->amb.ctx, a->sessao, 32);
    x25519_base(publica, a->privada);

    pos = por16(m, pos, 0x0303);                    /* versão "de fachada": TLS 1.2 */
    t->amb.aleatorio(t->amb.ctx, m + pos, 32);      /* random */
    pos += 32;
    /* No TLS 1.3 não existe mais id de sessão, mas mandar um faz a conversa
     * parecer uma retomada de TLS 1.2 para os equipamentos do caminho. */
    m[pos++] = 32;
    pos = por_bytes(m, pos, a->sessao, 32);
    pos = por16(m, pos, 6);                         /* conjuntos de cifras, o preferido primeiro */
    pos = por16(m, pos, 0x1303);                    /* TLS_CHACHA20_POLY1305_SHA256: o mais rápido aqui */
    pos = por16(m, pos, 0x1301);                    /* TLS_AES_128_GCM_SHA256 */
    pos = por16(m, pos, 0x1302);                    /* TLS_AES_256_GCM_SHA384 */
    m[pos++] = 1;                                   /* compressão: só "nenhuma" */
    m[pos++] = 0;

    ext = pos;                                      /* o tamanho das extensões entra no fim */
    pos += 2;

    if (!parece_endereco(op->nome)) {               /* server_name (RFC 6066) */
        pos = por16(m, pos, 0);
        pos = por16(m, pos, nnome + 5);
        pos = por16(m, pos, nnome + 3);
        m[pos++] = 0;                               /* tipo: nome de DNS */
        pos = por16(m, pos, nnome);
        pos = por_bytes(m, pos, op->nome, nnome);
    }
    pos = por16(m, pos, 43);                        /* supported_versions: só TLS 1.3 */
    pos = por16(m, pos, 3);
    m[pos++] = 2;
    pos = por16(m, pos, 0x0304);

    pos = por16(m, pos, 10);                        /* supported_groups: só x25519 */
    pos = por16(m, pos, 4);
    pos = por16(m, pos, 2);
    pos = por16(m, pos, 0x001d);

    pos = por16(m, pos, 13);                        /* signature_algorithms */
    pos = por16(m, pos, 2 + sizeof assinaturas);
    pos = por16(m, pos, sizeof assinaturas);
    for (i = 0; i < sizeof assinaturas / sizeof assinaturas[0]; i++) pos = por16(m, pos, assinaturas[i]);

    pos = por16(m, pos, 51);                        /* key_share: nossa metade x25519 */
    pos = por16(m, pos, 2 + 4 + 32);
    pos = por16(m, pos, 4 + 32);
    pos = por16(m, pos, 0x001d);
    pos = por16(m, pos, 32);
    pos = por_bytes(m, pos, publica, 32);

    if (nalpn) {                                    /* ALPN (RFC 7301): um protocolo só */
        pos = por16(m, pos, 16);
        pos = por16(m, pos, nalpn + 3);
        pos = por16(m, pos, nalpn + 1);
        m[pos++] = (u8)nalpn;
        pos = por_bytes(m, pos, op->alpn, nalpn);
    }
    por16(m, ext, pos - ext - 2);

    m[0] = 1;                                       /* ClientHello */
    m[1] = 0;
    por16(m, 2, pos - 4);
    return enviar_mensagem(t, m, pos);
}

/* ------------------------------------------------------------ ServerHello e chaves */

/* O "random" que marca um HelloRetryRequest: é o SHA-256 do texto
 * "HelloRetryRequest" (RFC 8446, 4.1.3). Calculamos em vez de copiar. */
static int eh_nova_tentativa(const u8 *aleatorio)
{
    u8 marca[32];
    sha256("HelloRetryRequest", 17, marca);
    return !cri_diferentes(marca, aleatorio, 32);
}

static int tratar_ola_servidor(Tls *t, Aperto *a, Fatia f)
{
    Fatia aleatorio, sessao, exts, e, k;
    u32 versao, conjunto, compressao, tipo, v, grupo;
    const u8 *publica = 0;
    int tls13 = 0;
    u8 comum[32], zeros[RESUMO_TAM_MAX], h[RESUMO_TAM_MAX], cedo[RESUMO_TAM_MAX], derivado[RESUMO_TAM_MAX];

    if (fatia_ler16(&f, &versao) || fatia_cortar(&f, 32, &aleatorio) || fatia_vetor(&f, 1, &sessao) ||
        fatia_ler16(&f, &conjunto) || fatia_ler8(&f, &compressao))
        return malformada(t, "ServerHello");

    /* Sem extensões é um servidor de TLS 1.2 ou mais velho respondendo. */
    if (f.n == 0)
        return tls_falhar(t, ALERTA_VERSAO, "o servidor não fala TLS 1.3 (só versões antigas)", 0, 0);
    if (fatia_vetor(&f, 2, &exts) || f.n) return malformada(t, "ServerHello");

    if (eh_nova_tentativa(aleatorio.p))
        /* O servidor quer outro grupo de troca de chaves. Só oferecemos
         * x25519, então não há segunda tentativa a fazer. */
        return tls_falhar(t, 40, "o servidor pediu outra forma de troca de chaves (HelloRetryRequest) ",
                          "e este sistema só fala x25519", 0);

    while (exts.n) {
        if (fatia_ler16(&exts, &tipo) || fatia_vetor(&exts, 2, &e)) return malformada(t, "ServerHello");
        if (tipo == 43) {                           /* supported_versions: a versão de verdade */
            if (fatia_ler16(&e, &v) || e.n || tls13) return malformada(t, "ServerHello");
            if (v != 0x0304)
                return tls_falhar(t, ALERTA_PARAMETRO, "o servidor escolheu uma versão que não oferecemos", 0, 0);
            tls13 = 1;
        } else if (tipo == 51) {                    /* key_share: a metade do servidor */
            if (fatia_ler16(&e, &grupo) || fatia_vetor(&e, 2, &k) || e.n || publica)
                return malformada(t, "ServerHello");
            if (grupo != 0x001d || k.n != 32)
                return tls_falhar(t, ALERTA_PARAMETRO, "o servidor escolheu um grupo que não oferecemos", 0, 0);
            publica = k.p;
        } else {
            return tls_falhar(t, ALERTA_EXTENSAO, "extensão não pedida no ServerHello", 0, 0);
        }
    }
    if (!tls13)
        return tls_falhar(t, ALERTA_VERSAO, "o servidor não fala TLS 1.3 (só versões antigas)", 0, 0);
    if (versao != 0x0303 || compressao != 0 || !fatia_igual(sessao, a->sessao, 32))
        return tls_falhar(t, ALERTA_PARAMETRO, "ServerHello com campos de compatibilidade errados", 0, 0);
    if (!publica)
        return tls_falhar(t, 109, "o servidor não mandou a metade dele da troca de chaves", 0, 0);

    switch (conjunto) {
    case 0x1301: t->resumo = RESUMO_SHA256; t->tam_chave = 16; break;
    case 0x1302: t->resumo = RESUMO_SHA384; t->tam_chave = 32; break;
    case 0x1303: t->resumo = RESUMO_SHA256; t->tam_chave = 32; break;
    default:
        return tls_falhar(t, ALERTA_PARAMETRO, "o servidor escolheu um conjunto de cifras que não oferecemos", 0, 0);
    }
    t->conjunto = (u16)conjunto;
    t->tam_resumo = resumo_tamanho(t->resumo);

    /* O segredo em comum. O x25519 recusa o resultado todo zero (ponto de
     * ordem pequena), como a RFC manda. */
    if (x25519(comum, a->privada, publica) != CRIPTO_OK)
        return tls_falhar(t, ALERTA_PARAMETRO, "troca de chaves inválida (ponto de ordem pequena)", 0, 0);

    /* A escada de segredos (RFC 8446, 7.1). Cada degrau é um HKDF-Extract
     * cujo sal vem do degrau anterior:
     *
     *     Early Secret     = Extract(sal = 0,                   material = 0)   (não usamos PSK)
     *     Handshake Secret = Extract(sal = Derivar(Early),      material = segredo x25519)
     *     Master Secret    = Extract(sal = Derivar(Handshake),  material = 0)
     *
     * "Derivar" é o Expand-Label com o rótulo "derived" e o resumo do
     * texto vazio. Os segredos de tráfego saem do Handshake Secret com o
     * transcrito até o ServerHello. */
    for (u32 i = 0; i < sizeof zeros; i++) zeros[i] = 0;
    resumo_calcular(t->resumo, "", 0, h);
    hkdf_extrair(t->resumo, zeros, t->tam_resumo, zeros, t->tam_resumo, cedo);
    tls_expandir_rotulo(t->resumo, cedo, "derived", h, t->tam_resumo, derivado, t->tam_resumo);
    hkdf_extrair(t->resumo, derivado, t->tam_resumo, comum, 32, a->aperto);

    transcrito_agora(t, h);
    tls_expandir_rotulo(t->resumo, a->aperto, "c hs traffic", h, t->tam_resumo, a->cliente, t->tam_resumo);
    tls_expandir_rotulo(t->resumo, a->aperto, "s hs traffic", h, t->tam_resumo, a->servidor, t->tam_resumo);

    cri_zerar(comum, sizeof comum);
    cri_zerar(cedo, sizeof cedo);
    cri_zerar(derivado, sizeof derivado);
    return 0;
}

/* ------------------------------------------------------------ mensagens cifradas do servidor */

static int tratar_extensoes(Tls *t, const TlsOpcoes *op, Fatia f)
{
    Fatia exts, e, lista, proto;
    u32 tipo;

    if (fatia_vetor(&f, 2, &exts) || f.n) return malformada(t, "EncryptedExtensions");
    while (exts.n) {
        if (fatia_ler16(&exts, &tipo) || fatia_vetor(&exts, 2, &e)) return malformada(t, "EncryptedExtensions");
        switch (tipo) {
        case 0:             /* server_name: "entendi o nome"; vem vazia */
        case 10:            /* supported_groups: os grupos que o servidor prefere; só informa */
            break;
        case 16:            /* ALPN: o protocolo escolhido tem de ser o que oferecemos */
            if (!op->alpn || fatia_vetor(&e, 2, &lista) || e.n || fatia_vetor(&lista, 1, &proto) || lista.n ||
                !fatia_igual(proto, (const u8 *)op->alpn, txt_tamanho(op->alpn)))
                return tls_falhar(t, ALERTA_PARAMETRO, "o servidor escolheu um protocolo de aplicação que não oferecemos", 0, 0);
            break;
        default:
            /* O servidor só pode responder a extensões que mandamos. */
            return tls_falhar(t, ALERTA_EXTENSAO, "extensão não pedida no EncryptedExtensions", 0, 0);
        }
    }
    return 0;
}

static int tratar_certificado(Tls *t, const TlsOpcoes *op, Fatia f)
{
    X509Der cadeia[X509_CADEIA_MAX];
    X509 folha;
    Fatia contexto, lista, cert, exts;
    char agora[14], motivo[TLS_ERRO_MAX];
    int ano, mes, dia, hora, min, seg;
    u32 n = 0;

    /* Certificate: contexto (vazio no aperto de mão) e a lista; cada item é
     * um certificado em DER e as extensões dele (que não usamos). */
    if (fatia_vetor(&f, 1, &contexto) || contexto.n || fatia_vetor(&f, 3, &lista) || f.n)
        return malformada(t, "Certificate");
    while (lista.n) {
        if (fatia_vetor(&lista, 3, &cert) || cert.n == 0 || fatia_vetor(&lista, 2, &exts))
            return malformada(t, "Certificate");
        if (n < X509_CADEIA_MAX) {
            cadeia[n].der = cert.p;
            cadeia[n].n = cert.n;
            n++;
        }
    }
    if (n == 0) return tls_falhar(t, ALERTA_FORMATO, "o servidor não mandou certificado", 0, 0);

    t->amb.data_atual(t->amb.ctx, &ano, &mes, &dia, &hora, &min, &seg);
    x509_montar_data(agora, ano, mes, dia, hora, min, seg);
    if (x509_validar_cadeia(cadeia, n, op->raizes, op->nraizes, op->nome, agora, motivo, sizeof motivo))
        return tls_falhar(t, ALERTA_CERTIFICADO_RUIM, motivo, 0, 0);

    /* A cadeia mora no buffer de mensagens, que vai ser reaproveitado; a
     * chave da folha ainda é necessária para o CertificateVerify. */
    if (x509_ler(&folha, cadeia[0].der, cadeia[0].n) || folha.ninfo_chave > TLS_CHAVE_MAX)
        return tls_falhar(t, ALERTA_CERTIFICADO_RUIM, "a chave do servidor é grande demais para este sistema", 0, 0);
    cri_copiar(t->chave_folha, folha.info_chave, folha.ninfo_chave);
    t->nchave_folha = folha.ninfo_chave;
    return 0;
}

/* O servidor prova que tem a chave privada do certificado assinando o
 * transcrito. O texto assinado (RFC 8446, 4.4.3) é: 64 espaços, a frase
 * abaixo, um byte zero e o resumo do transcrito até o Certificate. Os
 * espaços e a frase existem para essa assinatura nunca servir em outro
 * protocolo ou em outro ponto deste. */
static int tratar_prova(Tls *t, Fatia f, const u8 *h)
{
    static const char frase[] = "TLS 1.3, server CertificateVerify";
    u8 texto[64 + 33 + 1 + RESUMO_TAM_MAX];
    X509Chave chave;
    Fatia assin;
    u32 alg, pos = 0;
    int tipo, resumo = 0, tipo_chave;

    if (fatia_ler16(&f, &alg) || fatia_vetor(&f, 2, &assin) || f.n) return malformada(t, "CertificateVerify");

    switch (alg) {
    case 0x0403: tipo = X509_ASSIN_ECDSA;   resumo = RESUMO_SHA256; tipo_chave = X509_CHAVE_P256; break;
    case 0x0503: tipo = X509_ASSIN_ECDSA;   resumo = RESUMO_SHA384; tipo_chave = X509_CHAVE_P384; break;
    case 0x0804: tipo = X509_ASSIN_RSA_PSS; resumo = RESUMO_SHA256; tipo_chave = X509_CHAVE_RSA; break;
    case 0x0805: tipo = X509_ASSIN_RSA_PSS; resumo = RESUMO_SHA384; tipo_chave = X509_CHAVE_RSA; break;
    case 0x0806: tipo = X509_ASSIN_RSA_PSS; resumo = RESUMO_SHA512; tipo_chave = X509_CHAVE_RSA; break;
    case 0x0807: tipo = X509_ASSIN_ED25519; tipo_chave = X509_CHAVE_ED25519; break;
    default:
        /* Inclui os rsa_pkcs1, que oferecemos só para certificados: no
         * TLS 1.3 a assinatura do aperto com RSA tem de ser PSS. */
        return tls_falhar(t, ALERTA_PARAMETRO, "o servidor assinou o aperto de mão com um algoritmo não permitido", 0, 0);
    }
    if (x509_ler_chave(&chave, t->chave_folha, t->nchave_folha) || chave.tipo != tipo_chave)
        return tls_falhar(t, ALERTA_PARAMETRO, "a assinatura do aperto de mão não combina com a chave do certificado", 0, 0);

    for (; pos < 64; pos++) texto[pos] = ' ';
    cri_copiar(texto + pos, frase, 33);
    pos += 33;
    texto[pos++] = 0;
    cri_copiar(texto + pos, h, t->tam_resumo);
    pos += t->tam_resumo;

    if (x509_conferir(&chave, tipo, resumo, texto, pos, assin.p, assin.n) != 0)
        return tls_falhar(t, ALERTA_ASSINATURA,
                          "o servidor não provou ser o dono do certificado (assinatura do aperto de mão inválida)", 0, 0);
    return 0;
}

/* O Finished é um HMAC do transcrito com uma chave tirada do segredo de
 * tráfego de quem fala. Confere que os dois lados viram a mesma conversa e
 * chegaram às mesmas chaves. */
static void calcular_fim(Tls *t, const u8 *segredo, const u8 *h, u8 *saida)
{
    u8 chave[RESUMO_TAM_MAX];
    tls_expandir_rotulo(t->resumo, segredo, "finished", 0, 0, chave, t->tam_resumo);
    hmac_calcular(t->resumo, chave, t->tam_resumo, h, t->tam_resumo, saida);
    cri_zerar(chave, sizeof chave);
}

/* ------------------------------------------------------------ a conversa inteira */

static int apertar(Tls *t, Aperto *a, const TlsOpcoes *op)
{
    static const u8 troca_cifra[1] = { 1 };
    Fatia corpo, inteira;
    u32 tipo;
    u8 h[RESUMO_TAM_MAX], fim[RESUMO_TAM_MAX], zeros[RESUMO_TAM_MAX];
    u8 derivado[RESUMO_TAM_MAX], mestre[RESUMO_TAM_MAX], segredo[RESUMO_TAM_MAX];
    u8 m[4 + RESUMO_TAM_MAX];

    if (enviar_ola(t, a, op)) return TLS_ERRO;

    if (esperar_mensagem(t, &tipo, &corpo, &inteira)) return TLS_ERRO;
    if (tipo != 2) return tls_falhar(t, ALERTA_INESPERADO, "esperava o ServerHello e veio outra mensagem", 0, 0);
    transcrito_juntar(t, inteira.p, inteira.n);
    if (tratar_ola_servidor(t, a, corpo)) return TLS_ERRO;
    if (exigir_buffer_vazio(t)) return TLS_ERRO;

    /* Registro de "troca de cifra" de fachada, às claras, antes do nosso
     * primeiro registro cifrado: com ele a conversa parece TLS 1.2 para
     * equipamentos do caminho (RFC 8446, apêndice D.4). */
    if (tls_registro_enviar(t, REG_TROCA_CIFRA, troca_cifra, 1)) return TLS_ERRO;
    tls_sentido_armar(t, &t->rx, a->servidor);
    tls_sentido_armar(t, &t->tx, a->cliente);

    if (esperar_mensagem(t, &tipo, &corpo, &inteira)) return TLS_ERRO;
    if (tipo != 8) return tls_falhar(t, ALERTA_INESPERADO, "esperava o EncryptedExtensions e veio outra mensagem", 0, 0);
    if (tratar_extensoes(t, op, corpo)) return TLS_ERRO;
    transcrito_juntar(t, inteira.p, inteira.n);

    if (esperar_mensagem(t, &tipo, &corpo, &inteira)) return TLS_ERRO;
    if (tipo == 13) {
        /* CertificateRequest: o servidor aceita certificado de cliente. Não
         * temos; a resposta certa é um Certificate vazio, mais abaixo. */
        Fatia contexto, exts;
        if (fatia_vetor(&corpo, 1, &contexto) || contexto.n || fatia_vetor(&corpo, 2, &exts) || corpo.n)
            return malformada(t, "CertificateRequest");
        a->pediu_certificado = 1;
        transcrito_juntar(t, inteira.p, inteira.n);
        if (esperar_mensagem(t, &tipo, &corpo, &inteira)) return TLS_ERRO;
    }
    if (tipo != 11) return tls_falhar(t, ALERTA_INESPERADO, "esperava o certificado do servidor e veio outra mensagem", 0, 0);
    if (tratar_certificado(t, op, corpo)) return TLS_ERRO;
    transcrito_juntar(t, inteira.p, inteira.n);

    transcrito_agora(t, h);                 /* a assinatura cobre até o Certificate */
    if (esperar_mensagem(t, &tipo, &corpo, &inteira)) return TLS_ERRO;
    if (tipo != 15) return tls_falhar(t, ALERTA_INESPERADO, "esperava o CertificateVerify e veio outra mensagem", 0, 0);
    if (tratar_prova(t, corpo, h)) return TLS_ERRO;
    transcrito_juntar(t, inteira.p, inteira.n);

    transcrito_agora(t, h);                 /* o Finished cobre até o CertificateVerify */
    if (esperar_mensagem(t, &tipo, &corpo, &inteira)) return TLS_ERRO;
    if (tipo != 20) return tls_falhar(t, ALERTA_INESPERADO, "esperava o Finished e veio outra mensagem", 0, 0);
    calcular_fim(t, a->servidor, h, fim);
    if (corpo.n != t->tam_resumo || cri_diferentes(fim, corpo.p, t->tam_resumo))
        return tls_falhar(t, ALERTA_ASSINATURA, "o Finished do servidor não confere (a negociação foi alterada no caminho?)", 0, 0);
    transcrito_juntar(t, inteira.p, inteira.n);
    if (exigir_buffer_vazio(t)) return TLS_ERRO;

    /* As chaves de aplicação saem do Master Secret com o transcrito até o
     * Finished do servidor (não entra o que o cliente manda depois). */
    for (u32 i = 0; i < sizeof zeros; i++) zeros[i] = 0;
    resumo_calcular(t->resumo, "", 0, h);
    tls_expandir_rotulo(t->resumo, a->aperto, "derived", h, t->tam_resumo, derivado, t->tam_resumo);
    hkdf_extrair(t->resumo, derivado, t->tam_resumo, zeros, t->tam_resumo, mestre);
    transcrito_agora(t, h);
    tls_expandir_rotulo(t->resumo, mestre, "s ap traffic", h, t->tam_resumo, segredo, t->tam_resumo);
    tls_sentido_armar(t, &t->rx, segredo);

    /* A nossa parte, ainda com a chave de aperto. */
    if (a->pediu_certificado) {
        static const u8 sem_certificado[8] = { 11, 0, 0, 4, 0, 0, 0, 0 };
        if (enviar_mensagem(t, sem_certificado, sizeof sem_certificado)) return TLS_ERRO;
    }
    transcrito_agora(t, fim);
    m[0] = 20;
    m[1] = 0;
    m[2] = 0;
    m[3] = (u8)t->tam_resumo;
    calcular_fim(t, a->cliente, fim, m + 4);
    if (enviar_mensagem(t, m, 4 + t->tam_resumo)) return TLS_ERRO;

    tls_expandir_rotulo(t->resumo, mestre, "c ap traffic", h, t->tam_resumo, segredo, t->tam_resumo);
    tls_sentido_armar(t, &t->tx, segredo);

    cri_zerar(mestre, sizeof mestre);
    cri_zerar(derivado, sizeof derivado);
    cri_zerar(segredo, sizeof segredo);
    return 0;
}

int tls_conectar(Tls *t, const TlsAmbiente *amb, const TlsOpcoes *op)
{
    Aperto a;
    int r;

    /* Zera só o que é estado; os buffers grandes não precisam. */
    t->amb = *amb;
    t->estado = EST_NOVO;
    t->fim_remoto = t->cortado = 0;
    t->erro[0] = 0;
    t->conjunto = 0;
    t->resumo = RESUMO_SHA256;
    t->tam_resumo = t->tam_chave = 0;
    t->rx.ativa = t->tx.ativa = 0;
    t->nchave_folha = 0;
    t->nentrada = 0;
    t->reg_ini = t->reg_fim = t->claro_ini = t->claro_fim = 0;
    t->msg_ini = t->msg_fim = 0;
    t->limite_ms = op->limite_ms ? op->limite_ms : 15000;
    t->inicio_ms = amb->agora_ms(amb->ctx);
    resumo_iniciar(&t->transcrito[0], RESUMO_SHA256);
    resumo_iniciar(&t->transcrito[1], RESUMO_SHA384);

    if (!op->nome) {
        txt_juntar(t->erro, sizeof t->erro, "falta o nome do servidor", 0, 0, 0, 0, 0);
        t->estado = EST_ERRO;
        return TLS_ERRO;
    }

    for (u32 i = 0; i < sizeof a; i++) ((u8 *)&a)[i] = 0;
    t->estado = EST_APERTO;
    r = apertar(t, &a, op);
    cri_zerar(&a, sizeof a);
    if (r) {
        if (t->estado != EST_ERRO) tls_falhar(t, ALERTA_INTERNO, "o aperto de mão falhou", 0, 0);
        return TLS_ERRO;
    }
    t->estado = EST_ABERTO;
    return TLS_OK;
}
