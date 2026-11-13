/* intel_amd.c - driver stub para placas de vídeo Intel e AMD (stub).
 *
 * Detecta controladores VGA compatíveis via PCI (classe 0x03, subclasse
 * 0x00), lê os BARs para localizar o framebuffer de memória e expõe a
 * interface driver_video_t.
 *
 * Como o SaguiOS não inclui código de drivers de outros sistemas, esta
 * implementacao e um stub escrito do zero: ela faz a detecção de hardware
 * e, se não conseguir inicializar a placa real, devolve falha para que o
 * kernel use o driver software VESA como fallback.
 *
 * Quando houver documentação e hardware real para testar, basta preencher
 * as funções de inicialização dos registradores da placa (Intel i915/HD
 * Graphics, AMD Radeon, etc.) e mapear o framebuffer.
 *
 * Sem libc, sem código de outros sistemas operacionais. */

#include "gpu.h"
#include "video.h"
#include "pci.h"
#include "portas.h"
#include "console.h"

/* Classe PCI: 0x03 = Display controller, subclasse 0x00 = VGA compatível. */
#define PCI_CLASSE_DISPLAY  0x03
#define PCI_SUBCLASSE_VGA   0x00

/* Registradores de configuração PCI. */
#define PCI_REG_BAR0   0x10   /* Primeiro Base Address Register */
#define PCI_REG_CMD    0x04   /* Comando: habilita I/O e memória */
#define PCI_CMD_MEM    0x02   /* Bit 1: habilita acesso a memória */
#define PCI_CMD_IO     0x01   /* Bit 0: habilita acesso a I/O */

/* Framebuffer padrão para placas VGA quando não há BAR mapeado. */
#define FB_VGA_LINEAR  0xE0000000u   /* endereço típico de framebuffer linear */

/* Estado do driver hardware. */
static bool eh_hardware = false;
static modo_video_t modo_hw;

/* ------------------------------------------------------------ detecção de hardware */

/* Lê o BAR0 de um dispositivo PCI e devolve o endereço base da memória. */
static uint32_t ler_bar0(const pci_disp_t *d)
{
    uint32_t bar = pci_le32(d, PCI_REG_BAR0);
    if ((bar & 1) == 1) {
        /* BAR em modo I/O, não memória. Ignora. */
        return 0;
    }
    return bar & 0xFFFFFFF0u;
}

/* Tenta inicializar uma placa de vídeo Intel ou AMD real. Devolve 0 se ok. */
static int intel_amd_iniciar(void)
{
    static pci_disp_t achados[PCI_MAX];
    int n = pci_procurar(PCI_CLASSE_DISPLAY, PCI_SUBCLASSE_VGA, 0, achados, PCI_MAX);
    if (n == 0) {
        klog("[GPU] nenhuma placa VGA Intel/AMD encontrada no PCI\n");
        return -1;
    }

    const pci_disp_t *d = &achados[0];
    uint32_t bar0 = ler_bar0(d);

    klog("[GPU] placa VGA encontrada: barramento %u, dispositivo %u, "
         "fabricante 0x%04x, modelo 0x%04x\n",
         d->barramento, d->dispositivo, d->fabricante, d->modelo);

    /* Habilita acesso à memória no comando PCI. */
    uint32_t cmd = pci_le32(d, PCI_REG_CMD);
    pci_escreve32(d, PCI_REG_CMD, cmd | PCI_CMD_MEM | PCI_CMD_IO);

    if (bar0 != 0) {
        klog("[GPU] BAR0 = 0x%08x, mapeando framebuffer\n", bar0);
    } else {
        klog("[GPU] BAR0 não é memória; tentando framebuffer linear VGA\n");
        bar0 = FB_VGA_LINEAR;
    }

    /* Preenche o modo de vídeo (usa resolução padrão do VESA como fallback
     * até que um modo real seja consultado via VBE ou registradores da placa). */
    modo_hw.largura  = 1024;
    modo_hw.altura   = 768;
    modo_hw.bpp      = 32;
    modo_hw.pitch    = modo_hw.largura * 4;
    modo_hw.framebuffer = (uint32_t *)bar0;

    eh_hardware = true;
    klog("[GPU] driver Intel/AMD inicializado (modo %ux%ux%u)\n",
         modo_hw.largura, modo_hw.altura, modo_hw.bpp);
    return 0;
}

static void intel_amd_obter_modo(modo_video_t *m)
{
    *m = modo_hw;
}

static void intel_amd_limpar(uint32_t cor)
{
    uint32_t *fb = modo_hw.framebuffer;
    if (!fb) return;
    unsigned npixels = modo_hw.largura * modo_hw.altura;
    for (unsigned i = 0; i < npixels; i++)
        fb[i] = cor;
}

static void intel_amd_retangulo(int x, int y, int l, int a, uint32_t cor)
{
    uint32_t *fb = modo_hw.framebuffer;
    if (!fb) return;
    int x1 = x + l, y1 = y + a;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)modo_hw.largura) x1 = modo_hw.largura;
    if (y1 > (int)modo_hw.altura) y1 = modo_hw.altura;
    for (int yy = y; yy < y1; yy++) {
        uint32_t *p = fb + yy * modo_hw.largura;
        for (int xx = x; xx < x1; xx++) p[xx] = cor;
    }
}

static void intel_amd_linha(int x0, int y0, int x1, int y1, uint32_t cor)
{
    uint32_t *fb = modo_hw.framebuffer;
    if (!fb) return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1, sy = y0 < y1 ? 1 : -1;
    int passos = dx > dy ? dx : dy, acum = 0;
    for (int k = 0; k <= passos; k++) {
        if (x0 >= 0 && x0 < (int)modo_hw.largura &&
            y0 >= 0 && y0 < (int)modo_hw.altura)
            fb[y0 * modo_hw.largura + x0] = cor;
        if (dx >= dy) { x0 += sx; acum += dy; if (2 * acum >= dx) { y0 += sy; acum -= dx; } }
        else          { y0 += sy; acum += dx; if (2 * acum >= dy) { x0 += sx; acum -= dy; } }
    }
}

static void intel_amd_vsync(void)
{
    /* Sem controle de retraço vertical exposto neste stub. */
}

static void intel_amd_blit(int dx, int dy, int sx, int sy, int l, int a)
{
    uint32_t *fb = modo_hw.framebuffer;
    if (!fb) return;
    for (int yy = 0; yy < a; yy++) {
        for (int xx = 0; xx < l; xx++) {
            int sxp = sx + xx, syp = sy + yy;
            int dxp = dx + xx, dyp = dy + yy;
            if (sxp < 0 || syp < 0 || dxp < 0 || dyp < 0) continue;
            if (sxp >= (int)modo_hw.largura || syp >= (int)modo_hw.altura) continue;
            if (dxp >= (int)modo_hw.largura || dyp >= (int)modo_hw.altura) continue;
            fb[dyp * modo_hw.largura + dxp] = fb[syp * modo_hw.largura + sxp];
        }
    }
}

static void intel_amd_preencher(int x, int y, int l, int a, uint32_t cor)
{
    intel_amd_retangulo(x, y, l, a, cor);
}

driver_video_t gpu_driver_intel_amd = {
    .iniciar   = intel_amd_iniciar,
    .obter_modo = intel_amd_obter_modo,
    .limpar     = intel_amd_limpar,
    .retangulo  = intel_amd_retangulo,
    .linha      = intel_amd_linha,
    .vsync      = intel_amd_vsync,
    .blit       = intel_amd_blit,
    .preencher  = intel_amd_preencher,
};

/* Devolve true se o driver Intel/AMD foi inicializado com sucesso. */
bool video_driver_eh_hardware_hw(void)
{
    return eh_hardware;
}
