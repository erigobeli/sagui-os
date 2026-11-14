/* grep (procurar) - mostra as linhas que contêm um texto (H83, vindo do
 * pull request #7, do Gabriel).
 *
 *   grep [-i] [-n] [-v] [-c] <texto> [arquivo...]
 *
 * Sem arquivo, lê a entrada: assim funciona no fim de um pipe, como em
 * "ls bin | grep a". As opções podem vir juntas ("-in"):
 *   -i  ignora maiúsculas e minúsculas, inclusive nas letras acentuadas
 *       (É e é são a mesma letra);
 *   -n  mostra o número de cada linha;
 *   -v  inverte: mostra as linhas que NÃO têm o texto;
 *   -c  só conta quantas linhas seriam mostradas.
 * Com mais de um arquivo, cada linha sai com o nome do arquivo na frente.
 *
 * O código de saída segue o costume do grep dos sistemas Unix, para os
 * scripts poderem perguntar "achou?": 0 se alguma linha serviu, 1 se
 * nenhuma, 2 se um arquivo não pôde ser lido.
 *
 * O texto procurado é um texto simples (não uma "expressão regular"). O
 * SaguiOS guarda o texto em Latin-1 (um byte por letra, PRD 4.4), então
 * comparar letra a letra é comparar byte a byte. */
#include "sagui.h"

#define LINHA_MAX 1024

static bool ignorar_caixa, numerar, inverter, so_contar, com_nome;

/* Minúscula de uma letra Latin-1. As maiúsculas acentuadas (À a Þ) ficam
 * 32 posições antes das minúsculas, como no ASCII; a exceção é o × (0xD7),
 * que não é letra. */
static unsigned char minuscula(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return (unsigned char)(c + 32);
    return c;
}

/* A linha contém o texto? A busca é a mais simples: tenta cada posição da
 * linha como começo. Para linhas de terminal, isso é rápido o bastante. */
static bool contem(const char *linha, const char *texto)
{
    if (!*texto) return true;
    for (; *linha; linha++) {
        const unsigned char *a = (const unsigned char *)linha, *b = (const unsigned char *)texto;
        if (ignorar_caixa)
            while (*a && *b && minuscula(*a) == minuscula(*b)) { a++; b++; }
        else
            while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}

/* Leitura em blocos: ler a entrada de 4 KB em 4 KB é muito mais rápido do
 * que um byte por chamada de sistema (o que o ler_linha faz para não
 * consumir o que é de outro programa; aqui a entrada inteira é nossa). */
typedef struct {
    int  fd;
    char buf[4096];
    int  pos, qtd;
    bool fim;
} leitor_t;

/* Devolve o tamanho da linha (sem o '\n'), ou -1 no fim. Linhas maiores
 * que LINHA_MAX são cortadas (o resto é descartado). */
static int proxima_linha(leitor_t *l, char *linha)
{
    int n = 0;
    bool leu = false;
    for (;;) {
        if (l->pos == l->qtd) {
            if (l->fim) break;
            l->qtd = ler(l->fd, l->buf, sizeof l->buf);
            l->pos = 0;
            if (l->qtd <= 0) { l->qtd = 0; l->fim = true; break; }
        }
        char c = l->buf[l->pos++];
        leu = true;
        if (c == '\n') break;
        if (c != '\r' && n < LINHA_MAX - 1) linha[n++] = c;
    }
    linha[n] = 0;
    return leu ? n : -1;
}

/* Procura num arquivo já aberto. Devolve quantas linhas serviram. */
static unsigned procurar(int fd, const char *nome, const char *texto)
{
    static leitor_t l;
    static char linha[LINHA_MAX];
    l.fd = fd; l.pos = l.qtd = 0; l.fim = false;
    unsigned num = 0, achadas = 0;
    while (proxima_linha(&l, linha) >= 0) {
        num++;
        if (contem(linha, texto) == inverter) continue;
        achadas++;
        if (so_contar) continue;
        if (com_nome) imprimir("%s:", nome);
        if (numerar) imprimir("%u:", num);
        escrever_texto(SAIDA, linha);
        escrever_texto(SAIDA, "\n");
    }
    if (so_contar) {
        if (com_nome) imprimir("%s:", nome);
        imprimir("%u\n", achadas);
    }
    return achadas;
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: grep [-i] [-n] [-v] [-c] <texto> [arquivo...]\n");
    return 2;
}

int principal(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'i') ignorar_caixa = true;
            else if (*o == 'n') numerar = true;
            else if (*o == 'v') inverter = true;
            else if (*o == 'c') so_contar = true;
            else { imprimir_em(ERROS, "grep: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    if (i >= argc) return uso();
    const char *texto = argv[i++];
    int arquivos = argc - i;
    com_nome = arquivos > 1;

    unsigned achadas = 0;
    bool erro = false;
    if (arquivos == 0) {
        achadas = procurar(ENTRADA, "(entrada)", texto);
    } else {
        for (; i < argc; i++) {
            info_arquivo_t inf;
            if (info_arquivo(argv[i], &inf) == 0 && inf.tipo == TIPO_PASTA) {
                imprimir_em(ERROS, "grep: %s é uma pasta\n", argv[i]);
                erro = true;
                continue;
            }
            int fd = abrir(argv[i], ABRIR_LER);
            if (fd < 0) { imprimir_em(ERROS, "grep: %s: %s\n", argv[i], nome_erro(fd)); erro = true; continue; }
            achadas += procurar(fd, argv[i], texto);
            fechar(fd);
        }
    }
    if (erro && !achadas) return 2;
    return achadas ? 0 : 1;
}
