; vetores.asm - porta de entrada de todas as 256 interrupções.
;
; O processador não diz ao código qual vetor disparou, e só algumas exceções
; empilham um código de erro. Por isso cada vetor tem um trecho próprio,
; gerado abaixo por repetição, que:
;   1. empilha um código de erro 0 quando o processador não empilhou nenhum,
;      para que o quadro tenha sempre o mesmo formato;
;   2. empilha o número do vetor;
;   3. salta para o trecho comum.
; O trecho comum salva todos os registradores, entra nos segmentos do kernel
; e chama tratar_interrupcao(quadro). O C devolve o endereço do quadro a ser
; retomado: normalmente o mesmo, mas na fase 3 o escalonador devolve o quadro
; de outro processo, e é só isso que uma troca de contexto precisa ser.
;
; A tabela_vetores (endereço de cada trecho) é lida pelo idt.c para montar a IDT.

section .text
extern tratar_interrupcao
global tabela_vetores
global retomar_quadro

%assign v 0
%rep 256
vetor_%[v]:
%if !(v == 8 || (v >= 10 && v <= 14) || v == 17 || v == 21 || v == 29 || v == 30)
        push dword 0
%endif
        push dword v
        jmp entrada_comum
%assign v v+1
%endrep

entrada_comum:
        pushad
        push ds
        push es
        push fs
        push gs
        mov ax, 0x10                    ; segmentos de dados do kernel
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        cld                             ; o C supõe a flag de direção zerada
        push esp
        call tratar_interrupcao
        mov esp, eax                    ; quadro a retomar (pode ser outro)
; retomar_quadro: também usado para começar um processo novo a partir de um
; quadro montado à mão (fase 3). Espera ESP apontando para um quadro_t.
retomar_quadro:
        pop gs
        pop fs
        pop es
        pop ds
        popad
        add esp, 8                      ; descarta vetor e código de erro
        iretd

section .rodata
align 4
tabela_vetores:
%assign v 0
%rep 256
        dd vetor_%[v]
%assign v v+1
%endrep
