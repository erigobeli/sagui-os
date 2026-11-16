/* gdt.c - tabela de segmentos e TSS do kernel de 64 bits (3.0, D055).
 *
 * No modo longo a segmentação quase some: base e limite são ignorados nos
 * segmentos de código e dados de 64 bits (Intel SDM vol. 3A, 3.2.4). Mas a
 * GDT continua obrigatória, porque é ela que diz:
 *   - o nível de privilégio (anel 0 para o kernel, anel 3 para programas);
 *   - se um segmento de código é de 64 bits (bit L=1) ou de 32 bits (L=0,
 *     D=1). Um programa ELF32 da 2.x roda no "modo de compatibilidade"
 *     simplesmente porque o CS dele aponta para um descritor com L=0 e D=1
 *     (SDM vol. 3A, 5.2.1; AMD APM vol. 2, 4.8). Para esses programas, os
 *     segmentos voltam a valer como no modo protegido de 32 bits: por isso o
 *     de dados do usuário continua com base 0 e limite de 4 GB.
 *
 * Ordem das entradas:
 *   0x00 nula
 *   0x08 código do kernel, 64 bits
 *   0x10 dados do kernel
 *   0x18 código do usuário, 32 bits (seletor 0x1B, o mesmo da 2.x)
 *   0x20 dados do usuário (0x23)
 *   0x28 código do usuário, 64 bits (0x2B, para a fase 2)
 *   0x30 TSS de 64 bits (descritor de 16 bytes: ocupa 0x30 e 0x38)
 * Do 0x18 ao 0x28 é a ordem que o SYSRET espera do MSR STAR (fase 2).
 *
 * A TSS de 64 bits não guarda mais registradores de tarefa (o modo longo não
 * troca tarefas por hardware): só as pilhas. RSP0 é a pilha do kernel usada
 * quando um programa do anel 3 é interrompido (o escalonador troca a cada
 * processo), e os IST são pilhas de reserva que a CPU usa para vetores
 * escolhidos na IDT, sempre, mesmo vindo do anel 0 (SDM vol. 3A, 7.14.5).
 * O SaguiOS usa duas: falta dupla e NMI, que podem chegar com a pilha do
 * kernel estragada (é justamente estourar a pilha que costuma causar a falta
 * dupla) e sem elas virariam falta tripla, que reinicia a máquina sem
 * mostrar nada. */
#include "cpu.h"
#include "texto.h"

typedef struct EMPACOTADO {
    uint32_t reservado0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reservado1;
    uint64_t ist[7];          /* ist[0] é o IST1 */
    uint64_t reservado2;
    uint16_t reservado3;
    uint16_t mapa_es;         /* deslocamento do mapa de portas de E/S */
} tss64_t;

static uint64_t gdt[8] ALINHADO(16);
static tss64_t tss ALINHADO(16);
static uint8_t pilha_falta_dupla[8192] ALINHADO(16);
static uint8_t pilha_nmi[8192] ALINHADO(16);

/* Descritor de código ou dados. 'acesso' traz presente/anel/tipo; 'flags'
 * traz G (granularidade 4 KB), D/B (32 bits) e L (64 bits), nesta ordem:
 * bit 3 = G, bit 2 = D/B, bit 1 = L (SDM vol. 3A, 3.4.5). */
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

void tss_definir_pilha(uintptr_t rsp0)
{
    tss.rsp0 = rsp0;
}

void gdt_iniciar(void)
{
    gdt[0] = 0;
    gdt[1] = descritor(0, 0xFFFFF, 0x9A, 0xA);   /* código, anel 0, 64 bits (G=1, L=1) */
    gdt[2] = descritor(0, 0xFFFFF, 0x92, 0xC);   /* dados, anel 0 */
    gdt[3] = descritor(0, 0xFFFFF, 0xFA, 0xC);   /* código, anel 3, 32 bits (G=1, D=1, L=0) */
    gdt[4] = descritor(0, 0xFFFFF, 0xF2, 0xC);   /* dados, anel 3 */
    gdt[5] = descritor(0, 0xFFFFF, 0xFA, 0xA);   /* código, anel 3, 64 bits (fase 2) */

    memset(&tss, 0, sizeof tss);
    tss.ist[0] = (uintptr_t)(pilha_falta_dupla + sizeof pilha_falta_dupla);   /* IST1 */
    tss.ist[1] = (uintptr_t)(pilha_nmi + sizeof pilha_nmi);                   /* IST2 */
    tss.mapa_es = sizeof tss;   /* sem mapa de portas: anel 3 não acessa E/S */
    /* Descritor de sistema de 16 bytes (SDM vol. 3A, 8.2.3): os 8 primeiros
     * no formato de sempre (tipo 9 = TSS de 64 bits disponível) e os 8
     * seguintes com os bits 32-63 da base. */
    uintptr_t base = (uintptr_t)&tss;
    gdt[6] = descritor((uint32_t)base, sizeof tss - 1, 0x89, 0x0);
    gdt[7] = (uint64_t)base >> 32;

    struct EMPACOTADO { uint16_t limite; uint64_t base; } reg = { sizeof gdt - 1, (uintptr_t)gdt };
    /* No modo longo não há "ljmp" com seletor imediato: o CS novo é
     * carregado com um "lretq" (desempilha RIP e depois CS). */
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "mov %2, %%eax\n\t"
        "mov %%eax, %%ds\n\t mov %%eax, %%es\n\t mov %%eax, %%fs\n\t mov %%eax, %%gs\n\t mov %%eax, %%ss\n\t"
        "mov %3, %%eax\n\t"
        "ltr %%ax"
        : : "m"(reg), "i"(SEL_KCODIGO), "i"(SEL_KDADOS), "i"(SEL_TSS) : "rax", "memory");
}
