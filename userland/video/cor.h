/* cor.h - escala e conversão de cor de um quadro para a janela (cor.c, D071). */
#ifndef SAGUI_COR_H
#define SAGUI_COR_H
void cor_iniciar(void);
/* Desenha a imagem im no retângulo (ox, oy, ol, oa) de px (passo pontos por linha). */
void cor_desenhar(uint32_t *px, int passo, int ox, int oy, int ol, int oa, const h264_imagem_t *im);
#endif
