/* gpu.h - interface de abstração de vídeo/GPU.
 *
 * Define o contrato entre o kernel e drivers de placa de vídeo
 * (Intel, AMD, etc.). Hoje existem duas implementacoes:
 *   - driver software (VESA/framebuffer, em kernel/video/gpu.c)
 *   - driver stub Intel/AMD (kernel/video/intel_amd.c): detecta placas
 *     reais via PCI e, se nao conseguir inicializar, cai de volta para
 *     o software.
 *
 * A interface separa o generico do especifico de hardware para que um
 * driver real possa ser adicionado sem reescrever os apps.
 *
 * Sem libc, sem código de outros sistemas operacionais. */

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
    /* Aceleração 2D: copia um bloco de pixels (blit). */
    void (*blit)(int dx, int dy, int sx, int sy, int l, int a);
    /* Preenche um retângulo com aceleração de hardware (se suportado). */
    void (*preencher)(int x, int y, int l, int a, uint32_t cor);
} driver_video_t;

/* Registra um driver de vídeo. Chamado durante o boot. */
void video_registrar_driver(driver_video_t *drv);

/* Tenta registrar um driver sem afetar o driver_atual se falhar.
 * Devolve 0 se iniciou com sucesso, -1 caso contrário. */
int video_registrar_driver_tentativo(driver_video_t *drv);

/* Devolve o driver atualmente ativo (ou NULL se nenhum). */
driver_video_t *video_driver_ativo(void);

/* Devolve o modo de vídeo ativo. */
void video_obter_modo(modo_video_t *m);

/* Devolve true se o driver ativo é de hardware (Intel/AMD) e não software. */
bool video_driver_eh_hardware(void);

/* Inicializa a camada de vídeo: tenta driver Intel/AMD primeiro; se falhar,
 * usa o driver software VESA. Chamado do principal.c depois do console
 * gráfico estar ativo. */
bool video_gpu_iniciar(void);

/* Driver software VESA (definido em gpu.c), exposto para uso direto. */
extern driver_video_t gpu_driver_sw;

#endif
