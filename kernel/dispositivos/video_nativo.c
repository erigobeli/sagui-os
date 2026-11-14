/* video_nativo.c - resolução nativa do painel do G470 pela placa Intel HD
 * Graphics 3000 (Sandy Bridge), H84.
 *
 * De onde veio o conhecimento: só dos manuais públicos da Intel, "Intel
 * OpenSource HD Graphics Programmer's Reference Manual (PRM) for the 2011
 * Intel Core Processor Family (Sandy Bridge)": volume 1, parte 1 (espaço de
 * configuração PCI da placa, BARs, memória "roubada" e a GTT) e volume 3,
 * partes 1 a 3 (registradores de display: pipes, planos, panel fitter, e os
 * do PCH: porta LVDS e energia do painel). Nenhum driver de outro sistema foi
 * lido ou consultado. Onde a lembrança do manual não é certeza, o código não
 * aposta: confere o valor lido e desiste se não fizer sentido (D044).
 *
 * A ideia (D044): não refazer o modo do zero. Num notebook, a BIOS sempre
 * manda para o painel o sinal na resolução nativa dele (1366x768 no G470),
 * mesmo quando o VESA está em 1024x768: um circuito da placa, o "panel
 * fitter", estica a imagem de 1024x768 para 1366x768, e é por isso que o
 * texto fica borrado. Então basta:
 *   1. ler os tempos (timings) do pipe que a BIOS ligou, que dizem o tamanho
 *      nativo (HTOTAL e VTOTAL: parte "ativa" de cada linha e de cada quadro);
 *   2. dizer ao pipe que a imagem de origem já tem esse tamanho (PIPESRC);
 *   3. desligar o panel fitter (nada mais a esticar);
 *   4. dizer ao plano primário que cada linha da imagem agora tem mais bytes
 *      (DSPSTRIDE), começando no mesmo lugar onde o VESA desenhava.
 * Relógios (PLLs), tempos, FDI, transcodificador do PCH e a porta LVDS ficam
 * exatamente como a BIOS deixou: são a parte perigosa de um driver de vídeo,
 * e aqui ninguém mexe nela.
 *
 * Por segurança: desligado por padrão (configuração "video.nativo = sim",
 * lida do pendrive de dados); só age se a placa, o pipe, o painel e as
 * medidas conferirem; guarda os valores antigos e os devolve se algo der
 * errado; e escreve tudo o que leu e escreveu no diário ("registro VIDEO").
 *
 * Para os testes no QEMU (que não tem a placa Intel), "video.nativo =
 * teste-qemu" faz o mesmo caminho do lado do SaguiOS (troca de resolução com
 * o sistema já desenhando) pela placa VGA padrão do QEMU, cuja interface de
 * modos ("Bochs VBE", portas 0x1CE/0x1CF) é documentada pelo projeto Bochs.
 * Assim a tela de 1366x768 é provada na interface gráfica sem o G470. */
#include "video.h"
#include "pci.h"
#include "bootinfo.h"
#include "console.h"
#include "configuracoes.h"
#include "paginacao.h"
#include "portas.h"
#include "relogio.h"
#include "texto.h"
#include "cpu.h"
#include "mouse.h"
#include "memoria.h"

void termgraf_tela_mudou(void);

/* ------------------------------------------------------------ registradores (PRM)

 * Espaço de configuração PCI da placa (dispositivo 0:2.0), vol. 1 parte 1. */
#define CFG_COMANDO   0x04   /* bit 1: decodifica memória */
#define CFG_GTTMMADR  0x10   /* BAR0, 64 bits: registradores (2 MB) + GTT (2 MB de cima) */
#define CFG_GMADR     0x18   /* BAR2, 64 bits: abertura gráfica, por onde a CPU escreve */
#define CFG_GGC       0x50   /* 16 bits: GMS (bits 7:3, memória roubada) e GGMS (9:8, tamanho da GTT) */
#define CFG_BDSM      0x5C   /* base da memória roubada (data stolen memory) */

#define MMIO_TAMANHO  0x400000u   /* os 4 MB da BAR0 */
#define GTT_DESLOC    0x200000u   /* na Sandy Bridge, a GTT fica na metade de cima da BAR0 */

/* Pipes (vol. 3 parte 1). Pipe B = pipe A + 0x1000. Nos registradores de
 * tempo, bits 28:16 = total - 1 e bits 12:0 = ativo - 1. */
#define HTOTAL(p)    (0x60000u + (p) * 0x1000u)
#define HBLANK(p)    (0x60004u + (p) * 0x1000u)
#define HSYNC(p)     (0x60008u + (p) * 0x1000u)
#define VTOTAL(p)    (0x6000Cu + (p) * 0x1000u)
#define VBLANK(p)    (0x60010u + (p) * 0x1000u)
#define VSYNC(p)     (0x60014u + (p) * 0x1000u)
#define PIPESRC(p)   (0x6001Cu + (p) * 0x1000u)   /* bits 27:16 = largura - 1, 11:0 = altura - 1 */
#define PIPECONF(p)  (0x70008u + (p) * 0x1000u)   /* bit 31 ligar, bit 30 estado (ligado de fato) */
#define FRMCOUNT(p)  (0x70040u + (p) * 0x1000u)   /* contador de quadros: anda a cada imagem */

/* Panel fitter do pipe (vol. 3 parte 1). Pipe B = pipe A + 0x800. A escrita
 * em PF_WIN_SZ "arma" a mudança, que vale no próximo intervalo vertical. */
#define PF_WIN_POS(p) (0x68070u + (p) * 0x800u)
#define PF_WIN_SZ(p)  (0x68074u + (p) * 0x800u)
#define PF_CTL(p)     (0x68080u + (p) * 0x800u)   /* bit 31: ligado */

/* Plano primário (vol. 3 parte 1); o plano A mostra o pipe A, o B o pipe B. */
#define DSPCNTR(p)    (0x70180u + (p) * 0x1000u)   /* bit 31 ligado; 29:26 formato; bit 10 em ladrilhos */
#define DSPLINOFF(p)  (0x70184u + (p) * 0x1000u)   /* deslocamento do início (memória linear) */
#define DSPSTRIDE(p)  (0x70188u + (p) * 0x1000u)   /* bytes por linha, múltiplo de 64 */
#define DSPSURF(p)    (0x7019Cu + (p) * 0x1000u)   /* endereço gráfico da imagem, 4 KB; a escrita "arma" */
#define DSPTILEOFF(p) (0x701A4u + (p) * 0x1000u)
#define FORMATO_BGRX  6u   /* bits 29:26 = 0110: 32 bits, X:R:G:B de 8 bits (0x00RRGGBB) */

/* PCH (vol. 3 partes 2 e 3): porta LVDS e energia do painel. */
#define PCH_LVDS        0xE1180u   /* bit 31: porta ligada */
#define PCH_PP_STATUS   0xC7200u   /* bit 31: painel com energia */
#define PCH_PP_CONTROL  0xC7204u

static volatile uint32_t *mmio;

static uint32_t le(uint32_t r)              { return mmio[r / 4]; }
static void     escreve(uint32_t r, uint32_t v) { mmio[r / 4] = v; klog("[VIDEO] escrevi %x = %x\n", r, v); }

static bool desistir(const char *motivo)
{
    klog("[VIDEO] nativo: nao agi: %s. Continua o modo VESA.\n", motivo);
    return false;
}

/* Espera n quadros pelo contador do pipe; se ele não andar, espera pelo
 * relógio (n x 20 ms, mais que um quadro a 60 Hz). Devolve se andou. */
static bool esperar_quadros(int pipe, int n)
{
    uint32_t c0 = le(FRMCOUNT(pipe)), t0 = relogio_tiques();
    while (relogio_tiques() - t0 < (uint32_t)(n * 2 + 2)) {
        if (le(FRMCOUNT(pipe)) - c0 >= (uint32_t)n) return true;
        cpu_espera();
    }
    return le(FRMCOUNT(pipe)) != c0;
}

static const pci_disp_t *achar(uint16_t fabricante, uint8_t classe, pci_disp_t *d)
{
    static pci_disp_t todos[PCI_MAX];
    int n = pci_listar(todos, PCI_MAX);
    for (int i = 0; i < n; i++)
        if (todos[i].classe == classe && todos[i].subclasse == 0 && (!fabricante || todos[i].fabricante == fabricante)) {
            *d = todos[i];
            return d;
        }
    return NULL;
}

/* ------------------------------------------------------------ placa Intel */

static bool intel(const pci_disp_t *pd)
{
    if (pd->modelo != 0x0106 && pd->modelo != 0x0116 && pd->modelo != 0x0126)
        return desistir("placa Intel, mas nao e uma HD Graphics 2000/3000 de notebook Sandy Bridge (0106, 0116, 0126)");
    if (pd->barramento != 0 || pd->dispositivo != 2 || pd->funcao != 0)
        return desistir("a placa Intel nao esta em 00:02.0, onde o manual diz que ela fica");
    if (!info_boot.vesa_ativo || info_boot.vesa_bpp != 32 || info_boot.verm_pos != 16 || info_boot.verde_pos != 8 ||
        info_boot.azul_pos != 0)
        return desistir("o modo VESA nao e de 32 bits 0x00RRGGBB");

    /* PCI: memória ligada e as duas BARs abaixo de 4 GB (o SaguiOS é de 32
     * bits, sem PAE: não alcança nada acima disso). */
    uint32_t cmd = pci_le32(pd, CFG_COMANDO);
    uint32_t b0 = pci_le32(pd, CFG_GTTMMADR), b0h = pci_le32(pd, CFG_GTTMMADR + 4);
    uint32_t b2 = pci_le32(pd, CFG_GMADR), b2h = pci_le32(pd, CFG_GMADR + 4);
    uint32_t ggc = pci_le32(pd, CFG_GGC) & 0xFFFF, bdsm = pci_le32(pd, CFG_BDSM);
    klog("[VIDEO] PCI: comando %x, GTTMMADR %x:%x, GMADR %x:%x, GGC %x (roubada %u MB, GTT %u MB), BDSM %x\n",
         cmd, b0h, b0, b2h, b2, ggc, ((ggc >> 3) & 0x1F) * 32, (ggc >> 8) & 3, bdsm);
    if (!(cmd & 2)) return desistir("a placa esta com o acesso a memoria desligado");
    if (b0h || b2h) return desistir("os registradores ou a abertura da placa estao acima de 4 GB");
    uint32_t mmio_fis = b0 & 0xFFFFFFF0u, abertura = b2 & 0xFFFFFFF0u;
    if (!mmio_fis || !abertura || (b0 & 1) || (b2 & 1)) return desistir("BARs da placa sem endereco de memoria");

    mmio = (volatile uint32_t *)janela_mapear(mmio_fis, MMIO_TAMANHO);
    if (!mmio) return desistir("sem espaco na janela do kernel para os registradores");

    /* Qual pipe a BIOS ligou? Tem de ser um só (com monitor externo ligado
     * seriam dois, e aí não se sabe qual é o painel). */
    uint32_t conf[2] = { le(PIPECONF(0)), le(PIPECONF(1)) };
    klog("[VIDEO] PIPEACONF %x, PIPEBCONF %x\n", conf[0], conf[1]);
    bool liga[2] = { (conf[0] & 0xC0000000u) == 0xC0000000u, (conf[1] & 0xC0000000u) == 0xC0000000u };
    if (liga[0] == liga[1]) return desistir(liga[0] ? "os dois pipes estao ligados (monitor externo?)" : "nenhum pipe ligado");
    int p = liga[0] ? 0 : 1;

    uint32_t lvds = le(PCH_LVDS), pp = le(PCH_PP_STATUS), ppc = le(PCH_PP_CONTROL);
    klog("[VIDEO] pipe %c ligado; PCH_LVDS %x, PCH_PP_STATUS %x, PCH_PP_CONTROL %x\n", 'A' + p, lvds, pp, ppc);
    if (!(lvds & 0x80000000u)) return desistir("a porta LVDS (painel do notebook) esta desligada");
    if (!(pp & 0x80000000u)) return desistir("o painel esta sem energia");

    uint32_t ht = le(HTOTAL(p)), vt = le(VTOTAL(p)), src = le(PIPESRC(p));
    klog("[VIDEO] HTOTAL %x HBLANK %x HSYNC %x VTOTAL %x VBLANK %x VSYNC %x PIPESRC %x\n",
         ht, le(HBLANK(p)), le(HSYNC(p)), vt, le(VBLANK(p)), le(VSYNC(p)), src);
    uint32_t nl = (ht & 0x1FFF) + 1, nt = ((ht >> 16) & 0x1FFF) + 1;
    uint32_t na = (vt & 0x1FFF) + 1, vtot = ((vt >> 16) & 0x1FFF) + 1;
    uint32_t sl = ((src >> 16) & 0xFFF) + 1, sa = (src & 0xFFF) + 1;
    klog("[VIDEO] painel %ux%u (total %ux%u); origem do pipe %ux%u; VESA %ux%u\n",
         nl, na, nt, vtot, sl, sa, info_boot.vesa_largura, info_boot.vesa_altura);
    if (nl < 640 || nl > 1920 || na < 480 || na > 1200 || nt <= nl || vtot <= na)
        return desistir("os tempos do pipe nao parecem os de um painel");
    if (sl != info_boot.vesa_largura || sa != info_boot.vesa_altura)
        return desistir("a origem do pipe nao tem o tamanho do modo VESA");
    if (sl == nl && sa == na)
        return desistir("a BIOS ja usa a resolucao nativa: nada a fazer");
    if (sl > nl || sa > na) return desistir("o modo VESA e maior que o painel");

    uint32_t pfc = le(PF_CTL(p)), pfp = le(PF_WIN_POS(p)), pfs = le(PF_WIN_SZ(p));
    uint32_t cntr = le(DSPCNTR(p)), lin = le(DSPLINOFF(p)), passo = le(DSPSTRIDE(p));
    uint32_t surf = le(DSPSURF(p)), tile = le(DSPTILEOFF(p));
    klog("[VIDEO] PF_CTL %x PF_WIN_POS %x PF_WIN_SZ %x\n", pfc, pfp, pfs);
    klog("[VIDEO] DSPCNTR %x DSPLINOFF %x DSPSTRIDE %x DSPSURF %x DSPTILEOFF %x\n", cntr, lin, passo, surf, tile);
    if (!(cntr & 0x80000000u)) return desistir("o plano primario do pipe esta desligado");
    if (((cntr >> 26) & 0xF) != FORMATO_BGRX) return desistir("o plano nao esta em 32 bits BGRX");
    if (cntr & (1u << 10)) return desistir("a imagem do plano esta em ladrilhos (tiled), nao linear");
    if (passo != info_boot.vesa_pitch) return desistir("o passo do plano nao e o do modo VESA");
    uint32_t inicio = (surf & 0xFFFFF000u) + lin;   /* endereço gráfico do primeiro pixel */
    if (abertura + inicio != info_boot.vesa_fb) return desistir("o plano nao mostra o framebuffer do VESA");

    /* Passo novo: largura x 4 bytes, arredondado para 64 (o manual exige
     * múltiplo de 64 para memória linear). Ex.: 1366 x 4 = 5464 -> 5504. */
    uint32_t passo_novo = (nl * 4 + 63) & ~63u, bytes = passo_novo * na;
    if (passo_novo > 32768) return desistir("passo grande demais");

    /* A imagem nova começa no mesmo lugar e é maior: confere na GTT que a
     * BIOS mapeou todas as páginas (cada entrada: bit 0 válida, bits 31:12
     * o endereço físico). A BIOS mapeia toda a memória roubada, que tem pelo
     * menos 32 MB; 4 MB de tela cabem com folga. Se faltar alguma, o
     * SaguiOS não inventa memória para a placa: desiste e conta. */
    volatile uint32_t *gtt = mmio + GTT_DESLOC / 4;
    uint32_t pag0 = inicio / 4096, pag1 = (inicio + bytes - 1) / 4096;
    klog("[VIDEO] GTT: paginas %u a %u; primeira %x, ultima %x\n", pag0, pag1, gtt[pag0], gtt[pag1]);
    if (pag1 >= GTT_DESLOC / 4) return desistir("a imagem passa do fim da GTT");
    for (uint32_t k = pag0; k <= pag1; k++)
        if (!(gtt[k] & 1)) {
            klog("[VIDEO] GTT: entrada %u = %x nao e valida\n", k, gtt[k]);
            return desistir("a GTT da BIOS nao cobre a imagem nova");
        }

    /* Tudo pronto antes de mexer na placa: memória da CPU e buffer de trás. */
    bool wc = false;
    uint8_t *tela = (uint8_t *)janela_mapear_wc(info_boot.vesa_fb, bytes, &wc);
    uint32_t *tras = tela ? video_reservar(nl, na) : NULL;
    if (!tela || !tras) return desistir("sem memoria para a tela nova");

    /* Contador de quadros: se ele anda agora, tem de continuar andando
     * depois (sinal de que o pipe segue vivo). */
    bool contador = esperar_quadros(p, 1);
    klog("[VIDEO] contador de quadros do pipe %s\n", contador ? "andando" : "parado (espera pelo relogio)");

    /* A troca. A imagem fica preta por um instante (a tela já está toda
     * escura enquanto o sistema liga). */
    for (uint32_t y = 0; y < na; y++) memset(tela + y * passo_novo, 0, passo_novo);
    escreve(PIPESRC(p), ((nl - 1) << 16) | (na - 1));
    escreve(PF_CTL(p), 0);
    escreve(PF_WIN_POS(p), 0);
    escreve(PF_WIN_SZ(p), 0);
    escreve(DSPSTRIDE(p), passo_novo);
    escreve(DSPSURF(p), surf);   /* mesmo endereço: a escrita só arma a mudança do plano */
    bool andou = esperar_quadros(p, 3);

    uint32_t conf2 = le(PIPECONF(p)), src2 = le(PIPESRC(p)), passo2 = le(DSPSTRIDE(p)), pfc2 = le(PF_CTL(p));
    klog("[VIDEO] depois: PIPECONF %x PIPESRC %x DSPSTRIDE %x PF_CTL %x DSPCNTR %x, quadros %s\n",
         conf2, src2, passo2, pfc2, le(DSPCNTR(p)), andou ? "andando" : "parados");
    const char *erro = NULL;
    if ((conf2 & 0xC0000000u) != 0xC0000000u) erro = "o pipe desligou";
    else if (src2 != (((nl - 1) << 16) | (na - 1))) erro = "a placa nao aceitou o tamanho de origem";
    else if (passo2 != passo_novo) erro = "a placa nao aceitou o passo novo";
    else if (pfc2 & 0x80000000u) erro = "o panel fitter nao desligou";
    else if (contador && !andou) erro = "o contador de quadros parou";
    if (erro) {
        /* Volta tudo como a BIOS deixou, na ordem contrária. */
        klog("[VIDEO] nativo: %s; devolvendo os valores da BIOS\n", erro);
        escreve(DSPSTRIDE(p), passo);
        escreve(PIPESRC(p), src);
        escreve(PF_WIN_POS(p), pfp);
        escreve(PF_CTL(p), pfc);
        escreve(PF_WIN_SZ(p), pfs);
        escreve(DSPSURF(p), surf);
        esperar_quadros(p, 2);
        kfree(tras);
        video_mostrar_tudo();
        return desistir(erro);
    }
    video_adotar(tela, tras, nl, na, passo_novo);
    klog("[VIDEO] nativo: resolucao nativa do painel ligada, %ux%u (%s)\n", nl, na,
         wc ? "com escrita combinada" : "sem escrita combinada");
    return true;
}

/* ------------------------------------------------------------ teste no QEMU

 * A placa VGA padrão do QEMU (1234:1111) segue a interface "Bochs VBE": um
 * índice na porta 0x1CE e o valor na 0x1CF. Índices: 0 identificação
 * (0xB0C0 a 0xB0C5), 1 largura, 2 altura, 3 bits por pixel, 4 ligar (bit 0
 * ligado, bit 6 framebuffer linear), 6 largura virtual (o passo, em pixels).
 * Simula um painel de 1366x768, como o do G470. */
#define BOCHS_INDICE 0x1CE
#define BOCHS_DADO   0x1CF

static uint16_t bochs_le(uint16_t i)            { porta_escreve16(BOCHS_INDICE, i); return porta_le16(BOCHS_DADO); }
static void     bochs_escreve(uint16_t i, uint16_t v) { porta_escreve16(BOCHS_INDICE, i); porta_escreve16(BOCHS_DADO, v); }

static bool teste_qemu(const pci_disp_t *pd)
{
    if (pd->fabricante != 0x1234 || pd->modelo != 0x1111) return desistir("teste-qemu so vale na placa VGA padrao do QEMU");
    uint16_t id = bochs_le(0);
    uint32_t fb = pci_le32(pd, 0x10) & 0xFFFFFFF0u;
    klog("[VIDEO] teste-qemu: Bochs VBE %x, framebuffer %x\n", id, fb);
    if (id < 0xB0C0 || id > 0xB0C5 || fb != info_boot.vesa_fb) return desistir("teste-qemu: placa nao confere");
    /* O QEMU só aceita larguras múltiplas de 8: a placa fica em 1368 e o
     * SaguiOS usa 1366 delas (as 2 colunas da direita ficam pretas). Assim
     * a interface é provada com a largura exata do painel do G470. */
    const uint32_t l = 1366, a = 768;
    uint32_t *tras = video_reservar(l, a);
    if (!tras) return desistir("sem memoria para a tela nova");
    bochs_escreve(4, 0);
    bochs_escreve(1, (uint16_t)((l + 7) & ~7u));
    bochs_escreve(2, (uint16_t)a);
    bochs_escreve(3, 32);
    bochs_escreve(4, 0x41);
    uint32_t xl = bochs_le(1), ya = bochs_le(2), virt = bochs_le(6);
    klog("[VIDEO] teste-qemu: placa em %ux%u, largura virtual %u\n", xl, ya, virt);
    if (xl < l || ya != a || virt < l) {
        kfree(tras);
        return desistir("teste-qemu: a placa nao aceitou 1366x768");
    }
    bool wc = false;
    uint8_t *tela = (uint8_t *)janela_mapear_wc(fb, virt * 4 * a, &wc);
    if (!tela) { kfree(tras); return desistir("sem espaco para a tela nova"); }
    video_adotar(tela, tras, l, a, virt * 4);
    klog("[VIDEO] nativo: teste-qemu: tela trocada para %ux%u com o sistema ligado\n", l, a);
    return true;
}

/* ------------------------------------------------------------ entrada */

bool video_nativo_tentar(void)
{
    if (!video_ativo()) return false;
    const char *cfg = config_obter("video.nativo");
    pci_disp_t pd;
    const pci_disp_t *placa = achar(0, 0x03, &pd);
    if (placa)
        klog("[VIDEO] placa de video %04x:%04x em %02x:%02x.%x\n", placa->fabricante, placa->modelo,
             placa->barramento, placa->dispositivo, placa->funcao);
    if (!cfg || (txt_compara(cfg, "sim") != 0 && txt_compara(cfg, "teste-qemu") != 0))
        return desistir("desligado (a configuracao video.nativo = sim liga)");
    if (!placa) return desistir("nenhuma placa de video no PCI");
    bool ok;
    if (txt_compara(cfg, "teste-qemu") == 0) ok = teste_qemu(placa);
    else if (placa->fabricante != 0x8086) ok = desistir("a placa de video nao e Intel");
    else ok = intel(placa);
    if (ok) {
        termgraf_tela_mudou();
        mouse_tela_mudou();
    }
    return ok;
}
