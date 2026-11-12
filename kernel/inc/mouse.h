/* mouse.h - mouse/touchpad PS/2 (H19). */
#ifndef SAGUI_MOUSE_H
#define SAGUI_MOUSE_H
#include "tipos.h"

#define BOTAO_ESQUERDO 1
#define BOTAO_DIREITO  2
#define BOTAO_MEIO     4

typedef struct {
    int16_t  x, y;          /* posição na tela depois do movimento */
    int16_t  dx, dy;
    uint8_t  botoes;        /* estado atual dos botões */
    uint8_t  mudaram;       /* botões que mudaram neste pacote */
    int8_t   roda;          /* degraus da roda, positivo = para baixo */
} evento_mouse_t;

void iniciar_mouse(evento_mouse_t *m);   /* menu Iniciar (iniciar.c) */
bool mouse_iniciar(void);
bool mouse_tentar(evento_mouse_t *e);   /* próximo evento, se houver */
void mouse_posicao(int *x, int *y);
const void *mouse_canal(void);

#endif
