/* configuracoes.c - as configurações do sistema (H37).
 *
 * Uma tabela pequena de pares "chave = valor" na memória do kernel, que
 * qualquer programa lê e grava por duas chamadas de sistema. Os valores
 * ficam guardados num arquivo de texto no pendrive de dados,
 * /usb/.saguios/configuracoes.txt, para sobreviverem ao desligar (o SaguiOS
 * roda da RAM e não grava no disco interno). Formato:
 *
 *     # comentário
 *     aparencia.fundo = azul
 *
 * Sem pendrive montado, as mudanças valem até desligar e o programa é
 * avisado (a chamada devolve 0 em vez de 1).
 *
 * O texto do arquivo é inteiro reescrito a cada mudança: são poucas linhas,
 * e assim um desligamento no meio nunca deixa metade de uma linha. */
#include "configuracoes.h"
#include "fat32.h"
#include "texto.h"
#include "console.h"
#include "memoria.h"
#include "sistema.h"
#include "abi.h"

#define MAX_CONFIG 64
#define ARQUIVO ".saguios/configuracoes.txt"

static struct { char chave[32]; char valor[96]; } tabela[MAX_CONFIG];
static int qtd;
static bool persistente;
static uint32_t geracao;

const char *config_obter(const char *chave)
{
    for (int i = 0; i < qtd; i++)
        if (txt_compara(tabela[i].chave, chave) == 0) return tabela[i].valor;
    return NULL;
}

bool config_persistente(void) { return persistente; }
uint32_t config_geracao(void) { return geracao; }

static void guardar_na_memoria(const char *chave, const char *valor)
{
    for (int i = 0; i < qtd; i++)
        if (txt_compara(tabela[i].chave, chave) == 0) {
            txt_copia_n(tabela[i].valor, valor, sizeof tabela[i].valor);
            return;
        }
    if (qtd >= MAX_CONFIG) return;
    txt_copia_n(tabela[qtd].chave, chave, sizeof tabela[qtd].chave);
    txt_copia_n(tabela[qtd].valor, valor, sizeof tabela[qtd].valor);
    qtd++;
}

static int salvar(void)
{
    char *t = kmalloc(MAX_CONFIG * 140 + 128);
    if (!t) return ERRO_SEM_MEMORIA;
    int k = snformatar(t, 128, "# Configurações do SaguiOS. Gravado pelo sistema; editar com ele desligado.\n");
    for (int i = 0; i < qtd; i++)
        k += snformatar(t + k, 140, "%s = %s\n", tabela[i].chave, tabela[i].valor);
    fat_criar_pasta(".saguios");   /* se já existe, tudo bem */
    fat_arq_t f;
    int r = fat_abrir(ARQUIVO, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR, &f);
    if (r >= 0) {
        r = fat_escrever(&f, 0, t, (uint32_t)k);
        fat_fechar(&f);
    }
    kfree(t);
    return r < 0 ? r : 0;
}

int config_definir(const char *chave, const char *valor)
{
    guardar_na_memoria(chave, valor);
    geracao++;
    klog("[CONFIG] %s = %s%s\n", chave, valor, persistente ? "" : " (so na memoria)");
    if (!persistente) return 0;
    return salvar() == 0 ? 1 : 0;
}

bool config_tem_arquivo(void)
{
    info_arquivo_t inf;
    return fat_info(ARQUIVO, &inf) >= 0;
}

/* Lê "chave = valor" linha a linha; o que estiver no arquivo vale mais que o
 * que foi mudado antes de montar (o pendrive é a memória de longo prazo). */
void config_ao_montar(void)
{
    persistente = true;
    fat_arq_t f;
    if (fat_abrir(ARQUIVO, ABRIR_LER, &f) < 0) { klog("[CONFIG] pendrive sem configuracoes ainda\n"); return; }
    uint32_t tam = f.tamanho > 16384 ? 16384 : f.tamanho;
    char *t = kmalloc(tam + 1);
    if (!t) { fat_fechar(&f); return; }
    int n = fat_ler(&f, 0, t, tam);
    fat_fechar(&f);
    if (n < 0) n = 0;
    t[n] = 0;
    int lidas = 0;
    for (char *linha = t; *linha;) {
        char *fim = linha;
        while (*fim && *fim != '\n') fim++;
        char guardado = *fim;
        *fim = 0;
        char *igual = linha;
        while (*igual && *igual != '=') igual++;
        if (*linha != '#' && *igual == '=') {
            char *c0 = linha, *c1 = igual;
            while (*c0 == ' ') c0++;
            while (c1 > c0 && (c1[-1] == ' ' || c1[-1] == '\r')) c1--;
            *c1 = 0;
            char *v0 = igual + 1, *v1 = fim;
            while (*v0 == ' ') v0++;
            while (v1 > v0 && (v1[-1] == ' ' || v1[-1] == '\r')) v1--;
            *v1 = 0;
            if (*c0) { guardar_na_memoria(c0, v0); lidas++; }
        }
        if (!guardado) break;
        linha = fim + 1;
    }
    kfree(t);
    geracao++;
    klog("[CONFIG] %d configuracoes lidas do pendrive\n", lidas);
}

void config_ao_desmontar(void) { persistente = false; }

/* SIS_CONFIG_LER (chave, &valor, max) -> tamanho, ERRO_NAO_EXISTE.
 * A chave especial "sistema.persistente" diz se há onde guardar ("sim"/"nao"). */
int32_t sis_config_ler(uint32_t pchave, uint32_t pvalor, uint32_t max, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    char chave[32];
    int r = usuario_texto((const char *)pchave, chave, sizeof chave);
    if (r < 0) return r;
    if (max == 0 || !usuario_ok((void *)pvalor, max, true)) return ERRO_ENDERECO;
    const char *v = txt_compara(chave, "sistema.persistente") == 0 ? (persistente ? "sim" : "nao") : config_obter(chave);
    if (!v) return ERRO_NAO_EXISTE;
    txt_copia_n((char *)pvalor, v, max);
    return (int32_t)txt_tamanho((char *)pvalor);
}

/* SIS_CONFIG_GRAVAR (chave, valor) -> 1 guardada no pendrive, 0 só até desligar. */
int32_t sis_config_gravar(uint32_t pchave, uint32_t pvalor, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    char chave[32], valor[96];
    int r = usuario_texto((const char *)pchave, chave, sizeof chave);
    if (r < 0) return r;
    r = usuario_texto((const char *)pvalor, valor, sizeof valor);
    if (r < 0) return r;
    if (!chave[0]) return ERRO_INVALIDO;
    for (char *p = chave; *p; p++)
        if (*p == '=' || *p == ' ' || *p == '\n') return ERRO_INVALIDO;
    for (char *p = valor; *p; p++)
        if (*p == '\n') return ERRO_INVALIDO;
    return config_definir(chave, valor);
}
