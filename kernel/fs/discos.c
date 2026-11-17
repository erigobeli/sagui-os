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
        for (int p = 0; p < 4 && !d->fat32; p++) {
            const uint8_t *e = s + 446 + p * 16;
            uint8_t tipo = e[4];
            uint32_t ini = (uint32_t)e[8] | (uint32_t)e[9] << 8 | (uint32_t)e[10] << 16 | (uint32_t)e[11] << 24;
            if (!tipo || (e[0] != 0 && e[0] != 0x80)) continue;
            if (tipo == 0x0B || tipo == 0x0C) {
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
static bool montar_se_for_do_saguios(disco_t *d)
{
    if (disco_montado()) return false;
    if (d->interno && txt_compara(d->rotulo, "SAGUIOS") != 0) return false;   /* D062 */
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
        } else if (d->interno) ;  /* outros discos internos: sem aviso de "pendrive conectado" */
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
    /* Disco interno: só a partição que o instalador criou (rótulo SAGUIOS,
     * com .saguios/configuracoes.txt). Qualquer outra FAT32 do HD é de quem
     * testa (o Windows, por exemplo) e não é montada (D062). */
    if (d->interno && txt_compara(d->rotulo, "SAGUIOS") != 0) {
        klog("[DISCO] %s: disco interno sem a particao do SaguiOS, nao monta\n", d->nome);
        return ERRO_ACESSO;
    }
    int r = fat_montar(d);
    if (r < 0) return r;
    if (d->interno && !config_tem_arquivo() && d->liberado_pid != atual->pid) {
        fat_desmontar(false);
        klog("[DISCO] %s: disco interno sem a particao do SaguiOS, nao monta\n", d->nome);
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
    info_disco_t *inf = (info_disco_t *)pinfo;
    /* Programas antigos passam 0 em c e têm a estrutura sem o campo
     * "interno" (D062): escreve só o que cabe no tamanho pedido. */
    bool com_interno = c >= sizeof *inf;
    uint32_t tam = com_interno ? sizeof *inf : sizeof *inf - sizeof inf->interno;
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

/* Setores crus, para o instalador (tabela de partições, sistema e
 * formatação da FAT32). Ler e gravar de 1 a 128 setores; gravar num disco
 * montado é recusado, porque a FAT dele está na memória. */
int32_t sis_disco_setores(uint32_t pped, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    pedido_setores_t *p = (pedido_setores_t *)pped;
    if (!usuario_ok(p, sizeof *p, false)) return ERRO_ENDERECO;
    char nome[8];
    txt_copia_n(nome, p->nome, sizeof nome);
    uint32_t lba = p->lba, n = p->n, esc = p->escrever;
    void *buf = p->buf;
    disco_t *d = disco_por_nome(nome);
    if (!d || !d->presente) return ERRO_NAO_EXISTE;
    if (n == 0 || n > 128 || d->tam_setor != 512 || lba + n < lba || lba + n > d->setores) return ERRO_INVALIDO;
    if (!usuario_ok(buf, n * 512, !esc)) return ERRO_ENDERECO;
    /* Só depois da confirmação (SIS_DISCO_LIBERAR, D063): gravar em
     * qualquer disco e ler um disco interno. Sem ela, só dá para ler a
     * partição do sistema do disco de onde o SaguiOS ligou (o que o
     * instalador copia), e os pendrives. */
    bool liberado = d->liberado_pid == atual->pid;
    if (esc && !liberado) return ERRO_ACESSO;
    if (!esc && d->interno && !liberado && !(disco_eh_origem(d) && lba + n <= d->sistema_fim)) return ERRO_ACESSO;
    if (esc && d->montado) return ERRO_OCUPADO;
    /* O buffer do programa pode não ser contíguo na memória física: passa
     * por um do kernel. */
    uint8_t *k = kmalloc(n * 512);
    if (!k) return ERRO_SEM_MEMORIA;
    int r;
    if (esc) { memcpy(k, buf, n * 512); r = d->escrever(d, lba, n, k); }
    else { r = d->ler(d, lba, n, k); if (r == 0) memcpy(buf, k, n * 512); }
    kfree(k);
    return r < 0 ? ERRO_ES : 0;
}

int32_t sis_disco_reler(uint32_t pnome, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    char nome[8];
    int r = usuario_texto((const char *)pnome, nome, sizeof nome);
    if (r < 0) return r;
    disco_t *d = disco_por_nome(nome);
    if (!d || !d->presente) return ERRO_NAO_EXISTE;
    if (d->liberado_pid != atual->pid) return ERRO_ACESSO;
    if (d->montado) return ERRO_OCUPADO;
    d->fat32 = d->de_boot = d->tem_sistema = false;
    d->part_inicio = d->sistema_fim = 0;
    d->rotulo[0] = 0;
    reconhecer(d);
    klog("[DISCO] %s relido: %s, rotulo \"%s\"\n", d->nome, d->formato, d->rotulo);
    return 0;
}

/* O disco de onde o sistema ligou: o único com a partição do SaguiOS e o
 * mesmo identificador do MBR que o estágio 2 leu no boot. Com dois iguais
 * (a mesma imagem gravada em dois discos), nenhum é a origem: melhor não
 * saber do que copiar do disco errado. */
bool disco_eh_origem(const disco_t *d)
{
    if (!d->usado || !d->presente || !d->tem_sistema || d->id_mbr != info_boot.disco_id) return false;
    for (int i = 0; i < MAX_DISCOS; i++) {
        const disco_t *o = &discos[i];
        if (o != d && o->usado && o->presente && o->tem_sistema && o->id_mbr == d->id_mbr) return false;
    }
    return true;
}

void discos_processo_terminou(int pid)
{
    for (int i = 0; i < MAX_DISCOS; i++)
        if (discos[i].liberado_pid == pid) {
            discos[i].liberado_pid = 0;
            klog("[DISCO] %s: liberacao do processo %d acabou\n", discos[i].nome, pid);
        }
}

/* SIS_DISCO_LIBERAR (nome) -> 0 se a pessoa confirmou digitando o nome do
 * disco na caixa do kernel; ERRO_ACESSO se cancelou. Vale só para esse
 * disco, só para quem pediu e só até ele terminar (D063). */
int32_t sis_disco_liberar(uint32_t pnome, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    char nome[8];
    int r = usuario_texto((const char *)pnome, nome, sizeof nome);
    if (r < 0) return r;
    disco_t *d = disco_por_nome(nome);
    if (!d || !d->presente) return ERRO_NAO_EXISTE;
    if (d->liberado_pid == atual->pid) return 0;
    int res = gui_confirmacao_resultado(atual->pid);
    if (res == 1) {
        d->liberado_pid = atual->pid;
        klog("[DISCO] %s liberado para o processo %d (%s)\n", d->nome, atual->pid, atual->nome);
        return 0;
    }
    if (res == -1) return ERRO_ACESSO;
    if (res == -2) {
        char tam[24], l[5][88];
        tamanho_legivel(d->setores, tam, sizeof tam);
        snformatar(l[0], sizeof l[0], "O programa \"%s\" pediu para ler e gravar setores do disco:", atual->nome);
        snformatar(l[1], sizeof l[1], "%s, %s: %s %s, %s", d->nome, d->interno ? "disco interno" : "pendrive USB", d->fabricante, d->produto, tam);
        snformatar(l[2], sizeof l[2], "O que tem nele hoje: %s%s%s%s", d->formato, d->rotulo[0] ? " \"" : "", d->rotulo, d->rotulo[0] ? "\"" : "");
        l[3][0] = 0;
        snformatar(l[4], sizeof l[4], "Tudo o que está em %s pode ser apagado.", d->nome);
        const char *const linhas[5] = { l[0], l[1], l[2], l[3], l[4] };
        if (!gui_confirmar_disco(atual->pid, d->nome, linhas)) return gui_ativa() ? ERRO_OCUPADO : ERRO_ACESSO;
    }
    bloquear(gui_confirmacao_canal());   /* a caixa acorda quem pediu ao fechar */
    return SIS_REPETIR;
}

int32_t sis_montar(uint32_t pnome, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    char nome[16];
    int r = usuario_texto((const char *)pnome, nome, sizeof nome);
    if (r < 0) return r;
    disco_t *d = disco_por_nome(nome);
    return d ? disco_montar(d) : ERRO_NAO_EXISTE;
}

int32_t sis_desmontar(uint32_t pnome, uint32_t b, uint32_t c, uint32_t dd, uint32_t e)
{
    (void)b; (void)c; (void)dd; (void)e;
    char nome[16];
    int r = usuario_texto((const char *)pnome, nome, sizeof nome);
    if (r < 0) return r;
    disco_t *d = disco_por_nome(nome);
    if (!d) return ERRO_NAO_EXISTE;
    return disco_desmontar(d);
}
