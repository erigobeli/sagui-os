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

static uint32_t *fundo, *papel;   /* fundo = papel de parede + mascote + ícones */
static int larg, alt;
static int minuto_mostrado = -1;

typedef struct {
    const char *nome;
    const char *programa;
    const uint32_t *const *desenho;
} icone_t;

static const icone_t icones[] = {
    { "Arquivos",    "/bin/arquivos",    &arte_px_arquivos },
    { "Terminal",    "/bin/terminal",    &arte_px_terminal },
    { "Editor",      "/bin/editor",      &arte_px_editor },
    { "Calculadora", "/bin/calculadora", &arte_px_calculadora },
    { "Banana",      "/bin/banana",      &arte_px_banana },
    { "Sagui 3D",    "/bin/sagui3d",     &arte_px_sagui3d },   /* bônus B4 */
    { "Sobre",       "/bin/sobre-o-sistema",   &arte_px_sobre },
    { "Pendrive",    "/bin/arquivos",    &arte_px_pendrive },   /* só aparece com um pendrive conectado (H34) */
    { "Cipó",        "/bin/cipo",        &arte_px_cipo },
    { "Lixeira",     "/bin/arquivos",    &arte_px_lixeira },
};
#define QTD_ICONES (int)(sizeof icones / sizeof icones[0])
#define ICONE_PENDRIVE 7
#define ICONE_CIPO 8
#define ICONE_LIXEIRA 9
static bool pendrive_visivel;
static char nome_pendrive[24] = "Pendrive";

static bool ic_sel[QTD_ICONES], ic_lixo[QTD_ICONES];

static bool icone_visivel(int i) { return !ic_lixo[i] && (i != ICONE_PENDRIVE || pendrive_visivel); }
#define ICONE_X 20
#define ICONE_Y0 (BARRA_SUPERIOR + 16)
#define ICONE_L_MIN 96

static int ic_x[QTD_ICONES], ic_y[QTD_ICONES];
static int lado_icone = ICONE_LADO;
static bool grade_ligada;

static int celula_l(void) { return lado_icone + 32 > ICONE_L_MIN ? lado_icone + 32 : ICONE_L_MIN; }
static int celula_a(void) { return lado_icone + 20; }

int area_qtd_icones(void) { return QTD_ICONES; }
bool area_icone_e_pendrive(int i) { return i == ICONE_PENDRIVE; }
bool area_icone_e_lixeira(int i) { return i == ICONE_LIXEIRA; }
bool area_icone_selecionado(int i) { return ic_sel[i]; }
int area_icones_lado(void) { return lado_icone; }
bool area_grade_ligada(void) { return grade_ligada; }

void area_icone_retangulo(int i, int *x, int *y, int *l, int *a)
{
    *x = ic_x[i]; *y = ic_y[i]; *l = celula_l(); *a = celula_a() + 4;
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
    if (il > 0 && ia > 0 && tam >= (uint32_t)(SGI_CAB + il * ia) && memcmp(d, "SGI2", 4) == 0) {
        const uint8_t *idx = d + SGI_CAB;
        /* Outras resoluções: escolhe o pixel mais próximo da imagem. */
        for (int y = 0; y < alt; y++) {
            const uint8_t *linha = idx + (y * ia / alt) * il;
            for (int x = 0; x < larg; x++) {
                const uint8_t *c = d + 8 + linha[x * il / larg] * 3;
                papel[y * larg + x] = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
            }
        }
        ok = true;
        klog("[GUI] fundo: /arte/fundo.sgi %dx%d\n", il, ia);
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
            papel[y * larg + x] = c;
        }
    }
}

static void desenhar_icone(superficie_t *s, int i, int x, int y)
{
    int l = celula_l();
    if (ic_sel[i]) {
        sup_tingir(s, x + 2, y - 2, l - 4, celula_a() + 2, 0x18A65A, 90);
        sup_moldura(s, x + 2, y - 2, l - 4, celula_a() + 2, 0xE8BE30);
    }
    sup_icone_escala(s, x + (l - lado_icone) / 2, y, *icones[i].desenho, ICONE_LADO, lado_icone);
    const char *nome = i == ICONE_PENDRIVE ? nome_pendrive : icones[i].nome;
    texto_com_sombra(s, x + (l - texto_largura(nome)) / 2, y + lado_icone + 4, nome, 0xFFFFFF);
}

void area_icone_fantasma(superficie_t *s, int i, int x, int y) { desenhar_icone(s, i, x, y); }

/* Junta o papel de parede, o mascote e os ícones. Mover um ícone só refaz
 * isto: a imagem não é decodificada de novo. */
static void compor_fundo(void)
{
    memcpy(fundo, papel, (uint32_t)(larg * alt) * 4);
    superficie_t s;
    sup_iniciar(&s, fundo, larg, alt);
    /* O mascote, grande, no canto de baixo à direita. */
    int esc = larg >= 1000 ? 5 : 3;
    int mx = larg - 32 * esc - 30, my = alt - 32 * esc - 40;
    sup_desenho(&s, mx, my, &arte_sagui, esc);
    texto_com_sombra(&s, mx + 16 * esc - texto_largura("SaguiOS") / 2, my + 32 * esc + 6, "SaguiOS", 0xFFFFFF);
    for (int i = 0; i < QTD_ICONES; i++)
        if (icone_visivel(i)) desenhar_icone(&s, i, ic_x[i], ic_y[i]);
}

static void desenhar_fundo(void)
{
    if (fundo_escolhido != 0 || !desenhar_fundo_imagem()) desenhar_fundo_padrao(fundo_escolhido == 2);
    compor_fundo();
}

/* Põe os ícones em colunas, a partir do canto. */
void area_icones_organizar(void)
{
    int linhas = (alt - ICONE_Y0) / celula_a();
    if (linhas < 1) linhas = 1;
    for (int i = 0; i < QTD_ICONES; i++) {
        ic_x[i] = ICONE_X + i / linhas * celula_l();
        ic_y[i] = ICONE_Y0 + i % linhas * celula_a();
    }
    if (fundo) compor_fundo();
}

static bool celula_ocupada(int i, int x, int y)
{
    for (int k = 0; k < QTD_ICONES; k++)
        if (k != i && icone_visivel(k) && ic_x[k] == x && ic_y[k] == y) return true;
    return false;
}

void area_icone_mover(int i, int x, int y)
{
    int l = celula_l(), a = celula_a();
    if (grade_ligada) {
        int col = (x - ICONE_X + l / 2) / l, lin = (y - ICONE_Y0 + a / 2) / a;
        int cols = (larg - ICONE_X) / l, lins = (alt - ICONE_Y0) / a;
        if (col < 0) col = 0;
        if (lin < 0) lin = 0;
        if (col >= cols) col = cols - 1;
        if (lin >= lins) lin = lins - 1;
        /* Célula ocupada: desce até achar uma livre, e passa à coluna seguinte. */
        for (int n = 0; n < cols * lins && celula_ocupada(i, ICONE_X + col * l, ICONE_Y0 + lin * a); n++)
            if (++lin >= lins) { lin = 0; col = (col + 1) % cols; }
        x = ICONE_X + col * l;
        y = ICONE_Y0 + lin * a;
    }
    if (x < 0) x = 0;
    if (y < BARRA_SUPERIOR) y = BARRA_SUPERIOR;
    if (x > larg - l) x = larg - l;
    if (y > alt - a) y = alt - a;
    ic_x[i] = x; ic_y[i] = y;
    compor_fundo();
    klog("[GUI] icone %d movido para (%d,%d)\n", i, x, y);
}

void area_selecionar(int x, int y, int l, int a)
{
    int n = 0;
    for (int i = 0; i < QTD_ICONES; i++) {
        int il = celula_l(), ia = celula_a();
        ic_sel[i] = icone_visivel(i) && ic_x[i] < x + l && ic_x[i] + il > x && ic_y[i] < y + a && ic_y[i] + ia > y;
        n += ic_sel[i];
    }
    compor_fundo();
    klog("[GUI] icones selecionados: %d\n", n);
}

bool area_tem_selecao(void)
{
    for (int i = 0; i < QTD_ICONES; i++) if (ic_sel[i]) return true;
    return false;
}

bool area_limpar_selecao(void)
{
    bool havia = area_tem_selecao();
    memset(ic_sel, 0, sizeof ic_sel);
    if (havia) compor_fundo();
    return havia;
}

void area_mover_selecao(int i, int dx, int dy)
{
    for (int k = 0; k < QTD_ICONES; k++)
        if (k != i && ic_sel[k]) area_icone_mover(k, ic_x[k] + dx, ic_y[k] + dy);
}

/* A lixeira e o pendrive não vão para a lixeira. */
void area_jogar_fora(int i)
{
    for (int k = 0; k < QTD_ICONES; k++)
        if ((i < 0 ? ic_sel[k] : k == i) && k != ICONE_LIXEIRA && k != ICONE_PENDRIVE) {
            ic_lixo[k] = true;
            klog("[GUI] icone %s na lixeira\n", icones[k].nome);
        }
    memset(ic_sel, 0, sizeof ic_sel);
    compor_fundo();
}

void area_restaurar_icones(void)
{
    memset(ic_lixo, 0, sizeof ic_lixo);
    compor_fundo();
    klog("[GUI] icones restaurados\n");
}

void area_atualizar(void)
{
    memset(ic_sel, 0, sizeof ic_sel);
    desenhar_fundo();
    klog("[GUI] area de trabalho atualizada\n");
}

void area_grade(bool ligada)
{
    grade_ligada = ligada;
    if (ligada) area_icones_organizar();
    klog("[GUI] icones: grade %s\n", ligada ? "ligada" : "desligada");
}

void area_icones_tamanho(int lado)
{
    lado_icone = lado;
    area_icones_organizar();
    klog("[GUI] icones: tamanho %d\n", lado);
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
    papel = kmalloc((uint32_t)(larg * alt) * 4);
    arte_carregar_icones();
    if (!fundo || !papel) return false;
    area_icones_organizar();
    /* O Cipó começa no canto oposto, longe das janelas. */
    ic_x[ICONE_CIPO] = larg - celula_l();
    ic_y[ICONE_CIPO] = ICONE_Y0;
    ic_x[ICONE_LIXEIRA] = larg - celula_l();
    ic_y[ICONE_LIXEIRA] = alt - 2 * celula_a() - 110;
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
    int x_hora = larg - texto_largura(hora) - 10;
    sup_texto(s, x_hora, 3, hora, 0xFFFFFF);

    /* Botão de desligar na barra superior */
    int btn_l = 84, btn_a = 18;
    int btn_x = x_hora - btn_l - 16;
    int btn_y = 2;
    sup_degrade(s, btn_x, btn_y, btn_l, btn_a, 0xB03028, 0x7E1C18);
    sup_texto(s, btn_x + (btn_l - texto_largura("Desligar")) / 2, btn_y + 1, "Desligar", 0xFFFFFF);

    if (novo) klog("[GUI] relogio: %s\n", hora);
}

bool area_desligar_clicado(int x, int y)
{
    if (y < 0 || y >= BARRA_SUPERIOR) return false;
    char hora[48];
    data_hora_t d;
    cmos_ler_data(&d);
    static const char *semana[] = { "dom", "seg", "ter", "qua", "qui", "sex", "sáb" };
    snformatar(hora, sizeof hora, "%s %02u/%02u/%u  %02u:%02u", semana[dia_da_semana(d.ano, d.mes, d.dia)],
               d.dia, d.mes, d.ano, d.hora, d.minuto);
    int btn_l = 84;
    int btn_x = larg - texto_largura(hora) - 10 - btn_l - 16;
    return (x >= btn_x - 6 && x < btn_x + btn_l + 6);
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
    compor_fundo();
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
    if (i == ICONE_LIXEIRA) {
        char *argv[] = { "arquivos", "/.lixeira", NULL };
        processo_executar("/bin/arquivos", 2, argv, NULL);
        return;
    }
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
