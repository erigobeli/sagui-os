/* json.h - leitor e escritor de JSON (H69), para o Sagui Code conversar com a
 * API de IA. Os textos ficam em UTF-8, como vêm (a conversão para o Latin-1 da
 * tela é de quem mostra). Números: inteiros de 32 bits; os com fração ou
 * expoente guardam o texto e a parte inteira (o SaguiOS não usa ponto
 * flutuante). */
#ifndef SAGUI_JSON_H
#define SAGUI_JSON_H
#include "sagui.h"

#define JSON_NULO    0
#define JSON_BOOL    1
#define JSON_NUMERO  2
#define JSON_TEXTO   3
#define JSON_LISTA   4
#define JSON_OBJETO  5

typedef struct json {
    int          tipo;
    char        *nome;        /* dentro de um objeto: o nome do campo */
    char        *texto;       /* JSON_TEXTO (UTF-8, terminado em zero) ou o número como veio */
    unsigned     tam;         /* tamanho do texto (pode ter \0 no meio, vindo de \u0000) */
    int          valor;       /* JSON_NUMERO (parte inteira, saturada), JSON_BOOL (0/1) */
    int          qtd;         /* filhos (lista ou objeto) */
    struct json *filho, *prox;
} json_t;

/* Lê um documento. NULL = erro (motivo e posição em erro, se não for NULL). */
json_t     *json_ler(const char *texto, unsigned n, char *erro, int max_erro);
void        json_liberar(json_t *j);
json_t     *json_campo(const json_t *obj, const char *nome);     /* NULL se não houver */
json_t     *json_item(const json_t *lista, int i);
const char *json_texto(const json_t *j);                          /* "" se não for texto */
int         json_int(const json_t *j, int padrao);
const char *json_texto_de(const json_t *obj, const char *nome);   /* atalho: campo texto */

/* Escritor: monta o texto num buffer que cresce. As vírgulas e as aspas são
 * postas sozinhas. */
typedef struct {
    char    *d;
    unsigned n, cap;
    int      nivel;
    bool     primeiro[32];   /* em cada nível: ainda não escreveu nenhum item */
    bool     depois_chave;
    bool     erro;           /* faltou memória */
} json_escritor_t;

void je_iniciar(json_escritor_t *e);
void je_liberar(json_escritor_t *e);
void je_objeto(json_escritor_t *e);
void je_fim_objeto(json_escritor_t *e);
void je_lista(json_escritor_t *e);
void je_fim_lista(json_escritor_t *e);
void je_chave(json_escritor_t *e, const char *nome);
void je_texto(json_escritor_t *e, const char *utf8);
void je_texto_n(json_escritor_t *e, const char *utf8, unsigned n);
void je_texto_latin1(json_escritor_t *e, const char *latin1);    /* converte para UTF-8 */
void je_int(json_escritor_t *e, int v);
void je_bool(json_escritor_t *e, bool v);
void je_nulo(json_escritor_t *e);
void je_bruto(json_escritor_t *e, const char *json, unsigned n);  /* um valor já em JSON */

#endif
