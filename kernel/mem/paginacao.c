/* paginacao.c - paginação de 2 níveis com páginas de 4 KB. Ver D010.
 *
 * Diretório (1024 entradas de 4 MB) -> tabela (1024 entradas de 4 KB) ->
 * página. As 256 primeiras entradas do diretório (0 a 1 GB) são do kernel e
 * apontam para tabelas criadas no boot e compartilhadas por todos os
 * processos. As 768 seguintes são do programa de cada processo. */
#include "paginacao.h"
#include "console.h"
#include "cpu.h"
#include "memoria.h"
#include "texto.h"

#define ENTRADAS      1024
#define PDE_KERNEL    (USUARIO_INICIO >> 22)      /* 256 */
#define PDE_USUARIO_FIM (USUARIO_FIM >> 22)       /* 768 */
#define MASCARA_END   0xFFFFF000u

static espaco_t dir_kernel;
static uint32_t janela_proxima = KERNEL_JANELAS;

static inline uint32_t *tabela_de(uint32_t entrada) { return (uint32_t *)(entrada & MASCARA_END); }

static inline void invalidar(uint32_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

static uint32_t pagina_zerada(void)
{
    uint32_t p = memfisica_alocar();
    if (p) memset((void *)p, 0, PAGINA);
    return p;
}

void paginacao_iniciar(void)
{
    dir_kernel = pagina_zerada();
    uint32_t *dir = (uint32_t *)dir_kernel;
    /* Tabelas do kernel: 1:1 até KERNEL_JANELAS; vazias (mas existentes) na
     * faixa de janelas, para que todo processo as herde desde já. */
    for (uint32_t i = 0; i < PDE_KERNEL; i++) {
        uint32_t t = pagina_zerada();
        if (!t) { kprintf("paginacao: sem memoria para as tabelas do kernel\n"); parar_maquina(); }
        uint32_t *tab = (uint32_t *)t;
        uint32_t base = i << 22;
        if (base < KERNEL_JANELAS)
            for (uint32_t k = 0; k < ENTRADAS; k++)
                tab[k] = (base + (k << 12)) | PG_PRESENTE | PG_ESCRITA;
        dir[i] = t | PG_PRESENTE | PG_ESCRITA;
    }
    ((uint32_t *)(dir[0] & MASCARA_END))[0] = 0;   /* página 0 ausente: ponteiro nulo gera falta */

    espaco_ativar(dir_kernel);
    /* PG liga a paginação; WP faz o kernel também respeitar páginas só de
     * leitura; EM faz ponto flutuante gerar exceção até fpu_iniciar decidir
     * que o processador consegue salvar o estado dele. */
    uint32_t cr0 = le_cr0();
    cr0 |= 0x80000000u | 0x00010000u | 0x4u;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

espaco_t espaco_kernel(void) { return dir_kernel; }

espaco_t espaco_criar(void)
{
    espaco_t e = pagina_zerada();
    if (!e) return 0;
    memcpy((void *)e, (void *)dir_kernel, PDE_KERNEL * sizeof(uint32_t));
    return e;
}

void espaco_destruir(espaco_t e)
{
    if (!e || e == dir_kernel) return;
    if (espaco_atual() == e)
        espaco_ativar(dir_kernel);   /* nunca apagar o diretório em uso */
    uint32_t *dir = (uint32_t *)e;
    for (uint32_t i = PDE_KERNEL; i < PDE_USUARIO_FIM; i++) {
        if (!(dir[i] & PG_PRESENTE)) continue;
        uint32_t *tab = tabela_de(dir[i]);
        for (uint32_t k = 0; k < ENTRADAS; k++)
            if ((tab[k] & PG_PRESENTE) && !(tab[k] & PG_EMPRESTADA))   /* emprestadas têm outro dono */
                memfisica_liberar(tab[k] & MASCARA_END);
        memfisica_liberar((uint32_t)tab);
    }
    memfisica_liberar(e);
}

void espaco_ativar(espaco_t e)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(e) : "memory");
}

espaco_t espaco_atual(void) { return le_cr3(); }

bool espaco_mapear(espaco_t e, uint32_t virt, uint32_t fis, uint32_t flags)
{
    uint32_t *dir = (uint32_t *)e;
    uint32_t i = virt >> 22, k = (virt >> 12) & 0x3FF;
    if (!(dir[i] & PG_PRESENTE)) {
        if (i < PDE_KERNEL) return false;   /* tabelas do kernel já existem todas */
        uint32_t t = pagina_zerada();
        if (!t) return false;
        dir[i] = t | PG_PRESENTE | PG_ESCRITA | PG_USUARIO;
    }
    tabela_de(dir[i])[k] = (fis & MASCARA_END) | flags | PG_PRESENTE;
    if (espaco_atual() == e || i < PDE_KERNEL)
        invalidar(virt);
    return true;
}

uint32_t espaco_traduzir(espaco_t e, uint32_t virt, uint32_t *flags)
{
    uint32_t *dir = (uint32_t *)e;
    uint32_t pde = dir[virt >> 22];
    if (!(pde & PG_PRESENTE)) return 0;
    uint32_t pte = tabela_de(pde)[(virt >> 12) & 0x3FF];
    if (!(pte & PG_PRESENTE)) return 0;
    if (flags) *flags = pte & pde & 0xFFF;   /* permissão efetiva: as duas precisam permitir */
    return (pte & MASCARA_END) | (virt & 0xFFF);
}

bool espaco_alocar(espaco_t e, uint32_t virt, uint32_t bytes, uint32_t flags)
{
    uint32_t ini = virt & MASCARA_END, fim = (virt + bytes + PAGINA - 1) & MASCARA_END;
    if (ini < USUARIO_INICIO || fim > USUARIO_FIM || fim < ini) return false;
    for (uint32_t v = ini; v < fim; v += PAGINA) {
        if (espaco_traduzir(e, v, NULL)) continue;   /* já existe: mantém */
        uint32_t p = pagina_zerada();
        if (!p || !espaco_mapear(e, v, p, flags)) {
            if (p) memfisica_liberar(p);
            return false;
        }
    }
    return true;
}

void espaco_desmapear(espaco_t e, uint32_t virt)
{
    uint32_t *dir = (uint32_t *)e;
    uint32_t pde = dir[virt >> 22];
    if (!(pde & PG_PRESENTE)) return;
    tabela_de(pde)[(virt >> 12) & 0x3FF] = 0;
    if (espaco_atual() == e) invalidar(virt);
}

uint32_t espaco_paginas_usuario(espaco_t e)
{
    uint32_t *dir = (uint32_t *)e, n = 0;
    for (uint32_t i = PDE_KERNEL; i < PDE_USUARIO_FIM; i++) {
        if (!(dir[i] & PG_PRESENTE)) continue;
        uint32_t *tab = tabela_de(dir[i]);
        for (uint32_t k = 0; k < ENTRADAS; k++)
            if (tab[k] & PG_PRESENTE) n++;
    }
    return n;
}

uint32_t janela_mapear(uint32_t fis, uint32_t bytes)
{
    uint32_t desloc = fis & 0xFFF;
    uint32_t paginas = (desloc + bytes + PAGINA - 1) / PAGINA;
    if (janela_proxima + paginas * PAGINA > USUARIO_INICIO) return 0;
    uint32_t virt = janela_proxima;
    for (uint32_t i = 0; i < paginas; i++)
        espaco_mapear(dir_kernel, virt + i * PAGINA, (fis & MASCARA_END) + i * PAGINA, PG_ESCRITA);
    janela_proxima += paginas * PAGINA;
    return virt + desloc;
}
