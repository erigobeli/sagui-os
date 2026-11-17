/* instalar - o instalador do SaguiOS: um assistente em passos, como os de
 * outros sistemas (boas-vindas, disco, partições, resumo, instalação).
 *
 * O que ele faz no disco escolhido:
 *   - o cabeçalho de boot no setor 1 e o MBR do SaguiOS no setor 0 (com a
 *     tabela de partições e um identificador de disco novo);
 *   - a partição do sistema (tipo 0xDA), com o estágio 2, o kernel e o
 *     ramdisk copiados do disco de onde o SaguiOS ligou;
 *   - uma partição FAT32 (tipo 0x0C) com o rótulo SAGUIOS e o arquivo de
 *     configurações: no boot pelo disco, o kernel a monta em /usb sozinho, e
 *     o que você guardar ali continua depois de desligar.
 *
 * Dá para usar o disco inteiro (apaga tudo) ou um espaço livre, mantendo as
 * outras partições. No passo das partições também dá para excluir, criar e
 * formatar partições FAT32. Nada é gravado antes do botão "Instalar" no
 * resumo: até lá as mudanças ficam só aqui, como nos outros instaladores.
 *
 * Não há jeito de pular a confirmação: o kernel só deixa ler e gravar os
 * setores do disco depois de a pessoa digitar o nome dele numa caixa
 * desenhada pelo próprio kernel (SIS_DISCO_LIBERAR, D063).
 *
 * A FAT32 é formatada aqui, pela especificação da Microsoft ("FAT32 File
 * System Specification", 2000): setor de boot com o BPB, FSInfo, cópia do
 * setor de boot no setor 6, duas FATs e a pasta raiz no cluster 2. */
#include "sagui.h"
#include "gui.h"

extern const uint32_t arte_px_disco_40[], arte_px_pendrive_40[], arte_px_instalar_sistema[], arte_px_sobre_40[];

#define SETOR      512
#define POR_VEZ    128                    /* setores por chamada */
#define MB         2048u                  /* setores num MB */
#define DADOS_MIN  (64u * MB)             /* 64 MB para os arquivos, no mínimo */

static uint8_t *buf;                      /* POR_VEZ setores */
static char erro[120];

/* ------------------------------------------------------------ setores */

static int setores(const char *disco, uint32_t lba, uint32_t n, void *b, bool gravar)
{
    pedido_setores_t p;
    memset(&p, 0, sizeof p);
    copia_texto(p.nome, disco, sizeof p.nome);
    p.lba = lba; p.n = n; p.escrever = gravar; p.buf = b;
    int r = disco_setores(&p);
    if (r < 0) formatar_texto(erro, sizeof erro, "%s %s no setor %u: %s", gravar ? "gravando" : "lendo", disco, lba, nome_erro(r));
    return r;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void poe16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void poe32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t acima_mb(uint32_t s) { return (s + MB - 1) / MB * MB; }

/* CHS de 3 bytes da tabela de partições (255 cabeças, 63 setores). */
static void chs(uint8_t *p, uint32_t lba)
{
    uint32_t c = lba / (255 * 63), h = (lba / 63) % 255, s = lba % 63 + 1;
    if (c > 1023) { c = 1023; h = 254; s = 63; }
    p[0] = (uint8_t)h; p[1] = (uint8_t)(s | ((c >> 2) & 0xC0)); p[2] = (uint8_t)c;
}

/* Grava n setores de zeros a partir de lba. */
static int zerar(const char *disco, uint32_t lba, uint32_t n)
{
    memset(buf, 0, POR_VEZ * SETOR);
    while (n) {
        uint32_t k = n > POR_VEZ ? POR_VEZ : n;
        if (setores(disco, lba, k, buf, true) < 0) return -1;
        lba += k; n -= k;
    }
    return 0;
}

/* Formata uma FAT32 em [ini, ini + n) com o rótulo dado (até 11 letras). */
static int formatar_fat32(const char *disco, uint32_t ini, uint32_t n, const char *rotulo)
{
    /* Setores por cluster pela tabela da especificação. */
    uint32_t spc = n <= 532480 ? 1 : n <= 16777216 ? 8 : n <= 33554432 ? 16 : n <= 67108864 ? 32 : 64;
    const uint32_t reservados = 32, nfats = 2;
    uint32_t t1 = n - reservados, t2 = (256 * spc + nfats) / 2;
    uint32_t fatsz = (t1 + t2 - 1) / t2;
    uint32_t dados = n - reservados - nfats * fatsz, clusters = dados / spc;
    if (clusters < 65525) { copia_texto(erro, "o espaço é pequeno demais para uma FAT32 (mínimo de 33 MB)", sizeof erro); return -1; }
    char nome11[12];
    memset(nome11, ' ', 11);
    for (int i = 0; i < 11 && rotulo[i]; i++) nome11[i] = rotulo[i];

    uint8_t s[SETOR];
    memset(s, 0, sizeof s);
    s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90;
    memcpy(s + 3, "SAGUIOS ", 8);
    poe16(s + 11, SETOR);
    s[13] = (uint8_t)spc;
    poe16(s + 14, reservados);
    s[16] = (uint8_t)nfats;
    s[21] = 0xF8;                                    /* disco fixo */
    poe16(s + 24, 63);
    poe16(s + 26, 255);
    poe32(s + 28, ini);                              /* setores antes da partição */
    poe32(s + 32, n);
    poe32(s + 36, fatsz);
    poe32(s + 44, 2);                                /* pasta raiz no cluster 2 */
    poe16(s + 48, 1);                                /* FSInfo */
    poe16(s + 50, 6);                                /* cópia do setor de boot */
    s[64] = 0x80;
    s[66] = 0x29;
    poe32(s + 67, tempo_ms() ^ 0x5A6E1D05u ^ ini);   /* número de série */
    memcpy(s + 71, nome11, 11);
    memcpy(s + 82, "FAT32   ", 8);
    s[510] = 0x55; s[511] = 0xAA;

    uint8_t fsinfo[SETOR];
    memset(fsinfo, 0, sizeof fsinfo);
    poe32(fsinfo, 0x41615252);
    poe32(fsinfo + 484, 0x61417272);
    poe32(fsinfo + 488, clusters - 1);               /* livres (o 2 é a raiz) */
    poe32(fsinfo + 492, 3);                          /* próximo livre */
    poe32(fsinfo + 508, 0xAA550000);

    /* Área reservada e as FATs zeradas; depois os setores com conteúdo. */
    if (zerar(disco, ini, reservados + nfats * fatsz) < 0) return -1;
    if (zerar(disco, ini + reservados + nfats * fatsz, spc) < 0) return -1;   /* a pasta raiz */
    if (setores(disco, ini, 1, s, true) < 0 || setores(disco, ini + 6, 1, s, true) < 0) return -1;
    if (setores(disco, ini + 1, 1, fsinfo, true) < 0 || setores(disco, ini + 7, 1, fsinfo, true) < 0) return -1;
    uint8_t fat[SETOR];
    memset(fat, 0, sizeof fat);
    poe32(fat, 0x0FFFFFF8);
    poe32(fat + 4, 0x0FFFFFFF);
    poe32(fat + 8, 0x0FFFFFFF);                      /* a raiz: um cluster só */
    for (uint32_t f = 0; f < nfats; f++)
        if (setores(disco, ini + reservados + f * fatsz, 1, fat, true) < 0) return -1;
    uint8_t raiz[SETOR];
    memset(raiz, 0, sizeof raiz);
    memcpy(raiz, nome11, 11);
    raiz[11] = 0x08;                                 /* rótulo do volume */
    if (setores(disco, ini + reservados + nfats * fatsz, 1, raiz, true) < 0) return -1;
    return 0;
}

/* ------------------------------------------------------------ os discos */

#define MAX_DISCOS 4
static info_disco_t discos[MAX_DISCOS];
static int qtd_discos, origem = -1, escolhido = -1;

static void tamanho_texto(uint32_t setores_n, char *t, int max)
{
    uint32_t mb = setores_n / MB;
    if (mb >= 1024) formatar_texto(t, (size_t)max, "%u,%u GB", mb / 1024, mb % 1024 * 10 / 1024);
    else formatar_texto(t, (size_t)max, "%u MB", mb);
}

static void ler_discos(void)
{
    qtd_discos = 0;
    origem = -1;
    info_disco_t d;
    for (int i = 0; qtd_discos < MAX_DISCOS && listar_discos(i, &d) == 1; i++) {
        if (d.origem) origem = qtd_discos;
        discos[qtd_discos++] = d;
    }
}

/* ------------------------------------------------------------ as partições (só na memória até instalar) */

typedef struct {
    uint8_t  tipo;          /* 0: entrada vazia */
    uint32_t ini, n;
    bool     nova, formatar, excluida;
    bool     saguios;       /* formatada para o SaguiOS: o sistema vai no lugar dela */
    char     rotulo[12];
} part_t;

static part_t parts[4];
static bool gpt;
static uint32_t total;                    /* setores do disco escolhido */

/* As regiões do disco em ordem: partições (p >= 0) e espaços livres (p = -1). */
typedef struct { int p; uint32_t ini, n; } regiao_t;
static regiao_t reg[12];
static int qtd_reg, sel_reg = -1, topo;   /* topo: a primeira linha mostrada na lista */

/* Onde instalar: o disco inteiro (apaga tudo) ou um espaço livre. */
static bool modo_inteiro = true;
static int livre_inst = -1;               /* a região livre escolhida (modo espaço livre) */

static const char *nome_tipo(uint8_t t)
{
    switch (t) {
    case 0x0B: case 0x0C: return "FAT32";
    case 0x01: case 0x04: case 0x06: case 0x0E: return "FAT12/16";
    case 0x07: return "NTFS ou exFAT";
    case 0x27: return "recuperação do Windows";
    case 0x83: return "Linux (ext)";
    case 0x82: return "Linux (troca)";
    case 0x05: case 0x0F: case 0x85: return "estendida";
    case 0xDA: return "SaguiOS (sistema)";
    case 0xEE: return "GPT";
    default:   return "outro tipo";
    }
}

static uint32_t cor_tipo(uint8_t t)
{
    switch (t) {
    case 0x0B: case 0x0C: return 0x2A6FD8;
    case 0x07: case 0x27: return 0x0098D8;
    case 0x83: return 0xE2611E;
    case 0x82: return 0x9B59B6;
    case 0xDA: return COR_DESTAQUE;
    default:   return 0x7C8792;
    }
}

static void montar_regioes(void)
{
    qtd_reg = 0;
    int ordem[4], n = 0;
    for (int k = 0; k < 4; k++) if (parts[k].tipo && !parts[k].excluida) ordem[n++] = k;
    for (int a = 0; a < n; a++)                    /* em ordem de início */
        for (int b = a + 1; b < n; b++)
            if (parts[ordem[b]].ini < parts[ordem[a]].ini) { int t = ordem[a]; ordem[a] = ordem[b]; ordem[b] = t; }
    uint32_t pos = MB;                              /* o primeiro MB fica para o MBR e o cabeçalho de boot */
    for (int a = 0; a <= n; a++) {
        uint32_t fim = a < n ? parts[ordem[a]].ini : total;
        uint32_t ini = acima_mb(pos);
        if (fim > ini && fim - ini >= MB && qtd_reg < 12) reg[qtd_reg++] = (regiao_t){ -1, ini, (fim - ini) / MB * MB };
        if (a < n && qtd_reg < 12) {
            reg[qtd_reg++] = (regiao_t){ ordem[a], parts[ordem[a]].ini, parts[ordem[a]].n };
            pos = parts[ordem[a]].ini + parts[ordem[a]].n;
        }
    }
    if (sel_reg >= qtd_reg) sel_reg = qtd_reg - 1;
}

static int entradas_livres(void)
{
    int n = 0;
    for (int k = 0; k < 4; k++) if (!parts[k].tipo || parts[k].excluida) n++;
    return n;
}

static const char *motivo_livre(void);

/* Lê a tabela do disco escolhido (depois da liberação). */
static int ler_tabela(void)
{
    const info_disco_t *d = &discos[escolhido];
    uint8_t mbr[SETOR];
    total = d->setores;
    memset(parts, 0, sizeof parts);
    gpt = false;
    if (setores(d->nome, 0, 1, mbr, false) < 0) return -1;
    int usadas = 0;
    if (mbr[510] == 0x55 && mbr[511] == 0xAA)
        for (int k = 0; k < 4; k++) {
            const uint8_t *e = mbr + 446 + k * 16;
            if (!e[4]) continue;
            if (e[4] == 0xEE) gpt = true;
            part_t *p = &parts[k];
            p->tipo = e[4];
            p->ini = le32(e + 8);
            p->n = le32(e + 12);
            if (p->ini >= total || !p->n) { p->tipo = 0; continue; }
            if (p->ini + p->n > total || p->ini + p->n < p->ini) p->n = total - p->ini;
            usadas++;
            if (p->tipo == 0x0B || p->tipo == 0x0C) {          /* o rótulo da FAT32 */
                uint8_t s[SETOR];
                if (setores(d->nome, p->ini, 1, s, false) == 0 && s[510] == 0x55) {
                    memcpy(p->rotulo, s + 71, 11);
                    int f = 11;
                    while (f > 0 && p->rotulo[f - 1] == ' ') f--;
                    p->rotulo[f] = 0;
                    if (compara_texto(p->rotulo, "NO NAME") == 0) p->rotulo[0] = 0;
                }
            }
        }
    modo_inteiro = gpt || !usadas;
    livre_inst = -1;
    sel_reg = -1;
    montar_regioes();
    /* Com partições, já sugere o maior espaço livre onde o SaguiOS cabe.
     * Sem nenhum, nada fica marcado: apagar o disco tem de ser escolha. */
    if (!modo_inteiro) {
        for (int r = 0; r < qtd_reg; r++)
            if (reg[r].p < 0 && (sel_reg < 0 || reg[r].n > reg[sel_reg].n)) sel_reg = r;
        if (sel_reg >= 0 && !motivo_livre()) livre_inst = sel_reg;
    }
    registrar("instalar: particoes de %s: %d partições, %d regiões%s", d->nome, usadas, qtd_reg, gpt ? " (GPT)" : "");
    return 0;
}

/* ------------------------------------------------------------ o sistema a copiar */

typedef struct {
    uint8_t  cab[SETOR];                  /* o cabeçalho de boot (setor 1) da origem */
    uint8_t  mbr[SETOR];                  /* o MBR da origem: o código de boot vem dele */
    uint32_t ini, n;                      /* setores do sistema na origem (estágio 2, kernel, ramdisk) */
} sistema_t;
static sistema_t sis;
static bool sis_ok;

/* O cabeçalho (boot/cabecalho.inc): 'SGHD', LBA e tamanho do estágio 2, do
 * kernel e do ramdisk, e o fim da imagem (CAB_IMG_SETORES). */
#define CAB_E2_LBA  8
#define CAB_K_LBA   16
#define CAB_R_LBA   32
#define CAB_IMG_SET 48

static bool ler_sistema(void)
{
    if (origem < 0) return false;
    const char *o = discos[origem].nome;
    if (setores(o, 0, 1, sis.mbr, false) < 0 || setores(o, 1, 1, sis.cab, false) < 0) return false;
    if (le32(sis.cab) != 0x44484753) { copia_texto(erro, "o cabeçalho de boot da origem não é do SaguiOS", sizeof erro); return false; }
    sis.ini = le32(sis.cab + CAB_E2_LBA);
    uint32_t fim = le32(sis.cab + CAB_IMG_SET);
    if (sis.ini < 2 || fim <= sis.ini) { copia_texto(erro, "o cabeçalho de boot da origem está estranho", sizeof erro); return false; }
    sis.n = fim - sis.ini;
    return true;
}

static uint32_t sistema_mb(void) { return acima_mb(sis.n); }   /* em setores, arredondado em MB */
static uint32_t precisa(void) { return sistema_mb() + DADOS_MIN; }

/* Por que não dá para instalar no espaço livre selecionado (NULL: dá). */
static const char *motivo_livre(void)
{
    if (gpt) return "GPT: só MBR por enquanto";
    if (sel_reg < 0) return "escolha um espaço livre";
    bool alvo = reg[sel_reg].p >= 0 && parts[reg[sel_reg].p].saguios;
    if (reg[sel_reg].p >= 0 && !alvo) return "escolha um espaço livre";
    if (reg[sel_reg].n < precisa()) return alvo ? "partição pequena demais" : "espaço livre pequeno demais";
    if (entradas_livres() + (alvo ? 1 : 0) < 2) return "sem entrada livre na tabela";
    return NULL;
}

/* ------------------------------------------------------------ a instalação */

static void (*avisar_progresso)(int pct, const char *etapa);

static void progresso(int pct, const char *etapa) { if (avisar_progresso) avisar_progresso(pct, etapa); }

static void entrada(uint8_t *e, uint8_t tipo, bool ativa, uint32_t ini, uint32_t n)
{
    memset(e, 0, 16);
    if (!tipo) return;
    e[0] = ativa ? 0x80 : 0;
    chs(e + 1, ini);
    e[4] = tipo;
    chs(e + 5, ini + n - 1);
    poe32(e + 8, ini);
    poe32(e + 12, n);
}

static int instalar(void)
{
    const char *dest = discos[escolhido].nome;
    /* Onde vai cada coisa. */
    uint32_t r_ini, r_n;
    if (modo_inteiro) {
        for (int k = 0; k < 4; k++) if (parts[k].tipo) parts[k].excluida = true;
        r_ini = MB;
        r_n = (total - MB) / MB * MB;
    } else {
        r_ini = acima_mb(reg[livre_inst].ini);
        r_n = (reg[livre_inst].ini + reg[livre_inst].n - r_ini) / MB * MB;
        if (reg[livre_inst].p >= 0) parts[reg[livre_inst].p].excluida = true;   /* formatada para o SaguiOS */
    }
    uint32_t s_ini = r_ini, s_n = sistema_mb();
    uint32_t d_ini = s_ini + s_n, d_n = r_ini + r_n - d_ini;
    if (r_n < precisa()) { formatar_texto(erro, sizeof erro, "o espaço é pequeno demais (precisa de %u MB)", precisa() / MB); return -1; }
    registrar("instalar: %s -> %s: sistema em %u (%u setores), arquivos em %u (%u MB)%s",
              discos[origem].nome, dest, s_ini, sis.n, d_ini, d_n / MB, modo_inteiro ? ", disco inteiro" : ", espaço livre");

    /* 1. As partições novas e as formatações pedidas no passo das partições. */
    int qtd_fmt = 0, feitas = 0;
    for (int k = 0; k < 4; k++) if (parts[k].tipo && !parts[k].excluida && parts[k].formatar) qtd_fmt++;
    for (int k = 0; k < 4; k++) {
        part_t *p = &parts[k];
        if (!p->tipo || p->excluida || !p->formatar) continue;
        progresso(2 + 8 * feitas++ / qtd_fmt, "Formatando as partições");
        if (formatar_fat32(dest, p->ini, p->n, p->rotulo[0] ? p->rotulo : "DADOS") < 0) return -1;
        registrar("instalar: formatada FAT32 em %u (%u MB) \"%s\"", p->ini, p->n / MB, p->rotulo);
    }

    /* 2. O sistema, setor por setor, da origem para o lugar novo. */
    for (uint32_t k = 0; k < sis.n; ) {
        uint32_t q = sis.n - k > POR_VEZ ? POR_VEZ : sis.n - k;
        if (setores(discos[origem].nome, sis.ini + k, q, buf, false) < 0 || setores(dest, s_ini + k, q, buf, true) < 0) return -1;
        k += q;
        progresso(10 + (int)(k * 60 / sis.n), "Copiando o sistema");
    }

    /* 3. A FAT32 dos arquivos, com o rótulo SAGUIOS. */
    progresso(72, "Criando a partição dos seus arquivos");
    if (formatar_fat32(dest, d_ini, d_n, "SAGUIOS") < 0) return -1;

    /* 4. O cabeçalho de boot no setor 1, com os LBA do lugar novo. */
    static const int campos[3] = { CAB_E2_LBA, CAB_K_LBA, CAB_R_LBA };
    uint8_t cab[SETOR];
    memcpy(cab, sis.cab, SETOR);
    for (int c = 0; c < 3; c++) poe32(cab + campos[c], le32(sis.cab + campos[c]) - sis.ini + s_ini);
    poe32(cab + CAB_IMG_SET, s_ini + sis.n);
    progresso(85, "Gravando o setor de boot");
    if (setores(dest, 1, 1, cab, true) < 0) return -1;

    /* 5. O MBR: o código de boot do SaguiOS, um identificador novo e a
     * tabela com as partições que ficaram, o sistema (ativa) e os arquivos. */
    uint8_t m[SETOR];
    memcpy(m, sis.mbr, 440);
    uint32_t id = 0;
    while (!id) bytes_aleatorios(&id, sizeof id);
    poe32(m + 440, id);
    m[444] = m[445] = 0;
    int livre[4], nl = 0;
    for (int k = 0; k < 4; k++) {
        part_t *p = &parts[k];
        if (!p->tipo || p->excluida) { entrada(m + 446 + k * 16, 0, false, 0, 0); livre[nl++] = k; }
        else entrada(m + 446 + k * 16, p->tipo, false, p->ini, p->n);
    }
    if (nl < 2) { copia_texto(erro, "a tabela de partições não tem duas entradas livres", sizeof erro); return -1; }
    entrada(m + 446 + livre[0] * 16, 0xDA, true, s_ini, s_n);
    entrada(m + 446 + livre[1] * 16, 0x0C, false, d_ini, d_n);
    m[510] = 0x55; m[511] = 0xAA;
    progresso(90, "Gravando a tabela de partições");
    if (setores(dest, 0, 1, m, true) < 0) return -1;

    /* 6. O arquivo de configurações: é ele que faz o sistema montar a
     * partição sozinho no boot. Só um disco fica montado por vez. */
    progresso(95, "Guardando as configurações");
    info_disco_t d;
    for (int i = 0; listar_discos(i, &d) == 1; i++)
        if (d.montado) desmontar(d.nome);
    if (disco_reler(dest) < 0 || montar(dest) < 0) { formatar_texto(erro, sizeof erro, "a partição nova de %s não montou", dest); return -1; }
    criar_pasta("/usb/.saguios");
    int fd = abrir("/usb/.saguios/configuracoes.txt", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd >= 0) {
        const char *t = "# SaguiOS instalado neste disco\nsistema.instalado = sim\n";
        escrever(fd, t, (unsigned)tam_texto(t));
        fechar(fd);
    }
    desmontar(dest);
    if (fd < 0) { copia_texto(erro, "não deu para criar o arquivo de configurações", sizeof erro); return -1; }
    progresso(100, "Pronto");
    registrar("instalar: pronto em %s", dest);
    return 0;
}

/* ------------------------------------------------------------ a janela */

/* O tamanho da janela muda (ela é redimensionável e nasce do tamanho da
 * tela): tudo é medido a partir dela em medir(). Numa janela estreita, a
 * coluna dos passos some e o conteúdo usa a largura toda. */
static int L = 820, A = 560;
static int LADO, X0, CL, RODAPE_Y;
static int LINHAS = 5;                    /* linhas visíveis da lista de partições */
static bool modos_empilhados;             /* os dois modos um embaixo do outro */
static bool compacto;                     /* janela baixa (640x480): sem a barra do disco */
static int BARRA_Y = 104, LISTA_Y = 172;

#define COR_LADO     0xE6ECF0
#define COR_CARTAO   0xFFFFFF
#define COR_LINHA    0xD3DAE0
#define COR_VERDE_CL 0xE6F4EB
#define COR_AVISO    0xFFF4D6
#define COR_PERIGO   0xC4302B

enum { P_BOAS_VINDAS, P_DISCO, P_PARTICOES, P_RESUMO, P_INSTALANDO, P_PRONTO, P_ERRO };
static const char *const nomes_passos[5] = { "Boas-vindas", "Disco", "Partições", "Resumo", "Instalação" };

static jan_t *j;
static int passo;

/* Botões arredondados: o principal é verde cheio (vermelho no Instalar); os
 * outros, brancos com borda. */
typedef struct { int x, y, l, a; const char *texto; bool principal, ativo, perigo; } bt_t;
enum { B_VOLTAR, B_CONTINUAR, B_CANCELAR, B_REINICIAR, B_EXCLUIR, B_NOVA, B_FORMATAR, B_TUDO, B_MODO_INTEIRO, B_MODO_LIVRE, QTD_BT };
static bt_t bts[QTD_BT];

static int lim;                           /* (mais abaixo, com tx) */
static void tx(int x, int y, const char *t, uint32_t cor);

static void bt(int i, int x, int y, int l, const char *texto, bool principal, bool ativo)
{
    bts[i] = (bt_t){ x, y, l, 34, texto, principal, ativo, false };
}

static void desenhar_bt(const bt_t *b)
{
    uint32_t fundo = !b->ativo ? 0xEEF1F3 : b->principal ? (b->perigo ? COR_PERIGO : COR_DESTAQUE) : COR_CARTAO;
    uint32_t base = !b->ativo ? 0xE6EAED : b->principal ? (b->perigo ? 0xA82420 : 0x086331) : 0xF1F4F6;
    g_ret_redondo(j, b->x, b->y, b->l, b->a, 9, fundo, base);
    if (!b->principal || !b->ativo) g_contorno_redondo(j, b->x, b->y, b->l, b->a, 9, b->ativo ? 0xB8C2CB : 0xD8DEE3);
    uint32_t ct = !b->ativo ? 0xA5AEB6 : b->principal ? COR_BRANCO : COR_TEXTO;
    int w = g_largura_texto(b->texto), antes = lim;
    if (w > b->l - 12) w = b->l - 12;
    lim = b->x + b->l - 6;
    tx(b->x + (b->l - w) / 2, b->y + (b->a - 16) / 2, b->texto, ct);
    lim = antes;
}

static int bt_em(int x, int y)
{
    for (int i = 0; i < QTD_BT; i++) {
        const bt_t *b = &bts[i];
        if (b->texto && b->ativo && x >= b->x && x < b->x + b->l && y >= b->y && y < b->y + b->a) return i;
    }
    return -1;
}

/* Até onde o texto pode ir (x da borda direita); o que passar vira "...". */
static int lim = 1 << 30;

static void tx(int x, int y, const char *t, uint32_t cor)
{
    int max = lim - x;
    if (max < 8 * 4) return;
    if (g_largura_texto(t) <= max) { g_texto(j, x, y, t, cor); return; }
    char b[160];
    int n = max / 8 - 3;                       /* a fonte tem 8 px por letra (Latin-1) */
    if (n > (int)sizeof b - 4) n = (int)sizeof b - 4;
    memcpy(b, t, (size_t)n);
    while (n > 0 && b[n - 1] == ' ') n--;
    b[n] = '.'; b[n + 1] = '.'; b[n + 2] = '.'; b[n + 3] = 0;
    g_texto(j, x, y, b, cor);
}

static void negrito(int x, int y, const char *t, uint32_t cor)
{
    tx(x, y, t, cor);
    lim++;
    tx(x + 1, y, t, cor);
    lim--;
}

static void medir(void)
{
    L = j->larg;
    A = j->alt;
    LADO = L >= 800 ? 212 : 0;
    int m = L >= 560 ? 28 : 16;
    X0 = LADO + m;
    CL = L - X0 - m;
    RODAPE_Y = A - 58;
    modos_empilhados = CL < 500;
    compacto = A < 460;
    LISTA_Y = compacto ? 104 : 172;
    /* A lista de partições fica com o que sobra da altura. */
    /* A lista começa em 172; depois dela vêm os botões e o título (84) e os
     * cartões dos modos; o rodapé ocupa os últimos 72. */
    int modos = modos_empilhados ? 56 + 62 : 56;
    LINHAS = (A - 72 - LISTA_Y - (compacto ? 60 : 84) - modos) / 28;
    if (LINHAS > 5) LINHAS = 5;
    if (LINHAS < 1) LINHAS = 1;
}

static void cartao(int x, int y, int l, int a, uint32_t fundo, uint32_t borda)
{
    g_ret_redondo(j, x, y, l, a, 10, fundo, fundo);
    g_contorno_redondo(j, x, y, l, a, 10, borda);
}

static void radio(int cx, int cy, bool sel)
{
    uint32_t c = sel ? COR_DESTAQUE : 0xB8C2CB;
    g_ret_redondo(j, cx - 10, cy - 10, 20, 20, 10, c, c);
    g_ret_redondo(j, cx - 8, cy - 8, 16, 16, 8, COR_BRANCO, COR_BRANCO);
    if (sel) g_ret_redondo(j, cx - 5, cy - 5, 10, 10, 5, COR_DESTAQUE, COR_DESTAQUE);
}

/* Texto quebrado em linhas de até l pixels; devolve o y depois dele. */
static int paragrafo(int x, int y, int l, const char *t, uint32_t cor)
{
    char linha[128];
    while (*t) {
        int n = 0, corte = 0;
        while (t[n] && t[n] != '\n' && n < (int)sizeof linha - 1) {
            linha[n] = t[n];
            linha[n + 1] = 0;
            if (g_largura_texto(linha) > l) break;
            if (t[n] == ' ') corte = n;
            n++;
        }
        if (t[n] && t[n] != '\n' && corte > 0) n = corte;
        linha[n] = 0;
        tx(x, y, linha, cor);
        y += 20;
        t += n;
        while (*t == ' ' || *t == '\n') t++;
    }
    return y;
}

static void desenhar_lado(void)
{
    if (!LADO) return;
    int antes = lim;
    lim = LADO - 8;
    g_retangulo(j, 0, 0, LADO, A, COR_LADO);
    g_retangulo(j, LADO - 1, 0, 1, A, COR_LINHA);
    g_imagem(j, 22, 22, arte_px_sobre_40, 40, 40);
    negrito(72, 26, "SaguiOS", COR_TEXTO);
    tx(72, 44, "Instalação", COR_TEXTO_FRACO);
    int atual = passo >= P_INSTALANDO ? 4 : passo;
    for (int k = 0; k < 5; k++) {
        int y = 104 + k * 44, cx = 34, cy = y + 8;
        bool feito = k < atual || passo == P_PRONTO, agora = k == atual && passo != P_PRONTO;
        if (agora) g_ret_redondo(j, 12, y - 8, LADO - 24, 34, 9, 0xD6E9DE, 0xD6E9DE);
        uint32_t cor = feito || agora ? COR_DESTAQUE : 0xB5BEC6;
        g_ret_redondo(j, cx - 11, cy - 11, 22, 22, 11, cor, cor);
        if (!feito && !agora) g_ret_redondo(j, cx - 9, cy - 9, 18, 18, 9, COR_LADO, COR_LADO);
        if (feito) {                                    /* o "certo" */
            for (int w = 0; w < 2; w++) {
                g_linha(j, cx - 5, cy + w, cx - 1, cy + 4 + w, COR_BRANCO);
                g_linha(j, cx - 1, cy + 4 + w, cx + 6, cy - 4 + w, COR_BRANCO);
            }
        } else {
            char n[4];
            formatar_texto(n, sizeof n, "%d", k + 1);
            tx(cx - g_largura_texto(n) / 2, cy - 8, n, agora ? COR_BRANCO : 0x8A949D);
        }
        if (agora) negrito(56, y, nomes_passos[k], COR_TEXTO);
        else tx(56, y, nomes_passos[k], feito ? COR_TEXTO : COR_TEXTO_FRACO);
    }
    lim = antes;
}

#define DISCO_Y(i) (86 + (i) * 86)

static void desenhar_disco_cartao(int i, bool sel)
{
    const info_disco_t *d = &discos[i];
    int y = DISCO_Y(i);
    bool eh_origem = i == origem;
    cartao(X0, y, CL, 74, eh_origem ? 0xF2F4F6 : sel ? COR_VERDE_CL : COR_CARTAO, sel ? COR_DESTAQUE : COR_LINHA);
    if (sel) g_contorno_redondo(j, X0 + 1, y + 1, CL - 2, 72, 9, COR_DESTAQUE);
    g_imagem(j, X0 + 16, y + 17, d->interno ? arte_px_disco_40 : arte_px_pendrive_40, 40, 40);
    char t[96], tam[24];
    int antes = lim;
    const char *e = CL >= 480 ? "O SaguiOS está ligado daqui" : "Origem";
    int we = g_largura_texto(e);
    lim = eh_origem ? X0 + CL - we - 42 : X0 + CL - 52;     /* antes da etiqueta ou do botão de rádio */
    tamanho_texto(d->setores, tam, sizeof tam);
    formatar_texto(t, sizeof t, "%s  -  %s", d->nome, d->interno ? "Disco interno" : "Pendrive USB");
    negrito(X0 + 72, y + 12, t, eh_origem ? COR_TEXTO_FRACO : COR_TEXTO);
    formatar_texto(t, sizeof t, "%s %s, %s", d->fabricante, d->produto, tam);
    tx(X0 + 72, y + 32, t, COR_TEXTO_FRACO);
    if (eh_origem) {
        lim = antes;
        g_ret_redondo(j, X0 + CL - we - 34, y + 24, we + 20, 26, 13, 0xE3E8EC, 0xE3E8EC);
        tx(X0 + CL - we - 24, y + 29, e, COR_TEXTO_FRACO);
    } else {
        formatar_texto(t, sizeof t, "Hoje: %s%s%s", d->formato, d->rotulo[0] ? ", " : "", d->rotulo);
        tx(X0 + 72, y + 52, t, 0x8A949D);
        radio(X0 + CL - 32, y + 37, sel);
    }
    lim = antes;
}

#define LINHA_A 28

/* As colunas da lista, proporcionais à largura. */
static int col(int k) { static const int pct[4] = { 0, 30, 62, 79 }; return X0 + 14 + (CL - 14) * pct[k] / 100; }

/* Texto numa coluna: corta antes da próxima. */
static void tx_col(int k, int y, const char *t, uint32_t cor, bool forte)
{
    int antes = lim;
    lim = k < 3 ? col(k + 1) - 8 : X0 + CL - 8;
    int x = k == 0 ? col(0) + 24 : col(k);
    if (forte) negrito(x, y, t, cor);
    else tx(x, y, t, cor);
    lim = antes;
}

static int largura_regiao(int r)
{
    /* Em MB, para caber em 32 bits (até 2 TB x 548 pixels). */
    uint32_t soma = total / MB ? total / MB : 1;
    int w = (int)(reg[r].n / MB * (uint32_t)(CL - 4) / soma);
    return w < 8 ? 8 : w;
}

static void desenhar_particoes(void)
{
    const info_disco_t *d = &discos[escolhido];
    char t[120], tam[24];
    tamanho_texto(d->setores, tam, sizeof tam);
    g_imagem(j, X0, 20, d->interno ? arte_px_disco_40 : arte_px_pendrive_40, 40, 40);
    formatar_texto(t, sizeof t, "Partições de %s", d->nome);
    negrito(X0 + 54, 22, t, COR_TEXTO);
    formatar_texto(t, sizeof t, "%s %s, %s. Nada é gravado antes do resumo.", d->fabricante, d->produto, tam);
    tx(X0 + 54, 42, t, COR_TEXTO_FRACO);

    /* A barra do disco: cada região do tamanho dela (some na janela baixa). */
    if (!compacto) {
    tx(X0, BARRA_Y - 24, "Clique numa parte da barra ou da lista para escolher.", 0x8A949D);
    g_ret_redondo(j, X0, BARRA_Y, CL, 40, 9, 0xE9EDF0, 0xE9EDF0);
    int x = X0 + 2;
    for (int r = 0; r < qtd_reg; r++) {
        int w = largura_regiao(r);
        if (x + w > X0 + CL - 2) w = X0 + CL - 2 - x;
        if (w <= 2) break;
        bool livre = reg[r].p < 0;
        uint32_t c = livre ? 0xF7F9FA : (parts[reg[r].p].saguios ? COR_DESTAQUE : cor_tipo(parts[reg[r].p].tipo));
        g_ret_redondo(j, x, BARRA_Y + 2, w - 2, 36, 7, c, livre ? c : (c & 0xFEFEFE) >> 1 | (c & 0xFEFEFE) >> 2);
        if (livre) g_contorno_redondo(j, x, BARRA_Y + 2, w - 2, 36, 7, 0xC2CAD2);
        if (!modo_inteiro && r == livre_inst) g_ret_redondo(j, x + 3, BARRA_Y + 5, w - 8, 30, 5, COR_VERDE_CL, 0xC9E8D4);
        if (r == sel_reg) {
            g_contorno_redondo(j, x - 1, BARRA_Y + 1, w, 38, 8, COR_TEXTO);
            g_contorno_redondo(j, x, BARRA_Y + 2, w - 2, 36, 7, COR_BRANCO);
        }
        tamanho_texto(reg[r].n, tam, sizeof tam);
        if (w > g_largura_texto(tam) + 12) {
            tx(x + (w - 2 - g_largura_texto(tam)) / 2, BARRA_Y + 12, tam, livre ? COR_TEXTO_FRACO : COR_BRANCO);
        }
        x += w;
    }
    if (modo_inteiro) {                         /* tudo vai sumir: um aviso por cima da barra */
        const char *av = CL >= 360 ? "tudo vai ser apagado" : "apagar tudo";
        int wa = g_largura_texto(av) + 20;
        g_ret_redondo(j, X0 + CL - wa - 8, BARRA_Y + 8, wa, 24, 12, 0xFDECEA, 0xFDECEA);
        tx(X0 + CL - wa + 2, BARRA_Y + 12, av, COR_PERIGO);
    }
    }

    /* A lista. */
    static const char *const cab[4] = { "Partição", "Tipo", "Tamanho", "Rótulo" };
    for (int k = 0; k < 4; k++) {
        int antes = lim;
        lim = k < 3 ? col(k + 1) - 8 : X0 + CL - 8;
        tx(col(k), LISTA_Y - 22, cab[k], COR_TEXTO_FRACO);
        lim = antes;
    }
    cartao(X0, LISTA_Y - 4, CL, LINHAS * LINHA_A + 8, COR_CARTAO, COR_LINHA);
    if (gpt) paragrafo(X0 + 16, LISTA_Y + 10, CL - 32,
                       "Este disco usa a tabela GPT, que o instalador ainda não entende. Dá para instalar apagando o disco inteiro.",
                       COR_TEXTO);
    if (sel_reg >= 0 && sel_reg < topo) topo = sel_reg;
    if (sel_reg >= topo + LINHAS) topo = sel_reg - LINHAS + 1;
    if (topo > qtd_reg - LINHAS) topo = qtd_reg - LINHAS > 0 ? qtd_reg - LINHAS : 0;
    for (int r = topo; r < qtd_reg && r < topo + LINHAS && !gpt; r++) {
        int y = LISTA_Y + (r - topo) * LINHA_A;
        if (r == sel_reg) g_ret_redondo(j, X0 + 4, y, CL - 8, LINHA_A, 7, 0xDCE8F8, 0xDCE8F8);
        bool livre = reg[r].p < 0;
        uint32_t c = livre ? 0xF7F9FA : (parts[reg[r].p].saguios ? COR_DESTAQUE : cor_tipo(parts[reg[r].p].tipo));
        g_ret_redondo(j, col(0), y + 7, 14, 14, 4, c, c);
        if (livre) g_contorno_redondo(j, col(0), y + 7, 14, 14, 4, 0xB8C2CB);
        tamanho_texto(reg[r].n, tam, sizeof tam);
        if (livre) {
            tx_col(0, y + 6, "Espaço livre", COR_TEXTO_FRACO, false);
            tx_col(2, y + 6, tam, COR_TEXTO_FRACO, false);
            if (!modo_inteiro && r == livre_inst) tx_col(3, y + 6, "SaguiOS aqui", COR_DESTAQUE, true);
            continue;
        }
        const part_t *p = &parts[reg[r].p];
        formatar_texto(t, sizeof t, "Partição %u%s", reg[r].p + 1, p->nova ? " (nova)" : "");
        tx_col(0, y + 6, t, COR_TEXTO, false);
        if (p->saguios) copia_texto(t, "SaguiOS (formatar)", sizeof t);
        else formatar_texto(t, sizeof t, "%s%s", nome_tipo(p->tipo), p->formatar && !p->nova ? " (formatar)" : "");
        tx_col(1, y + 6, t, (p->formatar && !p->nova) || p->saguios ? COR_PERIGO : COR_TEXTO, false);
        tx_col(2, y + 6, tam, COR_TEXTO, false);
        if (p->saguios && !modo_inteiro && r == livre_inst) tx_col(3, y + 6, "SaguiOS aqui", COR_DESTAQUE, true);
        else tx_col(3, y + 6, p->rotulo, COR_TEXTO_FRACO, false);
    }
    if (!qtd_reg && !gpt) tx(X0 + 16, LISTA_Y + 6, "O disco não tem espaço utilizável.", COR_TEXTO_FRACO);
    if (qtd_reg > LINHAS && !gpt) {             /* tem mais: as setas rolam */
        char m[32];
        formatar_texto(m, sizeof m, "%d de %d (setas)", topo + 1, qtd_reg);
        tx(X0 + CL - g_largura_texto(m) - 4, LISTA_Y - 22, m, COR_SELECAO);
    }

    /* Onde instalar. */
    int oy = LISTA_Y + LINHAS * LINHA_A + (compacto ? 60 : 84);
    if (!compacto) negrito(X0, oy - 24, "Onde instalar o SaguiOS", COR_TEXTO);
    int antes = lim;
    const char *mot = motivo_livre();
    for (int k = 0; k < 2; k++) {
        bool sel = k == 0 ? modo_inteiro : !modo_inteiro && livre_inst >= 0;
        bool ok = k == 0 || !mot || (!modo_inteiro && livre_inst >= 0);
        int bx = modos_empilhados ? X0 : X0 + k * (CL / 2 + 6), bl = modos_empilhados ? CL : CL / 2 - 6;
        int by = modos_empilhados ? oy + k * 62 : oy;
        lim = bx + bl - 10;
        cartao(bx, by, bl, 56, sel ? COR_VERDE_CL : COR_CARTAO, sel ? COR_DESTAQUE : COR_LINHA);
        radio(bx + 22, by + 19, sel);
        bool na_part = livre_inst >= 0 && reg[livre_inst].p >= 0;
        negrito(bx + 42, by + 10, k == 0 ? "Apagar o disco e instalar" : na_part ? "Na partição formatada" : "No espaço livre",
                ok ? COR_TEXTO : 0xA5AEB6);
        const char *sub = k == 0 ? "Todas as partições somem." : ok ? "As outras partições ficam." : mot;
        tx(bx + 42, by + 30, sub, k == 0 ? COR_PERIGO : ok ? COR_TEXTO_FRACO : 0xA5AEB6);
        bts[k == 0 ? B_MODO_INTEIRO : B_MODO_LIVRE] = (bt_t){ bx, by, bl, 56, "", false, true, false };
    }
    lim = antes;
}

static int pct_atual = -1;
static char etapa_atual[64] = "Preparando";

#define PROG_Y 150

static void desenhar_progresso_area(void)
{
    g_retangulo(j, X0, PROG_Y - 4, CL, 64, COR_FUNDO);
    tx(X0, PROG_Y, etapa_atual, COR_TEXTO);
    char t[8];
    formatar_texto(t, sizeof t, "%d%%", pct_atual < 0 ? 0 : pct_atual);
    tx(X0 + CL - g_largura_texto(t), PROG_Y, t, COR_TEXTO_FRACO);
    g_ret_redondo(j, X0, PROG_Y + 28, CL, 16, 8, 0xDDE3E8, 0xDDE3E8);
    int w = pct_atual <= 0 ? 0 : CL * pct_atual / 100;
    if (w >= 16) g_ret_redondo(j, X0, PROG_Y + 28, w, 16, 8, 0x2BB463, COR_DESTAQUE);
}

static void progresso_janela(int pct, const char *etapa)
{
    if (pct == pct_atual && compara_texto(etapa, etapa_atual) == 0) return;
    pct_atual = pct;
    copia_texto(etapa_atual, etapa, sizeof etapa_atual);
    desenhar_progresso_area();
    jan_mostrar_area(j, X0, PROG_Y - 4, CL, 64);
}

static bool pode_continuar(void)
{
    switch (passo) {
    case P_BOAS_VINDAS: return origem >= 0 && sis_ok;
    case P_DISCO:       return escolhido >= 0 && escolhido != origem;
    case P_PARTICOES:   return modo_inteiro || livre_inst >= 0;
    case P_RESUMO:      return true;
    default:            return false;
    }
}

static void desenhar_resumo(void)
{
    const info_disco_t *d = &discos[escolhido];
    char t[160], tam[24];
    negrito(X0, 22, "Tudo pronto para instalar", COR_TEXTO);
    tx(X0, 42, "Confira. Ao clicar em Instalar, isto é gravado no disco:", COR_TEXTO_FRACO);
    /* O aviso vai colado no rodapé, com a altura do texto; a lista usa o
     * que sobra entre os dois. */
    const char *aviso = modo_inteiro
        ? "Tudo o que está no disco hoje se perde. Confira se é o disco certo antes de continuar."
        : "O disco passa a ligar o SaguiOS. Se outro sistema (o Windows, por exemplo) ligava por ele, deixa de ligar sozinho.";
    int por_linha = (CL - 36) / 8, linhas_aviso = ((int)tam_texto(aviso) + por_linha - 1) / por_linha;
    if (linhas_aviso < 1) linhas_aviso = 1;
    int ah = (compacto ? 24 : 44) + linhas_aviso * 20, ay = RODAPE_Y - 14 - 12 - ah;
    int fim_lista = ay - 12;
    cartao(X0, 72, CL, fim_lista - 72, COR_CARTAO, COR_LINHA);
    int antes = lim;
    lim = X0 + CL - 14;
    int y = 154, passo_linha = compacto ? 20 : 22;
    tamanho_texto(d->setores, tam, sizeof tam);
    formatar_texto(t, sizeof t, "%s  -  %s %s, %s", d->nome, d->fabricante, d->produto, tam);
    if (compacto) {                              /* sem o ícone: só o nome, numa linha */
        negrito(X0 + 16, 84, t, COR_TEXTO);
        y = 110;
    } else {
        g_imagem(j, X0 + 16, 86, d->interno ? arte_px_disco_40 : arte_px_pendrive_40, 40, 40);
        negrito(X0 + 70, 90, t, COR_TEXTO);
        tx(X0 + 70, 110, d->interno ? "Disco interno" : "Pendrive USB", COR_TEXTO_FRACO);
        g_retangulo(j, X0 + 16, 140, CL - 32, 1, COR_LINHA);
    }
    if (modo_inteiro) {
        if (y + 18 <= fim_lista) tx(X0 + 22, y, "- Apaga todas as partições do disco.", COR_PERIGO);
        y += passo_linha;
    } else {
        for (int k = 0; k < 4; k++) {
            const part_t *p = &parts[k];
            if (!p->tipo) continue;
            uint32_t c = COR_PERIGO;
            if (p->saguios && !p->nova) formatar_texto(t, sizeof t, "- Formata a partição %d (%s) para o SaguiOS: o que há nela se perde.", k + 1, nome_tipo(p->tipo));
            else if (p->excluida) formatar_texto(t, sizeof t, "- Exclui a partição %d (%s).", k + 1, nome_tipo(p->tipo));
            else if (p->nova) { formatar_texto(t, sizeof t, "- Cria a partição %d: FAT32 \"%s\", %u MB.", k + 1, p->rotulo, p->n / MB); c = COR_TEXTO; }
            else if (p->formatar) formatar_texto(t, sizeof t, "- Formata a partição %d como FAT32 (o que há nela se perde).", k + 1);
            else continue;
            if (y + 18 <= fim_lista) tx(X0 + 22, y, t, c);
            y += passo_linha;
        }
    }
    uint32_t livre_n = modo_inteiro ? (total - MB) / MB * MB : reg[livre_inst].n;
    formatar_texto(t, sizeof t, "- Copia o SaguiOS para uma partição de %u MB.", sistema_mb() / MB);
    if (y + 18 <= fim_lista) tx(X0 + 22, y, t, COR_TEXTO);
    y += passo_linha;
    formatar_texto(t, sizeof t, "- Cria a partição dos seus arquivos: FAT32 \"SAGUIOS\", %u MB.", (livre_n - sistema_mb()) / MB);
    if (y + 18 <= fim_lista) tx(X0 + 22, y, t, COR_TEXTO);
    y += passo_linha;
    if (y + 18 <= fim_lista) tx(X0 + 22, y, "- Grava o setor de boot do SaguiOS no começo do disco.", COR_TEXTO);
    lim = antes;
    cartao(X0, ay, CL, ah, COR_AVISO, 0xE8CF8A);
    if (!compacto) negrito(X0 + 18, ay + 12, "Atenção", 0x8A5A00);   /* na janela baixa, sem o título */
    paragrafo(X0 + 18, ay + (compacto ? 12 : 34), CL - 36, aviso, COR_TEXTO);
}

static void desenhar(void)
{
    memset(bts, 0, sizeof bts);
    lim = X0 + CL;
    g_retangulo(j, 0, 0, L, A, COR_FUNDO);
    desenhar_lado();
    char t[160];
    switch (passo) {
    case P_BOAS_VINDAS: {
        g_imagem(j, X0, 26, arte_px_instalar_sistema, 64, 64);
        negrito(X0 + 82, 40, "Instalar o SaguiOS", COR_TEXTO);
        tx(X0 + 82, 62, "Leve o sistema para um disco e guarde os seus arquivos nele.", COR_TEXTO_FRACO);
        int y = paragrafo(X0, 120, CL,
            "O instalador copia o SaguiOS para o disco que você escolher e cria uma partição para os seus "
            "arquivos. Depois, o computador liga pelo disco, sem o pendrive, e o que você guardar continua lá.", COR_TEXTO);
        if (!compacto) y = paragrafo(X0, y + 10, CL,
            "Você escolhe o disco, vê as partições dele e decide se apaga tudo ou usa um espaço livre. "
            "Nada é gravado antes do último passo.", COR_TEXTO);
        int cy = y + 22;
        if (origem >= 0) {
            const info_disco_t *o = &discos[origem];
            char tam[24];
            cartao(X0, cy, CL, 64, COR_CARTAO, COR_LINHA);
            g_imagem(j, X0 + 14, cy + 12, o->interno ? arte_px_disco_40 : arte_px_pendrive_40, 40, 40);
            tx(X0 + 68, cy + 12, "O SaguiOS será copiado de", COR_TEXTO_FRACO);
            tamanho_texto(o->setores, tam, sizeof tam);
            formatar_texto(t, sizeof t, "%s  -  %s, %s %s, %s", o->nome, o->interno ? "disco interno" : "pendrive USB", o->fabricante, o->produto, tam);
            negrito(X0 + 68, cy + 32, t, COR_TEXTO);
            if (!sis_ok) tx(X0, cy + 78, erro, COR_PERIGO);
        } else {
            cartao(X0, cy, CL, 64, 0xFDECEA, 0xEBB3AE);
            negrito(X0 + 18, cy + 12, "Não achei o disco de onde o SaguiOS ligou.", COR_PERIGO);
            tx(X0 + 18, cy + 34, "Sem ele, não há de onde copiar o sistema.", COR_TEXTO_FRACO);
        }
        break;
    }
    case P_DISCO: {
        negrito(X0, 22, "Onde instalar?", COR_TEXTO);
        tx(X0, 42, "Escolha o disco. No próximo passo você vê as partições dele.", COR_TEXTO_FRACO);
        int outros = 0;
        for (int i = 0; i < qtd_discos; i++) {
            desenhar_disco_cartao(i, i == escolhido);
            if (i != origem) outros++;
        }
        if (!outros) {
            int y = DISCO_Y(qtd_discos) + 4;
            cartao(X0, y, CL, 58, COR_AVISO, 0xE8CF8A);
            negrito(X0 + 18, y + 10, "Nenhum outro disco para instalar.", COR_TEXTO);
            tx(X0 + 18, y + 30, "Conecte um disco rígido (IDE/SATA) ou um pendrive e abra de novo.", COR_TEXTO_FRACO);
        }
        break;
    }
    case P_PARTICOES: {
        desenhar_particoes();
        bool parte = sel_reg >= 0 && reg[sel_reg].p >= 0, livre = sel_reg >= 0 && reg[sel_reg].p < 0;
        int by = LISTA_Y + LINHAS * LINHA_A + 14;
        int bw = (CL - 3 * 8) / 4;                /* quatro botões iguais na largura */
        bool curto = bw < 128;
        bt(B_EXCLUIR, X0, by, bw, "Excluir", false, parte && !gpt);
        bt(B_NOVA, X0 + (bw + 8), by, bw, curto ? "Nova" : "Nova partição", false, livre && !gpt && entradas_livres() > 0);
        bt(B_FORMATAR, X0 + 2 * (bw + 8), by, bw, "Formatar", false, parte && !gpt);
        bt(B_TUDO, X0 + 3 * (bw + 8), by, CL - 3 * (bw + 8), curto ? "Apagar" : "Apagar tudo", false, !modo_inteiro || gpt);
        break;
    }
    case P_RESUMO:
        desenhar_resumo();
        break;
    case P_INSTALANDO:
        g_imagem(j, X0, 26, arte_px_instalar_sistema, 64, 64);
        negrito(X0 + 82, 40, "Instalando o SaguiOS", COR_TEXTO);
        formatar_texto(t, sizeof t, "em %s. Não desligue o computador.", discos[escolhido].nome);
        tx(X0 + 82, 62, t, COR_TEXTO_FRACO);
        desenhar_progresso_area();
        break;
    case P_PRONTO: {
        int cx = X0 + 32, cy = 58;
        g_ret_redondo(j, cx - 32, cy - 32, 64, 64, 32, 0x2BB463, COR_DESTAQUE);
        for (int w = -2; w <= 2; w++) {
            g_linha(j, cx - 14, cy + w, cx - 4, cy + 10 + w, COR_BRANCO);
            g_linha(j, cx - 4, cy + 10 + w, cx + 16, cy - 10 + w, COR_BRANCO);
        }
        negrito(X0 + 82, 40, "O SaguiOS foi instalado", COR_TEXTO);
        formatar_texto(t, sizeof t, "no disco %s.", discos[escolhido].nome);
        tx(X0 + 82, 62, t, COR_TEXTO_FRACO);
        paragrafo(X0, 124, CL,
            "Desligue o computador, tire o pendrive de instalação e ligue de novo: o SaguiOS sobe pelo disco. "
            "Os seus arquivos ficam na partição SAGUIOS, que aparece como Disco no Arquivos.", COR_TEXTO);
        break;
    }
    case P_ERRO:
        cartao(X0, 40, CL, 120, 0xFDECEA, 0xEBB3AE);
        negrito(X0 + 18, 56, "Não deu para instalar", COR_PERIGO);
        paragrafo(X0 + 18, 82, CL - 36, erro, COR_TEXTO);
        break;
    }

    /* O rodapé. */
    g_retangulo(j, LADO, RODAPE_Y - 14, L - LADO, 1, COR_LINHA);
    int dir = X0 + CL;                            /* a borda direita dos botões */
    if (passo == P_PRONTO) {
        bt(B_CANCELAR, dir - 170 - 10 - 110, RODAPE_Y, 110, "Fechar", false, true);
        bt(B_REINICIAR, dir - 170, RODAPE_Y, 170, "Reiniciar agora", true, true);
    } else if (passo == P_ERRO) {
        bt(B_CANCELAR, dir - 120, RODAPE_Y, 120, "Fechar", true, true);
    } else if (passo != P_INSTALANDO) {
        bt(B_CANCELAR, X0, RODAPE_Y, 110, "Cancelar", false, true);
        if (passo > P_BOAS_VINDAS) bt(B_VOLTAR, dir - 140 - 10 - 110, RODAPE_Y, 110, "Voltar", false, true);
        bt(B_CONTINUAR, dir - 140, RODAPE_Y, 140, passo == P_RESUMO ? "Instalar" : "Continuar", true, pode_continuar());
        if (passo == P_RESUMO) bts[B_CONTINUAR].perigo = true;
    }
    for (int i = 0; i < QTD_BT; i++) if (bts[i].texto && bts[i].texto[0]) desenhar_bt(&bts[i]);
    jan_mostrar(j);
}

static void ir(int p)
{
    passo = p;
    registrar("instalar: passo %d (%s)", p, p < 5 ? nomes_passos[p] : p == P_PRONTO ? "pronto" : "erro");
    desenhar();
}

/* ------------------------------------------------------------ as ações do passo das partições */

static void excluir(void)
{
    part_t *p = &parts[reg[sel_reg].p];
    registrar("instalar: excluir %s%d", discos[escolhido].nome, reg[sel_reg].p + 1);
    if (p->nova) memset(p, 0, sizeof *p);
    else p->excluida = true;
    montar_regioes();
    modo_inteiro = false;
    livre_inst = -1;
}

static void nova(void)
{
    regiao_t r = reg[sel_reg];
    uint32_t max = r.n / MB;
    char resp[16], perg[96];
    formatar_texto(perg, sizeof perg, "Tamanho da partição FAT32 em MB (de 33 a %u):", max);
    formatar_texto(resp, sizeof resp, "%u", max);
    if (!pedir_texto("Nova partição", perg, resp, sizeof resp)) return;
    uint32_t mb = 0;
    for (const char *c = resp; *c >= '0' && *c <= '9'; c++) mb = mb * 10 + (uint32_t)(*c - '0');
    if (mb < 33 || mb > max) { mensagem("Nova partição", "Tamanho fora do limite."); return; }
    for (int k = 0; k < 4; k++) {
        part_t *p = &parts[k];
        if (p->tipo && !p->excluida) continue;
        if (p->excluida) {
            /* A entrada era de uma partição excluída: a exclusão continua no
             * resumo, guardada numa entrada que sobrou, se houver. */
            int livre = -1;
            for (int q = 0; q < 4; q++) if (q != k && !parts[q].tipo) livre = q;
            if (livre >= 0) parts[livre] = *p;
        }
        memset(p, 0, sizeof *p);
        p->tipo = 0x0C;
        p->ini = r.ini;
        p->n = mb * MB;
        p->nova = p->formatar = true;
        copia_texto(p->rotulo, "DADOS", sizeof p->rotulo);
        registrar("instalar: nova FAT32 em %s%d, %u MB", discos[escolhido].nome, k + 1, mb);
        break;
    }
    montar_regioes();
    modo_inteiro = false;
    livre_inst = -1;
}

static void formatar(void)
{
    int r = sel_reg;
    part_t *p = &parts[reg[r].p];
    char perg[160];
    formatar_texto(perg, sizeof perg, "Formatar a partição %d para o SaguiOS? O sistema e os seus arquivos vão nela, "
                   "e o que há nela hoje se perde (só ao instalar).", reg[r].p + 1);
    if (!confirmar("Formatar para o SaguiOS", perg)) return;
    for (int k = 0; k < 4; k++) parts[k].saguios = false;   /* uma só */
    p->saguios = true;
    modo_inteiro = false;
    livre_inst = !motivo_livre() ? r : -1;
    registrar("instalar: formatar %s%d para o SaguiOS (%s)", discos[escolhido].nome, reg[r].p + 1,
              livre_inst >= 0 ? "instala nela" : motivo_livre());
    if (livre_inst < 0) mensagem("Formatar para o SaguiOS", motivo_livre());
}

static void apagar_tudo(void)
{
    for (int k = 0; k < 4; k++) {
        if (parts[k].nova) memset(&parts[k], 0, sizeof parts[k]);
        else if (parts[k].tipo) parts[k].excluida = true;
    }
    montar_regioes();
    sel_reg = qtd_reg ? 0 : -1;
    modo_inteiro = true;
    livre_inst = -1;
    registrar("instalar: apagar tudo em %s", discos[escolhido].nome);
}

static int regiao_em(int x, int y)
{
    if (x < X0 || x >= X0 + CL || gpt) return -1;
    if (y >= LISTA_Y && y < LISTA_Y + LINHAS * LINHA_A) {
        int r = (y - LISTA_Y) / LINHA_A + topo;
        return r < qtd_reg ? r : -1;
    }
    if (!compacto && y >= BARRA_Y && y < BARRA_Y + 40) {
        int px = X0 + 2;
        for (int r = 0; r < qtd_reg; r++) {
            int w = largura_regiao(r);
            if (x >= px && x < px + w) return r;
            px += w;
        }
    }
    return -1;
}

/* ------------------------------------------------------------ os cliques */

static bool terminar;

static void continuar(void)
{
    if (passo == P_PRONTO || passo == P_ERRO) { terminar = true; return; }   /* Enter na tela final fecha */
    if (!pode_continuar()) return;
    switch (passo) {
    case P_BOAS_VINDAS:
        ir(P_DISCO);
        break;
    case P_DISCO: {
        /* Ver as partições já é ler o disco: o kernel pede o nome dele antes. */
        const info_disco_t *d = &discos[escolhido];
        if (d->montado) desmontar(d->nome);
        int r = disco_liberar(d->nome);
        registrar("instalar: liberar %s -> %d", d->nome, r);
        if (r < 0) {
            if (r != ERRO_ACESSO) mensagem("Instalar", nome_erro(r));
            break;
        }
        if (ler_tabela() < 0) { ir(P_ERRO); break; }
        ir(P_PARTICOES);
        break;
    }
    case P_PARTICOES:
        ir(P_RESUMO);
        break;
    case P_RESUMO:
        avisar_progresso = progresso_janela;
        ir(P_INSTALANDO);
        if (instalar() < 0) { registrar("instalar: erro: %s", erro); ir(P_ERRO); }
        else ir(P_PRONTO);
        break;
    case P_PRONTO:
    case P_ERRO:
        terminar = true;
        break;
    }
}

static void clique(int x, int y)
{
    if (passo == P_DISCO)
        for (int i = 0; i < qtd_discos; i++) {
            int cy = DISCO_Y(i);
            if (y >= cy && y < cy + 74 && x >= X0 && x < X0 + CL && i != origem) {
                escolhido = i;
                registrar("instalar: disco %s escolhido", discos[i].nome);
                desenhar();
                return;
            }
        }
    if (passo == P_PARTICOES) {
        int r = regiao_em(x, y);
        if (r >= 0) {
            sel_reg = r;
            if (!modo_inteiro) livre_inst = !motivo_livre() ? r : -1;
            registrar("instalar: regiao %d (%s)", r, reg[r].p < 0 ? "livre" : nome_tipo(parts[reg[r].p].tipo));
            desenhar();
            return;
        }
    }
    switch (bt_em(x, y)) {
    case B_CONTINUAR: continuar(); break;
    case B_VOLTAR:    ir(passo - 1); break;
    case B_CANCELAR:  terminar = true; break;
    case B_REINICIAR:
        registrar("instalar: reiniciar");
        jan_fechar(j);
        reiniciar();
        break;
    case B_EXCLUIR:   excluir(); desenhar(); break;
    case B_NOVA:      nova(); desenhar(); break;
    case B_FORMATAR:  formatar(); desenhar(); break;
    case B_TUDO:      apagar_tudo(); desenhar(); break;
    case B_MODO_INTEIRO:
        modo_inteiro = true;
        livre_inst = -1;
        registrar("instalar: modo disco inteiro");
        desenhar();
        break;
    case B_MODO_LIVRE:
        if (motivo_livre()) break;
        modo_inteiro = false;
        livre_inst = sel_reg;
        registrar("instalar: modo espaço livre (região %d)", sel_reg);
        desenhar();
        break;
    }
}

int principal(int argc, char **argv)
{
    (void)argv;
    if (argc >= 2) { imprimir("uso: instalar   (abre a janela; não há opções)\n"); return 1; }
    buf = aloca(POR_VEZ * SETOR);
    if (!buf) return 1;
    /* A janela nasce do tamanho da tela (até 820 x 560) e pode mudar de
     * tamanho; tudo se arranja de novo em medir(). */
    info_versao_t v;
    memset(&v, 0, sizeof v);
    if (info_sistema(INFO_VERSAO, &v, sizeof v) >= 0 && v.largura && v.altura) {
        if ((int)v.largura - 24 < L) L = (int)v.largura - 24;
        /* Entre a barra de cima (24) e a doca (62), com o título (24) e a borda. */
        if ((int)v.altura - 116 < A) A = (int)v.altura - 116;
    }
    j = jan_abrir("Instalar o SaguiOS", L, A);
    if (!j) return 1;
    jan_redimensionavel(j, 480, 400);
    medir();
    ler_discos();
    sis_ok = ler_sistema();
    /* Já deixa escolhido o primeiro disco que não é a origem. */
    int outros = 0;
    for (int i = 0; i < qtd_discos; i++)
        if (i != origem) { outros++; if (escolhido < 0) escolhido = i; }
    registrar("instalar: janela aberta (%d discos)", outros);
    ir(P_BOAS_VINDAS);
    evento_t e;
    while (!terminar) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) { if (passo != P_INSTALANDO) break; continue; }
        if (e.tipo == EV_REDIMENSIONAR) {
            jan_novo_tamanho(j, &e);
            medir();
            desenhar();
        } else if (e.tipo == EV_CLIQUE) clique(e.x, e.y);
        else if (e.tipo == EV_TECLA) {
            if (e.tecla == '\n') continuar();
            else if (passo == P_PARTICOES && (e.tecla == TECLA_CIMA || e.tecla == TECLA_BAIXO) && qtd_reg) {
                int r = sel_reg + (e.tecla == TECLA_BAIXO ? 1 : -1);
                if (r < 0) r = 0;
                if (r >= qtd_reg) r = qtd_reg - 1;
                sel_reg = r;
                if (!modo_inteiro) livre_inst = !motivo_livre() ? r : -1;
                desenhar();
            }
            else if (e.tecla == TECLA_ESC && passo != P_INSTALANDO) break;
        }
    }
    jan_fechar(j);
    return 0;
}
