/* ssh_interno.h - o que as partes da biblioteca SSH dividem entre si.
 *
 * As camadas, de baixo para cima:
 *   pacotes.c       pacote binário (tamanho, enchimento, cifra, MAC), leitura
 *                   e escrita dos campos, erros e textos
 *   troca.c         identificação, negociação, troca de chaves, derivação
 *   autenticacao.c  ssh-userauth
 *   canal.c         ssh-connection: o canal da sessão
 *   ssh.c           ssh_conectar, ssh_fechar e as consultas
 *   chaves.c        base64, impressão digital e arquivos de chave (não usa
 *                   o resto: serve também à ferramenta de chaves) */
#ifndef SSH_INTERNO_H
#define SSH_INTERNO_H

#include "ssh.h"

/* Estados da conexão. */
#define EST_NOVO         0
#define EST_TRANSPORTE   1      /* chaves trocadas e serviço de autenticação aceito */
#define EST_AUTENTICADO  2
#define EST_SESSAO       3      /* canal aberto */
#define EST_MORTO        4

/* Números das mensagens (RFC 4253, 4252 e 4254). */
#define MSG_DESCONECTAR        1
#define MSG_IGNORAR            2
#define MSG_NAO_IMPLEMENTADO   3
#define MSG_DEPURACAO          4
#define MSG_PEDIR_SERVICO      5
#define MSG_SERVICO_ACEITO     6
#define MSG_KEXINIT            20
#define MSG_NEWKEYS            21
#define MSG_ECDH_INICIO        30
#define MSG_ECDH_RESPOSTA      31
#define MSG_AUT_PEDIDO         50
#define MSG_AUT_FALHA          51
#define MSG_AUT_SUCESSO        52
#define MSG_AUT_AVISO          53
#define MSG_AUT_60             60      /* PK_OK ou PASSWD_CHANGEREQ, conforme o método */
#define MSG_PEDIDO_GLOBAL      80
#define MSG_GLOBAL_SUCESSO     81
#define MSG_GLOBAL_FALHA       82
#define MSG_CANAL_ABRIR        90
#define MSG_CANAL_CONFIRMADO   91
#define MSG_CANAL_RECUSADO     92
#define MSG_CANAL_JANELA       93
#define MSG_CANAL_DADOS        94
#define MSG_CANAL_DADOS_EXTRA  95
#define MSG_CANAL_FIM          96
#define MSG_CANAL_FECHAR       97
#define MSG_CANAL_PEDIDO       98
#define MSG_CANAL_SUCESSO      99
#define MSG_CANAL_FALHA        100

/* Motivos do SSH_MSG_DISCONNECT (RFC 4253, 11.1). */
#define MOTIVO_NENHUM          0       /* não mandar DISCONNECT */
#define MOTIVO_PROTOCOLO       2
#define MOTIVO_TROCA           3
#define MOTIVO_MAC             5
#define MOTIVO_VERSAO          8
#define MOTIVO_CHAVE           9
#define MOTIVO_APLICACAO       11

#define IDENTIFICACAO "SSH-2.0-SaguiOS_1.0"

/* ------------------------------------------------------------ pacotes.c */

/* Percorre os campos de um pacote. Nunca lê além do fim: se faltar byte,
 * marca "erro" e passa a devolver zeros, e quem chama confere uma vez só no
 * fim (ssh_i_ler_ok). */
typedef struct {
    const u8 *p;
    u32 n, pos;
    int erro;
} Leitor;

void ssh_i_ler_iniciar(Leitor *l, const u8 *p, u32 n);
u8   ssh_i_ler_byte(Leitor *l);
u32  ssh_i_ler_u32(Leitor *l);
void ssh_i_ler_cadeia(Leitor *l, const u8 **p, u32 *n);
int  ssh_i_ler_ok(const Leitor *l);         /* sem erro até aqui */
int  ssh_i_ler_fim(const Leitor *l);        /* sem erro e sem sobra */

/* Monta a carga de um pacote dentro do buffer de saída da conexão. */
typedef struct {
    u8 *p;
    u32 n, max;
    int erro;
} Escritor;

void ssh_i_esc_iniciar(Escritor *e, Ssh *s, u8 tipo);
void ssh_i_esc_byte(Escritor *e, u8 v);
void ssh_i_esc_u32(Escritor *e, u32 v);
void ssh_i_esc_cadeia(Escritor *e, const void *p, u32 n);
void ssh_i_esc_texto(Escritor *e, const char *t);

void ssh_i_por_u32(u8 *p, u32 v);
u32  ssh_i_pegar_u32(const u8 *p);

u32  ssh_i_tam(const char *t);
int  ssh_i_igual(const u8 *p, u32 n, const char *t);
void ssh_i_juntar(char *destino, u32 max, const char *texto);
/* Junta bytes vindos de fora, trocando o que não é texto visível por "?". */
void ssh_i_juntar_bytes(char *destino, u32 max, const u8 *p, u32 n, int linhas);
void ssh_i_juntar_numero(char *destino, u32 max, u32 v);

/* Mata a conexão com uma explicação (só a primeira fica) e, se "motivo" não
 * é MOTIVO_NENHUM, avisa o servidor com um DISCONNECT. Devolve SSH_ERRO. */
int  ssh_i_falhar(Ssh *s, u32 motivo, const char *texto);
int  ssh_i_falhar_com(Ssh *s, u32 motivo, const char *texto, const u8 *detalhe, u32 n);
int  ssh_i_caiu(Ssh *s);                    /* a conexão de baixo acabou: devolve SSH_CORTADO */
int  ssh_i_uso_errado(Ssh *s, const char *texto);   /* erro de quem chama: não mata a conexão */

void ssh_i_marcar_inicio(Ssh *s);           /* começa a contar o limite de tempo */
int  ssh_i_enviar_cru(Ssh *s, const u8 *p, u32 n);  /* bytes sem pacote (a identificação) */
int  ssh_i_enviar(Ssh *s, Escritor *e);     /* fecha o pacote, cifra e manda inteiro */

/* Junta o que chegou: 1 = há um pacote inteiro e conferido (carga em
 * ssh_i_carga, tamanho em s->ncarga), 0 = ainda não, negativo = erro. */
int  ssh_i_receber(Ssh *s);
int  ssh_i_receber_esperando(Ssh *s);       /* igual, mas espera até o limite */
#define ssh_i_carga(s) ((s)->entrada + 5)

/* Trata as mensagens que valem em qualquer hora (DISCONNECT, IGNORE, DEBUG,
 * UNIMPLEMENTED): 1 = era uma delas, 0 = não era, negativo = erro. */
int  ssh_i_basico(Ssh *s);
/* Próximo pacote das camadas de cima, já atendidas as mensagens do
 * transporte e as trocas de chaves que o servidor começar. Devolve o tipo
 * (> 0) ou um erro; o "se_houver" devolve 0 em vez de esperar. */
int  ssh_i_proximo(Ssh *s);
int  ssh_i_proximo_se_houver(Ssh *s);
int  ssh_i_nao_implementado(Ssh *s);        /* responde a um pacote que não entendemos */

/* ------------------------------------------------------------ troca.c */

int  ssh_i_identificar(Ssh *s);
int  ssh_i_enviar_kexinit(Ssh *s);
/* Faz a troca inteira; o KEXINIT do servidor está na entrada. */
int  ssh_i_trocar(Ssh *s);
int  ssh_i_lista_valida(const char *lista, const char *conhecidas);

/* ------------------------------------------------------------ canal.c */

int  ssh_i_canal(Ssh *s, int tipo);         /* trata um pacote da camada de conexão */
int  ssh_i_pendencias(Ssh *s);              /* manda o que ficou esperando vez */

#endif
