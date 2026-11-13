/* tls.h - certificados X.509 e cliente TLS 1.3 do SaguiOS.
 *
 * Escrito neste projeto a partir das especificações: RFC 8446 (TLS 1.3),
 * RFC 5280 (certificados), X.690 (DER), RFC 8017/5480/5758/8410 (nomes dos
 * algoritmos), RFC 6125 (conferência do nome) e RFC 6066 (SNI). A
 * criptografia vem toda de ../cripto/cripto.h.
 *
 * Como a biblioteca de criptografia, esta não depende de nada (nem libc, nem
 * núcleo) e não reserva memória: quem chama passa um "Tls" (uns 72 KB, com
 * todos os buffers dentro; NÃO o ponha na pilha) e uma tabela de funções
 * para falar com o mundo (TlsAmbiente). Assim o mesmo código roda no teste,
 * sobre soquetes do Linux, e no SaguiOS, sobre o TCP do sistema.
 *
 * Limitações (ver LEIA-ME.md): só cliente, só TLS 1.3, só x25519, sem
 * retomada de sessão, sem 0-RTT, sem certificado de cliente e SEM consulta
 * de revogação (OCSP/CRL). */
#ifndef TLS_H
#define TLS_H

#include "../cripto/cripto.h"

/* ------------------------------------------------------------ certificados */

/* Um certificado (ou qualquer pedaço de DER) na memória de quem chama. */
typedef struct {
    const u8 *der;
    u32       n;
} X509Der;

/* Tipos de chave pública que sabemos usar. */
#define X509_CHAVE_DESCONHECIDA 0
#define X509_CHAVE_RSA          1
#define X509_CHAVE_P256         2
#define X509_CHAVE_P384         3
#define X509_CHAVE_ED25519      4

/* Uma chave pública já separada. Os ponteiros apontam para dentro do DER de
 * onde ela saiu. Na RSA, "a" é o módulo e "b" o expoente; nas outras, "a" é
 * o ponto (04||X||Y nas curvas do NIST, 32 bytes na Ed25519) e "b" não é
 * usado. */
typedef struct {
    int       tipo;
    const u8 *a;  u32 na;
    const u8 *b;  u32 nb;
} X509Chave;

/* Jeitos de assinar que sabemos conferir. */
#define X509_ASSIN_DESCONHECIDA 0
#define X509_ASSIN_RSA_PKCS1    1
#define X509_ASSIN_RSA_PSS      2
#define X509_ASSIN_ECDSA        3
#define X509_ASSIN_ED25519      4

/* Bits do keyUsage que nos interessam (primeiro byte da cadeia de bits). */
#define X509_USO_ASSINAR        0x80    /* digitalSignature */
#define X509_USO_ASSINAR_CERT   0x04    /* keyCertSign */

#define X509_CN_MAX 68       /* o padrão limita o nome comum a 64 letras */

/* Um certificado lido. Nada é copiado além do nome comum e das datas: o
 * resto são ponteiros para dentro do DER, que tem de continuar existindo. */
typedef struct {
    const u8 *der;        u32 nder;         /* o certificado inteiro */
    const u8 *tbs;        u32 ntbs;         /* a parte assinada, com o cabeçalho */
    const u8 *emissor;    u32 nemissor;     /* Name de quem assinou, em DER cru */
    const u8 *sujeito;    u32 nsujeito;     /* Name do dono, em DER cru */
    char      inicio[14];                   /* validade: AAAAMMDDhhmmss (UTC) */
    char      fim[14];
    char      cn[X509_CN_MAX];              /* nome comum do dono, para mensagens */
    const u8 *info_chave; u32 ninfo_chave;  /* o SubjectPublicKeyInfo, em DER cru */
    X509Chave chave;
    int       assin_tipo;                   /* X509_ASSIN_... */
    int       assin_resumo;                 /* RESUMO_... da cripto.h */
    const u8 *assin;      u32 nassin;       /* a assinatura */
    const u8 *nomes;      u32 nnomes;       /* conteúdo do subjectAltName */
    u8        tem_nomes;                    /* veio a extensão subjectAltName */
    u8        tem_restricoes;               /* veio basicConstraints */
    u8        autoridade;                   /* basicConstraints: cA */
    int       caminho_max;                  /* pathLenConstraint; -1 se não há */
    u8        tem_uso;                      /* veio keyUsage */
    u8        uso;                          /* primeiro byte do keyUsage */
    u8        tem_uso_estendido;            /* veio extKeyUsage */
    u8        serve_servidor;               /* extKeyUsage tem serverAuth (ou "qualquer") */
    u8        critica_desconhecida;         /* há extensão crítica que não entendemos */
} X509;

/* Lê um certificado. Devolve 0, ou -1 se o DER está malformado. Chave ou
 * algoritmo de assinatura que não conhecemos NÃO é erro aqui (fica marcado
 * como "desconhecido" e só atrapalha se for preciso usar). */
int x509_ler(X509 *c, const u8 *der, u32 n);

/* Lê só uma chave pública (um SubjectPublicKeyInfo em DER). */
int x509_ler_chave(X509Chave *k, const u8 *spki, u32 n);

/* Confere uma assinatura sobre "dados" com a chave "k". Devolve 0 se é
 * válida, -1 se não é, -2 se a combinação chave/algoritmo não é suportada. */
int x509_conferir(const X509Chave *k, int assin_tipo, int assin_resumo,
                  const u8 *dados, u32 n, const u8 *assin, u32 nassin);

/* O nome "nome" (ex.: "pt.wikipedia.org") é um dos nomes do certificado?
 * Devolve 1 ou 0. Vale a regra do curinga só no rótulo mais à esquerda. */
int x509_nome_confere(const X509 *c, const char *nome);

/* Monta a data no formato usado nas comparações (14 dígitos, sem zero no
 * fim). */
void x509_montar_data(char data[14], int ano, int mes, int dia, int hora, int min, int seg);

#define X509_CADEIA_MAX 10

/* Valida a cadeia que o servidor mandou (a folha primeiro) contra as raízes
 * de confiança, a data de agora e o nome do servidor. Devolve 0 se está tudo
 * certo; senão -1, com a explicação em "erro" (texto em português, terminado
 * em zero). "nome" pode ser 0 para não conferir o nome. */
int x509_validar_cadeia(const X509Der *cadeia, u32 ncadeia,
                        const X509Der *raizes, u32 nraizes,
                        const char *nome, const char agora[14],
                        char *erro, u32 nerro);

/* ------------------------------------------------------------ TLS */

/* O mundo lá fora, visto pelo TLS. "ctx" é devolvido em todas as chamadas. */
typedef struct {
    void *ctx;
    /* Manda bytes. Devolve quantos foram (pode ser menos que n, ou 0 se não
     * coube nada agora) ou um número negativo se a conexão caiu. */
    int  (*enviar)(void *ctx, const u8 *dados, u32 n);
    /* Recebe até "max" bytes SEM esperar: 0 = nada por enquanto; negativo =
     * a conexão acabou ou deu erro. */
    int  (*receber)(void *ctx, u8 *dados, u32 max);
    /* Dorme um pouco (usada quando "receber" ou "enviar" devolvem 0). */
    void (*esperar)(void *ctx, u32 ms);
    /* Um relógio em milissegundos, para os limites de tempo. Pode dar a
     * volta. */
    u32  (*agora_ms)(void *ctx);
    /* Bytes aleatórios seguros (chave efêmera, "random" do ClientHello). */
    void (*aleatorio)(void *ctx, u8 *dados, u32 n);
    /* Data e hora de agora, em UTC, para a validade dos certificados. */
    void (*data_atual)(void *ctx, int *ano, int *mes, int *dia, int *hora, int *min, int *seg);
} TlsAmbiente;

typedef struct {
    const char    *nome;        /* nome do servidor: vai no SNI e é conferido no certificado */
    const X509Der *raizes;      /* raízes de confiança (certificados em DER) */
    u32            nraizes;
    u32            limite_ms;   /* tempo máximo do aperto de mão e de um envio parado; 0 = 15 s */
    const char    *alpn;        /* protocolo a anunciar (ex.: "http/1.1"), ou 0 para nenhum */
} TlsOpcoes;

/* Valores devolvidos (além de contagens de bytes). */
#define TLS_OK        0
#define TLS_FECHADO  (-1)       /* o outro lado encerrou direito (close_notify) */
#define TLS_ERRO     (-2)       /* erro; ver tls_erro() */
#define TLS_CORTADO  (-3)       /* a conexão caiu sem o aviso de fim: os dados podem estar truncados */

#define TLS_DADOS_MAX     16384                     /* dados de um registro (RFC 8446, 5.1) */
#define TLS_ENTRADA_MAX   (5 + TLS_DADOS_MAX + 256) /* um registro cifrado inteiro */
#define TLS_SAIDA_MAX     (5 + TLS_DADOS_MAX + 1 + 16)
#define TLS_MENSAGEM_MAX  (36 * 1024)               /* a maior mensagem do aperto: a cadeia de certificados */
#define TLS_CHAVE_MAX     600                       /* SubjectPublicKeyInfo da folha (RSA de 4096 cabe) */
#define TLS_ERRO_MAX      200

/* Um sentido da conversa (o que mandamos ou o que recebemos). */
typedef struct {
    int    ativa;               /* já há chave: os registros vão cifrados */
    u8     segredo[48];         /* segredo de tráfego atual (para o KeyUpdate) */
    u8     chave[32];
    u8     iv[12];
    u64    sequencia;           /* número do próximo registro */
    AesGcm gcm;                 /* só nos conjuntos com AES */
} TlsSentido;

typedef struct {
    TlsAmbiente amb;
    int   estado;               /* ver tls_interno.h */
    int   fim_remoto;           /* o outro lado mandou close_notify */
    int   cortado;              /* a conexão caiu sem close_notify */
    char  erro[TLS_ERRO_MAX];
    u32   limite_ms;
    u32   inicio_ms;            /* quando o aperto de mão começou */

    u16   conjunto;             /* conjunto de cifras escolhido (0x1301..0x1303) */
    int   resumo;               /* RESUMO_SHA256 ou RESUMO_SHA384 */
    u32   tam_resumo;
    u32   tam_chave;
    TlsSentido rx, tx;
    Resumo transcrito[2];       /* SHA-256 e SHA-384 de tudo que foi dito no aperto */

    u8    chave_folha[TLS_CHAVE_MAX];   /* chave pública do servidor, para o CertificateVerify */
    u32   nchave_folha;

    /* O registro que está chegando (cru) e, depois de decifrado, os dados
     * de aplicação que ainda não foram entregues. */
    u8    entrada[TLS_ENTRADA_MAX];
    u32   nentrada;
    u8    reg_tipo;
    u32   reg_ini, reg_fim;
    u32   claro_ini, claro_fim;

    /* Mensagens do aperto de mão, que podem vir partidas em vários registros. */
    u8    msg[TLS_MENSAGEM_MAX];
    u32   msg_ini, msg_fim;

    u8    saida[TLS_SAIDA_MAX];
} Tls;

/* Faz o aperto de mão inteiro sobre uma conexão já aberta: negocia, valida o
 * certificado (raízes, nome, data) e deixa pronto para ler e escrever.
 * Devolve TLS_OK ou TLS_ERRO (a explicação fica em tls_erro()). */
int tls_conectar(Tls *t, const TlsAmbiente *amb, const TlsOpcoes *op);

/* Lê dados já decifrados. Não espera: devolve a quantidade (> 0), 0 se não
 * há nada por enquanto, TLS_FECHADO, TLS_CORTADO ou TLS_ERRO. */
int tls_ler(Tls *t, u8 *dados, u32 max);

/* Escreve dados (parte em registros e só volta depois de mandar tudo).
 * Devolve n ou TLS_ERRO. */
int tls_escrever(Tls *t, const u8 *dados, u32 n);

/* Avisa o outro lado que acabou (close_notify) e apaga as chaves. Não fecha
 * a conexão de baixo: isso é com quem chamou. */
void tls_fechar(Tls *t);

/* A explicação do último erro, em português. */
const char *tls_erro(const Tls *t);

/* Nome do conjunto de cifras em uso (ex.: "TLS_CHACHA20_POLY1305_SHA256"). */
const char *tls_cifra(const Tls *t);

#endif
