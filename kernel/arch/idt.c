/* idt.c - tabela de interrupções e o despachante.
 *
 * Todas as 256 entradas apontam para os trechos de kernel/arch/vetores.asm,
 * que chamam tratar_interrupcao(). Quem quer tratar um vetor se registra na
 * tabela "manipuladores"; o despachante decide o resto:
 *   - IRQ de hardware (0x20-0x2F): descarta as espúrias, chama o
 *     manipulador e avisa o PIC do fim;
 *   - exceção da CPU sem manipulador: tela de erro e parada;
 *   - qualquer outra coisa sem manipulador: registra e ignora. */
#include "cpu.h"
#include "console.h"

#ifdef __x86_64__
/* No modo longo cada porta tem 16 bytes: o endereço do tratador tem 64 bits
 * e há o campo IST, que escolhe uma pilha de reserva da TSS (Intel SDM
 * vol. 3A, 7.14.1 "64-Bit Mode IDT"). O tipo 0xE continua sendo "porta de
 * interrupção", agora de 64 bits. */
typedef struct EMPACOTADO {
    uint16_t desloc_baixo;
    uint16_t seletor;
    uint8_t  ist;       /* 0 = pilha normal; 1-7 = pilha da TSS */
    uint8_t  tipo;      /* presente, anel permitido para "int n", tipo de porta */
    uint16_t desloc_meio;
    uint32_t desloc_alto;
    uint32_t reservado;
} porta_idt_t;
#else
typedef struct EMPACOTADO {
    uint16_t desloc_baixo;
    uint16_t seletor;
    uint8_t  zero;
    uint8_t  tipo;      /* presente, anel permitido para "int n", tipo de porta */
    uint16_t desloc_alto;
} porta_idt_t;
#endif

extern const uintptr_t tabela_vetores[256];

static porta_idt_t idt[256] ALINHADO(16);
static manipulador_t manipuladores[256];

static void definir_porta(int v, uintptr_t endereco, uint8_t tipo)
{
    idt[v].desloc_baixo = endereco & 0xFFFF;
    idt[v].seletor = SEL_KCODIGO;
    idt[v].tipo = tipo;
#ifdef __x86_64__
    idt[v].ist = 0;
    idt[v].desloc_meio = (endereco >> 16) & 0xFFFF;
    idt[v].desloc_alto = (uint32_t)(endereco >> 32);
    idt[v].reservado = 0;
#else
    idt[v].zero = 0;
    idt[v].desloc_alto = endereco >> 16;
#endif
}

void idt_iniciar(void)
{
    /* 0x8E = porta de interrupção de 32 bits, só o kernel pode usar "int n".
     * Porta de interrupção (e não de "trap") desliga IF na entrada: o kernel
     * não é interrompido no meio do tratamento de outra interrupção. */
    for (int v = 0; v < 256; v++)
        definir_porta(v, tabela_vetores[v], 0x8E);
    /* A chamada de sistema precisa ser alcançável a partir do anel 3. */
    definir_porta(VETOR_SISTEMA, tabela_vetores[VETOR_SISTEMA], 0xEE);
#ifdef __x86_64__
    /* Falta dupla e NMI usam pilhas próprias da TSS (IST1 e IST2, ver gdt.c). */
    idt[8].ist = 1;
    idt[2].ist = 2;
#endif

    struct EMPACOTADO { uint16_t limite; uintptr_t base; } reg = { sizeof idt - 1, (uintptr_t)idt };
    __asm__ volatile("lidt %0" : : "m"(reg));
}

void idt_registrar(uint8_t vetor, manipulador_t m)
{
    manipuladores[vetor] = m;
}

void irq_registrar(uint8_t irq, manipulador_t m)
{
    manipuladores[VETOR_IRQ0 + irq] = m;
    pic_habilitar(irq);
}

void aleatorio_evento(uint32_t origem);

quadro_t *tratar_interrupcao(quadro_t *q)
{
    uint32_t v = q->vetor;

    if (v >= VETOR_IRQ0 && v < VETOR_IRQ0 + 16) {
        uint8_t irq = (uint8_t)(v - VETOR_IRQ0);
        if (pic_espuria(irq))
            return q;
        aleatorio_evento(irq);   /* o instante de cada interrupção alimenta o gerador (H50) */
        if (manipuladores[v])
            q = manipuladores[v](q);
        pic_fim(irq);
        return q;
    }
    if (manipuladores[v])
        return manipuladores[v](q);
    if (v < 32) {
        if ((q->cs & 3) == 3)
            return excecao_usuario(q);   /* erro de um programa: só ele termina */
        excecao_fatal(q);                /* erro do kernel: não há como continuar */
    }
    klog("[SAGUI] interrupcao %u sem manipulador\n", v);
    return q;
}
