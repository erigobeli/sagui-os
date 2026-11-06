/* desenho.h - desenho com recorte sobre uma superfície de pixels (fase 5). */
#ifndef SAGUI_DESENHO_H
#define SAGUI_DESENHO_H
#include "tipos.h"
#include "arte.h"

typedef struct {
    uint32_t *px;
    int larg, alt;
    int cx0, cy0, cx1, cy1;   /* recorte: só [cx0,cx1) x [cy0,cy1) é pintado */
} superficie_t;

void sup_iniciar(superficie_t *s, uint32_t *px, int larg, int alt);
void sup_recorte(superficie_t *s, int x, int y, int l, int a);   /* l<=0: sem recorte */
void sup_retangulo(superficie_t *s, int x, int y, int l, int a, uint32_t cor);
void sup_moldura(superficie_t *s, int x, int y, int l, int a, uint32_t cor);
void sup_texto(superficie_t *s, int x, int y, const char *t, uint32_t cor);
void sup_desenho(superficie_t *s, int x, int y, const desenho_t *d, int escala);
void sup_copiar(superficie_t *s, int x, int y, const uint32_t *origem, int larg, int alt, int passo);
void sup_texto_forte(superficie_t *s, int x, int y, const char *t, uint32_t cor);
void sup_icone(superficie_t *s, int x, int y, const uint32_t *argb, int lado);
void sup_icone_escala(superficie_t *s, int x, int y, const uint32_t *argb, int lado, int destino);
void sup_escurecer(superficie_t *s, int x, int y, int l, int a, int alfa);   /* alfa de 0 a 256 */
void sup_degrade(superficie_t *s, int x, int y, int l, int a, uint32_t topo, uint32_t base);
uint32_t cor_mistura(uint32_t a, uint32_t b, int num, int den);
int  texto_largura(const char *t);

#endif
