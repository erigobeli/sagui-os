; entrada.asm - primeira instrução do kernel do SaguiOS.
;
; O estágio 2 chega aqui em modo protegido 32 bits, com EAX = assinatura do
; carregador e EBX = endereço da boot_info. Antes de qualquer código C:
;   1. zerar a BSS (o binário plano não a contém; o C espera zeros);
;   2. trocar para uma pilha nossa, dentro da BSS;
;   3. chamar inicio_kernel(assinatura, boot_info).

section .entrada
global _inicio
extern inicio_kernel
extern _bss_inicio, _bss_fim

_inicio:
        cli
        mov esi, eax                    ; guarda os dois valores do carregador
        mov edx, ebx                    ; (a BSS e a pilha vão ser apagadas)
        cld
        mov edi, _bss_inicio
        mov ecx, _bss_fim
        sub ecx, edi
        xor eax, eax
        rep stosb
        mov esp, pilha_kernel_topo
        xor ebp, ebp                    ; marca o fim da cadeia de quadros de pilha
        push edx
        push esi
        call inicio_kernel
.parado:
        cli
        hlt
        jmp .parado

section .bss
align 16
pilha_kernel_base:
        resb 16384
pilha_kernel_topo:
