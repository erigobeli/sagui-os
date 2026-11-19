/* cpu.h - segmentos, interrupções e o quadro salvo na pilha a cada interrupção. */
#ifndef SAGUI_CPU_H
#define SAGUI_CPU_H
#include "tipos.h"

/* Seletores da GDT do kernel (índice * 8 + nível de privilégio). */
#define SEL_KCODIGO  0x08   /* no kernel de 64 bits: código de 64 bits (L=1) */
#define SEL_KDADOS   0x10
#define SEL_UCODIGO  0x1B   /* anel 3; no kernel de 64 bits, código de 32 bits (compatibilidade) */
#define SEL_UDADOS   0x23   /* anel 3 */
#ifdef __x86_64__
/* A ordem código 32 / dados / código 64 do usuário é a que as instruções
 * SYSCALL/SYSRET exigem (MSR STAR, Intel SDM vol. 2B; fase 2 da 3.0). */
#define SEL_UCODIGO64 0x2B  /* anel 3, código de 64 bits (fase 2) */
#define SEL_TSS      0x30   /* a TSS de 64 bits ocupa duas entradas (0x30 e 0x38) */
#else
#define SEL_TSS      0x28
#endif

#define VETOR_IRQ0   0x20   /* o PIC é remapeado para 0x20-0x2F */
#define VETOR_SISTEMA 0x80  /* chamadas de sistema (fase 3) */
#define VETOR_CEDER   0x81  /* tarefa do kernel cede a vez (só anel 0) */

/* Tudo que o código de entrada (kernel/arch/vetores.asm) deixa na pilha, do
 * endereço mais baixo para o mais alto. O C recebe um ponteiro para isto e
 * devolve o quadro a retomar, que pode ser de outro processo (fase 3). */
#ifdef __x86_64__
/* Kernel de 64 bits (D077): os registradores têm 64 bits, mas os programas
 * da fase 1 são de 32 bits e só enxergam a metade baixa. Cada registrador
 * tem por isso dois nomes que ocupam o mesmo lugar (union): rax para o
 * valor inteiro e eax para a metade baixa (little-endian: os 4 primeiros
 * bytes), e o código das chamadas de sistema continua lendo q->eax, q->ebx...
 * Os 5 últimos campos são os que a CPU empilha sozinha; no modo longo ela
 * empilha SS e RSP sempre, mesmo sem troca de anel (Intel SDM vol. 3A,
 * 7.14.2, "64-Bit Mode Stack Frame"). */
#define REG64(n) union { uint64_t r##n; uint32_t e##n; }
typedef struct {
    uint64_t gs, fs, es, ds;
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    REG64(di); REG64(si); REG64(bp); REG64(bx); REG64(dx); REG64(cx); REG64(ax);
    uint64_t vetor, erro;
    REG64(ip);
    uint64_t cs;
    union { uint64_t rflags; uint32_t eflags; };
    union { uint64_t rsp; uint32_t esp_usuario; };
    union { uint64_t ss; uint32_t ss_usuario; };
} quadro_t;
#undef REG64
#else
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_kernel, ebx, edx, ecx, eax;  /* ordem do pushad */
    uint32_t vetor, erro;
    uint32_t eip, cs, eflags;
    uint32_t esp_usuario, ss_usuario;  /* só existem se a interrupção veio do anel 3 */
} quadro_t;
#endif

typedef quadro_t *(*manipulador_t)(quadro_t *q);

void gdt_iniciar(void);
void idt_iniciar(void);
void idt_registrar(uint8_t vetor, manipulador_t m);
void irq_registrar(uint8_t irq, manipulador_t m);

void pic_iniciar(void);
void pic_habilitar(uint8_t irq);
void pic_desabilitar(uint8_t irq);
void pic_fim(uint8_t irq);
bool pic_espuria(uint8_t irq);

SEM_RETORNO void excecao_fatal(quadro_t *q);
quadro_t *excecao_usuario(quadro_t *q);
const char *nome_excecao(uint32_t vetor);

static inline void int_liga(void)    { __asm__ volatile("sti"); }
static inline void int_desliga(void) { __asm__ volatile("cli"); }
static inline void cpu_espera(void)  { __asm__ volatile("hlt"); }

/* Desliga as interrupções e devolve se estavam ligadas, para restaurar depois. */
static inline bool int_salva_desliga(void)
{
    uintptr_t f;   /* "pushf" empilha 8 bytes no modo longo e 4 no de 32 bits */
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f) : : "memory");
    return (f & 0x200) != 0;
}
static inline void int_restaura(bool estavam)
{
    if (estavam) int_liga();
}

/* Os registradores de controle têm a largura dos ponteiros (no modo longo,
 * "mov" de/para CRn só aceita registradores de 64 bits). */
static inline uintptr_t le_cr0(void) { uintptr_t v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline uintptr_t le_cr2(void) { uintptr_t v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline uintptr_t le_cr3(void) { uintptr_t v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline uintptr_t le_cr4(void) { uintptr_t v; __asm__ volatile("mov %%cr4, %0" : "=r"(v)); return v; }
static inline void escreve_cr0(uintptr_t v) { __asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory"); }
static inline void escreve_cr3(uintptr_t v) { __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }
static inline void escreve_cr4(uintptr_t v) { __asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory"); }

/* MSRs (registradores específicos do modelo): 64 bits em EDX:EAX. */
static inline uint64_t le_msr(uint32_t msr)
{
    uint32_t a, d;
    __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(msr));
    return ((uint64_t)d << 32) | a;
}
static inline void escreve_msr(uint32_t msr, uint64_t v)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

SEM_RETORNO void parar_maquina(void);

#endif
