/* montador.h - as instruções do x86 que o compilador usa, em bytes. */
#ifndef SAGUI_MONTADOR_H
#define SAGUI_MONTADOR_H
#include "interno.h"

/* Registradores, com os números que o processador usa nas instruções. */
enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };

/* Contas "de dois lados" (o número entra no código da instrução). */
enum { A_ADD, A_OR, A_ADC, A_SBB, A_AND, A_SUB, A_XOR, A_CMP };

/* Condições dos saltos e do setcc. */
enum { C_B = 2, C_AE = 3, C_E = 4, C_NE = 5, C_BE = 6, C_A = 7, C_L = 12, C_GE = 13, C_LE = 14, C_G = 15 };

/* Um lugar da memória: [base + desl] ou, sem base (-1), o endereço fixo
 * "desl" (somado ao endereço do símbolo "sim", se houver). */
typedef struct { int base; i32 desl; int sim; } Mem;

void m_inicio(Secao *s);                    /* as instruções seguintes vão para esta seção */
u32  m_aqui(void);                          /* quantos bytes já foram postos */
void m_remenda(u32 onde, u32 valor);        /* troca 4 bytes já postos */

void m_alu_rr(int op, int dst, int src);
void m_alu_rm(int op, int reg, Mem m);
void m_alu_ri(int op, int reg, i32 imm);
void m_mov_rr(int dst, int src);
void m_mov_ri(int reg, i32 imm);
void m_mov_r_end(int reg, int sim, i32 desl);   /* reg = endereço de um símbolo */
void m_mov_mi(int tipo, Mem m, i32 imm);
void m_carrega(int tipo, int reg, Mem m);       /* lê da memória, estendendo para 32 bits */
void m_guarda(int tipo, Mem m, int reg);
void m_estende(int tipo, int reg);
void m_lea(int reg, Mem m);
void m_imul_rr(int dst, int src);
void m_imul_rm(int reg, Mem m);
void m_imul_ri(int reg, i32 imm);
void m_grupo3(int sub, int reg);                /* 2 not, 3 neg, 4 mul, 6 div, 7 idiv */
void m_desl_i(int sub, int reg, int n);         /* 4 shl, 5 shr, 7 sar */
void m_desl_cl(int sub, int reg);
void m_cdq(void);
void m_setcc(int cc, int reg);
void m_test_rr(int a, int b);
u32  m_jmp(void);                               /* devolve onde fica a distância, para remendar */
u32  m_jcc(int cc);
void m_call_sim(int sim);
void m_call_reg(int reg);
void m_ret(u32 desempilha);
void m_push(int reg);
void m_pop(int reg);
void m_leave(void);
void m_rep(int instr);                          /* 0xA5 movsd, 0xA4 movsb, 0xAB stosd, 0xAA stosb */
void m_byte(int b);
void m_u32(u32 v);

#endif
