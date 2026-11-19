; entrada.asm - primeira instrução do kernel de 64 bits do SaguiOS (3.0).
;
; O estágio 2 chega aqui já no modo longo (64 bits), com a paginação ligada
; e os primeiros 4 GB mapeados com endereço virtual = físico (D076), e com
; os dois valores do carregador nos registradores dos dois primeiros
; argumentos da convenção System V AMD64: RDI = assinatura do carregador,
; RSI = endereço da boot_info. Antes de qualquer código C:
;   1. zerar a BSS (o binário plano não a contém; o C espera zeros);
;   2. trocar para uma pilha nossa, dentro da BSS;
;   3. chamar inicio_kernel(assinatura, boot_info).

bits 64
section .entrada
global _inicio
extern inicio_kernel
extern _bss_inicio, _bss_fim

_inicio:
        cli
        mov r12, rdi                    ; guarda os dois valores do carregador
        mov r13, rsi                    ; (a BSS e a pilha vão ser apagadas)
        cld
        mov rdi, _bss_inicio
        mov rcx, _bss_fim
        sub rcx, rdi
        xor eax, eax
        rep stosb
        mov rsp, pilha_kernel_topo      ; alinhada em 16, como a ABI pede antes de um "call"
        xor ebp, ebp                    ; marca o fim da cadeia de quadros de pilha
        mov edi, r12d
        mov rsi, r13
        call inicio_kernel
.parado:
        cli
        hlt
        jmp .parado

section .bss
align 16
pilha_kernel_base:
        resb 32768                      ; o dobro da de 32 bits: quadros e registradores de 8 bytes
pilha_kernel_topo:
