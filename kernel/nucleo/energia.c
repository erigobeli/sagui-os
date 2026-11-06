/* energia.c - reiniciar e desligar a máquina. */
#include "cpu.h"
#include "console.h"
#include "portas.h"
#include "relogio.h"
#include "disco.h"
#include "som.h"
#include "video.h"
#include "desenho.h"
#include "texto.h"

SEM_RETORNO void maquina_reiniciar(void)
{
    klog("[SAGUI] reiniciando\n");
    int_desliga();
    som_parar();

    /* Se houver pendrive montado, desmonta antes de reiniciar */
    disco_t *d = disco_montado();
    if (d) disco_desmontar(d);

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

static bool acpi_soma_ok(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint8_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += b[i];
    return s == 0;
}

static void tentar_desligar_acpi(void)
{
    /* Procura a tabela RSDP na memória BIOS (0xE0000 a 0xFFFFF), alinhada em 16 bytes. */
    const char *bios = (const char *)0xE0000;
    const char *rsdp = NULL;
    for (uint32_t off = 0; off < 0x20000; off += 16) {
        if (memcmp(bios + off, "RSD PTR ", 8) == 0) {
            if (acpi_soma_ok(bios + off, 20)) {
                rsdp = bios + off;
                break;
            }
        }
    }
    if (!rsdp) return;

    uint32_t rsdt_end = *(const uint32_t *)(rsdp + 16);
    if (!rsdt_end || rsdt_end >= 0x38000000u) return; /* fora da área mapeada */

    const char *rsdt = (const char *)rsdt_end;
    if (memcmp(rsdt, "RSDT", 4) != 0) return;
    uint32_t rsdt_tam = *(const uint32_t *)(rsdt + 4);
    if (rsdt_tam < 36 || !acpi_soma_ok(rsdt, rsdt_tam)) return;

    int n_tabs = (int)(rsdt_tam - 36) / 4;
    const uint32_t *tabelas = (const uint32_t *)(rsdt + 36);

    for (int i = 0; i < n_tabs; i++) {
        uint32_t tab_end = tabelas[i];
        if (!tab_end || tab_end >= 0x38000000u) continue;
        const char *tab = (const char *)tab_end;
        if (memcmp(tab, "FACP", 4) == 0) {
            /* FADT encontrada */
            uint32_t pm1a = *(const uint32_t *)(tab + 64);
            uint32_t pm1b = *(const uint32_t *)(tab + 68);
            uint32_t dsdt_end = *(const uint32_t *)(tab + 40);

            uint16_t slp_a = 0xFFFF, slp_b = 0xFFFF;

            if (dsdt_end && dsdt_end < 0x38000000u) {
                const char *dsdt = (const char *)dsdt_end;
                if (memcmp(dsdt, "DSDT", 4) == 0) {
                    uint32_t dsdt_tam = *(const uint32_t *)(dsdt + 4);
                    for (uint32_t k = 0; k + 4 < dsdt_tam; k++) {
                        if (dsdt[k] == '_' && dsdt[k+1] == 'S' && dsdt[k+2] == '5' && dsdt[k+3] == '_') {
                            const uint8_t *b = (const uint8_t *)(dsdt + k + 4);
                            int idx = 0;
                            if (b[idx] == 0x08) idx++; /* NameOp opcional */
                            if (b[idx] == 0x12) {      /* PackageOp */
                                idx++;
                                uint8_t lead = b[idx++];
                                idx += (lead >> 6);
                                idx++; /* pula contagem de elementos */
                                if (b[idx] == 0x0A) { idx++; slp_a = b[idx++]; }
                                else if (b[idx] <= 0x07) { slp_a = b[idx++]; }
                                if (b[idx] == 0x0A) { idx++; slp_b = b[idx++]; }
                                else if (b[idx] <= 0x07) { slp_b = b[idx++]; }
                            }
                            break;
                        }
                    }
                }
            }

            if (slp_a != 0xFFFF && pm1a) {
                porta_escreve16((uint16_t)pm1a, (uint16_t)((slp_a << 10) | 0x2000));
                if (pm1b && slp_b != 0xFFFF)
                    porta_escreve16((uint16_t)pm1b, (uint16_t)((slp_b << 10) | 0x2000));
                for (volatile int w = 0; w < 1000000; w++) ;
            }

            /* Se não obteve os tipos do DSDT, tenta valores padrão de S5 */
            if (pm1a) {
                porta_escreve16((uint16_t)pm1a, 0x2000);              /* QEMU: SLP_TYP=0 */
                porta_escreve16((uint16_t)pm1a, 0x2000 | (5 << 10)); /* VirtualBox: SLP_TYP=5 */
                porta_escreve16((uint16_t)pm1a, 0x2000 | (7 << 10)); /* Intel Sandy Bridge: SLP_TYP=7 */
            }
            break;
        }
    }
}

SEM_RETORNO void maquina_desligar(void)
{
    klog("[SAGUI] desligando o computador\n");
    int_desliga();
    som_parar();

    /* Desmonta qualquer disco para garantir persistência dos dados no FAT32 */
    disco_t *d = disco_montado();
    if (d) disco_desmontar(d);

    /* 1. Tenta ACPI pelas tabelas da placa-mãe */
    tentar_desligar_acpi();

    /* 2. Portas de desligamento conhecidas de emuladores e hipervisores */
    porta_escreve16(0x604, 0x2000);   /* QEMU padrão ACPI */
    porta_escreve16(0xB004, 0x2000);  /* Bochs / QEMU legado */
    porta_escreve16(0x4004, 0x3400);  /* VirtualBox PIIX4 S5 */
    porta_escreve16(0x4004, 0x2000);  /* VirtualBox fallback */

    for (volatile int w = 0; w < 2000000; w++) ;

    /* 3. Se a placa não desligou automaticamente, encerra graciosamente na tela */
    if (video_ativo()) {
        superficie_t s;
        sup_iniciar(&s, video_buffer(), (int)video_largura(), (int)video_altura());
        int vl = (int)video_largura(), va = (int)video_altura();
        int box_l = 480, box_a = 120;
        int bx = (vl - box_l) / 2, by = (va - box_a) / 2;
        sup_retangulo(&s, bx, by, box_l, box_a, 0x18202A);
        sup_moldura(&s, bx, by, box_l, box_a, 0x0B7A3E);
        sup_retangulo(&s, bx, by, box_l, 24, 0x0B7A3E);
        sup_texto(&s, bx + 16, by + 4, "SaguiOS - Sistema Finalizado", 0xFFFFFF);
        const char *m1 = "O sistema foi desligado com segurança.";
        const char *m2 = "Você já pode desligar a alimentação do computador.";
        sup_texto(&s, bx + (box_l - texto_largura(m1)) / 2, by + 45, m1, 0xFFDF4A);
        sup_texto(&s, bx + (box_l - texto_largura(m2)) / 2, by + 75, m2, 0xFFFFFF);
        video_mostrar_tudo();
    } else {
        kprintf("\n[SAGUI] Sistema finalizado. Voce ja pode desligar o computador.\n");
    }

    parar_maquina();
}
