/* paginacao.c - paginação de 4 níveis do kernel de 64 bits (3.0, D056).
 *
 * No modo longo a tradução de endereços sempre tem 4 níveis (Intel SDM
 * vol. 3A, 5.5 "4-Level Paging"; AMD APM vol. 2, 5.3): PML4 (cada entrada
 * cobre 512 GB) -> PDPT (1 GB) -> diretório (2 MB) -> tabela (4 KB) ->
 * página. Cada tabela tem 512 entradas de 8 bytes e ocupa uma página.
 *
 * O SaguiOS mantém, na 3.0, o mesmo mapa de memória da 2.x (D010, D054),
 * todo abaixo de 4 GB:
 *     0          - 896 MB   kernel: endereço virtual = físico
 *     896 MB     - 1 GB     janelas do kernel para dispositivos (MMIO)
 *     1 GB       - 3 GB     o programa de cada processo
 * Assim os programas de 32 bits enxergam o mesmo que antes, e o kernel pode
 * continuar guardando endereços em 32 bits (as mudanças ficam pequenas).
 *
 * Cada espaço de endereço tem a própria PML4 e a própria PDPT. A entrada 0
 * da PDPT (o primeiro GB, do kernel) aponta para um diretório único criado
 * no boot e compartilhado por todos; as entradas 1 e 2 (1 a 3 GB) são do
 * programa. Todas as tabelas do kernel são criadas no boot, então mudar uma
 * janela do kernel vale na hora para todos os processos, como na 2.x.
 *
 * Permissões: uma página só é acessível ao anel 3 se TODOS os níveis tiverem
 * o bit U/S, e só é gravável se todos tiverem R/W (SDM vol. 3A, 5.6). O
 * diretório do kernel entra na PDPT sem U/S: nada do primeiro GB chega aos
 * programas, mesmo que alguma entrada lá embaixo tivesse o bit. */
#include "paginacao.h"
#include "console.h"
#include "cpu.h"
#include "memoria.h"
#include "texto.h"

#define ENTRADAS      512
#define MASCARA_END   0x000FFFFFFFFFF000ull   /* bits 12-51: endereço físico da próxima tabela/página */
#define PG_TAMANHO    0x080                   /* no diretório: página de 2 MB (não usada pelo kernel) */
#define GB            0x40000000u

typedef uint64_t entrada_t;

static espaco_t pml4_kernel;
static entrada_t *dir_kernel;    /* o diretório do primeiro GB, comum a todos */
static uint32_t janela_proxima = KERNEL_JANELAS;

/* Todo endereço físico de tabela fica abaixo de 896 MB (memfisica_alocar):
 * pelo mapeamento 1:1, o físico também é o virtual. */
static inline entrada_t *tabela_de(entrada_t e) { return (entrada_t *)(uintptr_t)(e & MASCARA_END); }

static inline void invalidar(uint32_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"((uintptr_t)virt) : "memory");
}

static uint32_t pagina_zerada(void)
{
    uint32_t p = memfisica_alocar();
    if (p) memset((void *)(uintptr_t)p, 0, PAGINA);
    return p;
}

static inline entrada_t *pdpt_de(espaco_t e)
{
    return tabela_de(((entrada_t *)(uintptr_t)e)[0]);
}

void paginacao_iniciar(void)
{
    pml4_kernel = pagina_zerada();
    uint32_t pdpt = pagina_zerada();
    uint32_t dir = pagina_zerada();
    if (!pml4_kernel || !pdpt || !dir) { kprintf("paginacao: sem memoria para as tabelas do kernel\n"); parar_maquina(); }
    dir_kernel = (entrada_t *)(uintptr_t)dir;
    /* Tabelas do kernel: 1:1 até KERNEL_JANELAS; vazias (mas existentes) na
     * faixa de janelas, para que todo processo as herde desde já. São 512
     * tabelas de 4 KB (2 MB de RAM). Páginas de 2 MB gastariam menos, mas
     * a página 0 precisa ficar ausente (ponteiro nulo gera falta) e o
     * resto do código mapeia sempre de 4 em 4 KB. */
    for (uint32_t i = 0; i < ENTRADAS; i++) {
        uint32_t t = pagina_zerada();
        if (!t) { kprintf("paginacao: sem memoria para as tabelas do kernel\n"); parar_maquina(); }
        entrada_t *tab = (entrada_t *)(uintptr_t)t;
        uint32_t base = i << 21;
        if (base < KERNEL_JANELAS)
            for (uint32_t k = 0; k < ENTRADAS; k++)
                tab[k] = (entrada_t)(base + (k << 12)) | PG_PRESENTE | PG_ESCRITA;
        dir_kernel[i] = t | PG_PRESENTE | PG_ESCRITA;
    }
    tabela_de(dir_kernel[0])[0] = 0;   /* página 0 ausente: ponteiro nulo gera falta */
    ((entrada_t *)(uintptr_t)pdpt)[0] = dir | PG_PRESENTE | PG_ESCRITA;   /* sem U/S: só o kernel */
    ((entrada_t *)(uintptr_t)pml4_kernel)[0] = pdpt | PG_PRESENTE | PG_ESCRITA | PG_USUARIO;

    espaco_ativar(pml4_kernel);
    /* A paginação já está ligada desde o estágio 2 (o modo longo exige).
     * WP faz o kernel também respeitar páginas só de leitura; EM faz
     * qualquer instrução de ponto flutuante gerar exceção até o
     * processos_iniciar ligar a FPU de verdade (H80). */
    escreve_cr0(le_cr0() | 0x00010000u | 0x4u);
    klog("[SAGUI] paginacao de 4 niveis: kernel 1:1 ate %u MB, programas de %u MB a %u MB\n",
         KERNEL_JANELAS >> 20, USUARIO_INICIO >> 20, USUARIO_FIM >> 20);
}

espaco_t espaco_kernel(void) { return pml4_kernel; }

espaco_t espaco_criar(void)
{
    uint32_t pml4 = pagina_zerada();
    if (!pml4) return 0;
    uint32_t pdpt = pagina_zerada();
    if (!pdpt) { memfisica_liberar(pml4); return 0; }
    ((entrada_t *)(uintptr_t)pdpt)[0] = (entrada_t)(uintptr_t)dir_kernel | PG_PRESENTE | PG_ESCRITA;
    ((entrada_t *)(uintptr_t)pml4)[0] = pdpt | PG_PRESENTE | PG_ESCRITA | PG_USUARIO;
    return pml4;
}

void espaco_destruir(espaco_t e)
{
    if (!e || e == pml4_kernel) return;
    if (espaco_atual() == e)
        espaco_ativar(pml4_kernel);   /* nunca apagar as tabelas em uso */
    entrada_t *pdpt = pdpt_de(e);
    for (uint32_t g = USUARIO_INICIO / GB; g < ENTRADAS; g++) {
        if (!(pdpt[g] & PG_PRESENTE)) continue;
        entrada_t *dir = tabela_de(pdpt[g]);
        for (uint32_t i = 0; i < ENTRADAS; i++) {
            if (!(dir[i] & PG_PRESENTE)) continue;
            entrada_t *tab = tabela_de(dir[i]);
            for (uint32_t k = 0; k < ENTRADAS; k++)
                if ((tab[k] & PG_PRESENTE) && !(tab[k] & PG_EMPRESTADA))   /* emprestadas têm outro dono */
                    memfisica_liberar((uint32_t)(tab[k] & MASCARA_END));
            memfisica_liberar((uint32_t)(uintptr_t)tab);
        }
        memfisica_liberar((uint32_t)(uintptr_t)dir);
    }
    memfisica_liberar((uint32_t)(uintptr_t)pdpt);
    memfisica_liberar(e);
}

void espaco_ativar(espaco_t e)
{
    escreve_cr3(e);
}

espaco_t espaco_atual(void) { return (espaco_t)le_cr3(); }

/* A entrada da tabela (último nível) de 'virt', criando o diretório e a
 * tabela se for preciso e 'criar' pedir. NULL se não existe. */
static entrada_t *entrada_de(espaco_t e, uint32_t virt, bool criar)
{
    entrada_t *pdpt = pdpt_de(e);
    uint32_t g = virt >> 30, i = (virt >> 21) & 511, k = (virt >> 12) & 511;
    if (!(pdpt[g] & PG_PRESENTE)) {
        if (!criar || virt < USUARIO_INICIO) return NULL;   /* o diretório do kernel já existe */
        uint32_t d = pagina_zerada();
        if (!d) return NULL;
        pdpt[g] = d | PG_PRESENTE | PG_ESCRITA | PG_USUARIO;
    }
    entrada_t *dir = tabela_de(pdpt[g]);
    if (!(dir[i] & PG_PRESENTE)) {
        if (!criar || virt < USUARIO_INICIO) return NULL;   /* tabelas do kernel já existem todas */
        uint32_t t = pagina_zerada();
        if (!t) return NULL;
        dir[i] = t | PG_PRESENTE | PG_ESCRITA | PG_USUARIO;
    }
    if (dir[i] & PG_TAMANHO) return NULL;
    return &tabela_de(dir[i])[k];
}

bool espaco_mapear(espaco_t e, uint32_t virt, uint32_t fis, uint32_t flags)
{
    entrada_t *pte = entrada_de(e, virt, true);
    if (!pte) return false;
    *pte = (entrada_t)(fis & ~0xFFFu) | flags | PG_PRESENTE;
    if (espaco_atual() == e || virt < USUARIO_INICIO)
        invalidar(virt);
    return true;
}

uint32_t espaco_traduzir(espaco_t e, uint32_t virt, uint32_t *flags)
{
    entrada_t *pml4 = (entrada_t *)(uintptr_t)e;
    entrada_t *pdpt = pdpt_de(e);
    uint32_t g = virt >> 30, i = (virt >> 21) & 511;
    if (!(pdpt[g] & PG_PRESENTE)) return 0;
    entrada_t *dir = tabela_de(pdpt[g]);
    if (!(dir[i] & PG_PRESENTE) || (dir[i] & PG_TAMANHO)) return 0;
    entrada_t pte = tabela_de(dir[i])[(virt >> 12) & 511];
    if (!(pte & PG_PRESENTE)) return 0;
    /* Permissão efetiva: todos os níveis precisam permitir (como na 2.x,
     * que fazia "pte & pde"). */
    if (flags) *flags = (uint32_t)(pte & pml4[0] & pdpt[g] & dir[i] & 0xFFF);
    return (uint32_t)(pte & MASCARA_END) | (virt & 0xFFF);
}

bool espaco_alocar(espaco_t e, uint32_t virt, uint32_t bytes, uint32_t flags)
{
    uint32_t ini = virt & ~0xFFFu;
    uint64_t fim64 = ((uint64_t)virt + bytes + PAGINA - 1) & ~(uint64_t)0xFFF;
    if (ini < USUARIO_INICIO || fim64 > USUARIO_FIM) return false;
    uint32_t fim = (uint32_t)fim64;
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
    entrada_t *pte = entrada_de(e, virt, false);
    if (!pte) return;
    *pte = 0;
    if (espaco_atual() == e) invalidar(virt);
}

uint32_t espaco_paginas_usuario(espaco_t e)
{
    entrada_t *pdpt = pdpt_de(e);
    uint32_t n = 0;
    for (uint32_t g = USUARIO_INICIO / GB; g < ENTRADAS; g++) {
        if (!(pdpt[g] & PG_PRESENTE)) continue;
        entrada_t *dir = tabela_de(pdpt[g]);
        for (uint32_t i = 0; i < ENTRADAS; i++) {
            if (!(dir[i] & PG_PRESENTE)) continue;
            entrada_t *tab = tabela_de(dir[i]);
            for (uint32_t k = 0; k < ENTRADAS; k++)
                if (tab[k] & PG_PRESENTE) n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------ escrita combinada
 *
 * Igual à 2.x (ver kernel/arch/i386/paginacao.c): o tipo 1 do PAT vira WC
 * (escrita combinada) e o bit PWT de uma página escolhe esse tipo. No modo
 * longo o PAT funciona do mesmo jeito para páginas de 4 KB (SDM vol. 3A,
 * 13.12.3): PAT (bit 7), PCD (bit 4) e PWT (bit 3) formam o índice. */
#define MSR_PAT 0x277
static bool pat_pronto, pat_existe;

static bool ligar_pat(void)
{
    if (pat_pronto) return pat_existe;
    pat_pronto = true;
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (!(d & (1u << 16))) return false;                      /* processador sem PAT */
    /* PA0 WB(6), PA1 WC(1), PA2 UC-(7), PA3 UC(0), PA4 WB, PA5 WT(4), PA6 UC-, PA7 UC */
    __asm__ volatile("wbinvd" ::: "memory");
    escreve_msr(MSR_PAT, ((uint64_t)0x00070406u << 32) | 0x00070106u);
    __asm__ volatile("wbinvd" ::: "memory");
    escreve_cr3(le_cr3());   /* esquece as traduções antigas */
    pat_existe = true;
    return true;
}

static uint32_t mapear_janela(uint32_t fis, uint32_t bytes, uint32_t flags)
{
    uint32_t desloc = fis & 0xFFF;
    uint32_t paginas = (desloc + bytes + PAGINA - 1) / PAGINA;
    if (janela_proxima + paginas * PAGINA > USUARIO_INICIO) return 0;
    uint32_t virt = janela_proxima;
    for (uint32_t i = 0; i < paginas; i++)
        espaco_mapear(pml4_kernel, virt + i * PAGINA, (fis & ~0xFFFu) + i * PAGINA, flags);
    janela_proxima += paginas * PAGINA;
    return virt + desloc;
}

uint32_t janela_mapear(uint32_t fis, uint32_t bytes)
{
    return mapear_janela(fis, bytes, PG_ESCRITA);
}

uint32_t janela_mapear_wc(uint32_t fis, uint32_t bytes, bool *wc)
{
    bool pat = ligar_pat();
    if (wc) *wc = pat;
    return mapear_janela(fis, bytes, PG_ESCRITA | (pat ? PG_PWT : 0));
}
