/* area_trabalho.c - o fundo, o mascote, os ícones e a barra superior (H20, H28).
 *
 * O fundo não muda: é desenhado uma vez numa superfície guardada (degradê
 * verde, amarelo e azul, o sagui no canto e os ícones), e a cada
 * redesenho só a parte necessária é copiada. A barra superior é desenhada
 * por cima, porque o relógio muda. */
#include "gui.h"
#include "arte.h"
#include "console.h"
#include "memoria.h"
#include "processo.h"
#include "relogio.h"
#include "texto.h"
#include "video.h"
#include "disco.h"
#include "sistema.h"
#include "vfs.h"

static uint32_t *fundo;
static int larg, alt;
static int minuto_mostrado = -1;

typedef struct {
    const char *nome;
    const char *programa;
    const uint32_t *desenho;
} icone_t;

static const icone_t icones[] = {
    { "Arquivos",    "/bin/arquivos",    arte_px_arquivos },
    { "Terminal",    "/bin/terminal",    arte_px_terminal },
    { "Editor",      "/bin/editor",      arte_px_editor },
    { "Calculadora", "/bin/calculadora", arte_px_calculadora },
    { "Banana",      "/bin/banana",      arte_px_banana },
    { "Sagui 3D",    "/bin/sagui3d",     arte_px_sagui3d },   /* bônus B4 */
    { "Sobre",       "/bin/sobre-o-sistema",   arte_px_sobre },
    { "Pendrive",    "/bin/arquivos",    arte_px_pendrive },   /* só aparece com um pendrive conectado (H34) */
};
#define QTD_ICONES (int)(sizeof icones / sizeof icones[0])
#define ICONE_PENDRIVE (QTD_ICONES - 1)
static bool pendrive_visivel;
static char nome_pendrive[24] = "Pendrive";

static bool icone_visivel(int i) { return i != ICONE_PENDRIVE || pendrive_visivel; }
#define ICONE_X 20
#define ICONE_Y0 (BARRA_SUPERIOR + 16)
#define ICONE_PASSO 84   /* 8 ícones cabem em 768 linhas */
#define ICONE_L 96
#define ICONE_A 88

int area_qtd_icones(void) { return QTD_ICONES; }

void area_icone_retangulo(int i, int *x, int *y, int *l, int *a)
{
    *x = ICONE_X; *y = ICONE_Y0 + i * ICONE_PASSO; *l = ICONE_L; *a = ICONE_A;
}

/* Seno de um ângulo de 0 a 1023 (volta inteira), de -256 a 256, por parábolas:
 * o kernel não usa ponto flutuante. */
static int seno(int a)
{
    a &= 1023;
    int t = a & 511, v = t * (512 - t) / 256;
    return a < 512 ? v : -v;
}

/* Altura de uma onda do fundo na coluna x. */
static int onda(int x, int base, int amplitude, int passo, int fase)
{
    return base + (seno(x * passo / 16 + fase) * amplitude + seno(x * passo / 7 + fase * 3) * amplitude / 3) / 256;
}

static void texto_com_sombra(superficie_t *s, int x, int y, const char *t, uint32_t cor)
{
    sup_texto(s, x + 1, y + 1, t, 0x101010);
    sup_texto(s, x + 1, y, t, 0x101010);
    sup_texto(s, x, y + 1, t, 0x101010);
    sup_texto(s, x, y, t, cor);
}

/* Papel de parede em imagem: /arte/fundo.sgi (formato em tools/mkfundo.py).
 * Trocar o arquivo troca o fundo. Se ele faltar ou estiver errado, o sistema
 * usa o fundo desenhado por desenhar_fundo_padrao. Devolve true se desenhou. */
#define SGI_CAB 776
static bool desenhar_fundo_imagem(void)
{
    uint8_t *d;
    uint32_t tam;
    if (vfs_ler_inteiro("/arte/fundo.sgi", &d, &tam) < 0) return false;
    bool ok = false;
    int il = tam > SGI_CAB ? d[4] | d[5] << 8 : 0, ia = tam > SGI_CAB ? d[6] | d[7] << 8 : 0;
    uint8_t *idx = il > 0 && ia > 0 && d[0] == 'S' && d[1] == 'G' && d[2] == 'I' && d[3] == '1'
                 ? kmalloc((uint32_t)(il * ia)) : NULL;
    if (idx) {
        uint32_t i = SGI_CAB, o = 0, total = (uint32_t)(il * ia);
        while (i < tam && o < total) {
            uint32_t n = d[i++];
            if (n < 128) {
                for (n++; n && i < tam && o < total; n--) idx[o++] = d[i++];
            } else if (i < tam) {
                uint8_t v = d[i++];
                for (n -= 125; n && o < total; n--) idx[o++] = v;
            }
        }
        if (o == total) {
            /* Outras resoluções: escolhe o pixel mais próximo da imagem. */
            for (int y = 0; y < alt; y++) {
                const uint8_t *linha = idx + (y * ia / alt) * il;
                for (int x = 0; x < larg; x++) {
                    const uint8_t *c = d + 8 + linha[x * il / larg] * 3;
                    fundo[y * larg + x] = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
                }
            }
            ok = true;
            klog("[GUI] fundo: /arte/fundo.sgi %dx%d\n", il, ia);
        }
        kfree(idx);
    }
    kfree(d);
    return ok;
}

/* Papéis de parede: a imagem e dois desenhados por código. */
static const char *const nomes_fundo[QTD_FUNDOS] = { "Mata Atlântica", "Brasil", "Noite" };
static int fundo_escolhido;

const char *area_fundo_nome(int i) { return nomes_fundo[i]; }
int area_fundo_atual(void) { return fundo_escolhido; }

static void desenhar_fundo_padrao(bool noite)
{
    /* Degradê vertical nas cores do Brasil: verde em cima, amarelo no meio,
     * azul embaixo. Por cima, a luz de um sol no centro, a copa da mata no
     * alto e ondas de rio embaixo. Uma trama 4x4 disfarça as faixas do degradê. */
    static const uint8_t trama[4][4] = { {0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5} };
    const uint32_t verde = noite ? 0x06281E : 0x0B7A3E, amarelo = noite ? 0x1C3A60 : 0xE8BE30,
                   azul = noite ? 0x050A18 : 0x14337E, sol = noite ? 0x4A6AA0 : 0xFFF0A0;
    int sx = larg * 3 / 5, sy = alt / 2, raio = alt / 2 + 40;
    for (int x = 0; x < larg; x++) {
        int mata1 = onda(x, alt / 7, 26, 5, 100), mata2 = onda(x, alt / 11, 20, 7, 420);
        int rio1 = onda(x, alt * 3 / 4, 24, 4, 0), rio2 = onda(x, alt * 5 / 6, 20, 6, 300), rio3 = onda(x, alt * 11 / 12, 12, 9, 640);
        for (int y = 0; y < alt; y++) {
            int m = alt / 2, num = (y < m ? y : y - m) * 16 + trama[y & 3][x & 3], den = (y < m ? m : alt - m) * 16;
            uint32_t c = y < m ? cor_mistura(verde, amarelo, num, den) : cor_mistura(amarelo, azul, num, den);
            int dx = x - sx, dy = y - sy, d = (dx * dx + dy * dy) / raio;
            if (d < raio) {
                int f = raio - d;
                c = cor_mistura(c, sol, f * f / raio * 3 / 5, raio);
            }
            if (y < mata1) c = cor_mistura(c, 0x075C2E, 1, 2);
            if (y < mata2) c = cor_mistura(c, 0x044422, 3, 5);
            if (y == mata1) c = cor_mistura(c, 0xFFFFFF, 1, 5);
            if (y > rio1) c = cor_mistura(c, 0x1C5AA8, 2, 5);
            if (y > rio2) c = cor_mistura(c, 0x123C86, 1, 2);
            if (y > rio3) c = cor_mistura(c, 0x0C2860, 1, 2);
            if (y == rio1 || y == rio2) c = cor_mistura(c, 0xFFFFFF, 1, 4);
            fundo[y * larg + x] = c;
        }
    }
}

static void desenhar_fundo(void)
{
    if (fundo_escolhido != 0 || !desenhar_fundo_imagem()) desenhar_fundo_padrao(fundo_escolhido == 2);
    superficie_t s;
    sup_iniciar(&s, fundo, larg, alt);
    /* O mascote, grande, no canto de baixo à direita. */
    int esc = larg >= 1000 ? 5 : 3;
    int mx = larg - 32 * esc - 30, my = alt - 32 * esc - 40;
    sup_desenho(&s, mx, my, &arte_sagui, esc);
    texto_com_sombra(&s, mx + 16 * esc - texto_largura("SaguiOS") / 2, my + 32 * esc + 6, "SaguiOS", 0xFFFFFF);
    /* Ícones na coluna da esquerda, com o nome embaixo. */
    for (int i = 0; i < QTD_ICONES; i++) {
        if (!icone_visivel(i)) continue;
        int x, y, l, a;
        area_icone_retangulo(i, &x, &y, &l, &a);
        sup_icone(&s, x + (l - ICONE_LADO) / 2, y, icones[i].desenho, ICONE_LADO);
        const char *nome = i == ICONE_PENDRIVE ? nome_pendrive : icones[i].nome;
        texto_com_sombra(&s, x + (l - texto_largura(nome)) / 2, y + 68, nome, 0xFFFFFF);
    }
}

void area_fundo_escolher(int i)
{
    if (i < 0 || i >= QTD_FUNDOS) return;
    fundo_escolhido = i;
    desenhar_fundo();
    klog("[GUI] papel de parede: %s\n", nomes_fundo[i]);
}

bool area_iniciar(void)
{
    larg = (int)video_largura();
    alt = (int)video_altura();
    fundo = kmalloc((uint32_t)(larg * alt) * 4);
    if (!fundo) return false;
    desenhar_fundo();
    return true;
}

void area_fundo(superficie_t *s)
{
    sup_copiar(s, 0, 0, fundo, larg, alt, larg);
}

bool area_relogio_mudou(void)
{
    /* Ler o CMOS é lento e acontece com as interrupções desligadas: uma vez
     * por segundo basta, e o mouse não engasga. */
    static uint32_t proxima;
    uint32_t agora = relogio_tiques();
    if ((int32_t)(agora - proxima) < 0) return false;
    proxima = agora + TIQUES_POR_SEGUNDO;
    data_hora_t d;
    cmos_ler_data(&d);
    return d.minuto != minuto_mostrado;
}

void area_barra(superficie_t *s, bool frase)
{
    static const char *semana[] = { "dom", "seg", "ter", "qua", "qui", "sex", "sáb" };
    for (int y = 0; y < BARRA_SUPERIOR; y++)
        sup_retangulo(s, 0, y, larg, 1, cor_mistura(0x18202A, 0x0E141B, y / 4 * 4, BARRA_SUPERIOR));
    /* Filete nas três cores do fundo. */
    sup_retangulo(s, 0, BARRA_SUPERIOR - 2, larg, 2, 0x0B7A3E);
    sup_retangulo(s, larg / 3, BARRA_SUPERIOR - 2, larg / 3, 2, 0xE8BE30);
    sup_retangulo(s, larg * 2 / 3, BARRA_SUPERIOR - 2, larg - larg * 2 / 3, 2, 0x2A6FD8);
    /* Cabecinha do sagui (as linhas 2 a 17 do ícone "sobre") e o nome. */
    desenho_t cabeca = { arte_icone_sobre.linhas + 2, 32, 16 };
    sup_desenho(s, 6, 4, &cabeca, 1);
    sup_texto_forte(s, 44, 3, "SaguiOS", 0xFFDF4A);
    if (frase) sup_texto(s, 116, 3, "um sistema operacional escrito do zero por IA", 0x9AA8B8);
    data_hora_t d;
    cmos_ler_data(&d);
    bool novo = d.minuto != minuto_mostrado;
    minuto_mostrado = d.minuto;
    char hora[48];
    snformatar(hora, sizeof hora, "%s %02u/%02u/%u  %02u:%02u", semana[dia_da_semana(d.ano, d.mes, d.dia)],
               d.dia, d.mes, d.ano, d.hora, d.minuto);
    sup_texto(s, larg - texto_largura(hora) - 10, 3, hora, 0xFFFFFF);
    if (novo) klog("[GUI] relogio: %s\n", hora);
}

int area_icone_em(int x, int y)
{
    for (int i = 0; i < QTD_ICONES; i++) {
        if (!icone_visivel(i)) continue;
        int ix, iy, l, a;
        area_icone_retangulo(i, &ix, &iy, &l, &a);
        if (x >= ix && x < ix + l && y >= iy && y < iy + a) return i;
    }
    return -1;
}

/* Mostra ou esconde o ícone do pendrive (H34). Devolve true se mudou. */
bool area_pendrive(bool presente, const char *rotulo)
{
    char nome[24];
    txt_copia_n(nome, rotulo && rotulo[0] ? rotulo : "Pendrive", sizeof nome);
    if (presente == pendrive_visivel && txt_compara(nome, nome_pendrive) == 0) return false;
    pendrive_visivel = presente;
    txt_copia_n(nome_pendrive, nome, sizeof nome_pendrive);
    desenhar_fundo();
    return true;
}

static disco_t *primeiro_disco(void)
{
    for (int i = 0; i < MAX_DISCOS; i++) {
        disco_t *d = disco_indice(i);
        if (d && d->presente && !d->de_boot) return d;
    }
    return NULL;
}

/* Botão direito num ícone: no pendrive, "remover com segurança". */
void area_icone_direito(int i)
{
    if (i != ICONE_PENDRIVE) return;
    disco_t *d = disco_montado();
    if (d) disco_desmontar(d);
    else gui_aviso("O pendrive não está montado: pode remover.");
}

/* Abre o programa do ícone (H28). Ele não tem pai: ninguém espera por ele. */
void area_icone_abrir(int i)
{
    if (i < 0 || i >= QTD_ICONES) return;
    if (i == ICONE_PENDRIVE) {
        disco_t *d = primeiro_disco();
        if (!d) return;
        int r = disco_montar(d);
        if (r == ERRO_FORMATO) { gui_aviso("Este pendrive não está em FAT32: formato não suportado."); return; }
        if (r < 0 && r != ERRO_OCUPADO) { gui_aviso("Não consegui montar o pendrive."); return; }
        char *argv[] = { "arquivos", "/usb", NULL };
        int pid = processo_executar("/bin/arquivos", 2, argv, NULL);
        klog("[GUI] icone Pendrive -> /bin/arquivos /usb (pid %d)\n", pid);
        return;
    }
    char *argv[] = { (char *)icones[i].programa + 5, NULL };
    int pid = processo_executar(icones[i].programa, 1, argv, NULL);
    klog("[GUI] icone %s -> %s (pid %d)\n", icones[i].nome, icones[i].programa, pid);
}
