/* som.c - bip pelo alto-falante do PC (bônus B3).
 *
 * O alto-falante interno do PC é ligado à saída do canal 2 do PIT, o mesmo
 * chip que dá a batida do sistema no canal 0. Programando o canal 2 como
 * gerador de onda quadrada com divisor N, a saída oscila a 1.193.182 / N Hz;
 * o bit 0 da porta 0x61 libera o relógio do canal 2 e o bit 1 liga a saída
 * ao alto-falante. Desligar o bit 1 cala o som.
 *
 * Os programas mandam uma lista de notas (frequência e duração) e voltam na
 * hora: quem toca é o tique do relógio, que a cada 10 ms desconta o tempo da
 * nota atual e passa para a próxima. Assim um jogo não trava enquanto o bip
 * toca. Uma lista nova substitui a que estiver tocando (um efeito novo é
 * mais importante que o fim do anterior), a não ser que o programa peça para
 * enfileirar.
 *
 * Diagnóstico: na primeira nota do boot, o kernel lê a saída do canal 2 pelo
 * bit 5 da porta 0x61 várias vezes; se ela muda, o canal está oscilando. Isso
 * vai para o log e mostra que o lado do sistema funcionou, mesmo que o
 * notebook não ligue esse sinal ao alto-falante. */
#include "som.h"
#include "cpu.h"
#include "portas.h"
#include "console.h"
#include "relogio.h"
#include "sistema.h"
#include "texto.h"
#include "hda.h"

#define PIT_FREQ    1193182u
#define PIT_CANAL2  0x42
#define PIT_CMD     0x43
#define PORTA_B     0x61
#define MAX_NOTAS   64

static nota_t fila[MAX_NOTAS];
static volatile int inicio, quantas;
static volatile int restante_tiques;   /* da nota tocando agora; 0 = parado */
static bool conferido;

static void soar(uint32_t freq)
{
    if (freq < 20 || freq > 20000) {           /* 0 = pausa; fora disso nem é audível */
        porta_escreve8(PORTA_B, porta_le8(PORTA_B) & ~3u);
        return;
    }
    uint32_t divisor = PIT_FREQ / freq;
    porta_escreve8(PIT_CMD, 0xB6);              /* canal 2, byte baixo e alto, modo 3 (onda quadrada) */
    porta_escreve8(PIT_CANAL2, divisor & 0xFF);
    porta_escreve8(PIT_CANAL2, (divisor >> 8) & 0xFF);
    porta_escreve8(PORTA_B, porta_le8(PORTA_B) | 3u);
}

void som_parar(void)
{
    porta_escreve8(PORTA_B, porta_le8(PORTA_B) & ~3u);
    restante_tiques = 0;
}

/* Começa a próxima nota da fila (ou cala, se acabou). Interrupções desligadas. */
static void proxima(void)
{
    if (quantas == 0) { som_parar(); klog("[BIP] silencio\n"); return; }
    nota_t n = fila[inicio];
    inicio = (inicio + 1) % MAX_NOTAS;
    quantas--;
    soar(n.freq);
    uint32_t t = (n.ms * TIQUES_POR_SEGUNDO + 999) / 1000;
    restante_tiques = t ? (int)t : 1;
    klog("[BIP] %u Hz %u ms\n", n.freq, n.ms);
}

void som_tique(void)
{
    if (restante_tiques == 0) return;
    if (--restante_tiques == 0) proxima();
}

/* Lê a saída do canal 2 (bit 5 da porta 0x61) por 5 ms e conta as viradas.
 * O tempo é medido pelo contador do canal 0 (que desce de ~11932 a 0 a cada
 * 10 ms a 1,19 MHz), e não por número de voltas: uma leitura de porta leva
 * ~1 µs num PC de verdade, mas bem menos num emulador. */
static uint32_t canal0_agora(void)
{
    porta_escreve8(0x43, 0x00);                  /* trava o valor atual do canal 0 */
    uint32_t lo = porta_le8(0x40);
    uint32_t hi = porta_le8(0x40);
    return lo | (hi << 8);
}

static void conferir_canal2(void)
{
    int viradas = 0;
    uint32_t passou = 0, antes_c = canal0_agora();
    uint8_t antes = porta_le8(PORTA_B) & 0x20;
    for (uint32_t guarda = 0; passou < 5966 && guarda < 2000000; guarda++) {   /* 5966 = 5 ms */
        uint8_t agora = porta_le8(PORTA_B) & 0x20;
        if (agora != antes) viradas++;
        antes = agora;
        uint32_t c = canal0_agora();
        passou += c <= antes_c ? antes_c - c : antes_c + 11932 - c;   /* deu a volta */
        antes_c = c;
    }
    klog("[BIP] canal 2 do PIT oscilando: %s (%d viradas em 5 ms)\n", viradas >= 2 ? "sim" : "NAO", viradas);
}

/* SIS_BIP (notas, quantidade, modo): modo 0 substitui o que toca, 1 enfileira. */
int32_t sis_bip(uint32_t pnotas, uint32_t n, uint32_t modo, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (n == 0) { bool i0 = int_salva_desliga(); quantas = 0; som_parar(); int_restaura(i0); return 0; }
    if (n > MAX_NOTAS) n = MAX_NOTAS;
    if (!usuario_ok((const void *)pnotas, n * sizeof(nota_t), false)) return ERRO_INVALIDO;
    nota_t copia[MAX_NOTAS];
    memcpy(copia, (const void *)pnotas, n * sizeof(nota_t));
    /* Com placa de som (H42), as notas tocam por ela; o alto-falante do PC
     * fica de reserva para quando não há placa. */
    if (misturador_notas(copia, (int)n, modo == BIP_ENFILEIRAR)) {
        for (uint32_t i = 0; i < n; i++) klog("[SOM] nota %u Hz %u ms\n", copia[i].freq, copia[i].ms);
        return (int32_t)n;
    }
    bool i0 = int_salva_desliga();
    if (modo != BIP_ENFILEIRAR) { inicio = 0; quantas = 0; restante_tiques = 0; }
    for (uint32_t i = 0; i < n && quantas < MAX_NOTAS; i++) {
        fila[(inicio + quantas) % MAX_NOTAS] = copia[i];
        quantas++;
    }
    if (restante_tiques == 0) proxima();
    int_restaura(i0);
    if (!conferido) {
        conferido = true;
        if (copia[0].freq >= 20) conferir_canal2();
    }
    return (int32_t)n;
}
