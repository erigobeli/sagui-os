/* area_trabalho.c - o fundo, o mascote, os ícones e a barra superior (H20, H28).
 *
 * O fundo não muda: é desenhado uma vez numa superfície guardada (degradê
 * verde, amarelo e azul, o sagui no canto e os ícones), e a cada
 * redesenho só a parte necessária é copiada. A barra superior é desenhada
 * por cima, porque o relógio muda. */
#include "gui.h"
#include "hda.h"
#include "vfs.h"
#include "abi.h"
#include "arte.h"
#include "console.h"
#include "memoria.h"
#include "processo.h"
#include "relogio.h"
#include "configuracoes.h"
#include "rede.h"
#include "fat32.h"
#include "texto.h"
#include "texto.h"
#include "video.h"
#include "disco.h"
#include "sistema.h"
#include "teclado.h"

static uint32_t *fundo;
static int larg, alt;
static int minuto_mostrado = -1;
static int rede_mostrada = -1;
static int volume_mostrado = -1;

typedef struct {
    const char *nome;
    const char *programa;
    const uint32_t *px;        /* ícone 64x64 em ARGB (tools/mkarte.py, H81) */
    const char *arg;           /* argumento do programa (a Lixeira abre o Arquivos nela) */
    const char *chave;         /* nome nas configurações, se não for o do programa */
} icone_t;

static const icone_t icones[] = {
    { "Arquivos",    "/bin/arquivos",    arte_px_arquivos, NULL, NULL },
    { "Terminal",    "/bin/terminal",    arte_px_terminal, NULL, NULL },
    { "Editor",      "/bin/editor",      arte_px_editor, NULL, NULL },
    { "Calculadora", "/bin/calculadora", arte_px_calculadora, NULL, NULL },
    { "Banana",      "/bin/banana",      arte_px_banana, NULL, NULL },
    { "Sagui 3D",    "/bin/sagui3d",     arte_px_sagui3d, NULL, NULL },   /* bônus B4 */
    { "Configurações", "/bin/configuracoes", arte_px_configuracoes, NULL, NULL },   /* H38 */
    { "Sobre",       "/bin/sobre-o-sistema",   arte_px_sobre, NULL, NULL },
    { "Lixeira",     "/bin/arquivos",    arte_px_lixeira, "/.lixeira", "lixeira" },   /* a lixeira do Arquivos */
};
#define QTD_ICONES (int)(sizeof icones / sizeof icones[0])
#define ICONE_LIXEIRA  (QTD_ICONES - 1)
static bool lixeira_cheia;   /* troca o desenho: com papéis saindo */
/* A pasta que o ícone abre: a do sistema, ou a do pendrive quando só ela tem
 * coisas (senão o ícone cheio abriria uma lixeira vazia). */
static const char *lixeira_abrir = "/.lixeira";

/* A coluna da direita, no canto de cima, como no Linux: o instalador (até
 * o sistema estar instalado) e as unidades (pendrives e o disco do
 * SaguiOS), um ícone cada. Os índices vêm depois dos programas instalados:
 * base_dir() é o instalador e base_dir() + 1 + k a unidade k. */
#define MAX_UNIDADES 4
static struct { char disco[8], nome[24]; bool interno; } unid[MAX_UNIDADES];
static int qtd_unid;
static bool mostrar_instalador;

/* Programas instalados no pendrive (H39): /usb/programas, um ícone cada,
 * depois dos ícones do sistema. A pasta é conferida a cada 2 segundos. */
#define MAX_INSTALADOS 8
static char instalados[MAX_INSTALADOS][NOME_MAX];
static int qtd_instalados;
static uint32_t conferido_em;

/* Programas do sistema escondidos pelo usuário (configuração
 * "programas.ocultos", uma lista separada por vírgulas). Eles continuam em
 * /bin e no Terminal; só o ícone some. */
static bool oculto[16];

/* O nome do ícone nas configurações (icone.<chave>, programas.ocultos). */
static const char *chave_icone(int i)
{
    return icones[i].chave ? icones[i].chave : icones[i].programa + 5;   /* depois de "/bin/" */
}

static int base_dir(void) { return QTD_ICONES + qtd_instalados; }

static bool icone_visivel(int i)
{
    if (i >= base_dir()) return i == base_dir() ? mostrar_instalador : i - base_dir() - 1 < qtd_unid;
    if (i >= QTD_ICONES) return i - QTD_ICONES < qtd_instalados;
    return !oculto[i];
}

static void ler_ocultos(void)
{
    const char *lista = config_obter("programas.ocultos");
    for (int i = 0; i < QTD_ICONES && i < 16; i++) {
        oculto[i] = false;
        if (!lista) continue;
        const char *nome = chave_icone(i);
        size_t n = txt_tamanho(nome);
        for (const char *p = lista; *p;) {
            while (*p == ',' || *p == ' ') p++;
            const char *fim = p;
            while (*fim && *fim != ',') fim++;
            size_t m = (size_t)(fim - p);
            while (m > 0 && p[m - 1] == ' ') m--;
            if (m == n && txt_compara_n(p, nome, n) == 0) oculto[i] = true;
            p = fim;
        }
    }
}

static const char *nome_icone(int i)
{
    if (i == base_dir()) return "Instalar o SaguiOS";
    if (i > base_dir()) return unid[i - base_dir() - 1].nome;
    if (i >= QTD_ICONES) return instalados[i - QTD_ICONES];
    /* Nome trocado pelo usuário (botão direito > Renomear): icone.<programa>. */
    char chave[48];
    snformatar(chave, sizeof chave, "icone.%s", chave_icone(i));
    const char *apelido = config_obter(chave);
    return apelido && apelido[0] ? apelido : icones[i].nome;
}

const char *area_icone_nome(int i) { return nome_icone(i); }   /* o menu Iniciar */

/* Que tipo de ícone é (para o menu do botão direito): 0 do sistema,
 * 1 programa instalado no pendrive, 2 uma unidade, 3 o instalador. */
int area_icone_tipo(int i)
{
    if (i == base_dir()) return 3;
    if (i > base_dir()) return 2;
    return i >= QTD_ICONES ? 1 : 0;
}

/* Ações do menu do botão direito. Renomear e remover perguntam (texto ou
 * confirmação) num programa comum, /bin/icone, porque o kernel não tem
 * caixas de texto. */
void area_icone_acao(int i, int acao)
{
    char caminho[NOME_MAX + 24], nome[48];
    if (i < 0 || i >= area_qtd_icones()) return;
    if (i >= base_dir()) {                         /* instalador e unidades: só abrir e remover */
        if (acao == ACAO_ABRIR) area_icone_abrir(i);
        else if (acao == ACAO_DESMONTAR) area_icone_direito(i);
        return;
    }
    txt_copia_n(nome, nome_icone(i), sizeof nome);
    if (i >= QTD_ICONES) snformatar(caminho, sizeof caminho, "/usb/programas/%s", instalados[i - QTD_ICONES]);
    else snformatar(caminho, sizeof caminho, "/bin/%s", chave_icone(i));   /* /bin/icone usa o fim do caminho */
    if (acao == ACAO_ABRIR) { area_icone_abrir(i); return; }
    if (acao == ACAO_DESMONTAR) { area_icone_direito(i); return; }
    if (acao == ACAO_RENOMEAR) { area_editar(i); return; }   /* no lugar, sem janela */
    const char *verbo = acao == ACAO_RENOMEAR ? (i >= QTD_ICONES ? "renomear-programa" : "renomear-icone")
                      : acao == ACAO_REMOVER ? "remover" : "esconder";
    char *argv[] = { "icone", (char *)verbo, caminho, nome, NULL };
    int pid = processo_executar("/bin/icone", 4, argv, NULL);
    klog("[GUI] icone %s: %s %s (pid %d)\n", nome, verbo, caminho, pid);
}

static const uint32_t *px_icone(int i)
{
    if (i == ICONE_LIXEIRA && lixeira_cheia) return arte_px_lixeira_cheia;
    if (i == base_dir()) return arte_px_instalar_sistema;
    if (i > base_dir()) return unid[i - base_dir() - 1].interno ? arte_px_disco : arte_px_pendrive;
    return i >= QTD_ICONES ? arte_px_instalado : icones[i].px;
}
#define ICONE_X 20
#define ICONE_Y0 (BARRA_SUPERIOR + 16)
#define ICONE_PASSO 92
#define ICONE_COLUNA 110   /* distância entre as colunas de ícones */
#define ICONE_L 96
#define ICONE_A 88

int area_qtd_icones(void) { return base_dir() + 1 + qtd_unid; }

/* O nome do ícone como é desenhado: l1 é a primeira linha e o retorno é a
 * segunda (NULL se o nome cabe numa linha só). Nome comprido ("Instalar o
 * SaguiOS") quebra no último espaço que cabe, só na coluna da direita, que
 * deixa 16 px a mais entre os ícones; na da esquerda o nome é cortado com
 * "...". O desenho e o retângulo do ícone usam esta mesma conta. */
static const char *nome_em_linhas(int i, char l1[32])
{
    const char *nome = nome_icone(i);
    txt_copia_n(l1, nome, 32);
    if (texto_largura(l1) <= ICONE_L + 10) return NULL;
    if (i >= base_dir()) {
        for (int k = (int)txt_tamanho(l1) - 1; k > 0; k--)
            if (l1[k] == ' ') {
                l1[k] = 0;
                if (texto_largura(l1) <= ICONE_L + 10) return nome + k + 1;
            }
        txt_copia_n(l1, nome, 32);
    }
    for (int k = (int)txt_tamanho(l1) - 1; k > 0; k--) {
        l1[k] = 0;
        if (k + 3 < 32) { l1[k] = '.'; l1[k + 1] = '.'; l1[k + 2] = '.'; l1[k + 3] = 0; }
        if (texto_largura(l1) <= ICONE_L + 10) break;
        l1[k] = 0;
    }
    return NULL;
}

void area_icone_retangulo(int i, int *x, int *y, int *l, int *a)
{
    /* Os ícones descem pela coluna da esquerda e, quando não cabem mais
     * (8 ou mais numa tela de 768 linhas), continuam numa segunda coluna. */
    int por_coluna = (alt - ICONE_Y0) / ICONE_PASSO;
    if (por_coluna < 1) por_coluna = 1;
    /* A posição na tela conta só os ícones visíveis antes deste (um
     * programa oculto não deixa buraco). */
    int vaga = 0;
    if (i >= base_dir()) {                         /* a coluna da direita, de cima para baixo */
        for (int k = base_dir(); k < i; k++) if (icone_visivel(k)) vaga++;
        *x = larg - ICONE_X - ICONE_L;
        *y = ICONE_Y0 + vaga * (ICONE_PASSO + 16);   /* + 16: nomes em duas linhas */
    } else {
        /* A Lixeira fica sempre por último, depois do pendrive e dos programas
         * instalados, como no fim da fila de ícones dos outros sistemas. */
        int ate = i == ICONE_LIXEIRA ? QTD_ICONES + qtd_instalados : i;
        for (int k = 0; k < ate; k++) if (k != ICONE_LIXEIRA && icone_visivel(k)) vaga++;
        *x = ICONE_X + (vaga / por_coluna) * ICONE_COLUNA;
        *y = ICONE_Y0 + (vaga % por_coluna) * ICONE_PASSO;
    }
    *l = ICONE_L; *a = ICONE_A;
    /* A seleção cobre o nome inteiro: desce até a segunda linha e, se o nome
     * é mais largo que o ícone ("Configurações"), alarga para os dois lados. */
    char l1[32];
    const char *l2 = nome_em_linhas(i, l1);
    int w = texto_largura(l1);
    if (l2) {
        *a += 16;
        if (texto_largura(l2) > w) w = texto_largura(l2);
    }
    if (w + 8 > *l) {
        *x -= (w + 8 - *l) / 2;
        *l = w + 8;
    }
}

/* ------------------------------------------------------------ seleção (issue #25, parte 3)
 *
 * O laço translúcido da área de trabalho veio do commit 0b3c462 do Robson
 * (ramo area-lixeira, do código da 1.0), refeito para o código atual: os
 * ícones tocados pelo retângulo ficam marcados; um clique num ícone marca só
 * ele; clicar no vazio desmarca. */
#define MAX_ICONES (QTD_ICONES + MAX_INSTALADOS + 1 + MAX_UNIDADES)
static bool selecionado[MAX_ICONES];

void area_selecionar(int x, int y, int l, int a, bool somar)
{
    for (int i = 0; i < area_qtd_icones(); i++) {
        if (!somar) selecionado[i] = false;
        if (!icone_visivel(i)) continue;
        int ix, iy, il, ia;
        area_icone_retangulo(i, &ix, &iy, &il, &ia);
        if (ix < x + l && x < ix + il && iy < y + a && y < iy + ia) selecionado[i] = true;
    }
}

bool area_limpar_selecao(void)
{
    bool havia = false;
    for (int i = 0; i < MAX_ICONES; i++) { havia |= selecionado[i]; selecionado[i] = false; }
    return havia;
}

bool area_icone_selecionado(int i)
{
    return i >= 0 && i < area_qtd_icones() && selecionado[i] && icone_visivel(i);
}

int area_qtd_selecionados(void)
{
    int n = 0;
    for (int i = 0; i < area_qtd_icones(); i++) n += area_icone_selecionado(i);
    return n;
}

/* ------------------------------------------------------------ renomear no lugar
 *
 * Um segundo clique, devagar, num ícone já marcado (ou botão direito >
 * Renomear) troca o nome embaixo do ícone por uma caixa de texto: o nome
 * vem todo marcado, digitar substitui, Enter grava, Esc desiste e um clique
 * fora também grava. Ícone do sistema: só o nome que aparece
 * (icone.<programa> nas configurações). Programa instalado: o arquivo em
 * /usb/programas muda de nome. O pendrive não se renomeia. */
static void desenhar_fundo(void);
static int ed_icone = -1;
static char ed_texto[48];
static int ed_cur;
static bool ed_tudo;   /* o nome inteiro marcado: a próxima letra substitui */

bool area_editando(void) { return ed_icone >= 0; }

bool area_editar(int i)
{
    if (i < 0 || i >= QTD_ICONES + qtd_instalados || !icone_visivel(i) || area_icone_tipo(i) == 2) return false;
    ed_icone = i;
    txt_copia_n(ed_texto, nome_icone(i), sizeof ed_texto);
    ed_cur = (int)txt_tamanho(ed_texto);
    ed_tudo = true;
    desenhar_fundo();   /* o nome some do fundo: a caixa fica no lugar */
    klog("[GUI] renomear no lugar: icone %d (%s)\n", i, ed_texto);
    return true;
}

void area_editar_terminar(bool gravar)
{
    if (ed_icone < 0) return;
    int i = ed_icone;
    ed_icone = -1;
    const char *antes = nome_icone(i);
    if (!gravar || !ed_texto[0] || txt_compara(ed_texto, antes) == 0) {
        klog("[GUI] renomear no lugar: nome mantido\n");
    } else if (i >= QTD_ICONES) {
        char de[NOME_MAX + 16], para[64];
        snformatar(de, sizeof de, "programas/%s", instalados[i - QTD_ICONES]);
        snformatar(para, sizeof para, "programas/%s", ed_texto);
        /* Sem "/" e sem começar com ponto ("." e ".." são as pastas; com
         * ponto, o programa some da lista). E nunca por cima de outro: o
         * fat_renomear substitui um arquivo com o nome novo, e com o nome de
         * uma pasta move o programa para dentro dela. */
        bool barra = ed_texto[0] == '.';
        for (const char *c = ed_texto; *c; c++) barra |= *c == '/';
        info_arquivo_t ja;
        int r = barra ? ERRO_INVALIDO : !disco_montado() ? ERRO_NAO_EXISTE
              : fat_info(para, &ja) == 0 ? ERRO_JA_EXISTE : fat_renomear(de, para);
        if (r == 0) txt_copia_n(instalados[i - QTD_ICONES], ed_texto, NOME_MAX);
        else gui_aviso(r == ERRO_INVALIDO ? "O nome não pode ter / nem começar com ponto."
                       : r == ERRO_JA_EXISTE ? "Já existe um programa ou pasta com esse nome."
                       : "Não consegui renomear o programa.");
        klog("[GUI] icone renomeado: /usb/%s -> /usb/%s (%d)\n", de, para, r);
    } else {
        char chave[48];
        snformatar(chave, sizeof chave, "icone.%s", chave_icone(i));
        int r = config_definir(chave, ed_texto);
        klog("[GUI] icone renomeado: %s agora se chama %s (%d)\n", chave_icone(i), ed_texto, r);
    }
    desenhar_fundo();
}

/* Uma tecla para a caixa. Devolve false se a tecla não é dela. */
bool area_editar_tecla(int t)
{
    if (ed_icone < 0) return false;
    int n = (int)txt_tamanho(ed_texto);
    if (t == '\n') { area_editar_terminar(true); return true; }
    if (t == TECLA_ESC) { area_editar_terminar(false); return true; }
    if (ed_tudo && (t == '\b' || t == TECLA_DELETE || (t >= 0x20 && t < 0x7F) || (t >= 0xA0 && t <= 0xFF))) {
        ed_texto[0] = 0; n = 0; ed_cur = 0;   /* apaga o nome marcado; a letra entra a seguir */
        ed_tudo = false;
        if (t == '\b' || t == TECLA_DELETE) return true;
    }
    ed_tudo = false;
    if (t == '\b') {
        if (ed_cur > 0) { for (int k = ed_cur - 1; k < n; k++) ed_texto[k] = ed_texto[k + 1]; ed_cur--; }
    } else if (t == TECLA_DELETE) {
        if (ed_cur < n) for (int k = ed_cur; k < n; k++) ed_texto[k] = ed_texto[k + 1];
    } else if (t == TECLA_ESQUERDA) { if (ed_cur > 0) ed_cur--; }
    else if (t == TECLA_DIREITA) { if (ed_cur < n) ed_cur++; }
    else if (t == TECLA_HOME) ed_cur = 0;
    else if (t == TECLA_END) ed_cur = n;
    else if ((t >= 0x20 && t < 0x7F) || (t >= 0xA0 && t <= 0xFF)) {
        if (n + 1 < (int)sizeof ed_texto) {
            for (int k = n; k >= ed_cur; k--) ed_texto[k + 1] = ed_texto[k];
            ed_texto[ed_cur++] = (char)t;
        }
    }
    return true;   /* com a caixa aberta, as outras teclas não vão para as janelas */
}

/* Para desenhar a caixa: o ícone, o texto, o cursor e se está tudo marcado. */
int area_editar_estado(const char **texto, int *cur, bool *tudo)
{
    *texto = ed_texto; *cur = ed_cur; *tudo = ed_tudo;
    return ed_icone;
}

/* ------------------------------------------------------------ fundos (H81)
 *
 * "Brasil com sol" e "Mata Atlântica" vieram do pull request #1 do Robson
 * de Lima Barbosa. O primeiro é desenhado por código; o segundo é uma imagem
 * (gerada por IA, com o modelo Z-Image Turbo, a pedido do Robson) no formato
 * .sgi descrito em tools/mkfundo.py. */

/* Seno de um ângulo de 0 a 1023 (volta inteira), de -256 a 256, por
 * parábolas: o kernel não usa ponto flutuante. */
static int seno(int a)
{
    a &= 1023;
    int t = a & 511, v = t * (512 - t) / 256;
    return a < 512 ? v : -v;
}

/* Altura de uma onda (copa da mata ou rio) na coluna x: duas senoides somadas. */
static int onda(int x, int base, int amplitude, int passo, int fase)
{
    return base + (seno(x * passo / 16 + fase) * amplitude + seno(x * passo / 7 + fase * 3) * amplitude / 3) / 256;
}

/* Degradê verde-amarelo-azul com a luz de um sol no meio, a copa da mata no
 * alto e ondas de rio embaixo. Uma trama 4x4 disfarça as faixas do degradê. */
static void fundo_brasil_com_sol(void)
{
    static const uint8_t trama[4][4] = { {0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5} };
    const uint32_t verde = 0x0B7A3E, amarelo = 0xE8BE30, azul = 0x14337E, sol = 0xFFF0A0;
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

/* Imagem .sgi (tools/mkfundo.py): "SGI1", largura, altura, paleta de 256
 * cores e os índices em RLE. Em outra resolução, pega o pixel mais próximo.
 * Devolve false se o arquivo faltar ou estiver errado. */
#define SGI_CAB 776
static bool fundo_imagem(const char *caminho)
{
    uint8_t *d;
    uint32_t tam;
    if (vfs_ler_inteiro(caminho, &d, &tam) < 0) return false;
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
            for (int y = 0; y < alt; y++) {
                const uint8_t *linha = idx + (y * ia / alt) * il;
                for (int x = 0; x < larg; x++) {
                    const uint8_t *c = d + 8 + linha[x * il / larg] * 3;
                    fundo[y * larg + x] = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
                }
            }
            ok = true;
            klog("[GUI] fundo: %s %dx%d\n", caminho, il, ia);
        }
        kfree(idx);
    }
    kfree(d);
    return ok;
}

static void texto_com_sombra(superficie_t *s, int x, int y, const char *t, uint32_t cor)
{
    /* Contorno escuro embaixo e à direita: o nome continua legível em cima
     * da imagem da Mata Atlântica (H81, do Robson). */
    sup_texto(s, x + 1, y + 1, t, 0x101010);
    sup_texto(s, x + 1, y, t, 0x101010);
    sup_texto(s, x, y + 1, t, 0x101010);
    sup_texto(s, x, y, t, cor);
}

static void desenhar_fundo(void)
{
    superficie_t s;
    sup_iniciar(&s, fundo, larg, alt);
    /* Degradê vertical em três cores, escolhido em Configurações > Aparência
     * (H38). O padrão são as cores do Brasil (verde, amarelo, azul) em tons
     * mais suaves que os da bandeira, para o texto e os ícones continuarem
     * legíveis. */
    uint32_t c0 = 0x0B7A3E, c1 = 0xE0B92A, c2 = 0x14337E;
    const char *tema = config_obter("aparencia.fundo");
    bool pronto = false;
    if (tema && txt_compara(tema, "mata-atlantica") == 0) pronto = fundo_imagem("/arte/mata-atlantica.sgi");
    if (!pronto && tema && (txt_compara(tema, "sol") == 0 || txt_compara(tema, "mata-atlantica") == 0)) {
        fundo_brasil_com_sol();   /* (também a reserva, se a imagem faltar) */
        pronto = true;
    }
    if (tema && txt_compara(tema, "mata") == 0)       { c0 = 0x0E3B22; c1 = 0x2E7D32; c2 = 0x0A2416; }
    else if (tema && txt_compara(tema, "ceu") == 0)   { c0 = 0x1E5AA8; c1 = 0x6FB3E8; c2 = 0xD8EEF8; }
    else if (tema && txt_compara(tema, "noite") == 0) { c0 = 0x0B1026; c1 = 0x2B1E5A; c2 = 0x060814; }
    else if (tema && txt_compara(tema, "terra") == 0) { c0 = 0x7A3E14; c1 = 0xD98A3A; c2 = 0x3A1E0C; }
    for (int y = 0; y < alt && !pronto; y++) {
        uint32_t c = y < alt / 2 ? cor_mistura(c0, c1, y, alt / 2)
                                 : cor_mistura(c1, c2, y - alt / 2, alt - alt / 2);
        uint32_t *p = fundo + y * larg;
        for (int x = 0; x < larg; x++) p[x] = c;
    }
    /* O mascote, grande, no canto de baixo à direita (dá para esconder). */
    const char *mascote = config_obter("aparencia.sagui");
    if (!mascote || txt_compara(mascote, "nao") != 0) {
        int esc = larg >= 1000 ? 5 : 3;
        int mx = larg - 32 * esc - 30, my = alt - 32 * esc - 40;
        sup_desenho(&s, mx, my, &arte_sagui, esc);
        texto_com_sombra(&s, mx + 16 * esc - texto_largura("SaguiOS") / 2, my + 32 * esc + 6, "SaguiOS", 0xFFFFFF);
    }
    /* Ícones na coluna da esquerda, com o nome embaixo. */
    ler_ocultos();
    for (int i = 0; i < area_qtd_icones(); i++) {
        if (!icone_visivel(i)) continue;
        int x, y, l, a;
        area_icone_retangulo(i, &x, &y, &l, &a);
        sup_icone(&s, x + (l - ICONE_LADO) / 2, y, px_icone(i), ICONE_LADO);
        if (i == ed_icone) continue;   /* renomeando: a caixa é desenhada por cima (janelas.c) */
        char l1[32];
        const char *l2 = nome_em_linhas(i, l1);
        texto_com_sombra(&s, x + (l - texto_largura(l1)) / 2, y + 68, l1, 0xFFFFFF);
        if (l2) texto_com_sombra(&s, x + (l - texto_largura(l2)) / 2, y + 84, l2, 0xFFFFFF);
    }
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

/* A lista de programas em /usb/programas mudou (H39)? Confere a cada 2 s. */
bool area_instalados_mudaram(void)
{
    if (relogio_tiques() - conferido_em < 2 * TIQUES_POR_SEGUNDO) return false;
    conferido_em = relogio_tiques();
    /* A Lixeira está cheia? (o que o Arquivos pôs em /.lixeira ou /usb/.lixeira,
     * sem contar o .origens) */
    static const char *const lixeiras[2] = { "/.lixeira", "/usb/.lixeira" };
    bool tem[2] = { false, false };
    for (int q = 0; q < 2; q++) {
        entrada_pasta_t e;
        /* /usb não está na árvore da memória: o pendrive se lê pela FAT32. */
        const char *rel;
        bool usb = vfs_na_usb(lixeiras[q], &rel);
        if (q == 1 && !usb) continue;
        for (int k = 0; (usb ? fat_listar(rel, k, &e) : vfs_listar(lixeiras[q], k, &e)) == 1; k++)
            if (e.nome[0] != '.') { tem[q] = true; break; }
    }
    bool cheia = tem[0] || tem[1];
    const char *abrir = !tem[0] && tem[1] ? lixeiras[1] : lixeiras[0];
    if (abrir != lixeira_abrir) {
        lixeira_abrir = abrir;
        klog("[GUI] lixeira abre %s\n", abrir);
    }
    if (cheia != lixeira_cheia) {
        lixeira_cheia = cheia;
        klog("[GUI] lixeira %s\n", cheia ? "cheia" : "vazia");
        desenhar_fundo();
        return true;
    }
    char novos[MAX_INSTALADOS][NOME_MAX];
    int n = 0;
    if (disco_montado()) {
        entrada_pasta_t e;
        for (int k = 0; n < MAX_INSTALADOS && fat_listar("programas", k, &e) == 1; k++)
            if (e.tipo != TIPO_PASTA && e.nome[0] != '.') txt_copia_n(novos[n++], e.nome, NOME_MAX);
    }
    bool igual = n == qtd_instalados;
    for (int k = 0; igual && k < n; k++) igual = txt_compara(novos[k], instalados[k]) == 0;
    if (igual) return false;
    for (int k = 0; k < n; k++) txt_copia_n(instalados[k], novos[k], NOME_MAX);
    qtd_instalados = n;
    klog("[GUI] programas instalados no pendrive: %d\n", n);
    desenhar_fundo();
    return true;
}

/* As configurações mudaram (H38)? Redesenha o fundo; true = redesenhar a tela. */
bool area_config_mudou(void)
{
    static uint32_t vista = 0;
    if (config_geracao() == vista) return false;
    vista = config_geracao();
    desenhar_fundo();
    return true;
}

void area_fundo(superficie_t *s)
{
    sup_copiar(s, 0, 0, fundo, larg, alt, larg);
}

bool area_relogio_mudou(void)
{
    if (rede_estado() != rede_mostrada) return true;   /* o ícone de rede também vive na barra */
    if (sis_som_volume((uint32_t)-1, 0, 0, 0, 0) != volume_mostrado && volume_mostrado != -1) return true;
    /* Ler o CMOS é lento e acontece com as interrupções desligadas: uma vez
     * por segundo basta, e o mouse não engasga (H81, do Robson). */
    static uint32_t proxima;
    uint32_t agora = relogio_tiques();
    if ((int32_t)(agora - proxima) < 0) return false;
    proxima = agora + TIQUES_POR_SEGUNDO;
    data_hora_t d;
    cmos_ler_data(&d);
    return d.minuto != minuto_mostrado;
}

/* vidro: o fundo da barra já foi pintado (vidro, H74); claro: tema claro (H76). */
void area_barra(superficie_t *s, bool vidro, bool claro, bool frase_visivel)
{
    static const char *semana[] = { "dom", "seg", "ter", "qua", "qui", "sex", "sáb" };
    uint32_t nome = claro ? 0x0B6A36 : 0xFFDF4A, frase = claro ? 0x4A5560 : 0x9AA8B8, relogio = claro ? 0x1E2328 : 0xFFFFFF;
    if (!vidro) sup_retangulo(s, 0, 0, larg, BARRA_SUPERIOR, claro ? 0xE8ECEF : 0x18202A);
    sup_retangulo(s, 0, BARRA_SUPERIOR - 1, larg, 1, 0x0B7A3E);
    /* O botão do menu (H75): cabecinha do sagui (as linhas 2 a 17 do ícone
     * "sobre"), o nome e uma setinha. */
    desenho_t cabeca = { arte_icone_sobre.linhas + 2, 32, 16 };
    sup_desenho(s, 6, 4, &cabeca, 1);
    sup_texto(s, 44, 4, "SaguiOS", nome);
    for (int k = 0; k < 4; k++) sup_retangulo(s, 104 - 4 + k, 10 + k, 8 - 2 * k, 1, nome);
    if (frase_visivel) sup_texto(s, 120, 4, "um sistema operacional escrito do zero por IA", frase);
    (void)relogio;
    data_hora_t d;
    cmos_ler_data(&d);
    minuto_mostrado = d.minuto;
    char hora[48];
    snformatar(hora, sizeof hora, "%s %02u/%02u/%u  %02u:%02u", semana[dia_da_semana(d.ano, d.mes, d.dia)],
               d.dia, d.mes, d.ano, d.hora, d.minuto);
    sup_texto(s, larg - texto_largura(hora) - 10, 4, hora, relogio);
    /* Ícone de rede (H47), à esquerda do botão de energia: verde conectado,
     * amarelo pedindo endereço, cinza sem cabo; some se não há placa. */
    int estado = rede_estado();
    rede_mostrada = estado;
    if (estado > 0) {
        int ex0, ey0, el0, ea0;
        area_energia_retangulo(&ex0, &ey0, &el0, &ea0);
        uint32_t cor = estado == 3 ? 0x5FD35F : (estado == 2 ? 0xF4D03F : 0x8A939C);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                if (arte_icone_rede[y][x] == 'B') sup_retangulo(s, ex0 - 28 + x, ey0 + 2 + y, 1, 1, cor);
        if (estado == 1) {   /* sem cabo: um x vermelho por cima */
            for (int k = 0; k < 8; k++) {
                sup_retangulo(s, ex0 - 28 + 4 + k, ey0 + 2 + 2 + k, 1, 1, 0xE04848);
                sup_retangulo(s, ex0 - 28 + 11 - k, ey0 + 2 + 2 + k, 1, 1, 0xE04848);
            }
        }
    }
    /* Som (H75), à esquerda da rede: um alto-falante com ondas pelo volume. */
    int vol = sis_som_volume((uint32_t)-1, 0, 0, 0, 0);
    if (vol >= 0) {
        int sx, sy, sl, sa;
        area_som_retangulo(&sx, &sy, &sl, &sa);
        uint32_t c = claro ? 0x2A323A : 0xE8ECEF;
        sup_retangulo(s, sx + 2, sy + 7, 3, 6, c);
        for (int k = 0; k < 5; k++) sup_retangulo(s, sx + 5 + k, sy + 7 - k, 1, 6 + 2 * k, c);
        int ondas = vol == 0 ? 0 : vol < 40 ? 1 : vol < 75 ? 2 : 3;
        for (int o = 0; o < ondas; o++) {
            int ox = sx + 12 + o * 3, meio = sy + 10, h = 3 + o * 2;
            sup_retangulo(s, ox, meio - h, 1, 2 * h, c);
        }
        if (vol == 0)
            for (int k = 0; k < 6; k++) { sup_retangulo(s, sx + 12 + k, sy + 7 + k, 1, 1, 0xE04848); sup_retangulo(s, sx + 17 - k, sy + 7 + k, 1, 1, 0xE04848); }
        volume_mostrado = vol;
    }
    /* Botão de energia (H36), à esquerda do relógio. */
    int ex, ey, el, ea;
    area_energia_retangulo(&ex, &ey, &el, &ea);
    desenho_t energia = { arte_botao_energia, 16, 16 };
    sup_desenho(s, ex + 2, ey + 2, &energia, 1);
    klog("[GUI] relogio: %s\n", hora);
}

/* Onde fica o botão de energia na barra: logo à esquerda da data e hora. */
void area_energia_retangulo(int *x, int *y, int *l, int *a)
{
    *l = 20; *a = 20; *y = 2;
    *x = larg - texto_largura("ddd 00/00/0000  00:00") - 10 - 30;
}

/* O som fica à esquerda do ícone de rede. */
void area_som_retangulo(int *x, int *y, int *l, int *a)
{
    area_energia_retangulo(x, y, l, a);
    *x -= 56;
}

bool area_som_em(int x, int y)
{
    int sx, sy, sl, sa;
    area_som_retangulo(&sx, &sy, &sl, &sa);
    return x >= sx && x < sx + sl && y >= sy && y < sy + sa;
}

bool area_energia_em(int x, int y)
{
    int ex, ey, el, ea;
    area_energia_retangulo(&ex, &ey, &el, &ea);
    return x >= ex && x < ex + el && y >= ey && y < ey + ea;
}

void area_energia_abrir(void)
{
    char *argv[] = { "energia", NULL };
    int pid = processo_executar("/bin/energia", 1, argv, NULL);
    klog("[GUI] botao de energia -> /bin/energia (pid %d)\n", pid);
}

int area_icone_em(int x, int y)
{
    for (int i = 0; i < area_qtd_icones(); i++) {
        if (!icone_visivel(i)) continue;
        int ix, iy, l, a;
        area_icone_retangulo(i, &ix, &iy, &l, &a);
        if (x >= ix && x < ix + l && y >= iy && y < iy + a) return i;
    }
    return -1;
}

/* As unidades e o instalador da coluna da direita. Confere os discos a
 * cada volta do gerenciador; devolve true se algo mudou. Entram os
 * pendrives (menos o de boot, que é o próprio sistema) e, dos discos
 * internos, só a partição do SaguiOS (os outros nem montam, D062). */
bool area_unidades(void)
{
    typeof(unid) novo;
    int n = 0;
    memset(novo, 0, sizeof novo);
    for (int i = 0; i < MAX_DISCOS && n < MAX_UNIDADES; i++) {
        disco_t *d = disco_indice(i);
        if (!d || !d->presente || d->de_boot) continue;
        if (d->interno && txt_compara(d->rotulo, "SAGUIOS") != 0) continue;
        txt_copia_n(novo[n].disco, d->nome, sizeof novo[n].disco);
        txt_copia_n(novo[n].nome, d->interno ? "Disco" : d->rotulo[0] ? d->rotulo : "Pendrive", sizeof novo[n].nome);
        novo[n].interno = d->interno;
        n++;
    }
    /* O instalador some quando o sistema ligou pelo disco instalado. */
    const char *inst = config_obter("sistema.instalado");
    bool instalador = !(inst && txt_compara(inst, "sim") == 0) && vfs_achar("/bin/instalar") != NULL;
    if (n == qtd_unid && instalador == mostrar_instalador && memcmp(novo, unid, sizeof unid) == 0) return false;
    memcpy(unid, novo, sizeof unid);
    qtd_unid = n;
    mostrar_instalador = instalador;
    klog("[GUI] coluna da direita: instalador %s, %d unidades\n", instalador ? "sim" : "nao", n);
    desenhar_fundo();
    return true;
}

static disco_t *disco_da_unidade(int i)
{
    int k = i - base_dir() - 1;
    return k >= 0 && k < qtd_unid ? disco_por_nome(unid[k].disco) : NULL;
}

/* Botão direito numa unidade: "remover com segurança". */
void area_icone_direito(int i)
{
    disco_t *d = disco_da_unidade(i);
    if (!d) return;
    if (d->montado) disco_desmontar(d);
    else gui_aviso(d->interno ? "O disco não está montado." : "O pendrive não está montado: pode remover.");
}

/* Abre o programa do ícone (H28). Ele não tem pai: ninguém espera por ele. */
void area_icone_abrir(int i)
{
    if (i < 0 || i >= area_qtd_icones()) return;
    if (i == base_dir()) {
        char *argv[] = { "instalar", NULL };
        int pid = processo_executar("/bin/instalar", 1, argv, NULL);
        klog("[GUI] icone Instalar o SaguiOS -> /bin/instalar (pid %d)\n", pid);
        return;
    }
    if (i > base_dir()) {
        disco_t *d = disco_da_unidade(i);
        if (!d) return;
        disco_t *m = disco_montado();
        if (m && m != d) {                          /* só um disco fica em /usb por vez */
            gui_aviso("Outra unidade está aberta em /usb. Remova-a com segurança antes.");
            return;
        }
        int r = disco_montar(d);
        if (r == ERRO_FORMATO) { gui_aviso("Esta unidade não está em FAT32: formato não suportado."); return; }
        if (r < 0) { gui_aviso("Não consegui montar a unidade."); return; }
        char *argv[] = { "arquivos", "/usb", NULL };
        int pid = processo_executar("/bin/arquivos", 2, argv, NULL);
        klog("[GUI] icone %s (%s) -> /bin/arquivos /usb (pid %d)\n", unid[i - base_dir() - 1].nome, d->nome, pid);
        return;
    }
    if (i >= QTD_ICONES) {
        char caminho[NOME_MAX + 24];
        snformatar(caminho, sizeof caminho, "/usb/programas/%s", instalados[i - QTD_ICONES]);
        /* Só um programa com a marca de janela (e_flags, posta pelo cc) abre
         * direto. Os de texto e os sem marca (compilados antes da marca
         * existir) abrem dentro de um Terminal: senão rodavam sem janela,
         * terminavam e nada aparecia na tela. */
        uint8_t *img = NULL;
        uint32_t tam = 0;
        bool janela = true;
        if (vfs_ler_inteiro(caminho, &img, &tam) == 0 && tam >= 52) {
            uint32_t flags = (uint32_t)img[36] | ((uint32_t)img[37] << 8) | ((uint32_t)img[38] << 16) | ((uint32_t)img[39] << 24);
            janela = flags == ELF_SAGUI_JANELA;
        }
        if (img) kfree(img);
        int pid;
        if (janela) {
            char *argv[] = { instalados[i - QTD_ICONES], NULL };
            pid = processo_executar(caminho, 1, argv, NULL);
        } else {
            char *argv[] = { "terminal", "-e", caminho, NULL };
            pid = processo_executar("/bin/terminal", 3, argv, NULL);
        }
        klog("[GUI] icone instalado %s -> %s%s (pid %d)\n", instalados[i - QTD_ICONES], janela ? "" : "terminal -e ", caminho, pid);
        return;
    }
    const char *arg = i == ICONE_LIXEIRA ? lixeira_abrir : icones[i].arg;
    char *argv[] = { (char *)icones[i].programa + 5, (char *)arg, NULL };
    int pid = processo_executar(icones[i].programa, arg ? 2 : 1, argv, NULL);
    klog("[GUI] icone %s -> %s (pid %d)\n", icones[i].nome, icones[i].programa, pid);
}
