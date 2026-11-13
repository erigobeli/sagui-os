/* relogio.c - o PIT (temporizador programável) e o relógio CMOS.
 *
 * PIT: o canal 0 recebe um relógio fixo de 1.193.182 Hz e conta de N até 0,
 * gerando a IRQ 0 a cada volta. Com N = 11932 temos ~100 interrupções por
 * segundo, a "batida" do sistema: conta o tempo desde o boot e, na fase 3,
 * é quem tira a CPU de um processo para dar a outro.
 *
 * CMOS: o chip de relógio da placa-mãe guarda data e hora mesmo desligado.
 * Os registradores são lidos pelas portas 0x70 (índice) e 0x71 (dado). */
#include "relogio.h"
#include "cpu.h"
#include "portas.h"
#include "processo.h"
#include "i8042.h"
#include "som.h"

#define PIT_FREQ   1193182u
#define PIT_CANAL0 0x40
#define PIT_CMD    0x43

static volatile uint32_t tiques;

/* A cada tique: conta o tempo e deixa o escalonador decidir se troca de
 * processo. É isto que torna a multitarefa preemptiva: nenhum programa
 * precisa "ceder a vez". */
static quadro_t *ao_tique(quadro_t *q)
{
    tiques++;
    som_tique();     /* bip sem travar ninguém (B3) */
    i8042_tique();   /* rede de segurança: nenhum byte fica parado na fila do teclado/touchpad */
    if (!atual)
        return q;   /* antes dos processos existirem */
    return escalonador_tique(q);
}

void relogio_iniciar(void)
{
    uint32_t divisor = (PIT_FREQ + TIQUES_POR_SEGUNDO / 2) / TIQUES_POR_SEGUNDO;
    porta_escreve8(PIT_CMD, 0x34);              /* canal 0, byte baixo e alto, modo 2 (gerador de taxa) */
    porta_escreve8(PIT_CANAL0, divisor & 0xFF);
    porta_escreve8(PIT_CANAL0, divisor >> 8);
    irq_registrar(0, ao_tique);
}

uint32_t relogio_tiques(void)   { return tiques; }
uint32_t relogio_segundos(void) { return tiques / TIQUES_POR_SEGUNDO; }

void relogio_dormir_ms(uint32_t ms)
{
    uint32_t alvo = tiques + (ms * TIQUES_POR_SEGUNDO + 999) / 1000;
    while ((int32_t)(alvo - tiques) > 0)
        cpu_espera();   /* dorme até a próxima interrupção */
}

/* ------------------------------------------------------------ CMOS */

static uint8_t cmos_le(uint8_t reg)
{
    porta_escreve8(0x70, reg | 0x80);   /* bit 7 = mantém a NMI desligada durante o acesso */
    return porta_le8(0x71);
}

static bool cmos_atualizando(void)
{
    return cmos_le(0x0A) & 0x80;
}

static uint8_t de_bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static void ler_bruto(uint8_t r[7])
{
    while (cmos_atualizando())
        ;
    r[0] = cmos_le(0x00); r[1] = cmos_le(0x02); r[2] = cmos_le(0x04);
    r[3] = cmos_le(0x07); r[4] = cmos_le(0x08); r[5] = cmos_le(0x09);
    r[6] = cmos_le(0x32);   /* século, quando a placa guarda (ACPI) */
}

void cmos_ler_data(data_hora_t *d)
{
    /* O chip atualiza os registradores uma vez por segundo; uma leitura no
     * meio disso pode misturar valores. Lemos até duas leituras seguidas
     * darem o mesmo resultado. */
    uint8_t a[7], b[7];
    ler_bruto(b);
    do {
        for (int i = 0; i < 7; i++) a[i] = b[i];
        ler_bruto(b);
    } while (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3] || a[4] != b[4] || a[5] != b[5]);

    uint8_t status_b = cmos_le(0x0B);
    bool bcd = !(status_b & 0x04);
    bool h12 = !(status_b & 0x02);
    uint8_t pm = b[2] & 0x80;
    b[2] &= 0x7F;
    if (bcd)
        for (int i = 0; i < 7; i++) b[i] = de_bcd(b[i]);
    if (h12) {
        if (pm && b[2] < 12) b[2] += 12;
        if (!pm && b[2] == 12) b[2] = 0;
    }
    d->segundo = b[0]; d->minuto = b[1]; d->hora = b[2];
    d->dia = b[3]; d->mes = b[4];
    uint32_t seculo = (b[6] >= 19 && b[6] <= 21) ? b[6] : 20;
    d->ano = (uint16_t)(seculo * 100 + b[5]);
}

static void cmos_escreve(uint8_t reg, uint8_t v)
{
    porta_escreve8(0x70, reg | 0x80);
    porta_escreve8(0x71, v);
}

static uint8_t para_bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* Acerta o relógio da placa-mãe (H38). Liga o bit SET do registrador B, que
 * congela a contagem enquanto os valores mudam, escreve no mesmo formato que
 * o chip usa (BCD ou binário, 12 ou 24 horas) e solta o bit. */
void cmos_gravar_data(const data_hora_t *d)
{
    bool i0 = int_salva_desliga();
    uint8_t status_b = cmos_le(0x0B);
    bool bcd = !(status_b & 0x04);
    bool h12 = !(status_b & 0x02);
    cmos_escreve(0x0B, status_b | 0x80);
    uint8_t hora = d->hora;
    uint8_t pm = 0;
    if (h12) {
        pm = hora >= 12 ? 0x80 : 0;
        hora = (uint8_t)(hora % 12 == 0 ? 12 : hora % 12);
    }
    uint8_t v[7] = { d->segundo, d->minuto, hora, d->dia, d->mes, (uint8_t)(d->ano % 100), (uint8_t)(d->ano / 100) };
    if (bcd)
        for (int i = 0; i < 7; i++) v[i] = para_bcd(v[i]);
    v[2] |= pm;
    cmos_escreve(0x00, v[0]); cmos_escreve(0x02, v[1]); cmos_escreve(0x04, v[2]);
    cmos_escreve(0x07, v[3]); cmos_escreve(0x08, v[4]); cmos_escreve(0x09, v[5]);
    cmos_escreve(0x32, v[6]);
    cmos_escreve(0x0B, status_b & 0x7F);
    int_restaura(i0);
}

/* Dia da semana: conta os dias desde 01/01/2000, que foi um sábado.
 * Devolve 0 = domingo ... 6 = sábado. */
int dia_da_semana(int ano, int mes, int dia)
{
    static const int dias_mes[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (ano < 2000) return 0;
    int total = 0;
    for (int a = 2000; a < ano; a++)
        total += (a % 4 == 0 && (a % 100 != 0 || a % 400 == 0)) ? 366 : 365;
    for (int m = 1; m < mes && m <= 12; m++)
        total += dias_mes[m - 1] + (m == 2 && ano % 4 == 0 && (ano % 100 != 0 || ano % 400 == 0));
    total += dia - 1;
    return (total + 6) % 7;
}
