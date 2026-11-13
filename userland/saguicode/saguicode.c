/* saguicode.c - o Sagui Code, agente de programação no Terminal (H70 a H72).
 *
 * O laço do agente:
 *   1. o pedido do usuário entra na conversa;
 *   2. a conversa vai para o modelo (ia.c), e a resposta aparece enquanto é
 *      gerada;
 *   3. se o modelo pediu ferramentas, cada uma é executada aqui (pedindo
 *      licença ao usuário antes de sobrescrever arquivos ou rodar programas)
 *      e o resultado volta para a conversa; volta ao passo 2;
 *   4. sem pedidos de ferramenta, o pedido acabou: mostra o gasto.
 * Há um limite de passos por pedido e um limite de gasto por sessão.
 *
 * As ferramentas são operações do próprio SaguiOS: arquivos, o shell
 * (sagsh -c), o compilador (cc) e os programas. Os arquivos do SaguiOS são em
 * Latin-1; a conversa é em UTF-8: a conversão é feita na entrada e na saída de
 * cada ferramenta.
 *
 * Contexto: o texto de instruções, o manual do C do Sagui e os cabeçalhos da
 * libsagui vão na primeira mensagem (marcada para o cache de prompt). Quando
 * a conversa passa do limite configurado, os resultados antigos de
 * ferramentas viram um resumo de uma linha e, se ainda não bastar, os pedidos
 * mais antigos saem inteiros.
 *
 * A chave fica em /usb/.saguios/chaves.txt (linha "openrouter = ...") e nunca
 * vai para a tela, o log ou as conversas salvas. */
#include "ia.h"

#define MAX_LINHA      2000
#define MAX_RESULTADO  12000      /* o que uma ferramenta devolve ao modelo, no máximo */

static ia_t ia;
static int max_passos = 25;
static long limite_micro = 1000000;       /* US$ 1,00 por sessão */
static int max_contexto = 60000;           /* tokens (estimados por letras / 4) */
static long gasto_sessao;
static int tokens_sessao_entrada, tokens_sessao_saida;
static bool permitir_tudo;
static char pasta_trabalho[96];
static char pasta_config[64];

/* ------------------------------------------------------------ tela */

static void cor(const char *c) { escrever_texto(SAIDA, c); }
#define CINZA    "\x1b[90m"
#define VERDE    "\x1b[32m"
#define AMARELO  "\x1b[33m"
#define VERMELHO "\x1b[31m"
#define AZUL     "\x1b[36m"
#define NEGRITO  "\x1b[1m"
#define NORMAL   "\x1b[0m"

static void ao_texto(void *ctx, const char *s, unsigned n)
{
    (void)ctx;
    escrever_tudo(SAIDA, s, n);     /* o Terminal entende UTF-8 */
}

/* Lê uma linha do teclado (modo bruto do Terminal), com Backspace e o
 * histórico nas setas para cima e para baixo. */
static char historico[16][MAX_LINHA];
static int hist_qtd;

static int ler_linha_teclado(const char *prompt, char *buf, int max)
{
    escrever_texto(SAIDA, prompt);
    int n = 0, h = hist_qtd;
    buf[0] = 0;
    for (;;) {
        char c;
        if (ler(ENTRADA, &c, 1) <= 0) return -1;
        if (c == '\n' || c == '\r') break;
        if (c == 3) { escrever_texto(SAIDA, "^C\n"); buf[0] = 0; return 0; }
        if (c == 4 && n == 0) return -1;                       /* Ctrl+D */
        if (c == 8 || c == 127) { if (n > 0) { n--; escrever_texto(SAIDA, "\b \b"); } continue; }
        if (c == 27) {
            char l;
            if (ler(ENTRADA, &l, 1) <= 0) return -1;
            if ((l == TECLA_B_CIMA && h > 0) || (l == TECLA_B_BAIXO && h < hist_qtd)) {
                h += l == TECLA_B_CIMA ? -1 : 1;
                while (n > 0) { escrever_texto(SAIDA, "\b \b"); n--; }
                if (h < hist_qtd) { copia_texto(buf, historico[h % 16], (size_t)max); n = (int)tam_texto(buf); escrever_tudo(SAIDA, buf, (unsigned)n); }
            }
            continue;
        }
        if ((unsigned char)c < 32) continue;
        if (n < max - 1) { buf[n++] = c; escrever(SAIDA, &c, 1); }
    }
    buf[n] = 0;
    escrever_texto(SAIDA, "\n");
    if (n > 0) { copia_texto(historico[hist_qtd % 16], buf, MAX_LINHA); hist_qtd++; }
    return n;
}

static bool perguntar_sim(const char *pergunta)
{
    if (permitir_tudo) return true;
    char r[8];
    cor(AMARELO);
    int n = ler_linha_teclado(pergunta, r, sizeof r);
    cor(NORMAL);
    if (n > 0 && (r[0] == 't' || r[0] == 'T')) { permitir_tudo = true; return true; }
    return n > 0 && (r[0] == 's' || r[0] == 'S');
}

static void mostrar_dinheiro(long micro)
{
    /* US$ com 4 casas: 1234 micro = 0,0012 */
    long dec = (micro + 50) / 100;           /* décimos de milésimo */
    imprimir("US$ %ld,%04ld", dec / 10000, dec % 10000);
}

/* ------------------------------------------------------------ texto */

/* Latin-1 -> UTF-8, em memória nova. */
static char *para_utf8(const char *s, unsigned n)
{
    char *d = aloca(2 * n + 1);
    if (!d) return NULL;
    unsigned k = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) d[k++] = (char)c;
        else { d[k++] = (char)(0xC0 | (c >> 6)); d[k++] = (char)(0x80 | (c & 63)); }
    }
    d[k] = 0;
    return d;
}

/* UTF-8 -> Latin-1, no lugar. */
static unsigned para_latin1(char *s)
{
    size_t sobra = 0;
    unsigned n = (unsigned)utf8_para_latin1(s, tam_texto(s), &sobra);
    s[n] = 0;                    /* a conversão encurta o texto e não põe o zero no fim */
    return n;
}

typedef struct { char *d; unsigned n, cap; } texto_t;

static void tx_por(texto_t *t, const char *s, unsigned n)
{
    if (t->n + n + 1 > t->cap) {
        unsigned c = t->cap ? t->cap * 2 : 1024;
        while (c < t->n + n + 1) c *= 2;
        char *x = realoca(t->d, c);
        if (!x) return;
        t->d = x;
        t->cap = c;
    }
    memcpy(t->d + t->n, s, n);
    t->n += n;
    t->d[t->n] = 0;
}

static void tx_texto(texto_t *t, const char *s) { tx_por(t, s, (unsigned)tam_texto(s)); }

static void tx_formatar(texto_t *t, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = formatar_lista(buf, sizeof buf, fmt, ap);
    va_end(ap);
    tx_por(t, buf, (unsigned)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
}

/* ------------------------------------------------------------ rodar programas */

/* Roda argv com a saída (e os erros) num pipe. Espera até "segundos";
 * depois disso, se matar_no_fim, encerra o programa; se não, deixa rodando
 * (programas com janela). Devolve o código de saída, ou -1000 se continua. */
static int rodar_capturando(const char *caminho, char *const argv[], int segundos, bool matar_no_fim, texto_t *saida)
{
    int p[2], e[2];
    if (criar_pipe(p) < 0) { tx_texto(saida, "(não consegui criar o pipe)"); return -1; }
    if (criar_pipe(e) < 0) { fechar(p[0]); fechar(p[1]); tx_texto(saida, "(não consegui criar o pipe)"); return -1; }
    int g0 = duplicar(ENTRADA, 14), g1 = duplicar(SAIDA, 15), g2 = duplicar(ERROS, 16);
    duplicar(e[0], ENTRADA);              /* a entrada é um pipe vazio que já acabou */
    duplicar(p[1], SAIDA);
    duplicar(p[1], ERROS);
    fechar(e[1]);
    int pid = executar(caminho, argv);
    duplicar(g0, ENTRADA);
    duplicar(g1, SAIDA);
    duplicar(g2, ERROS);
    fechar(g0); fechar(g1); fechar(g2);
    fechar(p[1]);
    fechar(e[0]);
    if (pid < 0) {
        fechar(p[0]);
        tx_formatar(saida, "(não consegui rodar %s: %s)", caminho, nome_erro(pid));
        return pid;
    }
    unsigned fim = tempo_ms() + (unsigned)segundos * 1000;
    int codigo = -1000;
    bool terminou = false;
    char buf[512];
    for (;;) {
        int d = disponivel(p[0]);
        if (d > 0) {
            int n = ler(p[0], buf, sizeof buf);
            if (n > 0 && saida->n < 200000) tx_por(saida, buf, (unsigned)n);
            continue;
        }
        if (d < 0 && terminou) break;            /* pipe quebrado: ninguém mais escreve */
        if (!terminou) {
            int c, r = recolher(&c);
            if (r == pid) { terminou = true; codigo = c; continue; }
        }
        if (d < 0) {                             /* fechou a saída mas ainda roda */
            if (!terminou) { int c; esperar(pid, &c); codigo = c; terminou = true; }
            break;
        }
        if ((int)(tempo_ms() - fim) >= 0) break;
        dormir(10);
    }
    fechar(p[0]);
    if (!terminou) {
        if (matar_no_fim) { matar(pid); int c; esperar(pid, &c); tx_formatar(saida, "\n(encerrado depois de %d s)", segundos); codigo = -999; }
        else tx_formatar(saida, "\n(o programa continua rodando, processo %d)", pid);
    }
    return codigo;
}

/* ------------------------------------------------------------ ferramentas */

static const char FERRAMENTAS[] =
    "["
    "{\"type\":\"function\",\"function\":{\"name\":\"ler_arquivo\",\"description\":\"Lê um arquivo de texto do SaguiOS.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"caminho\":{\"type\":\"string\"}},\"required\":[\"caminho\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"escrever_arquivo\",\"description\":\"Cria ou sobrescreve um arquivo de texto com o conteúdo dado.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"caminho\":{\"type\":\"string\"},\"conteudo\":{\"type\":\"string\"}},\"required\":[\"caminho\",\"conteudo\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"editar_arquivo\",\"description\":\"Troca um trecho de um arquivo por outro. O trecho antigo tem de aparecer exatamente uma vez.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"caminho\":{\"type\":\"string\"},\"trecho_antigo\":{\"type\":\"string\"},\"trecho_novo\":{\"type\":\"string\"}},\"required\":[\"caminho\",\"trecho_antigo\",\"trecho_novo\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"listar_pasta\",\"description\":\"Lista os arquivos e pastas de uma pasta.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"caminho\":{\"type\":\"string\"}},\"required\":[\"caminho\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"executar_comando\",\"description\":\"Roda uma linha de comando no shell do SaguiOS (sagsh) e devolve a saída. Pede licença ao usuário.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"comando\":{\"type\":\"string\"}},\"required\":[\"comando\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"compilar\",\"description\":\"Compila um programa em C do SaguiOS com o cc. Devolve as mensagens do compilador (erros com linha, coluna e dica).\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"arquivo\":{\"type\":\"string\"},\"saida\":{\"type\":\"string\",\"description\":\"nome do programa gerado\"}},\"required\":[\"arquivo\",\"saida\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"rodar\",\"description\":\"Roda um programa e devolve o que ele escreveu nos primeiros segundos. Programas com janela continuam abertos. Pede licença ao usuário.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"programa\":{\"type\":\"string\"},\"argumentos\":{\"type\":\"string\"}},\"required\":[\"programa\"]}}}"
    "]";

/* Um argumento em Latin-1 (caminhos e conteúdo de arquivos), em memória nova. */
static char *arg_latin1(json_t *a, const char *nome)
{
    json_t *c = json_campo(a, nome);
    const char *s = json_texto(c);
    unsigned n = c && c->tipo == JSON_TEXTO ? c->tam : 0;
    char *d = aloca(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = 0;
    para_latin1(d);
    return d;
}

static int ler_inteiro(const char *caminho, texto_t *t, unsigned max)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return fd;
    char buf[1024];
    int n;
    while ((n = ler(fd, buf, sizeof buf)) > 0 && t->n < max) tx_por(t, buf, (unsigned)n);
    fechar(fd);
    return 0;
}

static int gravar_inteiro(const char *caminho, const char *d, unsigned n)
{
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) return fd;
    int r = escrever_tudo(fd, d, n);
    fechar(fd);
    return r < 0 ? r : 0;
}

/* Divide "a b 'c d'" em argv (no lugar). */
static int separar_argumentos(char *s, char **argv, int max)
{
    int n = 0;
    while (*s && n < max - 1) {
        while (*s == ' ') s++;
        if (!*s) break;
        char aspas = (*s == '"' || *s == '\'') ? *s++ : 0;
        argv[n++] = s;
        while (*s && (aspas ? *s != aspas : *s != ' ')) s++;
        if (*s) *s++ = 0;
    }
    argv[n] = NULL;
    return n;
}

static void caminho_programa(const char *nome, char *out, int max)
{
    info_arquivo_t i;
    if (acha_char(nome, '/')) { copia_texto(out, nome, (size_t)max); return; }
    formatar_texto(out, (size_t)max, "%s/%s", pasta_trabalho, nome);
    if (info_arquivo(out, &i) >= 0) return;
    formatar_texto(out, (size_t)max, "/bin/%s", nome);
    if (info_arquivo(out, &i) >= 0) return;
    formatar_texto(out, (size_t)max, "/usb/programas/%s", nome);
}

/* Executa uma ferramenta; o resultado (UTF-8) vai em "res". */
static void executar_ferramenta(const char *nome, const char *argumentos, texto_t *res)
{
    char erro[96];
    json_t *a = json_ler(argumentos, (unsigned)tam_texto(argumentos), erro, sizeof erro);
    if (!a) { tx_formatar(res, "Erro: os argumentos não são um JSON válido (%s).", erro); return; }
    texto_t t = { 0, 0, 0 };
    if (compara_texto(nome, "ler_arquivo") == 0) {
        char *c = arg_latin1(a, "caminho");
        imprimir(CINZA "  lendo %s\n" NORMAL, c);
        int r = ler_inteiro(c, &t, 48 * 1024);
        if (r < 0) tx_formatar(res, "Erro: não consegui ler %s (%s).", c, nome_erro(r));
        else {
            tx_texto(res, t.d ? t.d : "");
            if (t.n >= 48 * 1024) tx_texto(res, "\n[arquivo cortado em 48 KB]");
        }
        libera(c);
    } else if (compara_texto(nome, "escrever_arquivo") == 0 || compara_texto(nome, "editar_arquivo") == 0) {
        bool editar = nome[0] == 'e' && nome[1] == 'd';
        char *c = arg_latin1(a, "caminho");
        info_arquivo_t inf;
        bool existe = info_arquivo(c, &inf) >= 0;
        char *novo = NULL;
        unsigned tam_novo = 0;
        if (editar) {
            char *antigo = arg_latin1(a, "trecho_antigo"), *trocar = arg_latin1(a, "trecho_novo");
            if (ler_inteiro(c, &t, 512 * 1024) < 0 || !t.d) tx_formatar(res, "Erro: não consegui ler %s.", c);
            else {
                unsigned ta = (unsigned)tam_texto(antigo), vezes = 0;
                char *onde = NULL;
                for (unsigned i = 0; ta && i + ta <= t.n; i++) if (memcmp(t.d + i, antigo, ta) == 0) { if (!onde) onde = t.d + i; vezes++; }
                if (vezes != 1) tx_formatar(res, "Erro: o trecho antigo aparece %u vezes em %s (tem de ser exatamente 1). Leia o arquivo e mande um trecho maior.", vezes, c);
                else {
                    unsigned tn = (unsigned)tam_texto(trocar), antes = (unsigned)(onde - t.d);
                    tam_novo = t.n - ta + tn;
                    novo = aloca(tam_novo + 1);
                    if (novo) {
                        memcpy(novo, t.d, antes);
                        memcpy(novo + antes, trocar, tn);
                        memcpy(novo + antes + tn, onde + ta, t.n - antes - ta);
                    }
                }
            }
            libera(antigo);
            libera(trocar);
        } else {
            novo = arg_latin1(a, "conteudo");
            tam_novo = novo ? (unsigned)tam_texto(novo) : 0;
        }
        if (novo) {
            char pergunta[200];
            formatar_texto(pergunta, sizeof pergunta, "  %s %s (%u bytes)? (s)im, (n)ão, (t)udo nesta sessão: ",
                           editar ? "Editar" : existe ? "Sobrescrever" : "Criar", c, tam_novo);
            bool ok = (!existe && !editar) ? true : perguntar_sim(pergunta);
            if (!existe && !editar) imprimir(CINZA "  criando %s (%u bytes)\n" NORMAL, c, tam_novo);
            if (!ok) tx_texto(res, "O usuário não permitiu mudar o arquivo.");
            else {
                int r = gravar_inteiro(c, novo, tam_novo);
                if (r < 0) tx_formatar(res, "Erro: não consegui gravar %s (%s).", c, nome_erro(r));
                else tx_formatar(res, "Arquivo %s %s (%u bytes).", c, editar ? "editado" : "gravado", tam_novo);
                registrar("saguicode: %s %s (%u bytes)", editar ? "editou" : "gravou", c, tam_novo);
            }
            libera(novo);
        }
        libera(c);
    } else if (compara_texto(nome, "listar_pasta") == 0) {
        char *c = arg_latin1(a, "caminho");
        entrada_pasta_t e;
        int i = 0;
        for (; listar(c[0] ? c : ".", i, &e) == 1; i++) {
            tx_formatar(res, "%s%s  %u\n", e.nome, e.tipo == TIPO_PASTA ? "/" : "", e.tamanho);
        }
        if (i == 0) tx_formatar(res, "(pasta vazia ou inexistente: %s)", c);
        libera(c);
    } else if (compara_texto(nome, "executar_comando") == 0) {
        char *cmd = arg_latin1(a, "comando");
        char pergunta[300];
        formatar_texto(pergunta, sizeof pergunta, "  Rodar \"%s\"? (s)im, (n)ão, (t)udo nesta sessão: ", cmd);
        if (!perguntar_sim(pergunta)) tx_texto(res, "O usuário não permitiu rodar o comando.");
        else {
            char *argv[] = { "sagsh", "-c", cmd, NULL };
            int cod = rodar_capturando("/bin/sagsh", argv, 30, true, &t);
            tx_texto(res, t.d ? t.d : "");
            tx_formatar(res, "\n(código de saída %d)", cod);
            registrar("saguicode: comando %s (codigo %d)", cmd, cod);
        }
        libera(cmd);
    } else if (compara_texto(nome, "compilar") == 0) {
        char *arq = arg_latin1(a, "arquivo"), *sai = arg_latin1(a, "saida");
        imprimir(CINZA "  cc %s -o %s\n" NORMAL, arq, sai);
        char *argv[] = { "cc", arq, "-o", sai, NULL };
        int cod = rodar_capturando("/bin/cc", argv, 60, true, &t);
        tx_texto(res, t.d ? t.d : "");
        if (cod == 0) tx_formatar(res, "\nCompilou sem erros: %s.", sai);
        else tx_formatar(res, "\nA compilação falhou (código %d).", cod);
        if (cod == 0) imprimir(VERDE "  compilou\n" NORMAL);
        else imprimir(VERMELHO "  erro de compilação\n" NORMAL);
        registrar("saguicode: compilar %s -> %s (codigo %d)", arq, sai, cod);
        libera(arq);
        libera(sai);
    } else if (compara_texto(nome, "rodar") == 0) {
        char *prog = arg_latin1(a, "programa"), *args = arg_latin1(a, "argumentos");
        char pergunta[300];
        formatar_texto(pergunta, sizeof pergunta, "  Rodar o programa %s %s? (s)im, (n)ão, (t)udo nesta sessão: ", prog, args ? args : "");
        if (!perguntar_sim(pergunta)) tx_texto(res, "O usuário não permitiu rodar o programa.");
        else {
            char caminho[128];
            caminho_programa(prog, caminho, sizeof caminho);
            char *argv[16];
            argv[0] = prog;
            int n = args ? separar_argumentos(args, argv + 1, 15) : 0;
            argv[n + 1] = NULL;
            int cod = rodar_capturando(caminho, argv, 8, false, &t);
            tx_texto(res, t.d ? t.d : "");
            if (cod != -1000) tx_formatar(res, "\n(código de saída %d)", cod);
            registrar("saguicode: rodou %s (codigo %d)", caminho, cod);
        }
        libera(prog);
        libera(args);
    } else {
        tx_formatar(res, "Erro: não existe a ferramenta %s.", nome);
    }
    libera(t.d);
    json_liberar(a);
    if (res->n > MAX_RESULTADO) {
        res->n = MAX_RESULTADO;
        res->d[res->n] = 0;
        tx_texto(res, "\n[resultado cortado]");
    }
}

/* ------------------------------------------------------------ contexto */

static int estimar_tokens(void)
{
    unsigned letras = 0;
    for (int i = 0; i < ia.qtd; i++) {
        if (ia.msgs[i].conteudo) letras += (unsigned)tam_texto(ia.msgs[i].conteudo);
        if (ia.msgs[i].chamadas) letras += (unsigned)tam_texto(ia.msgs[i].chamadas);
    }
    letras += sizeof FERRAMENTAS;
    return (int)(letras / 4);
}

/* Diminui a conversa até caber em max_contexto: primeiro resume os
 * resultados antigos de ferramentas, depois tira os pedidos mais antigos
 * (cada pedido: da mensagem do usuário até antes da próxima). A primeira
 * mensagem (as instruções) e o pedido atual ficam sempre. */
static void cortar_contexto(void)
{
    int antes = estimar_tokens();
    if (antes <= max_contexto) return;
    int ultimo_usuario = -1;
    for (int i = ia.qtd - 1; i >= 0; i--) if (compara_texto(ia.msgs[i].papel, "user") == 0) { ultimo_usuario = i; break; }
    for (int i = 1; i < ultimo_usuario && estimar_tokens() > max_contexto; i++) {
        ia_mensagem_t *m = &ia.msgs[i];
        if (compara_texto(m->papel, "tool") == 0 && m->conteudo && tam_texto(m->conteudo) > 200) {
            libera(m->conteudo);
            char *r = aloca(64);
            if (r) copia_texto(r, "[resultado antigo removido para economizar espaço]", 64);
            m->conteudo = r;
        }
    }
    while (estimar_tokens() > max_contexto) {
        /* Acha o primeiro pedido (depois das instruções) que não é o atual. */
        int ini = -1, fim = -1;
        for (int i = 1; i < ia.qtd; i++) if (compara_texto(ia.msgs[i].papel, "user") == 0) { ini = i; break; }
        if (ini < 0) break;
        for (int i = ini + 1; i < ia.qtd; i++) if (compara_texto(ia.msgs[i].papel, "user") == 0) { fim = i; break; }
        if (fim < 0) break;                   /* só sobrou o pedido atual */
        for (int k = ini; k < fim; k++) ia_apagar_mensagem(&ia, ini);
    }
    imprimir(CINZA "[conversa resumida: de ~%d para ~%d tokens]\n" NORMAL, antes, estimar_tokens());
    registrar("saguicode: contexto cortado de %d para %d tokens", antes, estimar_tokens());
}

/* ------------------------------------------------------------ instruções */

static const char INSTRUCOES[] =
    "Você é o Sagui Code, um agente de programação que roda DENTRO do SaguiOS, um sistema operacional x86 de 32 bits "
    "escrito do zero por IA. Você ajuda o usuário a criar e consertar programas em C para o SaguiOS.\n\n"
    "Regras:\n"
    "- Responda em português do Brasil, curto e direto.\n"
    "- Os programas usam só a libsagui (não existe libc: nada de printf, malloc, stdio.h). A função principal é "
    "`int principal(int argc, char **argv)`. Use imprimir() para escrever no terminal.\n"
    "- Não existe ponto flutuante: use inteiros (por exemplo, décimos ou centésimos).\n"
    "- Programas com janela usam gui.h: jan_abrir, g_retangulo, g_texto, proximo_evento etc.\n"
    "- Trabalhe com as ferramentas: escreva o arquivo .c, compile com `compilar`, leia os erros (eles têm linha, coluna "
    "e uma dica), corrija com `editar_arquivo` e compile de novo até passar. Depois rode com `rodar` para conferir.\n"
    "- Antes de mudar um arquivo que já existe, leia-o.\n"
    "- A pasta de trabalho é %s; use caminhos dentro dela.\n"
    "- Quando terminar, diga em uma ou duas frases o que fez e como o usuário roda o programa.\n";

static void acrescentar_arquivo(texto_t *t, const char *titulo, const char *caminho)
{
    texto_t a = { 0, 0, 0 };
    if (ler_inteiro(caminho, &a, 200 * 1024) < 0 || !a.d) return;
    tx_formatar(t, "\n\n# %s (%s)\n\n", titulo, caminho);
    tx_texto(t, a.d);
    libera(a.d);
}

static void iniciar_conversa(void)
{
    ia_liberar(&ia);
    texto_t t = { 0, 0, 0 };
    tx_formatar(&t, INSTRUCOES, pasta_trabalho);
    acrescentar_arquivo(&t, "Manual do C do SaguiOS", "/sistema/ajuda/c-do-sagui.md");
    acrescentar_arquivo(&t, "Cabeçalho sagui.h (a libsagui)", "/sistema/inc/sagui.h");
    acrescentar_arquivo(&t, "Cabeçalho gui.h (janelas)", "/sistema/inc/gui.h");
    char *u = para_utf8(t.d ? t.d : "", t.n);
    int i = ia_acrescentar(&ia, "system", u ? u : "", NULL, NULL);
    libera(u);
    if (i >= 0) ia.msgs[i].guardar_cache = true;
    libera(t.d);
}

/* ------------------------------------------------------------ um pedido */

static void montar_chamadas(const ia_resposta_t *r, json_escritor_t *e)
{
    je_iniciar(e);
    je_lista(e);
    for (int i = 0; i < r->n_chamadas; i++) {
        const ia_chamada_t *c = &r->chamadas[i];
        je_objeto(e);
        je_chave(e, "id"); je_texto(e, c->id);
        je_chave(e, "type"); je_texto(e, "function");
        je_chave(e, "function"); je_objeto(e);
        je_chave(e, "name"); je_texto(e, c->nome);
        je_chave(e, "arguments"); je_texto(e, c->argumentos ? c->argumentos : "{}");
        je_fim_objeto(e);
        je_fim_objeto(e);
    }
    je_fim_lista(e);
}

static void resumo_args(const char *args, char *out, int max)
{
    int k = 0;
    for (const char *p = args; *p && k < max - 4; p++) out[k++] = *p == '\n' ? ' ' : *p;
    if (args && (int)tam_texto(args) > max - 4) { out[k++] = '.'; out[k++] = '.'; out[k++] = '.'; }
    out[k] = 0;
}

static void pedido(const char *texto_latin1)
{
    char *u = para_utf8(texto_latin1, (unsigned)tam_texto(texto_latin1));
    ia_acrescentar(&ia, "user", u ? u : "", NULL, NULL);
    libera(u);
    long gasto_pedido = 0;
    int entrada = 0, saida = 0, cache = 0;
    for (int passo = 0; passo < max_passos; passo++) {
        if (gasto_sessao >= limite_micro) {
            imprimir(VERMELHO "\nLimite de gasto da sessão atingido (" NORMAL);
            mostrar_dinheiro(limite_micro);
            imprimir(VERMELHO "). Use /limite <dólares> para aumentar.\n" NORMAL);
            break;
        }
        cortar_contexto();
        ia_resposta_t r;
        imprimir("\n");
        cor(NORMAL);
        int ok = ia_perguntar(&ia, &r);
        if (ok < 0) {
            imprimir(VERMELHO "\nErro: %s\n" NORMAL, ia.erro);
            registrar("saguicode: erro da API: %s", ia.erro);
            ia_resposta_liberar(&r);
            break;
        }
        gasto_pedido += r.custo_micro;
        gasto_sessao += r.custo_micro;
        entrada += r.tokens_entrada;
        saida += r.tokens_saida;
        cache += r.tokens_cache;
        tokens_sessao_entrada += r.tokens_entrada;
        tokens_sessao_saida += r.tokens_saida;
        if (r.n_chamadas == 0) {
            ia_acrescentar(&ia, "assistant", r.texto ? r.texto : "", NULL, NULL);
            registrar("saguicode: resposta final (%u letras, fim %s)", r.tam, r.fim);
            ia_resposta_liberar(&r);
            imprimir("\n");
            break;
        }
        json_escritor_t e;
        montar_chamadas(&r, &e);
        ia_acrescentar(&ia, "assistant", r.texto, e.d, NULL);
        je_liberar(&e);
        for (int i = 0; i < r.n_chamadas; i++) {
            ia_chamada_t *c = &r.chamadas[i];
            char resumo[100];
            resumo_args(c->argumentos ? c->argumentos : "", resumo, sizeof resumo);
            imprimir(AZUL "\n> %s" CINZA " %s\n" NORMAL, c->nome, resumo);
            registrar("saguicode: ferramenta %s", c->nome);
            texto_t res = { 0, 0, 0 };
            executar_ferramenta(c->nome, c->argumentos ? c->argumentos : "{}", &res);
            /* O resultado foi montado em Latin-1 (arquivos e textos do SaguiOS): vai em UTF-8. */
            char *u = para_utf8(res.d ? res.d : "", res.n);
            ia_acrescentar(&ia, "tool", u ? u : "", NULL, c->id);
            libera(u);
            libera(res.d);
        }
        ia_resposta_liberar(&r);
        if (passo == max_passos - 1) imprimir(AMARELO "\nLimite de %d passos neste pedido. Diga \"continue\" para seguir.\n" NORMAL, max_passos);
    }
    imprimir(CINZA "[entrada %d tokens (%d do cache), saída %d; este pedido ", entrada, cache, saida);
    mostrar_dinheiro(gasto_pedido);
    imprimir(", sessão ");
    mostrar_dinheiro(gasto_sessao);
    imprimir("]\n" NORMAL);
    registrar("saguicode: pedido terminado: entrada %d (cache %d), saida %d, custo %ld micro-dolares, sessao %ld",
              entrada, cache, saida, gasto_pedido, gasto_sessao);
}

/* ------------------------------------------------------------ conversas salvas */

static void salvar_conversa(const char *nome)
{
    char caminho[128];
    formatar_texto(caminho, sizeof caminho, "%s/conversas", pasta_config);
    criar_pasta(caminho);
    formatar_texto(caminho, sizeof caminho, "%s/conversas/%s.json", pasta_config, nome);
    json_escritor_t e;
    je_iniciar(&e);
    je_objeto(&e);
    je_chave(&e, "modelo"); je_texto(&e, ia.modelo);
    je_chave(&e, "mensagens");
    je_lista(&e);
    for (int i = 1; i < ia.qtd; i++) {                /* as instruções não: são refeitas ao abrir */
        ia_mensagem_t *m = &ia.msgs[i];
        je_objeto(&e);
        je_chave(&e, "role"); je_texto(&e, m->papel);
        je_chave(&e, "content"); if (m->conteudo) je_texto(&e, m->conteudo); else je_nulo(&e);
        if (m->chamadas) { je_chave(&e, "tool_calls"); je_bruto(&e, m->chamadas, (unsigned)tam_texto(m->chamadas)); }
        if (m->id_chamada[0]) { je_chave(&e, "tool_call_id"); je_texto(&e, m->id_chamada); }
        je_fim_objeto(&e);
    }
    je_fim_lista(&e);
    je_fim_objeto(&e);
    int r = e.d ? gravar_inteiro(caminho, e.d, e.n) : -1;
    je_liberar(&e);
    if (r < 0) imprimir(VERMELHO "Não consegui gravar %s.\n" NORMAL, caminho);
    else imprimir("Conversa salva em %s.\n", caminho);
}

static void abrir_conversa(const char *nome)
{
    char caminho[128];
    formatar_texto(caminho, sizeof caminho, "%s/conversas/%s.json", pasta_config, nome);
    texto_t t = { 0, 0, 0 };
    if (ler_inteiro(caminho, &t, 4 * 1024 * 1024) < 0 || !t.d) { imprimir(VERMELHO "Não achei %s.\n" NORMAL, caminho); return; }
    char erro[96];
    json_t *j = json_ler(t.d, t.n, erro, sizeof erro);
    libera(t.d);
    if (!j) { imprimir(VERMELHO "Conversa estragada: %s\n" NORMAL, erro); return; }
    iniciar_conversa();
    const char *m = json_texto_de(j, "modelo");
    if (*m) copia_texto(ia.modelo, m, sizeof ia.modelo);
    int n = 0;
    for (json_t *x = json_campo(j, "mensagens") ? json_campo(j, "mensagens")->filho : NULL; x; x = x->prox, n++) {
        json_t *ch = json_campo(x, "tool_calls");
        char *chamadas = NULL;
        if (ch) {
            json_escritor_t e;
            je_iniciar(&e);
            /* reescreve o pedaço: simples, refaz a lista */
            je_lista(&e);
            for (json_t *c = ch->filho; c; c = c->prox) {
                json_t *f = json_campo(c, "function");
                je_objeto(&e);
                je_chave(&e, "id"); je_texto(&e, json_texto_de(c, "id"));
                je_chave(&e, "type"); je_texto(&e, "function");
                je_chave(&e, "function"); je_objeto(&e);
                je_chave(&e, "name"); je_texto(&e, json_texto_de(f, "name"));
                je_chave(&e, "arguments"); je_texto(&e, json_texto_de(f, "arguments"));
                je_fim_objeto(&e);
                je_fim_objeto(&e);
            }
            je_fim_lista(&e);
            chamadas = e.d;
        }
        json_t *conteudo = json_campo(x, "content");
        ia_acrescentar(&ia, json_texto_de(x, "role"), conteudo && conteudo->tipo == JSON_TEXTO ? conteudo->texto : NULL,
                       chamadas, *json_texto_de(x, "tool_call_id") ? json_texto_de(x, "tool_call_id") : NULL);
        libera(chamadas);
    }
    json_liberar(j);
    imprimir("Conversa %s aberta (%d mensagens).\n", nome, n);
}

/* ------------------------------------------------------------ configuração */

static void ler_chave(void)
{
    char caminho[96];
    formatar_texto(caminho, sizeof caminho, "%s/chaves.txt", pasta_config);
    texto_t t = { 0, 0, 0 };
    if (ler_inteiro(caminho, &t, 8192) < 0 || !t.d) return;
    for (char *p = t.d; *p;) {
        char *fim = acha_char(p, '\n');
        if (fim) *fim = 0;
        while (*p == ' ') p++;
        if (comeca_com(p, "openrouter")) {
            char *v = acha_char(p, '=');
            if (v) {
                v++;
                while (*v == ' ') v++;
                int n = (int)tam_texto(v);
                while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\r')) v[--n] = 0;
                copia_texto(ia.chave, v, sizeof ia.chave);
            }
        }
        if (!fim) break;
        p = fim + 1;
    }
    memset(t.d, 0, t.n);
    libera(t.d);
}

static void ler_configuracoes(void)
{
    char v[200];
    if (config_ler("ia.modelo", v, sizeof v) > 0) copia_texto(ia.modelo, v, sizeof ia.modelo);
    else copia_texto(ia.modelo, "anthropic/claude-sonnet-4.5", sizeof ia.modelo);
    if (config_ler("ia.servidor", v, sizeof v) > 0) copia_texto(ia.servidor, v, sizeof ia.servidor);
    if (config_ler("ia.limite_centavos", v, sizeof v) > 0) limite_micro = (long)texto_para_int(v) * 10000;
    if (config_ler("ia.max_passos", v, sizeof v) > 0 && texto_para_int(v) > 0) max_passos = texto_para_int(v);
    if (config_ler("ia.max_contexto", v, sizeof v) > 0 && texto_para_int(v) > 1000) max_contexto = texto_para_int(v);
}

static void ajuda(void)
{
    imprimir("Comandos:\n"
             "  /modelo [nome]     mostra ou troca o modelo (ex.: /modelo anthropic/claude-sonnet-4.5)\n"
             "  /gasto             tokens e dinheiro gastos nesta sessão\n"
             "  /limite <dólares>  muda o limite de gasto da sessão (ex.: /limite 2)\n"
             "  /limpar            começa uma conversa nova\n"
             "  /salvar <nome>     guarda a conversa no pendrive\n"
             "  /abrir <nome>      abre uma conversa guardada\n"
             "  /sair              sai do Sagui Code\n"
             "Qualquer outra coisa é um pedido para a IA.\n");
}

int principal(int argc, char **argv)
{
    ia_iniciar(&ia);
    ia.ferramentas = para_utf8(FERRAMENTAS, sizeof FERRAMENTAS - 1);   /* as descrições estão em Latin-1 no programa */
    ia.ao_texto = ao_texto;
    info_arquivo_t inf;
    bool pendrive = info_arquivo("/usb/.saguios", &inf) >= 0;
    copia_texto(pasta_config, pendrive ? "/usb/.saguios" : "/tmp", sizeof pasta_config);
    copia_texto(pasta_trabalho, pendrive ? "/usb/projetos" : "/tmp/projetos", sizeof pasta_trabalho);
    criar_pasta(pasta_trabalho);
    mudar_pasta(pasta_trabalho);
    ler_configuracoes();
    ler_chave();
    imprimir(NEGRITO VERDE "Sagui Code" NORMAL " - agente de programação do SaguiOS. Modelo: %s. /ajuda para os comandos.\n", ia.modelo);
    imprimir(CINZA "Pasta de trabalho: %s%s\n" NORMAL, pasta_trabalho, pendrive ? "" : " (sem pendrive: some ao desligar)");
    if (!ia.chave[0]) {
        imprimir(AMARELO "Não achei a chave do OpenRouter em %s/chaves.txt (linha \"openrouter = sk-or-...\").\n" NORMAL, pasta_config);
        char chave[200];
        imprimir("Cole a chave aqui (não aparece na tela; fica só até sair): ");
        int n = 0;
        for (;;) {
            char c;
            if (ler(ENTRADA, &c, 1) <= 0) return 1;
            if (c == '\n' || c == '\r') break;
            if (c == 8 || c == 127) { if (n) n--; continue; }
            if ((unsigned char)c >= 32 && n < (int)sizeof chave - 1) chave[n++] = c;
        }
        chave[n] = 0;
        imprimir("\n");
        copia_texto(ia.chave, chave, sizeof ia.chave);
        memset(chave, 0, sizeof chave);
        if (!ia.chave[0]) return 1;
    }
    registrar("saguicode: iniciado, modelo %s, servidor %s", ia.modelo, ia.servidor);
    iniciar_conversa();
    /* Um pedido direto na linha de comando: saguicode "faça um relógio" */
    if (argc > 1) {
        char p[MAX_LINHA] = "";
        for (int i = 1; i < argc; i++) { if (i > 1) junta_texto(p, " ", sizeof p); junta_texto(p, argv[i], sizeof p); }
        imprimir(NEGRITO "> " NORMAL "%s\n", p);
        pedido(p);
    }
    char linha[MAX_LINHA];
    for (;;) {
        int n = ler_linha_teclado(NEGRITO VERDE "\n> " NORMAL, linha, sizeof linha);
        if (n < 0) break;
        if (n == 0) continue;
        if (linha[0] != '/') { pedido(linha); continue; }
        char *resto = acha_char(linha, ' ');
        if (resto) { *resto++ = 0; while (*resto == ' ') resto++; }
        if (compara_texto(linha, "/sair") == 0) break;
        else if (compara_texto(linha, "/ajuda") == 0) ajuda();
        else if (compara_texto(linha, "/limpar") == 0) { iniciar_conversa(); permitir_tudo = false; imprimir("Conversa nova.\n"); }
        else if (compara_texto(linha, "/modelo") == 0) {
            if (resto && *resto) { copia_texto(ia.modelo, resto, sizeof ia.modelo); config_gravar("ia.modelo", ia.modelo); }
            imprimir("Modelo: %s\n", ia.modelo);
        } else if (compara_texto(linha, "/gasto") == 0) {
            imprimir("Sessão: entrada %d tokens, saída %d tokens, ", tokens_sessao_entrada, tokens_sessao_saida);
            mostrar_dinheiro(gasto_sessao);
            imprimir(" de ");
            mostrar_dinheiro(limite_micro);
            imprimir(". Conversa atual: ~%d tokens (limite %d).\n", estimar_tokens(), max_contexto);
        } else if (compara_texto(linha, "/limite") == 0 && resto && *resto) {
            /* "2" ou "0,50" ou "0.50" dólares */
            long inteiro = 0, cent = 0;
            const char *p = resto;
            while (*p >= '0' && *p <= '9') inteiro = inteiro * 10 + (*p++ - '0');
            if (*p == ',' || *p == '.') { p++; if (*p >= '0' && *p <= '9') cent = (*p++ - '0') * 10; if (*p >= '0' && *p <= '9') cent += *p - '0'; }
            limite_micro = inteiro * 1000000 + cent * 10000;
            imprimir("Limite da sessão: ");
            mostrar_dinheiro(limite_micro);
            imprimir("\n");
        } else if (compara_texto(linha, "/salvar") == 0 && resto && *resto) salvar_conversa(resto);
        else if (compara_texto(linha, "/abrir") == 0 && resto && *resto) abrir_conversa(resto);
        else imprimir("Comando desconhecido. /ajuda mostra a lista.\n");
    }
    memset(ia.chave, 0, sizeof ia.chave);
    ia_liberar(&ia);
    imprimir("Até logo!\n");
    return 0;
}
