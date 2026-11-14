/* vfs.h - camada de arquivos virtual: um caminho só para tudo (PRD 4.3). */
#ifndef SAGUI_VFS_H
#define SAGUI_VFS_H
#include "tipos.h"
#include "abi.h"
#include "caminho.h"

struct processo;

/* Um arquivo ou pasta na árvore em memória. */
typedef struct no {
    char           nome[NOME_MAX];
    uint8_t        tipo;           /* TIPO_ARQUIVO, TIPO_PASTA, TIPO_PROGRAMA */
    struct no     *pai, *filho, *irmao;
    uint32_t       tamanho;
    const uint8_t *original;       /* bytes no ramdisk, até a primeira escrita */
    uint8_t       *dados;          /* cópia própria, depois da primeira escrita */
    uint32_t       capacidade;
    uint32_t       refs;           /* quantos arquivos abertos apontam para ele */
    bool           removido;       /* apagado enquanto aberto: some no último fechar */
} no_t;

void     vfs_iniciar(void);
no_t    *vfs_raiz(void);
no_t    *vfs_achar(const char *caminho);          /* caminho absoluto simplificado */
int      vfs_criar(const char *caminho, uint8_t tipo, no_t **saida);
uint32_t vfs_ler(no_t *x, uint32_t desloc, void *destino, uint32_t n);
int      vfs_escrever(no_t *x, uint32_t desloc, const void *origem, uint32_t n);
void     vfs_truncar(no_t *x);
int      vfs_apagar(const char *caminho);
int      vfs_remover_pasta(const char *caminho);
int      vfs_renomear(const char *origem, const char *destino);
int      vfs_listar(const char *caminho, int indice, entrada_pasta_t *e);
void     vfs_segurar(no_t *x);
void     vfs_soltar(no_t *x);
/* Lê um arquivo inteiro para um buffer novo do kernel (liberar com kfree). */
int      vfs_ler_inteiro(const char *caminho, uint8_t **dados, uint32_t *tam);
uint32_t vfs_bytes_usados(void);                 /* soma dos arquivos da árvore (H83) */

/* /usb: existe enquanto há um pendrive montado (fase 6). */
void vfs_ponto_montagem(bool existe);
/* Se o caminho (absoluto) está dentro de /usb montado, devolve o resto
 * ("" para a própria /usb) em *rel. */
bool vfs_na_usb(const char *caminho, const char **rel);

void arquivos_herdar(struct processo *filho, struct processo *pai);
void arquivos_fechar_todos(struct processo *p);

#endif
