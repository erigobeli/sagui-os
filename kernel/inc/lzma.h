/* lzma.h - LZMA e filtro BCJ x86 (ramdisk "SGXZ"). */
#ifndef SAGUI_LZMA_H
#define SAGUI_LZMA_H
#include "tipos.h"
uint32_t lzma_expandir(const uint8_t *s, uint32_t n, uint8_t *d, uint32_t tam, uint8_t props);
void     bcj_x86_desfazer(uint8_t *b, uint32_t n);
#endif
