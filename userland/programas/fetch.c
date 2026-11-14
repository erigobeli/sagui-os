/* fetch (ficha) - a "ficha" do sistema: um sagui desenhado com letras e,
 * ao lado, o sistema, a versão, o processador, a memória, a tela e outros
 * dados (H83, vindo do pull request #7, do Gabriel).
 *
 * É um costume antigo dos terminais: um programa que mostra o desenho do
 * sistema e um resumo da máquina, bom para tirar foto da tela. Aqui o
 * desenho é um sagui-de-tufos-brancos, o mascote do SaguiOS: tufos brancos
 * nas orelhas, rosto escuro, corpo cinza e a cauda listrada.
 *
 * As cores usam as sequências de controle de terminal (ESC [ ... m), que o
 * Terminal do SaguiOS entende (16, 256 e 24 bits de cor). */
#include "sagui.h"

/* Cores pela tabela de 256 do terminal (ESC [ 38;5;n m). */
#define BRANCO   "\x1b[1;97m"
#define MARROM   "\x1b[38;5;94m"
#define CINZA    "\x1b[38;5;250m"
#define ESCURO   "\x1b[38;5;240m"
#define VERDE    "\x1b[1;32m"
#define AMARELO  "\x1b[33m"
#define NORMAL   "\x1b[0m"

/* O sagui. Cada linha já traz as suas cores; todas têm 22 colunas
 * visíveis, para as informações ficarem alinhadas ao lado. */
#define ARTE_L 22
static const char *const arte[] = {
    BRANCO "  \\\\\\\\\\ " MARROM ".-\"\"-." BRANCO " /////  ",
    BRANCO "   \\\\\\\\" MARROM "/      \\" BRANCO "////   ",
    MARROM "       | " BRANCO "o" MARROM "  " BRANCO "o" MARROM " |       ",
    MARROM "       |  " ESCURO "''" MARROM "  |       ",
    MARROM "        \\ " ESCURO "--" MARROM " /        ",
    CINZA  "      .-'----'-.      ",
    CINZA  "     /  |    |  \\     ",
    CINZA  "    (__/|    |\\__)    ",
    CINZA  "        |_/\\_|  " ESCURO "=" CINZA "=    ",
    CINZA  "       (_/  \\_)  " ESCURO "=" CINZA "=   ",
    CINZA  "                 " ESCURO "=" CINZA "=   ",
    CINZA  "                " ESCURO "=" CINZA "=    ",
};
#define ARTE_LINHAS (int)(sizeof arte / sizeof arte[0])

#define MAX_LINHAS 16
static char info[MAX_LINHAS][96];
static int qtd_info;

static void item(const char *rotulo, const char *fmt, ...)
{
    if (qtd_info >= MAX_LINHAS) return;
    char valor[80];
    va_list ap;
    va_start(ap, fmt);
    formatar_lista(valor, sizeof valor, fmt, ap);
    va_end(ap);
    if (rotulo) formatar_texto(info[qtd_info++], sizeof info[0], VERDE "%s:" NORMAL " %s", rotulo, valor);
    else copia_texto(info[qtd_info++], valor, sizeof info[0]);
}

/* O nome do processador, que ele mesmo informa pela instrução CPUID (as
 * funções 0x80000002 a 0x80000004 devolvem 48 letras; manual da Intel,
 * volume 2A, "CPUID"). Processadores antigos não têm essas funções. */
static void nome_processador(char *t, size_t max)
{
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u));
    if (a < 0x80000004u) { copia_texto(t, "x86 de 32 bits", max); return; }
    unsigned p[13];
    for (unsigned f = 0; f < 3; f++) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000002u + f));
        p[f * 4] = a; p[f * 4 + 1] = b; p[f * 4 + 2] = c; p[f * 4 + 3] = d;
    }
    p[12] = 0;
    const char *s = (const char *)p;
    while (*s == ' ') s++;
    /* Junta espaços repetidos ("Intel(R) Core(TM)  i3" tem vários). */
    size_t k = 0;
    for (; *s && k + 1 < max; s++)
        if (!(*s == ' ' && (s[1] == ' ' || s[1] == 0))) t[k++] = *s;
    t[k] = 0;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    info_versao_t v;
    info_memoria_t m;
    static info_processo_t procs[64];
    info_espaco_t esp[2];
    bool tem_v = info_sistema(INFO_VERSAO, &v, sizeof v) == 0;
    bool tem_m = info_sistema(INFO_MEMORIA, &m, sizeof m) == 0;
    int nproc = info_sistema(INFO_PROCESSOS, procs, 64);
    int nesp = info_sistema(INFO_DISCOS_ESPACO, esp, 2);

    item(NULL, VERDE "SaguiOS" NORMAL);
    item(NULL, "-------");
    item("Sistema", "SaguiOS %s", tem_v ? v.versao : "?");
    item("Núcleo", "SaguiOS i686 (32 bits, escrito do zero)");
    if (tem_v) {
        unsigned s = v.segundos_ligado;
        if (s >= 3600) item("Ligado há", "%u h %u min", s / 3600, s / 60 % 60);
        else item("Ligado há", "%u min %u s", s / 60, s % 60);
    }
    char cpu[64];
    nome_processador(cpu, sizeof cpu);
    item("Processador", "%s", cpu);
    if (tem_m) item("Memória", "%u MB usados de %u MB", m.usada_kb / 1024, m.gerida_kb / 1024);
    if (tem_v) {
        if (v.largura > 200) item("Tela", "%u x %u", v.largura, v.altura);
        else item("Tela", "texto, %u x %u letras", v.largura, v.altura);
    }
    item("Shell", "sagsh");
    if (nproc > 0) item("Processos", "%d", nproc);
    for (int i = 1; i < nesp; i++)   /* o pendrive, se estiver montado */
        item("Pendrive", "%u MB livres de %u MB", esp[i].livre_kb / 1024, esp[i].total_kb / 1024);
    item(NULL, "");
    /* A paleta: as 8 cores normais e as 8 fortes, como blocos coloridos. */
    char cores[2][200];
    for (int linha = 0; linha < 2; linha++) {
        cores[linha][0] = 0;
        for (int c = 0; c < 8; c++) {
            char um[24];
            formatar_texto(um, sizeof um, "\x1b[%dm   ", (linha ? 100 : 40) + c);
            junta_texto(cores[linha], um, sizeof cores[linha]);
        }
        junta_texto(cores[linha], NORMAL, sizeof cores[linha]);
        item(NULL, "%s", cores[linha]);
    }

    /* Desenho e informações lado a lado: linha por linha, a arte (ou
     * espaços, depois que ela acaba) e a informação daquela linha. */
    int total = ARTE_LINHAS > qtd_info ? ARTE_LINHAS : qtd_info;
    imprimir("\n");
    for (int i = 0; i < total; i++) {
        if (i < ARTE_LINHAS) imprimir("%s" NORMAL, arte[i]);
        else imprimir("%*s", ARTE_L, "");
        imprimir("  %s\n", i < qtd_info ? info[i] : "");
    }
    imprimir(NORMAL "\n");
    return 0;
}
