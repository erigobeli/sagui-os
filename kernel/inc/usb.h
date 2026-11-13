/* usb.h - controlador EHCI, dispositivos USB, hubs e armazenamento (fase 6). */
#ifndef SAGUI_USB_H
#define SAGUI_USB_H
#include "tipos.h"
#include "pci.h"

#define USB_MAX_CONTROLADORES 4
#define USB_MAX_PORTAS        16
#define USB_MAX_DISP          16

/* Erros internos das transferências. */
#define USB_ERRO_PAROU  -100   /* o dispositivo respondeu STALL */
#define USB_ERRO_TEMPO  -101   /* não respondeu a tempo */
#define USB_ERRO        -102   /* erro de transmissão, dispositivo sumiu... */

typedef struct ehci ehci_t;

typedef struct usb_disp {
    bool      usado;
    ehci_t   *ctl;
    uint8_t   endereco;
    uint8_t   mps0;            /* tamanho máximo de pacote do endpoint 0 */
    struct usb_disp *hub;      /* NULL = ligado direto na porta do controlador */
    uint8_t   porta;           /* porta do hub (ou do controlador) onde está */
    uint16_t  id_fabricante, id_produto;
    uint8_t   classe, subclasse, protocolo;   /* do dispositivo ou da interface */
    char      fabricante[32], produto[48];
    uint8_t   config;
    uint8_t   interface;
    uint8_t   ep_entrada, ep_saida;           /* endpoints bulk (armazenamento) */
    uint16_t  mps_entrada, mps_saida;
    uint8_t   alterna_entrada, alterna_saida;  /* "data toggle" de cada endpoint bulk */
    uint8_t   hub_portas;                      /* se for hub */
    void     *disco;                           /* se for armazenamento */
    void     *rede;                            /* se for adaptador de rede (H46) */
} usb_disp_t;

/* ---- ehci.c ---- */
struct ehci {
    pci_disp_t pci;
    volatile uint8_t *cap;     /* registradores de capacidade */
    volatile uint32_t *op;     /* registradores de operação */
    int       portas;
    uint8_t   proximo_endereco;
    bool      ok;
    int       companheiros;            /* controladores USB 1.1 associados (0 no G470) */
    bool      enderecos64;             /* usa as estruturas no formato de 64 bits */
    int       bios;                    /* 0 = não usava, 1 = soltou, -1 = não soltou */
    int       resultado[USB_MAX_PORTAS];   /* da última tentativa de enumerar a porta (0 = ok) */
    uint32_t  estruturas;              /* página com o anel assíncrono só deste controlador */
};

int  ehci_iniciar_todos(void);                 /* devolve quantos controladores ligou */
int  ehci_quantidade(void);
ehci_t *ehci_controlador(int i);
uint32_t ehci_porta_estado(ehci_t *c, int porta);
bool ehci_porta_mudou(ehci_t *c, int porta);   /* lê e limpa "mudou a conexão" */
int  ehci_porta_resetar(ehci_t *c, int porta); /* 0 = alta velocidade pronta; <0 = não serve */
void ehci_esperar_ms(ehci_t *c, uint32_t ms);
/* Transferências síncronas. Devolvem bytes transferidos ou um erro USB_ERRO_*. */
int  ehci_controle(ehci_t *c, uint8_t endereco, uint8_t mps0, const void *setup, void *dados, uint16_t tam, bool entrada);
int  ehci_bulk(ehci_t *c, uint8_t endereco, uint8_t ep, uint16_t mps, bool entrada, uint8_t *alterna, void *dados, uint32_t tam);
int  ehci_bulk_tentar(ehci_t *c, uint8_t endereco, uint8_t ep, uint16_t mps, uint8_t *alterna, void *dados, uint32_t tam);   /* 0 = nada agora */

/* ---- usb.c ---- */
void usb_iniciar(void);
int  usb_controle(usb_disp_t *d, uint8_t tipo, uint8_t pedido, uint16_t valor, uint16_t indice, void *dados, uint16_t tam);
int  usb_descritor(usb_disp_t *d, uint8_t tipo, uint8_t indice, uint16_t idioma, void *buf, uint16_t tam);
void usb_ler_texto(usb_disp_t *d, uint8_t indice, uint16_t idioma, char *saida, int max);
/* ---- rede_usb.c (H46) ---- */
bool rede_usb_conectar(usb_disp_t *d, int configuracoes);
void rede_usb_desconectar(usb_disp_t *d);
usb_disp_t *usb_disp(int i);
void usb_verificar_conexoes(void);             /* conexão e remoção a quente */

/* ---- armazenamento.c ---- */
bool armaz_conectar(usb_disp_t *d);
void armaz_desconectar(usb_disp_t *d);

#endif
