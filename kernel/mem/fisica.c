/* fisica.c - gerenciador de páginas físicas (bitmap). Ver D008. */
#include "memoria.h"
#include "bootinfo.h"
#include "console.h"
#include "cpu.h"
#include "texto.h"

#define TOTAL_PAGINAS (MEMFIS_LIMITE / PAGINA)
#define PALAVRAS      (TOTAL_PAGINAS / 32)

extern char _kernel_inicio[], _kernel_fim[];

static uint32_t mapa_bits[PALAVRAS];   /* 1 = ocupada */
static uint32_t livres, geridas;
static uint32_t dica;                  /* palavra onde a última busca parou */

static inline bool ocupada(uint32_t p)  { return mapa_bits[p >> 5] & (1u << (p & 31)); }
static inline void ocupar(uint32_t p)   { mapa_bits[p >> 5] |= 1u << (p & 31); }
static inline void desocupar(uint32_t p){ mapa_bits[p >> 5] &= ~(1u << (p & 31)); }

/* Marca [ini, fim) com o estado dado, página a página, mantendo a contagem. */
static void marcar(uint64_t ini, uint64_t fim, bool como_ocupada)
{
    if (fim > MEMFIS_LIMITE) fim = MEMFIS_LIMITE;
    for (uint64_t a = ini; a < fim; a += PAGINA) {
        uint32_t p = (uint32_t)(a / PAGINA);
        if (como_ocupada && !ocupada(p)) { ocupar(p); livres--; }
        else if (!como_ocupada && ocupada(p)) { desocupar(p); livres++; }
    }
}

void memfisica_iniciar(void)
{
    memset(mapa_bits, 0xFF, sizeof mapa_bits);
    livres = 0;
    for (uint32_t i = 0; i < info_boot.mapa_qtd; i++) {
        regiao_e820_t *r = &mapa_memoria[i];
        if (r->tipo != E820_LIVRE || !(r->extra & 1) || r->base >= MEMFIS_LIMITE)
            continue;
        /* Para dentro: início arredondado para cima, fim para baixo. */
        uint64_t ini = (r->base + PAGINA - 1) & ~(uint64_t)(PAGINA - 1);
        uint64_t fim = (r->base + r->tamanho) & ~(uint64_t)(PAGINA - 1);
        if (fim > ini)
            marcar(ini, fim, false);
    }
    /* Uma região "livre" pode se sobrepor a uma reservada em BIOS mal
     * feitas: o reservado vence. */
    for (uint32_t i = 0; i < info_boot.mapa_qtd; i++) {
        regiao_e820_t *r = &mapa_memoria[i];
        if (r->tipo != E820_LIVRE && r->base < MEMFIS_LIMITE)
            marcar(r->base & ~(uint64_t)(PAGINA - 1), r->base + r->tamanho, true);
    }
    marcar(0, 0x100000, true);                                   /* primeiro 1 MB */
    marcar((uint32_t)(uintptr_t)_kernel_inicio, (uint32_t)(uintptr_t)_kernel_fim, true);  /* kernel + BSS (inclui este bitmap) */
    marcar(info_boot.ramdisk_inicio, info_boot.ramdisk_inicio + info_boot.ramdisk_bytes, true);
    geridas = livres;
    dica = 0;
    klog("[SAGUI] memoria fisica: %u paginas livres (%u KB)\n", livres, livres * 4);
}

uint32_t memfisica_alocar(void)
{
    bool i = int_salva_desliga();
    for (uint32_t n = 0; n < PALAVRAS; n++) {
        uint32_t w = (dica + n) % PALAVRAS;
        if (mapa_bits[w] == 0xFFFFFFFFu)
            continue;
        uint32_t bit = (uint32_t)__builtin_ctz(~mapa_bits[w]);   /* primeiro bit 0 */
        uint32_t p = w * 32 + bit;
        ocupar(p);
        livres--;
        dica = w;
        int_restaura(i);
        return p * PAGINA;
    }
    int_restaura(i);
    return 0;
}

uint32_t memfisica_alocar_seguidas(uint32_t n)
{
    if (n == 0) return 0;
    if (n == 1) return memfisica_alocar();
    bool i = int_salva_desliga();
    uint32_t seguidas = 0;
    for (uint32_t p = 0; p < TOTAL_PAGINAS; p++) {
        if ((p & 31) == 0 && mapa_bits[p >> 5] == 0xFFFFFFFFu) {  /* pula 32 ocupadas de uma vez */
            seguidas = 0;
            p += 31;
            continue;
        }
        if (ocupada(p)) { seguidas = 0; continue; }
        if (++seguidas == n) {
            uint32_t ini = p + 1 - n;
            for (uint32_t k = ini; k <= p; k++) ocupar(k);
            livres -= n;
            int_restaura(i);
            return ini * PAGINA;
        }
    }
    int_restaura(i);
    return 0;
}

void memfisica_liberar_seguidas(uint32_t endereco, uint32_t n)
{
    bool i = int_salva_desliga();
    for (uint32_t k = 0; k < n; k++) {
        uint32_t p = endereco / PAGINA + k;
        if (p >= TOTAL_PAGINAS || !ocupada(p)) {
            klog("[SAGUI] AVISO: liberando pagina fisica invalida ou livre: %x\n", p * PAGINA);
            continue;
        }
        desocupar(p);
        livres++;
    }
    int_restaura(i);
}

void memfisica_liberar(uint32_t endereco)
{
    memfisica_liberar_seguidas(endereco, 1);
}

uint32_t memfisica_livres(void)  { return livres; }
uint32_t memfisica_geridas(void) { return geridas; }
