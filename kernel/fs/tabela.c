/* tabela.c - a tabela de partições de um disco, lida e conferida (D066).
 *
 * O instalador decide onde gravar a partir do que há no disco. Se ele ler
 * errado, grava por cima de uma partição de outra pessoa. Por isso a
 * leitura fica aqui, no kernel, e é desconfiada:
 *   - o setor 0 só é tabela MBR se tem a assinatura 0x55AA, não é o setor
 *     de boot de um volume (FAT, NTFS, exFAT gravados no disco inteiro) e
 *     cada entrada tem o byte de status 0x00 ou 0x80;
 *   - GPT é reconhecida pela entrada 0xEE e também pelo cabeçalho "EFI PART"
 *     no setor 1 ou no último setor (GPT sem o MBR protetor);
 *   - as partições lógicas são lidas seguindo a cadeia de EBRs da estendida;
 *   - as entradas são devolvidas como estão: nada é cortado nem descartado.
 * Qualquer coisa estranha (entrada sem tamanho, antes do setor 2, passando
 * do fim do disco, sobreposta a outra, lógica fora da estendida, cadeia em
 * laço) marca a tabela como não conferida, com o primeiro problema achado.
 * Com uma tabela não conferida, o instalador não oferece o espaço livre.
 *
 * De onde veio: a especificação UEFI (capítulo 5, "GUID Partition Table":
 * o MBR legado, o MBR protetor com a entrada 0xEE e o cabeçalho "EFI PART")
 * e a especificação FAT32 da Microsoft (o BPB do setor de boot de um
 * volume). As partições lógicas seguem a convenção do MBR estendido: cada
 * EBR traz a lógica com o início relativo a ele e o próximo EBR com o
 * início relativo à estendida. */
#include "disco.h"
#include "abi.h"
#include "console.h"
#include "memoria.h"
#include "processo.h"
#include "sistema.h"
#include "texto.h"

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static bool eh_estendida(uint32_t tipo) { return tipo == 0x05 || tipo == 0x0F || tipo == 0x85; }

static bool potencia_de_2(uint32_t v) { return v && !(v & (v - 1)); }

/* O nome do volume se o setor é o setor de boot de um (VBR); NULL se não.
 * O código de boot de um MBR também pode começar com um salto (0xEB), então
 * a conferência é pelos campos do BPB e pela assinatura do formato. */
static const char *volume(const uint8_t *s)
{
    if (s[510] != 0x55 || s[511] != 0xAA) return NULL;
    if (memcmp(s + 3, "NTFS    ", 8) == 0) return "NTFS";
    if (memcmp(s + 3, "EXFAT   ", 8) == 0) return "exFAT";
    uint32_t bps = (uint32_t)s[11] | (uint32_t)s[12] << 8, reservados = (uint32_t)s[14] | (uint32_t)s[15] << 8;
    bool bpb = (bps == 512 || bps == 1024 || bps == 2048 || bps == 4096) && potencia_de_2(s[13]) &&
               reservados >= 1 && (s[16] == 1 || s[16] == 2);
    if (bpb && memcmp(s + 82, "FAT32", 5) == 0) return "FAT32";
    if (bpb && memcmp(s + 54, "FAT1", 4) == 0) return "FAT12/16";
    return NULL;
}

static void problema(tabela_disco_t *t, const char *fmt, uint32_t a, uint32_t b)
{
    if (!t->conferida) return;              /* fica o primeiro */
    t->conferida = 0;
    snformatar(t->problema, sizeof t->problema, fmt, a, b);
}

static particao_t *nova(tabela_disco_t *t)
{
    if (t->n >= TABELA_MAX) { problema(t, "partições demais (mais de %u)", TABELA_MAX, 0); return NULL; }
    particao_t *p = &t->p[t->n++];
    memset(p, 0, sizeof *p);
    return p;
}

static void ver_fim(tabela_disco_t *t, const disco_t *d, uint32_t entrada, uint32_t ini, uint32_t n)
{
    uint64_t fim = (uint64_t)ini + n;
    if (fim > t->maior_fim) t->maior_fim = fim > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)fim;
    if (!n) problema(t, "a entrada %u não tem tamanho", entrada, 0);
    else if (ini < 2) problema(t, "a entrada %u começa no setor %u, antes do 2", entrada, ini);
    else if (fim > d->setores) problema(t, "a entrada %u passa do fim do disco", entrada, 0);
}

/* As lógicas: a cadeia de EBRs dentro da estendida [base, base + tam). Em
 * cada EBR, a primeira entrada é a lógica (relativa ao próprio EBR) e a
 * segunda aponta o próximo EBR (relativa ao início da estendida). */
static void ler_logicas(disco_t *d, tabela_disco_t *t, uint8_t *s, uint32_t base, uint32_t tam)
{
    uint64_t ext_fim = (uint64_t)base + tam;
    uint32_t l = base;
    for (int i = 0; i < 64; i++) {
        uint32_t num = 5 + (uint32_t)i;
        if (l >= d->setores || l >= ext_fim) { problema(t, "o EBR %u fica fora da estendida", num, 0); return; }
        if (d->ler(d, l, 1, s) < 0) { problema(t, "não deu para ler o EBR no setor %u", l, 0); return; }
        if (s[510] != 0x55 || s[511] != 0xAA) { problema(t, "o EBR no setor %u não tem a assinatura", l, 0); return; }
        for (int k = 478; k < 510; k++)
            if (s[k]) { problema(t, "o EBR no setor %u tem entradas a mais", l, 0); break; }
        const uint8_t *e = s + 446, *prox = s + 462;
        if (e[4]) {
            /* O início é relativo ao EBR: a soma em 64 bits, para um valor
             * enorme não dar a volta e cair "dentro" da estendida. */
            uint64_t ini64 = (uint64_t)l + le32(e + 8);
            uint32_t ini = (uint32_t)ini64, n = le32(e + 12);
            particao_t *p = nova(t);
            if (!p) return;
            p->entrada = num;
            p->tipo = e[4];
            p->ativa = e[0] == 0x80;
            p->inicio = ini;
            p->setores = n;
            if (e[0] != 0 && e[0] != 0x80) problema(t, "a lógica %u tem o status 0x%x", num, e[0]);
            if (le32(e + 8) == 0 || ini64 + n > ext_fim || ini64 < l) problema(t, "a lógica %u sai do seu lugar na estendida", num, 0);
            ver_fim(t, d, num, ini, n);
        }
        if (!prox[4]) return;
        if (!eh_estendida(prox[4])) { problema(t, "o EBR no setor %u aponta para o tipo 0x%x", l, prox[4]); return; }
        uint64_t seg64 = (uint64_t)base + le32(prox + 8);
        uint32_t seg = (uint32_t)seg64;
        if (seg64 <= l || seg64 >= ext_fim) { problema(t, "a cadeia das lógicas volta para trás no setor %u", l, 0); return; }
        l = seg;
    }
    problema(t, "a cadeia das lógicas tem mais de %u EBRs", 64, 0);
}

static bool sobrepoe(const particao_t *a, const particao_t *b)
{
    return (uint64_t)a->inicio < (uint64_t)b->inicio + b->setores && (uint64_t)b->inicio < (uint64_t)a->inicio + a->setores;
}

/* O formato de dentro de cada partição, pelo setor de boot (ou, no ext, pelo
 * superbloco, 1024 bytes depois do início). Só lê o que cabe no disco. */
static void formatos(disco_t *d, tabela_disco_t *t, uint8_t *s)
{
    for (uint32_t i = 0; i < t->n; i++) {
        particao_t *p = &t->p[i];
        if (eh_estendida(p->tipo) || p->setores < 3 || (uint64_t)p->inicio + p->setores > d->setores) continue;
        const char *v = d->ler(d, p->inicio, 1, s) == 0 ? volume(s) : NULL;
        if (!v && d->ler(d, p->inicio + 2, 1, s) == 0 && s[56] == 0x53 && s[57] == 0xEF) v = "ext";
        if (v) txt_copia_n(p->formato, v, sizeof p->formato);
    }
}

int tabela_ler(disco_t *d, tabela_disco_t *t)
{
    memset(t, 0, sizeof *t);
    t->conferida = 1;
    uint8_t *s = kmalloc(512);
    if (!s) return ERRO_SEM_MEMORIA;
    bool gpt = false;
    if (d->setores > 2 && d->ler(d, 1, 1, s) == 0 && memcmp(s, "EFI PART", 8) == 0) gpt = true;
    if (!gpt && d->setores > 2 && d->ler(d, d->setores - 1, 1, s) == 0 && memcmp(s, "EFI PART", 8) == 0) gpt = true;
    if (d->ler(d, 0, 1, s) < 0) {
        t->estado = TABELA_ERRO;
        problema(t, "não deu para ler o setor 0", 0, 0);
        kfree(s);
        return 0;
    }
    bool assinado = s[510] == 0x55 && s[511] == 0xAA, zerado = true;
    for (int i = 0; i < 512 && zerado; i++) zerado = s[i] == 0;
    if (assinado) {
        t->id_mbr = le32(s + 440);
        for (int k = 0; k < 4; k++) if (s[446 + k * 16 + 4] == 0xEE && !volume(s)) gpt = true;
    }
    const char *v = volume(s);
    if (gpt) {
        t->estado = TABELA_GPT;
        problema(t, assinado && !v ? "GPT: o SaguiOS ainda não mexe em discos GPT" : "GPT sem a entrada protetora 0xEE no setor 0", 0, 0);
    } else if (zerado) {
        t->estado = TABELA_VAZIO;
    } else if (!assinado) {
        t->estado = TABELA_DESCONHECIDA;
        problema(t, "o setor 0 não tem a assinatura 0x55AA", 0, 0);
    } else if (v) {
        t->estado = TABELA_VBR;
        particao_t *p = nova(t);
        p->setores = d->setores;               /* entrada 0: o disco inteiro */
        txt_copia_n(p->formato, v, sizeof p->formato);
        problema(t, "o setor 0 é um volume, sem tabela de partições", 0, 0);
        t->maior_fim = d->setores;
    } else {
        t->estado = TABELA_MBR;
        int estendidas = 0;
        uint8_t m[64];
        memcpy(m, s + 446, 64);                /* "s" vai ser reusado para os EBRs */
        for (uint32_t k = 0; k < 4; k++) {
            const uint8_t *e = m + k * 16;
            if (!e[4]) continue;
            particao_t *p = nova(t);
            p->entrada = k + 1;
            p->tipo = e[4];
            p->ativa = e[0] == 0x80;
            p->inicio = le32(e + 8);
            p->setores = le32(e + 12);
            if (e[0] != 0 && e[0] != 0x80) problema(t, "a entrada %u tem o status 0x%x", k + 1, e[0]);
            ver_fim(t, d, k + 1, p->inicio, p->setores);
            if (eh_estendida(e[4]) && ++estendidas > 1) problema(t, "há mais de uma partição estendida", 0, 0);
        }
        uint32_t primarias = t->n;
        for (uint32_t i = 0; i < primarias; i++)
            if (eh_estendida(t->p[i].tipo) && t->p[i].setores) { ler_logicas(d, t, s, t->p[i].inicio, t->p[i].setores); break; }
        /* Sobreposição: as primárias (a estendida inclusive) entre si e as
         * lógicas entre si; uma lógica fora da estendida já foi acusada. */
        for (uint32_t i = 0; i < t->n; i++)
            for (uint32_t j = i + 1; j < t->n; j++) {
                bool pi = t->p[i].entrada <= 4, pj = t->p[j].entrada <= 4;
                if (pi == pj && sobrepoe(&t->p[i], &t->p[j]))
                    problema(t, "as partições %u e %u se sobrepõem", t->p[i].entrada, t->p[j].entrada);
            }
        formatos(d, t, s);
    }
    kfree(s);
    return 0;
}

/* Uma linha para a caixa de confirmação: o que o próprio kernel leu. */
void tabela_resumo(const tabela_disco_t *t, char *l, size_t tam)
{
    uint32_t logicas = 0;
    for (uint32_t i = 0; i < t->n; i++) if (t->p[i].entrada >= 5) logicas++;
    if (!t->conferida) snformatar(l, tam, "Tabela com problema: %s", t->problema);
    else if (t->estado == TABELA_VAZIO) txt_copia_n(l, "Tabela: nenhuma (setor 0 zerado)", tam);
    else if (logicas) snformatar(l, tam, "Tabela MBR: %u partições (%u lógicas)", t->n, logicas);
    else snformatar(l, tam, "Tabela MBR: %u partições", t->n, 0);
}

/* SIS_DISCO_TABELA (nome, &tabela, tamanho) -> 0. Não precisa de
 * liberação: lê só a tabela e o primeiro setor de cada partição. */
int32_t sis_disco_tabela(uint32_t pnome, uint32_t ptab, uint32_t tam, uint32_t dd, uint32_t e)
{
    (void)dd; (void)e;
    char nome[8];
    int r = usuario_texto((const char *)pnome, nome, sizeof nome);
    if (r < 0) return r;
    if (tam < sizeof(tabela_disco_t)) return ERRO_INVALIDO;
    if (!usuario_ok((void *)ptab, sizeof(tabela_disco_t), true)) return ERRO_ENDERECO;
    disco_t *d = disco_por_nome(nome);
    if (!d || !d->presente) return ERRO_NAO_EXISTE;
    if (d->tam_setor != 512) return ERRO_INVALIDO;
    tabela_disco_t *k = kmalloc(sizeof *k);
    if (!k) return ERRO_SEM_MEMORIA;
    r = tabela_ler(d, k);
    if (r == 0) memcpy((void *)ptab, k, sizeof *k);
    kfree(k);
    return r;
}
