/* cc - o compilador C do SaguiOS, como comando do Terminal.
 *
 *     cc programa.c                 compila e grava "programa" na mesma pasta
 *     cc programa.c -o /tmp/teste   escolhe o nome do executável
 *     cc a.c b.c -o programa        um programa em vários arquivos
 *     cc programa.c --conferir      só procura erros, não grava nada
 *     cc programa.c --ri            mostra a representação intermediária
 *
 * Este arquivo é só o "ambiente": as funções de arquivo e de memória do
 * SaguiOS entregues ao compilador de verdade (compilador.h), que não sabe
 * em que sistema está. A biblioteca (/sistema/lib/libsagui.o) e os
 * cabeçalhos (/sistema/inc) vêm na imagem do sistema.
 *
 * A pilha de um programa do SaguiOS tem 64 KB, pouco para um compilador
 * (as funções que leem expressões chamam umas às outras a cada parêntese).
 * Por isso o cc pede 1 MB de memória comum e passa a usá-la como pilha
 * enquanto compila. */
#include "sagui.h"
#include "compilador.h"

#define BIBLIOTECA "/sistema/lib/libsagui.o"
#define CABECALHOS "/sistema/inc"
#define PILHA      (1024u * 1024u)

static int ler_arquivo(void *ctx, const char *caminho, unsigned char **dados, unsigned *tam)
{
    info_arquivo_t inf;
    unsigned char *d;
    unsigned lidos = 0;
    int fd, n;
    (void)ctx;
    if (info_arquivo(caminho, &inf) < 0 || inf.tipo == TIPO_PASTA) return -1;
    fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return -1;
    d = aloca(inf.tamanho + 1);
    if (!d) { fechar(fd); return -1; }
    while (lidos < inf.tamanho && (n = ler(fd, d + lidos, inf.tamanho - lidos)) > 0) lidos += (unsigned)n;
    fechar(fd);
    *dados = d;
    *tam = lidos;
    return 0;
}

static int gravar_arquivo(void *ctx, const char *caminho, const unsigned char *dados, unsigned tam)
{
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR), r;
    (void)ctx;
    if (fd < 0) return -1;
    r = escrever_tudo(fd, dados, tam);
    fechar(fd);
    return r < 0 ? -1 : 0;
}

static void *reservar(void *ctx, unsigned n) { (void)ctx; return aloca(n); }
static void liberar(void *ctx, void *p) { (void)ctx; libera(p); }
static void mensagem(void *ctx, const char *texto) { (void)ctx; escrever_texto(ERROS, texto); }
static void listagem(void *ctx, const char *texto) { (void)ctx; escrever_texto(SAIDA, texto); }

static const CcAmbiente ambiente = { NULL, ler_arquivo, gravar_arquivo, reservar, liberar, mensagem, listagem };
static CcOpcoes opcoes;
static int resultado;

/* Chamado já na pilha grande. */
void cc_na_pilha_grande(void) { resultado = cc_compilar(&ambiente, &opcoes); }

/* Troca ESP pelo topo da pilha nova, chama a função acima e volta. */
void trocar_pilha(void *topo);
__asm__(".text\n"
        ".globl trocar_pilha\n"
        "trocar_pilha:\n"
        "    push %ebp\n"
        "    mov %esp, %ebp\n"
        "    mov 8(%ebp), %esp\n"
        "    and $-16, %esp\n"          /* alinhada em 16: o SSE (H80) exige */
        "    call cc_na_pilha_grande\n"
        "    mov %ebp, %esp\n"
        "    pop %ebp\n"
        "    ret\n");

int principal(int argc, char **argv)
{
    static char saida[128];
    bool conferir = false;
    unsigned char *pilha;
    opcoes.biblioteca = BIBLIOTECA;
    opcoes.pastas[opcoes.qtd_pastas++] = CABECALHOS;
    opcoes.latin1 = 1;      /* o Terminal mostra Latin-1; os arquivos vêm em UTF-8 */
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-o") == 0 && i + 1 < argc) opcoes.saida = argv[++i];
        else if (compara_texto(argv[i], "-I") == 0 && i + 1 < argc && opcoes.qtd_pastas < CC_MAX_PASTAS) opcoes.pastas[opcoes.qtd_pastas++] = argv[++i];
        else if (compara_texto(argv[i], "--ri") == 0) opcoes.mostrar_ri = 1;
        else if (compara_texto(argv[i], "--conferir") == 0) conferir = true;
        else if (argv[i][0] == '-') { imprimir_em(ERROS, "cc: não conheço a opção %s\n", argv[i]); return 2; }
        else if (opcoes.qtd_fontes < CC_MAX_FONTES) opcoes.fontes[opcoes.qtd_fontes++] = argv[i];
    }
    if (opcoes.qtd_fontes == 0) {
        imprimir("uso: cc arquivo.c [-o programa] [--conferir] [--ri]\n"
                 "Compila um programa em C para o SaguiOS. O manual da linguagem está em docs/c-do-sagui.md.\n");
        return 2;
    }
    if (!opcoes.saida && !conferir) {
        /* sem -o: o nome do primeiro arquivo sem o ".c" */
        size_t n = tam_texto(opcoes.fontes[0]);
        copia_texto(saida, opcoes.fontes[0], sizeof saida);
        if (n > 2 && n < sizeof saida && saida[n - 2] == '.' && saida[n - 1] == 'c') saida[n - 2] = 0;
        else junta_texto(saida, ".prog", sizeof saida);
        opcoes.saida = saida;
    }
    if (conferir) opcoes.saida = NULL;
    pilha = aloca(PILHA);
    if (!pilha) { imprimir_em(ERROS, "cc: sem memória\n"); return 1; }
    /* O topo alinhado em 16 bytes: o gcc supõe isso e, com SSE2, guarda
     * valores de 16 bytes na pilha com instruções que exigem o alinhamento
     * (H82; o aloca só garante 8). */
    trocar_pilha((void *)(((unsigned)(pilha + PILHA) & ~15u) - 16));
    libera(pilha);
    return resultado;
}
