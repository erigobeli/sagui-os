/* ac97.h - saída de som PCM. */
#ifndef SAGUI_AC97_H
#define SAGUI_AC97_H
#include "tipos.h"
bool    ac97_iniciar(void);
int32_t sis_pcm(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#endif
