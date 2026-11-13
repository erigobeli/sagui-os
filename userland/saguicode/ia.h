/* ia.h - cliente da API de conversa do OpenRouter (H69), no formato de
 * "chat completions": uma lista de mensagens (sistema, usuário, assistente,
 * ferramenta) vai num POST em JSON; a resposta volta aos pedaços ("stream",
 * eventos "data: {...}") com o texto e os pedidos de ferramenta. */
#ifndef SAGUI_IA_H
#define SAGUI_IA_H
#include "sagui.h"
#include "http.h"
#include "json.h"

#define IA_MAX_CHAMADAS 8

typedef struct {
    char  papel[12];          /* "system", "user", "assistant", "tool" */
    char *conteudo;           /* UTF-8 (pode ser NULL no assistente que só chamou ferramentas) */
    char *chamadas;           /* assistente: a lista "tool_calls" em JSON, como veio */
    char  id_chamada[64];     /* tool: a qual chamada responde */
    bool  guardar_cache;      /* marcar para o cache de prompt (modelos que pedem marcação) */
} ia_mensagem_t;

typedef struct {
    char  id[64];
    char  nome[48];
    char *argumentos;         /* JSON em texto */
    unsigned tam_args, cap_args;
} ia_chamada_t;

typedef struct {
    char        *texto;       /* UTF-8, tudo o que o modelo escreveu */
    unsigned     tam, cap;
    ia_chamada_t chamadas[IA_MAX_CHAMADAS];
    int          n_chamadas;
    char         fim[24];     /* finish_reason: "stop", "tool_calls", "length"... */
    int          tokens_entrada, tokens_saida, tokens_cache;
    long         custo_micro; /* custo em milionésimos de dólar, se o servidor informou */
} ia_resposta_t;

typedef struct {
    char servidor[160];       /* https://openrouter.ai/api/v1 */
    char chave[200];
    char modelo[96];
    const char *ferramentas;  /* a lista "tools" em JSON (NULL = sem ferramentas) */
    ia_mensagem_t *msgs;
    int  qtd, cap;
    int  max_tokens;          /* 0 = o padrão do modelo */
    /* Chamada a cada pedaço de texto que chega (para mostrar enquanto gera). */
    void (*ao_texto)(void *ctx, const char *utf8, unsigned n);
    void *ctx;
    char erro[256];
} ia_t;

void ia_iniciar(ia_t *ia);
void ia_liberar(ia_t *ia);
int  ia_acrescentar(ia_t *ia, const char *papel, const char *conteudo, const char *chamadas, const char *id_chamada);
void ia_apagar_mensagem(ia_t *ia, int i);
/* Manda a conversa e espera a resposta inteira. 0 = ok; <0 = erro em ia->erro.
 * A resposta do assistente NÃO é acrescentada sozinha (quem chama decide). */
int  ia_perguntar(ia_t *ia, ia_resposta_t *r);
void ia_resposta_liberar(ia_resposta_t *r);
/* Monta o JSON do pedido (exposto para os testes e para medir o tamanho). */
char *ia_montar_pedido(ia_t *ia, unsigned *tam);
long ia_micro_de_texto(const char *s);   /* "0.00123" -> 1230 */

#endif
