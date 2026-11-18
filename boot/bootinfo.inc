; bootinfo.inc - estrutura que o estágio 2 entrega ao kernel.
;
; Fica em 0x5000 (memória baixa livre). O kernel recebe o endereço em EBX e a
; assinatura BOOT_MAGICO em EAX, copia tudo para dentro dele logo no início e
; não depende mais desta área. A mesma estrutura está em kernel/inc/bootinfo.h.

BOOTINFO            equ 0x5000
MAPA_E820           equ 0x5100     ; até 64 entradas de 24 bytes (até 0x5700)
MAPA_MAX            equ 64
VBE_INFO            equ 0x6000     ; 512 bytes (bloco de informação do VBE)
VBE_MODO            equ 0x6200     ; 256 bytes (informação de um modo)
BOOTINFO_TAM        equ 192

BOOT_MAGICO         equ 0x53414755 ; 'UGAS' - "fui carregado pelo estágio 2 do SaguiOS"

BI_ASSINATURA       equ 0          ; 'SGBI'
BI_VERSAO           equ 4
BI_DISCO            equ 8          ; DL recebido da BIOS
BI_MAPA_QTD         equ 12
BI_MAPA_END         equ 16
BI_K_INICIO         equ 20
BI_K_BYTES          equ 24
BI_R_INICIO         equ 28
BI_R_BYTES          equ 32
BI_A20              equ 36         ; 0 já ativa, 1 BIOS, 2 porta 0x92, 3 controlador de teclado
BI_VESA_OK          equ 40         ; 1 se achou um modo aceitável
BI_VESA_MODO        equ 44
BI_VESA_LARG        equ 48
BI_VESA_ALT         equ 52
BI_VESA_PITCH       equ 56
BI_VESA_BPP         equ 60
BI_VESA_FB          equ 64         ; endereço físico do framebuffer linear
BI_VESA_ATIVO       equ 68         ; 1 se o estágio 2 trocou de modo
BI_VESA_CORES       equ 72         ; 6 bytes: tam/pos do vermelho, verde, azul
BI_SETORES_LIDOS    equ 80
BI_OPCOES           equ 148        ; u32 cópia de CAB_OPCOES
BI_TESTE            equ 84         ; char[64] copiado de CAB_TESTE (até o byte 148)
BI_DISCO_ID         equ 152        ; u32 identificador do disco (MBR, bytes 0x1B8-0x1BB) de onde ligou
