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
        ; A convenção do i386 (com SSE, H80) quer a pilha alinhada em 16 bytes
        ; no momento do "call": alinha, desconta 8 e empilha os 2 argumentos
        ; (mais 8). Sem o desconto, o principal começava desalinhado em 8 e
        ; o movaps do GCC dava proteção geral (o cc caía no G470 e nos testes).
        and esp, ~15
        sub esp, 8
        push ecx
        push eax
        call principal
        push eax
        call sair
.nunca: jmp .nunca
