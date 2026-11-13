/* ligador.c - o ligador: junta o programa com a libsagui e grava o
 * executável ELF que o SaguiOS carrega.
 *
 * A libsagui chega como um arquivo só, /sistema/lib/libsagui.o: um objeto
 * ELF "relocável" (o formato que os compiladores usam para código ainda sem
 * endereço), feito no build com cada função e cada dado numa seção própria.
 * O que este arquivo faz:
 *
 *   1. lê o objeto: as seções (bytes), os símbolos (nomes) e as relocações
 *      (lugares onde falta escrever o endereço de um símbolo);
 *   2. casa os nomes: quem usa "imprimir" passa a apontar para quem define;
 *   3. escolhe o que entra: começa na seção de _inicio (o ponto de entrada)
 *      e segue as relocações; só o que for alcançado vai para o programa.
 *      Sem isso todo programa carregaria o TLS e o SSH inteiros;
 *   4. dá endereços: código e dados de leitura a partir de 0x40000000; os
 *      dados que mudam começam numa página nova (o carregador do kernel dá
 *      permissões por página: a primeira parte não pode ser escrita);
 *   5. escreve os endereços nos lugares anotados e grava o arquivo, com o
 *      cabeçalho ELF e a lista de "segmentos para carregar" que o
 *      kernel/nucleo/carregador.c lê.
 *
 * Os campos e os números mágicos são os da especificação ELF (System V ABI
 * e o suplemento do i386); a organização é deste projeto. */
#include "interno.h"

#define BASE     0x40000000u
#define PAGINA   4096u

static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }
static u32 le32(const u8 *p) { return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static void poe16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void poe32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* ------------------------------------------------------------ leitura do ELF */

Objeto *elf_ler_objeto(const char *nome, u8 *d, u32 tam)
{
    Objeto *o = novo(sizeof *o);
    u32 shoff, shnum, i, k, simtab = 0, qtd_comuns = 0;
    Secao **mapa;
    const u8 *sh, *nomes_sec;
    const char *ruim = "a biblioteca do sistema parece estragada; refaça a imagem do SaguiOS";
    o->nome = nome;
    if (tam < 52 || d[0] != 0x7F || d[1] != 'E' || d[2] != 'L' || d[3] != 'F' || d[4] != 1 || d[5] != 1 ||
        le16(d + 16) != 1 || le16(d + 18) != 3 || le16(d + 46) != 40)
        erro_geral(ruim, "'%s' não é um objeto ELF de 32 bits para x86", nome);
    shoff = le32(d + 32);
    shnum = le16(d + 48);
    if (shoff > tam || shnum * 40 > tam - shoff || le16(d + 50) >= shnum) erro_geral(ruim, "'%s': tabela de seções fora do arquivo", nome);
    sh = d + shoff;
    nomes_sec = d + le32(sh + le16(d + 50) * 40 + 16);
    mapa = novo(sizeof(Secao *) * (shnum + 1));
    o->secoes = novo(sizeof(Secao) * (shnum + 1));
    for (i = 0; i < shnum; i++) {
        const u8 *s = sh + i * 40;
        u32 tipo = le32(s + 4), marcas = le32(s + 8), desl = le32(s + 16), n = le32(s + 20);
        Secao *c;
        if (tipo == 2) simtab = i;
        if (!(marcas & 2) || (tipo != 1 && tipo != 8)) continue;       /* só o que ocupa memória: PROGBITS e NOBITS */
        if (tipo == 1 && (desl > tam || n > tam - desl)) erro_geral(ruim, "'%s': seção fora do arquivo", nome);
        c = &o->secoes[o->qtd_secoes++];
        c->nome = (const char *)nomes_sec + le32(s);
        c->classe = (u8)(tipo == 8 ? SEC_ZERADA : (marcas & 4) ? SEC_TEXTO : (marcas & 1) ? SEC_DADOS : SEC_LEITURA);
        c->alinha = le32(s + 32) ? le32(s + 32) : 1;
        c->tam = n;
        c->bytes = tipo == 8 ? NULL : d + desl;
        c->obj = o;
        mapa[i] = c;
    }
    if (!simtab) erro_geral(ruim, "'%s' não tem tabela de símbolos", nome);
    {
        const u8 *s = sh + simtab * 40;
        const u8 *tab = d + le32(s + 16);
        const char *textos = (const char *)d + le32(sh + le32(s + 24) * 40 + 16);
        u32 n = le32(s + 20) / 16;
        o->qtd_sims = (int)n;
        o->sims = novo(sizeof(SimLig) * (n + 1));
        for (i = 0; i < n; i++)
            if (le16(tab + i * 16 + 14) == 0xFFF2) qtd_comuns++;
        for (i = 0; i < n; i++) {
            const u8 *e = tab + i * 16;
            u32 onde = le16(e + 14);
            SimLig *l = &o->sims[i];
            l->nome = textos + le32(e);
            l->valor = le32(e + 4);
            l->global = (e[12] >> 4) != 0;
            if (onde == 0xFFF2) {
                /* símbolo "comum" (variável global sem valor inicial, no estilo antigo): ganha uma seção zerada */
                Secao *c = novo(sizeof *c);
                c->nome = l->nome;
                c->classe = SEC_ZERADA;
                c->alinha = l->valor ? l->valor : 4;
                c->tam = le32(e + 8);
                c->obj = o;
                l->secao = c;
                l->valor = 0;
            } else if (onde && onde < shnum) {
                l->secao = mapa[onde];
                if ((e[12] & 15) == 3 && l->secao) l->nome = l->secao->nome;    /* símbolo que é a própria seção */
            }
        }
    }
    for (i = 0; i < shnum; i++) {
        const u8 *s = sh + i * 40;
        const u8 *r;
        Secao *alvo;
        u32 n;
        if (le32(s + 4) != 9) continue;         /* SHT_REL */
        alvo = le32(s + 28) < shnum ? mapa[le32(s + 28)] : NULL;
        if (!alvo) continue;
        r = d + le32(s + 16);
        n = le32(s + 20) / 8;
        alvo->relocs = novo(sizeof(Reloc) * (n + 1));
        alvo->qtd_relocs = (int)n;
        for (k = 0; k < n; k++) {
            u32 info = le32(r + k * 8 + 4), tipo = info & 0xFF;
            if (tipo == 4) tipo = REL_PC32;     /* R_386_PLT32: num executável fixo é uma chamada comum */
            if (tipo != REL_32 && tipo != REL_PC32) erro_geral(ruim, "'%s' usa uma relocação (%u) que o ligador não conhece", nome, tipo);
            if ((info >> 8) >= (u32)o->qtd_sims) erro_geral(ruim, "'%s': relocação com símbolo inválido", nome);
            alvo->relocs[k].desloc = le32(r + k * 8);
            alvo->relocs[k].sim = info >> 8;
            alvo->relocs[k].tipo = (u8)tipo;
        }
    }
    (void)qtd_comuns;
    return o;
}

/* ------------------------------------------------------------ ligação */

static SimLig *alvo_de(Secao *s, Reloc *r)
{
    SimLig *l = &s->obj->sims[r->sim];
    return l->secao ? l : l->real;
}

static u32 alinhado(u32 v, u32 a) { return (v + a - 1) / a * a; }

void ligar(Objeto *lista, const char *saida)
{
    Objeto *o;
    SimLig *inicio;
    Secao **fila, *s;
    u32 total_secoes = 0, nf = 0, i, fim_leitura, ini_dados, fim_dados = 0, fim_mem, tam_arq, desl_dados;
    u8 *arq;
    int k, classe, tem_dados;

    /* 1. quem define cada nome global */
    for (o = lista; o; o = o->prox) {
        total_secoes += (u32)o->qtd_secoes + (u32)o->qtd_sims;      /* folga para as seções "comuns" */
        for (k = 0; k < o->qtd_sims; k++) {
            SimLig *l = &o->sims[k];
            Nome *n;
            if (!l->secao || !l->global || !l->nome[0]) continue;
            n = nome_c(l->nome);
            if (n->lig) {
                Objeto *outro = n->lig->secao->obj;
                if (o->do_programa && outro->do_programa)
                    erro_geral("cada função ou variável global só pode ser definida em um dos arquivos; nos outros, declare com extern",
                               "'%s' está definido em dois arquivos: %s e %s", l->nome, outro->nome, o->nome);
                /* O programa tem algo com o mesmo nome de uma coisa da libsagui: vale
                 * o do programa, desde que a própria biblioteca não dependa desse nome
                 * (isso é conferido no passo 2). */
                n->lig->sobrepoe = 1;
                continue;
            }
            n->lig = l;
        }
    }
    for (o = lista; o; o = o->prox)
        for (k = 0; k < o->qtd_sims; k++) {
            SimLig *l = &o->sims[k];
            if (!l->secao && l->nome[0]) l->real = nome_c(l->nome)->lig;
        }

    /* 2. o que é alcançado a partir de _inicio */
    inicio = nome_c("_inicio")->lig;
    if (!inicio) erro_geral("a libsagui parece estragada; refaça a imagem do SaguiOS", "a biblioteca não tem o ponto de entrada _inicio");
    fila = novo(sizeof(Secao *) * (total_secoes + 1));
    inicio->secao->viva = 1;
    fila[nf++] = inicio->secao;
    for (i = 0; i < nf; i++) {
        s = fila[i];
        for (k = 0; k < s->qtd_relocs; k++) {
            SimLig *a = alvo_de(s, &s->relocs[k]);
            if (!a) {
                const char *n = s->obj->sims[s->relocs[k].sim].nome;
                if (txt_igual(n, "principal"))
                    erro_geral("todo programa do SaguiOS começa por: int principal(int argc, char **argv) { ... } (é o 'main' dos outros sistemas)",
                               "o programa não tem a função 'principal'");
                erro_geral("a função (ou variável) foi declarada, mas o corpo dela não está em lugar nenhum: escreva a função, ou confira o nome",
                           "'%s' é usado em '%s' mas não está definido nem no programa nem na libsagui", n, s->nome);
            }
            if (a->sobrepoe && !s->obj->do_programa)
                erro_geral("a libsagui tem uma função com esse nome e precisa dela por dentro; dê outro nome à sua",
                           "'%s' está definido no seu programa e também na libsagui", a->nome);
            if (!a->secao->viva) { a->secao->viva = 1; fila[nf++] = a->secao; }
        }
    }

    /* 3. endereços: código, dados de leitura, [página nova] dados, zerados */
    {
        u32 e = BASE;
        fim_leitura = ini_dados = BASE;
        for (classe = SEC_TEXTO; classe <= SEC_ZERADA; classe++) {
            if (classe == SEC_DADOS) { fim_leitura = e; e = alinhado(e, PAGINA); ini_dados = e; }
            for (i = 0; i < nf; i++) {
                s = fila[i];
                if (s->classe != classe) continue;
                e = alinhado(e, s->alinha);
                s->endereco = e;
                e += s->tam;
            }
            if (classe == SEC_DADOS) fim_dados = e;
        }
        fim_mem = e;
    }
    tem_dados = fim_mem > ini_dados;

    /* 4. o arquivo: cabeçalhos numa página, depois o código, depois os dados */
    desl_dados = alinhado(PAGINA + (fim_leitura - BASE), PAGINA);
    tam_arq = tem_dados ? desl_dados + (fim_dados - ini_dados) : PAGINA + (fim_leitura - BASE);
    arq = novo(tam_arq + 1);
    for (i = 0; i < nf; i++) {
        u8 *dst;
        s = fila[i];
        if (s->classe == SEC_ZERADA) continue;
        dst = arq + (s->classe == SEC_DADOS ? desl_dados + (s->endereco - ini_dados) : PAGINA + (s->endereco - BASE));
        copia_bytes(dst, s->bytes, s->tam);
        for (k = 0; k < s->qtd_relocs; k++) {
            Reloc *r = &s->relocs[k];
            SimLig *a = alvo_de(s, r);
            u32 v = le32(dst + r->desloc) + a->secao->endereco + a->valor;
            if (r->tipo == REL_PC32) v -= s->endereco + r->desloc;
            poe32(dst + r->desloc, v);
        }
    }

    /* cabeçalho ELF */
    arq[0] = 0x7F; arq[1] = 'E'; arq[2] = 'L'; arq[3] = 'F';
    arq[4] = 1;                 /* 32 bits */
    arq[5] = 1;                 /* little-endian */
    arq[6] = 1;                 /* versão do formato */
    poe16(arq + 16, 2);         /* executável */
    poe16(arq + 18, 3);         /* Intel 386 */
    poe32(arq + 20, 1);
    poe32(arq + 24, inicio->secao->endereco + inicio->valor);
    poe32(arq + 28, 52);        /* onde estão os segmentos: logo depois deste cabeçalho */
    poe16(arq + 40, 52);
    poe16(arq + 42, 32);
    poe16(arq + 44, tem_dados ? 2 : 1);
    poe16(arq + 46, 40);
    /* segmento 1: código e dados de leitura (ler e executar) */
    poe32(arq + 52, 1);                         /* PT_LOAD */
    poe32(arq + 56, PAGINA);                    /* onde está no arquivo */
    poe32(arq + 60, BASE);
    poe32(arq + 64, BASE);
    poe32(arq + 68, fim_leitura - BASE);
    poe32(arq + 72, fim_leitura - BASE);
    poe32(arq + 76, 5);                         /* ler + executar */
    poe32(arq + 80, PAGINA);
    if (tem_dados) {                            /* segmento 2: dados (ler e escrever); o fim é zerado pelo carregador */
        poe32(arq + 84, 1);
        poe32(arq + 88, desl_dados);
        poe32(arq + 92, ini_dados);
        poe32(arq + 96, ini_dados);
        poe32(arq + 100, fim_dados - ini_dados);
        poe32(arq + 104, fim_mem - ini_dados);
        poe32(arq + 108, 6);                    /* ler + escrever */
        poe32(arq + 112, PAGINA);
    }
    if (G.amb->gravar_arquivo(G.amb->ctx, saida, arq, tam_arq) < 0)
        erro_geral("confira se a pasta existe e se há espaço (no pendrive, se for em /usb)", "não consegui gravar '%s'", saida);
}
