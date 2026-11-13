/* aleatorio.c - números aleatórios para a criptografia (H50).
 *
 * A criptografia precisa de números que ninguém consiga adivinhar: as chaves
 * de cada conexão TLS e SSH saem daqui. O processador do G470 (Sandy Bridge)
 * não tem a instrução RDRAND, que só chegou na geração seguinte; então a
 * "incerteza" vem do próprio hardware:
 *
 *   - a cada interrupção (relógio, teclado, touchpad...) o kernel lê o
 *     contador de ciclos da CPU (RDTSC). O instante exato, em ciclos, em que
 *     cada interrupção chega varia por motivos físicos (osciladores
 *     diferentes, atrasos de barramento, a pessoa digitando);
 *   - no boot, um laço mede quantos ciclos cabem entre dois tiques do
 *     relógio, várias vezes: a variação é pequena mas real;
 *   - se o processador tiver RDRAND, entra também.
 *
 * Tudo isso é misturado num "poço" de 32 palavras por rodadas de embaralhar
 * (somar, girar, ou-exclusivo, como no ChaCha). Para entregar números, o
 * kernel usa o poço como chave de um ChaCha20 próprio e, logo depois de cada
 * pedido, troca a chave por uma saída nova do próprio ChaCha20 ("apagar a
 * chave depois de usar"): mesmo quem lesse a memória depois não conseguiria
 * refazer os números já entregues. Ver D035. */
#include "cpu.h"
#include "console.h"
#include "relogio.h"
#include "sistema.h"
#include "texto.h"

static uint32_t poco[16];         /* chave (8 palavras) + mistura */
static uint32_t contador;
static uint32_t eventos;

static inline uint64_t ciclos(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define GIRAR(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define QUARTO(a, b, c, d) \
    a += b; d ^= a; d = GIRAR(d, 16); c += d; b ^= c; b = GIRAR(b, 12); \
    a += b; d ^= a; d = GIRAR(d, 8);  c += d; b ^= c; b = GIRAR(b, 7)

/* O bloco do ChaCha20 (20 rodadas): 16 palavras de entrada, 16 de saída. */
static void bloco(const uint32_t e[16], uint32_t s[16])
{
    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = e[i];
    for (int r = 0; r < 10; r++) {
        QUARTO(x[0], x[4], x[8], x[12]); QUARTO(x[1], x[5], x[9], x[13]);
        QUARTO(x[2], x[6], x[10], x[14]); QUARTO(x[3], x[7], x[11], x[15]);
        QUARTO(x[0], x[5], x[10], x[15]); QUARTO(x[1], x[6], x[11], x[12]);
        QUARTO(x[2], x[7], x[8], x[13]); QUARTO(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) s[i] = x[i] + e[i];
}

/* Junta um valor ao poço: entra numa palavra e uma rodada espalha pelas outras. */
static void misturar(uint32_t v)
{
    uint32_t i = eventos++ % 16;
    poco[i] ^= v;
    QUARTO(poco[i], poco[(i + 4) % 16], poco[(i + 8) % 16], poco[(i + 12) % 16]);
}

/* Chamada a cada interrupção de hardware. */
void aleatorio_evento(uint32_t origem)
{
    uint64_t t = ciclos();
    misturar((uint32_t)t ^ (origem << 24) ^ (uint32_t)(t >> 32));
}

static bool tem_rdrand(void)
{
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    return c & (1u << 30);
}

void aleatorio_iniciar(void)
{
    /* Variação dos ciclos entre tiques do relógio: 32 medidas. */
    for (int k = 0; k < 32; k++) {
        uint32_t t0 = relogio_tiques();
        while (relogio_tiques() == t0) cpu_espera();
        misturar((uint32_t)ciclos());
    }
    data_hora_t d;
    cmos_ler_data(&d);
    misturar((uint32_t)d.segundo | ((uint32_t)d.minuto << 8) | ((uint32_t)d.hora << 16) | ((uint32_t)d.dia << 24));
    bool rd = tem_rdrand();
    if (rd)
        for (int k = 0; k < 16; k++) {
            uint32_t v = 0;
            __asm__ volatile("rdrand %0" : "=r"(v) : : "cc");
            misturar(v);
        }
    klog("[ALEATORIO] poco iniciado: %u eventos, RDRAND %s\n", eventos, rd ? "sim" : "nao (G470: so o hardware)");
}

/* Enche "n" bytes com números aleatórios. */
void aleatorio_bytes(uint8_t *destino, uint32_t n)
{
    bool i0 = int_salva_desliga();
    uint32_t e[16], s[16];
    /* Constantes do ChaCha ("expand 32-byte k"), chave = poço, contador e o momento. */
    e[0] = 0x61707865; e[1] = 0x3320646e; e[2] = 0x79622d32; e[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) e[4 + i] = poco[i] ^ poco[8 + i];
    uint64_t t = ciclos();
    e[12] = contador++; e[13] = (uint32_t)t; e[14] = (uint32_t)(t >> 32); e[15] = eventos;
    /* O primeiro bloco vira a chave nova do poço; os seguintes são entregues. */
    bloco(e, s);
    for (int i = 0; i < 8; i++) poco[i] = s[i];
    while (n > 0) {
        e[12] = contador++;
        bloco(e, s);
        uint32_t k = n < 64 ? n : 64;
        memcpy(destino, s, k);
        destino += k;
        n -= k;
    }
    memset(s, 0, sizeof s);
    memset(e, 0, sizeof e);
    int_restaura(i0);
}

/* SIS_ALEATORIO (&destino, n) -> n. */
int32_t sis_aleatorio(uint32_t ptr, uint32_t n, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    if (n > 4096) n = 4096;
    if (!usuario_ok((void *)ptr, n, true)) return ERRO_ENDERECO;
    aleatorio_bytes((uint8_t *)ptr, n);
    return (int32_t)n;
}
