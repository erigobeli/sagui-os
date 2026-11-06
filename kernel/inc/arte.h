/* arte.h - desenhos em pixel art (letras = cores da paleta). */
#ifndef SAGUI_ARTE_H
#define SAGUI_ARTE_H
#include "tipos.h"

typedef struct {
    const char *const *linhas;
    int largura, altura;
} desenho_t;

extern const desenho_t arte_sagui, arte_icone_sobre;

/* Ícones 64x64 em ARGB, gerados por tools/mkarte.py. */
#define ICONE_LADO 64
extern const uint32_t arte_px_arquivos[], arte_px_terminal[], arte_px_editor[], arte_px_calculadora[],
                      arte_px_banana[], arte_px_sagui3d[], arte_px_sobre[], arte_px_pendrive[], arte_px_cipo[];
bool arte_cor(char letra, uint32_t *cor);

#endif
