/* gpu.c - camada de abstração de vídeo/GPU (stub software).
 *
 * Implementa a interface definida em gpu.h usando o backend VESA existente
 * (kernel/dispositivos/video.c). Quando um driver de GPU real (Intel, AMD)
 * for escrito, basta preencher a estrutura driver_video_t e chamar
 * video_registrar_driver(). Até lá, tudo roda por software na CPU.
 *
 * Sem libc, sem código de outros sistemas operacionais.
 */

#include "gpu.h"
#include "video.h"
#include "processo.h"
#include "relogio.h"

static driver_video_t *driver_atual = NULL;
static modo_video_t    modo_ativo;

/* ------------------------------------------------------------ backend software (VESA) */

/* Driver software: encaminha tudo para o módulo de vídeo VESA existente. */
static int gpu_sw_iniciar(void)
{
    if (!video_iniciar())
        return -1;
    modo_ativo.largura  = video_largura();
    modo_ativo.altura   = video_altura();
    modo_ativo.bpp      = 32;
    modo_ativo.pitch    = modo_ativo.largura * 4;
    modo_ativo.framebuffer = video_buffer();
    return 0;
}

static void gpu_sw_obter_modo(modo_video_t *m)
{
    *m = modo_ativo;
}

static void gpu_sw_limpar(uint32_t cor)
{
    uint32_t *fb = video_buffer();
    unsigned npixels = video_largura() * video_altura();
    for (unsigned i = 0; i < npixels; i++)
        fb[i] = cor;
    video_mostrar_tudo();
}

static void gpu_sw_retangulo(int x, int y, int l, int a, uint32_t cor)
{
    video_retangulo(x, y, l, a, (cor_t)cor);
}

static void gpu_sw_linha(int x0, int y0, int x1, int y1, uint32_t cor)
{
    video_linha(x0, y0, x1, y1, (cor_t)cor);
}

static void gpu_sw_vsync(void)
{
    /* VESA não tem controle de retraço vertical exposto; ignora. */
}

static driver_video_t gpu_driver_sw = {
    .iniciar   = gpu_sw_iniciar,
    .obter_modo = gpu_sw_obter_modo,
    .limpar     = gpu_sw_limpar,
    .retangulo  = gpu_sw_retangulo,
    .linha      = gpu_sw_linha,
    .vsync      = gpu_sw_vsync,
};

/* ------------------------------------------------------------ registro e despacho */

void video_registrar_driver(driver_video_t *drv)
{
    driver_atual = drv;
    if (drv->iniciar() == 0)
        drv->obter_modo(&modo_ativo);
    else
        driver_atual = NULL;
}

driver_video_t *video_driver_ativo(void)
{
    return driver_atual;
}

void video_obter_modo(modo_video_t *m)
{
    if (driver_atual)
        driver_atual->obter_modo(m);
    else
        *m = modo_ativo;
}

/* As funções de desenho (video_limpar, video_retangulo, video_linha, video_vsync)
 * já existem em dispositivos/video.c e são usadas diretamente pelos programas.
 * Esta camada so registra/despacha drivers. */

/* Inicialização da camada de vídeo: registra o driver software como padrão. */
bool video_gpu_iniciar(void)
{
    video_registrar_driver(&gpu_driver_sw);
    return driver_atual != NULL;
}
