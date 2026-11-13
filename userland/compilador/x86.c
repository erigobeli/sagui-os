/* x86.c - o fundo do compilador: da representação intermediária para as
 * instruções do x86 de 32 bits.
 *
 * O plano é o mais simples que funciona, sem tentar ser esperto:
 *
 *   - cada temporário da RI mora numa casa da pilha da função; cada
 *     instrução da RI traz os operandos para EAX (e ECX/EDX quando precisa),
 *     faz a conta e guarda o resultado na casa do destino. Não há "alocação
 *     de registradores": EAX, ECX e EDX são rascunho, e os registradores que
 *     a convenção manda preservar (EBX, ESI, EDI) quase não são tocados;
 *   - a casa de um temporário é devolvida depois do último uso e serve para
 *     outro (a regra dos temporários em ri.h garante que a ordem da lista
 *     basta para saber qual é o último uso);
 *   - dois atalhos tiram a maior parte do desperdício: (1) se um resultado
 *     é usado uma vez só, e logo pela instrução seguinte, ele fica em EAX e
 *     nem vai para a pilha; (2) uma comparação seguida do salto que a testa
 *     vira "cmp + jcc" direto.
 *
 * Quadro de uma função (a pilha cresce para baixo):
 *
 *     [ebp+8...]   parâmetros (postos por quem chamou)
 *     [ebp+4]      endereço de volta
 *     [ebp]        EBP de quem chamou
 *     [ebp-...]    variáveis locais, depois as casas dos temporários
 *     [esp...]     área dos argumentos das chamadas que a função faz
 *
 * A área dos argumentos é reservada uma vez, no começo: chamar uma função é
 * escrever os argumentos em [esp], [esp+4]... e dar o call, sem push nem
 * acerto de pilha depois. O tamanho do quadro é arredondado para a pilha
 * ficar alinhada em 16 bytes em toda chamada, como o GCC espera.
 *
 * Convenção de chamada: cdecl do System V para i386 (a mesma da libsagui):
 * argumentos na pilha, da direita para a esquerda; resultado em EAX (64
 * bits: EDX:EAX); struct devolvido num endereço que quem chama passa
 * escondido como primeiro argumento (e a função chamada tira da pilha).
 *
 * Contas de 64 bits: soma, subtração, comparação e bits são feitas aqui, em
 * duas metades. Multiplicação, divisão e deslocamento chamam rotinas da
 * libsagui (longo64.c). */
#include "montador.h"

static FuncaoRI *f;
static i32  *casa;          /* por temporário: distância de EBP, ou 0 = sem casa */
static int  *ultimo;        /* por temporário: índice da última instrução que o cita */
static u16  *usos;          /* por temporário: quantas vezes é lido */
static i32  *onde_local;    /* por local: distância de EBP */
static u32   usado;         /* bytes do quadro abaixo de EBP já ocupados */
static i32  *livres4, *livres8;
static int   n_livres4, n_livres8;
static int   em_eax;        /* temporário que está em EAX e não foi para a pilha, ou -1 */
static u32  *pos_rotulo;
static struct Remendo { u32 onde; int rotulo; } *remendos;
static int   n_remendos, cap_remendos;
static u32   max_args;
static int   ajudas[8];     /* símbolos das rotinas de 64 bits */

enum { AJ_MUL, AJ_DIV, AJ_DIV_U, AJ_RESTO, AJ_RESTO_U, AJ_DESL_E, AJ_DESL_D, AJ_DESL_D_U };

static Mem mem(int base, i32 desl) { Mem m; m.base = base; m.desl = desl; m.sim = -1; return m; }

static i32 casa_de(int t)
{
    if (!casa[t]) {
        if (f->larg[t] == 8 && n_livres8) casa[t] = livres8[--n_livres8];
        else if (f->larg[t] == 4 && n_livres4) casa[t] = livres4[--n_livres4];
        else { usado += f->larg[t]; casa[t] = -(i32)usado; }
    }
    return casa[t];
}

static void solta(Oper o, int idx)
{
    int t = o.n;
    if (o.classe != O_TEMP || ultimo[t] != idx || !casa[t]) return;
    if (f->larg[t] == 8) livres8[n_livres8++] = casa[t];
    else livres4[n_livres4++] = casa[t];
    casa[t] = 0;
}

static Mem mem_temp(int t, int parte) { return mem(EBP, casa_de(t) + 4 * parte); }

/* Traz para um registrador o valor de 32 bits de um operando (ou a metade
 * "parte" de um de 64 bits). */
static void carrega(int reg, Oper o, int parte)
{
    switch (o.classe) {
    case O_CONST: m_mov_ri(reg, (i32)((u64)o.c >> (32 * parte))); break;
    case O_TEMP:
        if (o.n == em_eax) { em_eax = -1; m_mov_rr(reg, EAX); break; }
        m_carrega(RT_I32, reg, mem_temp(o.n, parte));
        break;
    case O_LOCAL: m_lea(reg, mem(EBP, onde_local[o.n] + (i32)o.c)); break;
    case O_GLOBAL: m_mov_r_end(reg, o.n, (i32)o.c); break;
    }
}

/* O lugar da memória para onde um operando aponta. */
static Mem endereco(Oper o, int reg)
{
    Mem m;
    switch (o.classe) {
    case O_LOCAL: return mem(EBP, onde_local[o.n] + (i32)o.c);
    case O_GLOBAL: m.base = -1; m.desl = (i32)o.c; m.sim = o.n; return m;
    case O_CONST: return mem(-1, (i32)o.c);
    }
    carrega(reg, o, 0);
    return mem(reg, 0);
}

/* reg = reg (conta) operando b. */
static void alu_b(int op, int reg, Oper b, int parte)
{
    if (b.classe == O_CONST) m_alu_ri(op, reg, (i32)((u64)b.c >> (32 * parte)));
    else if (b.classe == O_TEMP) m_alu_rm(op, reg, mem_temp(b.n, parte));
    else { carrega(ECX, b, 0); m_alu_rr(op, reg, ECX); }
}

static void carrega64(Oper o) { carrega(EAX, o, 0); carrega(EDX, o, 1); }

/* A próxima instrução começa trazendo o temporário t para EAX (e não o usa
 * em mais nenhum lugar)? Então ele pode esperar lá. */
static int pega_de_eax(Instr *p, int t)
{
    Oper a = p->a, b = p->b;
    int eh_a = a.classe == O_TEMP && a.n == t, eh_b = b.classe == O_TEMP && b.n == t;
    switch (p->op) {
    case RI_GUARDA:
        return p->tipo != RT_I64 && eh_b && !eh_a;
    case RI_MOVE: case RI_NEG: case RI_INVERTE:
        return p->tipo == RT_I32 && eh_a;
    case RI_SOMA: case RI_SUB: case RI_MUL: case RI_DIV_S: case RI_DIV_U: case RI_RESTO_S: case RI_RESTO_U:
    case RI_E: case RI_OU: case RI_XOU: case RI_DESL_E: case RI_DESL_D_S: case RI_DESL_D_U:
    case RI_IGUAL: case RI_DIFERENTE: case RI_MENOR_S: case RI_MENOR_IG_S: case RI_MENOR_U: case RI_MENOR_IG_U:
        return p->tipo == RT_I32 && eh_a && !eh_b;
    case RI_CARREGA: case RI_ESTENDE: case RI_ALARGA_S: case RI_ALARGA_U: case RI_SALTA_SE_ZERO: case RI_SALTA_SE_NAO_ZERO:
        return eh_a;
    case RI_RETORNA:
        return p->tipo == RT_I32 && eh_a;
    }
    return 0;
}

/* O resultado de 32 bits está em EAX: vai para a casa do destino, ou fica. */
static void guarda32(int idx)
{
    Instr *i = &f->ins[idx];
    int t = i->dst;
    if (t < 0) return;
    if (usos[t] == 1 && !casa[t] && idx + 1 < f->qtd && pega_de_eax(&f->ins[idx + 1], t)) { em_eax = t; return; }
    if (usos[t] == 0 && !casa[t]) return;       /* ninguém vai ler */
    m_guarda(RT_I32, mem_temp(t, 0), EAX);
}

static void guarda64(int idx)
{
    int t = f->ins[idx].dst;
    if (t < 0) return;
    m_guarda(RT_I32, mem_temp(t, 0), EAX);
    m_guarda(RT_I32, mem_temp(t, 1), EDX);
}

static void salto_para(u32 onde, int rotulo)
{
    remendos = crescer(remendos, &cap_remendos, n_remendos, sizeof *remendos);
    remendos[n_remendos].onde = onde;
    remendos[n_remendos].rotulo = rotulo;
    n_remendos++;
}

/* Copia (ou zera) n bytes com as instruções de repetição do x86. O endereço
 * de destino já está em EDI (e o de origem em ESI); o contador é ECX. */
static void repete(u32 n, int de_quatro, int de_um)
{
    if (n >= 4) { m_mov_ri(ECX, (i32)(n / 4)); m_rep(de_quatro); }
    if (n & 3) { m_mov_ri(ECX, (i32)(n & 3)); m_rep(de_um); }
}

static int condicao(int op)
{
    switch (op) {
    case RI_IGUAL: return C_E;
    case RI_DIFERENTE: return C_NE;
    case RI_MENOR_S: return C_L;
    case RI_MENOR_IG_S: return C_LE;
    case RI_MENOR_U: return C_B;
    }
    return C_BE;
}

static void chamada(int idx)
{
    Instr *i = &f->ins[idx];
    u32 desl = 0;
    int k;
    for (k = 0; k < i->y; k++) {
        ArgRI *a = &f->args[i->x + k];
        if (a->tipo == RT_BLOCO) {
            /* struct passado por valor: uma cópia dele na área de argumentos */
            m_push(ESI); m_push(EDI);
            carrega(ESI, a->v, 0);
            m_lea(EDI, mem(ESP, (i32)desl + 8));
            repete(a->tam, 0xA5, 0xA4);
            m_pop(EDI); m_pop(ESI);
            desl += (a->tam + 3) & ~3u;
        } else if (a->tipo == RT_I64) {
            carrega64(a->v);
            m_guarda(RT_I32, mem(ESP, (i32)desl), EAX);
            m_guarda(RT_I32, mem(ESP, (i32)desl + 4), EDX);
            desl += 8;
        } else {
            if (a->v.classe == O_CONST) m_mov_mi(RT_I32, mem(ESP, (i32)desl), (i32)a->v.c);
            else { carrega(EAX, a->v, 0); m_guarda(RT_I32, mem(ESP, (i32)desl), EAX); }
            desl += 4;
        }
    }
    if (desl > max_args) max_args = desl;
    if (i->a.classe == O_GLOBAL && i->a.c == 0) m_call_sim(i->a.n);
    else { carrega(EAX, i->a, 0); m_call_reg(EAX); }
    if (i->memoria) m_alu_ri(A_SUB, ESP, 4);    /* a função tirou o ponteiro escondido; a área volta ao lugar */
    if (i->tipo == RT_I64) guarda64(idx);
    else if (i->tipo == RT_I32) guarda32(idx);
}

/* Conta de 64 bits feita por uma rotina da libsagui: (a, b) -> EDX:EAX. */
static void ajuda64(int qual, Instr *i, int b_curto)
{
    carrega64(i->a);
    m_guarda(RT_I32, mem(ESP, 0), EAX);
    m_guarda(RT_I32, mem(ESP, 4), EDX);
    carrega(EAX, i->b, 0);
    m_guarda(RT_I32, mem(ESP, 8), EAX);
    if (!b_curto) {
        carrega(EAX, i->b, 1);
        m_guarda(RT_I32, mem(ESP, 12), EAX);
    }
    if (max_args < 16) max_args = 16;
    m_call_sim(ajudas[qual]);
}

static void instrucao64(int idx)
{
    Instr *i = &f->ins[idx];
    switch (i->op) {
    case RI_SOMA: carrega64(i->a); alu_b(A_ADD, EAX, i->b, 0); alu_b(A_ADC, EDX, i->b, 1); break;
    case RI_SUB: carrega64(i->a); alu_b(A_SUB, EAX, i->b, 0); alu_b(A_SBB, EDX, i->b, 1); break;
    case RI_E: carrega64(i->a); alu_b(A_AND, EAX, i->b, 0); alu_b(A_AND, EDX, i->b, 1); break;
    case RI_OU: carrega64(i->a); alu_b(A_OR, EAX, i->b, 0); alu_b(A_OR, EDX, i->b, 1); break;
    case RI_XOU: carrega64(i->a); alu_b(A_XOR, EAX, i->b, 0); alu_b(A_XOR, EDX, i->b, 1); break;
    case RI_MUL: ajuda64(AJ_MUL, i, 0); break;
    case RI_DIV_S: ajuda64(AJ_DIV, i, 0); break;
    case RI_DIV_U: ajuda64(AJ_DIV_U, i, 0); break;
    case RI_RESTO_S: ajuda64(AJ_RESTO, i, 0); break;
    case RI_RESTO_U: ajuda64(AJ_RESTO_U, i, 0); break;
    case RI_DESL_E: ajuda64(AJ_DESL_E, i, 1); break;
    case RI_DESL_D_S: ajuda64(AJ_DESL_D, i, 1); break;
    case RI_DESL_D_U: ajuda64(AJ_DESL_D_U, i, 1); break;
    case RI_NEG:
        /* -x em duas metades: nega a de baixo, soma o "vai um" na de cima e nega */
        carrega64(i->a);
        m_grupo3(3, EAX);
        m_alu_ri(A_ADC, EDX, 0);
        m_grupo3(3, EDX);
        break;
    case RI_INVERTE: carrega64(i->a); m_grupo3(2, EAX); m_grupo3(2, EDX); break;
    case RI_IGUAL: case RI_DIFERENTE:
        carrega64(i->a);
        alu_b(A_XOR, EAX, i->b, 0);
        alu_b(A_XOR, EDX, i->b, 1);
        m_alu_rr(A_OR, EAX, EDX);
        m_setcc(i->op == RI_IGUAL ? C_E : C_NE, EAX);
        m_estende(RT_U8, EAX);
        guarda32(idx);
        return;
    case RI_MENOR_S: case RI_MENOR_U:
        /* a < b: subtrai as duas metades em cadeia e olha o "emprestou" final */
        carrega64(i->a);
        alu_b(A_CMP, EAX, i->b, 0);
        alu_b(A_SBB, EDX, i->b, 1);
        m_setcc(i->op == RI_MENOR_S ? C_L : C_B, EAX);
        m_estende(RT_U8, EAX);
        guarda32(idx);
        return;
    case RI_MENOR_IG_S: case RI_MENOR_IG_U:
        /* a <= b é o contrário de b < a */
        carrega64(i->b);
        alu_b(A_CMP, EAX, i->a, 0);
        alu_b(A_SBB, EDX, i->a, 1);
        m_setcc(i->op == RI_MENOR_IG_S ? C_GE : C_AE, EAX);
        m_estende(RT_U8, EAX);
        guarda32(idx);
        return;
    }
    guarda64(idx);
}

/* Devolve quantas instruções da RI consumiu (2 quando junta comparação e salto). */
static int instrucao(int idx)
{
    Instr *i = &f->ins[idx];
    Mem m;
    int sub;
    switch (i->op) {
    case RI_ROTULO: pos_rotulo[i->x] = m_aqui(); return 1;
    case RI_SALTA: salto_para(m_jmp(), i->x); return 1;
    case RI_SALTA_SE_ZERO: case RI_SALTA_SE_NAO_ZERO:
        carrega(EAX, i->a, 0);
        m_test_rr(EAX, EAX);
        salto_para(m_jcc(i->op == RI_SALTA_SE_ZERO ? C_E : C_NE), i->x);
        return 1;
    case RI_MOVE:
        if (i->tipo == RT_I64) { carrega64(i->a); guarda64(idx); }
        else { carrega(EAX, i->a, 0); guarda32(idx); }
        return 1;
    case RI_CARREGA:
        m = endereco(i->a, EAX);
        if (i->tipo == RT_I64) {
            m.desl += 4;
            m_carrega(RT_I32, EDX, m);      /* a metade de cima primeiro: EAX pode ser a base */
            m.desl -= 4;
            m_carrega(RT_I32, EAX, m);
            guarda64(idx);
        } else {
            m_carrega(i->tipo, EAX, m);
            guarda32(idx);
        }
        return 1;
    case RI_GUARDA:
        if (i->tipo == RT_I64) {
            carrega64(i->b);
            m = endereco(i->a, ECX);
            m_guarda(RT_I32, m, EAX);
            m.desl += 4;
            m_guarda(RT_I32, m, EDX);
        } else if (i->b.classe == O_CONST) {
            m = endereco(i->a, ECX);
            m_mov_mi(i->tipo, m, (i32)i->b.c);
        } else {
            carrega(EAX, i->b, 0);
            m = endereco(i->a, ECX);
            m_guarda(i->tipo, m, EAX);
        }
        return 1;
    case RI_CHAMA: chamada(idx); return 1;
    case RI_RETORNA:
        if (i->tipo == RT_I64) carrega64(i->a);
        else if (i->tipo != RT_NADA) carrega(EAX, i->a, 0);
        m_leave();
        m_ret(f->desempilha);
        return 1;
    case RI_COPIA:
        m_push(ESI); m_push(EDI);
        carrega(EDI, i->a, 0);
        carrega(ESI, i->b, 0);
        repete((u32)i->x, 0xA5, 0xA4);
        m_pop(EDI); m_pop(ESI);
        return 1;
    case RI_ZERA:
        m_push(EDI);
        carrega(EDI, i->a, 0);
        m_mov_ri(EAX, 0);
        repete((u32)i->x, 0xAB, 0xAA);
        m_pop(EDI);
        return 1;
    case RI_ESTENDE:
        carrega(EAX, i->a, 0);
        m_estende(i->tipo, EAX);
        guarda32(idx);
        return 1;
    case RI_ALARGA_S: carrega(EAX, i->a, 0); m_cdq(); guarda64(idx); return 1;
    case RI_ALARGA_U: carrega(EAX, i->a, 0); m_mov_ri(EDX, 0); guarda64(idx); return 1;
    case RI_ESTREITA: carrega(EAX, i->a, 0); guarda32(idx); return 1;
    }
    if (i->tipo == RT_I64) { instrucao64(idx); return 1; }

    carrega(EAX, i->a, 0);
    switch (i->op) {
    case RI_SOMA: alu_b(A_ADD, EAX, i->b, 0); break;
    case RI_SUB: alu_b(A_SUB, EAX, i->b, 0); break;
    case RI_E: alu_b(A_AND, EAX, i->b, 0); break;
    case RI_OU: alu_b(A_OR, EAX, i->b, 0); break;
    case RI_XOU: alu_b(A_XOR, EAX, i->b, 0); break;
    case RI_MUL:
        if (i->b.classe == O_CONST) m_imul_ri(EAX, (i32)i->b.c);
        else if (i->b.classe == O_TEMP) m_imul_rm(EAX, mem_temp(i->b.n, 0));
        else { carrega(ECX, i->b, 0); m_imul_rr(EAX, ECX); }
        break;
    case RI_DIV_S: case RI_RESTO_S:
        carrega(ECX, i->b, 0);
        m_cdq();                    /* EDX:EAX = EAX com sinal */
        m_grupo3(7, ECX);
        if (i->op == RI_RESTO_S) m_mov_rr(EAX, EDX);
        break;
    case RI_DIV_U: case RI_RESTO_U:
        carrega(ECX, i->b, 0);
        m_mov_ri(EDX, 0);
        m_grupo3(6, ECX);
        if (i->op == RI_RESTO_U) m_mov_rr(EAX, EDX);
        break;
    case RI_DESL_E: case RI_DESL_D_S: case RI_DESL_D_U:
        sub = i->op == RI_DESL_E ? 4 : i->op == RI_DESL_D_U ? 5 : 7;
        if (i->b.classe == O_CONST) m_desl_i(sub, EAX, (int)i->b.c);
        else { carrega(ECX, i->b, 0); m_desl_cl(sub, EAX); }
        break;
    case RI_NEG: m_grupo3(3, EAX); break;
    case RI_INVERTE: m_grupo3(2, EAX); break;
    case RI_IGUAL: case RI_DIFERENTE: case RI_MENOR_S: case RI_MENOR_IG_S: case RI_MENOR_U: case RI_MENOR_IG_U: {
        Instr *p = idx + 1 < f->qtd ? &f->ins[idx + 1] : NULL;
        int cc = condicao(i->op);
        alu_b(A_CMP, EAX, i->b, 0);
        if (p && i->dst >= 0 && usos[i->dst] == 1 && (p->op == RI_SALTA_SE_ZERO || p->op == RI_SALTA_SE_NAO_ZERO) &&
            p->a.classe == O_TEMP && p->a.n == i->dst) {
            salto_para(m_jcc(p->op == RI_SALTA_SE_NAO_ZERO ? cc : cc ^ 1), p->x);
            return 2;
        }
        m_setcc(cc, EAX);
        m_estende(RT_U8, EAX);
        break;
    }
    }
    guarda32(idx);
    return 1;
}

static void conta_uso(Oper o, int idx)
{
    if (o.classe != O_TEMP) return;
    usos[o.n]++;
    ultimo[o.n] = idx;
}

static void funcao(SimboloRI *s, Secao *sec)
{
    int i, k, n;
    u32 remenda_quadro, quadro;
    f = s->funcao;
    n = f->qtd_temps + 1;
    casa = novo(sizeof(i32) * (u32)n);
    ultimo = novo(sizeof(int) * (u32)n);
    usos = novo(sizeof(u16) * (u32)n);
    livres4 = novo(sizeof(i32) * (u32)n);
    livres8 = novo(sizeof(i32) * (u32)n);
    onde_local = novo(sizeof(i32) * (u32)(f->qtd_locais + 1));
    pos_rotulo = novo(sizeof(u32) * (u32)(f->qtd_rotulos + 1));
    n_livres4 = n_livres8 = n_remendos = 0;
    remendos = NULL;
    cap_remendos = 0;
    usado = 0;
    max_args = 0;
    em_eax = -1;

    for (i = 0; i < f->qtd; i++) {
        Instr *in = &f->ins[i];
        conta_uso(in->a, i);
        conta_uso(in->b, i);
        if (in->op == RI_CHAMA)
            for (k = 0; k < in->y; k++) conta_uso(f->args[in->x + k].v, i);
        if (in->dst >= 0 && ultimo[in->dst] < i) ultimo[in->dst] = i;
    }
    for (i = 0; i < f->qtd_locais; i++) {
        LocalRI *l = &f->locais[i];
        if (l->parametro >= 0) { onde_local[i] = 8 + l->parametro; continue; }
        usado = (usado + l->tam + 3) & ~3u;
        onde_local[i] = -(i32)usado;
    }

    m_inicio(sec);
    m_push(EBP);
    m_mov_rr(EBP, ESP);
    m_byte(0x81); m_byte(0xEC);         /* sub esp, (tamanho do quadro, posto no fim) */
    remenda_quadro = m_aqui();
    m_u32(0);

    for (i = 0; i < f->qtd; ) {
        int passo = instrucao(i), j;
        for (j = i; j < i + passo; j++) {
            Instr *x = &f->ins[j];
            solta(x->a, j);
            solta(x->b, j);
            if (x->op == RI_CHAMA)
                for (k = 0; k < x->y; k++) solta(f->args[x->x + k].v, j);
            if (x->dst >= 0 && ultimo[x->dst] == j && casa[x->dst]) {
                Oper d;
                d.classe = O_TEMP; d.n = x->dst; d.c = 0;
                solta(d, j);
            }
        }
        i += passo;
    }
    /* A lista sempre termina num retorna (a frente garante). */
    quadro = usado + max_args;
    quadro = ((quadro + 8 + 15) & ~15u) - 8;        /* ESP múltiplo de 16 depois do "push ebp" e do "sub" */
    m_remenda(remenda_quadro, quadro);
    for (i = 0; i < n_remendos; i++)
        m_remenda(remendos[i].onde, pos_rotulo[remendos[i].rotulo] - (remendos[i].onde + 4));
}

Objeto *x86_gerar(ModuloRI *m, const char *nome)
{
    static const char *NOMES[8] = { "__sagui_mul64", "__sagui_div64", "__sagui_divu64", "__sagui_resto64",
                                    "__sagui_restou64", "__sagui_desle64", "__sagui_desld64", "__sagui_deslu64" };
    Objeto *o = novo(sizeof *o);
    int i, k;
    for (i = 0; i < 8; i++) ajudas[i] = ri_simbolo(NOMES[i]);      /* antes de tudo: a lista de símbolos não muda mais */
    o->nome = nome;
    o->qtd_sims = m->qtd;
    o->sims = novo(sizeof(SimLig) * (u32)(m->qtd + 1));
    o->secoes = novo(sizeof(Secao) * (u32)(m->qtd + 1));
    for (i = 0; i < m->qtd; i++) {
        SimboloRI *s = &m->sims[i];
        SimLig *l = &o->sims[i];
        Secao *sec;
        l->nome = s->nome;
        l->global = s->global;
        if (s->especie == SR_INDEFINIDO) continue;
        sec = &o->secoes[o->qtd_secoes++];
        sec->nome = s->nome;
        sec->obj = o;
        l->secao = sec;
        if (s->especie == SR_FUNCAO) {
            sec->classe = SEC_TEXTO;
            sec->alinha = 4;
            funcao(s, sec);
            continue;
        }
        sec->classe = (u8)(!s->bytes ? SEC_ZERADA : s->so_leitura ? SEC_LEITURA : SEC_DADOS);
        sec->alinha = s->alinha ? s->alinha : 1;
        sec->tam = s->tam;
        sec->bytes = s->bytes;
        if (s->qtd_relocs) {
            sec->relocs = novo(sizeof(Reloc) * (u32)s->qtd_relocs);
            sec->qtd_relocs = s->qtd_relocs;
            for (k = 0; k < s->qtd_relocs; k++) {
                sec->relocs[k].desloc = s->relocs[k].desloc;
                sec->relocs[k].sim = (u32)s->relocs[k].sim;
                sec->relocs[k].tipo = REL_32;
            }
        }
    }
    return o;
}
