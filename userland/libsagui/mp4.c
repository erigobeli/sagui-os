/* mp4.c - leitor de contêiner MP4 (ISO BMFF) para o tocador.
 *
 * Lê o "moov" e monta a tabela de amostras (posição, tamanho e tempo) da
 * primeira trilha de vídeo avc1 e da primeira de áudio mp4a. Só precisa dos
 * bytes até o fim do moov: o tocador baixa o resto enquanto toca. */
#ifdef H264_HOST
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define aloca malloc
#define libera free
#else
#include "sagui.h"
#endif
#include "mp4.h"

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static bool tipo_eh(const uint8_t *p, const char *t) { return p[0] == t[0] && p[1] == t[1] && p[2] == t[2] && p[3] == t[3]; }

typedef struct {
    const uint8_t *stsz, *stco, *stsc, *stts;
    bool co64;
    uint32_t escala;
    int tipo;            /* 1 vídeo, 2 áudio */
    const uint8_t *avcc; uint32_t avcc_n;
    const uint8_t *esds; uint32_t esds_n;
    int larg, alt, canais, taxa;
} trilha_t;

/* Procura a caixa "nome" dentro de [d, d+n). */
static const uint8_t *achar(const uint8_t *d, uint32_t n, const char *nome, uint32_t *tam)
{
    uint32_t pos = 0;
    while (pos + 8 <= n) {
        uint32_t t = be32(d + pos), cab = 8;
        if (t == 1) {   /* tamanho de 64 bits */
            if (pos + 16 > n || be32(d + pos + 8)) return NULL;
            t = be32(d + pos + 12);
            cab = 16;
        } else if (t == 0) t = n - pos;
        if (t < cab || t > n - pos) return NULL;
        if (tipo_eh(d + pos + 4, nome)) { *tam = t - cab; return d + pos + cab; }
        pos += t;
    }
    return NULL;
}

static void ler_stsd(const uint8_t *d, uint32_t n, trilha_t *t)
{
    if (n < 16) return;
    const uint8_t *e = d + 8;       /* versão, flags, contagem */
    uint32_t tam = be32(e);
    if (tam > n - 8 || tam < 16) return;
    if (tipo_eh(e + 4, "avc1") || tipo_eh(e + 4, "avc3")) {
        if (tam < 8 + 78) return;
        t->tipo = 1;
        t->larg = (int)be16(e + 8 + 24);
        t->alt = (int)be16(e + 8 + 26);
        uint32_t c;
        const uint8_t *a = achar(e + 8 + 78, tam - 8 - 78, "avcC", &c);
        if (a) { t->avcc = a; t->avcc_n = c; }
    } else if (tipo_eh(e + 4, "mp4a")) {
        if (tam < 8 + 28) return;
        t->tipo = 2;
        t->canais = (int)be16(e + 8 + 16);
        t->taxa = (int)be16(e + 8 + 24);
        uint32_t c;
        const uint8_t *a = achar(e + 8 + 28, tam - 8 - 28, "esds", &c);
        if (a) { t->esds = a; t->esds_n = c; }
    }
}

/* Descritores do esds: tag, tamanho (até 4 bytes de 7 bits). */
static const uint8_t *descritor(const uint8_t *d, const uint8_t *fim, int tag, uint32_t *n)
{
    while (d + 2 <= fim) {
        int t = *d++;
        uint32_t l = 0;
        for (int i = 0; i < 4 && d < fim; i++) {
            uint8_t b = *d++;
            l = (l << 7) | (b & 127);
            if (!(b & 128)) break;
        }
        if (t == tag) { *n = l; return d; }
        if (t == 3) { d += 3; continue; }           /* ES_Descriptor: entra */
        if (t == 4) { d += 13; continue; }          /* DecoderConfig: entra */
        d += l;
    }
    return NULL;
}

static bool montar_tabela(const trilha_t *t, mp4_amostra_t **saida, int *qtd)
{
    if (!t->stsz || !t->stco || !t->stsc || !t->stts) return false;
    uint32_t fixo = be32(t->stsz + 4), n = be32(t->stsz + 8);
    if (n == 0 || n > 4000000) return false;
    mp4_amostra_t *a = aloca(sizeof(mp4_amostra_t) * n);
    if (!a) return false;
    for (uint32_t i = 0; i < n; i++) a[i].tam = fixo ? fixo : be32(t->stsz + 12 + i * 4);
    /* tempos */
    uint32_t ent = be32(t->stts + 4), k = 0, tempo = 0;
    for (uint32_t e = 0; e < ent && k < n; e++) {
        uint32_t c = be32(t->stts + 8 + e * 8), dur = be32(t->stts + 12 + e * 8);
        for (uint32_t i = 0; i < c && k < n; i++) { a[k++].tempo = tempo; tempo += dur; }
    }
    while (k < n) a[k++].tempo = tempo;
    /* posições: stsc diz quantas amostras por bloco (chunk) */
    uint32_t nblocos = be32(t->stco + 4), nsc = be32(t->stsc + 4);
    k = 0;
    for (uint32_t s = 0; s < nsc; s++) {
        uint32_t primeiro = be32(t->stsc + 8 + s * 12) - 1, por = be32(t->stsc + 12 + s * 12);
        uint32_t ultimo = s + 1 < nsc ? be32(t->stsc + 8 + (s + 1) * 12) - 1 : nblocos;
        for (uint32_t b = primeiro; b < ultimo && b < nblocos; b++) {
            uint32_t pos = t->co64 ? be32(t->stco + 8 + b * 8 + 4) : be32(t->stco + 8 + b * 4);
            for (uint32_t i = 0; i < por && k < n; i++) { a[k].pos = pos; pos += a[k].tam; k++; }
        }
    }
    *saida = a;
    *qtd = (int)k;
    return k > 0;
}

int mp4_ler(const uint8_t *d, uint32_t n, mp4_t *m)
{
    memset(m, 0, sizeof *m);
    uint32_t tm;
    const uint8_t *moov = achar(d, n, "moov", &tm);
    if (!moov) {
        /* Ainda não chegou: diz até onde precisa baixar, se der. */
        uint32_t pos = 0;
        while (pos + 8 <= n) {
            uint32_t t = be32(d + pos);
            if (tipo_eh(d + pos + 4, "moov")) return (int)(pos + t);
            if (t < 8) break;
            pos += t;
        }
        return MP4_FALTA;
    }
    uint32_t pos = 0;
    while (pos + 8 <= tm) {
        uint32_t t = be32(moov + pos);
        if (t < 8 || t > tm - pos) break;
        if (tipo_eh(moov + pos + 4, "trak")) {
            trilha_t tr;
            memset(&tr, 0, sizeof tr);
            uint32_t a, b, c, e;
            const uint8_t *mdia = achar(moov + pos + 8, t - 8, "mdia", &a);
            const uint8_t *mdhd = mdia ? achar(mdia, a, "mdhd", &b) : NULL;
            if (mdhd) tr.escala = mdhd[0] == 1 ? be32(mdhd + 20) : be32(mdhd + 12);
            const uint8_t *minf = mdia ? achar(mdia, a, "minf", &b) : NULL;
            const uint8_t *stbl = minf ? achar(minf, b, "stbl", &c) : NULL;
            if (stbl) {
                const uint8_t *stsd = achar(stbl, c, "stsd", &e);
                if (stsd) ler_stsd(stsd, e, &tr);
                tr.stsz = achar(stbl, c, "stsz", &e);
                tr.stsc = achar(stbl, c, "stsc", &e);
                tr.stts = achar(stbl, c, "stts", &e);
                tr.stco = achar(stbl, c, "stco", &e);
                if (!tr.stco) { tr.stco = achar(stbl, c, "co64", &e); tr.co64 = true; }
            }
            if (tr.tipo == 1 && !m->video && tr.avcc) {
                if (montar_tabela(&tr, &m->video, &m->n_video)) {
                    m->escala_video = tr.escala;
                    m->larg = tr.larg; m->alt = tr.alt;
                    m->avcc = tr.avcc; m->avcc_n = tr.avcc_n;
                }
            } else if (tr.tipo == 2 && !m->audio && tr.esds) {
                if (montar_tabela(&tr, &m->audio, &m->n_audio)) {
                    m->escala_audio = tr.escala;
                    m->canais = tr.canais; m->taxa = tr.taxa;
                    uint32_t l;
                    const uint8_t *asc = descritor(tr.esds + 4, tr.esds + tr.esds_n, 5, &l);
                    if (asc) { m->asc = asc; m->asc_n = l; }
                }
            }
        }
        pos += t;
    }
    return (m->video || m->audio) ? 0 : MP4_SEM_TRILHA;
}

void mp4_fim(mp4_t *m)
{
    if (m->video) libera(m->video);
    if (m->audio) libera(m->audio);
    m->video = m->audio = NULL;
}
