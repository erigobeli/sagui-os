/* der.c - fatias, texto e o leitor de DER (X.690).
 *
 * O DER é o jeito "de uma forma só" de escrever as estruturas ASN.1: cada
 * elemento é etiqueta, tamanho e conteúdo, e para cada valor só existe uma
 * escrita certa. O leitor daqui é pequeno e rígido de propósito: só aceita
 * tamanho definido, escrito do jeito mais curto, e etiquetas de um byte (as
 * únicas que os certificados usam). O que fugir disso é recusado, em vez de
 * "dar um jeito": certificado é dado de fora e é por leitores tolerantes que
 * entram as falsificações. */
#include "tls_interno.h"

/* ------------------------------------------------------------ fatias */

int fatia_ler8(Fatia *f, u32 *v)
{
    if (f->n < 1) return -1;
    *v = f->p[0];
    f->p += 1; f->n -= 1;
    return 0;
}

int fatia_ler16(Fatia *f, u32 *v)
{
    if (f->n < 2) return -1;
    *v = ((u32)f->p[0] << 8) | f->p[1];
    f->p += 2; f->n -= 2;
    return 0;
}

int fatia_ler24(Fatia *f, u32 *v)
{
    if (f->n < 3) return -1;
    *v = ((u32)f->p[0] << 16) | ((u32)f->p[1] << 8) | f->p[2];
    f->p += 3; f->n -= 3;
    return 0;
}

int fatia_cortar(Fatia *f, u32 n, Fatia *pedaco)
{
    if (f->n < n) return -1;
    pedaco->p = f->p;
    pedaco->n = n;
    f->p += n; f->n -= n;
    return 0;
}

int fatia_vetor(Fatia *f, u32 bytes_tam, Fatia *pedaco)
{
    u32 n;
    int r = bytes_tam == 1 ? fatia_ler8(f, &n) : bytes_tam == 2 ? fatia_ler16(f, &n) : fatia_ler24(f, &n);
    if (r) return -1;
    return fatia_cortar(f, n, pedaco);
}

int fatia_igual(Fatia f, const u8 *b, u32 nb)
{
    if (f.n != nb) return 0;
    for (u32 i = 0; i < nb; i++)
        if (f.p[i] != b[i]) return 0;
    return 1;
}

/* ------------------------------------------------------------ texto e memória */

u32 txt_tamanho(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

void txt_juntar(char *destino, u32 max, const char *a, const char *b, const char *c,
                const char *d, const char *e, const char *f)
{
    const char *pedacos[6] = { a, b, c, d, e, f };
    u32 pos = 0;
    if (max == 0) return;
    for (u32 i = 0; i < 6; i++) {
        const char *s = pedacos[i];
        if (!s) continue;
        while (*s && pos + 1 < max) destino[pos++] = *s++;
    }
    destino[pos] = 0;
}

char *txt_numero(char destino[12], u32 v)
{
    char ao_contrario[12];
    u32 n = 0, i = 0;
    do {
        ao_contrario[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) destino[i++] = ao_contrario[--n];
    destino[i] = 0;
    return destino;
}

void mem_descer(u8 *destino, const u8 *origem, u32 n)
{
    for (u32 i = 0; i < n; i++) destino[i] = origem[i];
}

/* ------------------------------------------------------------ DER */

int der_proximo(Fatia *f, u8 *etiqueta, Fatia *conteudo, Fatia *inteiro)
{
    const u8 *p = f->p;
    u32 resta = f->n, cab = 2, tam;

    if (resta < 2) return -1;
    /* Etiqueta com número 31 continua em mais bytes: não aparece em
     * certificado nenhum. */
    if ((p[0] & 0x1f) == 0x1f) return -1;

    if (p[1] < 0x80) {
        tam = p[1];                         /* forma curta */
    } else {
        /* Forma longa: o byte diz quantos bytes de tamanho vêm depois.
         * 0x80 seria "tamanho indefinido" (proibido no DER); mais de 4
         * não cabe em memória nenhuma nossa. */
        u32 k = p[1] & 0x7f;
        if (k == 0 || k > 4 || resta < 2 + k) return -1;
        if (p[2] == 0) return -1;           /* zero à esquerda: não é a forma mais curta */
        tam = 0;
        for (u32 i = 0; i < k; i++) tam = (tam << 8) | p[2 + i];
        if (tam < 0x80) return -1;          /* cabia na forma curta */
        cab = 2 + k;
    }
    if (tam > resta - cab) return -1;

    *etiqueta = p[0];
    conteudo->p = p + cab;
    conteudo->n = tam;
    if (inteiro) {
        inteiro->p = p;
        inteiro->n = cab + tam;
    }
    f->p += cab + tam;
    f->n -= cab + tam;
    return 0;
}

int der_esperar(Fatia *f, u8 etiqueta, Fatia *conteudo)
{
    u8 veio;
    Fatia copia = *f;
    if (der_proximo(&copia, &veio, conteudo, 0) || veio != etiqueta) return -1;
    *f = copia;
    return 0;
}

int der_espiar(const Fatia *f)
{
    return f->n ? f->p[0] : -1;
}

/* Um INTEGER é um número com sinal em complemento de dois, escrito com o
 * mínimo de bytes: o primeiro byte só pode ser 00 se o seguinte tiver o bit
 * de cima ligado (senão o zero estaria sobrando). */
static int inteiro_nao_negativo(Fatia *f, Fatia *v)
{
    if (der_esperar(f, DER_INTEIRO, v) || v->n == 0) return -1;
    if (v->p[0] & 0x80) return -1;                                  /* negativo */
    if (v->n > 1 && v->p[0] == 0 && !(v->p[1] & 0x80)) return -1;   /* zero sobrando */
    return 0;
}

int der_numero(Fatia *f, u32 *v)
{
    Fatia c, copia = *f;
    u32 x = 0;
    if (inteiro_nao_negativo(&copia, &c) || c.n > 4) return -1;
    for (u32 i = 0; i < c.n; i++) x = (x << 8) | c.p[i];
    *v = x;
    *f = copia;
    return 0;
}

int der_inteiro_grande(Fatia *f, Fatia *valor)
{
    Fatia c, copia = *f;
    if (inteiro_nao_negativo(&copia, &c)) return -1;
    if (c.p[0] == 0) {
        if (c.n == 1) return -1;            /* o número zero não nos serve */
        c.p++; c.n--;
    }
    *valor = c;
    *f = copia;
    return 0;
}

int der_booleano(Fatia *f, int *v)
{
    Fatia c, copia = *f;
    if (der_esperar(&copia, DER_BOOLEANO, &c) || c.n != 1) return -1;
    if (c.p[0] != 0x00 && c.p[0] != 0xff) return -1;    /* no DER, verdadeiro é só FF */
    *v = c.p[0] != 0;
    *f = copia;
    return 0;
}

int der_bits(Fatia *f, Fatia *bytes, u32 *sobra)
{
    Fatia c, copia = *f;
    if (der_esperar(&copia, DER_BITS, &c) || c.n < 1) return -1;
    /* O primeiro byte diz quantos bits do último byte são enchimento. */
    if (c.p[0] > 7 || (c.n == 1 && c.p[0] != 0)) return -1;
    *sobra = c.p[0];
    bytes->p = c.p + 1;
    bytes->n = c.n - 1;
    *f = copia;
    return 0;
}
