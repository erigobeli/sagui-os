/* tls_ponte.c - lugar do TLS até a H53: por enquanto, https:// avisa que
 * ainda não sabe. (Na H53 este arquivo liga o HTTP ao TLS de userland/tls.) */
#include "http.h"

tls_sessao_t *tls_conectar(int socket, const char *servidor, char *erro, int max_erro)
{
    (void)socket; (void)servidor;
    copia_texto(erro, "HTTPS ainda não (chega na H53)", (size_t)max_erro);
    return NULL;
}
int  tls_ler(tls_sessao_t *t, void *buf, unsigned max) { (void)t; (void)buf; (void)max; return ERRO_REDE; }
int  tls_escrever(tls_sessao_t *t, const void *dados, unsigned n) { (void)t; (void)dados; (void)n; return ERRO_REDE; }
void tls_fechar(tls_sessao_t *t) { (void)t; }
