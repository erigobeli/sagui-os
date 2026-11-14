/* top (monitor) - os processos e a memória, atualizados a cada segundo,
 * dentro do Terminal (H83, vindo do pull request #7, do Gabriel).
 *
 *   top [-n vezes]
 *
 * Mostra o tempo ligado, a memória total, usada e livre, quanto do
 * processador está ocupado e, para cada processo: PID, pai, estado,
 * memória, %CPU e nome, com os que mais gastam processador em cima.
 * Sai com q (ou Esc); Ctrl+C também encerra (o Terminal cuida disso).
 *
 * A %CPU de cada processo é a parte do último intervalo que ele passou
 * rodando: o kernel conta, para cada processo, quantos milissegundos de
 * processador ele já usou desde que começou (cpu_ms). O top guarda essa
 * conta numa amostra, espera, tira outra amostra e divide a diferença pelo
 * tempo de relógio que passou entre as duas:
 *     %CPU = (cpu_ms agora - cpu_ms antes) / (ms passados) x 100
 * Dividir o total acumulado pelo tempo ligado daria a média da vida
 * inteira do processo, que não mostra quem está ocupado agora. O processo
 * 0, o "ocioso", roda quando ninguém mais quer o processador: o que sobra
 * dele é o quanto o processador está ocupado.
 *
 * As teclas chegam pelo pipe do Terminal; disponivel() diz se há alguma
 * sem esperar. Sem Terminal (a tela cheia do kernel, ou a entrada vindo de
 * um arquivo), não há como saber se apertaram q, então o top mostra uma
 * vez só (ou -n vezes) e sai. */
#include "sagui.h"

#define MAX_PROC   64
#define MAX_LINHAS 18          /* processos que cabem no Terminal (28 linhas) */
#define INTERVALO  1000        /* ms entre duas telas */

static info_processo_t agora[MAX_PROC], antes[MAX_PROC];
static int qtd_agora, qtd_antes;
static unsigned pct[MAX_PROC];   /* %CPU de cada processo de "agora", em décimos */
static int ordem[MAX_PROC];
static char tela[8192];

static const char *nome_estado(unsigned e)
{
    static const char *const nomes[] = { "livre", "pronto", "executando", "bloqueado", "zumbi" };
    return e <= 4 ? nomes[e] : "?";
}

/* Quanto processador cada processo usou desde a amostra anterior. */
static void calcular(unsigned passou_ms)
{
    if (passou_ms == 0) passou_ms = 1;
    for (int i = 0; i < qtd_agora; i++) {
        unsigned antes_ms = 0;
        /* Processo que não estava na amostra anterior nasceu no intervalo:
         * todo o tempo dele foi usado agora. Os PIDs não se repetem, então
         * o PID basta para achar o mesmo processo. */
        for (int k = 0; k < qtd_antes; k++)
            if (antes[k].pid == agora[i].pid) { antes_ms = antes[k].cpu_ms; break; }
        unsigned usado = agora[i].cpu_ms >= antes_ms ? agora[i].cpu_ms - antes_ms : 0;
        unsigned p = (usado * 1000 + passou_ms / 2) / passou_ms;
        pct[i] = p > 1000 ? 1000 : p;   /* o relógio conta em tiques de 10 ms: arredonda */
    }
}

/* Ordem de exibição: mais %CPU primeiro; empate, menor PID primeiro.
 * Ordenação por inserção: são poucos processos. */
static void ordenar(void)
{
    for (int i = 0; i < qtd_agora; i++) {
        int x = i, j = i - 1;
        while (j >= 0 && (pct[ordem[j]] < pct[x] || (pct[ordem[j]] == pct[x] && agora[ordem[j]].pid > agora[x].pid))) {
            ordem[j + 1] = ordem[j];
            j--;
        }
        ordem[j + 1] = x;
    }
}

static void por(const char *fmt, ...)
{
    size_t n = tam_texto(tela);
    va_list ap;
    va_start(ap, fmt);
    formatar_lista(tela + n, sizeof tela - n, fmt, ap);
    va_end(ap);
}

static void desenhar(void)
{
    info_versao_t v;
    info_memoria_t m;
    if (info_sistema(INFO_VERSAO, &v, sizeof v) < 0) v.segundos_ligado = 0;
    bool tem_m = info_sistema(INFO_MEMORIA, &m, sizeof m) == 0;
    unsigned s = v.segundos_ligado;
    unsigned ocioso = 0;
    for (int i = 0; i < qtd_agora; i++) if (agora[i].pid == 0) ocioso = pct[i];
    unsigned ocupado = 1000 - ocioso;

    /* Tudo vai para um texto só e sai de uma vez: escrever aos pedaços
     * faria a tela piscar. ESC [ H leva o cursor ao canto; ESC [ J apaga
     * dali para baixo. */
    tela[0] = 0;
    por("\x1b[H\x1b[J");
    por("\x1b[1mtop\x1b[0m - ligado há %u:%02u:%02u, %d processos\n", s / 3600, s / 60 % 60, s % 60, qtd_agora);
    if (tem_m)
        por("Memória: %u KB no total, %u KB usados, %u KB livres\n", m.gerida_kb, m.usada_kb, m.livre_kb);
    por("CPU: %u,%u%% ocupada\n\n", ocupado / 10, ocupado % 10);
    por("\x1b[7m  PID  PAI  ESTADO       MEMÓRIA   %%CPU  NOME                \x1b[0m\n");
    int mostrar = qtd_agora < MAX_LINHAS ? qtd_agora : MAX_LINHAS;
    for (int k = 0; k < mostrar; k++) {
        const info_processo_t *p = &agora[ordem[k]];
        unsigned c = pct[ordem[k]];
        por("%5d %4d  %-10s %6u KB  %3u,%u  %s%s\n", p->pid, p->pai, nome_estado(p->estado), p->memoria_kb,
            c / 10, c % 10, p->nome, p->usuario ? "" : " (kernel)");
    }
    if (qtd_agora > mostrar) por("  ... e mais %d\n", qtd_agora - mostrar);
    por("\nq: sair\n");
    escrever_tudo(SAIDA, tela, (unsigned)tam_texto(tela));
}

static int amostrar(info_processo_t *v)
{
    int n = info_sistema(INFO_PROCESSOS, v, MAX_PROC);
    return n < 0 ? 0 : (n > MAX_PROC ? MAX_PROC : n);
}

/* Espera até "ms" milissegundos olhando o teclado. true = pediram para sair. */
static bool esperar_tecla(unsigned ms, bool teclado)
{
    unsigned inicio = tempo_ms();
    while (tempo_ms() - inicio < ms) {
        if (teclado && disponivel(ENTRADA) > 0) {
            char b[16];
            int n = ler(ENTRADA, b, sizeof b);
            for (int i = 0; i < n; i++) {
                if (b[i] == 'q' || b[i] == 'Q') return true;
                if (b[i] == 27 && i + 1 < n && b[i + 1] == 27) return true;   /* Esc chega como ESC ESC */
            }
        }
        dormir(50);
    }
    return false;
}

int principal(int argc, char **argv)
{
    int vezes = 0;   /* 0 = até apertarem q */
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-n") == 0 && i + 1 < argc) vezes = texto_para_int(argv[++i]);
        else { imprimir_em(ERROS, "uso: top [-n vezes]\n"); return 1; }
    }
    /* Teclado só existe se a entrada é o pipe do Terminal. */
    bool teclado = disponivel(ENTRADA) >= 0;
    if (!teclado && vezes <= 0) vezes = 1;

    qtd_antes = amostrar(antes);
    unsigned t_antes = tempo_ms();
    /* A primeira tela sai depressa (meio segundo de amostra); as outras,
     * a cada segundo. */
    unsigned espera = 500;
    for (int feitas = 0; vezes <= 0 || feitas < vezes; feitas++) {
        if (esperar_tecla(espera, teclado)) break;
        espera = INTERVALO;
        qtd_agora = amostrar(agora);
        unsigned t = tempo_ms();
        calcular(t - t_antes);
        ordenar();
        desenhar();
        memcpy(antes, agora, sizeof agora);
        qtd_antes = qtd_agora;
        t_antes = t;
    }
    return 0;
}
