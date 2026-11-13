/* compilador.h - o compilador C do SaguiOS (fase 7 da versão 2.0).
 *
 * Esta é a única coisa que um programa precisa incluir para compilar C. O
 * compilador não conhece o sistema em que roda: quem chama entrega um
 * "ambiente", a tabela de funções para ler e gravar arquivos, pedir memória
 * e mostrar mensagens (o mesmo desenho do SshAmbiente e do TlsAmbiente).
 * Assim o mesmo código roda no SaguiOS (o comando cc) e no computador de
 * desenvolvimento (os testes).
 *
 * O caminho de um programa:
 *
 *     arquivo.c --frente--> representação intermediária --fundo--> ELF
 *
 * A frente (pré-processador, leitura, tipos) está em lexico.c, preproc.c,
 * tipos.c, expressao.c, declaracao.c e gerar.c. O fundo está em x86.c
 * (escolha das instruções), montador.c (instruções em bytes) e ligador.c
 * (junta com a libsagui e grava o executável). O que passa de uma metade
 * para a outra está em ri.h. O LEIA-ME.md desta pasta conta o raciocínio. */
#ifndef SAGUI_COMPILADOR_H
#define SAGUI_COMPILADOR_H

typedef struct {
    void *ctx;
    /* Lê um arquivo inteiro para memória pedida com "reservar". Devolve 0,
     * ou negativo se o arquivo não existe. */
    int   (*ler_arquivo)(void *ctx, const char *caminho, unsigned char **dados, unsigned *tam);
    /* Grava (criando ou trocando) um arquivo inteiro. 0 ou negativo. */
    int   (*gravar_arquivo)(void *ctx, const char *caminho, const unsigned char *dados, unsigned tam);
    void *(*reservar)(void *ctx, unsigned n);     /* NULL se não há memória */
    void  (*liberar)(void *ctx, void *p);
    /* Mensagens de erro (texto terminado em zero, pode ter várias linhas). */
    void  (*mensagem)(void *ctx, const char *texto);
    /* Saída do "--ri" (a listagem da representação intermediária). */
    void  (*listagem)(void *ctx, const char *texto);
} CcAmbiente;

/* Marca no e_flags do executável: o programa abre janela (igual a
 * ELF_SAGUI_JANELA de kernel/inc/abi.h; repetido aqui porque o núcleo do
 * compilador não depende dos cabeçalhos do sistema). */
#define ELF_SAGUI_JANELA 0x4A475301u
#define ELF_SAGUI_TEXTO  0x54475301u
#define CC_MAX_FONTES 16
#define CC_MAX_PASTAS 8

typedef struct {
    const char *fontes[CC_MAX_FONTES];   /* os arquivos .c */
    int         qtd_fontes;
    const char *saida;                   /* o executável; NULL = só conferir */
    const char *biblioteca;              /* a libsagui num objeto só (ELF relocável) */
    const char *pastas[CC_MAX_PASTAS];   /* onde procurar os #include */
    int         qtd_pastas;
    int         mostrar_ri;              /* lista a representação intermediária */
    int         latin1;                  /* mensagens: linha do código convertida de UTF-8 para Latin-1 */
} CcOpcoes;

/* Devolve 0 se compilou; 1 se houve erro (a mensagem já foi entregue). */
int cc_compilar(const CcAmbiente *amb, const CcOpcoes *op);

#endif
