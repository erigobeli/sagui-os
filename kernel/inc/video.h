/* video.h - modo gráfico VESA com buffer duplo (H17). */
#ifndef SAGUI_VIDEO_H
#define SAGUI_VIDEO_H
#include "tipos.h"

typedef uint32_t cor_t;   /* 0x00RRGGBB */
#define RGB(r, g, b) ((cor_t)(((r) << 16) | ((g) << 8) | (b)))

bool     video_iniciar(void);     /* false: sem modo gráfico, o sistema fica em texto */
bool     video_ativo(void);
uint32_t video_largura(void);
uint32_t video_altura(void);
uint32_t *video_buffer(void);     /* buffer de trás, 32 bits por pixel, largura * altura */

void video_pixel(int x, int y, cor_t c);
void video_retangulo(int x, int y, int l, int a, cor_t c);
void video_linha(int x0, int y0, int x1, int y1, cor_t c);
void video_moldura(int x, int y, int l, int a, int espessura, cor_t borda);
void video_caixa(int x, int y, int l, int a, cor_t fundo, cor_t borda);   /* retângulo com borda */
void video_degrade_vertical(int x, int y, int l, int a, cor_t topo, cor_t base);
void video_rolar(int y, int altura, int linhas, cor_t fundo);   /* sobe uma faixa da tela */

void video_marcar(int x, int y, int l, int a);   /* região alterada, a copiar na próxima vez */
void video_mostrar(void);                         /* copia o que mudou para a tela */
void video_mostrar_tudo(void);

/* H84: troca de resolução depois de ligado (driver da placa Intel). */
uint32_t *video_reservar(uint32_t l, uint32_t a);   /* buffer de trás novo, antes de mexer na placa */
void video_adotar(uint8_t *tela, uint32_t *tras, uint32_t l, uint32_t a, uint32_t pitch);
bool video_nativo_tentar(void);   /* video_nativo.c: resolução nativa do painel, se a configuração pedir */

/* Cursor do mouse: desenhado por cima, na hora de mostrar (H19). */
void video_cursor(int x, int y, bool visivel);

#endif
