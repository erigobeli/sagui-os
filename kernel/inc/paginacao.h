/* paginacao.h - espaços de endereço (diretórios de páginas). Ver D010. */
#ifndef SAGUI_PAGINACAO_H
#define SAGUI_PAGINACAO_H
#include "tipos.h"

#define PG_PRESENTE  0x001
#define PG_ESCRITA   0x002
#define PG_USUARIO   0x004
#define PG_PWT       0x008   /* com o PAT do SaguiOS: memória com escrita combinada (WC) */
#define PG_EMPRESTADA 0x200   /* bit livre para o sistema: página que não é do processo (ex.: pixels de janela) */

#define KERNEL_JANELAS   0x38000000u   /* janelas para dispositivos (fases 4-6) */
#define USUARIO_INICIO   0x40000000u
#define USUARIO_FIM      0xC0000000u

/* Um espaço de endereço é identificado pelo endereço físico do seu
 * diretório de páginas (que, pelo mapeamento 1:1, também é o virtual). */
typedef uint32_t espaco_t;

void     paginacao_iniciar(void);
espaco_t espaco_kernel(void);
espaco_t espaco_criar(void);
void     espaco_destruir(espaco_t e);
void     espaco_ativar(espaco_t e);
espaco_t espaco_atual(void);
bool     espaco_mapear(espaco_t e, uint32_t virt, uint32_t fis, uint32_t flags);
uint32_t espaco_traduzir(espaco_t e, uint32_t virt, uint32_t *flags);   /* 0 se não mapeado */
uint32_t espaco_paginas_usuario(espaco_t e);
void     espaco_desmapear(espaco_t e, uint32_t virt);

/* Aloca páginas novas e zeradas para [virt, virt+bytes) no espaço do usuário. */
bool     espaco_alocar(espaco_t e, uint32_t virt, uint32_t bytes, uint32_t flags);

/* Mapeia uma faixa física alta (vídeo, USB) na janela do kernel; devolve o
 * endereço virtual. */
uint32_t janela_mapear(uint32_t fis, uint32_t bytes);
/* O mesmo, com escrita combinada (write-combining) se o processador tiver
 * PAT: para a memória de vídeo (G470: a tela ficava "com poucos hertz"). */
uint32_t janela_mapear_wc(uint32_t fis, uint32_t bytes, bool *wc);

#endif
