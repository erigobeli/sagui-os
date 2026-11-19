/* http.h - cliente HTTP/1.1 (H49) e o canal TCP ou TLS por baixo dele. */
#ifndef SAGUI_HTTP_H
#define SAGUI_HTTP_H
#include "sagui.h"

#define MAX_URL_HTTP 1280

typedef struct {
    bool seguro;               /* https:// */
    char servidor[128];
    int  porta;
    char caminho[1024];   /* o CSS da Wikipédia passa de 400 (H129) */
} url_t;

/* O TLS (H53) fica atrás desta ponte; o HTTP não sabe como ele é por dentro. */
typedef struct tls_sessao tls_sessao_t;
tls_sessao_t *ponte_tls_conectar(int socket, const char *servidor, char *erro, int max_erro);
int  ponte_tls_ler(tls_sessao_t *t, void *buf, unsigned max);           /* 0 nada ainda, <0 fim ou erro */
int  ponte_tls_escrever(tls_sessao_t *t, const void *dados, unsigned n);
void ponte_tls_fechar(tls_sessao_t *t);

typedef struct {
    int           socket;
    tls_sessao_t *tls;
    char          buf[4096];
    unsigned      pos, tam;
} canal_t;

typedef struct {
    int      situacao;          /* 200, 404... */
    char     tipo[64];          /* Content-Type */
    char     url_final[MAX_URL_HTTP];   /* depois dos redirecionamentos */
    unsigned recebidos;
    char     erro[128];
} http_resposta_t;

/* Recebe cada pedaço do conteúdo; devolver <0 interrompe. */
typedef int (*http_receptor_t)(void *ctx, const char *dados, unsigned n);

int  url_separar(const char *url, url_t *u);
int  canal_abrir(canal_t *c, const url_t *u, char *erro, int max_erro);
void canal_fechar(canal_t *c);
int  http_obter(const char *url, http_receptor_t receptor, void *ctx, http_resposta_t *resp);
int  http_pedir(const char *metodo, const char *url, const char *extra_cabecalhos, const void *corpo, unsigned n_corpo,
                http_receptor_t receptor, void *ctx, http_resposta_t *resp);

/* Com os cabeçalhos de cada resposta (linha de situação primeiro). */
typedef void (*http_cabecalhos_t)(void *ctx, const char *url, const char *linha);
int  http_pedir_completo(const char *metodo, const char *url, const char *extra_cabecalhos, const void *corpo, unsigned n_corpo,
                         http_receptor_t receptor, void *ctx, http_resposta_t *resp, http_cabecalhos_t cabecalhos);

/* Guardar a conexão de um pedido para o próximo ao mesmo servidor (H129).
 * Desligado por padrão; false fecha as guardadas. */
void http_guardar_conexoes(bool sim);

/* Vários GET ao mesmo servidor numa conexão só (pipelining, H129). Usa a
 * conexão guardada, se houver. Devolve quantas respostas chegaram inteiras
 * (sempre as primeiras, em ordem); o resto o chamador pede uma a uma. */
typedef int  (*http_lote_receptor_t)(void *ctx, int i, const char *dados, unsigned n);
typedef void (*http_lote_fim_t)(void *ctx, int i, const http_resposta_t *resp);
int http_lote(const char *const *urls, const char *const *extras, int n,
              http_lote_receptor_t receptor, http_lote_fim_t fim, void *ctx);

#endif
