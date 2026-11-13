/* #if, #ifdef, #ifndef, #elif, #else: escolhem o que é compilado. */
#include "sagui.h"

#define VERSAO 3
#define COM_CORES
#define NIVEL (VERSAO * 2 - 1)

#ifdef COM_CORES
#define CORES 16
#else
#define CORES 2
#endif

#ifndef NAO_EXISTE
#define PADRAO 1
#endif

#if VERSAO == 1
#define NOME "um"
#elif VERSAO == 2
#define NOME "dois"
#elif VERSAO >= 3 && defined(COM_CORES)
#define NOME "tres com cores"
#else
#define NOME "outro"
#endif

#if 0
isto nem precisa ser C: 'aspas soltas, @ $ e tudo
#if 1
continua pulando
#endif
#else
#define PULOU 1
#endif

#if defined COM_CORES && !defined(NAO_EXISTE) && (NIVEL > 4 || 0)
#define TUDO 1
#else
#define TUDO 0
#endif

#if NAO_DEFINIDA
#error nome sem define vale zero
#endif

#ifdef __SAGUI__
#define COMPILADOR "sagui"
#else
#define COMPILADOR "outro"
#endif

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    imprimir("%d %d %s %d %d %d\n", CORES, PADRAO, NOME, PULOU, TUDO, NIVEL);
#if VERSAO > 2
    imprimir("versao nova\n");
#else
    imprimir("versao velha\n");
#endif
    imprimir("%d\n", (int)sizeof(COMPILADOR) > 1);
    return 0;
}
