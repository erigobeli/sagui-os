/* cpu.h - segmentos, interrupções e o quadro salvo na pilha a cada interrupção. */
#ifndef SAGUI_CPU_H
#define SAGUI_CPU_H
#include "tipos.h"

/* Seletores da GDT do kernel (índice * 8 + nível de privilégio). */
#define SEL_KCODIGO  0x08
#define SEL_KDADOS   0x10
#define SEL_UCODIGO  0x1B   /* anel 3 */
#define SEL_UDADOS   0x23   /* anel 3 */
#define SEL_TSS      0x28

#define VETOR_IRQ0   0x20   /* o PIC é remapeado para 0x20-0x2F */
#define VETOR_SISTEMA 0x80  /* chamadas de sistema (fase 3) */
#define VETOR_CEDER   0x81  /* tarefa do kernel cede a vez (só anel 0) */

/* Tudo que o código de entrada (kernel/arch/vetores.asm) deixa na pilha, do
 * endereço mais baixo para o mais alto. O C recebe um ponteiro para isto e
 * devolve o quadro a retomar, que pode ser de outro processo (fase 3). */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_kernel, ebx, edx, ecx, eax;  /* ordem do pushad */
    uint32_t vetor, erro;
    uint32_t eip, cs, eflags;
    uint32_t esp_usuario, ss_usuario;  /* só existem se a interrupção veio do anel 3 */
} quadro_t;

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
    uint32_t f;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f));
    return (f & 0x200) != 0;
}
static inline void int_restaura(bool estavam)
{
    if (estavam) int_liga();
}

static inline uint32_t le_cr0(void) { uint32_t v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline uint32_t le_cr2(void) { uint32_t v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline uint32_t le_cr3(void) { uint32_t v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }

SEM_RETORNO void parar_maquina(void);
SEM_RETORNO void maquina_reiniciar(void);
SEM_RETORNO void maquina_desligar(void);

#endif
