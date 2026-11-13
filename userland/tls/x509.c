/* x509.c - leitura de certificados X.509 e validação da cadeia (RFC 5280).
 *
 * Um certificado é uma SEQUENCE de três coisas: a parte assinada (TBS, "to
 * be signed"), o algoritmo da assinatura e a assinatura. A leitura só guarda
 * ponteiros para dentro do DER recebido; não copia nem reserva nada.
 *
 * A validação monta o caminho de baixo para cima: parte do certificado do
 * servidor e, a cada passo, procura quem o assinou, primeiro entre as raízes
 * de confiança (aí acabou) e depois entre os outros certificados que o
 * servidor mandou. */
#include "tls_interno.h"

/* ------------------------------------------------------------ identificadores
 *
 * Os OIDs já na forma em que aparecem no DER (RFC 8017, 5480, 5758, 8410 e
 * 5280). O primeiro byte junta os dois primeiros números (40*a + b); os
 * outros vão de 7 em 7 bits, com o bit de cima dizendo "continua". */

static const u8 OID_PKCS1[]      = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01 };  /* 1.2.840.113549.1.1 + um byte */
#define PKCS1_RSA      0x01
#define PKCS1_MGF1     0x08
#define PKCS1_PSS      0x0a
#define PKCS1_SHA256   0x0b
#define PKCS1_SHA384   0x0c
#define PKCS1_SHA512   0x0d
static const u8 OID_CHAVE_EC[]   = { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01 };        /* 1.2.840.10045.2.1 */
static const u8 OID_P256[]       = { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07 };  /* 1.2.840.10045.3.1.7 */
static const u8 OID_P384[]       = { 0x2b, 0x81, 0x04, 0x00, 0x22 };                    /* 1.3.132.0.34 */
static const u8 OID_ECDSA_SHA2[] = { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03 };        /* 1.2.840.10045.4.3 + um byte */
static const u8 OID_ED25519[]    = { 0x2b, 0x65, 0x70 };                                /* 1.3.101.112 */
static const u8 OID_SHA2[]       = { 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02 };  /* 2.16.840.1.101.3.4.2 + um byte */
static const u8 OID_EXTENSAO[]   = { 0x55, 0x1d };                                      /* 2.5.29 + um byte */
#define EXT_USO            0x0f     /* keyUsage */
#define EXT_NOMES          0x11     /* subjectAltName */
#define EXT_RESTRICOES     0x13     /* basicConstraints */
#define EXT_POLITICAS      0x20     /* certificatePolicies */
#define EXT_USO_ESTENDIDO  0x25     /* extKeyUsage */
static const u8 OID_NOME_COMUM[] = { 0x55, 0x04, 0x03 };                                /* 2.5.4.3 */
static const u8 OID_USO_SERVIDOR[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01 };/* 1.3.6.1.5.5.7.3.1 */
static const u8 OID_USO_QUALQUER[] = { 0x55, 0x1d, 0x25, 0x00 };                        /* 2.5.29.37.0 */

/* O OID é "prefixo + um byte"? Se for, devolve esse byte; senão -1. */
static int oid_familia(Fatia oid, const u8 *prefixo, u32 n)
{
    if (oid.n != n + 1 || !fatia_igual((Fatia){ oid.p, n }, prefixo, n)) return -1;
    return oid.p[n];
}

/* Um AlgorithmIdentifier de resumo: SEQUENCE { OID, NULL opcional }. */
static int ler_resumo(Fatia *f)
{
    Fatia alg, oid, nada;
    if (der_esperar(f, DER_SEQUENCIA, &alg) || der_esperar(&alg, DER_OID, &oid)) return 0;
    if (alg.n && (der_esperar(&alg, DER_NULO, &nada) || nada.n || alg.n)) return 0;
    switch (oid_familia(oid, OID_SHA2, sizeof OID_SHA2)) {
    case 1: return RESUMO_SHA256;
    case 2: return RESUMO_SHA384;
    case 3: return RESUMO_SHA512;
    }
    return 0;
}

/* Parâmetros do RSASSA-PSS (RFC 8017, A.2.3). Todos os campos têm valor
 * padrão baseado em SHA-1, que não aceitamos; então, para servir, o
 * certificado tem de dizer tudo: o resumo, o MGF1 com o mesmo resumo e o sal
 * do tamanho do resumo (é a única forma que a nossa cripto confere, e é a
 * que as autoridades usam). Devolve o resumo ou 0. */
static int ler_parametros_pss(Fatia par)
{
    Fatia seq, x, mgf, oid;
    u32 v;
    int resumo, resumo_mgf;

    if (der_esperar(&par, DER_SEQUENCIA, &seq) || par.n) return 0;
    if (der_esperar(&seq, 0xa0, &x)) return 0;
    resumo = ler_resumo(&x);
    if (!resumo || x.n) return 0;

    if (der_esperar(&seq, 0xa1, &x) || der_esperar(&x, DER_SEQUENCIA, &mgf) || x.n) return 0;
    if (der_esperar(&mgf, DER_OID, &oid) || oid_familia(oid, OID_PKCS1, sizeof OID_PKCS1) != PKCS1_MGF1) return 0;
    resumo_mgf = ler_resumo(&mgf);
    if (resumo_mgf != resumo || mgf.n) return 0;

    if (der_esperar(&seq, 0xa2, &x) || der_numero(&x, &v) || x.n) return 0;
    if (v != resumo_tamanho(resumo)) return 0;

    if (seq.n) {                            /* trailerField: só existe o valor 1 */
        if (der_esperar(&seq, 0xa3, &x) || der_numero(&x, &v) || x.n || v != 1 || seq.n) return 0;
    }
    return resumo;
}

/* Descobre o jeito de assinar a partir do AlgorithmIdentifier. O que não
 * conhecemos (SHA-1, MD5...) fica como "desconhecido": não é erro de
 * leitura, só não dá para conferir. */
static void ler_algoritmo_assinatura(Fatia alg, int *tipo, int *resumo)
{
    Fatia oid, nada;
    int b;

    *tipo = X509_ASSIN_DESCONHECIDA;
    *resumo = 0;
    if (der_esperar(&alg, DER_OID, &oid)) return;

    if ((b = oid_familia(oid, OID_PKCS1, sizeof OID_PKCS1)) >= 0) {
        if (b == PKCS1_PSS) {
            int r = ler_parametros_pss(alg);
            if (r) { *tipo = X509_ASSIN_RSA_PSS; *resumo = r; }
            return;
        }
        if (b != PKCS1_SHA256 && b != PKCS1_SHA384 && b != PKCS1_SHA512) return;
        /* O parâmetro é NULL; algumas escritas antigas o deixam de fora. */
        if (alg.n && (der_esperar(&alg, DER_NULO, &nada) || nada.n || alg.n)) return;
        *tipo = X509_ASSIN_RSA_PKCS1;
        *resumo = b == PKCS1_SHA256 ? RESUMO_SHA256 : b == PKCS1_SHA384 ? RESUMO_SHA384 : RESUMO_SHA512;
    } else if ((b = oid_familia(oid, OID_ECDSA_SHA2, sizeof OID_ECDSA_SHA2)) >= 0) {
        if (alg.n || b < 2 || b > 4) return;        /* 2, 3, 4: SHA-256, 384, 512; sem parâmetros */
        *tipo = X509_ASSIN_ECDSA;
        *resumo = b == 2 ? RESUMO_SHA256 : b == 3 ? RESUMO_SHA384 : RESUMO_SHA512;
    } else if (fatia_igual(oid, OID_ED25519, sizeof OID_ED25519)) {
        if (alg.n) return;
        *tipo = X509_ASSIN_ED25519;
    }
}

/* ------------------------------------------------------------ chave pública */

int x509_ler_chave(X509Chave *k, const u8 *spki, u32 n)
{
    Fatia tudo = { spki, n }, info, alg, oid, bits, seq, x;
    u32 sobra;

    k->tipo = X509_CHAVE_DESCONHECIDA;
    k->a = k->b = 0;
    k->na = k->nb = 0;

    /* SubjectPublicKeyInfo ::= SEQUENCE { algoritmo, BIT STRING com a chave } */
    if (der_esperar(&tudo, DER_SEQUENCIA, &info) || tudo.n) return -1;
    if (der_esperar(&info, DER_SEQUENCIA, &alg) || der_esperar(&alg, DER_OID, &oid)) return -1;
    if (der_bits(&info, &bits, &sobra) || info.n) return -1;
    if (sobra) return 0;

    if (oid_familia(oid, OID_PKCS1, sizeof OID_PKCS1) == PKCS1_RSA) {
        /* A chave é, ela mesma, um DER: SEQUENCE { módulo, expoente }. */
        Fatia modulo, expoente;
        if (der_esperar(&bits, DER_SEQUENCIA, &seq) || bits.n) return -1;
        if (der_inteiro_grande(&seq, &modulo) || der_inteiro_grande(&seq, &expoente) || seq.n) return -1;
        k->tipo = X509_CHAVE_RSA;
        k->a = modulo.p;   k->na = modulo.n;
        k->b = expoente.p; k->nb = expoente.n;
    } else if (fatia_igual(oid, OID_CHAVE_EC, sizeof OID_CHAVE_EC)) {
        /* O parâmetro diz a curva; a chave é o ponto 04 || X || Y. */
        if (der_esperar(&alg, DER_OID, &x) || alg.n) return 0;     /* curva descrita por extenso: não usamos */
        if (fatia_igual(x, OID_P256, sizeof OID_P256) && bits.n == 65 && bits.p[0] == 4)
            k->tipo = X509_CHAVE_P256;
        else if (fatia_igual(x, OID_P384, sizeof OID_P384) && bits.n == 97 && bits.p[0] == 4)
            k->tipo = X509_CHAVE_P384;
        else
            return 0;
        k->a = bits.p; k->na = bits.n;
    } else if (fatia_igual(oid, OID_ED25519, sizeof OID_ED25519)) {
        if (alg.n || bits.n != 32) return 0;
        k->tipo = X509_CHAVE_ED25519;
        k->a = bits.p; k->na = bits.n;
    }
    return 0;
}

int x509_conferir(const X509Chave *k, int assin_tipo, int assin_resumo,
                  const u8 *dados, u32 n, const u8 *assin, u32 nassin)
{
    u8 h[RESUMO_TAM_MAX];
    u32 tam = resumo_tamanho(assin_resumo);
    int r;

    switch (assin_tipo) {
    case X509_ASSIN_RSA_PKCS1:
    case X509_ASSIN_RSA_PSS:
        if (k->tipo != X509_CHAVE_RSA || !tam) return -2;
        resumo_calcular(assin_resumo, dados, n, h);
        if (assin_tipo == X509_ASSIN_RSA_PKCS1)
            r = rsa_verificar_pkcs1(k->a, k->na, k->b, k->nb, assin_resumo, h, assin, nassin);
        else
            r = rsa_verificar_pss(k->a, k->na, k->b, k->nb, assin_resumo, h, assin, nassin);
        return r == CRIPTO_OK ? 0 : -1;
    case X509_ASSIN_ECDSA:
        if ((k->tipo != X509_CHAVE_P256 && k->tipo != X509_CHAVE_P384) || !tam) return -2;
        resumo_calcular(assin_resumo, dados, n, h);
        r = ecdsa_verificar_der(k->tipo == X509_CHAVE_P256 ? CURVA_P256 : CURVA_P384,
                                k->a, k->na, h, tam, assin, nassin);
        return r == CRIPTO_OK ? 0 : -1;
    case X509_ASSIN_ED25519:
        if (k->tipo != X509_CHAVE_ED25519) return -2;
        if (nassin != 64) return -1;
        return ed25519_verificar(assin, dados, n, k->a) == CRIPTO_OK ? 0 : -1;
    }
    return -2;
}

/* ------------------------------------------------------------ nomes e datas */

/* Procura o nome comum (CN) dentro de um Name, que é uma SEQUENCE de SETs
 * de pares (tipo, valor). Fica com o último, que é o mais específico. Só
 * serve para mensagens e para o caso antigo de certificado sem
 * subjectAltName. */
static void nome_comum(const u8 *nome, u32 n, char cn[X509_CN_MAX])
{
    Fatia tudo = { nome, n }, lista, conjunto, par, oid, valor;
    u8 etq;

    cn[0] = 0;
    if (der_esperar(&tudo, DER_SEQUENCIA, &lista)) return;
    while (lista.n) {
        if (der_esperar(&lista, DER_CONJUNTO, &conjunto)) return;
        while (conjunto.n) {
            if (der_esperar(&conjunto, DER_SEQUENCIA, &par) || der_esperar(&par, DER_OID, &oid)) return;
            if (der_proximo(&par, &etq, &valor, 0)) return;
            if (!fatia_igual(oid, OID_NOME_COMUM, sizeof OID_NOME_COMUM)) continue;
            /* UTF8String, PrintableString, TeletexString e IA5String: um
             * byte por letra (ou UTF-8, que mostramos como veio). */
            if (etq != 0x0c && etq != 0x13 && etq != 0x14 && etq != 0x16) continue;
            u32 i;
            for (i = 0; i < valor.n && i + 1 < X509_CN_MAX; i++)
                cn[i] = valor.p[i] < 0x20 || valor.p[i] == 0x7f ? '?' : (char)valor.p[i];
            cn[i] = 0;
        }
    }
}

static int dois_digitos(const u8 *p, u32 min, u32 max)
{
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    u32 v = (u32)(p[0] - '0') * 10 + (u32)(p[1] - '0');
    return v < min || v > max ? -1 : 0;
}

/* Uma data do certificado vira 14 letras, AAAAMMDDhhmmss: assim comparar
 * datas é comparar texto, sem conta nenhuma. A RFC 5280 só admite as formas
 * com segundos e terminadas em Z (UTC). */
static int ler_data(Fatia *f, char data[14])
{
    Fatia c;
    u8 etq;
    const u8 *p;

    if (der_proximo(f, &etq, &c, 0)) return -1;
    if (etq == DER_UTCTIME && c.n == 13) {
        /* Ano com dois dígitos: de 50 a 99 é 19xx, de 00 a 49 é 20xx. */
        if (dois_digitos(c.p, 0, 99)) return -1;
        data[0] = c.p[0] >= '5' ? '1' : '2';
        data[1] = c.p[0] >= '5' ? '9' : '0';
        data[2] = (char)c.p[0];
        data[3] = (char)c.p[1];
        p = c.p + 2;
    } else if (etq == DER_GENTIME && c.n == 15) {
        if (dois_digitos(c.p, 0, 99) || dois_digitos(c.p + 2, 0, 99)) return -1;
        for (u32 i = 0; i < 4; i++) data[i] = (char)c.p[i];
        p = c.p + 4;
    } else {
        return -1;
    }
    if (dois_digitos(p, 1, 12) || dois_digitos(p + 2, 1, 31) || dois_digitos(p + 4, 0, 23) ||
        dois_digitos(p + 6, 0, 59) || dois_digitos(p + 8, 0, 60) || p[10] != 'Z')
        return -1;
    for (u32 i = 0; i < 10; i++) data[4 + i] = (char)p[i];
    return 0;
}

void x509_montar_data(char data[14], int ano, int mes, int dia, int hora, int min, int seg)
{
    u32 v[6] = { (u32)ano, (u32)mes, (u32)dia, (u32)hora, (u32)min, (u32)seg };
    data[0] = (char)('0' + v[0] / 1000 % 10);
    data[1] = (char)('0' + v[0] / 100 % 10);
    data[2] = (char)('0' + v[0] / 10 % 10);
    data[3] = (char)('0' + v[0] % 10);
    for (u32 i = 1; i < 6; i++) {
        data[2 + 2 * i] = (char)('0' + v[i] / 10 % 10);
        data[3 + 2 * i] = (char)('0' + v[i] % 10);
    }
}

/* -1, 0 ou 1, como uma subtração. */
static int comparar_datas(const char a[14], const char b[14])
{
    for (u32 i = 0; i < 14; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* "DD/MM/AAAA" para as mensagens. */
static void data_legivel(const char data[14], char texto[11])
{
    texto[0] = data[6]; texto[1] = data[7]; texto[2] = '/';
    texto[3] = data[4]; texto[4] = data[5]; texto[5] = '/';
    texto[6] = data[0]; texto[7] = data[1]; texto[8] = data[2]; texto[9] = data[3];
    texto[10] = 0;
}

/* ------------------------------------------------------------ extensões */

static int ler_extensoes(X509 *c, Fatia lista)
{
    u32 vistas = 0;                         /* um bit por extensão conhecida, para pegar repetição */

    while (lista.n) {
        Fatia ext, oid, valor, x;
        int critica = 0, qual;
        u32 sobra, v;

        /* Extension ::= SEQUENCE { OID, crítica (padrão: não), OCTET STRING } */
        if (der_esperar(&lista, DER_SEQUENCIA, &ext) || der_esperar(&ext, DER_OID, &oid)) return -1;
        if (der_espiar(&ext) == DER_BOOLEANO && der_booleano(&ext, &critica)) return -1;
        if (der_esperar(&ext, DER_OCTETOS, &valor) || ext.n) return -1;

        qual = oid_familia(oid, OID_EXTENSAO, sizeof OID_EXTENSAO);
        if (qual == EXT_USO || qual == EXT_NOMES || qual == EXT_RESTRICOES || qual == EXT_USO_ESTENDIDO) {
            /* A mesma extensão duas vezes é proibida (e seria um jeito de
             * confundir: qual das duas vale?). */
            u32 bit = 1u << (qual & 31);
            if (vistas & bit) return -1;
            vistas |= bit;
        }

        switch (qual) {
        case EXT_USO:
            /* Cadeia de bits; o bit 0 é o mais alto do primeiro byte. */
            if (der_bits(&valor, &x, &sobra) || valor.n) return -1;
            c->tem_uso = 1;
            c->uso = x.n ? x.p[0] : 0;
            break;
        case EXT_NOMES:
            if (der_esperar(&valor, DER_SEQUENCIA, &x) || valor.n) return -1;
            c->tem_nomes = 1;
            c->nomes = x.p;
            c->nnomes = x.n;
            break;
        case EXT_RESTRICOES:
            /* SEQUENCE { cA BOOLEAN (padrão: não), pathLen INTEGER opcional } */
            if (der_esperar(&valor, DER_SEQUENCIA, &x) || valor.n) return -1;
            c->tem_restricoes = 1;
            if (der_espiar(&x) == DER_BOOLEANO) {
                int ca;
                if (der_booleano(&x, &ca)) return -1;
                c->autoridade = (u8)ca;
            }
            if (x.n) {
                if (der_numero(&x, &v) || x.n || v > 0x7fffffff) return -1;
                c->caminho_max = (int)v;
            }
            break;
        case EXT_USO_ESTENDIDO:
            if (der_esperar(&valor, DER_SEQUENCIA, &x) || valor.n) return -1;
            c->tem_uso_estendido = 1;
            while (x.n) {
                Fatia uso;
                if (der_esperar(&x, DER_OID, &uso)) return -1;
                if (fatia_igual(uso, OID_USO_SERVIDOR, sizeof OID_USO_SERVIDOR) ||
                    fatia_igual(uso, OID_USO_QUALQUER, sizeof OID_USO_QUALQUER))
                    c->serve_servidor = 1;
            }
            break;
        case EXT_POLITICAS:
            /* Não exigimos política nenhuma, e nesse caso qualquer conjunto
             * de políticas serve: pode vir marcada como crítica. */
            break;
        default:
            /* RFC 5280, 4.2: extensão crítica que não se entende obriga a
             * recusar o certificado. Fica anotado; quem valida recusa. */
            if (critica) c->critica_desconhecida = 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ o certificado */

int x509_ler(X509 *c, const u8 *der, u32 n)
{
    Fatia tudo = { der, n }, cert, tbs, inteiro, alg_fora, alg_dentro, x, validade;
    u32 sobra, versao = 0;
    u8 etq;

    for (u32 i = 0; i < sizeof *c; i++) ((u8 *)c)[i] = 0;
    c->caminho_max = -1;

    /* Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signatureValue } */
    if (der_esperar(&tudo, DER_SEQUENCIA, &cert) || tudo.n) return -1;
    c->der = der;
    c->nder = n;
    if (der_proximo(&cert, &etq, &tbs, &inteiro) || etq != DER_SEQUENCIA) return -1;
    c->tbs = inteiro.p;                     /* a assinatura cobre o TBS com o cabeçalho */
    c->ntbs = inteiro.n;
    if (der_esperar(&cert, DER_SEQUENCIA, &alg_fora)) return -1;
    if (der_bits(&cert, &x, &sobra) || sobra || cert.n) return -1;
    c->assin = x.p;
    c->nassin = x.n;

    /* TBSCertificate: versão [0] (padrão v1), número de série, algoritmo,
     * emissor, validade, sujeito, chave, e as extensões em [3]. */
    if (der_espiar(&tbs) == 0xa0) {
        if (der_esperar(&tbs, 0xa0, &x) || der_numero(&x, &versao) || x.n || versao > 2) return -1;
    }
    if (der_esperar(&tbs, DER_INTEIRO, &x) || x.n == 0) return -1;      /* o número de série não nos importa */
    if (der_esperar(&tbs, DER_SEQUENCIA, &alg_dentro)) return -1;
    /* O algoritmo aparece dentro e fora da parte assinada e tem de ser o
     * mesmo (RFC 5280, 4.1.1.2): o de fora, sozinho, não está assinado. */
    if (!fatia_igual(alg_fora, alg_dentro.p, alg_dentro.n)) return -1;
    ler_algoritmo_assinatura(alg_dentro, &c->assin_tipo, &c->assin_resumo);

    if (der_proximo(&tbs, &etq, &x, &inteiro) || etq != DER_SEQUENCIA) return -1;
    c->emissor = inteiro.p;
    c->nemissor = inteiro.n;

    if (der_esperar(&tbs, DER_SEQUENCIA, &validade)) return -1;
    if (ler_data(&validade, c->inicio) || ler_data(&validade, c->fim) || validade.n) return -1;

    if (der_proximo(&tbs, &etq, &x, &inteiro) || etq != DER_SEQUENCIA) return -1;
    c->sujeito = inteiro.p;
    c->nsujeito = inteiro.n;
    nome_comum(c->sujeito, c->nsujeito, c->cn);

    if (der_proximo(&tbs, &etq, &x, &inteiro) || etq != DER_SEQUENCIA) return -1;
    c->info_chave = inteiro.p;
    c->ninfo_chave = inteiro.n;
    if (x509_ler_chave(&c->chave, inteiro.p, inteiro.n)) return -1;

    /* Identificadores únicos [1] e [2]: coisa da versão 2, sem uso. */
    if (versao >= 1 && der_espiar(&tbs) == 0x81 && der_esperar(&tbs, 0x81, &x)) return -1;
    if (versao >= 1 && der_espiar(&tbs) == 0x82 && der_esperar(&tbs, 0x82, &x)) return -1;
    if (versao == 2 && der_espiar(&tbs) == 0xa3) {
        Fatia lista;
        if (der_esperar(&tbs, 0xa3, &x) || der_esperar(&x, DER_SEQUENCIA, &lista) || x.n) return -1;
        if (ler_extensoes(c, lista)) return -1;
    }
    return tbs.n ? -1 : 0;
}

/* ------------------------------------------------------------ nome do servidor */

static u8 minuscula(u8 c)
{
    return c >= 'A' && c <= 'Z' ? (u8)(c + 32) : c;
}

static int texto_igual(const u8 *a, const u8 *b, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (minuscula(a[i]) != minuscula(b[i])) return 0;
    return 1;
}

/* O nome pedido casa com o nome (ou padrão) do certificado? RFC 6125, 6.4:
 * comparação sem diferenciar maiúsculas; o curinga só vale como o rótulo
 * mais à esquerda INTEIRO ("*.exemplo.org", nunca "w*.exemplo.org") e cobre
 * exatamente um rótulo: "*.exemplo.org" casa com "a.exemplo.org", mas não
 * com "exemplo.org" nem com "a.b.exemplo.org". Também não aceitamos curinga
 * colado no domínio de topo ("*.org"). */
static int nome_casa(const u8 *padrao, u32 np, const u8 *nome, u32 nn)
{
    u32 i, pontos = 0;

    if (np && padrao[np - 1] == '.') np--;      /* ponto final (raiz do DNS) não conta */
    if (nn && nome[nn - 1] == '.') nn--;
    if (np == 0 || nn == 0) return 0;

    if (padrao[0] != '*') {
        for (i = 0; i < np; i++)
            if (padrao[i] == '*') return 0;
        return np == nn && texto_igual(padrao, nome, np);
    }

    if (np < 3 || padrao[1] != '.') return 0;
    for (i = 2; i < np; i++) {
        if (padrao[i] == '*') return 0;
        if (padrao[i] == '.') pontos++;
    }
    if (pontos == 0 || padrao[np - 1] == '.') return 0;

    /* O primeiro rótulo do nome (não vazio) fica por conta do curinga; o
     * resto, a partir do ponto, tem de ser igual. */
    for (i = 0; i < nn; i++)
        if (nome[i] == '*') return 0;           /* asterisco não é letra de nome de servidor */
    for (i = 0; i < nn && nome[i] != '.'; i++) { }
    if (i == 0 || i == nn) return 0;
    return nn - i == np - 1 && texto_igual(padrao + 1, nome + i, np - 1);
}

/* "192.168.0.1" → 4 bytes. Um nome só de números e pontos é endereço, não
 * nome de DNS, e é conferido contra as entradas de endereço do certificado. */
static int ler_ipv4(const u8 *s, u32 n, u8 ip[4])
{
    u32 parte = 0, v = 0, digitos = 0;
    for (u32 i = 0; i <= n; i++) {
        if (i < n && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (u32)(s[i] - '0');
            if (++digitos > 3 || v > 255) return 0;
        } else if (i == n || s[i] == '.') {
            if (digitos == 0 || parte > 3) return 0;
            ip[parte++] = (u8)v;
            v = 0; digitos = 0;
        } else {
            return 0;
        }
    }
    return parte == 4;
}

int x509_nome_confere(const X509 *c, const char *nome)
{
    const u8 *s = (const u8 *)nome;
    u32 n = txt_tamanho(nome);
    u8 ip[4];
    int eh_ip = ler_ipv4(s, n, ip);

    if (c->tem_nomes) {
        /* GeneralNames: cada entrada tem uma etiqueta de contexto; [2] é
         * nome de DNS e [7] é endereço IP. Havendo a extensão, o nome comum
         * não é mais consultado (RFC 6125, 6.4.4). */
        Fatia lista = { c->nomes, c->nnomes }, v;
        u8 etq;
        while (lista.n) {
            if (der_proximo(&lista, &etq, &v, 0)) return 0;
            if (etq == 0x82 && !eh_ip && nome_casa(v.p, v.n, s, n)) return 1;
            if (etq == 0x87 && eh_ip && fatia_igual(v, ip, 4)) return 1;
        }
        return 0;
    }
    if (eh_ip) return 0;
    return nome_casa((const u8 *)c->cn, txt_tamanho(c->cn), s, n);
}

/* ------------------------------------------------------------ a cadeia */

static int mesmo_nome(const u8 *a, u32 na, const u8 *b, u32 nb)
{
    /* A RFC 5280 descreve uma comparação de nomes cheia de regras; na
     * prática as autoridades copiam o nome byte a byte, e comparar os bytes
     * só erra para o lado seguro (recusar). */
    return fatia_igual((Fatia){ a, na }, b, nb);
}

static const char *cn_ou_nada(const char *cn)
{
    return cn[0] ? cn : "(sem nome)";
}

/* Motivos pelos quais um candidato a emissor foi descartado. Se nenhum
 * candidato servir, o motivo mais grave vira a mensagem. */
#define MOTIVO_NENHUM       0
#define MOTIVO_ALGORITMO    1
#define MOTIVO_CAMINHO      2
#define MOTIVO_NAO_AUTORID  3
#define MOTIVO_RAIZ_DATA    4
#define MOTIVO_ASSINATURA   5

/* "emissor" pode ter assinado "c", que está "nivel" degraus acima da folha?
 * Devolve MOTIVO_NENHUM se sim. */
static int testar_emissor(const X509 *c, const X509 *emissor, u32 nivel, int eh_raiz)
{
    /* Tem de ser autoridade. Numa raiz antiga (versão 1, sem extensões)
     * vale a palavra de quem a pôs entre as de confiança. */
    if (emissor->tem_restricoes ? !emissor->autoridade : !eh_raiz) return MOTIVO_NAO_AUTORID;
    if (emissor->tem_uso && !(emissor->uso & X509_USO_ASSINAR_CERT)) return MOTIVO_NAO_AUTORID;
    /* pathLen: quantas autoridades intermediárias podem existir abaixo
     * desta. Abaixo do emissor há "nivel" delas (a folha não conta). */
    if (emissor->caminho_max >= 0 && (u32)emissor->caminho_max < nivel) return MOTIVO_CAMINHO;

    int r = x509_conferir(&emissor->chave, c->assin_tipo, c->assin_resumo, c->tbs, c->ntbs, c->assin, c->nassin);
    if (r == -2) return MOTIVO_ALGORITMO;
    if (r != 0) return MOTIVO_ASSINATURA;
    return MOTIVO_NENHUM;
}

static int fora_da_validade(const X509 *c, const char agora[14], char *erro, u32 nerro)
{
    char dia[11];
    if (comparar_datas(agora, c->inicio) < 0) {
        data_legivel(c->inicio, dia);
        txt_juntar(erro, nerro, "o certificado de '", cn_ou_nada(c->cn),
                   "' ainda não é válido: só vale a partir de ", dia, " (confira o relógio)", 0);
        return 1;
    }
    if (comparar_datas(agora, c->fim) > 0) {
        data_legivel(c->fim, dia);
        txt_juntar(erro, nerro, "certificado vencido: o de '", cn_ou_nada(c->cn), "' venceu em ", dia, 0, 0);
        return 1;
    }
    return 0;
}

int x509_validar_cadeia(const X509Der *cadeia, u32 ncadeia,
                        const X509Der *raizes, u32 nraizes,
                        const char *nome, const char agora[14],
                        char *erro, u32 nerro)
{
    X509 c[X509_CADEIA_MAX];
    u8 serve[X509_CADEIA_MAX];              /* leu direito e ainda não entrou no caminho */
    X509 raiz;
    const X509 *atual;
    char cn_emissor[X509_CN_MAX];
    u32 nivel = 0, i;

    if (ncadeia == 0) {
        txt_juntar(erro, nerro, "o servidor não mandou certificado", 0, 0, 0, 0, 0);
        return -1;
    }
    if (ncadeia > X509_CADEIA_MAX) ncadeia = X509_CADEIA_MAX;
    for (i = 0; i < ncadeia; i++)
        serve[i] = x509_ler(&c[i], cadeia[i].der, cadeia[i].n) == 0;
    if (!serve[0]) {
        txt_juntar(erro, nerro, "o certificado do servidor está malformado", 0, 0, 0, 0, 0);
        return -1;
    }
    serve[0] = 0;
    atual = &c[0];

    /* O que a folha tem de ter para servir a um servidor TLS 1.3: se diz
     * para que serve a chave, tem de incluir assinar; se lista usos
     * estendidos, tem de incluir autenticar servidor. */
    if ((atual->tem_uso && !(atual->uso & X509_USO_ASSINAR)) ||
        (atual->tem_uso_estendido && !atual->serve_servidor)) {
        txt_juntar(erro, nerro, "o certificado de '", cn_ou_nada(atual->cn),
                   "' não serve para identificar um servidor", 0, 0, 0);
        return -1;
    }

    for (;;) {
        int motivo = MOTIVO_NENHUM, m;
        const X509 *achado = 0;

        if (atual->critica_desconhecida) {
            txt_juntar(erro, nerro, "o certificado de '", cn_ou_nada(atual->cn),
                       "' tem uma extensão crítica que este sistema não conhece", 0, 0, 0);
            return -1;
        }
        if (fora_da_validade(atual, agora, erro, nerro)) return -1;

        /* 1. O próprio certificado está entre os de confiança? (A raiz veio
         * na cadeia, ou o usuário confia direto neste certificado.) */
        for (i = 0; i < nraizes; i++)
            if (fatia_igual((Fatia){ raizes[i].der, raizes[i].n }, atual->der, atual->nder)) goto confiavel;

        /* 2. Alguma raiz de confiança o assinou? */
        for (i = 0; i < nraizes; i++) {
            if (x509_ler(&raiz, raizes[i].der, raizes[i].n)) continue;
            if (!mesmo_nome(raiz.sujeito, raiz.nsujeito, atual->emissor, atual->nemissor)) continue;
            m = testar_emissor(atual, &raiz, nivel, 1);
            if (m == MOTIVO_NENHUM && (comparar_datas(agora, raiz.inicio) < 0 || comparar_datas(agora, raiz.fim) > 0))
                m = MOTIVO_RAIZ_DATA;
            if (m == MOTIVO_NENHUM) goto confiavel;
            if (m > motivo) motivo = m;
        }

        /* 3. Algum outro certificado da cadeia o assinou? Não confiamos na
         * ordem em que o servidor mandou: procuramos pelo nome. */
        for (i = 1; i < ncadeia && !achado; i++) {
            if (!serve[i]) continue;
            if (!mesmo_nome(c[i].sujeito, c[i].nsujeito, atual->emissor, atual->nemissor)) continue;
            m = testar_emissor(atual, &c[i], nivel, 0);
            if (m == MOTIVO_NENHUM) {
                achado = &c[i];
                serve[i] = 0;               /* cada certificado entra uma vez só: não há como dar voltas */
            } else if (m > motivo) {
                motivo = m;
            }
        }
        if (achado) {
            atual = achado;
            nivel++;
            continue;
        }

        /* Ninguém serviu: explicar por quê. */
        nome_comum(atual->emissor, atual->nemissor, cn_emissor);
        switch (motivo) {
        case MOTIVO_ASSINATURA:
            txt_juntar(erro, nerro, "assinatura inválida no certificado de '", cn_ou_nada(atual->cn),
                       "' (não foi mesmo '", cn_ou_nada(cn_emissor), "' quem assinou, ou ele foi alterado)", 0);
            break;
        case MOTIVO_RAIZ_DATA:
            txt_juntar(erro, nerro, "certificado vencido: o da autoridade raiz '", cn_ou_nada(cn_emissor),
                       "' está fora da validade", 0, 0, 0);
            break;
        case MOTIVO_NAO_AUTORID:
            txt_juntar(erro, nerro, "o certificado de '", cn_ou_nada(atual->cn), "' foi emitido por '",
                       cn_ou_nada(cn_emissor), "', que não é uma autoridade certificadora", 0);
            break;
        case MOTIVO_CAMINHO:
            txt_juntar(erro, nerro, "a autoridade '", cn_ou_nada(cn_emissor),
                       "' não tem permissão para ter outras autoridades abaixo dela (limite de caminho)", 0, 0, 0);
            break;
        case MOTIVO_ALGORITMO:
            txt_juntar(erro, nerro, "o certificado de '", cn_ou_nada(atual->cn),
                       "' usa um algoritmo de assinatura que este sistema não conhece", 0, 0, 0);
            break;
        default:
            if (mesmo_nome(atual->sujeito, atual->nsujeito, atual->emissor, atual->nemissor))
                txt_juntar(erro, nerro, "autoridade desconhecida: o certificado de '", cn_ou_nada(atual->cn),
                           "' é autoassinado e não está entre os de confiança", 0, 0, 0);
            else if (nivel == 0)
                /* Nem chegamos a uma autoridade: falta um elo no meio. */
                txt_juntar(erro, nerro, "cadeia quebrada: o servidor não mandou o certificado de '",
                           cn_ou_nada(cn_emissor), "', que emitiu o de '", cn_ou_nada(atual->cn), "'", 0);
            else
                txt_juntar(erro, nerro, "autoridade desconhecida: '", cn_ou_nada(cn_emissor),
                           "' não está entre as raízes de confiança", 0, 0, 0);
        }
        return -1;
    }

confiavel:
    /* Por último, o nome: o certificado é bom, mas é deste servidor? */
    if (nome && !x509_nome_confere(&c[0], nome)) {
        txt_juntar(erro, nerro, "nome não confere: o certificado é de '", cn_ou_nada(c[0].cn),
                   "' e não vale para '", nome, "'", 0);
        return -1;
    }
    if (nerro) erro[0] = 0;
    return 0;
}
