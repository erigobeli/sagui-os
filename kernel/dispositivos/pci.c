/* pci.c - o barramento PCI (H29).
 *
 * Cada função de cada dispositivo tem 256 bytes de configuração: quem fez,
 * que tipo de coisa é (classe, subclasse, interface) e onde ficam os seus
 * registradores (BARs). O acesso é pelo "mecanismo 1" da especificação PCI:
 * escreve-se o endereço (barramento, dispositivo, função, registrador) na
 * porta 0xCF8 e lê-se o valor na 0xCFC. */
#include "pci.h"
#include "hda.h"
#include "portas.h"
#include "console.h"

static uint32_t endereco(uint8_t b, uint8_t d, uint8_t f, uint8_t reg)
{
    return 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)d << 11) | ((uint32_t)f << 8) | (reg & 0xFC);
}

static uint32_t ler(uint8_t b, uint8_t d, uint8_t f, uint8_t reg)
{
    porta_escreve32(0xCF8, endereco(b, d, f, reg));
    return porta_le32(0xCFC);
}

uint32_t pci_le32(const pci_disp_t *d, uint8_t reg)
{
    return ler(d->barramento, d->dispositivo, d->funcao, reg);
}

void pci_escreve32(const pci_disp_t *d, uint8_t reg, uint32_t v)
{
    porta_escreve32(0xCF8, endereco(d->barramento, d->dispositivo, d->funcao, reg));
    porta_escreve32(0xCFC, v);
}

/* Percorre todo o barramento e devolve cada função encontrada (H35). */
int pci_listar(pci_disp_t *achados, int max)
{
    int n = 0;
    for (int b = 0; b < 256; b++)
        for (int d = 0; d < 32; d++) {
            uint32_t id = ler((uint8_t)b, (uint8_t)d, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;                    /* ninguém aqui */
            bool multifuncao = (ler((uint8_t)b, (uint8_t)d, 0, 0x0C) >> 16) & 0x80;
            for (int f = 0; f < (multifuncao ? 8 : 1); f++) {
                uint32_t idf = ler((uint8_t)b, (uint8_t)d, (uint8_t)f, 0);
                if ((idf & 0xFFFF) == 0xFFFF) continue;
                uint32_t cl = ler((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x08);
                if (n < max)
                    achados[n] = (pci_disp_t){ (uint8_t)b, (uint8_t)d, (uint8_t)f, (uint16_t)idf, (uint16_t)(idf >> 16),
                                               (uint8_t)(cl >> 24), (uint8_t)(cl >> 16), (uint8_t)(cl >> 8) };
                n++;
            }
        }
    return n < max ? n : max;
}

int pci_procurar(uint8_t classe, uint8_t subclasse, uint8_t interface, pci_disp_t *achados, int max)
{
    static pci_disp_t todos[PCI_MAX];
    int t = pci_listar(todos, PCI_MAX), n = 0;
    for (int i = 0; i < t && n < max; i++)
        if (todos[i].classe == classe && todos[i].subclasse == subclasse && todos[i].interface == interface)
            achados[n++] = todos[i];
    return n;
}

/* ------------------------------------------------------------ relatório (comando pci) */

/* Nomes dos fabricantes mais comuns em PCs. Os números são atribuídos pelo
 * PCI-SIG a cada empresa; os nomes ajudam a ler o relatório. */
static const char *nome_fabricante(uint16_t f)
{
    switch (f) {
    case 0x8086: return "Intel";       case 0x10EC: return "Realtek";
    case 0x1969: return "Atheros";     case 0x168C: return "Atheros";
    case 0x14E4: return "Broadcom";    case 0x1022: return "AMD";
    case 0x1002: return "AMD/ATI";     case 0x10DE: return "NVIDIA";
    case 0x14F1: return "Conexant";    case 0x1106: return "VIA";
    case 0x1039: return "SiS";         case 0x197B: return "JMicron";
    case 0x1180: return "Ricoh";       case 0x1814: return "Ralink";
    case 0x1B21: return "ASMedia";     case 0x104C: return "Texas Instruments";
    case 0x11AB: return "Marvell";     case 0x1033: return "NEC";
    case 0x80EE: return "VirtualBox";  case 0x1234: return "QEMU";
    case 0x1AF4: return "virtio";      case 0x15AD: return "VMware";
    case 0x1B36: return "QEMU";        case 0x10B7: return "3Com";
    default: return "?";
    }
}

/* Alguns modelos conhecidos: os do G470 e os das máquinas virtuais. */
static const char *nome_modelo(uint16_t f, uint16_t m)
{
    if (f == 0x8086)
        switch (m) {
        case 0x100E: return "PRO/1000 MT (82540EM)";   case 0x100F: return "PRO/1000 MT (82545EM)";
        case 0x10D3: return "82574L";                  case 0x1503: return "82579V";
        case 0x2668: return "HD Audio (ICH6)";         case 0x293E: return "HD Audio (ICH9)";
        case 0x1C20: return "HD Audio (série 6)";      case 0x1C26: case 0x1C2D: return "USB 2.0 EHCI (série 6)";
        case 0x24CD: return "USB 2.0 EHCI (ICH4)";     case 0x293A: return "USB 2.0 EHCI (ICH9)";
        case 0x1C03: return "SATA AHCI (série 6)";     case 0x1C49: return "ponte LPC HM65";
        case 0x1C22: return "SMBus (série 6)";         case 0x1C3A: return "Management Engine";
        case 0x0104: return "Sandy Bridge (controlador de memória)";
        case 0x0106: case 0x0116: case 0x0126: return "HD Graphics (Sandy Bridge)";
        case 0x1237: return "ponte 440FX";             case 0x7000: return "ponte ISA PIIX3";
        case 0x7010: return "IDE PIIX3";               case 0x7113: return "ACPI PIIX4";
        case 0x29C0: return "ponte Q35";               case 0x2918: return "ponte LPC ICH9";
        case 0x2922: return "SATA AHCI (ICH9)";        case 0x2930: return "SMBus (ICH9)";
        }
    if (f == 0x1969)
        switch (m) {
        case 0x1062: return "AR8132 Fast Ethernet";    case 0x1063: return "AR8131 Gigabit";
        case 0x1073: return "AR8151 Gigabit";          case 0x1083: return "AR8151 v2 Gigabit";
        case 0x2060: return "AR8152 Fast Ethernet";    case 0x2062: return "AR8152 v2 Fast Ethernet";
        }
    if (f == 0x10EC)
        switch (m) {
        case 0x8139: return "RTL8139";   case 0x8168: return "RTL8111/8168";   case 0x8136: return "RTL810x";
        }
    if (f == 0x1234 && m == 0x1111) return "vídeo padrão";
    if (f == 0x80EE && m == 0xBEEF) return "vídeo";
    if (f == 0x80EE && m == 0xCAFE) return "dispositivo do convidado";
    if (f == 0x1022 && m == 0x2000) return "PCnet-FAST III";
    return "";
}

static const char *nome_classe(const pci_disp_t *d)
{
    uint16_t cs = (uint16_t)((d->classe << 8) | d->subclasse);
    switch (cs) {
    case 0x0101: return "Disco IDE";            case 0x0106: return "Disco SATA";
    case 0x0108: return "Disco NVMe";           case 0x0200: return "Rede com cabo (Ethernet)";
    case 0x0280: return "Rede (outra, em geral sem fio)";
    case 0x0300: return "Vídeo";                case 0x0380: return "Vídeo (outro)";
    case 0x0401: return "Som";                  case 0x0403: return "Som (HD Audio)";
    case 0x0600: return "Ponte do processador"; case 0x0601: return "Ponte ISA/LPC";
    case 0x0604: return "Ponte PCI";            case 0x0680: return "Ponte (outra)";
    case 0x0780: return "Comunicação";          case 0x0880: return "Periférico do sistema";
    case 0x0C05: return "SMBus";                case 0x0D80: return "Rede sem fio";
    case 0x0C03:
        switch (d->interface) {
        case 0x00: return "USB 1.1 (UHCI)";   case 0x10: return "USB 1.1 (OHCI)";
        case 0x20: return "USB 2.0 (EHCI)";   case 0x30: return "USB 3.0 (xHCI)";
        default:   return "USB";
        }
    }
    switch (d->classe) {
    case 0x01: return "Armazenamento"; case 0x02: return "Rede";  case 0x03: return "Vídeo";
    case 0x04: return "Multimídia";    case 0x06: return "Ponte"; case 0x0C: return "Barramento";
    default:   return "Outro";
    }
}

/* Quem cuida desse dispositivo no SaguiOS (o relatório diz o que falta). */
static const char *suporte(const pci_disp_t *d)
{
    if (d->classe == 0x0C && d->subclasse == 0x03 && d->interface == 0x20) return "driver EHCI do SaguiOS";
    if (d->classe == 0x03) return "vídeo pela BIOS (VESA)";
    if (d->classe == 0x04 && d->subclasse == 0x03) return hda_pronto() ? "driver HD Audio do SaguiOS" : "HD Audio sem codec com saída";
    if (d->classe == 0x02 && d->subclasse == 0x00 && d->fabricante == 0x8086) return "driver Intel PRO/1000 do SaguiOS (se for 8254x)";
    if (d->classe == 0x02 || d->classe == 0x04 || d->classe == 0x0D) return "ainda sem driver";
    return "-";
}

int pci_relatorio(char *s, int max)
{
    static pci_disp_t todos[PCI_MAX];
    int n = pci_listar(todos, PCI_MAX), k = 0;
    k += snformatar(s + k, (size_t)(max - k), "Dispositivos PCI: %d\n", n);
    for (int i = 0; i < n && k < max - 1; i++) {
        const pci_disp_t *d = &todos[i];
        const char *modelo = nome_modelo(d->fabricante, d->modelo);
        k += snformatar(s + k, (size_t)(max - k), "%02x:%02x.%x  %04x:%04x  %s\n           %s %s  [%s]\n",
                        d->barramento, d->dispositivo, d->funcao, d->fabricante, d->modelo, nome_classe(d),
                        nome_fabricante(d->fabricante), modelo, suporte(d));
    }
    return k < max ? k : max - 1;
}
