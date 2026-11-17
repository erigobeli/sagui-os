/* disco.h - discos (pendrives) vistos pelo sistema de arquivos (fase 6). */
#ifndef SAGUI_DISCO_H
#define SAGUI_DISCO_H
#include "tipos.h"

#define MAX_DISCOS 4

typedef struct disco {
    bool     usado;
    bool     presente;            /* false: foi removido (o registro some quando desmontar) */
    char     nome[8];             /* "usb0" */
    char     fabricante[32], produto[48];
    uint32_t setores, tam_setor;
    void    *dono;
    int    (*ler)(struct disco *d, uint32_t lba, uint32_t n, void *buf);
    int    (*escrever)(struct disco *d, uint32_t lba, uint32_t n, const void *buf);
    /* Preenchidos pelo discos.c ao reconhecer o formato: */
    char     formato[40];         /* "FAT32" ou "exFAT: formato não suportado"... */
    char     rotulo[12];
    uint32_t part_inicio;         /* primeiro setor do volume FAT32 */
    bool     fat32;
    bool     de_boot;             /* o próprio pendrive de boot do SaguiOS: sem aviso nem ícone */
    bool     montado;
    bool     interno;             /* disco rígido (hd0), não pendrive */
    /* Para o instalador (D063): */
    uint32_t id_mbr;              /* identificador do disco no MBR (0x1B8) */
    bool     tem_sistema;         /* tem a partição do SaguiOS (0xDA)... */
    uint32_t sistema_ini;         /* ...que começa neste setor... */
    uint32_t sistema_fim;         /* ...e acaba antes deste */
    int      liberado_pid;        /* processo que pode ler e gravar setores crus (0: nenhum) */
} disco_t;

disco_t *disco_registrar(const char *fabricante, const char *produto, uint32_t setores, uint32_t tam_setor, void *dono,
                         int (*ler)(disco_t *, uint32_t, uint32_t, void *),
                         int (*escrever)(disco_t *, uint32_t, uint32_t, const void *));
/* disco interno (ATA): nome hd0, hd1... */
disco_t *disco_registrar_interno(const char *modelo, uint32_t setores, void *dono,
                                 int (*ler)(disco_t *, uint32_t, uint32_t, void *),
                                 int (*escrever)(disco_t *, uint32_t, uint32_t, const void *));
void     disco_removido(disco_t *d);
disco_t *disco_por_nome(const char *nome);
disco_t *disco_indice(int i);
int      disco_montar(disco_t *d);
int      disco_desmontar(disco_t *d);
disco_t *disco_montado(void);
bool     disco_eh_origem(const disco_t *d);    /* o disco de onde o sistema ligou */
void     discos_processo_terminou(int pid);   /* tira as liberações do processo */

/* Avisos para a área de trabalho (H34). */
#define AVISO_CONECTADO   1
#define AVISO_REMOVIDO    2   /* removido sem desmontar */
#define AVISO_DESMONTADO  3   /* pode remover */
#define AVISO_MONTADO     4
bool disco_proximo_aviso(int *tipo, char *texto, int max);

#endif
