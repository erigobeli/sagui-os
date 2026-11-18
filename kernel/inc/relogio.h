/* relogio.h - tempo desde o boot (PIT) e data/hora (CMOS). */
#ifndef SAGUI_RELOGIO_H
#define SAGUI_RELOGIO_H
#include "tipos.h"

#define TIQUES_POR_SEGUNDO 100

void     relogio_iniciar(void);
uint32_t relogio_tiques(void);
uint32_t relogio_segundos(void);
void     relogio_dormir_ms(uint32_t ms);

/* Tempo medido pelo contador do PIT: funciona com as interrupções desligadas. */
typedef struct { uint16_t ult; uint32_t pulsos; } cronometro_t;
void     cronometro_iniciar(cronometro_t *c);
uint32_t cronometro_ms(cronometro_t *c);

typedef struct {
    uint16_t ano;
    uint8_t  mes, dia, hora, minuto, segundo;
} data_hora_t;

void cmos_ler_data(data_hora_t *d);
void cmos_gravar_data(const data_hora_t *d);   /* H38 */
int  dia_da_semana(int ano, int mes, int dia);   /* 0 = domingo */

#endif
