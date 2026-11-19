/* gdt.c - tabela de segmentos do kernel e a TSS.
 *
 * O SaguiOS usa o modelo "plano": todos os segmentos começam em 0 e cobrem
 * 4 GB, e a proteção de memória de verdade vem da paginação (fase 3). Ainda
 * assim a GDT é obrigatória, porque é ela que define o nível de privilégio:
 * código e dados do kernel no anel 0, código e dados dos programas no anel 3.
 *
 * A TSS existe por um único motivo: quando um programa no anel 3 sofre uma
 * interrupção, o processador precisa saber qual pilha do kernel usar, e lê
 * isso dos campos esp0/ss0 da TSS. O escalonador troca esp0 a cada processo. */
#include "cpu.h"
#include "texto.h"

typedef struct EMPACOTADO {
    uint32_t anterior;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, mapa_es;
} tss_t;

static uint64_t gdt[6] ALINHADO(8);
static tss_t tss ALINHADO(16);

/* Monta um descritor de segmento. 'acesso' traz presente/anel/tipo e
 * 'flags' traz granularidade (4 KB) e tamanho (32 bits), como no manual da Intel. */
static uint64_t descritor(uint32_t base, uint32_t limite, uint8_t acesso, uint8_t flags)
{
    uint64_t d = 0;
    d |= limite & 0xFFFF;
    d |= (uint64_t)(base & 0xFFFFFF) << 16;
    d |= (uint64_t)acesso << 40;
    d |= (uint64_t)((limite >> 16) & 0xF) << 48;
    d |= (uint64_t)(flags & 0xF) << 52;
    d |= (uint64_t)((base >> 24) & 0xFF) << 56;
    return d;
}

void tss_definir_pilha(uintptr_t esp0)
{
    tss.esp0 = esp0;
}

void gdt_iniciar(void)
{
    gdt[0] = 0;
    gdt[1] = descritor(0, 0xFFFFF, 0x9A, 0xC);   /* código, anel 0 */
    gdt[2] = descritor(0, 0xFFFFF, 0x92, 0xC);   /* dados, anel 0 */
    gdt[3] = descritor(0, 0xFFFFF, 0xFA, 0xC);   /* código, anel 3 */
    gdt[4] = descritor(0, 0xFFFFF, 0xF2, 0xC);   /* dados, anel 3 */
    memset(&tss, 0, sizeof tss);
    tss.ss0 = SEL_KDADOS;
    tss.mapa_es = sizeof tss;                    /* sem mapa de portas: anel 3 não acessa E/S */
    gdt[5] = descritor((uint32_t)&tss, sizeof tss - 1, 0x89, 0x0);  /* TSS 32 bits disponível */

    struct EMPACOTADO { uint16_t limite; uint32_t base; } reg = { sizeof gdt - 1, (uint32_t)gdt };
    __asm__ volatile(
        "lgdt %0\n\t"
        "ljmp %1, $1f\n"                          /* recarrega CS com o seletor novo */
        "1:\n\t"
        "mov %2, %%ax\n\t"
        "mov %%ax, %%ds\n\t mov %%ax, %%es\n\t mov %%ax, %%fs\n\t mov %%ax, %%gs\n\t mov %%ax, %%ss\n\t"
        "mov %3, %%ax\n\t"
        "ltr %%ax"
        : : "m"(reg), "i"(SEL_KCODIGO), "i"(SEL_KDADOS), "i"(SEL_TSS) : "eax", "memory");
}
