/* energia.c - desligar e reiniciar a máquina (H36).
 *
 * Desligar de verdade é coisa do ACPI, a interface que o firmware do PC
 * oferece para energia. O firmware deixa na memória baixa uma "ponta" (RSDP,
 * marcada com o texto "RSD PTR ") que leva a uma lista de tabelas. Duas
 * importam aqui:
 *   - FACP: diz em que portas fica o registrador de controle de energia
 *     (PM1a_CNT, às vezes também PM1b_CNT), como ligar o modo ACPI (porta e
 *     valor) e, nas versões mais novas, um registrador próprio de reset;
 *   - DSDT: código numa linguagem do firmware (AML). Não precisamos
 *     interpretar a linguagem: só achar o objeto chamado "_S5_", um pacote
 *     cujos dois primeiros números são o "tipo de sono" do estado S5
 *     (desligado) para PM1a e PM1b.
 * Desligar = escrever (tipo << 10) | bit 13 (SLP_EN) no PM1a_CNT (e no b).
 *
 * Reiniciar tenta, em ordem: o registrador de reset do ACPI, a porta 0xCF9
 * (reset dos chipsets Intel), o pulso pelo controlador de teclado e, por
 * último, uma falta tripla de propósito.
 *
 * Antes das duas coisas, os pendrives montados são desmontados, para tudo o
 * que estava pendente ser gravado. */
#include "desenho.h"
#include "video.h"
#include "cpu.h"
#include "console.h"
#include "portas.h"
#include "relogio.h"
#include "paginacao.h"
#include "disco.h"
#include "texto.h"
#include "sistema.h"

static struct {
    bool     ok;
    uint16_t pm1a, pm1b;          /* portas de controle */
    uint16_t slp_a, slp_b;        /* tipos de sono do S5 */
    uint32_t smi_cmd;
    uint8_t  ativar;              /* valor para ligar o modo ACPI */
    bool     tem_reset;
    uint8_t  reset_espaco;        /* 0 memória, 1 porta, 2 PCI */
    uint32_t reset_end;
    uint8_t  reset_valor;
} acpi;

static bool soma_ok(const uint8_t *p, uint32_t n)
{
    uint8_t s = 0;
    for (uint32_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]);
    return s == 0;
}

/* Procura "RSD PTR " alinhado em 16 bytes num pedaço da memória baixa. */
static const uint8_t *procurar_rsdp(uint32_t ini, uint32_t fim)
{
    for (uint32_t a = ini; a + 20 <= fim; a += 16) {
        const uint8_t *p = (const uint8_t *)a;
        if (memcmp(p, "RSD PTR ", 8) == 0 && soma_ok(p, 20)) return p;
    }
    return NULL;
}

/* Mapeia uma tabela ACPI inteira (o cabeçalho diz o tamanho). */
static const uint8_t *mapear_tabela(uint32_t fis)
{
    if (!fis) return NULL;
    const uint8_t *cab = fis < 0x38000000u ? (const uint8_t *)fis : (const uint8_t *)janela_mapear(fis, 36);
    if (!cab) return NULL;
    uint32_t tam = *(const uint32_t *)(cab + 4);
    if (tam < 36 || tam > 1024 * 1024) return NULL;
    if (fis < 0x38000000u) return cab;
    return (const uint8_t *)janela_mapear(fis, tam);
}

/* Lê um valor pequeno do AML: 0 (ZeroOp), 1 (OneOp) ou 0x0A seguido do byte. */
static int aml_numero(const uint8_t **p)
{
    uint8_t op = *(*p)++;
    if (op == 0x0A) return *(*p)++;
    if (op == 0x00 || op == 0x01) return op;
    if (op == 0xFF) return 0xFF;
    return op;   /* alguns firmwares escrevem o número direto */
}

static void ler_s5(const uint8_t *dsdt)
{
    uint32_t tam = *(const uint32_t *)(dsdt + 4);
    for (uint32_t i = 36; i + 8 < tam; i++) {
        if (memcmp(dsdt + i, "_S5_", 4) != 0) continue;
        /* Precisa ser a definição de um nome: NameOp (0x08) antes, talvez com '\'. */
        if (!(dsdt[i - 1] == 0x08 || (dsdt[i - 2] == 0x08 && dsdt[i - 1] == '\\'))) continue;
        const uint8_t *p = dsdt + i + 4;
        if (*p++ != 0x12) continue;                 /* PackageOp */
        p += 1 + (*p >> 6);                         /* tamanho do pacote: 1 a 4 bytes */
        p++;                                        /* número de elementos */
        acpi.slp_a = (uint16_t)aml_numero(&p);
        acpi.slp_b = (uint16_t)aml_numero(&p);
        acpi.ok = true;
        return;
    }
}

void energia_iniciar(void)
{
    const uint8_t *rsdp = NULL;
    /* O segmento da EBDA fica na área de dados da BIOS, no endereço 0x40E.
     * A página 0 fica desmapeada de propósito (pega ponteiro nulo), então a
     * leitura passa por uma janela do kernel. */
    const uint16_t *bda = (const uint16_t *)janela_mapear(0x40E, 2);
    uint32_t ebda = bda ? (uint32_t)(*bda) << 4 : 0;
    if (ebda >= 0x80000 && ebda < 0xA0000) rsdp = procurar_rsdp(ebda, ebda + 1024);
    if (!rsdp) rsdp = procurar_rsdp(0xE0000, 0x100000);
    if (!rsdp) { klog("[ACPI] sem RSDP: desligar só manual\n"); return; }
    const uint8_t *rsdt = mapear_tabela(*(const uint32_t *)(rsdp + 16));
    if (!rsdt || memcmp(rsdt, "RSDT", 4) != 0) { klog("[ACPI] RSDT invalida\n"); return; }
    uint32_t n = (*(const uint32_t *)(rsdt + 4) - 36) / 4;
    const uint8_t *facp = NULL;
    for (uint32_t i = 0; i < n && !facp; i++) {
        const uint8_t *t = mapear_tabela(*(const uint32_t *)(rsdt + 36 + i * 4));
        if (t && memcmp(t, "FACP", 4) == 0) facp = t;
    }
    if (!facp) { klog("[ACPI] sem FACP\n"); return; }
    uint32_t tam = *(const uint32_t *)(facp + 4);
    acpi.smi_cmd = *(const uint32_t *)(facp + 48);
    acpi.ativar = facp[52];
    acpi.pm1a = (uint16_t)*(const uint32_t *)(facp + 64);
    acpi.pm1b = (uint16_t)*(const uint32_t *)(facp + 68);
    if (tam >= 129 && (*(const uint32_t *)(facp + 112) & (1u << 10))) {   /* RESET_REG_SUP */
        acpi.tem_reset = true;
        acpi.reset_espaco = facp[116];
        acpi.reset_end = *(const uint32_t *)(facp + 120);
        acpi.reset_valor = facp[128];
    }
    const uint8_t *dsdt = mapear_tabela(*(const uint32_t *)(facp + 40));
    if (dsdt && memcmp(dsdt, "DSDT", 4) == 0) ler_s5(dsdt);
    klog("[ACPI] PM1a %x PM1b %x, S5 %s (%u/%u), reset %s\n", acpi.pm1a, acpi.pm1b, acpi.ok ? "ok" : "NAO achado",
         acpi.slp_a, acpi.slp_b, acpi.tem_reset ? "pelo ACPI" : "pela porta 0xCF9");
}

/* Grava o que estiver pendente nos pendrives. */
static void guardar_discos(void)
{
    for (int i = 0; i < MAX_DISCOS; i++) {
        disco_t *d = disco_indice(i);
        if (d && d->montado) disco_desmontar(d);
    }
}

/* Tela "pode desligar" (H83, ideia do pull request #4 de Clayton): quando o
 * ACPI não desliga a placa (computadores antigos, alguns emuladores), a
 * pessoa vê na tela inteira que já pode tirar da tomada, em vez da área de
 * trabalho parada. */
static void tela_pode_desligar(void)
{
    if (!video_ativo()) return;
    int vl = (int)video_largura(), va = (int)video_altura();
    superficie_t s;
    sup_iniciar(&s, video_buffer(), vl, va);
    sup_degrade(&s, 0, 0, vl, va, 0x0E1A14, 0x050807);
    video_cursor(0, 0, false);
    int esc = 4, mx = (vl - 32 * esc) / 2, my = va / 2 - 32 * esc - 20;
    sup_desenho(&s, mx, my, &arte_sagui, esc);
    const char *m1 = "O SaguiOS terminou com segurança.";
    const char *m2 = "Já pode desligar o computador.";
    sup_texto_forte(&s, (vl - texto_largura(m1)) / 2, va / 2 + 10, m1, 0xFFDF4A);
    sup_texto(&s, (vl - texto_largura(m2)) / 2, va / 2 + 40, m2, 0xFFFFFF);
    sup_retangulo(&s, vl / 2 - 60, va / 2 + 70, 40, 3, 0x0B7A3E);
    sup_retangulo(&s, vl / 2 - 20, va / 2 + 70, 40, 3, 0xE8BE30);
    sup_retangulo(&s, vl / 2 + 20, va / 2 + 70, 40, 3, 0x2A6FD8);
    video_mostrar_tudo();
    klog("[SAGUI] tela: pode desligar\n");
}

SEM_RETORNO void maquina_desligar(void)
{
    guardar_discos();
    klog("[SAGUI] desligando\n");
    if (acpi.ok && acpi.pm1a) {
        /* Liga o modo ACPI, se o firmware ainda não ligou (bit SCI_EN). */
        if (!(porta_le16(acpi.pm1a) & 1) && acpi.smi_cmd && acpi.ativar) {
            porta_escreve8((uint16_t)acpi.smi_cmd, acpi.ativar);
            for (int i = 0; i < 300 && !(porta_le16(acpi.pm1a) & 1); i++) relogio_dormir_ms(10);
        }
        int_desliga();
        porta_escreve16(acpi.pm1a, (uint16_t)((acpi.slp_a << 10) | (1u << 13)));
        if (acpi.pm1b) porta_escreve16(acpi.pm1b, (uint16_t)((acpi.slp_b << 10) | (1u << 13)));
        for (volatile int i = 0; i < 10000000; i++) ;
    }
    /* Não desligou: avisa e para (o pendrive já foi gravado). */
    tela_pode_desligar();
    kprintf("\nO SaguiOS terminou. Pode desligar o computador.\n");
    klog("[SAGUI] desligar pelo ACPI nao funcionou\n");
    parar_maquina();
}

SEM_RETORNO void maquina_reiniciar(void)
{
    guardar_discos();
    klog("[SAGUI] reiniciando\n");
    int_desliga();
    if (acpi.tem_reset && acpi.reset_espaco == 1) {
        porta_escreve8((uint16_t)acpi.reset_end, acpi.reset_valor);
        for (volatile int i = 0; i < 1000000; i++) ;
    }
    /* Chipsets Intel: porta 0xCF9, 0x02 prepara e 0x06 dispara o reset completo. */
    porta_escreve8(0xCF9, 0x02);
    for (volatile int i = 0; i < 1000; i++) ;
    porta_escreve8(0xCF9, 0x06);
    for (volatile int i = 0; i < 1000000; i++) ;
    /* O controlador de teclado tem uma linha ligada ao reset da CPU: o
     * comando 0xFE dá um pulso nela. */
    for (int i = 0; i < 100000 && (porta_le8(0x64) & 2); i++) ;
    porta_escreve8(0x64, 0xFE);
    for (volatile int i = 0; i < 1000000; i++) ;
    /* Se não funcionou: IDT vazia + interrupção = falta tripla, que também reinicia. */
    struct EMPACOTADO { uint16_t limite; uint32_t base; } vazia = { 0, 0 };
    __asm__ volatile("lidt %0; int3" : : "m"(vazia));
    parar_maquina();
}

int32_t sis_desligar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    maquina_desligar();
}
