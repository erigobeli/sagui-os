/* chamadas_processos.c - chamadas dos grupos "processos" e "diversas". */
#include "sistema.h"
#include "console.h"
#include "memoria.h"
#include "processo.h"
#include "relogio.h"
#include "texto.h"
#include "bootinfo.h"

SEM_RETORNO void maquina_reiniciar(void);

int32_t sis_sair(uint32_t codigo, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    processo_encerrar(atual, (int)codigo, NULL);
    return 0;
}

int32_t sis_pid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return atual->pid;
}

int32_t sis_matar(uint32_t pid, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    processo_t *p = processo_por_pid((int)pid);
    if (!p || p->estado == P_ZUMBI) return ERRO_SEM_PROCESSO;
    if (!p->usuario) return ERRO_ACESSO;   /* tarefas do kernel e o ocioso não */
    processo_encerrar(p, SAIDA_ENCERRADO_PELO_KERNEL, "encerrado pelo comando matar");
    return 0;
}

/* esperar(pid ou -1, &codigo, sem_bloquear): recolhe um filho que terminou.
 * Se nenhum terminou ainda, bloqueia e a chamada é repetida quando um filho
 * terminar (processo_encerrar acorda o pai); com sem_bloquear, devolve 0. */
int32_t sis_esperar(uint32_t pid, uint32_t ptr_codigo, uint32_t sem_bloquear, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    int *codigo = (int *)ptr_codigo;
    if (codigo && !usuario_ok(codigo, sizeof(int), true)) return ERRO_ENDERECO;
    bool tem_filho = false;
    for (int i = 1; i < MAX_PROCESSOS; i++) {
        processo_t *p = processo_indice(i);
        if (p->estado == P_LIVRE || p->pai != atual->pid) continue;
        if ((int)pid != -1 && p->pid != (int)pid) continue;
        tem_filho = true;
        if (p->estado == P_ZUMBI) {
            int r = p->pid;
            if (codigo) *codigo = p->codigo_saida;
            processo_liberar(p);
            return r;
        }
    }
    if (!tem_filho) return ERRO_SEM_PROCESSO;
    if (sem_bloquear) return 0;   /* ninguém terminou ainda; quem chamou não quer esperar */
    bloquear(atual);
    return SIS_REPETIR;
}

static const char canal_relogio = 0;

int32_t sis_dormir(uint32_t ms, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    uint32_t tiques = (ms * TIQUES_POR_SEGUNDO + 999) / 1000;
    if (tiques == 0) tiques = 1;
    atual->acordar_em = relogio_tiques() + tiques;
    bloquear(&canal_relogio);
    return 0;   /* resultado já pronto: não repete ao acordar */
}

int32_t sis_tempo(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (int32_t)(relogio_tiques() * (1000 / TIQUES_POR_SEGUNDO));
}

int32_t sis_data(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    data_t *dt = (data_t *)ptr;
    if (!usuario_ok(dt, sizeof *dt, true)) return ERRO_ENDERECO;
    data_hora_t h;
    cmos_ler_data(&h);
    dt->ano = h.ano; dt->mes = h.mes; dt->dia = h.dia;
    dt->hora = h.hora; dt->minuto = h.minuto; dt->segundo = h.segundo;
    dt->dia_semana = (uint8_t)dia_da_semana(h.ano, h.mes, h.dia);
    return 0;
}

int32_t sis_info_sistema(uint32_t tipo, uint32_t ptr, uint32_t n, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (tipo == INFO_MEMORIA) {
        info_memoria_t *m = (info_memoria_t *)ptr;
        if (!usuario_ok(m, sizeof *m, true)) return ERRO_ENDERECO;
        estat_memoria_t est;
        memoria_estatisticas(&est);
        m->ram_total_kb = (uint32_t)(est.ram_total >> 10);
        m->gerida_kb = est.paginas_geridas * 4;
        m->livre_kb = est.paginas_livres * 4;
        m->usada_kb = m->gerida_kb - m->livre_kb;
        return 0;
    }
    if (tipo == INFO_PROCESSOS) {
        info_processo_t *v = (info_processo_t *)ptr;
        if (n > MAX_PROCESSOS) n = MAX_PROCESSOS;
        if (!usuario_ok(v, n * sizeof *v, true)) return ERRO_ENDERECO;
        uint32_t k = 0;
        for (int i = 0; i < MAX_PROCESSOS && k < n; i++) {
            processo_t *p = processo_indice(i);
            if (p->estado == P_LIVRE) continue;
            v[k].pid = p->pid;
            v[k].pai = p->pai;
            v[k].estado = p->estado;
            v[k].memoria_kb = (p->usuario && p->estado != P_ZUMBI ? espaco_paginas_usuario(p->espaco) * 4 : 0)
                              + (p->pid ? PILHA_KERNEL / 1024 : 0);
            v[k].cpu_ms = p->tiques_cpu * (1000 / TIQUES_POR_SEGUNDO);
            v[k].usuario = p->usuario;
            txt_copia_n(v[k].nome, p->nome, sizeof v[k].nome);
            k++;
        }
        return (int32_t)k;
    }
    if (tipo == INFO_USB) {
        if (!usuario_ok((void *)ptr, n, true) || n < 2) return ERRO_ENDERECO;
        int usb_relatorio(char *s, int max);
        return usb_relatorio((char *)ptr, (int)n);
    }
    if (tipo == INFO_REDE) {
        if (!usuario_ok((void *)ptr, n, true) || n < 2) return ERRO_ENDERECO;
        int rede_relatorio(char *s, int max);
        return rede_relatorio((char *)ptr, (int)n);
    }
    if (tipo == INFO_VERSAO) {
        info_versao_t *v = (info_versao_t *)ptr;
        if (!usuario_ok(v, sizeof *v, true)) return ERRO_ENDERECO;
        extern const char *sagui_versao;
        extern uint32_t imagem_bytes;
        txt_copia_n(v->versao, sagui_versao, sizeof v->versao);
        v->imagem_bytes = imagem_bytes;
        v->segundos_ligado = relogio_segundos();
        v->largura = info_boot.vesa_ativo ? info_boot.vesa_largura : 80;
        v->altura = info_boot.vesa_ativo ? info_boot.vesa_altura : 25;
        return 0;
    }
    return ERRO_INVALIDO;
}

/* aumentar_memoria(n): estende a área de dados do programa em n bytes e
 * devolve onde ela terminava. É a base do malloc da libsagui. */
int32_t sis_aumentar_memoria(uint32_t n, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    uint32_t antigo = atual->dados_fim;
    if (n == 0) return (int32_t)antigo;
    if (n > DADOS_LIMITE - antigo) return ERRO_SEM_MEMORIA;
    if (!espaco_alocar(atual->espaco, antigo, n, PG_ESCRITA | PG_USUARIO)) return ERRO_SEM_MEMORIA;
    atual->dados_fim = antigo + n;
    return (int32_t)antigo;
}

int32_t sis_reiniciar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    maquina_reiniciar();
}

int32_t sis_desligar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    maquina_desligar();
}

/* registrar(texto): uma linha no log do sistema (a serial), com o nome e o
 * PID do programa. Os programas gráficos registram o que fazem por aqui,
 * sem sujar o terminal de quem os abriu. */
int32_t sis_registrar(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    char t[256];
    int r = usuario_texto((const char *)ptr, t, sizeof t);
    if (r < 0) return r;
    klog("[%s %d] %s\n", atual->nome, atual->pid, t);
    return 0;
}

/* sis_executar fica em kernel/nucleo/carregador.c (H13). */
