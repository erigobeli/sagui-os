/* acumulador.h - rasterizador por área acumulada (derivado do font-rs,
 * Apache 2.0; veja acumulador.c, LICENSE e ORIGEM.md nesta pasta). */
#ifndef TERCEIROS_FONTRS_ACUMULADOR_H
#define TERCEIROS_FONTRS_ACUMULADOR_H
void fontrs_linha(float *acc, int w, int h, float x0, float y0, float x1, float y1);
void fontrs_linha_recortada(float *acc, int passo, int l, int a, double x0, double y0, double x1, double y1);
void fontrs_acumular(const float *acc, int w, int h, unsigned char *cob);
void fontrs_acumular_regra(const float *acc, int passo, int l, int a, int parimpar, unsigned char *m);
#endif
