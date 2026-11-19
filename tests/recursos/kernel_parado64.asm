; Kernel de mentira de 64 bits para o teste da H127: o estágio 2 só entra no
; modo longo se o kernel for ELF64; este para a máquina sem limpar a tela,
; para que a mensagem "[6/6] entrando no modo longo" fique visível. Antes de
; parar, escreve "64" no canto da tela VGA (0xB8000): prova que o código de
; 64 bits rodou de verdade.
bits 64
section .entrada
global _inicio
_inicio:
        cli
        mov rax, 0x0F340F36             ; '6' e '4', branco sobre preto
        mov [0xB8000 + 158 - 2], eax    ; os dois últimos caracteres da linha 0
.l:     hlt
        jmp .l
