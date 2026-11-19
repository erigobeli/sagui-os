/* memoria.c - aloca/libera para os programas.
 *
 * A área de dados do programa é uma faixa contínua que cresce com a chamada
 * aumentar_memoria. Dentro dela os blocos ficam um atrás do outro, cada um
 * com um cabeçalho de 8 bytes: o tamanho do bloco (o bit 0 diz se está
 * livre) e o tamanho do bloco de antes, para achar os dois vizinhos.
 *
 * Os blocos livres ficam em listas, uma por faixa de tamanho (de potência em
 * potência de 2), com os ponteiros guardados dentro do próprio bloco livre.
 * Achar um bloco olha só a lista certa, e não a área inteira, e libera junta
 * o bloco com os vizinhos livres na hora. Até a 3.0 a busca começava sempre
 * do início e passava por todos os blocos: numa página grande do Cipó, com
 * dezenas de milhares de blocos, cada aloca dava uma volta inteira (H129). */
#include "sagui.h"

#define ALINHAMENTO 8
#define CRESCER_MIN (16 * 1024)
#define MINIMO      16          /* cabeçalho + os dois ponteiros da lista */
#define LISTAS      32
#define CAB         8u

typedef struct bloco {
    uint32_t tam;               /* do bloco inteiro, cabeçalho incluído; bit 0: livre */
    uint32_t ant;               /* tamanho do bloco de antes (0 no primeiro) */
    struct bloco *prox_l, *ant_l;   /* só nos livres: a lista */
} bloco_t;

#define TAM(b)   ((b)->tam & ~7u)
#define LIVRE(b) ((b)->tam & 1u)

static uint8_t *inicio, *fim;
static bloco_t *ultimo;         /* o bloco que termina em fim */
static bloco_t *listas[LISTAS];
static uint32_t ocupadas;       /* bit i: a lista i tem algum bloco */

static bloco_t *depois(bloco_t *b) { uint8_t *p = (uint8_t *)b + TAM(b); return p < fim ? (bloco_t *)p : NULL; }
static bloco_t *antes(bloco_t *b) { return b->ant ? (bloco_t *)((uint8_t *)b - b->ant) : NULL; }

static int lista_de(uint32_t t) { return 31 - __builtin_clz(t); }

static void por(bloco_t *b)
{
    int i = lista_de(TAM(b));
    b->ant_l = NULL;
    b->prox_l = listas[i];
    if (listas[i]) listas[i]->ant_l = b;
    listas[i] = b;
    ocupadas |= 1u << i;
}

static void tirar(bloco_t *b)
{
    int i = lista_de(TAM(b));
    if (b->ant_l) b->ant_l->prox_l = b->prox_l;
    else listas[i] = b->prox_l;
    if (b->prox_l) b->prox_l->ant_l = b->ant_l;
    if (!listas[i]) ocupadas &= ~(1u << i);
}

/* Muda o tamanho de b e avisa o vizinho de depois. */
static void tamanho(bloco_t *b, uint32_t t, uint32_t livre)
{
    b->tam = t | livre;
    bloco_t *d = depois(b);
    if (d) d->ant = t;
    else ultimo = b;
}

/* Um bloco livre que ainda não está em lista nenhuma: junta com os vizinhos
 * livres e entra na lista. */
static void devolver(bloco_t *b)
{
    uint32_t t = TAM(b);
    bloco_t *d = depois(b);
    if (d && LIVRE(d)) { tirar(d); t += TAM(d); }
    bloco_t *a = antes(b);
    if (a && LIVRE(a)) { tirar(a); t += TAM(a); b = a; }
    tamanho(b, t, 1);
    por(b);
}

/* Separa os primeiros "precisa" bytes de b (já fora das listas) para uso; o
 * resto, se der um bloco, volta livre. */
static void usar(bloco_t *b, uint32_t t, uint32_t precisa)
{
    if (t - precisa >= MINIMO) {
        tamanho(b, precisa, 0);
        bloco_t *resto = (bloco_t *)((uint8_t *)b + precisa);
        resto->ant = precisa;
        tamanho(resto, t - precisa, 1);
        por(resto);   /* o vizinho de depois do resto não está livre: os livres já estão juntos */
    } else tamanho(b, t, 0);
}

static bool crescer(uint32_t pelo_menos)
{
    uint32_t n = pelo_menos < CRESCER_MIN ? CRESCER_MIN : (pelo_menos + 4095) & ~4095u;
    uint8_t *antigo = aumentar_memoria(n);
    if (!antigo) return false;
    if (!inicio) inicio = fim = antigo;
    if (antigo != fim) return false;   /* a área cresce sempre no fim dela */
    bloco_t *b = (bloco_t *)fim;
    b->ant = ultimo ? TAM(ultimo) : 0;
    fim += n;
    tamanho(b, n, 1);
    devolver(b);
    return true;
}

void *aloca(size_t n)
{
    if (n == 0) n = 1;
    if (n > 0x7FFFFF00u) return NULL;
    uint32_t precisa = (uint32_t)((n + CAB + ALINHAMENTO - 1) & ~(ALINHAMENTO - 1));
    if (precisa < MINIMO) precisa = MINIMO;
    for (int tentativa = 0; tentativa < 2; tentativa++) {
        int i = lista_de(precisa);
        bloco_t *b = NULL;
        for (bloco_t *c = listas[i]; c; c = c->prox_l)   /* na mesma faixa: o primeiro que couber */
            if (TAM(c) >= precisa) { b = c; break; }
        if (!b) {
            uint32_t maiores = i < 31 ? ocupadas & ~((2u << i) - 1) : 0;
            if (maiores) b = listas[__builtin_ctz(maiores)];   /* faixa maior: qualquer um cabe */
        }
        if (b) {
            tirar(b);
            usar(b, TAM(b), precisa);
            return (uint8_t *)b + CAB;
        }
        if (!crescer(precisa)) return NULL;
    }
    return NULL;
}

void *aloca_zerado(size_t n)
{
    void *p = aloca(n);
    if (p) memset(p, 0, n);
    return p;
}

void libera(void *p)
{
    if (!p) return;
    bloco_t *b = (bloco_t *)((uint8_t *)p - CAB);
    if (LIVRE(b)) return;   /* já estava livre */
    b->tam |= 1;
    devolver(b);
}

void *realoca(void *p, size_t n)
{
    if (!p) return aloca(n);
    bloco_t *b = (bloco_t *)((uint8_t *)p - CAB);
    size_t atual = TAM(b) - CAB;
    if (n <= atual) return p;
    if (n > 0x7FFFFF00u) return NULL;
    /* cresce no lugar, se o vizinho de depois está livre e basta */
    uint32_t precisa = (uint32_t)((n + CAB + ALINHAMENTO - 1) & ~(ALINHAMENTO - 1));
    bloco_t *d = depois(b);
    if (d && LIVRE(d) && TAM(b) + TAM(d) >= precisa) {
        tirar(d);
        usar(b, TAM(b) + TAM(d), precisa);
        return p;
    }
    void *novo = aloca(n);
    if (!novo) return NULL;
    memcpy(novo, p, atual);
    libera(p);
    return novo;
}
