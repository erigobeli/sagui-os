; inicio.asm - ponto de entrada de todo programa do SaguiOS.
;
; O kernel começa o programa com [esp] = argc e [esp+4] = argv (ver
; kernel/nucleo/carregador.c). Chamamos principal(argc, argv) e encerramos o
; processo com o valor devolvido.
section .text.inicio
global _inicio
extern principal
extern sair

_inicio:
        xor ebp, ebp                    ; fim da cadeia de quadros
        mov eax, [esp]
        mov ecx, [esp + 4]
        and esp, ~15                    ; pilha alinhada para o C:
        sub esp, 8                      ; no "call" ela tem de estar em múltiplo de 16
        push ecx                        ; (depois dos 2 argumentos); com SSE2 o gcc
                                        ; guarda valores de 16 bytes na pilha (movdqa) (H82)
        push eax
        call principal
        push eax
        call sair
.nunca: jmp .nunca
