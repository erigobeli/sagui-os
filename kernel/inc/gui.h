/* gui.h - área de trabalho e gerenciador de janelas (fase 5). */
#ifndef SAGUI_GUI_H
#define SAGUI_GUI_H
#include "tipos.h"
#include "desenho.h"

#define BARRA_SUPERIOR 24

/* Área de trabalho (area_trabalho.c) */
bool area_iniciar(void);
void area_fundo(superficie_t *s);          /* copia o fundo pronto para o recorte de s */
void area_barra(superficie_t *s, bool frase);   /* barra superior com o relógio */
bool area_relogio_mudou(void);
int  area_icone_em(int x, int y);          /* índice do ícone sob o ponto, ou -1 */
void area_icone_abrir(int i);
int  area_qtd_icones(void);
void area_icone_retangulo(int i, int *x, int *y, int *l, int *a);
void area_icone_direito(int i);
bool area_pendrive(bool presente, const char *rotulo);
bool area_icone_e_pendrive(int i);
void area_icone_mover(int i, int x, int y);    /* solta o ícone: encaixa na grade, se ligada */
void area_icone_fantasma(superficie_t *s, int i, int x, int y);
void area_icones_organizar(void);
void area_icones_tamanho(int lado);
int  area_icones_lado(void);
void area_grade(bool ligada);
bool area_grade_ligada(void);
#define QTD_FUNDOS 3
const char *area_fundo_nome(int i);
int  area_fundo_atual(void);
void area_fundo_escolher(int i);           /* troca o papel de parede */
bool area_desligar_clicado(int x, int y);

/* Gerenciador de janelas (janelas.c) */
bool gui_iniciar(void);                    /* liga a área de trabalho e o gerenciador */
bool gui_ativa(void);
void gui_redesenhar(int x, int y, int l, int a);
void gui_mostrar(void);
void gui_sombra(int x, int y, int l, int a);
void gui_aviso(const char *texto);   /* notificação no canto da tela por alguns segundos */
/* Menu Iniciar (iniciar.c) */
bool iniciar_aberto(void);
void iniciar_abrir(void);
void iniciar_fechar(void);
void iniciar_tecla(int t);
void iniciar_desenhar(superficie_t *s);
struct processo;
void janelas_do_processo_fechar(struct processo *p);

#endif
