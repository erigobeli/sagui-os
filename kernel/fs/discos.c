/* discos.c - pendrives: formato, montagem em /usb e avisos (H32, H34).
 *
 * Quando um pendrive aparece, este módulo descobre o que há nele:
 *   - setor 0 com tabela de partições MBR: olha a primeira partição usada;
 *   - setor 0 que já é um setor de boot FAT32 ("superfloppy", sem tabela);
 *   - outros formatos (NTFS, exFAT, ext, o próprio SaguiOS...) aparecem com
 *     o nome e "formato não suportado", e não montam.
 * Só um pendrive fica montado por vez, em /usb (PRD 4.3). Desmontar grava
 * tudo o que estiver pendente. Cada conexão, remoção e montagem vira um
 * aviso, que a área de trabalho mostra num canto da tela. */
#include "disco.h"
#include "configuracoes.h"
#include "abi.h"
#include "console.h"
#include "fat32.h"
#include "memoria.h"
#include "processo.h"
#include "sistema.h"
#include "texto.h"
#include "vfs.h"
#include "gui.h"
#include "bootinfo.h"

static disco_t discos[MAX_DISCOS];
static int contador;

#define MAX_AVISOS 8
static struct { int tipo; char texto[96]; } avisos[MAX_AVISOS];
static int av_ini, av_fim;

static void avisar(int tipo, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    formatar(avisos[av_fim].texto, sizeof avisos[av_fim].texto, fmt, ap);
    va_end(ap);
    avisos[av_fim].tipo = tipo;
    av_fim = (av_fim + 1) % MAX_AVISOS;
    if (av_fim == av_ini) av_ini = (av_ini + 1) % MAX_AVISOS;
    klog("[DISCO] aviso: %s\n", avisos[(av_fim + MAX_AVISOS - 1) % MAX_AVISOS].texto);
}

bool disco_proximo_aviso(int *tipo, char *texto, int max)
{
    if (av_ini == av_fim) return false;
    *tipo = avisos[av_ini].tipo;
    txt_copia_n(texto, avisos[av_ini].texto, (size_t)max);
    av_ini = (av_ini + 1) % MAX_AVISOS;
    return true;
}

disco_t *disco_indice(int i) { return (i >= 0 && i < MAX_DISCOS && discos[i].usado) ? &discos[i] : NULL; }

disco_t *disco_por_nome(const char *nome)
{
    for (int i = 0; i < MAX_DISCOS; i++)
        if (discos[i].usado && txt_compara(discos[i].nome, nome) == 0) return &discos[i];
    return NULL;
}

disco_t *disco_montado(void)
{
    for (int i = 0; i < MAX_DISCOS; i++)
        if (discos[i].usado && discos[i].montado) return &discos[i];
    return NULL;
}

static void tamanho_legivel(uint32_t setores, char *s, size_t max)
{
    uint32_t mb = setores / 2048;
    if (mb >= 1024) snformatar(s, max, "%u,%u GB", mb / 1024, (mb % 1024) * 10 / 1024);
    else snformatar(s, max, "%u MB", mb);
}

static void copiar_rotulo(disco_t *d, const uint8_t *r)
{
    int n = 11;
    while (n > 0 && (r[n - 1] == ' ' || r[n - 1] == 0)) n--;
    memcpy(d->rotulo, r, (size_t)n);
    d->rotulo[n] = 0;
    if (txt_compara(d->rotulo, "NO NAME") == 0) d->rotulo[0] = 0;
}

static bool eh_fat32(const uint8_t *s)
{
    return s[510] == 0x55 && s[511] == 0xAA && memcmp(s + 82, "FAT32   ", 8) == 0 &&
           (s[11] | (s[12] << 8)) == 512 && s[13] != 0 && (s[22] | (s[23] << 8)) == 0;
}

/* Descobre o formato a partir do setor 0 e, se houver, da primeira partição. */
static void reconhecer(disco_t *d)
{
    uint8_t *s = kmalloc(512);
    if (!s) return;
    txt_copia_n(d->formato, "desconhecido: formato não suportado", sizeof d->formato);
    if (d->ler(d, 0, 1, s) < 0) { txt_copia_n(d->formato, "erro de leitura", sizeof d->formato); kfree(s); return; }
    d->id_mbr = s[510] == 0x55 && s[511] == 0xAA ? (uint32_t)s[440] | (uint32_t)s[441] << 8 | (uint32_t)s[442] << 16 | (uint32_t)s[443] << 24 : 0;
    bool vazio = true;
    for (int i = 0; i < 512 && vazio; i++) vazio = s[i] == 0;
    if (vazio) txt_copia_n(d->formato, "vazio", sizeof d->formato);
    if (eh_fat32(s)) {                                   /* sem tabela de partições */
        d->part_inicio = 0;
        d->fat32 = true;
        copiar_rotulo(d, s + 71);
    } else if (s[510] == 0x55 && s[511] == 0xAA) {
        /* Olha as quatro partições e fica com a primeira FAT32. Um pendrive
         * gravado com o saguios.img e depois formatado só no espaço livre
         * tem a partição do SaguiOS (0xDA) primeiro e a FAT32 depois: foi o
         * caso no G470 (erro 18). Sem FAT32, o formato mostrado é o da
         * primeira partição usada. */
        bool achou_outro = false;
        for (int p = 0; p < 4; p++) {             /* todas: a partição do sistema (0xDA) pode vir depois */
            const uint8_t *e = s + 446 + p * 16;
            uint8_t tipo = e[4];
            uint32_t ini = (uint32_t)e[8] | (uint32_t)e[9] << 8 | (uint32_t)e[10] << 16 | (uint32_t)e[11] << 24;
            if (!tipo || (e[0] != 0 && e[0] != 0x80)) continue;
            if (tipo == 0x0B || tipo == 0x0C) {
                if (d->fat32) continue;            /* fica a primeira FAT32 */
                uint8_t *v = kmalloc(512);
                if (v && d->ler(d, ini, 1, v) == 0 && eh_fat32(v)) {
                    d->part_inicio = ini;
                    d->fat32 = true;
                    copiar_rotulo(d, v + 71);
                } else if (!achou_outro) {
                    txt_copia_n(d->formato, "FAT32 danificado: não suportado", sizeof d->formato);
                    achou_outro = true;
                }
                if (v) kfree(v);
                continue;
            }
            if (tipo == 0xDA) {
                d->de_boot = d->tem_sistema = true;
                d->sistema_ini = ini;
                d->sistema_fim = ini + ((uint32_t)e[12] | (uint32_t)e[13] << 8 | (uint32_t)e[14] << 16 | (uint32_t)e[15] << 24);
            }
            if (achou_outro) continue;
            achou_outro = true;
            switch (tipo) {
            case 0x01: case 0x04: case 0x06: case 0x0E:
                txt_copia_n(d->formato, "FAT12/16: formato não suportado", sizeof d->formato); break;
            case 0x07: {
                uint8_t *v = kmalloc(512);
                bool exfat = v && d->ler(d, ini, 1, v) == 0 && memcmp(v + 3, "EXFAT   ", 8) == 0;
                txt_copia_n(d->formato, exfat ? "exFAT: formato não suportado" : "NTFS: formato não suportado", sizeof d->formato);
                if (v) kfree(v);
                break;
            }
            case 0x83: txt_copia_n(d->formato, "ext (Linux): formato não suportado", sizeof d->formato); break;
            case 0xDA: txt_copia_n(d->formato, "pendrive de boot do SaguiOS", sizeof d->formato); break;
            case 0xEE: txt_copia_n(d->formato, "GPT: formato não suportado", sizeof d->formato); break;
            default:   snformatar(d->formato, sizeof d->formato, "partição tipo %x: não suportado", tipo);
            }
        }
        if (d->fat32) d->de_boot = false;   /* tem uma FAT32: é um pendrive de dados (e talvez de boot também) */
    }
    if (d->fat32) {
        txt_copia_n(d->formato, "FAT32", sizeof d->formato);
        /* O rótulo de verdade fica numa entrada especial na pasta raiz. */
        char r[12];
        if (fat_rotulo_raiz(d, r)) txt_copia_n(d->rotulo, r, sizeof d->rotulo);
    }
    kfree(s);
}

/* Um pendrive que já guarda configurações do SaguiOS (.saguios/) é montado
 * sozinho ao conectar: ele já foi montado pelo usuário antes, e é assim que
 * as configurações valem desde o boot (D031). Os outros continuam esperando
 * o usuário montar. A espiada é só leitura: se não for do SaguiOS, desmonta
 * sem gravar nada. */
static bool liberado_para_atual(const disco_t *d);

static bool montar_se_for_do_saguios(disco_t *d)
{
    if (disco_montado()) return false;
    /* Disco interno: só a partição que o instalador criou, ou seja, rótulo
     * SAGUIOS *e* a partição do sistema (0xDA) no mesmo disco. O rótulo
     * sozinho qualquer FAT32 pode ter (D064). */
    if (d->interno && (!d->tem_sistema || txt_compara(d->rotulo, "SAGUIOS") != 0)) return false;
    if (fat_montar(d) < 0) return false;
    if (!config_tem_arquivo()) { fat_desmontar(false); return false; }
    d->montado = true;
    vfs_ponto_montagem(true);
    config_ao_montar();
    klog("[DISCO] %s tem configuracoes do SaguiOS: montado sozinho\n", d->nome);
    return true;
}

static disco_t *registrar(bool interno, const char *fabricante, const char *produto, uint32_t setores, uint32_t tam_setor, void *dono,
                          int (*ler)(disco_t *, uint32_t, uint32_t, void *),
                          int (*escrever)(disco_t *, uint32_t, uint32_t, const void *));

disco_t *disco_registrar(const char *fabricante, const char *produto, uint32_t setores, uint32_t tam_setor, void *dono,
                         int (*ler)(disco_t *, uint32_t, uint32_t, void *),
                         int (*escrever)(disco_t *, uint32_t, uint32_t, const void *))
{
    return registrar(false, fabricante, produto, setores, tam_setor, dono, ler, escrever);
}

disco_t *disco_registrar_interno(const char *modelo, uint32_t setores, void *dono,
                                 int (*ler)(disco_t *, uint32_t, uint32_t, void *),
                                 int (*escrever)(disco_t *, uint32_t, uint32_t, const void *))
{
    return registrar(true, "ATA", modelo, setores, 512, dono, ler, escrever);
}

static int contador_hd;

static disco_t *registrar(bool interno, const char *fabricante, const char *produto, uint32_t setores, uint32_t tam_setor, void *dono,
                          int (*ler)(disco_t *, uint32_t, uint32_t, void *),
                          int (*escrever)(disco_t *, uint32_t, uint32_t, const void *))
{
    for (int i = 0; i < MAX_DISCOS; i++) {
        disco_t *d = &discos[i];
        if (d->usado) continue;
        memset(d, 0, sizeof *d);
        d->usado = d->presente = true;
        d->interno = interno;
        if (interno) snformatar(d->nome, sizeof d->nome, "hd%d", contador_hd++);
        else snformatar(d->nome, sizeof d->nome, "usb%d", contador++);
        txt_copia_n(d->fabricante, fabricante, sizeof d->fabricante);
        txt_copia_n(d->produto, produto, sizeof d->produto);
        d->setores = setores;
        d->tam_setor = tam_setor;
        d->dono = dono;
        d->ler = ler;
        d->escrever = escrever;
        reconhecer(d);
        char tam[24];
        tamanho_legivel(setores, tam, sizeof tam);
        klog("[DISCO] %s: %s %s, %s, %s, rotulo \"%s\"\n", d->nome, fabricante, produto, tam, d->formato, d->rotulo);
        if (d->de_boot) ;   /* o pendrive de onde o sistema acabou de ligar: nada a avisar */
        else if (d->fat32 && montar_se_for_do_saguios(d)) {
            if (!d->interno)      /* o disco instalado monta calado, como um disco de sistema */
                avisar(AVISO_MONTADO, "Pendrive com as suas configurações montado em /usb (%s)", d->rotulo[0] ? d->rotulo : d->produto);
        } else if (d->interno) ;  /* outros discos internos: nada a avisar nem montar */
        else if (d->fat32) avisar(AVISO_CONECTADO, "Pendrive conectado: %s (%s)", d->rotulo[0] ? d->rotulo : d->produto, tam);
        else avisar(AVISO_CONECTADO, "Pendrive conectado: %s (%s), %s", d->produto, tam, d->formato);
        return d;
    }
    return NULL;
}

void disco_removido(disco_t *d)
{
    d->presente = false;
    if (d->montado) {
        fat_desmontar(false);   /* não dá para gravar: o pendrive já saiu */
        d->montado = false;
        vfs_ponto_montagem(false);
        config_ao_desmontar();
        avisar(AVISO_REMOVIDO, "O pendrive foi removido sem desmontar: dados podem ter se perdido.");
    } else if (!d->de_boot) {
        avisar(AVISO_REMOVIDO, "Pendrive removido.");
    }
    klog("[DISCO] %s removido\n", d->nome);
    d->usado = false;
}

int disco_montar(disco_t *d)
{
    if (!d->presente) return ERRO_NAO_EXISTE;
    if (d->montado) return 0;
    if (!d->fat32) return ERRO_FORMATO;
    if (disco_montado()) return ERRO_OCUPADO;      /* um por vez em /usb */
    /* Disco interno: é o HD de quem testa (o Windows, outro Linux). Só
     * monta a partição que o instalador criou: FAT32 com rótulo SAGUIOS e
     * .saguios/configuracoes.txt. O instalador, liberado para esse disco
     * (D063), monta a partição recém-formatada para gravar esse arquivo
     * (D064). */
    if (d->interno && !liberado_para_atual(d) && (!d->tem_sistema || txt_compara(d->rotulo, "SAGUIOS") != 0)) {
        klog("[DISCO] %s: disco interno, nao monta\n", d->nome);
        return ERRO_ACESSO;
    }
    int r = fat_montar(d);
    if (r < 0) return r;
    if (d->interno && !config_tem_arquivo() && !liberado_para_atual(d)) {
        fat_desmontar(false);
        klog("[DISCO] %s: disco interno, nao monta\n", d->nome);
        return ERRO_ACESSO;
    }
    d->montado = true;
    vfs_ponto_montagem(true);
    config_ao_montar();   /* as configurações guardadas nesse pendrive passam a valer (H37) */
    avisar(AVISO_MONTADO, "Pendrive montado em /usb");
    return 0;
}

int disco_desmontar(disco_t *d)
{
    if (!d->montado) return 0;
    /* Ninguém pode estar com a pasta atual dentro do pendrive. */
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        processo_t *p = processo_indice(i);
        if (p->estado != P_LIVRE && p->estado != P_ZUMBI && txt_comeca_com(p->pasta, "/usb") &&
            (p->pasta[4] == 0 || p->pasta[4] == '/'))
            txt_copia_n(p->pasta, "/", sizeof p->pasta);
    }
    int r = fat_desmontar(true);
    d->montado = false;
    vfs_ponto_montagem(false);
    config_ao_desmontar();
    if (d->interno) avisar(AVISO_DESMONTADO, "Disco %s desmontado: tudo gravado.", d->nome);
    else avisar(AVISO_DESMONTADO, "Pode remover o pendrive.");
    return r;
}

/* ------------------------------------------------------------ chamadas */

int32_t sis_listar_discos(uint32_t indice, uint32_t pinfo, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)dd; (void)e;
    info_disco_t *inf = (info_disco_t *)(uintptr_t)pinfo;
    /* Programas antigos passam 0 em c e têm a estrutura sem o campo
     * "interno" (D062): escreve só o que cabe no tamanho pedido. */
    bool com_interno = c >= sizeof *inf;
    uint32_t tam = com_interno ? sizeof *inf : __builtin_offsetof(info_disco_t, interno);
    if (!usuario_ok(inf, tam, true)) return ERRO_ENDERECO;
    int k = 0;
    for (int i = 0; i < MAX_DISCOS; i++) {
        disco_t *d = &discos[i];
        if (!d->usado || !d->presente) continue;
        if (k++ != (int)indice) continue;
        txt_copia_n(inf->nome, d->nome, sizeof inf->nome);
        txt_copia_n(inf->fabricante, d->fabricante, sizeof inf->fabricante);
        txt_copia_n(inf->produto, d->produto, sizeof inf->produto);
        txt_copia_n(inf->formato, d->formato, sizeof inf->formato);
        txt_copia_n(inf->rotulo, d->rotulo, sizeof inf->rotulo);
        inf->setores = d->setores;
        inf->montado = d->montado;
        inf->suportado = d->fat32;
        if (com_interno) {
            inf->interno = d->interno;
            inf->origem = disco_eh_origem(d);
        }
        return 1;
    }
    return 0;
}

int32_t sis_montar(uint32_t pnome, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    char nome[16];
    int r = usuario_texto((const char *)(uintptr_t)pnome, nome, sizeof nome);
    if (r < 0) return r;
    disco_t *d = disco_por_nome(nome);
    return d ? disco_montar(d) : ERRO_NAO_EXISTE;
}

int32_t sis_desmontar(uint32_t pnome, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    char nome[16];
    int r = usuario_texto((const char *)(uintptr_t)pnome, nome, sizeof nome);
    if (r < 0) return r;
    disco_t *d = disco_por_nome(nome);
    if (!d) return ERRO_NAO_EXISTE;
    return disco_desmontar(d);
}

/* ------------------------------------------------------------ setores crus (D063)
 *
 * Ler e gravar setores direto no disco é o que um instalador precisa (tabela
 * de partições, cópia do sistema, formatar), e também o jeito mais fácil de
 * destruir os dados de quem testa. Por isso fica travado: só depois que a
 * pessoa digita o nome do disco numa caixa desenhada pelo kernel
 * (SIS_DISCO_LIBERAR), e só para o processo que pediu, até ele terminar. */

static bool liberado_para_atual(const disco_t *d)
{
    return d->liberado_pid != 0 && d->liberado_pid == atual->pid;
}

/* O disco de onde o sistema ligou: o único com a partição do SaguiOS e o
 * mesmo identificador do MBR que o estágio 2 leu no boot. Com dois iguais
 * (a mesma imagem gravada em dois discos), nenhum é a origem: melhor não
 * saber do que copiar do disco errado. */
bool disco_eh_origem(const disco_t *d)
{
    if (!d->usado || !d->presente || !d->tem_sistema || !d->id_mbr || d->id_mbr != info_boot.disco_id) return false;
    for (int i = 0; i < MAX_DISCOS; i++) {
        const disco_t *o = &discos[i];
        if (o != d && o->usado && o->presente && o->tem_sistema && o->id_mbr == d->id_mbr) return false;
    }
    return true;
}

/* SIS_DISCO_SETORES (&pedido) -> 0. De 1 a 128 setores de 512 bytes.
 *   - gravar: só no disco liberado para este processo e não montado (a FAT
 *     de um disco montado está na memória);
 *   - ler um disco interno: só liberado, exceto o que o instalador copia
 *     do disco de onde o sistema ligou (setores 0 e 1 e a partição 0xDA);
 *   - ler um pendrive: livre, como antes. */
int32_t sis_disco_setores(uint32_t pped, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    pedido_setores_t *p = (pedido_setores_t *)(uintptr_t)pped;
    if (!usuario_ok(p, sizeof *p, false)) return ERRO_ENDERECO;
    char nome[8];
    txt_copia_n(nome, p->nome, sizeof nome);
    uint32_t lba = p->lba, n = p->n, esc = p->escrever;
    void *buf = (void *)(uintptr_t)p->buf;
    disco_t *d = disco_por_nome(nome);
    if (!d || !d->presente) return ERRO_NAO_EXISTE;
    if (n == 0 || n > 128 || d->tam_setor != 512 || lba + n < lba || lba + n > d->setores) return ERRO_INVALIDO;
    if (!usuario_ok(buf, n * 512, !esc)) return ERRO_ENDERECO;
    bool liberado = liberado_para_atual(d);
    if (esc && !liberado) return ERRO_ACESSO;
    bool do_sistema = disco_eh_origem(d) && (lba + n <= 2 || (lba >= d->sistema_ini && lba + n <= d->sistema_fim));
    if (!esc && d->interno && !liberado && !do_sistema) return ERRO_ACESSO;
    if (esc && d->montado) return ERRO_OCUPADO;
    /* O buffer do programa pode não ser contíguo na memória física: passa
     * por um do kernel. */
    uint8_t *k = kmalloc(n * 512);
    if (!k) return ERRO_SEM_MEMORIA;
    int r;
    if (esc) { memcpy(k, buf, n * 512); r = d->escrever(d, lba, n, k); }
    else { r = d->ler(d, lba, n, k); if (r == 0) memcpy(buf, k, n * 512); }
    kfree(k);
    if (esc) klog("[DISCO] %s: pid %d gravou %u setor(es) em %u%s\n", d->nome, atual->pid, n, lba, r < 0 ? " (erro)" : "");
    return r < 0 ? ERRO_ES : 0;
}

/* SIS_DISCO_RELER (nome) -> 0: reconhece de novo o formato do disco
 * liberado, depois que o programa gravou uma tabela nova. */
int32_t sis_disco_reler(uint32_t pnome, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    char nome[8];
    int r = usuario_texto((const char *)(uintptr_t)pnome, nome, sizeof nome);
    if (r < 0) return r;
    disco_t *d = disco_por_nome(nome);
    if (!d || !d->presente) return ERRO_NAO_EXISTE;
    if (!liberado_para_atual(d)) return ERRO_ACESSO;
    if (d->montado) return ERRO_OCUPADO;
    d->fat32 = d->de_boot = d->tem_sistema = false;
    d->part_inicio = d->sistema_ini = d->sistema_fim = d->id_mbr = 0;
    d->rotulo[0] = 0;
    reconhecer(d);
    klog("[DISCO] %s relido: %s, rotulo \"%s\"\n", d->nome, d->formato, d->rotulo);
    return 0;
}

void discos_processo_terminou(int pid)
{
    if (!pid) return;
    for (int i = 0; i < MAX_DISCOS; i++)
        if (discos[i].liberado_pid == pid) {
            discos[i].liberado_pid = 0;
            klog("[DISCO] %s: liberacao do processo %d acabou\n", discos[i].nome, pid);
        }
}

/* Copia até "max" letras de "t" para "l", cortando com "..." se não couber
 * na largura da caixa. */
static void linha_curta(char *l, size_t tam, const char *t, size_t max)
{
    size_t n = txt_tamanho(t);
    if (max + 1 > tam) max = tam - 1;
    if (n <= max) { txt_copia_n(l, t, n + 1); return; }
    txt_copia_n(l, t, max - 2);
    txt_copia_n(l + max - 3, "...", 4);
}

/* SIS_DISCO_LIBERAR (nome, resumo) -> 0 se a pessoa confirmou digitando o
 * nome do disco na caixa do kernel; ERRO_ACESSO se cancelou. O resumo é o
 * que o programa diz que vai fazer (até 4 linhas, separadas por '\n'): a
 * caixa mostra ele separado do que o próprio kernel sabe do disco (nome,
 * modelo, tamanho e o que há nele hoje). Vale só para esse disco, só para
 * quem pediu e só até ele terminar (D063). */
int32_t sis_disco_liberar(uint32_t pnome, uint32_t presumo, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)c; (void)dd; (void)e;
    char nome[8];
    int r = usuario_texto((const char *)(uintptr_t)pnome, nome, sizeof nome);
    if (r < 0) return r;
    disco_t *d = disco_por_nome(nome);
    if (!d || !d->presente) return ERRO_NAO_EXISTE;
    if (!atual->pid) return ERRO_ACESSO;
    if (liberado_para_atual(d)) return 0;
    int res = gui_confirmacao_resultado(atual->pid, d->nome);
    if (res == 1) {
        d->liberado_pid = atual->pid;
        klog("[DISCO] %s liberado para o processo %d (%s)\n", d->nome, atual->pid, atual->nome);
        return 0;
    }
    if (res == -1) return ERRO_ACESSO;
    if (res == -2) {
        char resumo[CONF_RESUMO_MAX];
        resumo[0] = 0;
        if (presumo) {
            r = usuario_texto((const char *)(uintptr_t)presumo, resumo, sizeof resumo);
            if (r < 0) return r;
        }
        static char l[CONF_LINHAS][CONF_COLUNAS + 1];
        const char *linhas[CONF_LINHAS];
        int k = 0;
        char t[160], tam[24];
        tamanho_legivel(d->setores, tam, sizeof tam);
        snformatar(t, sizeof t, "O programa \"%s\" pediu para gravar direto no disco:", atual->nome);
        linha_curta(l[k++], sizeof l[0], t, CONF_COLUNAS);
        snformatar(t, sizeof t, "%s, %s: %s %s, %s", d->nome, d->interno ? "disco interno" : "pendrive USB", d->fabricante, d->produto, tam);
        linha_curta(l[k++], sizeof l[0], t, CONF_COLUNAS);
        snformatar(t, sizeof t, "O que tem nele hoje: %s%s%s%s", d->formato, d->rotulo[0] ? " \"" : "", d->rotulo, d->rotulo[0] ? "\"" : "");
        linha_curta(l[k++], sizeof l[0], t, CONF_COLUNAS);
        tabela_disco_t *tab = kmalloc(sizeof *tab);       /* o que o kernel leu da tabela (D066) */
        if (tab && tabela_ler(d, tab) == 0) {
            tabela_resumo(tab, t, sizeof t);
            linha_curta(l[k++], sizeof l[0], t, CONF_COLUNAS);
        }
        if (tab) kfree(tab);
        if (resumo[0]) {
            linha_curta(l[k++], sizeof l[0], "O programa diz que vai:", CONF_COLUNAS);
            const char *q = resumo;
            for (int n = 0; *q && n < 4; n++) {
                char um[CONF_RESUMO_MAX];
                size_t m = 0;
                while (q[m] && q[m] != '\n') m++;
                txt_copia_n(um, q, m + 1);
                snformatar(t, sizeof t, "  - %s", um);
                linha_curta(l[k++], sizeof l[0], t, CONF_COLUNAS);
                q += m;
                if (*q == '\n') q++;
            }
        }
        snformatar(t, sizeof t, "Tudo o que está em %s pode ser apagado.", d->nome);
        linha_curta(l[k++], sizeof l[0], t, CONF_COLUNAS);
        for (int i = 0; i < k; i++) linhas[i] = l[i];
        if (!gui_confirmar_disco(atual->pid, d->nome, linhas, k)) return gui_ativa() ? ERRO_OCUPADO : ERRO_ACESSO;
    }
    bloquear(gui_confirmacao_canal());   /* a caixa acorda quem pediu ao fechar */
    return SIS_REPETIR;
}
