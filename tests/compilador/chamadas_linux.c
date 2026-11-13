/* chamadas_linux.c - a camada de chamadas de sistema da libsagui trocada
 * por chamadas do Linux, só para os testes do compilador.
 *
 * O compilador do SaguiOS gera um ELF de 32 bits em 0x40000000, que o Linux
 * também sabe carregar. Ligando o programa de teste a uma libsagui em que
 * só este arquivo (no lugar de chamadas.c e inicio.asm) é diferente, dá
 * para rodar os programas compilados direto no computador de
 * desenvolvimento, em milissegundos, antes do teste completo no QEMU. Todo
 * o resto (imprimir, aloca, texto) é o código de verdade da libsagui. */
#include "sagui.h"

static inline int linux3(int n, unsigned a, unsigned b, unsigned c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

__asm__(".section .text.inicio,\"ax\"\n"
        ".globl _inicio\n"
        "_inicio:\n"
        "    xor %ebp, %ebp\n"
        "    mov (%esp), %eax\n"
        "    lea 4(%esp), %ecx\n"
        "    and $-16, %esp\n"
        "    push %ecx\n"
        "    push %eax\n"
        "    call principal\n"
        "    push %eax\n"
        "    call sair\n");

void sair(int codigo) { linux3(1, (unsigned)codigo, 0, 0); for (;;) ; }
int  ler(int fd, void *b, unsigned n) { return linux3(3, (unsigned)fd, (unsigned)b, n); }
int  escrever(int fd, const void *b, unsigned n) { return linux3(4, (unsigned)fd, (unsigned)b, n); }
int  fechar(int fd) { return linux3(6, (unsigned)fd, 0, 0); }
int  posicionar(int fd, int d, int o) { return linux3(19, (unsigned)fd, (unsigned)d, (unsigned)o); }
int  apagar(const char *c) { return linux3(10, (unsigned)c, 0, 0) < 0 ? ERRO_NAO_EXISTE : 0; }
int  criar_pasta(const char *c) { return linux3(39, (unsigned)c, 0755, 0) < 0 ? ERRO_JA_EXISTE : 0; }
int  meu_pid(void) { return linux3(20, 0, 0, 0); }
int  registrar_texto(const char *t) { (void)t; return 0; }
unsigned tempo_ms(void) { return 0; }
int  dormir(unsigned ms) { (void)ms; return 0; }

int abrir(const char *c, int modo)
{
    int f = 0, r;
    if ((modo & ABRIR_LER) && (modo & ABRIR_ESCREVER)) f = 2;
    else if (modo & ABRIR_ESCREVER) f = 1;
    if (modo & ABRIR_CRIAR) f |= 0100;
    if (modo & ABRIR_TRUNCAR) f |= 01000;
    if (modo & ABRIR_ACRESCENTAR) f |= 02000;
    r = linux3(5, (unsigned)c, (unsigned)f, 0644);
    return r < 0 ? ERRO_NAO_EXISTE : r;
}

void *aumentar_memoria(unsigned n)
{
    static unsigned fim;
    unsigned antigo;
    if (!fim) fim = (unsigned)linux3(45, 0, 0, 0);
    antigo = fim;
    if ((unsigned)linux3(45, fim + n, 0, 0) != fim + n) return NULL;
    fim += n;
    return (void *)antigo;
}
