/* gpu.h - interface de abstração de vídeo/GPU (stub).
 *
 * Define o contrato entre o kernel e eventuais drivers de placa de vídeo
 * (Intel, AMD, etc.). Hoje so existe a implementacao software (VESA/framebuffer),
 * mas a interface ja separa o que e generico do que e especifico de hardware,
 * para que um driver real possa ser adicionado no futuro sem reescrever tudo.
 *
 * Sem libc, sem código de outros sistemas operacionais.
 */

#ifndef KERNEL_VIDEO_GPU_H
#define KERNEL_VIDEO_GPU_H

#include "tipos.h"
#include "video.h"

/* Modo de vídeo: resolução, profundidade e endereço do framebuffer. */
typedef struct {
    unsigned largura;
    unsigned altura;
    unsigned bpp;          /* bits por pixel (24 ou 32) */
    unsigned pitch;        /* bytes por linha */
    uint32_t *framebuffer; /* endereço virtual do framebuffer linear */
} modo_video_t;

/* Operações de um driver de vídeo. */
typedef struct {
    /* Inicializa o driver. Devolve 0 se ok. */
    int  (*iniciar)(void);
    /* Devolve o modo de vídeo ativo. */
    void (*obter_modo)(modo_video_t *m);
    /* Limpa a tela com uma cor. */
    void (*limpar)(uint32_t cor);
    /* Desenha um retângulo preenchido. */
    void (*retangulo)(int x, int y, int l, int a, uint32_t cor);
    /* Desenha uma linha. */
    void (*linha)(int x0, int y0, int x1, int y1, uint32_t cor);
    /* Sincroniza com o retraço vertical (opcional). */
    void (*vsync)(void);
} driver_video_t;

/* Registra um driver de vídeo. Chamado durante o boot. */
void video_registrar_driver(driver_video_t *drv);

/* Devolve o driver atualmente ativo (ou NULL se nenhum). */
driver_video_t *video_driver_ativo(void);

/* Devolve o modo de vídeo ativo. */
void video_obter_modo(modo_video_t *m);

/* Inicializa a camada de vídeo: registra o driver software (VESA) como padrão.
 * Chamado do principal.c depois do console gráfico estar ativo. */
bool video_gpu_iniciar(void);

#endif
