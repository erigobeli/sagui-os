; cabecalho.inc - formato do cabeçalho de boot do SaguiOS (LBA 1 da imagem).
;
; O MBR e o estágio 2 leem este cabeçalho para saber onde está cada parte do
; sistema. Quem preenche é o tools/mkimagem.py no build, então nenhum LBA fica
; fixo no código além do próprio LBA 1. Os mesmos offsets estão repetidos no
; script Python (classe Cabecalho); se mudar aqui, mude lá.

CAB_LBA             equ 1          ; único LBA fixo do projeto
CAB_ENDERECO        equ 0x7E00     ; onde o MBR deixa o cabeçalho na memória
CAB_ASSINATURA_VAL  equ 0x44484753 ; 'SGHD' em little-endian

CAB_ASSINATURA      equ 0          ; u32 'SGHD'
CAB_VERSAO          equ 4          ; u16
CAB_OPCOES          equ 6          ; u16, bit 0 = trocar para o modo VESA
CAB_E2_LBA          equ 8          ; u32 LBA do estágio 2
CAB_E2_SETORES      equ 12         ; u32
CAB_K_LBA           equ 16         ; u32 LBA do kernel
CAB_K_SETORES       equ 20         ; u32
CAB_K_BYTES         equ 24         ; u32 tamanho exato do kernel.bin
CAB_K_ENDERECO      equ 28         ; u32 endereço físico de carga (1 MB)
CAB_R_LBA           equ 32         ; u32 LBA do ramdisk
CAB_R_SETORES       equ 36         ; u32
CAB_R_BYTES         equ 40         ; u32
CAB_R_ENDERECO      equ 44         ; u32 endereço físico do ramdisk (depois do fim do kernel, BSS incluída)
CAB_IMG_SETORES     equ 48         ; u32 tamanho total útil
CAB_K_ENTRADA       equ 52         ; u32 endereço de entrada do kernel

OPC_VESA            equ 1
OPC_CONSOLE         equ 2      ; sem área de trabalho: terminal de tela cheia (testes)
OPC_VESA_LARGO      equ 4      ; prefere telas largas (1366x768...): testes da H84 no QEMU

; Endereços de trabalho em modo real (todos abaixo de 0x7C00 ou entre o
; estágio 2 e 0x9F000, longe da EBDA).
ESTAGIO2_ENDERECO   equ 0x8000     ; até 0xFFFF (32 KB no máximo)
ESTAGIO2_MAX_SET    equ 64

CAB_K_SOMA          equ 56         ; u32 soma simples dos bytes do kernel (detecta leitura corrompida)
CAB_R_SOMA          equ 60         ; u32 soma simples dos bytes do ramdisk
CAB_TESTE           equ 64         ; char[64] autoteste do kernel, ou programa inicial com argumentos (vazio = boot normal)
