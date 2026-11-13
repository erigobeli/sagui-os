/* ssh.h - cliente SSH 2 do SaguiOS.
 *
 * Escrito neste projeto a partir das especificações: RFC 4251 (arquitetura e
 * tipos de dados), 4252 (autenticação), 4253 (transporte), 4254 (conexão e
 * canais), 4344 (aes-ctr), 6668 (hmac-sha2-256), 8709 (chaves ssh-ed25519),
 * 8731 (curve25519-sha256), 8332 (rsa-sha2-256) e 4648 (base64). A
 * criptografia vem toda de ../cripto/cripto.h.
 *
 * Como a cripto e o TLS, esta biblioteca não depende de nada (nem libc, nem
 * núcleo) e não reserva memória: quem chama passa um "Ssh" (uns 53 KB, com
 * todos os buffers dentro; NÃO o ponha na pilha) e uma tabela de funções
 * para falar com o mundo (SshAmbiente). O mesmo código roda no teste, sobre
 * soquetes do Linux, e no SaguiOS, sobre o TCP do sistema.
 *
 * A ordem de uso:
 *
 *     ssh_conectar()            identificação, troca de chaves, chave do
 *                               servidor conferida por quem chama
 *     ssh_autenticar_nenhum()   pergunta os métodos que o servidor aceita
 *     ssh_autenticar_chave()    e/ou ssh_autenticar_senha()
 *     ssh_abrir_sessao()        canal, terminal e shell (ou um comando)
 *     ssh_ler() / ssh_escrever() / ssh_mudar_tamanho() / ssh_enviar_fim()
 *     ssh_fechar()
 *
 * Limitações (ver LEIA-ME.md): só cliente, um canal de sessão por conexão,
 * sem compressão, sem encaminhamento de portas nem de agente, chave de
 * usuário só Ed25519. */
#ifndef SSH_H
#define SSH_H

#include "../cripto/cripto.h"

/* O mundo lá fora, visto pelo SSH. "ctx" é devolvido em todas as chamadas. */
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
    /* Bytes aleatórios seguros (chave efêmera, enchimento dos pacotes). */
    void (*aleatorio)(void *ctx, u8 *dados, u32 n);
} SshAmbiente;

#define SSH_DIGITAL_MAX 52      /* "SHA256:" + 43 letras + zero, com folga */

/* Chamada uma vez, no meio do ssh_conectar, depois que o servidor PROVOU
 * (com a assinatura da troca de chaves) que é dono da chave. "tipo" é o tipo
 * da chave ("ssh-ed25519" ou "ssh-rsa"), "blob" é a chave pública no formato
 * do protocolo (a mesma coisa que, em base64, vai num arquivo de servidores
 * conhecidos) e "digital" é a impressão digital "SHA256:...", igual à que o
 * ssh-keygen -l mostra. Devolve 1 para aceitar, 0 para recusar. Pode
 * demorar (perguntar ao usuário). */
typedef int (*SshConfereChave)(void *ctx, const char *tipo, const u8 *blob, u32 nblob,
                               const char *digital);

typedef struct {
    SshConfereChave conferir_chave;     /* obrigatória */
    void       *ctx_chave;              /* devolvido à conferir_chave */
    u32         limite_ms;              /* espera máxima por uma resposta do servidor; 0 = 20 s */
    /* Listas separadas por vírgula, em ordem de preferência, para restringir
     * a negociação; 0 = tudo que sabemos. Chaves de servidor:
     * "ssh-ed25519,rsa-sha2-256". Cifras: "aes128-ctr,aes256-ctr". */
    const char *chaves_servidor;
    const char *cifras;
    /* Depois de quantos bytes cifrados (somando os dois sentidos) pedimos
     * nós mesmos chaves novas; 0 = 1 GB. Também pedimos depois de 1 hora. */
    u32         trocar_apos_bytes;
} SshOpcoes;

/* Valores devolvidos (além de contagens de bytes). */
#define SSH_OK         0
#define SSH_FECHADO   (-1)      /* o servidor fechou o canal direito (ver ssh_saida) */
#define SSH_ERRO      (-2)      /* erro; ver ssh_erro() */
#define SSH_CORTADO   (-3)      /* a conexão caiu sem aviso */
#define SSH_RECUSADO  (-4)      /* autenticação recusada; dá para tentar outro método */

#define SSH_PACOTE_MAX   35000                  /* maior pacote aceito (RFC 4253, 6.1) */
#define SSH_ENTRADA_MAX  (SSH_PACOTE_MAX + 32)  /* o pacote e o MAC */
#define SSH_DADOS_MAX    32768                  /* maior bloco de dados que aceitamos por pacote */
#define SSH_ENVIO_MAX    16384                  /* maior bloco de dados que mandamos por pacote */
#define SSH_SAIDA_MAX    (SSH_ENVIO_MAX + 512)
#define SSH_JANELA       (1024 * 1024)          /* quanto o servidor pode mandar antes de liberarmos mais */
#define SSH_ERRO_MAX     240
#define SSH_VERSAO_MAX   256
#define SSH_KEXINIT_MAX  320
#define SSH_METODOS_MAX  128
#define SSH_AVISO_MAX    512
#define SSH_ALGOS_MAX    112
#define SSH_SINAL_MAX    16
#define SSH_FILA_MAX     16

/* Um sentido da conversa (o que mandamos ou o que recebemos). */
typedef struct {
    int    ativa;               /* já há chave: os pacotes vão cifrados e com MAC */
    u32    sequencia;           /* número do próximo pacote; nunca volta a zero */
    u64    bytes;               /* bytes passados desde a última troca de chaves */
    AesCtr cifra;
    Hmac   mac;                 /* HMAC-SHA-256 já com a chave absorvida */
} SshSentido;

typedef struct {
    SshAmbiente amb;
    int   estado;               /* ver ssh_interno.h */
    int   codigo;               /* o que devolver depois que a conexão morreu */
    char  erro[SSH_ERRO_MAX];
    u32   limite_ms;
    u32   inicio_ms;            /* quando começou a espera atual */

    /* Opções copiadas do SshOpcoes. */
    SshConfereChave conferir_chave;
    void *ctx_chave;
    char  lista_chaves[48];
    char  lista_cifras[32];
    u32   trocar_apos_bytes;

    /* Transporte. */
    char  versao_servidor[SSH_VERSAO_MAX];
    SshSentido rx, tx;
    u8    sessao[32];           /* identificador da sessão: o H da primeira troca */
    int   tem_sessao;
    u8    resumo_chave[32];     /* SHA-256 da chave do servidor (tem de ser a mesma nas trocas seguintes) */
    u32   trocas;               /* quantas trocas de chaves já terminaram */
    int   kex_enviado;          /* mandamos KEXINIT e ainda não NEWKEYS: só pacotes da troca podem sair */
    u32   inicio_troca_ms;      /* quando terminou a última troca */
    u8    kexinit[SSH_KEXINIT_MAX];     /* o nosso KEXINIT: entra na conta do H */
    u32   nkexinit;
    char  algoritmos[SSH_ALGOS_MAX];

    /* Autenticação. */
    char  metodos[SSH_METODOS_MAX];
    char  aviso[SSH_AVISO_MAX];

    /* O canal da sessão. */
    u32   canal_remoto;         /* o número que o servidor deu ao canal */
    u32   janela_remota;        /* quanto ainda podemos mandar */
    u32   pacote_remoto;        /* maior bloco de dados que o servidor aceita */
    u32   janela_local;         /* quanto o servidor ainda pode mandar */
    u32   consumido;            /* já entregue a quem chama e ainda não liberado ao servidor */
    u8    abertura;             /* 0 esperando, 1 confirmada, 2 recusada */
    u8    resposta;             /* resposta a um pedido nosso: 0 esperando, 1 sim, 2 não */
    u8    tem_terminal;
    u8    fim_remoto;           /* o servidor mandou EOF */
    u8    fechado_remoto;       /* o servidor mandou CLOSE */
    u8    fim_enviado;
    u8    fechado_enviado;
    u8    fim_pendente;         /* coisas a mandar assim que puder (ver canal.c) */
    u8    tamanho_pendente;
    u8    tem_status;
    u32   colunas, linhas;
    u32   status;
    char  sinal[SSH_SINAL_MAX];
    u8    fila_tipo[SSH_FILA_MAX];      /* respostas que devemos ao servidor */
    u32   fila_canal[SSH_FILA_MAX];
    u32   nfila;

    /* O pacote que está chegando e, depois de conferido, a carga dele; os
     * dados do canal são entregues direto daqui, sem cópia intermediária. */
    u32   nentrada;
    u32   tam_pacote;
    int   tem_cabecalho;
    u32   ncarga;
    u32   dados_ini, dados_fim;
    u8    entrada[SSH_ENTRADA_MAX];
    u8    saida[SSH_SAIDA_MAX];
} Ssh;

/* ------------------------------------------------------------ a conexão */

/* Sobre uma conexão TCP já aberta: troca as identificações, negocia, faz a
 * troca de chaves, confere a assinatura do servidor, chama conferir_chave e
 * pede o serviço de autenticação. Devolve SSH_OK, SSH_ERRO ou SSH_CORTADO. */
int ssh_conectar(Ssh *s, const SshAmbiente *amb, const SshOpcoes *op);

/* Autenticação. Cada uma devolve SSH_OK (entrou), SSH_RECUSADO (não entrou,
 * mas a conexão continua: ssh_metodos() diz o que ainda dá para tentar),
 * SSH_ERRO ou SSH_CORTADO. A "nenhum" serve para descobrir os métodos (e
 * entra direto nos raros servidores que não pedem nada). */
int ssh_autenticar_nenhum(Ssh *s, const char *usuario);
int ssh_autenticar_chave(Ssh *s, const char *usuario, const u8 semente[32]);
int ssh_autenticar_senha(Ssh *s, const char *usuario, const char *senha);

/* Os métodos que o servidor disse que podem continuar, separados por
 * vírgula (ex.: "publickey,password"). Vazio antes da primeira recusa. */
const char *ssh_metodos(const Ssh *s);

/* O texto de aviso (banner) que o servidor mandou durante a autenticação,
 * já sem caracteres de controle; vazio se não mandou. */
const char *ssh_aviso(const Ssh *s);

/* Abre o canal da sessão. Se "term" não é 0, pede um terminal desse tipo
 * (ex.: "xterm-256color") com esse tamanho; se "comando" é 0, pede o shell
 * do usuário, senão manda executar o comando. */
int ssh_abrir_sessao(Ssh *s, const char *term, u32 colunas, u32 linhas, const char *comando);

/* Lê o que o programa remoto escreveu (saída e saída de erro misturadas).
 * Não espera: devolve a quantidade (> 0), 0 se não há nada por enquanto,
 * SSH_FECHADO quando o servidor fechou o canal (aí ssh_saida() diz como o
 * programa terminou), SSH_CORTADO ou SSH_ERRO. É também quem atende o resto
 * do protocolo (janelas, pedidos do servidor, troca de chaves): chame-a
 * sempre, mesmo sem esperar dados. */
int ssh_ler(Ssh *s, u8 *dados, u32 max);

/* Manda dados ao programa remoto. Devolve quantos bytes foram aceitos, que
 * pode ser MENOS que n e até 0 (a janela do servidor encheu ou há uma troca
 * de chaves em curso): nesse caso chame ssh_ler() e tente o resto depois. */
int ssh_escrever(Ssh *s, const u8 *dados, u32 n);

/* Avisa o servidor que a janela do terminal mudou de tamanho. */
int ssh_mudar_tamanho(Ssh *s, u32 colunas, u32 linhas);

/* Avisa que não vamos mandar mais nada (fim da entrada do programa remoto).
 * Continua dando para ler. */
int ssh_enviar_fim(Ssh *s);

/* Como o programa remoto terminou; só vale depois que ssh_ler devolveu
 * SSH_FECHADO. Devolve 1 se saiu com um código (em *status), 2 se morreu
 * por um sinal (nome em *sinal, ex.: "KILL"), 0 se o servidor não contou. */
int ssh_saida(const Ssh *s, u32 *status, const char **sinal);

/* Despede-se do servidor (SSH_MSG_DISCONNECT) e apaga as chaves. Não fecha
 * a conexão de baixo: isso é com quem chamou. */
void ssh_fechar(Ssh *s);

const char *ssh_erro(const Ssh *s);             /* explicação do último erro, em português */
const char *ssh_versao_servidor(const Ssh *s);  /* ex.: "SSH-2.0-OpenSSH_9.6" */
/* Algoritmos em uso: troca, chave do servidor, cifra de ida, cifra de volta
 * e MAC, separados por espaço. */
const char *ssh_algoritmos(const Ssh *s);
u32 ssh_trocas(const Ssh *s);                   /* trocas de chaves feitas até agora */

/* ------------------------------------------------------------ chaves e textos */

/* Base64 (RFC 4648). O codificar devolve o número de letras escritas (sem
 * contar o zero do fim) ou -1 se não coube; com "enchimento" 0 não põe os
 * "=". O decodificar aceita com ou sem "=", recusa qualquer outra letra e
 * devolve o número de bytes ou -1. */
int ssh_base64_codificar(const u8 *dados, u32 n, char *saida, u32 max, int enchimento);
int ssh_base64_decodificar(const char *texto, u32 n, u8 *saida, u32 max);

/* Impressão digital de uma chave pública no formato do protocolo:
 * "SHA256:" e o SHA-256 em base64 sem enchimento. */
void ssh_digital(const u8 *blob, u32 nblob, char saida[SSH_DIGITAL_MAX]);

#define SSH_BLOB_ED25519  51    /* tamanho da chave pública Ed25519 no formato do protocolo */

/* A chave pública (formato do protocolo, RFC 8709) da semente dada. Gerar um
 * par de chaves é só sortear 32 bytes de semente e chamar esta função. */
void ssh_chave_blob(const u8 semente[32], u8 blob[SSH_BLOB_ED25519]);

/* A linha de chave pública que se cola no authorized_keys do servidor:
 * "ssh-ed25519 <base64> <comentário>\n". Devolve o tamanho ou -1 se não
 * coube ou se o comentário tem quebra de linha. */
int ssh_chave_publica_linha(const u8 semente[32], const char *comentario, char *saida, u32 max);

/* Lê uma linha "tipo base64 [comentário]" (de chave pública ou de um arquivo
 * de servidores conhecidos sem o nome na frente) e devolve o tamanho do
 * blob, ou -1. O tipo escrito tem de ser o mesmo que está dentro do blob. */
int ssh_chave_publica_ler(const char *linha, u32 n, u8 *blob, u32 max);

#define SSH_ARQUIVO_CHAVE_MAX 512

/* O arquivo de chave privada do SaguiOS (formato nosso, descrito no
 * LEIA-ME.md; NÃO é cifrado). O escrever devolve o tamanho do texto ou -1;
 * o ler devolve 0 e a semente, ou -1 se o arquivo não é válido. */
int ssh_chave_privada_escrever(const u8 semente[32], const char *comentario, char *saida, u32 max);
int ssh_chave_privada_ler(const char *texto, u32 n, u8 semente[32]);

#endif
