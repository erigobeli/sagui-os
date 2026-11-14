/* i8042.c - o controlador do teclado e do touchpad (H19-fix).
 *
 * O teclado e o mouse/touchpad PS/2 ficam atrás do mesmo chip, que tem UMA
 * fila de saída de um byte. O bit 5 do estado diz de quem é o byte que está
 * esperando. Se um byte não for lido, nenhum outro passa: um byte do mouse
 * esquecido na fila trava o teclado. Foi o que aconteceu no G470 na fase 4.
 *
 * Por isso este é o único lugar que lê a fila. Cada byte é entregue ao
 * teclado ou ao mouse conforme o bit 5, e a fila é esvaziada:
 *   - na interrupção do teclado (IRQ 1);
 *   - na interrupção do mouse (IRQ 12);
 *   - a cada tique do relógio, como rede de segurança: se uma das
 *     interrupções não chegar (configuração diferente no hardware real),
 *     nada fica parado mais que 10 ms.
 *
 * A configuração do chip é lida com a fila vazia e conferida; se a leitura
 * falhar ou vier estranha, usamos valores seguros, sempre mantendo a
 * interrupção e a tradução do teclado ligadas. */
#include "i8042.h"
#include "console.h"
#include "cpu.h"
#include "portas.h"

#define DADOS  0x60
#define ESTADO 0x64

void teclado_byte(uint8_t b);
void mouse_byte(uint8_t b);

static bool mouse_ligado;
static i8042_diag_t diag;

const i8042_diag_t *i8042_diagnostico(void) { return &diag; }

static bool esperar_escrita(void)
{
    for (int i = 0; i < 200000; i++)
        if (!(porta_le8(ESTADO) & 2)) return true;
    return false;
}

/* Espera um byte por até ~0,5 s (cada leitura de porta leva ~1 µs). */
static int ler_resposta(void)
{
    for (int i = 0; i < 500000; i++)
        if (porta_le8(ESTADO) & 1) return porta_le8(DADOS);
    return -1;
}

static void descartar_pendentes(void)
{
    for (int i = 0; i < 32 && (porta_le8(ESTADO) & 1); i++) {
        porta_le8(DADOS);
        porta_pausa();
    }
}

static void comando(uint8_t c)
{
    esperar_escrita();
    porta_escreve8(ESTADO, c);
}

/* Manda um byte ao mouse (prefixo 0xD4) e devolve a resposta. 0xFE = "repita". */
static int para_mouse(uint8_t c)
{
    for (int tentativa = 0; tentativa < 3; tentativa++) {
        comando(0xD4);
        esperar_escrita();
        porta_escreve8(DADOS, c);
        int r = ler_resposta();
        if (r != 0xFE) return r;
    }
    return 0xFE;
}

/* Entrega tudo o que estiver na fila. */
void i8042_esvaziar(void)
{
    for (int i = 0; i < 32; i++) {
        uint8_t estado = porta_le8(ESTADO);
        if (!(estado & 1)) return;
        uint8_t b = porta_le8(DADOS);
        if (estado & 0x20) {
            if (mouse_ligado) mouse_byte(b);   /* sem mouse ligado, o byte é só descartado */
        } else {
            teclado_byte(b);
        }
    }
}

static quadro_t *ao_interromper(quadro_t *q)
{
    i8042_esvaziar();
    return q;
}

void i8042_tique(void)
{
    if (porta_le8(ESTADO) & 1)
        i8042_esvaziar();
}

static void gravar_config(uint8_t cfg)
{
    comando(0x60);
    esperar_escrita();
    porta_escreve8(DADOS, cfg);
}

bool i8042_iniciar(bool com_mouse)
{
    bool ints = int_salva_desliga();
    diag = (i8042_diag_t){ -1, -1, -1, -1, 0 };
    comando(0xAD);                       /* teclado e mouse quietos durante a configuração */
    comando(0xA7);
    descartar_pendentes();               /* a fila TEM que estar vazia antes de pedir a configuração */

    comando(0x20);
    int lida = ler_resposta();
    diag.config_lida = lida;
    /* Bits 3 e 7 são reservados (zero). Se a leitura falhou ou veio
     * estranha, parte de um valor seguro: interrupção do teclado, "sistema
     * passou no POST" e tradução para o código de teclas set 1. */
    uint8_t cfg = (lida < 0 || (lida & 0x88)) ? 0x45 : (uint8_t)lida;
    cfg |= 0x01 | 0x40;                  /* sempre: IRQ 1 e tradução (o mapa ABNT2 usa o set 1) */
    cfg &= (uint8_t)~0x10;               /* relógio do teclado ligado */
    cfg &= (uint8_t)~0x02;               /* IRQ 12 só depois que o mouse responder */
    cfg |= 0x20;                         /* relógio do mouse desligado por enquanto */
    gravar_config(cfg);
    comando(0xAE);                       /* teclado de volta */

    bool ok = false;
    if (com_mouse) {
        comando(0xA8);                   /* porta do mouse */
        cfg &= (uint8_t)~0x20;
        gravar_config(cfg);
        descartar_pendentes();
        diag.resposta_padrao = para_mouse(0xF6);   /* valores padrão */
        if (diag.resposta_padrao != 0xFA) {
            /* Alguns touchpads só respondem depois de um reset (0xFF -> FA, AA, 00). */
            diag.resposta_reset = para_mouse(0xFF);
            if (diag.resposta_reset == 0xFA) { ler_resposta(); ler_resposta(); }
            diag.resposta_padrao = para_mouse(0xF6);
        }
        /* 200 pacotes por segundo (comando 0xF3 "taxa de amostragem" do
         * protocolo PS/2): o padrão de 100 deixa o cursor aos saltos. Quem não
         * aceitar segue no padrão (H81, do Robson). */
        if (diag.resposta_padrao == 0xFA && para_mouse(0xF3) == 0xFA) para_mouse(200);
        diag.resposta_ligar = para_mouse(0xF4);    /* começar a enviar */
        ok = diag.resposta_padrao == 0xFA && diag.resposta_ligar == 0xFA;
        if (ok) {
            cfg |= 0x02;                 /* agora sim: IRQ 12 */
        } else {
            comando(0xA7);               /* mouse mudo: porta desligada, para não sujar a fila */
            cfg |= 0x20;
        }
        gravar_config(cfg);
        descartar_pendentes();
    }
    diag.config_final = cfg;
    mouse_ligado = ok;
    irq_registrar(1, ao_interromper);
    if (ok) irq_registrar(12, ao_interromper);
    int_restaura(ints);
    klog("[I8042] config lida %x, gravada %x; mouse: padrao=%x reset=%x ligar=%x -> %s\n",
         (uint32_t)diag.config_lida, cfg, (uint32_t)diag.resposta_padrao, (uint32_t)diag.resposta_reset,
         (uint32_t)diag.resposta_ligar, ok ? "ligado" : "nao respondeu");
    return ok;
}
