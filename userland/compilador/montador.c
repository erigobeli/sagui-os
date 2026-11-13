/* montador.c - o montador: transforma cada instrução x86 nos seus bytes.
 *
 * Não há texto de assembly no meio do caminho: o gerador de código chama
 * uma função por instrução ("ponha aqui um mov deste registrador para
 * aquele lugar da memória") e a função escreve os bytes na seção. Os
 * formatos vêm do manual da Intel (volume 2). Uma instrução comum é:
 *
 *     [código] [ModRM] [SIB] [deslocamento] [valor imediato]
 *
 * O byte ModRM diz os operandos: 2 bits de "modo" (11 = registrador; 00, 01
 * e 10 = memória sem deslocamento, com deslocamento de 1 byte ou de 4), 3
 * bits de um registrador (ou um pedaço do código da instrução) e 3 bits do
 * outro operando. Duas exceções do formato, que o processador impõe:
 *   - base ESP exige o byte SIB (0x24 = "só a base ESP");
 *   - modo 00 com base EBP significa "endereço fixo de 4 bytes"; por isso
 *     [ebp] é sempre escrito com deslocamento.
 *
 * Quando o endereço de um símbolo entra numa instrução, o montador ainda
 * não sabe onde o símbolo vai morar: ele deixa 4 bytes com o que deve ser
 * somado e anota uma relocação para o ligador completar. */
#include "montador.h"

static Secao *sec;
static u32    cap;

void m_inicio(Secao *s) { sec = s; cap = 0; }
u32  m_aqui(void) { return sec->tam; }

void m_byte(int b)
{
    if (sec->tam == cap) {
        u32 nova = cap ? cap * 2 : 256;
        u8 *n = novo(nova);
        if (sec->tam) copia_bytes(n, sec->bytes, sec->tam);
        sec->bytes = n;
        cap = nova;
    }
    sec->bytes[sec->tam++] = (u8)b;
}

void m_u32(u32 v)
{
    m_byte((int)(v & 0xFF)); m_byte((int)((v >> 8) & 0xFF)); m_byte((int)((v >> 16) & 0xFF)); m_byte((int)(v >> 24));
}

void m_remenda(u32 onde, u32 valor)
{
    sec->bytes[onde] = (u8)valor; sec->bytes[onde + 1] = (u8)(valor >> 8);
    sec->bytes[onde + 2] = (u8)(valor >> 16); sec->bytes[onde + 3] = (u8)(valor >> 24);
}

static void reloc(int sim, int tipo)
{
    sec->relocs = crescer(sec->relocs, &sec->cap_relocs, sec->qtd_relocs, sizeof(Reloc));
    sec->relocs[sec->qtd_relocs].desloc = sec->tam;
    sec->relocs[sec->qtd_relocs].sim = (u32)sim;
    sec->relocs[sec->qtd_relocs].tipo = (u8)tipo;
    sec->qtd_relocs++;
}

static int cabe_i8(i32 v) { return v >= -128 && v <= 127; }

static void modrm_rr(int campo, int rm) { m_byte(0xC0 | (campo << 3) | rm); }

/* ModRM (e SIB e deslocamento) de um operando na memória. */
static void modrm_mem(int campo, Mem m)
{
    if (m.base < 0) {
        m_byte((campo << 3) | 5);
        if (m.sim >= 0) reloc(m.sim, REL_32);
        m_u32((u32)m.desl);
        return;
    }
    if (m.desl == 0 && m.base != EBP) m_byte((campo << 3) | m.base);
    else if (cabe_i8(m.desl)) m_byte(0x40 | (campo << 3) | m.base);
    else m_byte(0x80 | (campo << 3) | m.base);
    if (m.base == ESP) m_byte(0x24);
    if (m.desl == 0 && m.base != EBP) return;
    if (cabe_i8(m.desl)) m_byte(m.desl & 0xFF);
    else m_u32((u32)m.desl);
}

void m_alu_rr(int op, int dst, int src) { m_byte(0x01 | (op << 3)); modrm_rr(src, dst); }
void m_alu_rm(int op, int reg, Mem m) { m_byte(0x03 | (op << 3)); modrm_mem(reg, m); }

void m_alu_ri(int op, int reg, i32 imm)
{
    if (cabe_i8(imm)) { m_byte(0x83); modrm_rr(op, reg); m_byte(imm & 0xFF); }
    else { m_byte(0x81); modrm_rr(op, reg); m_u32((u32)imm); }
}

void m_mov_rr(int dst, int src) { if (dst != src) { m_byte(0x89); modrm_rr(src, dst); } }

void m_mov_ri(int reg, i32 imm)
{
    if (imm == 0) { m_alu_rr(A_XOR, reg, reg); return; }    /* xor reg, reg: o zero mais curto */
    m_byte(0xB8 + reg);
    m_u32((u32)imm);
}

void m_mov_r_end(int reg, int sim, i32 desl)
{
    m_byte(0xB8 + reg);
    reloc(sim, REL_32);
    m_u32((u32)desl);
}

void m_mov_mi(int tipo, Mem m, i32 imm)
{
    if (tipo == RT_I8 || tipo == RT_U8) { m_byte(0xC6); modrm_mem(0, m); m_byte(imm & 0xFF); }
    else if (tipo == RT_I16 || tipo == RT_U16) { m_byte(0x66); m_byte(0xC7); modrm_mem(0, m); m_byte(imm & 0xFF); m_byte((imm >> 8) & 0xFF); }
    else { m_byte(0xC7); modrm_mem(0, m); m_u32((u32)imm); }
}

void m_carrega(int tipo, int reg, Mem m)
{
    switch (tipo) {
    case RT_I8: m_byte(0x0F); m_byte(0xBE); break;      /* movsx r32, m8 */
    case RT_U8: m_byte(0x0F); m_byte(0xB6); break;      /* movzx r32, m8 */
    case RT_I16: m_byte(0x0F); m_byte(0xBF); break;
    case RT_U16: m_byte(0x0F); m_byte(0xB7); break;
    default: m_byte(0x8B);
    }
    modrm_mem(reg, m);
}

void m_guarda(int tipo, Mem m, int reg)
{
    if (tipo == RT_I8 || tipo == RT_U8) m_byte(0x88);
    else if (tipo == RT_I16 || tipo == RT_U16) { m_byte(0x66); m_byte(0x89); }
    else m_byte(0x89);
    modrm_mem(reg, m);
}

void m_estende(int tipo, int reg)
{
    m_byte(0x0F);
    m_byte(tipo == RT_I8 ? 0xBE : tipo == RT_U8 ? 0xB6 : tipo == RT_I16 ? 0xBF : 0xB7);
    modrm_rr(reg, reg);
}

void m_lea(int reg, Mem m) { m_byte(0x8D); modrm_mem(reg, m); }
void m_imul_rr(int dst, int src) { m_byte(0x0F); m_byte(0xAF); modrm_rr(dst, src); }
void m_imul_rm(int reg, Mem m) { m_byte(0x0F); m_byte(0xAF); modrm_mem(reg, m); }

void m_imul_ri(int reg, i32 imm)
{
    if (cabe_i8(imm)) { m_byte(0x6B); modrm_rr(reg, reg); m_byte(imm & 0xFF); }
    else { m_byte(0x69); modrm_rr(reg, reg); m_u32((u32)imm); }
}

void m_grupo3(int sub, int reg) { m_byte(0xF7); modrm_rr(sub, reg); }
void m_desl_i(int sub, int reg, int n) { m_byte(0xC1); modrm_rr(sub, reg); m_byte(n & 31); }
void m_desl_cl(int sub, int reg) { m_byte(0xD3); modrm_rr(sub, reg); }
void m_cdq(void) { m_byte(0x99); }
void m_setcc(int cc, int reg) { m_byte(0x0F); m_byte(0x90 + cc); modrm_rr(0, reg); }
void m_test_rr(int a, int b) { m_byte(0x85); modrm_rr(b, a); }

u32 m_jmp(void) { m_byte(0xE9); m_u32(0); return sec->tam - 4; }
u32 m_jcc(int cc) { m_byte(0x0F); m_byte(0x80 + cc); m_u32(0); return sec->tam - 4; }

/* call com distância: o processador soma a distância ao endereço da
 * instrução SEGUINTE, 4 bytes depois do campo; daí o -4 deixado no lugar. */
void m_call_sim(int sim) { m_byte(0xE8); reloc(sim, REL_PC32); m_u32(0xFFFFFFFCu); }
void m_call_reg(int reg) { m_byte(0xFF); modrm_rr(2, reg); }

void m_ret(u32 desempilha)
{
    if (!desempilha) { m_byte(0xC3); return; }
    m_byte(0xC2); m_byte((int)(desempilha & 0xFF)); m_byte((int)(desempilha >> 8));
}

void m_push(int reg) { m_byte(0x50 + reg); }
void m_pop(int reg) { m_byte(0x58 + reg); }
void m_leave(void) { m_byte(0xC9); }
void m_rep(int instr) { m_byte(0xF3); m_byte(instr); }
