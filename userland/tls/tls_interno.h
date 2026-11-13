/* tls_interno.h - o que os arquivos desta pasta dividem entre si e não é
 * para os programas usarem. */
#ifndef TLS_INTERNO_H
#define TLS_INTERNO_H
#include "tls.h"

/* Como na biblioteca de criptografia: o GCC não pode trocar os nossos laços
 * de copiar e zerar por chamadas a memcpy/memset, que não existem aqui. */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")

/* ------------------------------------------------------------ fatias
 *
 * Tudo que chega pela rede é lido através de uma "fatia": um ponteiro e
 * quantos bytes ainda restam. Cada leitura confere o tamanho antes e anda a
 * fatia; nenhum código do TLS ou dos certificados soma em ponteiro por conta
 * própria. É a nossa defesa contra tamanhos mentirosos. */
typedef struct {
    const u8 *p;
    u32       n;
} Fatia;

int fatia_ler8(Fatia *f, u32 *v);
int fatia_ler16(Fatia *f, u32 *v);
int fatia_ler24(Fatia *f, u32 *v);
/* Separa os próximos "n" bytes numa fatia nova. */
int fatia_cortar(Fatia *f, u32 n, Fatia *pedaco);
/* Separa um vetor do TLS: tamanho (1, 2 ou 3 bytes) seguido dos bytes. */
int fatia_vetor(Fatia *f, u32 bytes_tam, Fatia *pedaco);
int fatia_igual(Fatia f, const u8 *b, u32 nb);

/* ------------------------------------------------------------ texto e memória */

u32  txt_tamanho(const char *s);
/* Junta até seis pedaços de texto (0 = pedaço vazio) em "destino", cortando
 * se não couber e sempre terminando em zero. */
void txt_juntar(char *destino, u32 max, const char *a, const char *b, const char *c,
                const char *d, const char *e, const char *f);
/* Escreve um número pequeno em decimal; devolve "destino". */
char *txt_numero(char destino[12], u32 v);
/* Copia para trás no mesmo buffer (destino antes da origem). */
void mem_descer(u8 *destino, const u8 *origem, u32 n);

/* ------------------------------------------------------------ DER (der.c) */

#define DER_BOOLEANO    0x01
#define DER_INTEIRO     0x02
#define DER_BITS        0x03
#define DER_OCTETOS     0x04
#define DER_NULO        0x05
#define DER_OID         0x06
#define DER_UTCTIME     0x17
#define DER_GENTIME     0x18
#define DER_SEQUENCIA   0x30
#define DER_CONJUNTO    0x31

/* Lê um elemento (etiqueta, tamanho, conteúdo) e anda a fatia. "inteiro",
 * se não for 0, recebe o elemento com o cabeçalho. Devolve 0 ou -1. */
int der_proximo(Fatia *f, u8 *etiqueta, Fatia *conteudo, Fatia *inteiro);
/* Lê um elemento que tem de ter esta etiqueta. */
int der_esperar(Fatia *f, u8 etiqueta, Fatia *conteudo);
/* A etiqueta do próximo elemento, sem andar; -1 se a fatia acabou. */
int der_espiar(const Fatia *f);
/* INTEGER não negativo que cabe em 31 bits. */
int der_numero(Fatia *f, u32 *v);
/* INTEGER positivo de qualquer tamanho, sem o zero de sinal na frente. */
int der_inteiro_grande(Fatia *f, Fatia *valor);
int der_booleano(Fatia *f, int *v);
/* BIT STRING: devolve os bytes e quantos bits do último não valem. */
int der_bits(Fatia *f, Fatia *bytes, u32 *sobra);


/* ------------------------------------------------------------ TLS por dentro */

#define EST_NOVO     0
#define EST_APERTO   1      /* no meio do aperto de mão */
#define EST_ABERTO   2
#define EST_FECHADO  3      /* nós encerramos */
#define EST_ERRO     4

/* Tipos de registro (RFC 8446, 5.1). */
#define REG_TROCA_CIFRA  20     /* change_cipher_spec: só existe por compatibilidade */
#define REG_ALERTA       21
#define REG_APERTO       22
#define REG_DADOS        23

/* Alertas que mandamos (RFC 8446, 6). */
#define ALERTA_FIM                0     /* close_notify */
#define ALERTA_INESPERADO        10
#define ALERTA_MARCA_RUIM        20
#define ALERTA_REGISTRO_GRANDE   22
#define ALERTA_CERTIFICADO_RUIM  42
#define ALERTA_PARAMETRO         47
#define ALERTA_FORMATO           50
#define ALERTA_ASSINATURA        51
#define ALERTA_VERSAO            70
#define ALERTA_INTERNO           80
#define ALERTA_EXTENSAO         110

/* registros.c */
/* Anota o erro (só o primeiro vale), avisa o outro lado com um alerta fatal
 * (se "alerta" >= 0) e devolve TLS_ERRO, para se usar como "return". */
int  tls_falhar(Tls *t, int alerta, const char *a, const char *b, const char *c);
int  tls_registro_enviar(Tls *t, u8 tipo, const u8 *dados, u32 n);
/* Tenta completar e abrir um registro, sem esperar: 1 = há um registro em
 * t->entrada[reg_ini..reg_fim) do tipo reg_tipo; 0 = ainda não; < 0 = erro. */
int  tls_registro_tentar(Tls *t);
/* O mesmo, mas espera até o limite de tempo do aperto de mão. */
int  tls_registro_esperar(Tls *t);
/* Trata o alerta que está no registro atual: 0 = pode ignorar; senão
 * TLS_FECHADO ou TLS_ERRO. */
int  tls_alerta_recebido(Tls *t);
/* Junta o registro atual (de aperto de mão) ao buffer de mensagens. */
int  tls_msg_anexar(Tls *t);
/* Tira a próxima mensagem inteira do buffer: 1 = tirou; 0 = falta chegar. */
int  tls_msg_tirar(Tls *t, u32 *tipo, Fatia *corpo, Fatia *inteira);

/* chaves.c */
void tls_expandir_rotulo(int resumo, const u8 *segredo, const char *rotulo,
                         const u8 *contexto, u32 ncontexto, u8 *saida, u32 n);
/* Tira chave e IV de um segredo de tráfego e zera a contagem de registros. */
void tls_sentido_armar(Tls *t, TlsSentido *s, const u8 *segredo);
/* Troca o segredo de um sentido pelo seguinte (KeyUpdate). */
void tls_sentido_avancar(Tls *t, TlsSentido *s);

#endif
