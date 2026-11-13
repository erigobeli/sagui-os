/* arte.h - desenhos em pixel art (letras = cores da paleta). */
#ifndef SAGUI_ARTE_H
#define SAGUI_ARTE_H
#include "tipos.h"

typedef struct {
    const char *const *linhas;
    int largura, altura;
} desenho_t;

extern const desenho_t arte_sagui, arte_icone_arquivos, arte_icone_terminal, arte_icone_editor,
                       arte_icone_calculadora, arte_icone_banana, arte_icone_sagui3d, arte_icone_sobre, arte_icone_pendrive;
extern const char *const arte_botao_energia[16];
extern const desenho_t arte_icone_configuracoes;
bool arte_cor(char letra, uint32_t *cor);

#endif
