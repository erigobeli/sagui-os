/* mp4.h - leitor de MP4 (mp4.c). */
#ifndef SAGUI_MP4_H
#define SAGUI_MP4_H

#define MP4_FALTA      (-1)   /* o moov ainda não chegou inteiro */
#define MP4_SEM_TRILHA (-2)

typedef struct {
    uint32_t pos, tam, tempo;   /* tempo na escala da trilha */
} mp4_amostra_t;

typedef struct {
    mp4_amostra_t *video, *audio;
    int n_video, n_audio;
    uint32_t escala_video, escala_audio;
    int larg, alt, canais, taxa;
    const uint8_t *avcc, *asc;   /* apontam para dentro do buffer lido */
    uint32_t avcc_n, asc_n;
} mp4_t;

/* 0 = ok; MP4_FALTA; ou > 0 = bytes necessários para ter o moov inteiro. */
int  mp4_ler(const uint8_t *d, uint32_t n, mp4_t *m);
void mp4_fim(mp4_t *m);
#endif
