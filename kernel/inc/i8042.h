/* i8042.h - controlador do teclado e do touchpad. */
#ifndef SAGUI_I8042_H
#define SAGUI_I8042_H
#include "tipos.h"

typedef struct {
    int config_lida;       /* -1 = sem resposta */
    int resposta_padrao;   /* do mouse ao 0xF6 (0xFA = ok) */
    int resposta_reset;    /* ao 0xFF, se foi preciso */
    int resposta_ligar;    /* ao 0xF4 */
    int config_final;
    int id_mouse;          /* resposta ao 0xF2: 0 mouse comum, 3 com roda (H134) */
} i8042_diag_t;

bool i8042_iniciar(bool com_mouse);   /* devolve se o mouse respondeu */
void i8042_esvaziar(void);
void i8042_tique(void);               /* chamado pelo relógio */
const i8042_diag_t *i8042_diagnostico(void);
bool i8042_mouse_roda(void);          /* o mouse manda a roda (pacotes de 4 bytes) */

#endif
