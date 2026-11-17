/* instalar - o instalador do SaguiOS: um assistente em passos (boas-vindas,
 * disco, resumo, instalação), como os de outros sistemas.
 *
 * Ele instala de dois jeitos (D067):
 *   - no espaço livre: as partições que já estão no disco ficam como estão,
 *     byte a byte, e o SaguiOS vai para a maior região sem partição. Só é
 *     oferecido com uma tabela MBR conferida pelo kernel (D066) e duas
 *     entradas vazias;
 *   - apagando o disco inteiro.
 * O que fica no disco escolhido:
 *   - o código de boot do SaguiOS no setor 0, com o LBA do cabeçalho de boot
 *     (0x1B4); no espaço livre, o resto do setor (o identificador e as
 *     entradas de hoje) fica como estava, e o setor 0 antigo é guardado em
 *     /usb/.saguios/mbr-antes.bin;
 *   - a partição do sistema (tipo 0xDA): o cabeçalho de boot no primeiro
 *     setor dela e, depois, o estágio 2, o kernel e o ramdisk copiados do
 *     disco de onde o SaguiOS ligou. O setor 1 do disco não é usado: nele
 *     costuma estar o carregador de outro sistema (o core.img do GRUB);
 *   - uma partição FAT32 (tipo 0x0C) com o rótulo SAGUIOS e o arquivo de
 *     configurações: no boot pelo disco, o kernel a monta em /usb sozinho, e
 *     o que você guardar ali continua depois de desligar.
 *
 * Nada é lido nem gravado no disco escolhido antes do botão "Instalar" no
 * resumo. É nele que o kernel pede para a pessoa digitar o nome do disco,
 * numa caixa desenhada pelo próprio kernel, com o resumo do que vai ser
 * feito (SIS_DISCO_LIBERAR, D063). O botão Instalar não responde ao Enter.
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
#define GPT_FIM    33                     /* a cópia da GPT no fim do disco: cabeçalho e 32 setores de entradas */
#define MBR_CAB    0x1B4                  /* no MBR: o LBA do cabeçalho de boot (boot/mbr.asm) */

static uint8_t *buf, *volta;              /* POR_VEZ setores cada */
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

/* Grava e lê de volta, conferindo byte a byte. */
static int gravar_conferindo(const char *disco, uint32_t lba, uint32_t n, const void *b)
{
    if (setores(disco, lba, n, (void *)b, true) < 0 || setores(disco, lba, n, volta, false) < 0) return -1;
    if (memcmp(b, volta, n * SETOR) != 0) {
        formatar_texto(erro, sizeof erro, "%s: o que foi lido no setor %u não é o que foi gravado", disco, lba);
        return -1;
    }
    return 0;
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
    if (gravar_conferindo(disco, ini, 1, s) < 0 || gravar_conferindo(disco, ini + 6, 1, s) < 0) return -1;
    if (gravar_conferindo(disco, ini + 1, 1, fsinfo) < 0 || gravar_conferindo(disco, ini + 7, 1, fsinfo) < 0) return -1;
    uint8_t fat[SETOR];
    memset(fat, 0, sizeof fat);
    poe32(fat, 0x0FFFFFF8);
    poe32(fat + 4, 0x0FFFFFFF);
    poe32(fat + 8, 0x0FFFFFFF);                      /* a raiz: um cluster só */
    for (uint32_t f = 0; f < nfats; f++)
        if (gravar_conferindo(disco, ini + reservados + f * fatsz, 1, fat) < 0) return -1;
    uint8_t raiz[SETOR];
    memset(raiz, 0, sizeof raiz);
    memcpy(raiz, nome11, 11);
    raiz[11] = 0x08;                                 /* rótulo do volume */
    return gravar_conferindo(disco, ini + reservados + nfats * fatsz, 1, raiz);
}

/* ------------------------------------------------------------ os discos e o sistema a copiar */

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

typedef struct {
    uint8_t  cab[SETOR];                  /* o cabeçalho de boot (setor 1) da origem */
    uint8_t  mbr[SETOR];                  /* o MBR da origem: o código de boot vem dele */
    uint32_t ini, n;                      /* setores do sistema na origem (estágio 2, kernel, ramdisk) */
} sistema_t;
static sistema_t sis;
static bool sis_ok;

/* O cabeçalho (boot/cabecalho.inc): 'SGHD', LBA do estágio 2, do kernel e
 * do ramdisk, e o fim da imagem (CAB_IMG_SETORES). */
#define CAB_E2_LBA  8
#define CAB_K_LBA   16
#define CAB_R_LBA   32
#define CAB_IMG_SET 48

/* O kernel deixa ler da origem, sem confirmação, só os setores 0 e 1 e a
 * partição do sistema (D063). */
static bool ler_sistema(void)
{
    if (origem < 0) return false;
    const char *o = discos[origem].nome;
    if (setores(o, 0, 1, sis.mbr, false) < 0) return false;
    uint32_t cab_lba = le32(sis.mbr + MBR_CAB);                /* 1 no pendrive; dentro da partição 0xDA no disco instalado */
    if (!cab_lba || cab_lba >= discos[origem].setores || setores(o, cab_lba, 1, sis.cab, false) < 0) {
        copia_texto(erro, "não deu para ler o cabeçalho de boot da origem", sizeof erro);
        return false;
    }
    if (le32(sis.cab) != 0x44484753) { copia_texto(erro, "o cabeçalho de boot da origem não é do SaguiOS", sizeof erro); return false; }
    sis.ini = le32(sis.cab + CAB_E2_LBA);
    uint32_t fim = le32(sis.cab + CAB_IMG_SET);
    if (sis.ini < 2 || fim <= sis.ini) { copia_texto(erro, "o cabeçalho de boot da origem está estranho", sizeof erro); return false; }
    sis.n = fim - sis.ini;
    return true;
}

static uint32_t sistema_mb(void) { return acima_mb(sis.n + 1); }   /* o cabeçalho e o sistema, em setores, arredondado em MB */

/* Onde vai cada coisa no disco escolhido. */
static uint32_t s_ini, s_n, d_ini, d_n;

enum { MODO_INTEIRO, MODO_LIVRE, MODO_EDITAR };
static int modo = MODO_INTEIRO;
static tabela_disco_t tab;                /* o que o kernel leu da tabela do disco escolhido (D066) */
static bool tab_ok;
static uint32_t livre_ini, livre_n;       /* a maior região sem partição, alinhada em MB */
static int vagas[4], qtd_vagas;           /* as entradas vazias da tabela */
static char livre_motivo[120];            /* por que não dá (com as exclusões, no editor) */
static bool pode_livre;                   /* o espaço livre de hoje, sem excluir nada, basta */
static char motivo_livre[120];            /* ...e, se não basta, por quê */
static uint32_t excluir;                  /* no editor: as primárias (índices em tab.p) que vão ser excluídas */

static bool eh_estendida(uint32_t tipo) { return tipo == 0x05 || tipo == 0x0F || tipo == 0x85; }

/* Uma partição sai junto: a lógica, com a estendida dela. */
static bool excluida(uint32_t i)
{
    if (tab.p[i].entrada <= 4) return (excluir >> i) & 1;
    for (uint32_t k = 0; k < tab.n; k++)
        if (eh_estendida(tab.p[k].tipo) && tab.p[k].entrada <= 4) return (excluir >> k) & 1;
    return false;
}

/* A maior região livre. As primárias (a estendida conta inteira, com as
 * lógicas dentro) são percorridas pelo início, e a região livre começa no
 * MAIOR fim visto até ali, não no fim da última: com uma partição dentro
 * de outra, o fim da menor cai no meio da maior (B2 da revisão do #53). */
static bool achar_livre(void)
{
    uint32_t excl = modo == MODO_EDITAR ? excluir : 0;    /* no editor, o lugar das excluídas conta como livre */
    livre_ini = livre_n = 0;
    qtd_vagas = 0;
    livre_motivo[0] = 0;
    if (!tab_ok) { copia_texto(livre_motivo, "não deu para ler a tabela de partições", sizeof livre_motivo); return false; }
    if (tab.estado == TABELA_VAZIO) { copia_texto(livre_motivo, "o disco não tem partições: use o disco inteiro", sizeof livre_motivo); return false; }
    if (tab.estado != TABELA_MBR || !tab.conferida) {
        formatar_texto(livre_motivo, sizeof livre_motivo, "%s", tab.problema[0] ? tab.problema : "a tabela não é MBR");
        return false;
    }
    bool usada[4] = { false, false, false, false };
    uint32_t ini[4], fim[4];
    int n = 0;
    for (uint32_t i = 0; i < tab.n; i++) {
        const particao_t *p = &tab.p[i];
        if (p->entrada < 1 || p->entrada > 4 || ((excl >> i) & 1)) continue;
        usada[p->entrada - 1] = true;
        int k = n++;
        while (k > 0 && ini[k - 1] > p->inicio) { ini[k] = ini[k - 1]; fim[k] = fim[k - 1]; k--; }
        ini[k] = p->inicio;
        fim[k] = p->inicio + p->setores;      /* conferida: não passa do disco */
    }
    for (int k = 0; k < 4; k++) if (!usada[k]) vagas[qtd_vagas++] = k;
    if (qtd_vagas < 2) {
        formatar_texto(livre_motivo, sizeof livre_motivo, "a tabela já tem %d partições primárias; o SaguiOS precisa de 2 entradas vazias", 4 - qtd_vagas);
        return false;
    }
    uint32_t pos = MB, total = discos[escolhido].setores;
    for (int k = 0; k <= n; k++) {
        uint32_t prox = k < n ? ini[k] : total;
        uint32_t a = acima_mb(pos), b = prox / MB * MB;
        if (b > a && b - a > livre_n) { livre_ini = a; livre_n = b - a; }
        if (k < n && fim[k] > pos) pos = fim[k];
    }
    if (livre_n < sistema_mb() + DADOS_MIN) {
        formatar_texto(livre_motivo, sizeof livre_motivo, "o maior espaço livre tem %u MB; o SaguiOS precisa de %u MB",
                       livre_n / MB, (sistema_mb() + DADOS_MIN) / MB);
        return false;
    }
    return true;
}

static bool cabe_inteiro(void)
{
    uint32_t fim = (discos[escolhido].setores - GPT_FIM) / MB * MB;      /* longe da cópia da GPT, que é apagada */
    return fim >= MB + sistema_mb() + DADOS_MIN;
}

static bool planejar(void)
{
    s_n = sistema_mb();
    if (modo != MODO_INTEIRO) {
        if (!livre_n) return false;
        s_ini = livre_ini;
        d_ini = s_ini + s_n;
        d_n = livre_ini + livre_n > d_ini ? livre_ini + livre_n - d_ini : 0;
        return d_n >= DADOS_MIN;
    }
    uint32_t fim = (discos[escolhido].setores - GPT_FIM) / MB * MB;
    s_ini = MB;
    d_ini = s_ini + s_n;
    d_n = fim > d_ini ? fim - d_ini : 0;
    return d_n >= DADOS_MIN;
}

/* ------------------------------------------------------------ a instalação */

static void (*avisar_progresso)(int pct, const char *etapa);

static void progresso(int pct, const char *etapa) { if (avisar_progresso) avisar_progresso(pct, etapa); }

static void entrada(uint8_t *e, uint8_t tipo, bool ativa, uint32_t ini, uint32_t n)
{
    memset(e, 0, 16);
    e[0] = ativa ? 0x80 : 0;
    chs(e + 1, ini);
    e[4] = tipo;
    chs(e + 5, ini + n - 1);
    poe32(e + 8, ini);
    poe32(e + 12, n);
}

static uint8_t antes[SETOR];               /* o setor 0 do disco escolhido, antes da instalação */

static int instalar(void)
{
    const char *dest = discos[escolhido].nome;
    uint32_t total = discos[escolhido].setores;
    if (!planejar()) { copia_texto(erro, "não há lugar para o SaguiOS neste disco", sizeof erro); return -1; }
    registrar("instalar: %s -> %s: sistema em %u (%u setores), arquivos em %u (%u MB), %s",
              discos[origem].nome, dest, s_ini, sis.n, d_ini, d_n / MB,
              modo == MODO_LIVRE ? "espaço livre" : modo == MODO_EDITAR ? "editor" : "disco inteiro");

    /* 0. O disco tem de estar como no resumo: a tabela que o kernel lê
     * agora é comparada com a do resumo, e, no espaço livre, cada entrada
     * de hoje é conferida no próprio setor 0. Se algo mudou, nada é gravado. */
    static tabela_disco_t agora;
    if (disco_tabela(dest, &agora) < 0 || memcmp(&agora, &tab, sizeof tab) != 0) {
        formatar_texto(erro, sizeof erro, "a tabela de %s mudou depois do resumo; nada foi gravado", dest);
        return -1;
    }
    if (setores(dest, 0, 1, antes, false) < 0) return -1;
    if (modo != MODO_INTEIRO) {
        for (uint32_t i = 0; i < tab.n; i++) {
            const particao_t *p = &tab.p[i];
            const uint8_t *e = antes + 446 + (p->entrada - 1) * 16;
            if (p->entrada > 4) continue;
            if (e[4] != p->tipo || le32(e + 8) != p->inicio || le32(e + 12) != p->setores) {
                formatar_texto(erro, sizeof erro, "a entrada %u do setor 0 não é a que o kernel leu; nada foi gravado", p->entrada);
                return -1;
            }
        }
        for (int k = 0; k < 2; k++) {
            bool de_excluida = false;
            for (uint32_t i = 0; i < tab.n; i++) if (tab.p[i].entrada == (uint32_t)vagas[k] + 1 && excluida(i)) de_excluida = true;
            if (antes[446 + vagas[k] * 16 + 4] && !de_excluida) { copia_texto(erro, "a entrada vazia não está vazia; nada foi gravado", sizeof erro); return -1; }
        }
        /* Uma última conta: o lugar do SaguiOS não encosta em nenhuma partição que fica. */
        for (uint32_t i = 0; i < tab.n; i++) {
            const particao_t *p = &tab.p[i];
            if (excluida(i) || (uint64_t)p->inicio >= (uint64_t)d_ini + d_n || (uint64_t)p->inicio + p->setores <= s_ini) continue;
            formatar_texto(erro, sizeof erro, "o lugar do SaguiOS cairia na partição %u; nada foi gravado", p->entrada);
            return -1;
        }
    }

    /* 1. Disco inteiro: apaga o que diz como o disco era, o primeiro MB
     * (MBR, a GPT e o que um carregador de boot deixa ali) e a cópia da GPT
     * no fim. Sem isso, um disco que era GPT continuaria parecendo GPT para
     * outros sistemas. No espaço livre, nada disso é tocado. */
    if (modo == MODO_INTEIRO) {
        progresso(2, "Apagando a tabela de partições antiga");
        if (zerar(dest, 0, MB) < 0 || zerar(dest, total - GPT_FIM, GPT_FIM) < 0) return -1;
    }

    /* 2. O sistema, da origem para o lugar novo (depois do cabeçalho), conferido. */
    for (uint32_t k = 0; k < sis.n; ) {
        uint32_t q = sis.n - k > POR_VEZ ? POR_VEZ : sis.n - k;
        if (setores(discos[origem].nome, sis.ini + k, q, buf, false) < 0 || gravar_conferindo(dest, s_ini + 1 + k, q, buf) < 0) return -1;
        k += q;
        progresso(5 + (int)(k * 65 / sis.n), "Copiando o sistema");
    }

    /* 3. A FAT32 dos arquivos, com o rótulo SAGUIOS. */
    progresso(72, "Criando a partição dos seus arquivos");
    if (formatar_fat32(dest, d_ini, d_n, "SAGUIOS") < 0) return -1;

    /* 4. O cabeçalho de boot no primeiro setor da partição do sistema, com
     * os LBA do lugar novo. O setor 1 do disco fica como está. */
    static const int campos[3] = { CAB_E2_LBA, CAB_K_LBA, CAB_R_LBA };
    uint8_t cab[SETOR];
    memcpy(cab, sis.cab, SETOR);
    for (int c = 0; c < 3; c++) poe32(cab + campos[c], le32(sis.cab + campos[c]) - sis.ini + s_ini + 1);
    poe32(cab + CAB_IMG_SET, s_ini + 1 + sis.n);
    progresso(85, "Gravando o setor de boot");
    if (gravar_conferindo(dest, s_ini, 1, cab) < 0) return -1;

    /* 5. Por último, o setor 0: o código de boot do SaguiOS, com o LBA do
     * cabeçalho. Disco inteiro: identificador novo e só as duas partições
     * do SaguiOS (o sistema ativo). Espaço livre: o identificador e as
     * entradas de hoje ficam byte a byte; as do SaguiOS vão nas vazias. A
     * troca do identificador quebraria o boot do Windows (BCD) e o
     * PARTUUID do Linux. */
    uint8_t m[SETOR];
    if (modo != MODO_INTEIRO) {
        memcpy(m, antes, SETOR);
        memcpy(m, sis.mbr, 440);
        for (uint32_t i = 0; i < tab.n; i++)                  /* no editor: as excluídas somem da tabela */
            if (tab.p[i].entrada <= 4 && excluida(i)) memset(m + 446 + (tab.p[i].entrada - 1) * 16, 0, 16);
        poe32(m + MBR_CAB, s_ini);
        uint32_t id = le32(antes + 440);
        while (!id) bytes_aleatorios(&id, sizeof id);
        poe32(m + 440, id);
        bool alguma_ativa = false;
        for (int k = 0; k < 4; k++) if (m[446 + k * 16] == 0x80) alguma_ativa = true;
        entrada(m + 446 + vagas[0] * 16, 0xDA, !alguma_ativa, s_ini, s_n);
        entrada(m + 446 + vagas[1] * 16, 0x0C, false, d_ini, d_n);
    } else {
        memset(m, 0, sizeof m);
        memcpy(m, sis.mbr, 440);
        poe32(m + MBR_CAB, s_ini);
        uint32_t id = 0;
        while (!id || id == le32(sis.mbr + 440)) bytes_aleatorios(&id, sizeof id);
        poe32(m + 440, id);
        entrada(m + 446, 0xDA, true, s_ini, s_n);
        entrada(m + 446 + 16, 0x0C, false, d_ini, d_n);
    }
    m[510] = 0x55; m[511] = 0xAA;
    progresso(90, "Gravando a tabela de partições");
    if (gravar_conferindo(dest, 0, 1, m) < 0) return -1;

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
    if (fd >= 0 && modo != MODO_INTEIRO) {           /* o setor 0 de antes: com ele, o boot antigo volta */
        int g = abrir("/usb/.saguios/mbr-antes.bin", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
        if (g < 0 || escrever(g, antes, SETOR) != SETOR) fd = -1;
        if (g >= 0) fechar(g);
    }
    desmontar(dest);
    if (fd < 0) { copia_texto(erro, "não deu para criar o arquivo de configurações", sizeof erro); return -1; }
    progresso(100, "Pronto");
    registrar("instalar: pronto em %s", dest);
    return 0;
}

/* ------------------------------------------------------------ a janela */

/* A janela nasce do tamanho da tela e pode mudar de tamanho: tudo é medido
 * a partir dela em medir(). Numa janela estreita, a coluna dos passos some
 * e o conteúdo usa a largura toda. */
static int L = 820, A = 560;
static int LADO, X0, CL, RODAPE_Y;
static bool compacto;                     /* janela baixa (640x480) */

#define COR_LADO     0xE6ECF0
#define COR_CARTAO   0xFFFFFF
#define COR_LINHA    0xD3DAE0
#define COR_VERDE_CL 0xE6F4EB
#define COR_AVISO    0xFFF4D6
#define COR_PERIGO   0xC4302B

enum { P_BOAS_VINDAS, P_DISCO, P_MODO, P_RESUMO, P_INSTALANDO, P_PRONTO, P_ERRO };
#define QTD_PASSOS 5
static const char *const nomes_passos[QTD_PASSOS] = { "Boas-vindas", "Disco", "Partições", "Resumo", "Instalação" };

static jan_t *j;
static int passo;

/* Botões arredondados: o principal é verde cheio (vermelho no Instalar); os
 * outros, brancos com borda. */
typedef struct { int x, y, l, a; const char *texto; bool principal, ativo, perigo; } bt_t;
enum { B_VOLTAR, B_CONTINUAR, B_CANCELAR, B_REINICIAR, QTD_BT };
static bt_t bts[QTD_BT];

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

static void medir(void)
{
    L = j->larg;
    A = j->alt;
    LADO = L >= 800 ? 212 : 0;
    int m = L >= 560 ? 28 : 16;
    X0 = LADO + m;
    CL = L - X0 - m;
    RODAPE_Y = A - 58;
    compacto = A < 460;
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
    int atual = passo >= P_INSTALANDO ? P_INSTALANDO : passo;
    for (int k = 0; k < QTD_PASSOS; k++) {
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
    case P_DISCO:       return escolhido >= 0 && escolhido != origem && cabe_inteiro();
    case P_MODO:        return planejar();
    case P_RESUMO:      return true;
    default:            return false;
    }
}

/* As linhas do resumo: as mesmas vão para a janela e, as quatro primeiras,
 * para a caixa de confirmação do kernel. */
static int linhas_resumo(char l[5][120])
{
    const char *n = discos[escolhido].nome;
    if (modo == MODO_LIVRE) {
        formatar_texto(l[0], 120, "usar o espaço livre de %s: %u MB, a partir de %u MB", n, livre_n / MB, livre_ini / MB);
        formatar_texto(l[1], 120, "copiar o SaguiOS para uma partição de %u MB", s_n / MB);
        formatar_texto(l[2], 120, "criar a partição dos seus arquivos: FAT32 \"SAGUIOS\", %u MB", d_n / MB);
        formatar_texto(l[3], 120, "trocar o código de boot; as %u partições de hoje ficam", tab.n);
        return 4;
    }
    if (modo == MODO_EDITAR) {
        /* Todas as exclusões, sem cortar: as primárias pelo número, e as
         * lógicas (que saem com a estendida) pelo intervalo. */
        char lista[40] = "";
        uint32_t lmin = 0, lmax = 0, ficam = 0, qtd = 0;
        for (uint32_t i = 0; i < tab.n; i++) {
            const particao_t *p = &tab.p[i];
            if (!excluida(i)) { ficam++; continue; }
            if (p->entrada >= 5) { if (!lmin || p->entrada < lmin) lmin = p->entrada; if (p->entrada > lmax) lmax = p->entrada; continue; }
            char um[8];
            formatar_texto(um, sizeof um, "%s%u", qtd++ ? ", " : "", p->entrada);
            junta_texto(lista, um, sizeof lista);
        }
        if (lmin == lmax && lmin) formatar_texto(l[0], 120, "excluir %s %s de %s (e a lógica %u)", qtd > 1 ? "as partições" : "a partição", lista, n, lmin);
        else if (lmin) formatar_texto(l[0], 120, "excluir %s %s de %s (e as lógicas %u a %u)", qtd > 1 ? "as partições" : "a partição", lista, n, lmin, lmax);
        else if (qtd) formatar_texto(l[0], 120, "excluir %s %s de %s", qtd > 1 ? "as partições" : "a partição", lista, n);
        else formatar_texto(l[0], 120, "não excluir nada; usar o espaço livre de %s", n);
        formatar_texto(l[1], 120, "copiar o SaguiOS para uma partição de %u MB, em %u MB", s_n / MB, s_ini / MB);
        formatar_texto(l[2], 120, "criar a partição dos seus arquivos: FAT32 \"SAGUIOS\", %u MB", d_n / MB);
        if (ficam == 1) copia_texto(l[3], "trocar o código de boot; a outra partição fica", 120);
        else formatar_texto(l[3], 120, "trocar o código de boot; as outras %u partições ficam", ficam);
        return 4;
    }
    unsigned logicas = 0;
    for (unsigned i = 0; tab_ok && i < tab.n; i++) if (tab.p[i].entrada >= 5) logicas++;
    if (tab_ok && tab.estado == TABELA_MBR && logicas)
        formatar_texto(l[0], 120, "apagar todas as partições de %s (%u, sendo %u lógicas)", n, tab.n, logicas);
    else if (tab_ok && tab.estado == TABELA_MBR)
        formatar_texto(l[0], 120, "apagar todas as partições de %s (%u) e tudo o que há nele", n, tab.n);
    else
        formatar_texto(l[0], 120, "apagar todas as partições e tudo o que está em %s", n);
    formatar_texto(l[1], 120, "copiar o SaguiOS para uma partição de %u MB", s_n / MB);
    formatar_texto(l[2], 120, "criar a partição dos seus arquivos: FAT32 \"SAGUIOS\", %u MB", d_n / MB);
    copia_texto(l[3], "trocar o código de boot, a tabela e o identificador do disco", 120);
    return 4;
}

static void desenhar_resumo(void)
{
    const info_disco_t *d = &discos[escolhido];
    char t[160], tam[24], l[5][120];
    negrito(X0, 22, "Tudo pronto para instalar", COR_TEXTO);
    tx(X0, 42, "Confira. Ao clicar em Instalar, o sistema pede o nome do disco.", COR_TEXTO_FRACO);
    /* O aviso vai colado no rodapé, com a altura do texto; a lista usa o
     * que sobra entre os dois. */
    const char *aviso = modo == MODO_EDITAR ?
        "O que está nas partições a excluir se perde. As outras ficam, mas o código de boot do disco é trocado: "
        "outro sistema que ligava por ele deixa de ligar. O setor 0 de antes fica em /usb/.saguios/mbr-antes.bin." :
        modo == MODO_LIVRE ?
        "As partições de hoje ficam, mas o código de boot do disco é trocado: outro sistema que ligava por ele "
        "(o Windows, um Linux) deixa de ligar. O setor 0 de antes fica guardado em /usb/.saguios/mbr-antes.bin." :
        "Tudo o que está no disco hoje se perde, e outro sistema que ligava por ele "
        "(o Windows, um Linux) deixa de ligar. Confira se é o disco certo.";
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
        formatar_texto(t, sizeof t, "%s. Hoje: %s", d->interno ? "Disco interno" : "Pendrive USB", d->formato);
        tx(X0 + 70, 110, t, COR_TEXTO_FRACO);
        g_retangulo(j, X0 + 16, 140, CL - 32, 1, COR_LINHA);
    }
    int n = linhas_resumo(l);
    for (int k = 0; k < n; k++) {
        formatar_texto(t, sizeof t, "- %c%s.", l[k][0] - 'a' + 'A', l[k] + 1);
        if (y + 18 <= fim_lista) tx(X0 + 22, y, t, k == 0 && modo != MODO_LIVRE ? COR_PERIGO : COR_TEXTO);
        y += passo_linha;
    }
    if (modo == MODO_EDITAR)                      /* cada partição que sai, com o que há nela */
        for (uint32_t i = 0; i < tab.n && y + 18 <= fim_lista; i++) {
            const particao_t *p = &tab.p[i];
            if (!excluida(i)) continue;
            char tam2[24];
            tamanho_texto(p->setores, tam2, sizeof tam2);
            formatar_texto(t, sizeof t, "    partição %u: %s, %s, de %u MB a %u MB", p->entrada,
                           eh_estendida(p->tipo) ? "estendida" : p->formato[0] ? p->formato : "formato desconhecido", tam2,
                           p->inicio / MB, (uint32_t)(((uint64_t)p->inicio + p->setores) / MB));
            tx(X0 + 22, y, t, COR_PERIGO);
            y += passo_linha;
        }
    lim = antes;
    cartao(X0, ay, CL, ah, COR_AVISO, 0xE8CF8A);
    if (!compacto) negrito(X0 + 18, ay + 12, "Atenção", 0x8A5A00);   /* na janela baixa, sem o título */
    paragrafo(X0 + 18, ay + (compacto ? 12 : 34), CL - 36, aviso, COR_TEXTO);
}

/* As três opções do passo Partições e a lista do que há no disco. */
#define MODO_Y(i) (72 + (i) * (compacto ? 52 : 66))
#define MODO_A    (compacto ? 46 : 58)

static void opcao(int i, const char *titulo, const char *texto, bool pode)
{
    int y = MODO_Y(i);
    bool sel = (i == 0 ? MODO_LIVRE : i == 1 ? MODO_INTEIRO : MODO_EDITAR) == modo && pode;
    cartao(X0, y, CL, MODO_A, !pode ? 0xF2F4F6 : sel ? COR_VERDE_CL : COR_CARTAO, sel ? COR_DESTAQUE : COR_LINHA);
    if (pode) radio(X0 + 22, y + MODO_A / 2, sel);
    negrito(X0 + 44, y + (compacto ? 6 : 10), titulo, pode ? COR_TEXTO : COR_TEXTO_FRACO);
    tx(X0 + 44, y + (compacto ? 24 : 32), texto, pode ? COR_TEXTO_FRACO : 0x8A949D);
}

static void linha_part(int y, const char *n, const char *o, uint32_t ini, uint32_t q, uint32_t cor)
{
    char t[40];
    tx(X0 + 16, y, n, cor);
    tx(X0 + 70, y, o, cor);
    formatar_texto(t, sizeof t, "%u MB a %u MB", ini / MB, (uint32_t)(((uint64_t)ini + q) / MB));
    tx(X0 + CL / 2, y, t, cor);
    tamanho_texto(q, t, sizeof t);
    tx(X0 + CL - 16 - g_largura_texto(t), y, t, cor);
}

/* No editor, onde cada partição ficou na lista (para o clique). */
static int linha_y[TABELA_MAX], linha_i[TABELA_MAX], qtd_linhas;

static bool pode_editar(void) { return tab_ok && tab.estado == TABELA_MBR && tab.conferida && tab.n; }

static void desenhar_modos(void)
{
    char t[160];
    int antes = lim;
    lim = X0 + CL - 12;
    qtd_linhas = 0;
    if (pode_livre) formatar_texto(t, sizeof t, "%u MB livres a partir de %u MB. As partições de hoje ficam.", livre_n / MB, livre_ini / MB);
    else formatar_texto(t, sizeof t, "Não dá: %s.", motivo_livre);
    if (modo == MODO_EDITAR && pode_livre) formatar_texto(t, sizeof t, "As partições de hoje ficam.");
    opcao(0, "Usar o espaço livre", t, pode_livre);
    formatar_texto(t, sizeof t, "Tudo o que está em %s se perde.", discos[escolhido].nome);
    opcao(1, "Apagar o disco inteiro", t, true);
    if (!pode_editar()) formatar_texto(t, sizeof t, "Não dá: %s.", tab_ok && tab.problema[0] ? tab.problema : "o disco não tem uma tabela MBR com partições");
    else if (modo != MODO_EDITAR) copia_texto(t, "Escolha na lista quais saem; o SaguiOS vai no lugar delas.", sizeof t);
    else if (!excluir) copia_texto(t, "Clique na lista nas partições que vão sair.", sizeof t);
    else if (livre_motivo[0]) formatar_texto(t, sizeof t, "Ainda não dá: %s.", livre_motivo);
    else formatar_texto(t, sizeof t, "O SaguiOS vai em %u MB, a partir de %u MB.", livre_n / MB, livre_ini / MB);
    opcao(2, "Excluir partições e usar o lugar delas", t, pode_editar());
    lim = antes;
    int y0 = MODO_Y(3) + 4, fim = RODAPE_Y - 26;
    if (fim - y0 < 40) return;
    cartao(X0, y0, CL, fim - y0, COR_CARTAO, COR_LINHA);
    lim = X0 + CL - 12;
    int y = y0 + 12;
    if (!tab_ok || tab.estado != TABELA_MBR) {
        tx(X0 + 16, y, tab_ok && tab.estado == TABELA_VAZIO ? "Nenhuma partição: o setor 0 está zerado." : tab.problema, COR_TEXTO_FRACO);
        lim = antes;
        return;
    }
    /* As partições e as regiões livres, em ordem de início. */
    uint32_t feito = 0, pos = MB;
    for (uint32_t c = 0; c <= tab.n && y + 18 <= fim - 8; c++) {
        const particao_t *p = NULL;
        for (uint32_t i = 0; i < tab.n; i++)
            if (!(feito & (1u << i)) && (!p || tab.p[i].inicio < p->inicio)) p = &tab.p[i];
        uint32_t prox = p ? p->inicio : discos[escolhido].setores;
        uint32_t a = acima_mb(pos), b = prox / MB * MB;
        if ((!p || p->entrada <= 4) && b > a && modo != MODO_EDITAR) {   /* as lógicas ficam dentro da estendida: lá não há livre */
            linha_part(y, "", a == livre_ini && modo == MODO_LIVRE ? "livre: SaguiOS" : "livre", a, b - a,
                       a == livre_ini && modo == MODO_LIVRE ? COR_DESTAQUE : 0x8A949D);
            y += 20;
            if (y + 18 > fim - 8) break;
        }
        if (!p) break;
        feito |= 1u << (p - tab.p);
        char n[8], o[40];
        formatar_texto(n, sizeof n, p->entrada >= 5 ? "  %u" : "%u", p->entrada);
        bool est = p->tipo == 0x05 || p->tipo == 0x0F || p->tipo == 0x85;
        static const char hex[] = "0123456789ABCDEF";
        char tipo[3] = { hex[(p->tipo >> 4) & 15], hex[p->tipo & 15], 0 };
        uint32_t idx = (uint32_t)(p - tab.p);
        bool sai = modo == MODO_EDITAR && excluida(idx);
        formatar_texto(o, sizeof o, "%s%s (0x%s)", sai ? "sai: " : "", est ? "estendida" : p->formato[0] ? p->formato : "partição", tipo);
        linha_part(y, n, o, p->inicio, p->setores, sai ? COR_PERIGO : COR_TEXTO);
        if (modo == MODO_EDITAR) { linha_y[qtd_linhas] = y; linha_i[qtd_linhas++] = (int)idx; }
        y += 20;
        if (p->entrada <= 4 && p->inicio + p->setores > pos) pos = p->inicio + p->setores;
    }
    lim = antes;
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
            "Por enquanto, ele usa o disco inteiro: tudo o que estiver nele se perde. "
            "Nada é gravado antes de você confirmar no último passo.", COR_TEXTO);
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
        tx(X0, 42, "Escolha o disco. No próximo passo, você vê o que há nele.", COR_TEXTO_FRACO);
        int outros = 0;
        for (int i = 0; i < qtd_discos; i++) {
            desenhar_disco_cartao(i, i == escolhido);
            if (i != origem) outros++;
        }
        int y = DISCO_Y(qtd_discos) + 4;
        if (!outros) {
            cartao(X0, y, CL, 58, COR_AVISO, 0xE8CF8A);
            negrito(X0 + 18, y + 10, "Nenhum outro disco para instalar.", COR_TEXTO);
            tx(X0 + 18, y + 30, "Conecte um disco rígido (IDE) ou um pendrive e abra de novo.", COR_TEXTO_FRACO);
        } else if (escolhido >= 0 && escolhido != origem && !cabe_inteiro()) {
            formatar_texto(t, sizeof t, "%s é pequeno demais: o SaguiOS precisa de %u MB.", discos[escolhido].nome,
                           (MB + sistema_mb() + DADOS_MIN + GPT_FIM) / MB + 1);
            tx(X0, y, t, COR_PERIGO);
        }
        break;
    }
    case P_MODO: {
        formatar_texto(t, sizeof t, "Como instalar em %s?", discos[escolhido].nome);
        negrito(X0, 22, t, COR_TEXTO);
        tx(X0, 42, "As partições abaixo são as que o sistema leu do disco.", COR_TEXTO_FRACO);
        desenhar_modos();
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
    registrar("instalar: passo %d (%s)", p, p < QTD_PASSOS ? nomes_passos[p] : p == P_PRONTO ? "pronto" : "erro");
    desenhar();
}

/* ------------------------------------------------------------ os cliques */

static bool terminar;

/* "Instalar": o kernel mostra o disco e o resumo e pede o nome dele. Só
 * depois disso o disco escolhido é lido ou gravado. */
static void instalar_confirmado(void)
{
    const info_disco_t *d = &discos[escolhido];
    char l[5][120], resumo[400];
    int n = linhas_resumo(l);
    resumo[0] = 0;
    for (int k = 0; k < n; k++) {
        if (k) junta_texto(resumo, "\n", sizeof resumo);
        junta_texto(resumo, l[k], sizeof resumo);
    }
    int r = disco_liberar(d->nome, resumo);
    registrar("instalar: liberar %s -> %d", d->nome, r);
    if (r < 0) {
        if (r != ERRO_ACESSO) mensagem("Instalar", nome_erro(r));
        return;                                   /* Esc ou nome errado: continua no resumo, nada gravado */
    }
    avisar_progresso = progresso_janela;
    ir(P_INSTALANDO);
    if (instalar() < 0) { registrar("instalar: erro: %s", erro); ir(P_ERRO); }
    else ir(P_PRONTO);
}

static void continuar(void)
{
    if (!pode_continuar()) return;
    if (passo == P_BOAS_VINDAS) ir(P_DISCO);
    else if (passo == P_DISCO) {
        /* A tabela, lida e conferida pelo kernel (sem liberação), e o
         * espaço livre. Com ele, a opção já vem marcada: é a que não apaga
         * nada de ninguém. */
        tab_ok = disco_tabela(discos[escolhido].nome, &tab) == 0;
        excluir = 0;
        modo = MODO_LIVRE;
        bool livre = pode_livre = achar_livre();
        copia_texto(motivo_livre, livre_motivo, sizeof motivo_livre);
        modo = livre ? MODO_LIVRE : MODO_INTEIRO;
        registrar("instalar: tabela de %s: estado %u, %u partições, conferida %u (%s)", discos[escolhido].nome,
                  tab.estado, tab.n, tab.conferida, tab.problema);
        if (livre) registrar("instalar: espaço livre: %u MB em %u", livre_n / MB, livre_ini);
        else registrar("instalar: espaço livre: não (%s)", livre_motivo);
        ir(P_MODO);
    } else if (passo == P_MODO) ir(P_RESUMO);
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
    if (passo == P_MODO) {
        for (int i = 0; i < 3; i++) {
            int cy = MODO_Y(i);
            int m = i == 0 ? MODO_LIVRE : i == 1 ? MODO_INTEIRO : MODO_EDITAR;
            bool pode = m == MODO_INTEIRO || (m == MODO_LIVRE && pode_livre) || (m == MODO_EDITAR && pode_editar());
            if (y >= cy && y < cy + MODO_A && x >= X0 && x < X0 + CL && pode) {
                modo = m;
                if (m != MODO_INTEIRO) achar_livre();
                registrar("instalar: modo %s", m == MODO_LIVRE ? "espaço livre" : m == MODO_EDITAR ? "editor" : "disco inteiro");
                desenhar();
                return;
            }
        }
        /* No editor, um clique numa partição a marca para sair, ou desmarca.
         * Uma lógica não sai sozinha: sai a estendida, com todas as lógicas. */
        for (int k = 0; modo == MODO_EDITAR && k < qtd_linhas; k++) {
            if (y < linha_y[k] - 2 || y >= linha_y[k] + 18 || x < X0 || x >= X0 + CL) continue;
            uint32_t i = (uint32_t)linha_i[k];
            if (tab.p[i].entrada >= 5)
                for (uint32_t e = 0; e < tab.n; e++) if (eh_estendida(tab.p[e].tipo) && tab.p[e].entrada <= 4) i = e;
            excluir ^= 1u << i;
            achar_livre();
            registrar("instalar: partição %u %s", tab.p[i].entrada, (excluir >> i) & 1 ? "sai" : "fica");
            if (livre_motivo[0]) registrar("instalar: editor: não dá (%s)", livre_motivo);
            else registrar("instalar: editor: SaguiOS em %u MB, %u MB", livre_ini / MB, livre_n / MB);
            desenhar();
            return;
        }
    }
    switch (bt_em(x, y)) {
    case B_CONTINUAR:
        if (passo == P_RESUMO) instalar_confirmado();
        else continuar();
        break;
    case B_VOLTAR:    ir(passo - 1); break;
    case B_CANCELAR:  terminar = true; break;
    case B_REINICIAR:
        registrar("instalar: reiniciar");
        jan_fechar(j);
        reiniciar();
        break;
    }
}

int principal(int argc, char **argv)
{
    (void)argv;
    if (argc >= 2) { imprimir("uso: instalar   (abre a janela; não há opções)\n"); return 1; }
    buf = aloca(POR_VEZ * SETOR);
    volta = aloca(POR_VEZ * SETOR);
    if (!buf || !volta) return 1;
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
            /* Enter avança nos passos e fecha no fim; no resumo, não: o
             * Instalar só com o clique. */
            if (e.tecla == '\n' && (passo == P_PRONTO || passo == P_ERRO)) break;
            if (e.tecla == '\n' && passo != P_RESUMO) continuar();
            else if (e.tecla == TECLA_ESC && passo != P_INSTALANDO) break;
        }
    }
    jan_fechar(j);
    return 0;
}
