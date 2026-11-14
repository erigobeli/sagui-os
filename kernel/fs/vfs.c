/* vfs.c - camada de arquivos virtual com a camada gravável na RAM (H14).
 *
 * No boot, o SGFS (somente leitura) vira uma árvore de nós na memória:
 * cada pasta tem uma lista de filhos. Um arquivo começa apontando para os
 * bytes originais dentro do ramdisk; na primeira escrita ganha uma cópia
 * própria no heap do kernel ("cópia na escrita"). Criar, apagar e renomear
 * mexem só na árvore. O ramdisk nunca é alterado; ao desligar, tudo o que
 * foi mudado se perde (PRD 4.3), como num sistema "live".
 *
 * A partir da fase 6, um ramo da árvore (/usb) pode pertencer a outro
 * sistema de arquivos (FAT32); este arquivo é o lugar onde os caminhos vão
 * ser desviados para ele. */
#include "vfs.h"
#include "console.h"
#include "memoria.h"
#include "sgfs.h"
#include "texto.h"
#include "fat32.h"

static no_t raiz;
static uint32_t total_nos;

static no_t *novo_no(no_t *pai, const char *nome, size_t n, uint8_t tipo)
{
    no_t *x = kcalloc(sizeof(no_t));
    if (!x) return NULL;
    if (n >= NOME_MAX) n = NOME_MAX - 1;
    memcpy(x->nome, nome, n);
    x->nome[n] = 0;
    x->tipo = tipo;
    x->pai = pai;
    /* Insere em ordem alfabética: o ls sai ordenado sem precisar ordenar. */
    no_t **p = &pai->filho;
    while (*p && txt_compara((*p)->nome, x->nome) < 0)
        p = &(*p)->irmao;
    x->irmao = *p;
    *p = x;
    total_nos++;
    return x;
}

static no_t *filho_chamado(no_t *pasta, const char *nome, size_t n)
{
    for (no_t *f = pasta->filho; f; f = f->irmao)
        if (txt_tamanho(f->nome) == n && txt_compara_n(f->nome, nome, n) == 0)
            return f;
    return NULL;
}

void vfs_iniciar(void)
{
    memset(&raiz, 0, sizeof raiz);
    raiz.tipo = TIPO_PASTA;
    for (uint32_t i = 0; i < sgfs_quantidade(); i++) {
        const sgfs_entrada_t *e = sgfs_entrada(i);
        /* Percorre o caminho criando as pastas intermediárias que faltarem. */
        no_t *atual = &raiz;
        const char *c = e->caminho;
        while (*c) {
            const char *fim = c;
            while (*fim && *fim != '/') fim++;
            bool ultimo = !*fim;
            no_t *x = filho_chamado(atual, c, (size_t)(fim - c));
            if (!x) {
                uint8_t tipo = ultimo ? (e->tipo == SGFS_PASTA ? TIPO_PASTA :
                                         e->tipo == SGFS_PROGRAMA ? TIPO_PROGRAMA : TIPO_ARQUIVO) : TIPO_PASTA;
                x = novo_no(atual, c, (size_t)(fim - c), tipo);
                if (!x) return;
                if (ultimo && tipo != TIPO_PASTA) {
                    x->tamanho = e->tamanho;
                    x->original = e->tamanho ? sgfs_dados(e) : NULL;
                }
            }
            atual = x;
            c = ultimo ? fim : fim + 1;
        }
    }
    klog("[VFS] %u nos montados a partir do SGFS\n", total_nos);
}

no_t *vfs_raiz(void) { return &raiz; }

/* Caminho absoluto já simplificado (ver caminho_resolver). */
no_t *vfs_achar(const char *caminho)
{
    no_t *atual = &raiz;
    const char *c = caminho;
    while (*c == '/') c++;
    while (*c) {
        const char *fim = c;
        while (*fim && *fim != '/') fim++;
        if (atual->tipo != TIPO_PASTA) return NULL;
        atual = filho_chamado(atual, c, (size_t)(fim - c));
        if (!atual) return NULL;
        c = *fim ? fim + 1 : fim;
    }
    return atual;
}

/* Separa "/a/b/c" em pasta "/a/b" (achada) e nome "c". */
static int separar(const char *caminho, no_t **pasta, const char **nome)
{
    const char *ultima = NULL;
    for (const char *p = caminho; *p; p++)
        if (*p == '/') ultima = p;
    if (!ultima || !ultima[1]) return ERRO_INVALIDO;   /* raiz ou terminado em "/" */
    if (txt_tamanho(ultima + 1) >= NOME_MAX) return ERRO_INVALIDO;
    char pai[CAMINHO_MAX];
    size_t n = (size_t)(ultima - caminho);
    if (n >= sizeof pai) return ERRO_INVALIDO;
    memcpy(pai, caminho, n);
    pai[n] = 0;
    *pasta = vfs_achar(n ? pai : "/");
    if (!*pasta) return ERRO_NAO_EXISTE;
    if ((*pasta)->tipo != TIPO_PASTA) return ERRO_NAO_E_PASTA;
    *nome = ultima + 1;
    return 0;
}

int vfs_criar(const char *caminho, uint8_t tipo, no_t **saida)
{
    no_t *pasta;
    const char *nome;
    int r = separar(caminho, &pasta, &nome);
    if (r < 0) return r;
    if (filho_chamado(pasta, nome, txt_tamanho(nome))) return ERRO_JA_EXISTE;
    no_t *x = novo_no(pasta, nome, txt_tamanho(nome), tipo);
    if (!x) return ERRO_SEM_MEMORIA;
    if (saida) *saida = x;
    return 0;
}

uint32_t vfs_ler(no_t *x, uint32_t desloc, void *destino, uint32_t n)
{
    if (x->tipo == TIPO_PASTA || desloc >= x->tamanho) return 0;
    if (n > x->tamanho - desloc) n = x->tamanho - desloc;
    const uint8_t *fonte = x->dados ? x->dados : x->original;
    if (fonte) memcpy(destino, fonte + desloc, n);
    else memset(destino, 0, n);
    return n;
}

/* Garante uma cópia própria com pelo menos 'cap' bytes (cópia na escrita). */
static bool garantir_capacidade(no_t *x, uint32_t cap)
{
    if (x->dados && x->capacidade >= cap) return true;
    uint32_t nova = x->capacidade ? x->capacidade : 256;
    while (nova < cap) nova *= 2;
    uint8_t *d = kmalloc(nova);
    if (!d) return false;
    const uint8_t *velho = x->dados ? x->dados : x->original;
    if (velho && x->tamanho) memcpy(d, velho, x->tamanho);
    if (x->dados) kfree(x->dados);
    x->dados = d;
    x->capacidade = nova;
    x->original = NULL;
    return true;
}

int vfs_escrever(no_t *x, uint32_t desloc, const void *origem, uint32_t n)
{
    if (x->tipo == TIPO_PASTA) return ERRO_E_PASTA;
    if (n == 0) return 0;
    if (desloc > 64u * 1024 * 1024 || n > 64u * 1024 * 1024) return ERRO_SEM_MEMORIA;
    if (!garantir_capacidade(x, desloc + n)) return ERRO_SEM_MEMORIA;
    if (desloc > x->tamanho)   /* escrever depois do fim: o buraco vira zeros */
        memset(x->dados + x->tamanho, 0, desloc - x->tamanho);
    memcpy(x->dados + desloc, origem, n);
    if (desloc + n > x->tamanho) x->tamanho = desloc + n;
    return (int)n;
}

void vfs_truncar(no_t *x)
{
    x->tamanho = 0;
    x->original = NULL;
}

static void destruir_no(no_t *x)
{
    if (x->dados) kfree(x->dados);
    kfree(x);
    total_nos--;
}

/* Tira o nó da lista de filhos do pai. */
static void desligar(no_t *x)
{
    no_t **p = &x->pai->filho;
    while (*p && *p != x) p = &(*p)->irmao;
    if (*p) *p = x->irmao;
    x->irmao = NULL;
}

int vfs_apagar(const char *caminho)
{
    no_t *x = vfs_achar(caminho);
    if (!x) return ERRO_NAO_EXISTE;
    if (x->tipo == TIPO_PASTA) return ERRO_E_PASTA;
    desligar(x);
    /* Arquivo aberto por alguém: some da pasta já, mas os dados ficam até o
     * último fechar (vfs_soltar). */
    if (x->refs) x->removido = true;
    else destruir_no(x);
    return 0;
}

int vfs_remover_pasta(const char *caminho)
{
    no_t *x = vfs_achar(caminho);
    if (!x) return ERRO_NAO_EXISTE;
    if (x == &raiz) return ERRO_ACESSO;
    if (x->tipo != TIPO_PASTA) return ERRO_NAO_E_PASTA;
    if (x->filho) return ERRO_NAO_VAZIA;
    desligar(x);
    destruir_no(x);
    return 0;
}

int vfs_renomear(const char *origem, const char *destino)
{
    no_t *x = vfs_achar(origem);
    if (!x) return ERRO_NAO_EXISTE;
    if (x == &raiz) return ERRO_ACESSO;
    char final[CAMINHO_MAX];
    txt_copia_n(final, destino, sizeof final);
    no_t *d = vfs_achar(destino);
    if (d && d->tipo == TIPO_PASTA) {
        /* "mv a.txt pasta" = mover para dentro da pasta, com o mesmo nome. */
        size_t n = txt_tamanho(final);
        if (n + 1 + txt_tamanho(x->nome) + 1 > sizeof final) return ERRO_INVALIDO;
        if (n > 1) final[n++] = '/';
        txt_copia_n(final + n, x->nome, sizeof final - n);
        d = vfs_achar(final);
    }
    if (d == x) return 0;
    if (d) {
        if (d->tipo == TIPO_PASTA) return ERRO_JA_EXISTE;
        vfs_apagar(final);   /* destino é arquivo: é substituído */
    }
    no_t *pasta;
    const char *nome;
    int r = separar(final, &pasta, &nome);
    if (r < 0) return r;
    for (no_t *p = pasta; p; p = p->pai)   /* pasta não pode ir para dentro dela mesma */
        if (p == x) return ERRO_INVALIDO;
    desligar(x);
    txt_copia_n(x->nome, nome, NOME_MAX);
    x->pai = pasta;
    no_t **p = &pasta->filho;
    while (*p && txt_compara((*p)->nome, x->nome) < 0) p = &(*p)->irmao;
    x->irmao = *p;
    *p = x;
    return 0;
}

int vfs_listar(const char *caminho, int indice, entrada_pasta_t *e)
{
    no_t *x = vfs_achar(caminho);
    if (!x) return ERRO_NAO_EXISTE;
    if (x->tipo != TIPO_PASTA) return ERRO_NAO_E_PASTA;
    no_t *f = x->filho;
    for (int i = 0; f && i < indice; i++) f = f->irmao;
    if (!f) return 0;
    txt_copia_n(e->nome, f->nome, NOME_MAX);
    e->tipo = f->tipo;
    e->tamanho = f->tamanho;
    return 1;
}

void vfs_segurar(no_t *x) { x->refs++; }

void vfs_soltar(no_t *x)
{
    if (x->refs) x->refs--;
    if (!x->refs && x->removido) destruir_no(x);
}

static bool usb_montado;

/* Soma o tamanho de todos os arquivos da árvore (H83, comando df). Anda
 * pela árvore sem recursão: desce no primeiro filho, passa ao irmão, e
 * sobe pelo "pai" quando acaba uma pasta. Os arquivos que ainda apontam
 * para o ramdisk contam também: eles ocupam memória do mesmo jeito. */
uint32_t vfs_bytes_usados(void)
{
    uint32_t soma = 0;
    no_t *x = raiz.filho;
    while (x) {
        if (x->tipo != TIPO_PASTA && !x->removido) soma += x->tamanho;
        if (x->filho) { x = x->filho; continue; }
        while (x && !x->irmao) { x = x->pai; if (x == &raiz) x = NULL; }
        if (x) x = x->irmao;
    }
    return soma;
}

void vfs_ponto_montagem(bool existe)
{
    usb_montado = existe;
    no_t *u = vfs_achar("/usb");
    if (existe && !u) vfs_criar("/usb", TIPO_PASTA, NULL);
    if (!existe && u && !u->filho) vfs_remover_pasta("/usb");
}

bool vfs_na_usb(const char *caminho, const char **rel)
{
    if (!usb_montado || !txt_comeca_com(caminho, "/usb")) return false;
    if (caminho[4] != 0 && caminho[4] != '/') return false;   /* "/usbx" não é /usb */
    *rel = caminho[4] ? caminho + 5 : caminho + 4;
    return true;
}

int vfs_ler_inteiro(const char *caminho, uint8_t **dados, uint32_t *tam)
{
    const char *rel;
    if (vfs_na_usb(caminho, &rel)) {   /* programas também podem rodar do pendrive */
        fat_arq_t f;
        int r = fat_abrir(rel, ABRIR_LER, &f);
        if (r < 0) return r;
        uint8_t *buf = kmalloc(f.tamanho ? f.tamanho : 1);
        if (!buf) return ERRO_SEM_MEMORIA;
        int n = fat_ler(&f, 0, buf, f.tamanho);
        if (n < 0) { kfree(buf); return n; }
        *tam = (uint32_t)n;
        *dados = buf;
        return 0;
    }
    no_t *x = vfs_achar(caminho);
    if (!x) return ERRO_NAO_EXISTE;
    if (x->tipo == TIPO_PASTA) return ERRO_E_PASTA;
    uint8_t *buf = kmalloc(x->tamanho ? x->tamanho : 1);
    if (!buf) return ERRO_SEM_MEMORIA;
    *tam = vfs_ler(x, 0, buf, x->tamanho);
    *dados = buf;
    return 0;
}
