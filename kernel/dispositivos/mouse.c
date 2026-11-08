/* mouse.c - driver do mouse PS/2 (o touchpad do G470 fala o mesmo protocolo).
 *
 * O mouse fica na segunda porta ("auxiliar") do mesmo controlador i8042 do
 * teclado. Para ligá-lo: habilitar a porta auxiliar, ligar a IRQ 12 no byte
 * de configuração do controlador e mandar ao mouse "valores padrão" (0xF6) e
 * "comece a enviar" (0xF4). Comandos para o mouse passam pelo controlador
 * com o prefixo 0xD4.
 *
 * Depois disso, cada movimento ou clique chega como um pacote de 3 bytes:
 *   byte 0: botões (bits 0-2), bit 3 sempre 1, sinais de X e Y (bits 4-5),
 *           estouro de X e Y (bits 6-7);
 *   byte 1: deslocamento X; byte 2: deslocamento Y (para cima é positivo).
 * O bit 3 sempre ligado é o que permite reencontrar o começo de um pacote
 * se um byte se perder. */
#include "mouse.h"
#include "console.h"
#include "cpu.h"
#include "portas.h"
#include "processo.h"
#include "video.h"
#include "i8042.h"

#define FILA   64

static uint8_t pacote[4];
extern bool mouse_com_roda;
static int pos_pacote;
static int mx, my, larg = 1024, alt = 768;
static uint8_t botoes;
static evento_mouse_t fila[FILA];
static uint32_t ini, fim;
static const char canal = 0;

const void *mouse_canal(void) { return &canal; }

static void guardar(int16_t dx, int16_t dy, uint8_t mudaram, int8_t roda)
{
    uint32_t prox = (fim + 1) % FILA;
    if (prox == ini) ini = (ini + 1) % FILA;   /* fila cheia: esquece o mais antigo */
    fila[fim] = (evento_mouse_t){ (int16_t)mx, (int16_t)my, dx, dy, botoes, mudaram, roda };
    fim = prox;
    acordar(&canal);
}

static void tratar_pacote(void)
{
    uint8_t b0 = pacote[0];
    if (b0 & 0xC0) return;                         /* estouro: o valor não é confiável */
    int dx = pacote[1] - ((b0 & 0x10) ? 256 : 0);
    int dy = pacote[2] - ((b0 & 0x20) ? 256 : 0);
    mx += dx;
    my -= dy;                                      /* na tela, y cresce para baixo */
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= larg) mx = larg - 1;
    if (my >= alt) my = alt - 1;
    uint8_t novos = b0 & 7, mudaram = novos ^ botoes;
    botoes = novos;
    if (mudaram & BOTAO_ESQUERDO)
        klog("[MOUSE] botao esquerdo %s em (%d,%d)\n", novos & BOTAO_ESQUERDO ? "apertado" : "solto", mx, my);
    if (mudaram & BOTAO_DIREITO)
        klog("[MOUSE] botao direito %s em (%d,%d)\n", novos & BOTAO_DIREITO ? "apertado" : "solto", mx, my);
    /* Quarto byte: deslocamento da roda em 4 bits com sinal (positivo = para baixo). */
    int roda = 0;
    if (mouse_com_roda) { roda = pacote[3] & 0x0F; if (roda & 8) roda -= 16; }
    guardar((int16_t)dx, (int16_t)-dy, mudaram, (int8_t)roda);
    if (video_ativo()) {
        video_cursor(mx, my, true);
        video_mostrar();
    }
}

/* Cada byte do mouse chega aqui pelo i8042.c, venha da IRQ 12, da IRQ 1 ou
 * da verificação do relógio. */
void mouse_byte(uint8_t b)
{
    if (pos_pacote == 0 && !(b & 0x08)) return;   /* fora de sincronia: espera um byte 0 válido */
    pacote[pos_pacote++] = b;
    if (pos_pacote == (mouse_com_roda ? 4 : 3)) {
        pos_pacote = 0;
        tratar_pacote();
    }
}

bool mouse_iniciar(void)
{
    if (video_ativo()) {
        larg = (int)video_largura();
        alt = (int)video_altura();
    }
    mx = larg / 2;
    my = alt / 2;
    pos_pacote = 0;
    bool ok = i8042_iniciar(true);
    if (ok && video_ativo()) {
        video_cursor(mx, my, true);
        video_mostrar();
    }
    klog("[MOUSE] %s\n", ok ? "mouse PS/2 ligado" : "mouse PS/2 nao respondeu");
    return ok;
}

bool mouse_tentar(evento_mouse_t *e)
{
    bool i = int_salva_desliga();
    bool tem = ini != fim;
    if (tem) { *e = fila[ini]; ini = (ini + 1) % FILA; }
    int_restaura(i);
    return tem;
}

void mouse_posicao(int *x, int *y) { *x = mx; *y = my; }
