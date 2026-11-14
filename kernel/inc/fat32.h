/* fat32.h - leitura e gravação de FAT32 (H32, H33). Ver D026. */
#ifndef SAGUI_FAT32_H
#define SAGUI_FAT32_H
#include "tipos.h"
#include "abi.h"
#include "disco.h"

/* Um arquivo aberto no FAT32: onde está a entrada dele na pasta, o primeiro
 * cluster e o tamanho. */
typedef struct {
    uint32_t setor_entrada;    /* setor (absoluto) da entrada curta na pasta */
    uint32_t desloc_entrada;   /* deslocamento dentro do setor */
    uint32_t primeiro;         /* primeiro cluster (0 = arquivo vazio) */
    uint32_t tamanho;
    bool     mudou;
    uint32_t geracao;          /* montagem em que foi aberto */
} fat_arq_t;

extern uint32_t fat_geracao;   /* muda a cada montar/desmontar */

int  fat_montar(disco_t *d);
int  fat_desmontar(bool gravar);
bool fat_rotulo_raiz(disco_t *d, char *rotulo);   /* rótulo da entrada especial da raiz */
int  fat_espaco(uint32_t *total_kb, uint32_t *livre_kb);   /* do volume montado (H83) */

/* Caminhos relativos ao volume, sem barra inicial ("" = raiz). */
int  fat_info(const char *rel, info_arquivo_t *inf);
int  fat_listar(const char *rel, int indice, entrada_pasta_t *e);
int  fat_abrir(const char *rel, int modo, fat_arq_t *f);
int  fat_ler(fat_arq_t *f, uint32_t pos, void *buf, uint32_t n);
int  fat_escrever(fat_arq_t *f, uint32_t pos, const void *buf, uint32_t n);
int  fat_fechar(fat_arq_t *f);
int  fat_criar_pasta(const char *rel);
int  fat_remover_pasta(const char *rel);
int  fat_apagar(const char *rel);
int  fat_renomear(const char *de, const char *para);

#endif
