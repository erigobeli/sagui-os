/* iniciar.c - o menu Iniciar: programas e busca de arquivos.
 *
 * Abre pelo sagui da barra superior. Com ele aberto, o teclado escreve na
 * busca: o nome de um programa ou de um arquivo, sem diferença entre
 * maiúsculas e sem contar acentos. Enter ou um clique abre o resultado. */
#include "gui.h"
#include "abi.h"
#include "console.h"
#include "mouse.h"
#include "processo.h"
#include "teclado.h"
#include "texto.h"
#include "vfs.h"

#define PX 4
#define PY (BARRA_SUPERIOR + 2)
#define PL 380
#define CABECALHO 36
#define BUSCA_Y (PY + CABECALHO + 8)
#define BUSCA_A 28
#define LISTA_Y (BUSCA_Y + BUSCA_A + 8)
#define ITEM_A 36
#define MAX_RES 10
#define RODAPE_A 36
#define PA (LISTA_Y - PY + MAX_RES * ITEM_A + RODAPE_A)
#define MAX_BUSCA 28

typedef struct { const char *nome, *programa; const uint32_t *icone; } app_t;

static const app_t apps[] = {
    { "Arquivos",     "/bin/arquivos",       arte_px_arquivos },
    { "Banana",       "/bin/banana",         arte_px_banana },
    { "Calculadora",  "/bin/calculadora",    arte_px_calculadora },
    { "Cipó (navegador)", "/bin/cipo",       arte_px_cipo },
    { "Editor",       "/bin/editor",         arte_px_editor },
    { "Sagui 3D",     "/bin/sagui3d",        arte_px_sagui3d },
    { "Sobre o SaguiOS", "/bin/sobre-o-sistema", arte_px_sobre },
    { "Terminal",     "/bin/terminal",       arte_px_terminal },
};
#define QTD_APPS (int)(sizeof apps / sizeof apps[0])

typedef struct {
    char nome[34];
    char caminho[120];
    uint8_t tipo;            /* 0 = programa da lista; senão, o TIPO_ do arquivo */
    const uint32_t *icone;
} resultado_t;

static bool aberto;
static char busca[MAX_BUSCA + 1];
static resultado_t res[MAX_RES];
static int qtd_res, sel;

bool iniciar_aberto(void) { return aberto; }

static void sujar(void) { gui_redesenhar(PX - 8, PY - 8, PL + 16, PA + 19); }

/* Minúscula sem acento, em Latin-1. */
static char dobrar(char c)
{
    uint8_t u = (uint8_t)c;
    if (u >= 'A' && u <= 'Z') return (char)(u + 32);
    if (u >= 0xC0 && u <= 0xC5) return 'a';
    if (u >= 0xE0 && u <= 0xE5) return 'a';
    if (u == 0xC7 || u == 0xE7) return 'c';
    if ((u >= 0xC8 && u <= 0xCB) || (u >= 0xE8 && u <= 0xEB)) return 'e';
    if ((u >= 0xCC && u <= 0xCF) || (u >= 0xEC && u <= 0xEF)) return 'i';
    if ((u >= 0xD2 && u <= 0xD6) || (u >= 0xF2 && u <= 0xF6)) return 'o';
    if ((u >= 0xD9 && u <= 0xDC) || (u >= 0xF9 && u <= 0xFC)) return 'u';
    return c;
}

static bool contem(const char *texto, const char *agulha)
{
    if (!*agulha) return true;
    for (; *texto; texto++) {
        int k = 0;
        while (agulha[k] && texto[k] && dobrar(texto[k]) == dobrar(agulha[k])) k++;
        if (!agulha[k]) return true;
    }
    return false;
}

static void achar_arquivos(no_t *pasta, const char *caminho, int nivel)
{
    if (nivel > 6) return;
    for (no_t *n = pasta->filho; n && qtd_res < MAX_RES; n = n->irmao) {
        if (n->removido || n->nome[0] == '.') continue;
        char c[120];
        snformatar(c, sizeof c, "%s/%s", caminho, n->nome);
        if (txt_compara(c, "/bin") == 0 || txt_compara(c, "/testes") == 0) continue;
        if (contem(n->nome, busca)) {
            resultado_t *r = &res[qtd_res++];
            txt_copia_n(r->nome, n->nome, sizeof r->nome);
            txt_copia_n(r->caminho, c, sizeof r->caminho);
            r->tipo = n->tipo;
            r->icone = n->tipo == TIPO_PASTA ? arte_px_arquivos : arte_px_editor;
        }
        if (n->tipo == TIPO_PASTA) achar_arquivos(n, c, nivel + 1);
    }
}

static void procurar(void)
{
    qtd_res = 0;
    for (int i = 0; i < QTD_APPS && qtd_res < MAX_RES; i++)
        if (contem(apps[i].nome, busca)) {
            resultado_t *r = &res[qtd_res++];
            txt_copia_n(r->nome, apps[i].nome, sizeof r->nome);
            txt_copia_n(r->caminho, apps[i].programa, sizeof r->caminho);
            r->tipo = 0;
            r->icone = apps[i].icone;
        }
    if (busca[0]) achar_arquivos(vfs_raiz(), "", 0);
    sel = qtd_res ? 0 : -1;
    klog("[GUI] iniciar: busca \"%s\", %d resultados\n", busca, qtd_res);
    sujar();
}

void iniciar_fechar(void)
{
    if (aberto) sujar();
    aberto = false;
}

void iniciar_abrir(void)
{
    aberto = true;
    busca[0] = 0;
    procurar();
}

static void abrir(int i)
{
    if (i < 0 || i >= qtd_res) return;
    resultado_t *r = &res[i];
    klog("[GUI] iniciar: abrir %s\n", r->caminho);
    if (r->tipo == 0 || r->tipo == TIPO_PROGRAMA) {
        const char *nome = r->caminho;
        for (const char *p = r->caminho; *p; p++) if (*p == '/') nome = p + 1;
        char *argv[] = { (char *)nome, NULL };
        processo_executar(r->caminho, 1, argv, NULL);
    } else {
        bool pasta = r->tipo == TIPO_PASTA;
        char *argv[] = { pasta ? "arquivos" : "editor", r->caminho, NULL };
        processo_executar(pasta ? "/bin/arquivos" : "/bin/editor", 2, argv, NULL);
    }
    iniciar_fechar();
}

static void desligar(void)
{
    char *argv[] = { "desligar", NULL };
    processo_executar("/bin/desligar", 1, argv, NULL);
    iniciar_fechar();
}

void iniciar_tecla(int t)
{
    int n = (int)txt_tamanho(busca);
    if (t == TECLA_ESC) { iniciar_fechar(); return; }
    if (t == '\n' || t == '\r') { abrir(sel); return; }
    if (t == TECLA_BAIXO && sel + 1 < qtd_res) { sel++; sujar(); return; }
    if (t == TECLA_CIMA && sel > 0) { sel--; sujar(); return; }
    if (t == '\b') { if (n) { busca[n - 1] = 0; procurar(); } return; }
    if (t >= 32 && t < 256 && t != 127 && n < MAX_BUSCA) {
        busca[n] = (char)t;
        busca[n + 1] = 0;
        procurar();
    }
}

static int item_em(int x, int y)
{
    if (x < PX || x >= PX + PL || y < LISTA_Y || y >= LISTA_Y + qtd_res * ITEM_A) return -1;
    return (y - LISTA_Y) / ITEM_A;
}

/* Devolve sem fazer nada se o menu estiver fechado. Clique fora fecha. */
void iniciar_mouse(evento_mouse_t *m)
{
    int i = item_em(m->x, m->y);
    if (i >= 0 && i != sel) { sel = i; sujar(); }
    if (!(m->mudaram & m->botoes)) return;
    bool dentro = m->x >= PX && m->x < PX + PL && m->y >= PY && m->y < PY + PA;
    if (!dentro) { iniciar_fechar(); return; }
    if (!(m->mudaram & BOTAO_ESQUERDO)) return;
    if (i >= 0) abrir(i);
    else if (m->y >= PY + PA - RODAPE_A && m->x >= PX + PL - 110) desligar();
}

void iniciar_desenhar(superficie_t *s)
{
    if (!aberto) return;
    gui_sombra(PX, PY, PL, PA);
    sup_degrade(s, PX, PY, PL, PA, 0x1C2630, 0x10161D);
    sup_moldura(s, PX, PY, PL, PA, 0x0B7A3E);
    sup_degrade(s, PX + 1, PY + 1, PL - 2, CABECALHO - 1, 0x18A65A, 0x0A6E38);
    sup_retangulo(s, PX + 1, PY + CABECALHO - 1, PL - 2, 1, 0xE8BE30);
    sup_texto_forte(s, PX + 14, PY + 10, "SaguiOS", 0xFFFFFF);
    sup_retangulo(s, PX + 10, BUSCA_Y, PL - 20, BUSCA_A, 0xFFFFFF);
    sup_moldura(s, PX + 10, BUSCA_Y, PL - 20, BUSCA_A, 0xE8BE30);
    if (busca[0]) sup_texto(s, PX + 18, BUSCA_Y + 6, busca, 0x1E2328);
    else sup_texto(s, PX + 18, BUSCA_Y + 6, "Pesquisar programas e arquivos", 0x8A939C);
    sup_retangulo(s, PX + 18 + texto_largura(busca), BUSCA_Y + 6, 2, 16, 0x0B7A3E);
    for (int i = 0; i < qtd_res; i++) {
        int y = LISTA_Y + i * ITEM_A;
        if (i == sel) {
            sup_degrade(s, PX + 6, y, PL - 12, ITEM_A - 2, 0x18A65A, 0x0A6E38);
            sup_retangulo(s, PX + 6, y, 3, ITEM_A - 2, 0xE8BE30);
        }
        sup_icone_escala(s, PX + 16, y + 3, res[i].icone, ICONE_LADO, 28);
        sup_texto(s, PX + 54, y + (res[i].tipo ? 1 : 8), res[i].nome, 0xFFFFFF);
        if (res[i].tipo) {
            char c[39];
            txt_copia_n(c, res[i].caminho, sizeof c);
            sup_texto(s, PX + 54, y + 17, c, i == sel ? 0xD8F0E0 : 0x8A98A8);
        }
    }
    if (!qtd_res) sup_texto(s, PX + 18, LISTA_Y + 10, "Nada encontrado.", 0x9AA8B8);
    int ry = PY + PA - RODAPE_A;
    sup_retangulo(s, PX + 1, ry, PL - 2, 1, 0x34404C);
    sup_texto(s, PX + 14, ry + 10, "Enter abre, Esc fecha", 0x7A8896);
    sup_degrade(s, PX + PL - 100, ry + 7, 88, 22, 0xB03028, 0x7E1C18);
    sup_texto(s, PX + PL - 88, ry + 10, "Desligar", 0xFFFFFF);
}
