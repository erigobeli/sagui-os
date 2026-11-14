/* video.c - framebuffer VESA com buffer duplo.
 *
 * O estágio 2 escolheu e ligou o modo (1024x768, 800x600 ou 640x480, com 32
 * ou 24 bits) e deixou na boot_info o endereço físico do framebuffer linear,
 * a largura de cada linha em bytes (pitch) e a posição de cada cor no pixel.
 *
 * Tudo é desenhado num buffer na RAM ("buffer de trás"), sempre com 32 bits
 * por pixel no formato 0x00RRGGBB, e só depois copiado para a placa. Assim:
 *   - quem desenha não vê a imagem pela metade (sem tremido);
 *   - o resto do kernel não precisa saber se a placa usa 24 ou 32 bits nem a
 *     ordem das cores: a conversão acontece só na cópia;
 *   - ler um pixel (para rolar o texto, por exemplo) é ler RAM, e não a
 *     memória da placa, que é muito lenta para leitura.
 * A cópia é feita só do retângulo que mudou desde a última vez. */
#include "video.h"
#include "bootinfo.h"
#include "console.h"
#include "memoria.h"
#include "paginacao.h"
#include "texto.h"
#include "cpu.h"

static bool ativo;
static uint32_t larg, alt, pitch, bpp;
static uint8_t *tela;          /* framebuffer da placa (janela do kernel) */
static uint32_t *tras;         /* buffer de trás */
static uint8_t pos_r, pos_g, pos_b;
static bool formato_direto;    /* 32 bits com R em 16, G em 8, B em 0: copia sem converter */
static int suj_x0, suj_y0, suj_x1, suj_y1;   /* retângulo alterado; vazio se x0 >= x1 */

/* Cursor do mouse (H19): seta 12x19, '#' contorno, '.' miolo. Desenhado aqui. */
static int cur_x, cur_y, cur_ant_x, cur_ant_y;
static bool cur_visivel, cur_ant_visivel;
#define CUR_L 12
#define CUR_A 19
static const char *const seta[CUR_A] = {
    "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ",
    "#....#      ", "#.....#     ", "#......#    ", "#.......#   ", "#........#  ",
    "#.........# ", "#......#####", "#...#..#    ", "#..# #..#   ", "#.#  #..#   ",
    "##    #..#  ", "#     #..#  ", "       ##   ", "            ",
};

bool video_ativo(void)      { return ativo; }
uint32_t video_largura(void) { return larg; }
uint32_t video_altura(void)  { return alt; }
uint32_t *video_buffer(void) { return tras; }

bool video_iniciar(void)
{
    if (!info_boot.vesa_ativo || (info_boot.vesa_bpp != 32 && info_boot.vesa_bpp != 24))
        return false;
    larg = info_boot.vesa_largura;
    alt = info_boot.vesa_altura;
    pitch = info_boot.vesa_pitch;
    bpp = info_boot.vesa_bpp;
    pos_r = info_boot.verm_pos;
    pos_g = info_boot.verde_pos;
    pos_b = info_boot.azul_pos;
    if (!larg || !alt || pitch < larg * (bpp / 8)) return false;
    bool wc = false;
    tela = (uint8_t *)janela_mapear_wc(info_boot.vesa_fb, pitch * alt, &wc);
    klog("[VIDEO] memoria de video %s\n", wc ? "com escrita combinada (PAT/WC)" : "sem escrita combinada (processador sem PAT)");
    tras = kmalloc(larg * alt * 4);
    if (!tela || !tras) return false;
    formato_direto = bpp == 32 && pos_r == 16 && pos_g == 8 && pos_b == 0;
    memset(tras, 0, larg * alt * 4);
    ativo = true;
    suj_x0 = suj_y0 = 0; suj_x1 = (int)larg; suj_y1 = (int)alt;
    klog("[VIDEO] %ux%u, %u bits, pitch %u, framebuffer %x, cores R%u G%u B%u\n",
         larg, alt, bpp, pitch, info_boot.vesa_fb, pos_r, pos_g, pos_b);
    return true;
}

/* ------------------------------------------------------------ troca de resolução (H84)

 * Um driver de placa (video_nativo.c) pode trocar a resolução depois que o
 * sistema já está desenhando. Em duas etapas, para nunca deixar a tela sem
 * dono: antes de mexer na placa ele reserva o buffer de trás novo (se faltar
 * memória, desiste sem ter tocado em nada); depois que a placa já está no
 * modo novo, entrega a memória de vídeo e as medidas. O formato do pixel do
 * modo novo é sempre 32 bits 0x00RRGGBB (o "BGRX 8:8:8" das placas Intel). */
uint32_t *video_reservar(uint32_t l, uint32_t a)
{
    return kmalloc(l * a * 4);
}

void video_adotar(uint8_t *tela_nova, uint32_t *tras_novo, uint32_t l, uint32_t a, uint32_t pitch_novo)
{
    /* O que já estava desenhado vai para o canto de cima à esquerda do
     * buffer novo; o resto fica preto (quem desenha refaz depois). */
    memset(tras_novo, 0, l * a * 4);
    uint32_t cl = l < larg ? l : larg, ca = a < alt ? a : alt;
    for (uint32_t y = 0; y < ca; y++)
        memcpy(tras_novo + y * l, tras + y * larg, cl * 4);
    /* A troca acontece com as interrupções desligadas: o mouse desenha o
     * cursor de dentro da interrupção, e não pode ver metade da troca. */
    bool i0 = int_salva_desliga();
    uint32_t *velho = tras;
    tras = tras_novo;
    tela = tela_nova;
    larg = l; alt = a; pitch = pitch_novo; bpp = 32;
    pos_r = 16; pos_g = 8; pos_b = 0;
    formato_direto = true;
    if (cur_x >= (int)l) cur_x = (int)l - 1;
    if (cur_y >= (int)a) cur_y = (int)a - 1;
    cur_ant_visivel = false;
    suj_x0 = suj_y0 = 0; suj_x1 = (int)l; suj_y1 = (int)a;
    int_restaura(i0);
    kfree(velho);
    klog("[VIDEO] agora %ux%u, 32 bits, pitch %u\n", l, a, pitch_novo);
    video_mostrar();
}

void video_marcar(int x, int y, int l, int a)
{
    if (l <= 0 || a <= 0) return;
    int x1 = x + l, y1 = y + a;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)larg) x1 = (int)larg;
    if (y1 > (int)alt) y1 = (int)alt;
    if (x >= x1 || y >= y1) return;
    if (suj_x0 >= suj_x1) { suj_x0 = x; suj_y0 = y; suj_x1 = x1; suj_y1 = y1; return; }
    if (x < suj_x0) suj_x0 = x;
    if (y < suj_y0) suj_y0 = y;
    if (x1 > suj_x1) suj_x1 = x1;
    if (y1 > suj_y1) suj_y1 = y1;
}

void video_pixel(int x, int y, cor_t c)
{
    if ((uint32_t)x >= larg || (uint32_t)y >= alt) return;
    tras[(uint32_t)y * larg + (uint32_t)x] = c;
    video_marcar(x, y, 1, 1);
}

void video_retangulo(int x, int y, int l, int a, cor_t c)
{
    int x1 = x + l, y1 = y + a;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)larg) x1 = (int)larg;
    if (y1 > (int)alt) y1 = (int)alt;
    for (int j = y; j < y1; j++) {
        uint32_t *p = tras + (uint32_t)j * larg;
        for (int i = x; i < x1; i++) p[i] = c;
    }
    video_marcar(x, y, x1 - x, y1 - y);
}

/* Linha entre dois pontos: anda sempre um pixel no eixo mais longo e
 * acumula o "erro" do outro eixo, só com inteiros. */
void video_linha(int x0, int y0, int x1, int y1, cor_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1, sy = y0 < y1 ? 1 : -1;
    int passos = dx > dy ? dx : dy;
    int acum = 0;
    for (int k = 0; k <= passos; k++) {
        video_pixel(x0, y0, c);
        if (dx >= dy) {
            x0 += sx;
            acum += dy;
            if (2 * acum >= dx) { y0 += sy; acum -= dx; }
        } else {
            y0 += sy;
            acum += dx;
            if (2 * acum >= dy) { x0 += sx; acum -= dy; }
        }
    }
}

void video_moldura(int x, int y, int l, int a, int e, cor_t c)
{
    video_retangulo(x, y, l, e, c);
    video_retangulo(x, y + a - e, l, e, c);
    video_retangulo(x, y, e, a, c);
    video_retangulo(x + l - e, y, e, a, c);
}

void video_caixa(int x, int y, int l, int a, cor_t fundo, cor_t borda)
{
    video_retangulo(x, y, l, a, fundo);
    video_moldura(x, y, l, a, 1, borda);
}

void video_degrade_vertical(int x, int y, int l, int a, cor_t topo, cor_t base)
{
    for (int j = 0; j < a; j++) {
        /* Interpolação de cada canal em inteiros: topo + (base - topo) * j / a. */
        int r = (int)((topo >> 16) & 255) + (((int)((base >> 16) & 255) - (int)((topo >> 16) & 255)) * j) / a;
        int g = (int)((topo >> 8) & 255) + (((int)((base >> 8) & 255) - (int)((topo >> 8) & 255)) * j) / a;
        int b = (int)(topo & 255) + (((int)(base & 255) - (int)(topo & 255)) * j) / a;
        video_retangulo(x, y + j, l, 1, RGB(r, g, b));
    }
}

void video_rolar(int y, int altura, int linhas, cor_t fundo)
{
    if (linhas <= 0 || linhas >= altura) { video_retangulo(0, y, (int)larg, altura, fundo); return; }
    memmove(tras + (uint32_t)y * larg, tras + (uint32_t)(y + linhas) * larg, (uint32_t)(altura - linhas) * larg * 4);
    video_retangulo(0, y + altura - linhas, (int)larg, linhas, fundo);
    video_marcar(0, y, (int)larg, altura);
}

/* Copia uma faixa de pixels do buffer de trás para a placa, convertendo. */
static void copiar_linha(int y, int x0, int x1)
{
    uint32_t *o = tras + (uint32_t)y * larg + (uint32_t)x0;
    uint8_t *d = tela + (uint32_t)y * pitch + (uint32_t)x0 * (bpp / 8);
    int n = x1 - x0;
    if (formato_direto) { memcpy(d, o, (uint32_t)n * 4); return; }
    for (int i = 0; i < n; i++) {
        uint32_t c = o[i];
        uint32_t v = (((c >> 16) & 255) << pos_r) | (((c >> 8) & 255) << pos_g) | ((c & 255) << pos_b);
        if (bpp == 32) { *(uint32_t *)d = v; d += 4; }
        else { d[0] = (uint8_t)v; d[1] = (uint8_t)(v >> 8); d[2] = (uint8_t)(v >> 16); d += 3; }
    }
}

static void escrever_pixel_tela(int x, int y, uint32_t c)
{
    if ((uint32_t)x >= larg || (uint32_t)y >= alt) return;
    uint8_t *d = tela + (uint32_t)y * pitch + (uint32_t)x * (bpp / 8);
    uint32_t v = (((c >> 16) & 255) << pos_r) | (((c >> 8) & 255) << pos_g) | ((c & 255) << pos_b);
    if (bpp == 32) *(uint32_t *)d = v;
    else { d[0] = (uint8_t)v; d[1] = (uint8_t)(v >> 8); d[2] = (uint8_t)(v >> 16); }
}

static void desenhar_cursor(int x, int y)
{
    for (int j = 0; j < CUR_A; j++)
        for (int i = 0; i < CUR_L; i++) {
            char p = seta[j][i];
            if (p == '#') escrever_pixel_tela(x + i, y + j, 0x000000);
            else if (p == '.') escrever_pixel_tela(x + i, y + j, 0xFFFFFF);
        }
}

void video_mostrar(void)
{
    if (!ativo) return;
    /* Onde o cursor estava (ou vai estar) também precisa ser refeito. */
    if (cur_ant_visivel && (cur_ant_x != cur_x || cur_ant_y != cur_y || !cur_visivel))
        video_marcar(cur_ant_x, cur_ant_y, CUR_L, CUR_A);
    if (suj_x0 >= suj_x1 && cur_ant_visivel == cur_visivel && cur_ant_x == cur_x && cur_ant_y == cur_y)
        return;
    bool toca_cursor = false;
    if (suj_x0 < suj_x1) {
        for (int y = suj_y0; y < suj_y1; y++)
            copiar_linha(y, suj_x0, suj_x1);
        toca_cursor = cur_x < suj_x1 && cur_x + CUR_L > suj_x0 && cur_y < suj_y1 && cur_y + CUR_A > suj_y0;
    }
    if (cur_visivel && (toca_cursor || cur_ant_x != cur_x || cur_ant_y != cur_y || !cur_ant_visivel))
        desenhar_cursor(cur_x, cur_y);
    cur_ant_x = cur_x; cur_ant_y = cur_y; cur_ant_visivel = cur_visivel;
    suj_x0 = suj_x1 = 0;
}

void video_mostrar_tudo(void)
{
    video_marcar(0, 0, (int)larg, (int)alt);
    video_mostrar();
}

void video_cursor(int x, int y, bool visivel)
{
    cur_x = x; cur_y = y; cur_visivel = visivel;
}
