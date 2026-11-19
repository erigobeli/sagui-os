/* tipos.h - tipos básicos do kernel. Sem libc: definimos os nossos. */
#ifndef SAGUI_TIPOS_H
#define SAGUI_TIPOS_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
/* Tamanhos e endereços acompanham a largura dos ponteiros: 32 bits na
 * imagem de 32 bits (make ARQ=32) e 64 bits no kernel em modo longo (3.0).
 * Os campos de tamanho fixo (uint32_t...) continuam iguais nos dois, e é
 * com eles que se descreve o que o hardware e os programas enxergam. */
#ifdef __x86_64__
typedef unsigned long      size_t;
typedef long               ssize_t;
typedef unsigned long      uintptr_t;
#else
typedef unsigned int       size_t;
typedef int                ssize_t;
typedef unsigned int       uintptr_t;
#endif
typedef _Bool              bool;

#define true  1
#define false 0
#define NULL  ((void *)0)

#define EMPACOTADO   __attribute__((packed))
#define SEM_RETORNO  __attribute__((noreturn))
#define ALINHADO(n)  __attribute__((aligned(n)))

typedef __builtin_va_list va_list;
#define va_start(l, u) __builtin_va_start(l, u)
#define va_arg(l, t)   __builtin_va_arg(l, t)
#define va_end(l)      __builtin_va_end(l)

#endif
