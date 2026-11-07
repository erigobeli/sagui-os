/* aac.h - decodificador AAC-LC (aac.c). */
#ifndef SAGUI_AAC_H
#define SAGUI_AAC_H

#define AAC_ERRO        (-1)
#define AAC_SEM_SUPORTE (-2)

typedef struct aac aac_t;
aac_t *aac_novo(const uint8_t *asc, int n);    /* AudioSpecificConfig do esds */
void   aac_fim(aac_t *a);
int    aac_taxa(aac_t *a);
/* Um quadro cru: grava 1024 amostras estéreo intercaladas (16 bits) e
 * devolve 1024, ou 0 se não havia áudio, ou um erro. */
int    aac_quadro(aac_t *a, const uint8_t *d, int n, int16_t *saida);
#endif
