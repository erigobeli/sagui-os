; vetores.asm - porta de entrada das 256 interrupções no kernel de 64 bits.
;
; A ideia é a mesma da versão de 32 bits (kernel/arch/i386/vetores.asm):
; cada vetor tem um trecho próprio que empilha um código de erro 0 (quando a
; CPU não empilhou nenhum) e o número do vetor, e salta para o trecho comum.
; O trecho comum salva todos os registradores, chama tratar_interrupcao(quadro)
; e retoma o quadro que o C devolver, que pode ser o de outro processo (D007).
;
; O que muda no modo longo (Intel SDM vol. 3A, 7.14 "64-Bit Mode Exception
; and Interrupt Handling"; AMD APM vol. 2, 8.9):
;   - a CPU alinha o RSP em 16 bytes e empilha SS, RSP, RFLAGS, CS e RIP
;     sempre, com ou sem troca de anel; o retorno é o "iretq";
;   - não existe pushad/popad: os 15 registradores vão um a um;
;   - "push ds"/"push es" não existem em 64 bits: os seletores passam por RAX.
;     Os programas de 32 bits (modo de compatibilidade) usam DS e ES de
;     verdade, então eles precisam ser salvos e restaurados com o processo.
; O quadro tem 26 campos de 8 bytes (208, múltiplo de 16): com o RSP que a
; CPU alinhou, o "call" ao C sai com a pilha alinhada em 16, como a ABI pede.

bits 64
section .text
extern tratar_interrupcao
global tabela_vetores
global retomar_quadro

%assign v 0
%rep 256
vetor_%[v]:
%if !(v == 8 || (v >= 10 && v <= 14) || v == 17 || v == 21 || v == 29 || v == 30)
        push qword 0
%endif
        push qword v
        jmp entrada_comum
%assign v v+1
%endrep

entrada_comum:
        push rax
        push rcx
        push rdx
        push rbx
        push rbp
        push rsi
        push rdi
        push r8
        push r9
        push r10
        push r11
        push r12
        push r13
        push r14
        push r15
        mov eax, ds
        push rax
        mov eax, es
        push rax
        mov eax, fs
        push rax
        mov eax, gs
        push rax
        cld                             ; o C supõe a flag de direção zerada
        mov rdi, rsp                    ; 1º argumento: o quadro
        call tratar_interrupcao
        mov rsp, rax                    ; quadro a retomar (pode ser outro)
; retomar_quadro: também usado para começar um processo novo a partir de um
; quadro montado à mão. Espera RSP apontando para um quadro_t.
retomar_quadro:
        pop rax
        mov gs, ax
        pop rax
        mov fs, ax
        pop rax
        mov es, ax
        pop rax
        mov ds, ax
        pop r15
        pop r14
        pop r13
        pop r12
        pop r11
        pop r10
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rbp
        pop rbx
        pop rdx
        pop rcx
        pop rax
        add rsp, 16                     ; descarta vetor e código de erro
        iretq

section .rodata
align 8
tabela_vetores:
%assign v 0
%rep 256
        dq vetor_%[v]
%assign v v+1
%endrep
