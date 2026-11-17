; mbr.asm - estágio 1 do carregador de boot do SaguiOS.
;
; A BIOS copia este setor (512 bytes) para 0x7C00 e salta para cá em modo real.
; O trabalho dele é pequeno de propósito: descobrir de qual disco viemos (DL),
; ler o cabeçalho de boot (no LBA guardado em 0x1B4) e, com ele, trazer o
; estágio 2 para 0x8000. Todo o resto (A20, memória, kernel, vídeo) fica no
; estágio 2, que não tem o limite de 512 bytes.
;
; Mapa do setor:
;   0x000-0x002  salto curto + NOP (formato esperado por BIOS que procuram BPB)
;   0x003-0x059  área de BPB falso, zerada. Algumas BIOS, ao dar boot por USB,
;                escrevem a geometria aqui. Por isso nenhum código fica nela.
;   0x05A-0x1B3  código e mensagens
;   0x1B4-0x1B7  LBA do cabeçalho de boot: 1 na imagem; no disco instalado, o
;                primeiro setor da partição do sistema, para não usar o setor
;                1, onde costuma estar o carregador de outro sistema (D067)
;   0x1B8-0x1BD  identificador do disco
;   0x1BE-0x1FD  tabela de partições (preenchida pelo tools/mkimagem.py)
;   0x1FE-0x1FF  assinatura 0x55AA (exigida pela BIOS)

bits 16
org 0x7C00

%include "cabecalho.inc"

        jmp short inicio
        nop
        times 0x5A - ($ - $$) db 0

inicio:
        ; Segmentos e pilha num estado conhecido. A pilha cresce para baixo a
        ; partir de 0x7C00, num espaço livre da memória baixa.
        cli
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, 0x7C00
        ; Há BIOS que entram em 07C0:0000 em vez de 0000:7C00. O salto longo
        ; deixa CS=0 para que os endereços do "org 0x7C00" valham sempre.
        jmp 0x0000:.cs_zerado
.cs_zerado:
        sti
        cld
        mov [disco], dl                 ; DL = número do disco de boot dado pela BIOS

        mov si, msg_carregando
        call escrever

        ; Só usamos leitura por LBA (INT 13h AH=42h). Pendrive emulado como
        ; disco rígido no G470 e as VMs suportam; se não houver, avisamos.
        mov ah, 0x41
        mov bx, 0x55AA
        mov dl, [disco]
        int 0x13
        jc .sem_lba
        cmp bx, 0xAA55
        jne .sem_lba

        ; Cabeçalho de boot para 0x7E00, logo depois deste setor.
        mov word [dap_qtd], 1
        mov word [dap_destino], CAB_ENDERECO
        mov eax, [cab_lba]
        mov [dap_lba], eax
        call ler_setores
        cmp dword [CAB_ENDERECO + CAB_ASSINATURA], CAB_ASSINATURA_VAL
        jne .sem_cabecalho

        ; Estágio 2 para 0x8000, com tamanho e posição tirados do cabeçalho.
        mov eax, [CAB_ENDERECO + CAB_E2_SETORES]
        test eax, eax
        jz .sem_cabecalho
        cmp eax, ESTAGIO2_MAX_SET
        ja .sem_cabecalho
        mov [dap_qtd], ax
        mov word [dap_destino], ESTAGIO2_ENDERECO
        mov eax, [CAB_ENDERECO + CAB_E2_LBA]
        mov [dap_lba], eax
        call ler_setores

        mov dl, [disco]                 ; o estágio 2 recebe o disco em DL, como nós
        jmp 0x0000:ESTAGIO2_ENDERECO

.sem_lba:
        mov si, msg_sem_lba
        jmp parar
.sem_cabecalho:
        mov si, msg_cabecalho
        jmp parar

; ler_setores: lê conforme o pacote "dap". Tenta 3 vezes, reiniciando o
; disco entre tentativas (pendrives às vezes falham na primeira leitura).
ler_setores:
        mov di, 3
.tentar:
        mov si, dap
        mov ah, 0x42
        mov dl, [disco]
        int 0x13
        jnc .ok
        xor ah, ah
        mov dl, [disco]
        int 0x13
        dec di
        jnz .tentar
        mov si, msg_leitura
        jmp parar
.ok:
        ret

; escrever: mostra a string terminada em zero apontada por SI (teletipo da BIOS).
escrever:
        lodsb
        test al, al
        jz .fim
        mov ah, 0x0E
        mov bx, 0x0007
        int 0x10
        jmp escrever
.fim:
        ret

; parar: mostra a mensagem em SI e fica parado. Sem saída de depuração no
; G470, a mensagem na tela é a única pista do que aconteceu.
parar:
        call escrever
.laco:
        hlt
        jmp .laco

disco:          db 0

; Pacote de endereço de disco (formato definido pela especificação das
; extensões do INT 13h): tamanho, reservado, quantidade, destino seg:off, LBA.
align 4
dap:            db 0x10, 0
dap_qtd:        dw 0
dap_destino:    dw 0, 0                 ; offset, segmento
dap_lba:        dd 0, 0

msg_carregando: db "SaguiOS: carregando...", 13, 10, 0
msg_sem_lba:    db "Erro: BIOS sem INT13 LBA", 0
msg_cabecalho:  db "Erro: cabecalho invalido", 0
msg_leitura:    db "Erro de leitura", 0

        times 0x1B4 - ($ - $$) db 0     ; falha na montagem se o código passar do limite
cab_lba:        dd CAB_LBA              ; o instalador troca (D067)
id_disco:       dd 0x53475549           ; 'IUGS' - identificador do disco
                dw 0
tabela_part:    times 64 db 0           ; preenchida pelo mkimagem.py
                dw 0xAA55
