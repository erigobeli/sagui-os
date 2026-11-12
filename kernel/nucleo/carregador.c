/* carregador.c - cria processos a partir de executáveis ELF32 (H13).
 *
 * Um executável ELF traz uma lista de "segmentos para carregar" (PT_LOAD):
 * cada um diz o endereço virtual, quantos bytes vêm do arquivo e quantos
 * ocupa na memória (a diferença é zerada: é a BSS). O carregador:
 *   1. lê o arquivo inteiro para um buffer do kernel;
 *   2. confere o cabeçalho (ELF, 32 bits, little-endian, executável, i386);
 *   3. cria um espaço de endereço novo e, para cada segmento, aloca as
 *      páginas no endereço pedido e copia os bytes do arquivo;
 *   4. cria a pilha do programa no topo da área do usuário e coloca nela os
 *      argumentos (argc, argv e os textos);
 *   5. monta o quadro inicial para o anel 3 e deixa o processo pronto.
 * Os campos usados são os da especificação ELF; o código é deste projeto. */
#include "sistema.h"
#include "console.h"
#include "memoria.h"
#include "paginacao.h"
#include "processo.h"
#include "texto.h"
#include "vfs.h"

#define MAX_ARGS      16
#define MAX_ARGS_BYTES 8192   /* URLs de vídeo passam de 1 KB */

typedef struct EMPACOTADO {
    uint8_t  ident[16];
    uint16_t tipo, maquina;
    uint32_t versao, entrada, desloc_ph, desloc_sh, flags;
    uint16_t tam_cabecalho, tam_ph, qtd_ph, tam_sh, qtd_sh, indice_nomes;
} elf_cabecalho_t;

typedef struct EMPACOTADO {
    uint32_t tipo, desloc, virt, fis, tam_arquivo, tam_memoria, flags, alinhamento;
} elf_programa_t;

#define PT_LOAD 1
#define PF_W    2

/* Copia bytes para dentro de outro espaço de endereço. As páginas já estão
 * alocadas; o kernel escreve nelas pelo endereço físico (mapeamento 1:1). */
static void copiar_para(espaco_t e, uint32_t virt, const void *origem, uint32_t n)
{
    const uint8_t *o = origem;
    while (n) {
        uint32_t fis = espaco_traduzir(e, virt, NULL);
        uint32_t cabe = PAGINA - (virt & 0xFFF);
        if (cabe > n) cabe = n;
        memcpy((void *)fis, o, cabe);
        virt += cabe; o += cabe; n -= cabe;
    }
}

static int carregar_segmentos(espaco_t e, const uint8_t *img, uint32_t tam, uint32_t *entrada, uint32_t *fim)
{
    const elf_cabecalho_t *c = (const elf_cabecalho_t *)img;
    if (tam < sizeof *c || img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F')
        return ERRO_FORMATO;
    if (img[4] != 1 || img[5] != 1 || c->tipo != 2 || c->maquina != 3 || c->tam_ph != sizeof(elf_programa_t))
        return ERRO_FORMATO;   /* 32 bits, little-endian, executável, Intel 386 */
    if (c->desloc_ph > tam || (uint32_t)c->qtd_ph * sizeof(elf_programa_t) > tam - c->desloc_ph)
        return ERRO_FORMATO;
    *fim = USUARIO_INICIO;
    bool algum = false;
    for (uint32_t i = 0; i < c->qtd_ph; i++) {
        const elf_programa_t *p = (const elf_programa_t *)(img + c->desloc_ph + i * sizeof(elf_programa_t));
        if (p->tipo != PT_LOAD || p->tam_memoria == 0) continue;
        if (p->virt < USUARIO_INICIO || p->tam_memoria > DADOS_LIMITE - p->virt ||
            p->tam_arquivo > p->tam_memoria || p->desloc > tam || p->tam_arquivo > tam - p->desloc)
            return ERRO_FORMATO;   /* segmento fora da área dos programas ou além do arquivo */
        uint32_t flags = PG_USUARIO | ((p->flags & PF_W) ? PG_ESCRITA : 0);
        if (!espaco_alocar(e, p->virt, p->tam_memoria, flags))
            return ERRO_SEM_MEMORIA;
        copiar_para(e, p->virt, img + p->desloc, p->tam_arquivo);
        if (p->virt + p->tam_memoria > *fim) *fim = p->virt + p->tam_memoria;
        algum = true;
    }
    if (!algum || c->entrada < USUARIO_INICIO || c->entrada >= *fim)
        return ERRO_FORMATO;
    *entrada = c->entrada;
    return 0;
}

/* Pilha inicial, de cima para baixo: os textos dos argumentos, o vetor argv
 * (terminado em NULL), e por fim argc e o ponteiro para argv, que a função
 * _inicio da libsagui lê em [esp] e [esp+4]. */
static uint32_t montar_pilha(espaco_t e, int argc, char *const argv[])
{
    uint32_t sp = PILHA_USUARIO_TOPO;
    uint32_t ponteiros[MAX_ARGS + 1];
    for (int i = argc - 1; i >= 0; i--) {
        uint32_t n = txt_tamanho(argv[i]) + 1;
        sp -= n;
        copiar_para(e, sp, argv[i], n);
        ponteiros[i] = sp;
    }
    ponteiros[argc] = 0;
    sp &= ~3u;
    sp -= (argc + 1) * 4;
    uint32_t vetor = sp;
    copiar_para(e, sp, ponteiros, (argc + 1) * 4);
    sp -= 4; copiar_para(e, sp, &vetor, 4);
    uint32_t c = (uint32_t)argc;
    sp -= 4; copiar_para(e, sp, &c, 4);
    return sp;
}

int processo_executar(const char *caminho, int argc, char *const argv[], processo_t *pai)
{
    uint32_t tam;
    uint8_t *img;
    int erro = vfs_ler_inteiro(caminho, &img, &tam);
    if (erro < 0) return erro;

    espaco_t e = espaco_criar();
    if (!e) { kfree(img); return ERRO_SEM_MEMORIA; }
    uint32_t entrada = 0, fim = 0;
    int r = carregar_segmentos(e, img, tam, &entrada, &fim);
    kfree(img);
    if (r == 0 && !espaco_alocar(e, PILHA_USUARIO_TOPO - PILHA_USUARIO_TAM, PILHA_USUARIO_TAM,
                                 PG_USUARIO | PG_ESCRITA))
        r = ERRO_SEM_MEMORIA;
    if (r < 0) { espaco_destruir(e); return r; }

    const char *nome = caminho;
    for (const char *p = caminho; *p; p++)
        if (*p == '/') nome = p + 1;
    processo_t *p = processo_novo(nome, pai ? pai->pid : 0);
    if (!p) { espaco_destruir(e); return ERRO_LIMITE; }
    p->espaco = e;
    p->dados_inicio = p->dados_fim = (fim + PAGINA - 1) & ~(PAGINA - 1);
    if (pai) txt_copia_n(p->pasta, pai->pasta, sizeof p->pasta);
    arquivos_herdar(p, pai);
    processo_preparar_usuario(p, entrada, montar_pilha(e, argc, argv));
    klog("[SAGUI] executar %s -> processo %d\n", caminho, p->pid);
    return p->pid;
}

/* executar(caminho, argv): argv é um vetor de textos terminado em NULL, no
 * espaço do programa. Tudo é copiado para o kernel antes de criar o processo. */
int32_t sis_executar(uint32_t ptr_caminho, uint32_t ptr_argv, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    char bruto[CAMINHO_MAX], caminho[CAMINHO_MAX];
    int r = usuario_texto((const char *)ptr_caminho, bruto, sizeof bruto);
    if (r < 0) return r;
    if (!caminho_resolver(atual->pasta, bruto, caminho, sizeof caminho)) return ERRO_INVALIDO;

    char *argv[MAX_ARGS + 1];
    char *textos = kmalloc(MAX_ARGS_BYTES);
    if (!textos) return ERRO_SEM_MEMORIA;
    int argc = 0;
    uint32_t usado = 0;
    const char *const *uargv = (const char *const *)ptr_argv;
    while (uargv) {
        if (!usuario_ok(&uargv[argc], sizeof(char *), false)) { r = ERRO_ENDERECO; break; }
        const char *a = uargv[argc];
        if (!a) break;
        if (argc == MAX_ARGS) { r = ERRO_LIMITE; break; }
        r = usuario_texto(a, textos + usado, MAX_ARGS_BYTES - usado);
        if (r < 0) break;
        argv[argc++] = textos + usado;
        usado += (uint32_t)r + 1;
    }
    if (r >= 0) {
        if (argc == 0) argv[argc++] = bruto;   /* sem argv: o nome do programa */
        argv[argc] = NULL;
        r = processo_executar(caminho, argc, argv, atual);
    }
    kfree(textos);
    return r;
}
