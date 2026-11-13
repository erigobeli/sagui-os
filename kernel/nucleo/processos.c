/* processos.c - tabela de processos e escalonador round-robin preemptivo.
 *
 * Cada processo tem a própria pilha do kernel. Quando ele é interrompido,
 * o código de vetores.asm salva os registradores nessa pilha (o "quadro") e
 * o despachante devolve o quadro a retomar (D007). Trocar de processo é,
 * portanto, só escolher outro quadro, trocar o diretório de páginas (CR3) e
 * avisar a TSS de qual pilha do kernel usar quando o próximo programa no
 * anel 3 for interrompido.
 *
 * Política: round-robin. A cada tique do relógio o processo atual gasta um
 * pouco da sua fatia (QUANTUM_TIQUES); quando acaba, o escalonador procura o
 * próximo processo pronto na tabela, a partir do atual, dando a volta. O
 * processo 0 ("ocioso") só roda quando ninguém mais está pronto. */
#include "processo.h"
#include "console.h"
#include "memoria.h"
#include "relogio.h"
#include "texto.h"
#include "vfs.h"
#include "gui.h"

void tss_definir_pilha(uint32_t esp0);


static processo_t tabela[MAX_PROCESSOS];
processo_t *atual;
static int proximo_pid = 1;
static int fatia;
quadro_t *ao_ceder(quadro_t *q);

const char *nome_estado(estado_t e)
{
    static const char *n[] = { "livre", "pronto", "executando", "bloqueado", "zumbi" };
    return e <= P_ZUMBI ? n[e] : "?";
}

static uint8_t fpu_limpa[512] __attribute__((aligned(16)));   /* ver "ponto flutuante" abaixo */
static void fpu_iniciar(void);

void processos_iniciar(void)
{
    /* O código que está rodando agora (o fim da inicialização do kernel) vira
     * o processo 0, o "ocioso". Ele usa a pilha do boot e o quadro dele é
     * preenchido na primeira interrupção do relógio. */
    processo_t *p = &tabela[0];
    memset(p, 0, sizeof *p);
    p->pid = 0;
    p->estado = P_EXECUTANDO;
    txt_copia_n(p->nome, "ocioso", sizeof p->nome);
    p->espaco = espaco_kernel();
    txt_copia_n(p->pasta, "/", sizeof p->pasta);
    atual = p;
    fatia = QUANTUM_TIQUES;
    idt_registrar(VETOR_CEDER, ao_ceder);
    fpu_iniciar();
    memcpy(p->fpu, fpu_limpa, sizeof p->fpu);
}

/* ------------------------------------------------------------ ponto flutuante (H80)
 *
 * Os programas podem usar float e double. O processador guarda os números
 * de ponto flutuante em registradores próprios (os 8 do x87 e os 8 XMM do
 * SSE); como todos os programas dividem o mesmo processador, a cada troca de
 * processo o kernel salva os registradores do que sai e restaura os do que
 * entra, com FXSAVE/FXRSTOR (512 bytes, manual da Intel, vol. 1 cap. 10). O
 * próprio kernel é compilado sem ponto flutuante (-mgeneral-regs-only), então
 * nunca estraga esses registradores no meio do caminho. Salvar sempre (e não
 * só quando o programa usou) é mais simples e custa pouco: ~512 bytes. */
static uint8_t fpu_limpa[512] __attribute__((aligned(16)));
static bool fpu_ok;

static void fpu_iniciar(void)
{
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (!(d & (1u << 24))) { klog("[FPU] processador sem FXSAVE: programas sem ponto flutuante\n"); return; }
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((1u << 2) | (1u << 3));          /* EM = 0 (há FPU), TS = 0 */
    cr0 |= (1u << 1) | (1u << 5);             /* MP = 1, NE = 1 (erros por exceção) */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
    if (d & (1u << 25)) {                     /* SSE: o sistema sabe salvar os XMM */
        uint32_t cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1u << 9) | (1u << 10);        /* OSFXSR, OSXMMEXCPT */
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
    }
    __asm__ volatile("fninit");
    __asm__ volatile("fxsave %0" : "=m"(fpu_limpa));
    fpu_ok = true;
    klog("[FPU] ponto flutuante ligado (x87%s, salvo por processo)\n", (d & (1u << 25)) ? " e SSE" : "");
}

/* Estado inicial de um programa novo: a FPU zerada. */
void processo_fpu_limpar(processo_t *p) { memcpy(p->fpu, fpu_limpa, sizeof p->fpu); }

processo_t *processo_indice(int i) { return (i >= 0 && i < MAX_PROCESSOS) ? &tabela[i] : NULL; }

processo_t *processo_por_pid(int pid)
{
    for (int i = 0; i < MAX_PROCESSOS; i++)
        if (tabela[i].estado != P_LIVRE && tabela[i].pid == pid)
            return &tabela[i];
    return NULL;
}

processo_t *processo_novo(const char *nome, int pai)
{
    for (int i = 1; i < MAX_PROCESSOS; i++) {
        processo_t *p = &tabela[i];
        if (p->estado != P_LIVRE) continue;
        uint32_t pilha = memfisica_alocar_seguidas(PILHA_KERNEL / PAGINA);
        if (!pilha) return NULL;
        memset(p, 0, sizeof *p);
        memcpy(p->fpu, fpu_limpa, sizeof p->fpu);
        p->pid = proximo_pid++;
        p->pai = pai;
        p->estado = P_BLOQUEADO;   /* só fica pronto quando o quadro inicial estiver montado */
        p->espera = p;
        txt_copia_n(p->nome, nome, sizeof p->nome);
        p->pilha_kernel = pilha;
        p->espaco = espaco_kernel();
        txt_copia_n(p->pasta, "/", sizeof p->pasta);
        return p;
    }
    return NULL;
}

/* Uma tarefa do kernel que termina cai aqui. Vira zumbi e espera o próximo
 * tique, que nunca mais a escolhe. */
static void fim_tarefa(void)
{
    int_desliga();
    processo_encerrar(atual, 0, NULL);
    int_liga();
    for (;;) cpu_espera();
}

processo_t *tarefa_kernel(const char *nome, void (*funcao)(void *), void *arg)
{
    processo_t *p = processo_novo(nome, 0);
    if (!p) return NULL;
    /* No anel 0 o iret só desempilha EIP, CS e EFLAGS. Os dois campos
     * seguintes do quadro (esp_usuario e ss_usuario) ficam no topo da pilha
     * quando a função começa: viram o endereço de retorno e o argumento. */
    quadro_t *q = (quadro_t *)(p->pilha_kernel + PILHA_KERNEL - sizeof(quadro_t));
    memset(q, 0, sizeof *q);
    q->ds = q->es = q->fs = q->gs = SEL_KDADOS;
    q->cs = SEL_KCODIGO;
    q->eip = (uint32_t)funcao;
    q->eflags = 0x202;                     /* IF=1: a tarefa pode ser interrompida */
    q->esp_usuario = (uint32_t)fim_tarefa; /* "endereço de retorno" da função */
    q->ss_usuario = (uint32_t)arg;         /* primeiro argumento */
    p->quadro = q;
    p->estado = P_PRONTO;
    p->espera = NULL;
    return p;
}

void processo_liberar(processo_t *p)
{
    if (p->pilha_kernel)
        memfisica_liberar_seguidas(p->pilha_kernel, PILHA_KERNEL / PAGINA);
    p->pilha_kernel = 0;
    p->estado = P_LIVRE;
}

/* Termina um processo (ele mesmo ou outro). A memória do programa e os
 * arquivos são liberados já; a pilha do kernel e a entrada na tabela ficam
 * até o pai recolher o código de saída com "esperar". */
void processo_encerrar(processo_t *p, int codigo, const char *motivo)
{
    if (p->estado == P_ZUMBI || p->estado == P_LIVRE || p->pid == 0) return;
    p->codigo_saida = codigo;
    if (motivo) txt_copia_n(p->motivo, motivo, sizeof p->motivo);
    arquivos_fechar_todos(p);
    janelas_do_processo_fechar(p);   /* antes de destruir o espaço: as páginas das janelas são do gerenciador */
    if (p->usuario && p->espaco != espaco_kernel()) {
        espaco_destruir(p->espaco);   /* troca o CR3 para o do kernel se for o atual */
        p->espaco = espaco_kernel();
    }
    p->estado = P_ZUMBI;
    p->espera = NULL;
    /* Os filhos ficam órfãos: passam para o processo 0, que os recolhe. */
    for (int i = 1; i < MAX_PROCESSOS; i++)
        if (tabela[i].estado != P_LIVRE && tabela[i].pai == p->pid)
            tabela[i].pai = 0;
    acordar(processo_por_pid(p->pai));   /* o pai pode estar em "esperar" */
    klog("[SAGUI] processo %u (%s) terminou com codigo %d%s%s\n", p->pid, p->nome, codigo,
         motivo ? ": " : "", motivo ? motivo : "");
}

/* Uma tarefa do kernel não passa por "int 0x80"; para dormir de verdade ela
 * bloqueia e cede a vez por esta interrupção de software, que vai direto ao
 * escalonador. Sem isso, uma tarefa que acorda a cada tique ficaria sempre
 * pronta e o processo 0 nunca mais rodaria. */
quadro_t *ao_ceder(quadro_t *q)
{
    return escalonar(q);
}

void ceder(void)
{
    __asm__ volatile("int %0" : : "i"(VETOR_CEDER) : "memory");
}

void bloquear(const void *canal)
{
    atual->estado = P_BLOQUEADO;
    atual->espera = canal;
}

void acordar(const void *canal)
{
    if (!canal) return;
    for (int i = 0; i < MAX_PROCESSOS; i++)
        if (tabela[i].estado == P_BLOQUEADO && tabela[i].espera == canal) {
            tabela[i].estado = P_PRONTO;
            tabela[i].espera = NULL;
        }
}

quadro_t *escalonar(quadro_t *q)
{
    atual->quadro = q;
    if (atual->estado == P_EXECUTANDO)
        atual->estado = P_PRONTO;

    /* Recolhe zumbis que ninguém vai esperar (órfãos e tarefas do kernel). */
    for (int i = 1; i < MAX_PROCESSOS; i++)
        if (tabela[i].estado == P_ZUMBI && tabela[i].pai == 0 && &tabela[i] != atual)
            processo_liberar(&tabela[i]);

    int ini = (int)(atual - tabela);
    processo_t *prox = NULL;
    for (int k = 1; k <= MAX_PROCESSOS; k++) {
        processo_t *c = &tabela[(ini + k) % MAX_PROCESSOS];
        if (c->estado == P_PRONTO && c->pid != 0) { prox = c; break; }
    }
    if (!prox)
        prox = &tabela[0];   /* ninguém pronto: o ocioso */
    prox->estado = P_EXECUTANDO;
    fatia = QUANTUM_TIQUES;
    if (prox->usuario)
        tss_definir_pilha(prox->pilha_kernel + PILHA_KERNEL);
    if (prox->espaco != espaco_atual())
        espaco_ativar(prox->espaco);
    if (fpu_ok && prox != atual) {
        __asm__ volatile("fxsave %0" : "=m"(atual->fpu));
        __asm__ volatile("fxrstor %0" :: "m"(prox->fpu));
    }
    atual = prox;
    return prox->quadro;
}

quadro_t *escalonador_tique(quadro_t *q)
{
    uint32_t agora = relogio_tiques();
    atual->tiques_cpu++;
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        processo_t *p = &tabela[i];
        if (p->estado == P_BLOQUEADO && p->acordar_em && (int32_t)(agora - p->acordar_em) >= 0) {
            p->acordar_em = 0;
            p->estado = P_PRONTO;
            p->espera = NULL;
        }
    }
    /* Troca quando a fatia acaba, ou na hora se o ocioso está rodando e
     * alguém ficou pronto. */
    if (atual->estado != P_EXECUTANDO || --fatia <= 0 || atual->pid == 0)
        return escalonar(q);   /* bloqueado (ex.: tarefa do kernel dormindo), fim da fatia, ou ocioso */
    return q;
}

/* Quadro inicial de um programa no anel 3: segmentos de usuário, a pilha
 * do programa e o ponto de entrada. O iret com troca de anel desempilha
 * também ESP e SS, e a CPU passa a executar o programa sem privilégios. */
void processo_preparar_usuario(processo_t *p, uint32_t entrada, uint32_t pilha)
{
    quadro_t *q = (quadro_t *)(p->pilha_kernel + PILHA_KERNEL - sizeof(quadro_t));
    memset(q, 0, sizeof *q);
    q->ds = q->es = q->fs = q->gs = SEL_UDADOS;
    q->cs = SEL_UCODIGO;
    q->ss_usuario = SEL_UDADOS;
    q->esp_usuario = pilha;
    q->eip = entrada;
    q->eflags = 0x202;   /* IF=1, IOPL=0: o programa não pode usar cli/sti nem portas */
    p->quadro = q;
    p->usuario = true;
    p->estado = P_PRONTO;
    p->espera = NULL;
}
