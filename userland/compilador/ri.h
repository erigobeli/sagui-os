/* ri.h - a representação intermediária (RI) do compilador do SaguiOS.
 *
 * É a fronteira entre as duas metades. A frente (que entende C) entrega um
 * "módulo": uma lista de símbolos, cada um uma função ou um dado. O fundo
 * (que entende x86 e ELF) só enxerga isto aqui: não sabe o que é um struct,
 * um typedef ou um "for". Uma linguagem nova só precisa de uma frente que
 * monte um módulo destes.
 *
 * Uma função é uma lista de instruções simples, cada uma com no máximo um
 * destino e dois operandos:
 *
 *     t3 = soma t1, 4          t5 = carrega.i8 [local 2 + 1]
 *     guarda.i32 [t3], t5      salta_se_zero t5, rotulo 7
 *
 * Os valores intermediários moram em "temporários" (t0, t1, ...), de 32 ou
 * de 64 bits. Tipos menores só existem na memória: carregar estende para 32
 * bits, guardar corta. As variáveis são "locais" numerados (a frente diz o
 * tamanho; o fundo decide onde ficam na pilha) e símbolos globais.
 *
 * Regra dos temporários: depois de receber um valor, um temporário só é
 * lido em instruções que vêm DEPOIS na lista (nunca por um salto para trás).
 * Com isso o fundo pode reaproveitar o lugar dele na pilha depois do último
 * uso, olhando só a ordem da lista. */
#ifndef SAGUI_RI_H
#define SAGUI_RI_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        i8;
typedef short              i16;
typedef int                i32;
typedef long long          i64;

/* Tipos de máquina. */
enum { RT_NADA, RT_I8, RT_U8, RT_I16, RT_U16, RT_I32, RT_I64, RT_BLOCO };

/* Operandos. Um operando vale um número: O_LOCAL e O_GLOBAL valem o
 * ENDEREÇO do local ou do símbolo (mais um deslocamento). */
enum { O_NADA, O_TEMP, O_CONST, O_LOCAL, O_GLOBAL };
typedef struct {
    u8  classe;
    i32 n;      /* número do temporário, do local ou do símbolo */
    i64 c;      /* O_CONST: o valor; O_LOCAL/O_GLOBAL: o deslocamento */
} Oper;

enum {
    RI_NADA,
    RI_MOVE,            /* dst = a */
    RI_CARREGA,         /* dst = memória[a]         (tipo = o que há na memória) */
    RI_GUARDA,          /* memória[a] = b           (tipo = o que vai para a memória) */
    /* contas: tipo RT_I32 ou RT_I64 (o tamanho dos operandos) */
    RI_SOMA, RI_SUB, RI_MUL, RI_DIV_S, RI_DIV_U, RI_RESTO_S, RI_RESTO_U,
    RI_E, RI_OU, RI_XOU, RI_DESL_E, RI_DESL_D_S, RI_DESL_D_U,
    /* comparações: o destino é sempre de 32 bits, 0 ou 1 */
    RI_IGUAL, RI_DIFERENTE, RI_MENOR_S, RI_MENOR_IG_S, RI_MENOR_U, RI_MENOR_IG_U,
    RI_NEG, RI_INVERTE,
    RI_ESTENDE,         /* dst(32) = os bits de baixo de a, estendidos (tipo I8, U8, I16 ou U16) */
    RI_ALARGA_S,        /* dst(64) = a(32) com sinal */
    RI_ALARGA_U,        /* dst(64) = a(32) sem sinal */
    RI_ESTREITA,        /* dst(32) = a(64) cortado */
    RI_ROTULO,          /* x = número do rótulo */
    RI_SALTA,           /* x = rótulo */
    RI_SALTA_SE_ZERO,   /* a (32 bits), x = rótulo */
    RI_SALTA_SE_NAO_ZERO,
    RI_CHAMA,           /* dst = a(argumentos x .. x+y-1); tipo = o que volta */
    RI_RETORNA,         /* a (ou nada); tipo */
    RI_COPIA,           /* x bytes de memória[b] para memória[a] */
    RI_ZERA,            /* x bytes em memória[a] */
    RI_QTD
};

typedef struct {
    u8   op, tipo;
    u8   memoria;       /* RI_CHAMA: o resultado volta por um ponteiro escondido (1º argumento) */
    i32  dst;           /* temporário, ou -1 */
    Oper a, b;
    i32  x, y;
} Instr;

typedef struct {
    Oper v;
    u8   tipo;          /* RT_I32, RT_I64 ou RT_BLOCO (v = endereço do bloco) */
    u32  tam;           /* RT_BLOCO: bytes */
} ArgRI;

typedef struct {
    u32 tam, alinha;
    i32 parametro;      /* >= 0: é um parâmetro, a esta distância do primeiro; -1: variável */
} LocalRI;

typedef struct {
    Instr   *ins;    int qtd, cap;
    u8      *larg;   int qtd_temps, cap_temps;     /* largura de cada temporário: 4 ou 8 */
    LocalRI *locais; int qtd_locais, cap_locais;
    ArgRI   *args;   int qtd_args, cap_args;
    int      qtd_rotulos;
    u32      desempilha;    /* bytes que a função tira da pilha ao voltar (4 se devolve bloco) */
} FuncaoRI;

typedef struct { u32 desloc; i32 sim; } RelocRI;   /* 4 bytes em "desloc" += endereço de sim */

enum { SR_INDEFINIDO, SR_FUNCAO, SR_DADO };
typedef struct {
    const char *nome;
    u8   especie;
    u8   global;            /* visível para os outros arquivos e para a libsagui */
    u8   so_leitura;        /* dado que não muda (textos, const) */
    u32  tam, alinha;       /* dado */
    u8  *bytes;             /* NULL = tudo zero */
    RelocRI *relocs; int qtd_relocs, cap_relocs;
    FuncaoRI *funcao;
} SimboloRI;

typedef struct {
    SimboloRI *sims; int qtd, cap;
} ModuloRI;

#endif
