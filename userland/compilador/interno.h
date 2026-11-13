/* interno.h - o que as partes do compilador dividem entre si. */
#ifndef SAGUI_CC_INTERNO_H
#define SAGUI_CC_INTERNO_H
#include "compilador.h"
#include "ri.h"

#ifndef NULL
#define NULL ((void *)0)
#endif
typedef __builtin_va_list cc_lista;

/* ------------------------------------------------------------ base.c */

/* Onde um pedaço do programa está: arquivo, linha e coluna (contadas de 1). */
typedef struct { u16 arq; u16 col; u32 lin; } Pos;

typedef struct {
    const char *nome;
    u8  *dados;
    u32  tam;
    u8   uma_vez;       /* #pragma once */
} Arquivo;

typedef struct Macro Macro;
typedef struct Simbolo Simbolo;
typedef struct Etiqueta Etiqueta;

/* Cada nome que aparece no programa é guardado uma vez só; comparar dois
 * nomes vira comparar dois ponteiros, e o próprio nome carrega o que ele
 * significa agora (macro, variável, struct). */
typedef struct Nome {
    struct Nome *prox;      /* mesma gaveta da tabela */
    const char  *texto;
    u32          tam;
    u8           chave;     /* palavra reservada (CH_*), ou 0 */
    Macro       *macro;
    Simbolo     *sim;       /* variável, função, typedef ou constante de enum */
    Etiqueta    *etiqueta;  /* struct, union ou enum */
    struct SimLig *lig;     /* no ligador: quem define este nome */
} Nome;

void *novo(u32 n);                              /* memória zerada, devolvida toda no fim */
void *crescer(void *p, int *cap, int qtd, u32 tam_item);   /* garante lugar para mais um item */
Nome *nome_de(const char *t, u32 n);
Nome *nome_c(const char *t);
u32   tam_txt(const char *t);
int   txt_igual(const char *a, const char *b);
void  copia_bytes(void *d, const void *s, u32 n);
void  zera_bytes(void *d, u32 n);
char *copia_txt(const char *t, u32 n);
int   formatar(char *dst, u32 max, const char *fmt, ...);
int   formatar_v(char *dst, u32 max, const char *fmt, cc_lista ap);
u64   div_u64(u64 a, u64 b, u64 *resto);       /* sem a libgcc: divisão longa, bit a bit */
i64   div_s64(i64 a, i64 b, i64 *resto);
int   novo_arquivo(const char *nome, u8 *dados, u32 tam);
int   ler_arquivo(const char *caminho, u8 **dados, u32 *tam);

/* Mostra o erro (arquivo:linha:coluna, a linha, o ^ e a dica) e abandona a
 * compilação. Nunca volta. "dica" pode ser NULL. No formato, %T é um tipo e
 * %N um nome. */
void erro(Pos p, const char *dica, const char *fmt, ...) __attribute__((noreturn));
void erro_geral(const char *dica, const char *fmt, ...) __attribute__((noreturn));

/* ------------------------------------------------------------ lexico.c */

enum {
    T_FIM, T_NOME, T_NUM, T_REAL, T_TEXTO, T_PONT, T_LIXO, T_MARCA
};
/* Pontuação de mais de um caractere (a de um só é o próprio caractere). */
enum {
    P_SETA = 256, P_INC, P_DEC, P_DESL_E, P_DESL_D, P_MENOR_IG, P_MAIOR_IG, P_IGUAL, P_DIF,
    P_E_LOG, P_OU_LOG, P_RETIC, P_COLA,
    P_ATR_SOMA, P_ATR_SUB, P_ATR_MUL, P_ATR_DIV, P_ATR_RESTO, P_ATR_E, P_ATR_OU, P_ATR_XOU,
    P_ATR_DESL_E, P_ATR_DESL_D
};

#define TF_INICIO_LINHA 1   /* primeiro da linha */
#define TF_ESPACO       2   /* havia espaço antes */
#define TF_NAO_EXPANDE  4   /* nome de macro que não pode mais ser trocado */
#define TF_SEM_SINAL    8   /* número com sufixo u */
#define TF_LONGO        16  /* número com sufixo ll */
#define TF_HEXA         32  /* número escrito em hexadecimal ou octal */
#define TF_CARACTERE    64  /* 'a' */

typedef struct {
    u8   tipo, marcas;
    u16  pont;          /* T_PONT */
    Pos  pos;
    Nome *nome;         /* T_NOME */
    i64  valor;         /* T_NUM */
    const char *texto;  /* T_TEXTO: bytes já em Latin-1; T_LIXO: nada */
    u32  tam;
    u16  larg;          /* quantas colunas ocupa no arquivo */
} Token;

typedef struct {
    int  arq;
    u32  p, lin, col;
    u8   inicio;        /* ainda não veio nada nesta linha */
} Leitor;

void lex_abrir(Leitor *l, int arq);
void lex_proximo(Leitor *l, Token *t);
int  lex_resto_include(Leitor *l, char *destino, u32 max);   /* <arquivo.h> depois de #include */
const char *texto_pont(int p);

/* ------------------------------------------------------------ preproc.c */

void pp_iniciar(void);
void pp_arquivo(const char *caminho);     /* pré-processa e junta os tokens em G.toks */

/* ------------------------------------------------------------ tipos.c */

enum {
    TP_VOID, TP_BOOL, TP_CHAR, TP_SCHAR, TP_UCHAR, TP_SHORT, TP_USHORT, TP_INT, TP_UINT,
    TP_LONGO, TP_ULONGO, TP_PONTEIRO, TP_VETOR, TP_FUNCAO, TP_STRUCT, TP_UNIAO
};

typedef struct Tipo Tipo;
typedef struct Campo {
    struct Campo *prox;
    Nome *nome;         /* NULL: struct ou union sem nome, os campos de dentro valem aqui */
    Tipo *tipo;
    u32   desloc;
} Campo;

typedef struct { Nome *nome; Tipo *tipo; Pos pos; } Param;

struct Tipo {
    u8    classe;
    u8    constante;    /* const */
    u8    completo;     /* struct/union com os campos; vetor com tamanho */
    u8    variadica;    /* função com ... */
    u8    sem_lista;    /* função declarada com (): aceita qualquer argumento */
    u8    eh_enum;
    u32   tam, alinha;
    Tipo *base;         /* ponteiro: para o quê; vetor: de quê; função: o que devolve */
    Tipo *puro;         /* o mesmo tipo sem const (ele mesmo, se não tem) */
    u32   n;            /* vetor: quantos */
    Campo *campos;
    Param *params; int qtd_params;
    Nome *etiqueta;
    Nome *apelido;      /* struct sem nome que ganhou um typedef: o nome do typedef, para as mensagens */
};

extern Tipo *t_void, *t_bool, *t_char, *t_schar, *t_uchar, *t_short, *t_ushort, *t_int, *t_uint,
            *t_longo, *t_ulongo, *t_ptr_char, *t_ptr_void;

void  tipos_iniciar(void);
Tipo *tp_ponteiro(Tipo *base);
Tipo *tp_vetor(Tipo *base, u32 n, int completo);
Tipo *tp_funcao(Tipo *devolve);
Tipo *tp_agregado(int classe, Nome *etiqueta);
Tipo *tp_const(Tipo *t);
Tipo *tp_puro(Tipo *t);
u32   tp_tam(Tipo *t);
u32   tp_alinha(Tipo *t);
int   tp_inteiro(Tipo *t);
int   tp_escalar(Tipo *t);      /* inteiro ou ponteiro */
int   tp_sem_sinal(Tipo *t);
int   tp_agreg(Tipo *t);        /* struct ou union */
int   tp_igual(Tipo *a, Tipo *b);
int   tp_completo(Tipo *t);
Campo *tp_campo(Tipo *t, Nome *n, u32 *desloc);
void  tp_texto(Tipo *t, char *dst, u32 max);
int   tp_ri(Tipo *t);           /* o tipo de máquina (RT_*) de um escalar */

/* ------------------------------------------------------------ símbolos */

enum { S_VAR, S_FUNCAO, S_TYPEDEF, S_CONST_ENUM };
struct Simbolo {
    Simbolo *anterior;      /* o que este nome era antes (escopo de fora) */
    Simbolo *prox_escopo;   /* lista do escopo, para desfazer ao sair */
    Nome *nome;
    u8    classe;
    u8    global;           /* fora de função (ou static/extern de dentro) */
    u8    definido;
    u8    estatico;
    int   nivel;
    Tipo *tipo;
    int   local;            /* variável local: número do local na RI */
    int   sim;              /* global ou função: número do símbolo na RI */
    i32   valor;            /* constante de enum */
    Pos   pos;
};
struct Etiqueta {
    Etiqueta *anterior, *prox_escopo;
    Nome *nome;
    Tipo *tipo;
    int   nivel;
    Pos   pos;
};

/* ------------------------------------------------------------ expressões */

enum {
    N_NUM, N_TEXTO, N_VAR, N_NEG, N_INVERTE, N_NAO, N_DEREF, N_END, N_CONV,
    N_SOMA, N_SUB, N_MUL, N_DIV, N_RESTO, N_E, N_OU, N_XOU, N_DESL_E, N_DESL_D,
    N_IGUAL, N_DIF, N_MENOR, N_MENOR_IG,           /* > e >= trocam os lados */
    N_E_LOG, N_OU_LOG, N_ATR, N_ATR_OP, N_COND, N_VIRGULA, N_CHAMA, N_MEMBRO,
    N_VA_INICIO, N_VA_ARG, N_COMPOSTO
};

typedef struct ItemInic ItemInic;
typedef struct No {
    u8    op;
    u8    lvalor;       /* dá para pegar o endereço e atribuir */
    u8    sub;          /* N_ATR_OP: a conta (N_SOMA...) */
    u8    pos_fixo;     /* N_ATR_OP: o valor é o de antes (x++) */
    Pos   pos;
    Tipo *tipo;
    struct No *a, *b, *c;
    i64   valor;        /* N_NUM; N_MEMBRO: deslocamento */
    Simbolo *sim;       /* N_VAR */
    int   n;            /* N_TEXTO: símbolo da RI; N_COMPOSTO: local */
    struct No **args; int qtd_args;
    Tipo *tipo_conta;   /* N_ATR_OP: o tipo em que a conta é feita */
    ItemInic *itens;    /* N_COMPOSTO */
} No;

/* Um pedaço de um inicializador: "ponha isto a tantos bytes do começo". */
struct ItemInic {
    ItemInic *prox;
    u32   desloc;
    Tipo *tipo;         /* escalar ou agregado copiado inteiro */
    No   *valor;        /* NULL = texto */
    const char *texto; u32 tam_texto;
};

No  *expressao(void);               /* com vírgula */
No  *expr_atribuicao(void);
No  *expr_condicional(void);
i64  expr_constante(const char *para_que, const char *dica);     /* inteira, conhecida ao compilar */
No  *converte(No *n, Tipo *para);              /* conversão implícita já conferida */
No  *para_atribuir(No *n, Tipo *para, const char *onde);   /* confere e converte */
No  *valor_de(No *n);               /* vetor e função viram ponteiro */
No  *no_num(i64 v, Tipo *t, Pos p);
No  *no_novo(int op, Tipo *t, Pos p);
int  eh_zero_const(No *n);

/* ------------------------------------------------------------ declaracao.c */

void analisar_unidade(void);        /* o arquivo inteiro: declarações e funções */
Tipo *nome_de_tipo(void);           /* em sizeof(tipo) e (tipo)expr */
int   comeca_tipo(Token *t);
ItemInic *ler_inicializador(Tipo **tipo);
int   texto_global(const char *bytes, u32 n);   /* cria o dado de um "texto"; devolve o símbolo */
int   novo_local(Tipo *t);

/* palavras reservadas */
enum {
    CH_NADA, CH_VOID, CH_CHAR, CH_SHORT, CH_INT, CH_LONG, CH_SIGNED, CH_UNSIGNED, CH_BOOL,
    CH_FLOAT, CH_DOUBLE, CH_STRUCT, CH_UNION, CH_ENUM, CH_TYPEDEF, CH_EXTERN, CH_STATIC,
    CH_AUTO, CH_REGISTER, CH_CONST, CH_VOLATILE, CH_RESTRICT, CH_INLINE, CH_NORETURN,
    CH_ATTRIBUTE, CH_IF, CH_ELSE, CH_WHILE, CH_DO, CH_FOR, CH_SWITCH, CH_CASE, CH_DEFAULT,
    CH_BREAK, CH_CONTINUE, CH_RETURN, CH_GOTO, CH_SIZEOF, CH_VA_START, CH_VA_ARG, CH_VA_END,
    CH_ALIGNOF
};

/* leitura dos tokens (declaracao.c) */
Token *tk(void);                    /* o token da vez */
Token *tk_adiante(int n);
void   avanca(void);
int    eh_pont(int p);
int    eh_chave(int c);
int    aceita(int p);               /* se for esta pontuação, passa por ela */
void   exige(int p, const char *dica);
Pos    pos_anterior_fim(void);      /* logo depois do token anterior (para "falta ;") */

Simbolo *declarar(Nome *n, int classe, Tipo *t, Pos p);

/* ------------------------------------------------------------ gerar.c */

/* Monta a RI. A frente chama estas funções; nada aqui sabe de x86. */
int   ri_simbolo(const char *nome);             /* acha ou cria; devolve o número */
int   ri_simbolo_anonimo(const char *prefixo);
void  ri_funcao_inicio(int sim);
void  ri_funcao_fim(void);
int   ri_local(u32 tam, u32 alinha, i32 parametro);
int   ri_rotulo(void);
void  ri_poe_rotulo(int r);
void  ri_salta(int r);
int   ri_temp(int largura);
Instr *ri_emite(int op, int tipo, i32 dst, Oper a, Oper b);
Oper  o_const(i64 v);
Oper  o_temp(int t);
Oper  o_local(int l, i64 d);
Oper  o_global(int s, i64 d);
void  ri_reloc(SimboloRI *s, u32 desloc, int sim);

Oper  gera(No *n);                              /* o valor (ou o endereço, se for bloco) */
void  gera_descarta(No *n);                     /* só os efeitos */
void  gera_salto(No *n, int rotulo, int se_verdade);
void  gera_inic_local(int local, Tipo *t, ItemInic *itens, int zerar);
void  gera_retorno(No *n, Tipo *devolve, int local_escondido);
void  ri_listar(ModuloRI *m);
int   avalia_const(No *n, i64 *valor, int *sim);   /* número ou endereço fixo; 0 = não é */

/* ------------------------------------------------------------ fundo */

/* Um "objeto" é o que o ligador junta: seções de bytes com os lugares a
 * corrigir (relocações) e os nomes (símbolos). O x86.c faz um a partir da
 * RI; o ligador.c lê a libsagui de um arquivo ELF para a mesma forma. */
enum { SEC_TEXTO, SEC_LEITURA, SEC_DADOS, SEC_ZERADA };
enum { REL_32 = 1, REL_PC32 = 2 };      /* os mesmos números do ELF i386 */

typedef struct { u32 desloc; u32 sim; u8 tipo; } Reloc;

typedef struct Secao {
    const char *nome;
    u8   classe;
    u8   viva;          /* alcançada a partir de _inicio */
    u32  alinha, tam;
    u8  *bytes;         /* NULL em SEC_ZERADA */
    Reloc *relocs; int qtd_relocs, cap_relocs;
    u32  endereco;
    struct Objeto *obj;
} Secao;

typedef struct SimLig {
    const char *nome;
    Secao *secao;       /* NULL = não definido aqui */
    u32   valor;        /* distância do começo da seção */
    u8    global;
    u8    sobrepoe;     /* do programa, com o mesmo nome de algo da libsagui */
    struct SimLig *real; /* onde está definido de verdade (resolvido) */
} SimLig;

typedef struct Objeto {
    struct Objeto *prox;
    const char *nome;
    Secao  *secoes; int qtd_secoes;
    SimLig *sims;   int qtd_sims;
    u8      do_programa;
} Objeto;

Objeto *x86_gerar(ModuloRI *m, const char *nome);
Objeto *elf_ler_objeto(const char *nome, u8 *d, u32 tam);
void    ligar(Objeto *lista, const char *saida);

/* ------------------------------------------------------------ estado geral */

typedef struct Bloco { struct Bloco *prox; } Bloco;

typedef struct {
    const CcAmbiente *amb;
    const CcOpcoes   *op;
    void  *salto[5];            /* para onde o erro volta (__builtin_setjmp) */
    Bloco *blocos;              /* tudo o que foi reservado */
    u8    *livre; u32 resta;    /* o pedaço atual da reserva */
    Nome **gavetas;             /* tabela de nomes */
    Arquivo *arqs; int qtd_arqs, cap_arqs;
    Token *toks; int qtd_toks, cap_toks;    /* o programa depois do pré-processador */
    int    it;                  /* token da vez */
    ModuloRI *mod;
    FuncaoRI *f;                /* função sendo gerada */
    int    pulando;             /* pré-processador num trecho desligado por #if */
    int    fundura;             /* aninhamento de expressões (limite de pilha) */
} Geral;
extern Geral G;

#endif
