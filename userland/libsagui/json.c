/* json.c - leitor e escritor de JSON (H69), a partir da RFC 8259.
 *
 * JSON é texto com seis tipos de valor: null, true/false, números, textos
 * entre aspas, listas [a, b] e objetos {"nome": valor}. O leitor anda pelo
 * texto uma vez, montando uma árvore de nós (cada nó com o primeiro filho e o
 * próximo irmão). Textos com escapes (\n, \", é, pares 😀)
 * viram UTF-8. A profundidade é limitada (128) para um documento malicioso
 * não estourar a pilha.
 *
 * O escritor guarda, para cada nível aberto, se já escreveu algum item: assim
 * sabe quando pôr a vírgula. */
#include "json.h"

/* ------------------------------------------------------------ leitura */

typedef struct {
    const char *d;
    unsigned n, pos;
    char *erro;
    int max_erro;
    bool falhou;
} leitor_json_t;

static void falhar(leitor_json_t *l, const char *motivo)
{
    if (l->falhou) return;
    l->falhou = true;
    if (l->erro) formatar_texto(l->erro, (size_t)l->max_erro, "%s (posição %u)", motivo, l->pos);
}

static void pular_espacos(leitor_json_t *l)
{
    while (l->pos < l->n && (l->d[l->pos] == ' ' || l->d[l->pos] == '\t' || l->d[l->pos] == '\n' || l->d[l->pos] == '\r')) l->pos++;
}

static json_t *novo(int tipo)
{
    json_t *j = aloca_zerado(sizeof(json_t));
    if (j) j->tipo = tipo;
    return j;
}

static int hex4(leitor_json_t *l)
{
    if (l->pos + 4 > l->n) return -1;
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = l->d[l->pos + (unsigned)i];
        int h = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
        if (h < 0) return -1;
        v = v * 16 + h;
    }
    l->pos += 4;
    return v;
}

static unsigned utf8(unsigned cp, char *s)
{
    if (cp < 0x80) { s[0] = (char)cp; return 1; }
    if (cp < 0x800) { s[0] = (char)(0xC0 | (cp >> 6)); s[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) {
        s[0] = (char)(0xE0 | (cp >> 12)); s[1] = (char)(0x80 | ((cp >> 6) & 63)); s[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    s[0] = (char)(0xF0 | (cp >> 18)); s[1] = (char)(0x80 | ((cp >> 12) & 63));
    s[2] = (char)(0x80 | ((cp >> 6) & 63)); s[3] = (char)(0x80 | (cp & 63));
    return 4;
}

/* Lê um texto entre aspas (l->pos no '"'). */
static char *ler_texto(leitor_json_t *l, unsigned *tam)
{
    l->pos++;
    /* O texto decodificado nunca é maior que o original. */
    unsigned ini = l->pos, fim = ini;
    while (fim < l->n && l->d[fim] != '"') { if (l->d[fim] == '\\') fim++; fim++; }
    if (fim >= l->n) { falhar(l, "texto sem as aspas do fim"); return NULL; }
    char *s = aloca(fim - ini + 1);
    if (!s) { falhar(l, "memória insuficiente"); return NULL; }
    unsigned k = 0;
    while (l->pos < l->n && l->d[l->pos] != '"') {
        unsigned char c = (unsigned char)l->d[l->pos++];
        if (c < 0x20) { libera(s); falhar(l, "caractere de controle dentro de um texto"); return NULL; }
        if (c != '\\') { s[k++] = (char)c; continue; }
        char e = l->d[l->pos++];
        switch (e) {
        case '"': s[k++] = '"'; break;
        case '\\': s[k++] = '\\'; break;
        case '/': s[k++] = '/'; break;
        case 'b': s[k++] = '\b'; break;
        case 'f': s[k++] = '\f'; break;
        case 'n': s[k++] = '\n'; break;
        case 'r': s[k++] = '\r'; break;
        case 't': s[k++] = '\t'; break;
        case 'u': {
            int cp = hex4(l);
            if (cp < 0) { libera(s); falhar(l, "\\u sem 4 dígitos hexadecimais"); return NULL; }
            if (cp >= 0xD800 && cp < 0xDC00 && l->pos + 6 <= l->n && l->d[l->pos] == '\\' && l->d[l->pos + 1] == 'u') {
                unsigned salvo = l->pos;
                l->pos += 2;
                int baixo = hex4(l);
                if (baixo >= 0xDC00 && baixo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (baixo - 0xDC00);
                else l->pos = salvo;
            }
            if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;      /* metade de par sozinha */
            k += utf8((unsigned)cp, s + k);
            break;
        }
        default: libera(s); falhar(l, "escape desconhecido num texto"); return NULL;
        }
    }
    l->pos++;
    s[k] = 0;
    *tam = k;
    return s;
}

static json_t *ler_valor(leitor_json_t *l, int profundidade);

static json_t *ler_numero(leitor_json_t *l)
{
    unsigned ini = l->pos;
    bool negativo = false;
    if (l->d[l->pos] == '-') { negativo = true; l->pos++; }
    if (l->pos >= l->n || l->d[l->pos] < '0' || l->d[l->pos] > '9') { falhar(l, "número inválido"); return NULL; }
    long long v = 0;
    while (l->pos < l->n && l->d[l->pos] >= '0' && l->d[l->pos] <= '9') {
        if (v < 0x7FFFFFFF) v = v * 10 + (l->d[l->pos] - '0');
        l->pos++;
    }
    if (l->pos < l->n && l->d[l->pos] == '.') {
        l->pos++;
        while (l->pos < l->n && l->d[l->pos] >= '0' && l->d[l->pos] <= '9') l->pos++;
    }
    if (l->pos < l->n && (l->d[l->pos] == 'e' || l->d[l->pos] == 'E')) {
        l->pos++;
        if (l->pos < l->n && (l->d[l->pos] == '+' || l->d[l->pos] == '-')) l->pos++;
        while (l->pos < l->n && l->d[l->pos] >= '0' && l->d[l->pos] <= '9') l->pos++;
    }
    json_t *j = novo(JSON_NUMERO);
    if (!j) { falhar(l, "memória insuficiente"); return NULL; }
    if (v > 0x7FFFFFFF) v = 0x7FFFFFFF;
    j->valor = negativo ? -(int)v : (int)v;
    j->tam = l->pos - ini;
    j->texto = aloca(j->tam + 1);
    if (j->texto) { memcpy(j->texto, l->d + ini, j->tam); j->texto[j->tam] = 0; }
    return j;
}

static bool palavra(leitor_json_t *l, const char *p)
{
    unsigned t = (unsigned)tam_texto(p);
    if (l->pos + t > l->n || compara_texto_n(l->d + l->pos, p, t) != 0) return false;
    l->pos += t;
    return true;
}

static json_t *ler_valor(leitor_json_t *l, int profundidade)
{
    if (profundidade > 128) { falhar(l, "aninhamento fundo demais"); return NULL; }
    pular_espacos(l);
    if (l->pos >= l->n) { falhar(l, "o texto acabou antes do valor"); return NULL; }
    char c = l->d[l->pos];
    if (c == '"') {
        json_t *j = novo(JSON_TEXTO);
        if (!j) { falhar(l, "memória insuficiente"); return NULL; }
        j->texto = ler_texto(l, &j->tam);
        if (!j->texto) { libera(j); return NULL; }
        return j;
    }
    if (c == '{' || c == '[') {
        bool objeto = c == '{';
        json_t *j = novo(objeto ? JSON_OBJETO : JSON_LISTA), *ultimo = NULL;
        if (!j) { falhar(l, "memória insuficiente"); return NULL; }
        l->pos++;
        pular_espacos(l);
        if (l->pos < l->n && l->d[l->pos] == (objeto ? '}' : ']')) { l->pos++; return j; }
        for (;;) {
            char *nome = NULL;
            unsigned tn;
            if (objeto) {
                pular_espacos(l);
                if (l->pos >= l->n || l->d[l->pos] != '"') { falhar(l, "esperava o nome de um campo entre aspas"); json_liberar(j); return NULL; }
                nome = ler_texto(l, &tn);
                if (!nome) { json_liberar(j); return NULL; }
                pular_espacos(l);
                if (l->pos >= l->n || l->d[l->pos] != ':') { libera(nome); falhar(l, "esperava ':' depois do nome"); json_liberar(j); return NULL; }
                l->pos++;
            }
            json_t *v = ler_valor(l, profundidade + 1);
            if (!v) { libera(nome); json_liberar(j); return NULL; }
            v->nome = nome;
            if (ultimo) ultimo->prox = v; else j->filho = v;
            ultimo = v;
            j->qtd++;
            pular_espacos(l);
            if (l->pos < l->n && l->d[l->pos] == ',') { l->pos++; continue; }
            if (l->pos < l->n && l->d[l->pos] == (objeto ? '}' : ']')) { l->pos++; return j; }
            falhar(l, objeto ? "esperava ',' ou '}'" : "esperava ',' ou ']'");
            json_liberar(j);
            return NULL;
        }
    }
    if (c == '-' || (c >= '0' && c <= '9')) return ler_numero(l);
    json_t *j = NULL;
    if (palavra(l, "true")) { j = novo(JSON_BOOL); if (j) j->valor = 1; }
    else if (palavra(l, "false")) j = novo(JSON_BOOL);
    else if (palavra(l, "null")) j = novo(JSON_NULO);
    else { falhar(l, "valor inválido"); return NULL; }
    if (!j) falhar(l, "memória insuficiente");
    return j;
}

json_t *json_ler(const char *texto, unsigned n, char *erro, int max_erro)
{
    leitor_json_t l = { texto, n, 0, erro, max_erro, false };
    if (erro && max_erro > 0) erro[0] = 0;
    json_t *j = ler_valor(&l, 0);
    if (!j) return NULL;
    pular_espacos(&l);
    if (l.pos != n) { falhar(&l, "sobrou texto depois do valor"); json_liberar(j); return NULL; }
    return j;
}

void json_liberar(json_t *j)
{
    while (j) {
        json_t *prox = j->prox;
        json_liberar(j->filho);
        libera(j->nome);
        libera(j->texto);
        libera(j);
        j = prox;
    }
}

json_t *json_campo(const json_t *obj, const char *nome)
{
    if (!obj || obj->tipo != JSON_OBJETO) return NULL;
    for (json_t *c = obj->filho; c; c = c->prox) if (c->nome && compara_texto(c->nome, nome) == 0) return c;
    return NULL;
}

json_t *json_item(const json_t *lista, int i)
{
    if (!lista || (lista->tipo != JSON_LISTA && lista->tipo != JSON_OBJETO)) return NULL;
    json_t *c = lista->filho;
    while (c && i-- > 0) c = c->prox;
    return c;
}

const char *json_texto(const json_t *j) { return j && j->tipo == JSON_TEXTO ? j->texto : ""; }
int json_int(const json_t *j, int padrao) { return j && (j->tipo == JSON_NUMERO || j->tipo == JSON_BOOL) ? j->valor : padrao; }
const char *json_texto_de(const json_t *obj, const char *nome) { return json_texto(json_campo(obj, nome)); }

/* ------------------------------------------------------------ escrita */

static void por(json_escritor_t *e, const char *s, unsigned n)
{
    if (e->erro) return;
    if (e->n + n + 1 > e->cap) {
        unsigned cap = e->cap ? e->cap : 1024;
        while (cap < e->n + n + 1) cap *= 2;
        char *d = realoca(e->d, cap);
        if (!d) { e->erro = true; return; }
        e->d = d;
        e->cap = cap;
    }
    memcpy(e->d + e->n, s, n);
    e->n += n;
    e->d[e->n] = 0;
}

/* Antes de cada valor: a vírgula, se não for o primeiro do nível. */
static void antes_valor(json_escritor_t *e)
{
    if (e->depois_chave) { e->depois_chave = false; return; }
    if (e->nivel > 0) {
        if (!e->primeiro[e->nivel]) por(e, ",", 1);
        e->primeiro[e->nivel] = false;
    }
}

void je_iniciar(json_escritor_t *e) { memset(e, 0, sizeof *e); }
void je_liberar(json_escritor_t *e) { libera(e->d); memset(e, 0, sizeof *e); }

static void abrir_nivel(json_escritor_t *e, const char *c)
{
    antes_valor(e);
    por(e, c, 1);
    if (e->nivel < 31) e->nivel++;
    e->primeiro[e->nivel] = true;
}

static void fechar_nivel(json_escritor_t *e, const char *c)
{
    por(e, c, 1);
    if (e->nivel > 0) e->nivel--;
}

void je_objeto(json_escritor_t *e) { abrir_nivel(e, "{"); }
void je_fim_objeto(json_escritor_t *e) { fechar_nivel(e, "}"); }
void je_lista(json_escritor_t *e) { abrir_nivel(e, "["); }
void je_fim_lista(json_escritor_t *e) { fechar_nivel(e, "]"); }

static void texto_escapado(json_escritor_t *e, const char *s, unsigned n, bool latin1)
{
    static const char hexa[] = "0123456789abcdef";
    por(e, "\"", 1);
    for (unsigned i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        char t[8];
        if (c == '"') por(e, "\\\"", 2);
        else if (c == '\\') por(e, "\\\\", 2);
        else if (c == '\n') por(e, "\\n", 2);
        else if (c == '\r') por(e, "\\r", 2);
        else if (c == '\t') por(e, "\\t", 2);
        else if (c < 0x20) { t[0] = '\\'; t[1] = 'u'; t[2] = '0'; t[3] = '0'; t[4] = hexa[c >> 4]; t[5] = hexa[c & 15]; por(e, t, 6); }
        else if (c >= 0x80 && latin1) { unsigned k = utf8(c, t); por(e, t, k); }
        else por(e, (const char *)&s[i], 1);
    }
    por(e, "\"", 1);
}

void je_chave(json_escritor_t *e, const char *nome)
{
    antes_valor(e);
    texto_escapado(e, nome, (unsigned)tam_texto(nome), false);
    por(e, ":", 1);
    e->depois_chave = true;
}

void je_texto(json_escritor_t *e, const char *s) { antes_valor(e); texto_escapado(e, s, (unsigned)tam_texto(s), false); }
void je_texto_n(json_escritor_t *e, const char *s, unsigned n) { antes_valor(e); texto_escapado(e, s, n, false); }
void je_texto_latin1(json_escritor_t *e, const char *s) { antes_valor(e); texto_escapado(e, s, (unsigned)tam_texto(s), true); }

void je_int(json_escritor_t *e, int v)
{
    char t[16];
    int n = formatar_texto(t, sizeof t, "%d", v);
    antes_valor(e);
    por(e, t, (unsigned)n);
}

void je_bool(json_escritor_t *e, bool v) { antes_valor(e); por(e, v ? "true" : "false", v ? 4 : 5); }
void je_nulo(json_escritor_t *e) { antes_valor(e); por(e, "null", 4); }
void je_bruto(json_escritor_t *e, const char *json, unsigned n) { antes_valor(e); por(e, json, n); }
