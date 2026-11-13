/* pci.h - acesso ao espaço de configuração PCI (mecanismo 1, portas 0xCF8/0xCFC). */
#ifndef SAGUI_PCI_H
#define SAGUI_PCI_H
#include "tipos.h"

typedef struct {
    uint8_t  barramento, dispositivo, funcao;
    uint16_t fabricante, modelo;
    uint8_t  classe, subclasse, interface;
} pci_disp_t;

uint32_t pci_le32(const pci_disp_t *d, uint8_t reg);
void     pci_escreve32(const pci_disp_t *d, uint8_t reg, uint32_t v);
/* Procura dispositivos de uma classe/subclasse/interface; devolve quantos achou. */
#define PCI_MAX 64
int      pci_listar(pci_disp_t *achados, int max);   /* todas as funções do barramento */
int      pci_relatorio(char *texto, int max);          /* comando pci (H35) */
int      pci_procurar(uint8_t classe, uint8_t subclasse, uint8_t interface, pci_disp_t *achados, int max);

#endif
