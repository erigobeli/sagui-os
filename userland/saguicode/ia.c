/* ia.c - cliente da API de conversa do OpenRouter (H69).
 *
 * Um pedido é um POST para <servidor>/chat/completions com:
 *   {"model": "...", "stream": true, "usage": {"include": true},
 *    "messages": [...], "tools": [...]}
 * e o cabeçalho "Authorization: Bearer <chave>". Com "stream", a resposta
 * vem como uma sequência de eventos do tipo "server-sent events":
 *   data: {"choices":[{"delta":{"content":"Olá"}}]}
 *   data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"ca"}}]}}]}
 *   ...
 *   data: {"choices":[...],"usage":{"prompt_tokens":...,"cost":0.0012}}
 *   data: [DONE]
 * Linhas que começam com ':' são comentários (o OpenRouter manda
 * ": OPENROUTER PROCESSING" enquanto o modelo pensa). Cada evento é um JSON
 * pequeno; os pedaços de texto vão para a tela na hora, e os argumentos das
 * ferramentas são emendados até o fim.
 *
 * Cache de prompt: os modelos da Anthropic (e alguns outros) só guardam em
 * cache o que for marcado com "cache_control"; para eles, as mensagens com
 * guardar_cache vão no formato de partes ([{"type":"text","text":...,
 * "cache_control":{"type":"ephemeral"}}]). Os demais (OpenAI, Google, DeepSeek)
 * fazem cache sozinhos. A chave nunca vai para o log. */
#include "ia.h"

void ia_iniciar(ia_t *ia)
{
    memset(ia, 0, sizeof *ia);
    copia_texto(ia->servidor, "https://openrouter.ai/api/v1", sizeof ia->servidor);
}

void ia_liberar(ia_t *ia)
{
    for (int i = 0; i < ia->qtd; i++) { libera(ia->msgs[i].conteudo); libera(ia->msgs[i].chamadas); }
    libera(ia->msgs);
    ia->msgs = NULL;
    ia->qtd = ia->cap = 0;
}

static char *duplicar_texto(const char *s)
{
    if (!s) return NULL;
    size_t n = tam_texto(s);
    char *d = aloca(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

int ia_acrescentar(ia_t *ia, const char *papel, const char *conteudo, const char *chamadas, const char *id_chamada)
{
    if (ia->qtd == ia->cap) {
        int cap = ia->cap ? ia->cap * 2 : 16;
        ia_mensagem_t *n = realoca(ia->msgs, (size_t)cap * sizeof(ia_mensagem_t));
        if (!n) return ERRO_SEM_MEMORIA;
        ia->msgs = n;
        ia->cap = cap;
    }
    ia_mensagem_t *m = &ia->msgs[ia->qtd];
    memset(m, 0, sizeof *m);
    copia_texto(m->papel, papel, sizeof m->papel);
    m->conteudo = duplicar_texto(conteudo);
    m->chamadas = duplicar_texto(chamadas);
    if (id_chamada) copia_texto(m->id_chamada, id_chamada, sizeof m->id_chamada);
    return ia->qtd++;
}

void ia_apagar_mensagem(ia_t *ia, int i)
{
    if (i < 0 || i >= ia->qtd) return;
    libera(ia->msgs[i].conteudo);
    libera(ia->msgs[i].chamadas);
    memmove(&ia->msgs[i], &ia->msgs[i + 1], sizeof(ia_mensagem_t) * (size_t)(ia->qtd - i - 1));
    ia->qtd--;
}

static bool marca_cache(const ia_t *ia)
{
    return comeca_com(ia->modelo, "anthropic/") || comeca_com(ia->modelo, "google/gemini");
}

char *ia_montar_pedido(ia_t *ia, unsigned *tam)
{
    json_escritor_t e;
    je_iniciar(&e);
    je_objeto(&e);
    je_chave(&e, "model"); je_texto(&e, ia->modelo);
    je_chave(&e, "stream"); je_bool(&e, true);
    je_chave(&e, "usage"); je_objeto(&e); je_chave(&e, "include"); je_bool(&e, true); je_fim_objeto(&e);
    if (ia->max_tokens > 0) { je_chave(&e, "max_tokens"); je_int(&e, ia->max_tokens); }
    je_chave(&e, "messages");
    je_lista(&e);
    for (int i = 0; i < ia->qtd; i++) {
        const ia_mensagem_t *m = &ia->msgs[i];
        je_objeto(&e);
        je_chave(&e, "role"); je_texto(&e, m->papel);
        je_chave(&e, "content");
        if (!m->conteudo) je_nulo(&e);
        else if (m->guardar_cache && marca_cache(ia)) {
            je_lista(&e);
            je_objeto(&e);
            je_chave(&e, "type"); je_texto(&e, "text");
            je_chave(&e, "text"); je_texto(&e, m->conteudo);
            je_chave(&e, "cache_control"); je_objeto(&e); je_chave(&e, "type"); je_texto(&e, "ephemeral"); je_fim_objeto(&e);
            je_fim_objeto(&e);
            je_fim_lista(&e);
        } else je_texto(&e, m->conteudo);
        if (m->chamadas) { je_chave(&e, "tool_calls"); je_bruto(&e, m->chamadas, (unsigned)tam_texto(m->chamadas)); }
        if (m->id_chamada[0]) { je_chave(&e, "tool_call_id"); je_texto(&e, m->id_chamada); }
        je_fim_objeto(&e);
    }
    je_fim_lista(&e);
    if (ia->ferramentas) { je_chave(&e, "tools"); je_bruto(&e, ia->ferramentas, (unsigned)tam_texto(ia->ferramentas)); }
    je_fim_objeto(&e);
    if (e.erro) { je_liberar(&e); return NULL; }
    *tam = e.n;
    return e.d;
}

long ia_micro_de_texto(const char *s)
{
    /* Número decimal sem ponto flutuante: parte inteira e até 6 casas. Com
     * expoente (1.2e-05), ajusta as casas. */
    long inteiro = 0, fracao = 0;
    int casas = 0, expoente = 0;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    while (*s >= '0' && *s <= '9') inteiro = inteiro * 10 + (*s++ - '0');
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { if (casas < 12) { fracao = fracao * 10 + (*s - '0'); casas++; } s++; }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        bool eneg = *s == '-';
        if (*s == '-' || *s == '+') s++;
        while (*s >= '0' && *s <= '9') expoente = expoente * 10 + (*s++ - '0');
        if (eneg) expoente = -expoente;
    }
    /* valor = (inteiro.fracao) * 10^expoente; em milionésimos: * 10^6 */
    long micro = inteiro;
    int escala = 6 + expoente;           /* quantas vezes multiplicar o inteiro por 10 */
    for (int i = 0; i < escala; i++) micro *= 10;
    for (int i = 0; i > escala && micro; i--) micro /= 10;
    /* a fração: fracao / 10^casas * 10^escala */
    long f = fracao;
    int pot = escala - casas;
    for (int i = 0; i < pot; i++) f *= 10;
    for (int i = 0; i > pot && f; i--) f /= 10;
    micro += f;
    return neg ? -micro : micro;
}

/* ------------------------------------------------------------ leitura do stream */

typedef struct {
    ia_t           *ia;
    ia_resposta_t  *r;
    http_resposta_t *http;
    char           *linha;           /* linha em montagem */
    unsigned        tam, cap;
    char           *corpo_erro;      /* resposta de erro (sem stream) */
    unsigned        tam_erro;
    bool            acabou;
} leitura_t;

static bool emendar(char **d, unsigned *tam, unsigned *cap, const char *s, unsigned n)
{
    if (*tam + n + 1 > *cap) {
        unsigned c = *cap ? *cap : 256;
        while (c < *tam + n + 1) c *= 2;
        char *x = realoca(*d, c);
        if (!x) return false;
        *d = x;
        *cap = c;
    }
    memcpy(*d + *tam, s, n);
    *tam += n;
    (*d)[*tam] = 0;
    return true;
}

static void evento(leitura_t *l, const char *dados, unsigned n)
{
    if (n == 6 && compara_texto_n(dados, "[DONE]", 6) == 0) { l->acabou = true; return; }
    json_t *j = json_ler(dados, n, NULL, 0);
    if (!j) return;
    ia_resposta_t *r = l->r;
    json_t *erro = json_campo(j, "error");
    if (erro) {
        formatar_texto(l->ia->erro, sizeof l->ia->erro, "a API respondeu com erro: %s", json_texto_de(erro, "message"));
        l->acabou = true;
    }
    json_t *escolha = json_item(json_campo(j, "choices"), 0);
    json_t *delta = json_campo(escolha, "delta");
    if (!delta) delta = json_campo(escolha, "message");   /* servidores sem stream de verdade */
    json_t *texto = json_campo(delta, "content");
    if (texto && texto->tipo == JSON_TEXTO && texto->tam) {
        emendar(&r->texto, &r->tam, &r->cap, texto->texto, texto->tam);
        if (l->ia->ao_texto) l->ia->ao_texto(l->ia->ctx, texto->texto, texto->tam);
    }
    json_t *chamadas = json_campo(delta, "tool_calls");
    for (json_t *c = chamadas ? chamadas->filho : NULL; c; c = c->prox) {
        int i = json_int(json_campo(c, "index"), -1);
        if (i < 0) i = r->n_chamadas ? r->n_chamadas - 1 : 0;   /* sem índice: a última (ou a primeira) */
        if (json_campo(c, "id") && json_campo(c, "index") == NULL) i = r->n_chamadas;   /* sem índice e com id: uma nova */
        if (i >= IA_MAX_CHAMADAS) continue;
        if (i >= r->n_chamadas) r->n_chamadas = i + 1;
        ia_chamada_t *ch = &r->chamadas[i];
        const char *id = json_texto_de(c, "id");
        if (*id) copia_texto(ch->id, id, sizeof ch->id);
        json_t *f = json_campo(c, "function");
        const char *nome = json_texto_de(f, "name");
        if (*nome) copia_texto(ch->nome, nome, sizeof ch->nome);
        json_t *args = json_campo(f, "arguments");
        if (args && args->tipo == JSON_TEXTO) emendar(&ch->argumentos, &ch->tam_args, &ch->cap_args, args->texto, args->tam);
    }
    const char *fim = json_texto_de(escolha, "finish_reason");
    if (*fim) copia_texto(r->fim, fim, sizeof r->fim);
    json_t *uso = json_campo(j, "usage");
    if (uso) {
        r->tokens_entrada = json_int(json_campo(uso, "prompt_tokens"), r->tokens_entrada);
        r->tokens_saida = json_int(json_campo(uso, "completion_tokens"), r->tokens_saida);
        json_t *det = json_campo(uso, "prompt_tokens_details");
        r->tokens_cache = json_int(json_campo(det, "cached_tokens"), r->tokens_cache);
        json_t *custo = json_campo(uso, "cost");
        if (custo && custo->tipo == JSON_NUMERO) r->custo_micro = ia_micro_de_texto(custo->texto);
    }
    json_liberar(j);
}

static int receber(void *ctx, const char *dados, unsigned n)
{
    leitura_t *l = ctx;
    if (l->http->situacao != 200) {               /* erro: guarda o corpo inteiro (é um JSON) */
        unsigned c = l->tam_erro + n + 1;
        char *x = realoca(l->corpo_erro, c);
        if (!x) return -1;
        l->corpo_erro = x;
        memcpy(x + l->tam_erro, dados, n);
        l->tam_erro += n;
        x[l->tam_erro] = 0;
        return l->tam_erro > 65536 ? -1 : 0;
    }
    for (unsigned i = 0; i < n; i++) {
        char c = dados[i];
        if (c == '\r') continue;
        if (c != '\n') {
            if (!emendar(&l->linha, &l->tam, &l->cap, &c, 1)) return -1;
            continue;
        }
        if (l->tam >= 6 && compara_texto_n(l->linha, "data: ", 6) == 0) evento(l, l->linha + 6, l->tam - 6);
        else if (l->tam >= 5 && compara_texto_n(l->linha, "data:", 5) == 0) evento(l, l->linha + 5, l->tam - 5);
        else if (l->tam > 0 && l->linha[0] == '{') evento(l, l->linha, l->tam);   /* resposta sem stream */
        l->tam = 0;
        if (l->linha) l->linha[0] = 0;
    }
    return 0;
}

int ia_perguntar(ia_t *ia, ia_resposta_t *r)
{
    memset(r, 0, sizeof *r);
    ia->erro[0] = 0;
    unsigned tam;
    char *corpo = ia_montar_pedido(ia, &tam);
    if (!corpo) { copia_texto(ia->erro, "memória insuficiente para montar o pedido", sizeof ia->erro); return ERRO_SEM_MEMORIA; }
    char url[256];
    formatar_texto(url, sizeof url, "%s/chat/completions", ia->servidor);
    char extra[400];
    formatar_texto(extra, sizeof extra,
                   "Authorization: Bearer %s\r\nContent-Type: application/json\r\n"
                   "HTTP-Referer: https://github.com/erigobeli/sagui-os\r\nX-Title: Sagui Code\r\n", ia->chave);
    http_resposta_t resp;
    leitura_t l;
    memset(&l, 0, sizeof l);
    l.ia = ia;
    l.r = r;
    l.http = &resp;
    int ok = http_pedir("POST", url, extra, corpo, tam, receber, &l, &resp);
    memset(extra, 0, sizeof extra);                 /* a chave não fica na memória à toa */
    libera(corpo);
    if (l.tam) evento(&l, l.linha + (comeca_com(l.linha, "data: ") ? 6 : 0), l.tam - (comeca_com(l.linha, "data: ") ? 6 : 0));
    libera(l.linha);
    if (resp.situacao != 200 && resp.situacao != 0) {
        const char *msg = "";
        json_t *j = l.corpo_erro ? json_ler(l.corpo_erro, l.tam_erro, NULL, 0) : NULL;
        if (j) msg = json_texto_de(json_campo(j, "error"), "message");
        formatar_texto(ia->erro, sizeof ia->erro, "a API respondeu %d%s%s", resp.situacao, *msg ? ": " : "", msg);
        json_liberar(j);
        libera(l.corpo_erro);
        return ERRO_REDE;
    }
    libera(l.corpo_erro);
    if (ok < 0) {
        if (!ia->erro[0]) formatar_texto(ia->erro, sizeof ia->erro, "falha na conexão: %s", resp.erro);
        return ERRO_REDE;
    }
    if (ia->erro[0]) return ERRO_INVALIDO;
    return 0;
}

void ia_resposta_liberar(ia_resposta_t *r)
{
    libera(r->texto);
    for (int i = 0; i < IA_MAX_CHAMADAS; i++) libera(r->chamadas[i].argumentos);
    memset(r, 0, sizeof *r);
}
