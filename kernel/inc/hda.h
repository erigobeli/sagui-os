/* hda.h - som Intel High Definition Audio (H40) e o misturador (H41). */
#ifndef SAGUI_HDA_H
#define SAGUI_HDA_H
#include "tipos.h"

#define HDA_TAXA     48000                 /* amostras por segundo, por canal */
#define HDA_QUADRO   4                     /* bytes por quadro: 16 bits x 2 canais */
#define HDA_ANEL     (64 * 1024)           /* anel do DMA: ~340 ms */
#define HDA_PEDACOS  16

bool        hda_iniciar(void);
bool        hda_pronto(void);
void        hda_diagnostico(int fase);   /* estado do fluxo e dos pinos, no diário (G470) */
void        hda_reiniciar_fluxo(void);
void        hda_gpio(uint32_t mascara);   /* configuração som.gpio (G470) */   /* reset e RUN de novo, se a posição não andar */
const char *hda_descricao(void);
uint8_t    *hda_anel(void);
uint32_t    hda_posicao(void);             /* byte que o DMA está lendo agora */

/* misturador.c */
void misturador_iniciar(void);
void misturador_tique(void);               /* chamado pelo relógio a cada 10 ms */
bool misturador_notas(const void *notas, int n, bool enfileirar);   /* o bip pela placa de som */
void misturador_som_sistema(int qual);     /* SOM_INICIO, SOM_AVISO, SOM_PENDRIVE */
enum { SOM_INICIO, SOM_AVISO, SOM_PENDRIVE };
int32_t sis_som_abrir(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_som_enviar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_som_fechar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_som_volume(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#endif
