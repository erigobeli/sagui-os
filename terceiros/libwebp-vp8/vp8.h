/* vp8.h - WebP com perdas (derivado do libwebp, BSD; veja vp8.c, LICENSE,
 * PATENTS e ORIGEM.md nesta pasta). */
#ifndef TERCEIROS_LIBWEBP_VP8_H
#define TERCEIROS_LIBWEBP_VP8_H
/* Arquivo .webp inteiro (RIFF); pixels 0xAARRGGBB alocados com aloca. */
bool webp_ler(const uint8_t *d, unsigned n, uint32_t **saida, int *pl, int *pa);
#endif
