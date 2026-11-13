/* configuracoes.h - configurações do sistema guardadas no pendrive (H37). */
#ifndef SAGUI_CONFIGURACOES_H
#define SAGUI_CONFIGURACOES_H
#include "tipos.h"

const char *config_obter(const char *chave);              /* NULL se não definida */
int         config_definir(const char *chave, const char *valor);   /* 1 guardada no pendrive, 0 só na memória */
void        config_ao_montar(void);                      /* lê o arquivo do pendrive recém-montado */
void        config_ao_desmontar(void);
bool        config_persistente(void);
uint32_t    config_geracao(void);                        /* muda a cada alteração (a interface redesenha) */
bool        config_tem_arquivo(void);                    /* o pendrive montado tem configurações do SaguiOS? */
int32_t     sis_config_ler(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t     sis_config_gravar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#endif
