; estagio2.asm - estágio 2 do carregador de boot do SaguiOS.
;
; O MBR nos deixa em 0000:8000, modo real, com o disco de boot em DL e o
; cabeçalho de boot em 0x7E00. Aqui preparamos tudo que só a BIOS sabe fazer,
; porque depois do modo protegido não há mais BIOS:
;
;   [1/6] linha A20         (sem ela, o bit 20 do endereço fica preso em zero)
;   [2/6] mapa de memória   (INT 15h E820)
;   [3/6] kernel            (INT 13h, em blocos, copiado para 1 MB)
;   [4/6] ramdisk           (idem, logo depois do fim do kernel)
;   [5/6] vídeo             (INT 10h VBE: escolher modo; trocar só se pedido)
;   [6/6] modo protegido    (GDT própria e salto para o kernel)
;         ou modo longo     (kernel de 64 bits, 3.0: confere a CPU, monta as
;                            tabelas de página e liga o IA-32e; D076)
;
; Cada etapa mostra uma linha na tela. No G470 não há serial: se travar, a
; foto da tela mostra em qual etapa parou.
;
; Carregar acima de 1 MB: o INT 13h só escreve abaixo de 1 MB. Lemos cada
; bloco num buffer em 0x10000 e copiamos para cima usando "unreal mode":
; entramos rapidamente no modo protegido só para carregar DS/ES com um
; descritor de limite 4 GB e voltamos ao modo real. O processador guarda o
; limite em cache, então endereços de 32 bits passam a funcionar em modo real
; e a BIOS continua disponível. A entrada é refeita antes de cada cópia,
; porque uma rotina da BIOS pode ter recarregado os segmentos.

bits 16
org 0x8000

%include "cabecalho.inc"
%include "bootinfo.inc"

BUFFER_SEG      equ 0x1000              ; buffer de leitura em 0x10000
BUFFER_LINEAR   equ 0x10000
BLOCO_SETORES   equ 64                  ; 32 KB por leitura: dentro do limite de 127 setores
                                        ; e sem cruzar fronteira de 64 KB

SEL_CODIGO      equ 0x08
SEL_DADOS       equ 0x10
SEL_CODIGO64    equ 0x18

; Tabelas de página provisórias do modo longo (D076): 7 páginas a partir de
; 0x20000, uma área livre da memória baixa (o buffer de leitura do disco
; termina em 0x18000 e o kernel não usa nada abaixo de 1 MB). O kernel monta
; as tabelas definitivas dele logo no começo e esquece estas.
TAB_PML4        equ 0x20000
TAB_PDPT        equ 0x21000
TAB_PD          equ 0x22000             ; 4 diretórios: 0x22000 a 0x25FFF
TAB_PT0         equ 0x26000             ; tabela de 4 KB para os primeiros 2 MB
MSR_EFER        equ 0xC0000080

inicio:
        cli
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, 0x7C00
        sti
        cld
        mov [disco], dl

        ; Zera a boot_info e preenche o que já sabemos.
        mov di, BOOTINFO
        mov cx, BOOTINFO_TAM / 2
        xor ax, ax
        rep stosw
        mov dword [BOOTINFO + BI_ASSINATURA], 0x49424753     ; 'SGBI'
        mov dword [BOOTINFO + BI_VERSAO], 1
        movzx eax, byte [disco]
        mov [BOOTINFO + BI_DISCO], eax
        mov si, CAB_ENDERECO + CAB_TESTE        ; repassa o nome do autoteste (se houver)
        mov di, BOOTINFO + BI_TESTE
        mov cx, 64
        rep movsb
        movzx eax, word [CAB_ENDERECO + CAB_OPCOES]
        mov [BOOTINFO + BI_OPCOES], eax
        ; O identificador do disco, do MBR que a BIOS deixou em 0x7C00 (a
        ; pilha cresce para baixo de 0x7C00 e não chega nele): o kernel acha
        ; por ele o disco de onde o sistema ligou (o instalador copia dele).
        mov eax, [0x7C00 + 0x1B8]
        mov [BOOTINFO + BI_DISCO_ID], eax

        mov si, msg_titulo
        call escrever

; ---------------------------------------------------------------- [1/6] A20
        call habilitar_a20
        jc .falha_a20
        mov si, msg_a20
        call escrever

; ---------------------------------------------------------------- [2/6] memória
        call ler_mapa_memoria
        cmp dword [BOOTINFO + BI_MAPA_QTD], 0
        je .falha_memoria
        mov si, msg_mem
        call escrever
        mov eax, [BOOTINFO + BI_MAPA_QTD]
        call escrever_decimal
        mov si, msg_mem2
        call escrever
        call somar_memoria_mb
        call escrever_decimal
        mov si, msg_mb
        call escrever

; ---------------------------------------------------------------- [3/6] kernel
        mov si, msg_kernel
        call escrever
        mov eax, [CAB_ENDERECO + CAB_K_ENDERECO]
        mov [BOOTINFO + BI_K_INICIO], eax
        mov eax, [CAB_ENDERECO + CAB_K_BYTES]
        mov [BOOTINFO + BI_K_BYTES], eax
        mov eax, [CAB_ENDERECO + CAB_K_LBA]
        mov ecx, [CAB_ENDERECO + CAB_K_SETORES]
        mov edi, [CAB_ENDERECO + CAB_K_ENDERECO]
        call carregar_regiao
        mov esi, [CAB_ENDERECO + CAB_K_ENDERECO]
        mov ecx, [CAB_ENDERECO + CAB_K_BYTES]
        call somar_bytes
        cmp eax, [CAB_ENDERECO + CAB_K_SOMA]
        jne .falha_soma
        mov si, msg_ok
        call escrever

; ---------------------------------------------------------------- [4/6] ramdisk
        mov si, msg_ramdisk
        call escrever
        mov eax, [CAB_ENDERECO + CAB_R_ENDERECO]
        mov [BOOTINFO + BI_R_INICIO], eax
        mov eax, [CAB_ENDERECO + CAB_R_BYTES]
        mov [BOOTINFO + BI_R_BYTES], eax
        mov eax, [CAB_ENDERECO + CAB_R_LBA]
        mov ecx, [CAB_ENDERECO + CAB_R_SETORES]
        mov edi, [CAB_ENDERECO + CAB_R_ENDERECO]
        call carregar_regiao
        mov esi, [CAB_ENDERECO + CAB_R_ENDERECO]
        mov ecx, [CAB_ENDERECO + CAB_R_BYTES]
        call somar_bytes
        cmp eax, [CAB_ENDERECO + CAB_R_SOMA]
        jne .falha_soma
        mov si, msg_ok
        call escrever

; ---------------------------------------------------------------- [5/6] vídeo
        mov si, msg_video
        call escrever
        call procurar_vesa
        cmp dword [BOOTINFO + BI_VESA_OK], 0
        je .sem_vesa
        mov eax, [BOOTINFO + BI_VESA_LARG]
        call escrever_decimal
        mov al, 'x'
        call escrever_char
        mov eax, [BOOTINFO + BI_VESA_ALT]
        call escrever_decimal
        mov al, 'x'
        call escrever_char
        mov eax, [BOOTINFO + BI_VESA_BPP]
        call escrever_decimal
        mov si, msg_crlf
        call escrever
        jmp .video_fim
.sem_vesa:
        mov si, msg_sem_vesa
        call escrever
.video_fim:

; ---------------------------------------------------------------- [6/6] modo protegido
        ; Kernel de 64 bits (3.0): confere antes de tudo se a CPU tem o modo
        ; longo. A conferência vem antes da troca de vídeo para a mensagem de
        ; erro aparecer no modo texto da BIOS.
        test word [CAB_ENDERECO + CAB_OPCOES], OPC_LONGO
        jz .msg_32
        call tem_modo_longo
        jc .falha_64
        mov si, msg_longo
        call escrever
        jmp .trocar_video
.msg_32:
        mov si, msg_pm
        call escrever
.trocar_video:

        ; Troca de modo de vídeo só quando o build pede (fase 4 em diante) e
        ; sempre por último, porque depois dela o texto da BIOS some.
        test word [CAB_ENDERECO + CAB_OPCOES], OPC_VESA
        jz .entrar_pm
        cmp dword [BOOTINFO + BI_VESA_OK], 0
        je .entrar_pm
        mov bx, [BOOTINFO + BI_VESA_MODO]
        or bx, 0x4000                   ; bit 14: framebuffer linear
        mov ax, 0x4F02
        int 0x10
        cmp ax, 0x004F
        jne .entrar_pm                  ; falhou: o kernel continua em modo texto e avisa
        mov dword [BOOTINFO + BI_VESA_ATIVO], 1

.entrar_pm:
        cli
        lgdt [gdt_descritor]
        mov eax, cr0
        or eax, 1
        mov cr0, eax
        jmp SEL_CODIGO:modo_protegido   ; salto longo: recarrega CS com o seletor de 32 bits

.falha_a20:
        mov si, msg_falha_a20
        jmp parar
.falha_memoria:
        mov si, msg_falha_mem
        jmp parar
.falha_soma:
        mov si, msg_falha_soma
        jmp parar
.falha_64:
        mov si, msg_falha_64
        jmp parar

; ============================================================================
; Modo longo (64 bits): a CPU tem?
; ============================================================================

; tem_modo_longo: CF=0 se a CPU sabe rodar em 64 bits. Pelo manual da Intel
; (vol. 2A, CPUID) e da AMD (APM vol. 3): a instrução CPUID existe se o bit
; 21 (ID) do EFLAGS pode ser trocado; a função 0x80000000 diz até onde vão
; as funções estendidas; e o bit 29 (LM) do EDX da função 0x80000001 diz se
; há o modo longo (IA-32e).
tem_modo_longo:
        pushfd
        pop eax
        mov ecx, eax
        xor eax, 1 << 21
        push eax
        popfd
        pushfd
        pop eax
        push ecx                        ; devolve o EFLAGS original
        popfd
        xor eax, ecx
        test eax, 1 << 21
        jz .nao                         ; o bit não mudou: CPU sem CPUID
        mov eax, 0x80000000
        cpuid
        cmp eax, 0x80000001
        jb .nao
        mov eax, 0x80000001
        cpuid
        test edx, 1 << 29
        jz .nao
        clc
        ret
.nao:
        stc
        ret

; ============================================================================
; A20
; ============================================================================

; a20_ligada: escreve valores diferentes em 0x000500 e 0x100500 (FFFF:0510).
; Se a escrita no segundo aparecer no primeiro, o endereço deu a volta e a
; A20 está desligada. Saída: ZF=1 desligada, ZF=0 ligada. Usa AX, DX.
a20_ligada:
        pushf
        push ds
        push es
        push si
        push di
        cli
        xor ax, ax
        mov ds, ax
        not ax
        mov es, ax
        mov si, 0x0500
        mov di, 0x0510
        mov dl, [ds:si]                 ; guarda os valores originais
        mov dh, [es:di]
        mov byte [ds:si], 0x00
        mov byte [es:di], 0xFF
        mov al, [ds:si]
        mov [es:di], dh
        mov [ds:si], dl
        pop di
        pop si
        pop es
        pop ds
        popf
        cmp al, 0xFF
        ret

; habilitar_a20: tenta em ordem do mais seguro ao mais antigo e confere
; depois de cada tentativa. CF=1 se nenhum método funcionou.
habilitar_a20:
        call a20_ligada
        jne .metodo0
        ; Método 1: serviço da BIOS (INT 15h AX=2401).
        mov ax, 0x2401
        int 0x15
        call a20_ligada
        jne .metodo1
        ; Método 2: "A20 rápida" pela porta 0x92 (bit 1). O bit 0 dessa porta
        ; reinicia a máquina, então ele é sempre escrito como zero.
        in al, 0x92
        or al, 0x02
        and al, 0xFE
        out 0x92, al
        call esperar_um_pouco
        call a20_ligada
        jne .metodo2
        ; Método 3: pela porta de saída do controlador de teclado (i8042).
        call kbc_livre_escrita
        mov al, 0xAD                    ; desliga o teclado durante a operação
        out 0x64, al
        call kbc_livre_escrita
        mov al, 0xD0                    ; pede o byte da porta de saída
        out 0x64, al
        call kbc_tem_dado
        in al, 0x60
        mov bl, al
        call kbc_livre_escrita
        mov al, 0xD1                    ; vai escrever na porta de saída
        out 0x64, al
        call kbc_livre_escrita
        mov al, bl
        or al, 0x02                     ; bit 1 = A20
        out 0x60, al
        call kbc_livre_escrita
        mov al, 0xAE                    ; religa o teclado
        out 0x64, al
        call kbc_livre_escrita
        mov cx, 16                      ; o controlador pode demorar a aplicar
.espera_kbc:
        call esperar_um_pouco
        call a20_ligada
        jne .metodo3
        loop .espera_kbc
        stc
        ret
.metodo0:
        xor eax, eax
        jmp .registra
.metodo1:
        mov eax, 1
        jmp .registra
.metodo2:
        mov eax, 2
        jmp .registra
.metodo3:
        mov eax, 3
.registra:
        mov [BOOTINFO + BI_A20], eax
        clc
        ret

; Esperas com limite: se o controlador não responder, seguimos em frente em
; vez de travar para sempre.
kbc_livre_escrita:
        push cx
        mov cx, 0xFFFF
.l:     in al, 0x64
        test al, 2
        jz .fim
        loop .l
.fim:   pop cx
        ret

kbc_tem_dado:
        push cx
        mov cx, 0xFFFF
.l:     in al, 0x64
        test al, 1
        jnz .fim
        loop .l
.fim:   pop cx
        ret

; Cerca de alguns milissegundos: a porta 0x80 (diagnóstico POST) é segura
; para escrever e cada acesso a ela leva ~1 µs no barramento.
esperar_um_pouco:
        push cx
        mov cx, 2000
.l:     out 0x80, al
        loop .l
        pop cx
        ret

; ============================================================================
; Mapa de memória (E820)
; ============================================================================

ler_mapa_memoria:
        xor ebx, ebx                    ; EBX = "continuação", 0 na primeira chamada
        mov di, MAPA_E820
        xor bp, bp
.proxima:
        mov dword [di + 20], 1          ; atributo estendido ACPI: "entrada válida"
        mov eax, 0xE820
        mov edx, 0x534D4150             ; 'SMAP'
        mov ecx, 24
        int 0x15
        jc .fim                         ; carry: sem suporte ou fim da lista
        cmp eax, 0x534D4150
        jne .fim
        mov eax, [di + 8]               ; ignora regiões de tamanho zero
        or eax, [di + 12]
        jz .pula
        inc bp
        add di, 24
        cmp bp, MAPA_MAX
        jae .fim
.pula:
        test ebx, ebx
        jnz .proxima
.fim:
        movzx eax, bp
        mov [BOOTINFO + BI_MAPA_QTD], eax
        mov dword [BOOTINFO + BI_MAPA_END], MAPA_E820
        ret

; somar_memoria_mb: soma as regiões do tipo 1 (RAM livre) e devolve em EAX o
; total em MB, só para a mensagem de progresso.
somar_memoria_mb:
        xor esi, esi                    ; soma baixa
        xor edi, edi                    ; soma alta
        mov cx, [BOOTINFO + BI_MAPA_QTD]
        mov bx, MAPA_E820
.l:     jcxz .fim
        cmp dword [bx + 16], 1
        jne .prox
        add esi, [bx + 8]
        adc edi, [bx + 12]
.prox:  add bx, 24
        dec cx
        jmp .l
.fim:   mov eax, esi
        shrd eax, edi, 20               ; (alta:baixa) >> 20 = MB
        ret

; ============================================================================
; Disco
; ============================================================================

; ler_setores: executa o pacote "dap" com 3 tentativas.
ler_setores:
        push di
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
        mov si, msg_falha_leitura
        jmp parar
.ok:
        pop di
        ret

; carregar_regiao: EAX = LBA inicial, ECX = setores, EDI = destino linear (pode
; estar acima de 1 MB). Lê em blocos para o buffer baixo e copia para cima.
carregar_regiao:
.laco:
        test ecx, ecx
        jz .fim
        mov edx, ecx
        cmp edx, BLOCO_SETORES
        jbe .tam_ok
        mov edx, BLOCO_SETORES
.tam_ok:
        push eax
        push ecx
        push edx
        mov [dap_qtd], dx
        mov word [dap_destino], 0
        mov word [dap_destino + 2], BUFFER_SEG
        mov [dap_lba], eax
        mov dword [dap_lba + 4], 0
        push edi                        ; a BIOS não promete preservar a metade alta
        call ler_setores
        pop edi
        pop edx
        add [BOOTINFO + BI_SETORES_LIDOS], edx

        call entrar_unreal
        mov esi, BUFFER_LINEAR
        mov ecx, edx
        shl ecx, 7                      ; setores * 512 / 4 = palavras duplas
        a32 rep movsd                   ; DS:ESI -> ES:EDI com endereços de 32 bits

        mov al, '.'
        call escrever_char
        pop ecx
        pop eax
        add eax, edx
        sub ecx, edx
        jmp .laco
.fim:
        ret

; entrar_unreal: carrega DS e ES com limite de 4 GB e volta ao modo real.
entrar_unreal:
        cli
        push ds
        push es
        lgdt [gdt_descritor]
        mov eax, cr0
        or al, 1
        mov cr0, eax
        jmp short .pm                   ; esvazia a fila de instruções
.pm:
        mov bx, SEL_DADOS
        mov ds, bx
        mov es, bx
        and al, 0xFE
        mov cr0, eax
        pop es                          ; seletores voltam a 0, o limite em cache fica
        pop ds
        sti
        ret

; somar_bytes: ESI = início (32 bits), ECX = quantidade. Devolve a soma em EAX.
; Detecta um kernel ou ramdisk corrompido na leitura antes de saltar para ele.
somar_bytes:
        call entrar_unreal
        xor eax, eax
        xor edx, edx
.l:     test ecx, ecx
        jz .fim
        mov dl, [esi]
        add eax, edx
        inc esi
        dec ecx
        jmp .l
.fim:   ret

; ============================================================================
; Vídeo (VBE)
; ============================================================================

; procurar_vesa: percorre a lista de modos e escolhe, nesta ordem de
; preferência, 1024x768, 800x600, 640x480, com 32 ou 24 bits e framebuffer
; linear. Só registra na boot_info; não troca de modo.
procurar_vesa:
        mov dword [VBE_INFO], 0x32454256        ; 'VBE2': pede a resposta do VBE 2.0+
        mov di, VBE_INFO
        mov ax, 0x4F00
        int 0x10
        cmp ax, 0x004F
        jne .fim
        cmp dword [VBE_INFO], 0x41534556        ; 'VESA'
        jne .fim
        mov si, [VBE_INFO + 14]                 ; ponteiro longe para a lista de modos
        mov ax, [VBE_INFO + 16]
        mov fs, ax
        mov word [melhor_pontos], 0
.laco:
        mov cx, [fs:si]
        add si, 2
        cmp cx, 0xFFFF
        je .fim
        push si
        push fs
        mov [modo_atual], cx
        mov di, VBE_MODO
        mov ax, 0x4F01
        int 0x10
        cmp ax, 0x004F
        jne .proximo
        mov ax, [VBE_MODO]                      ; atributos
        and ax, 0x0091                          ; suportado + gráfico + linear
        cmp ax, 0x0091
        jne .proximo
        cmp byte [VBE_MODO + 0x1B], 6           ; modelo de memória: cor direta
        jne .proximo
        mov bl, 2
        cmp byte [VBE_MODO + 0x19], 32
        je .bpp_ok
        mov bl, 1
        cmp byte [VBE_MODO + 0x19], 24
        jne .proximo
.bpp_ok:
        mov ax, [VBE_MODO + 0x12]               ; largura
        mov dx, [VBE_MODO + 0x14]               ; altura
        ; Só nos testes da H84 (bit OPC_VESA_LARGO): uma tela larga de 768
        ; linhas, como a do G470, vale mais que 1024x768. Serve para provar
        ; no QEMU que a interface funciona com 1366 pixels de largura.
        test word [CAB_ENDERECO + CAB_OPCOES], OPC_VESA_LARGO
        jz .r1024
        cmp dx, 768
        jne .r1024
        mov bh, 40
        cmp ax, 1366
        je .pontua
        mov bh, 38
        cmp ax, 1360
        je .pontua
        mov bh, 36
        cmp ax, 1280
        je .pontua
.r1024: mov bh, 30
        cmp ax, 1024
        jne .r800
        cmp dx, 768
        je .pontua
.r800:  mov bh, 20
        cmp ax, 800
        jne .r640
        cmp dx, 600
        je .pontua
.r640:  mov bh, 10
        cmp ax, 640
        jne .proximo
        cmp dx, 480
        jne .proximo
.pontua:
        add bh, bl
        movzx ax, bh
        cmp ax, [melhor_pontos]
        jbe .proximo
        mov [melhor_pontos], ax
        mov dword [BOOTINFO + BI_VESA_OK], 1
        movzx eax, word [modo_atual]
        mov [BOOTINFO + BI_VESA_MODO], eax
        movzx eax, word [VBE_MODO + 0x12]
        mov [BOOTINFO + BI_VESA_LARG], eax
        movzx eax, word [VBE_MODO + 0x14]
        mov [BOOTINFO + BI_VESA_ALT], eax
        movzx eax, word [VBE_MODO + 0x10]
        mov [BOOTINFO + BI_VESA_PITCH], eax
        movzx eax, byte [VBE_MODO + 0x19]
        mov [BOOTINFO + BI_VESA_BPP], eax
        mov eax, [VBE_MODO + 0x28]
        mov [BOOTINFO + BI_VESA_FB], eax
        mov eax, [VBE_MODO + 0x1F]              ; tam/pos vermelho, tam/pos verde
        mov [BOOTINFO + BI_VESA_CORES], eax
        mov ax, [VBE_MODO + 0x23]               ; tam/pos azul
        mov [BOOTINFO + BI_VESA_CORES + 4], ax
.proximo:
        pop fs
        pop si
        jmp .laco
.fim:
        xor ax, ax
        mov fs, ax
        ret

; ============================================================================
; Texto pela BIOS
; ============================================================================

escrever:
        push ax
.l:     lodsb
        test al, al
        jz .fim
        call escrever_char
        jmp .l
.fim:   pop ax
        ret

escrever_char:
        push ax
        push bx
        mov ah, 0x0E
        mov bx, 0x0007
        int 0x10
        pop bx
        pop ax
        ret

; escrever_decimal: EAX sem sinal, em base 10.
escrever_decimal:
        push eax
        push ecx
        push edx
        push ebx
        mov ebx, 10
        xor ecx, ecx
.div:   xor edx, edx
        div ebx
        push dx
        inc cx
        test eax, eax
        jnz .div
.mostra:
        pop ax
        add al, '0'
        call escrever_char
        loop .mostra
        pop ebx
        pop edx
        pop ecx
        pop eax
        ret

parar:
        call escrever
.l:     hlt
        jmp .l

; ============================================================================
; Modo protegido (32 bits)
; ============================================================================

bits 32
modo_protegido:
        mov ax, SEL_DADOS
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov esp, 0x7C00                 ; pilha provisória; o kernel monta a dele
        test word [CAB_ENDERECO + CAB_OPCOES], OPC_LONGO
        jnz entrar_modo_longo
        mov eax, BOOT_MAGICO
        mov ebx, BOOTINFO
        jmp [CAB_ENDERECO + CAB_K_ENTRADA]

; ============================================================================
; Modo longo (kernel de 64 bits, 3.0)
; ============================================================================

; A sequência é a do manual da Intel (vol. 3A, 10.8.5, "Initializing IA-32e
; Mode") e da AMD (APM vol. 2, 14.6, "Enabling and Activating Long Mode"):
; com a paginação desligada, ligar a PAE (CR4), apontar o CR3 para uma
; tabela de 4 níveis, ligar o EFER.LME e por fim a paginação (CR0.PG). A
; partir daí a CPU está no modo longo, ainda executando este código de 32
; bits no "modo de compatibilidade"; um salto longo para um segmento com
; L=1 entra nos 64 bits de verdade.
;
; As tabelas provisórias cobrem os primeiros 4 GB com o endereço virtual
; igual ao físico ("identidade"), em páginas de 2 MB (bit PS no diretório):
; 1 PML4 -> 1 PDPT -> 4 diretórios de 512 entradas. Assim o kernel (em 1 MB),
; a memória baixa (boot_info, VGA) e o framebuffer continuam no mesmo
; endereço de antes.
entrar_modo_longo:
        cld
        mov edi, TAB_PML4               ; zera as 7 páginas
        mov ecx, 7 * 4096 / 4
        xor eax, eax
        rep stosd
        mov dword [TAB_PML4], TAB_PDPT | 3              ; presente + escrita
        mov edi, TAB_PDPT
        mov eax, TAB_PD | 3
        mov ecx, 4
.pdpt:  mov [edi], eax                  ; PDPT[i] -> diretório i (1 GB cada)
        add eax, 4096
        add edi, 8
        loop .pdpt
        mov edi, TAB_PD                 ; 2048 entradas de 2 MB = 4 GB
        mov eax, 0x83                   ; presente + escrita + página de 2 MB
        xor edx, edx                    ; metade alta do endereço (sempre 0 aqui)
        mov ecx, 2048
.pd:    mov [edi], eax
        mov [edi + 4], edx
        add eax, 0x200000
        add edi, 8
        loop .pd
        ; Os primeiros 2 MB vão em páginas de 4 KB: ali se misturam RAM, a
        ; memória de vídeo VGA (0xA0000) e a ROM da BIOS, com tipos de memória
        ; diferentes nos MTRRs, e o manual da Intel (vol. 3A, 13.11.9) diz que
        ; uma página grande sobre tipos diferentes tem comportamento indefinido.
        mov dword [TAB_PD], TAB_PT0 | 3
        mov edi, TAB_PT0
        mov eax, 3                      ; presente + escrita, a partir do endereço 0
        mov ecx, 512
.pt0:   mov [edi], eax
        add eax, 4096
        add edi, 8
        loop .pt0

        mov eax, cr4
        or eax, 1 << 5                  ; PAE
        mov cr4, eax
        mov eax, TAB_PML4
        mov cr3, eax
        mov ecx, MSR_EFER
        rdmsr
        or eax, 1 << 8                  ; LME: pede o modo longo
        wrmsr
        mov eax, cr0
        or eax, 1 << 31                 ; PG: liga a paginação e ativa o modo longo
        mov cr0, eax
        jmp SEL_CODIGO64:modo_longo

bits 64
modo_longo:
        mov ax, SEL_DADOS
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov rsp, 0x7C00
        ; Convenção de chamada do System V AMD64 (a do gcc): primeiro
        ; argumento em RDI, segundo em RSI. O kernel recebe os mesmos dois
        ; valores que a versão de 32 bits recebe em EAX e EBX.
        mov edi, BOOT_MAGICO
        mov esi, BOOTINFO
        mov eax, [CAB_ENDERECO + CAB_K_ENTRADA]
        jmp rax

; ============================================================================
; Dados
; ============================================================================

disco:          db 0
melhor_pontos:  dw 0
modo_atual:     dw 0

align 4
dap:            db 0x10, 0
dap_qtd:        dw 0
dap_destino:    dw 0, 0
dap_lba:        dd 0, 0

; GDT mínima do carregador: código e dados de 32 bits, base 0 e limite 4 GB.
; O kernel troca por uma GDT própria logo que começa.
align 8
gdt:
        dq 0                            ; descritor nulo (obrigatório)
        dw 0xFFFF, 0x0000               ; 0x08 código: limite 0-15, base 0-15
        db 0x00, 0x9A, 0xCF, 0x00       ;      base 16-23, presente/anel 0/código, 4 KB + 32 bits, base 24-31
        dw 0xFFFF, 0x0000               ; 0x10 dados
        db 0x00, 0x92, 0xCF, 0x00
        dw 0xFFFF, 0x0000               ; 0x18 código de 64 bits (L=1, D=0), só para o modo longo
        db 0x00, 0x9A, 0xAF, 0x00
gdt_fim:
gdt_descritor:
        dw gdt_fim - gdt - 1
        dd gdt

msg_titulo:     db "SaguiOS - estagio 2 do carregador", 13, 10, 0
msg_a20:        db "[1/6] A20 ok", 13, 10, 0
msg_mem:        db "[2/6] memoria ok: ", 0
msg_mem2:       db " regioes, ", 0
msg_mb:         db " MB livres", 13, 10, 0
msg_kernel:     db "[3/6] kernel ", 0
msg_ramdisk:    db "[4/6] ramdisk ", 0
msg_ok:         db " ok", 13, 10, 0
msg_video:      db "[5/6] video: ", 0
msg_sem_vesa:   db "VESA nao encontrado, fica em modo texto", 13, 10, 0
msg_crlf:       db 13, 10, 0
msg_pm:         db "[6/6] entrando no modo protegido", 13, 10, 0
msg_longo:      db "[6/6] entrando no modo longo (64 bits)", 13, 10, 0
msg_falha_64:   db "ERRO: este processador nao tem 64 bits (x86-64).", 13, 10
                db "Esta imagem do SaguiOS precisa de um processador de 64 bits;", 13, 10
                db "use a imagem de 32 bits (make ARQ=32).", 13, 10
                db "No VirtualBox, escolha a versao Other/Unknown (64-bit).", 0
msg_falha_a20:  db "ERRO: nao consegui ligar a linha A20", 0
msg_falha_mem:  db "ERRO: BIOS nao deu o mapa de memoria (E820)", 0
msg_falha_leitura: db 13, 10, "ERRO: falha de leitura do disco (INT 13h)", 0
msg_falha_soma: db 13, 10, "ERRO: dados lidos estao corrompidos (soma nao bate)", 0
