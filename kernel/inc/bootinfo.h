/* bootinfo.h - o que o estágio 2 entrega ao kernel (espelho de boot/bootinfo.inc). */
#ifndef SAGUI_BOOTINFO_H
#define SAGUI_BOOTINFO_H
#include "tipos.h"

#define BOOT_MAGICO      0x53414755u
#define BOOTINFO_ASSIN   0x49424753u  /* 'SGBI' */
#define MAPA_MAX         64
#define E820_LIVRE       1

typedef struct EMPACOTADO {
    uint64_t base;
    uint64_t tamanho;
    uint32_t tipo;      /* 1 = RAM livre; os demais são reservados para o firmware/ACPI */
    uint32_t extra;     /* atributo estendido ACPI 3.0: bit 0 = entrada válida */
} regiao_e820_t;

typedef struct EMPACOTADO {
    uint32_t assinatura;
    uint32_t versao;
    uint32_t disco;
    uint32_t mapa_qtd;
    uint32_t mapa_end;
    uint32_t kernel_inicio;
    uint32_t kernel_bytes;
    uint32_t ramdisk_inicio;
    uint32_t ramdisk_bytes;
    uint32_t a20_metodo;
    uint32_t vesa_ok;
    uint32_t vesa_modo;
    uint32_t vesa_largura;
    uint32_t vesa_altura;
    uint32_t vesa_pitch;
    uint32_t vesa_bpp;
    uint32_t vesa_fb;
    uint32_t vesa_ativo;
    uint8_t  verm_tam, verm_pos, verde_tam, verde_pos, azul_tam, azul_pos;
    uint8_t  reservado[2];
    uint32_t setores_lidos;
    char     teste[64];   /* autoteste pedido pelo build de teste; vazio no uso normal */
    uint32_t opcoes;      /* bit 0 VESA, bit 1 terminal de tela cheia sem área de trabalho */
    uint32_t disco_id;    /* identificador do MBR (0x1B8) do disco de onde o sistema ligou */
} info_boot_t;

/* Cópia feita no início do kernel; vale durante toda a execução. */
extern info_boot_t info_boot;
extern regiao_e820_t mapa_memoria[MAPA_MAX];

#endif
