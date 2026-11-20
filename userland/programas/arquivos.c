/* arquivos [pasta] - o gerenciador de arquivos do SaguiOS (H23).
 *
 * Mostra uma pasta como lista com ícones (pastas primeiro). Duplo clique:
 * pasta entra, arquivo de texto abre no Editor, programa executa. Botões
 * para voltar, criar pasta e apagar (com confirmação).
 *
 * Lixeira (issue #25, parte 5): "Apagar" move para a lixeira em vez de apagar
 * de vez. Cada disco tem a sua, porque mover entre a memória e o pendrive não
 * é renomear: /.lixeira e /usb/.lixeira. O arquivo .origens de cada lixeira
 * guarda, por linha, "nome na lixeira<TAB>caminho original", para restaurar.
 * Nomes que começam com ponto não aparecem na lista. Na lixeira, os botões
 * viram Restaurar, Apagar de vez e Esvaziar. Shift+Delete apaga de vez. */
#include "sagui.h"
#include "gui.h"

static int larg_jan = 900, alt_jan = 560;   /* a janela pode mudar de tamanho (H78) */
#define LARG larg_jan
#define ALT  alt_jan
#define MAX_ITENS 128

/* Visual (H136): barra de
 * comandos em cima, barra de endereço com voltar/avançar/subir/atualizar, o
 * caminho clicável e a pesquisa, painel de navegação à esquerda, lista em
 * colunas (nome, data de modificação, tipo, tamanho) e barra de status. */
#define CMD_A     48                 /* barra de comandos */
#define END_Y     CMD_A
#define END_A     44                 /* barra de endereço */
#define CORPO_Y   (CMD_A + END_A)
#define LATERAL   200
#define CAB_A     30                 /* cabeçalho das colunas */
#define STATUS_A  28
#define ITEM_A    28
#define C_BARRA   0xF3F3F3
#define C_LATERAL 0xF9F9F9
#define C_LINHA   0xE3E3E3
#define C_TEXTO   0x1A1A1A
#define C_FRACO   0x616161
#define C_APAGADO 0xA8A8A8
#define C_SOB     0xE6E6E6
#define C_ATUAL   0xE0E0E0
#define C_MARCA   0x0B7A3E           /* o verde do SaguiOS no lugar do azul */

static jan_t *j;
static char pasta[160] = "/";
static char nomes[MAX_ITENS][64];
static uint8_t tipos[MAX_ITENS];
static uint32_t tamanhos[MAX_ITENS];
static lista_t lista;
/* Renomear devagar: o item clicado e quando; a edição abre se o segundo
 * clique de um duplo clique não vier nesse tempo (o do kernel é 450 ms). */
#define DEVAGAR_MS 500
static int devagar_i = -1;
static unsigned devagar_ms;
static uint8_t marc[MAX_ITENS];   /* D058: itens marcados (laço, Ctrl, Shift) */
static info_disco_t pendrive;
static bool tem_pendrive;

/* Ícones 16x16 em pixel art. */
static const char *const ic_pasta[] = {
    "................", "................", ".PPPPP..........", "PYYYYYP.........",
    "PYYYYYYPPPPPPPP.", "PYYYYYYYYYYYYYYP", "PPPPPPPPPPPPPPPP", "PAAAAAAAAAAAAAAP",
    "PAAAAAAAAAAAAAAP", "PAAAAAAAAAAAAAAP", "PAAAAAAAAAAAAAAP", "PAAAAAAAAAAAAAAP",
    "PAAAAAAAAAAAAAAP", "PPPPPPPPPPPPPPPP", "................", "................",
};
static const char *const ic_arquivo[] = {
    "..PPPPPPPPP.....", "..PBBBBBBBPP....", "..PBBBBBBBPBP...", "..PBBBBBBBPPPP..",
    "..PBCCCCCBBBBP..", "..PBBBBBBBBBBP..", "..PBCCCCCCCCBP..", "..PBBBBBBBBBBP..",
    "..PBCCCCCCCBBP..", "..PBBBBBBBBBBP..", "..PBCCCCCCCCBP..", "..PBBBBBBBBBBP..",
    "..PBCCCCCBBBBP..", "..PBBBBBBBBBBP..", "..PPPPPPPPPPPP..", "................",
};
static const char *const ic_programa[] = {
    "................", ".PPPPPPPPPPPPPP.", ".PVVVVVVVVVVVVP.", ".PPPPPPPPPPPPPP.",
    ".PDDDDDDDDDDDDP.", ".PDGDDDDDDDDDDP.", ".PDDGDDDDDDDDDP.", ".PDDDGDDDDDDDDP.",
    ".PDDGDDDDDDDDDP.", ".PDGDDGGGGDDDDP.", ".PDDDDDDDDDDDDP.", ".PDDDDDDDDDDDDP.",
    ".PPPPPPPPPPPPPP.", "................", "................", "................",
};

/* Ícones por tipo de arquivo (antes, um .wav aparecia com o ícone de texto). */
static const char *const ic_musica[] = {          /* uma nota musical */
    "..PPPPPPPPPPPP..", "..PBBBBBBBBBBP..", "..PBBBBBNNNBBP..", "..PBBBBBNBNNBP..",
    "..PBBBBBNBBNBP..", "..PBBBBBNBBBBP..", "..PBBBBBNBBBBP..", "..PBBBBBNBBBBP..",
    "..PBBBNNNBBBBP..", "..PBBNNNNBBBBP..", "..PBBNNNNBBBBP..", "..PBBBNNBBBBBP..",
    "..PBBBBBBBBBBP..", "..PBBBBBBBBBBP..", "..PPPPPPPPPPPP..", "................",
};
static const char *const ic_imagem[] = {          /* céu, sol e morro */
    "................", "PPPPPPPPPPPPPPPP", "PSSSSSSSSSSSSSSP", "PSSSSSSSSSOOSSSP",
    "PSSSSSSSSSOOSSSP", "PSSSSSSSSSSSSSSP", "PSSSSSGSSSSSSSSP", "PSSSSGGGSSSSSSSP",
    "PSSSGGGGGSSSGSSP", "PSSGGGGGGGSGGGSP", "PSGGGGGGGGGGGGGP", "PGGGGGGGGGGGGGGP",
    "PPPPPPPPPPPPPPPP", "................", "................", "................",
};
static const char *const ic_pagina[] = {          /* uma página com a faixa azul de navegador */
    "................", "PPPPPPPPPPPPPPPP", "PVVVVVVVVVVVVVVP", "PVBVBVBVVVVVVVVP",
    "PPPPPPPPPPPPPPPP", "PBBBBBBBBBBBBBBP", "PBNNNNNNNBBBBBBP", "PBBBBBBBBBBBBBBP",
    "PBCCCCCCCCCCCCBP", "PBCCCCCCCCCCBBBP", "PBBBBBBBBBBBBBBP", "PBVVVVVBBBBBBBBP",
    "PBBBBBBBBBBBBBBP", "PPPPPPPPPPPPPPPP", "................", "................",
};
static const char *const ic_video[] = {           /* um rolo de filme com o botão de tocar */
    "................", "PPPPPPPPPPPPPPPP", "PBPBPBPBPBPBPBPP", "PPPPPPPPPPPPPPPP",
    "PDDDDDDDDDDDDDDP", "PDDDDDBDDDDDDDDP", "PDDDDDBBDDDDDDDP", "PDDDDDBBBDDDDDDP",
    "PDDDDDBBBBDDDDDP", "PDDDDDBBBDDDDDDP", "PDDDDDBBDDDDDDDP", "PDDDDDBDDDDDDDDP",
    "PPPPPPPPPPPPPPPP", "PBPBPBPBPBPBPBPP", "PPPPPPPPPPPPPPPP", "................",
};

/* Ícones das barras (H136), 16x16. */
static const char *const ic_mais[] = {
    "................", "................", "......MMMM......", "......MMMM......",
    "......MMMM......", "......MMMM......", "..MMMMMMMMMMMM..", "..MMMMMMMMMMMM..",
    "..MMMMMMMMMMMM..", "..MMMMMMMMMMMM..", "......MMMM......", "......MMMM......",
    "......MMMM......", "......MMMM......", "................", "................",
};
static const char *const ic_lapis[] = {
    "................", "...........WW...", "..........WLLW..", ".........WLLLW..",
    "........WOOLW...", ".......WOOOW....", "......WOOOW.....", ".....WOOOW......",
    "....WOOOW.......", "...WOOOW........", "..WOOOW.........", "..WWOW..........",
    "..WWW...........", "..W.............", "................", "................",
};
static const char *const ic_lixo[] = {
    "................", "......WWWW......", "..WWWWWWWWWWWW..", "................",
    "...WKKKKKKKKW...", "...WKWKWKWKKW...", "...WKWKWKWKKW...", "...WKWKWKWKKW...",
    "...WKWKWKWKKW...", "...WKWKWKWKKW...", "...WKWKWKWKKW...", "...WKKKKKKKKW...",
    "....WWWWWWWW....", "................", "................", "................",
};
static const char *const ic_ordenar[] = {
    "................", "...W............", "..WWW...........", ".WWWWW..........",
    "...W............", "...W.....WWWWWW.", "...W............", "...W.....WWWW...",
    "...W............", "...W.....WW.....", "...W............", "...W............",
    "................", "................", "................", "................",
};
static const char *const ic_restaurar[] = {
    "................", "....W...........", "...WW...........", "..WWWWWWWWW.....",
    "...WW......W....", "....W.......W...", "............W...", "............W...",
    "............W...", "...........W....", "....WWWWWWWW....", "................",
    "................", "................", "................", "................",
};
static const char *const ic_esvaziar[] = {
    "................", "......WWWW......", "..WWWWWWWWWWWW..", "................",
    "...WKKKKKKKKW...", "...WKRKKKKRKW...", "...WKKRKKRKKW...", "...WKKKRRKKKW...",
    "...WKKKRRKKKW...", "...WKKRKKRKKW...", "...WKRKKKKRKW...", "...WKKKKKKKKW...",
    "....WWWWWWWW....", "................", "................", "................",
};
static const char *const ic_esq[] = {
    "................", "................", "................", "......W.........",
    ".....WW.........", "....WW..........", "...WWWWWWWWWWW..", "..WWWWWWWWWWWW..",
    "...WWWWWWWWWWW..", "....WW..........", ".....WW.........", "......W.........",
    "................", "................", "................", "................",
};
static const char *const ic_dir[] = {
    "................", "................", "................", ".........W......",
    ".........WW.....", "..........WW....", "..WWWWWWWWWWW...", "..WWWWWWWWWWWW..",
    "..WWWWWWWWWWW...", "..........WW....", ".........WW.....", ".........W......",
    "................", "................", "................", "................",
};
static const char *const ic_cima[] = {
    "................", ".......W........", "......WWW.......", ".....WWWWW......",
    "....WW.W.WW.....", "...WW..W..WW....", ".......W........", ".......W........",
    ".......W........", ".......W........", ".......W........", ".......W........",
    ".......W........", "................", "................", "................",
};
static const char *const ic_atualizar[] = {
    "................", "......WWWW..W...", "....WW....WWW...", "...W......WWW...",
    "..W.............", "..W.............", "..W.............", "..W.............",
    "...W.......W....", "....W.....W.....", ".....WWWWW......", "................",
    "................", "................", "................", "................",
};
static const char *const ic_lupa[] = {
    "................", "................", "....KKKK........", "...K....K.......",
    "..K......K......", "..K......K......", "..K......K......", "..K......K......",
    "...K....K.......", "....KKKKKK......", ".........KK.....", "..........KK....",
    "...........KK...", "................", "................", "................",
};
static const char *const ic_pc[] = {
    "................", ".PPPPPPPPPPPPPP.", ".PSSSSSSSSSSSSP.", ".PSSSSSSSSSSSSP.",
    ".PSSSSSSSSSSSSP.", ".PSSSSSSSSSSSSP.", ".PSSSSSSSSSSSSP.", ".PSSSSSSSSSSSSP.",
    ".PPPPPPPPPPPPPP.", "......PPPP......", "......PPPP......", "....PPPPPPPP....",
    "................", "................", "................", "................",
};
static const char *const ic_pendrive[] = {
    "................", ".....PPPPPP.....", ".....PLLLLP.....", ".....PLPPLP.....",
    "....PPPPPPPP....", "....PVVVVVVP....", "....PVVVVVVP....", "....PVVVVVVP....",
    "....PVVVVVVP....", "....PVVVVVVP....", "....PVVVVVVP....", "....PVVVVVVP....",
    ".....PVVVVP.....", "......PPPP......", "................", "................",
};
static const char *const ic_lixeira[] = {
    "................", "......PPPP......", "..PPPPPPPPPPPP..", "...PLLLLLLLLP...",
    "...PLKLKLKLLP...", "...PLKLKLKLLP...", "...PLKLKLKLLP...", "...PLKLKLKLLP...",
    "...PLKLKLKLLP...", "...PLKLKLKLLP...", "...PLLLLLLLLP...", "....PPPPPPPP....",
    "................", "................", "................", "................",
};
static const char *const ic_ejetar[] = {
    "................", "................", ".......W........", "......WWW.......",
    ".....WWWWW......", "....WWWWWWW.....", "...WWWWWWWWW....", "................",
    "...WWWWWWWWW....", "...WWWWWWWWW....", "................", "................",
    "................", "................", "................", "................",
};

static bool extensao(const char *s, const char *fim)
{
    unsigned a = tam_texto(s), b = tam_texto(fim);
    if (a < b) return false;
    for (unsigned i = 0; i < b; i++) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != fim[i]) return false;
    }
    return true;
}

static const char *const *icone_do_arquivo(const char *nome)
{
    if (extensao(nome, ".wav") || extensao(nome, ".mp3")) return ic_musica;
    if (extensao(nome, ".png") || extensao(nome, ".jpg") || extensao(nome, ".jpeg") || extensao(nome, ".gif") ||
        extensao(nome, ".webp") || extensao(nome, ".svg") || extensao(nome, ".bmp")) return ic_imagem;
    if (extensao(nome, ".html") || extensao(nome, ".htm")) return ic_pagina;
    if (extensao(nome, ".mp4")) return ic_video;
    return ic_arquivo;
}

static uint32_t paleta(char c)
{
    switch (c) {
    case 'P': return 0x5A5F66;   /* contorno mais leve (H136) */
    case 'Y': return 0xFFE070;
    case 'A': return 0xF2C230;
    case 'B': return 0xFFFFFF;
    case 'C': return 0x9AA3AC;
    case 'V': return 0x2A6FD8;
    case 'D': return 0x182030;
    case 'G': return 0x3CDC6E;
    case 'N': return 0x0B7A3E;   /* nota e texto em verde */
    case 'S': return 0x8FD0F0;   /* céu */
    case 'O': return 0xFFD23A;   /* sol */
    case 'M': return 0x0B7A3E;   /* o "+" do Novo, no verde do SaguiOS */
    case 'W': return 0x3A3A3A;   /* traço dos ícones das barras */
    case 'L': return 0xD9D9D9;
    case 'K': return 0x8A8A8A;
    case 'R': return 0xC42B1C;
    default:  return 0xFF000000;
    }
}

static void icone(jan_t *jj, int x, int y, int i)
{
    const char *const *d = tipos[i] == TIPO_PASTA ? ic_pasta : tipos[i] == TIPO_PROGRAMA ? ic_programa : icone_do_arquivo(nomes[i]);
    g_desenho(jj, x, y, d, 16, 16, paleta, 1);
}

static void caminho_de(int i, char *saida, size_t max)
{
    copia_texto(saida, pasta, max);
    if (compara_texto(pasta, "/") != 0) junta_texto(saida, "/", max);
    junta_texto(saida, nomes[i], max);
}

/* ------------------------------------------------------------ as barras (H136) */

static data_t datas[MAX_ITENS];      /* modificação de cada item (ano 0: não se sabe) */
static char busca[48];               /* o que está na caixa de pesquisa */
static bool busca_foco;
static int ordem_col;                /* 0 nome, 1 data, 2 tipo, 3 tamanho */
static bool ordem_desc;
static char hist[24][160];           /* voltar e avançar */
static int hist_n, hist_pos = -1;

static uint32_t paleta_apagada(char c) { return paleta(c) == 0xFF000000 ? 0xFF000000 : C_APAGADO; }

/* "Data de modificação": dd/mm/aaaa hh:mm, ou nada. */
static void texto_data(const data_t *d, char *s, int max)
{
    if (!d->ano) { s[0] = 0; return; }
    formatar_texto(s, (size_t)max, "%02d/%02d/%04d %02d:%02d", d->dia, d->mes, d->ano, d->hora, d->minuto);
}

static void texto_tipo(int i, char *s, int max)
{
    if (tipos[i] == TIPO_PASTA) { copia_texto(s, "Pasta de arquivos", (size_t)max); return; }
    if (tipos[i] == TIPO_PROGRAMA) { copia_texto(s, "Programa", (size_t)max); return; }
    static const struct { const char *ext, *nome; } t[] = {
        { ".txt", "Documento de texto" }, { ".md", "Texto Markdown" }, { ".html", "Página HTML" }, { ".htm", "Página HTML" },
        { ".wav", "Som WAV" }, { ".mp3", "Som MP3" }, { ".mp4", "Vídeo MP4" }, { ".c", "Código C" }, { ".h", "Cabeçalho C" },
        { ".zip", "Pasta compactada" }, { ".tar.gz", "Pacote tar.gz" }, { ".tgz", "Pacote tar.gz" }, { ".tar", "Pacote tar" },
        { ".gz", "Arquivo gzip" }, { ".json", "Arquivo JSON" }, { ".css", "Folha de estilo" },
    };
    for (unsigned k = 0; k < sizeof t / sizeof t[0]; k++) if (extensao(nomes[i], t[k].ext)) { copia_texto(s, t[k].nome, (size_t)max); return; }
    const char *p = acha_ultimo_char(nomes[i], '.');
    if (p && p != nomes[i] && p[1] && tam_texto(p) <= 6) {   /* "Arquivo PNG", "Imagem JPG"... */
        char e[8];
        int k = 0;
        for (const char *q = p + 1; *q && k < 6; q++) e[k++] = (char)(*q >= 'a' && *q <= 'z' ? *q - 32 : *q);
        e[k] = 0;
        bool imagem = icone_do_arquivo(nomes[i]) == ic_imagem;
        formatar_texto(s, (size_t)max, "%s %s", imagem ? "Imagem" : "Arquivo", e);
        return;
    }
    copia_texto(s, "Arquivo", (size_t)max);
}

/* Em KB, arredondado para cima ("1 KB" para 1 byte). */
static void texto_tamanho(uint32_t b, char *s, int max)
{
    if (b >= 10u * 1024 * 1024) formatar_texto(s, (size_t)max, "%u MB", (b + 1024 * 1024 - 1) / (1024 * 1024));
    else formatar_texto(s, (size_t)max, "%u KB", (b + 1023) / 1024);
}

/* As colunas: da direita para a esquerda, Tamanho, Tipo, Data; numa janela
 * estreita, a Data e depois o Tipo somem. x0 de cada uma (0 = não cabe). */
static int col_x[4];
static void calcular_colunas(void)
{
    int fim = lista.x + lista.l - 10;
    col_x[3] = fim - 90;
    col_x[2] = lista.l >= 420 ? col_x[3] - 170 : 0;
    col_x[1] = lista.l >= 600 ? (col_x[2] ? col_x[2] : col_x[3]) - 176 : 0;
    col_x[0] = lista.x + 12;
    int primeira = col_x[1] ? col_x[1] : col_x[2] ? col_x[2] : col_x[3];
    lista.larg_nome = primeira - 8 - lista.x;
}

static void colunas(jan_t *jj, int y, int i)
{
    char s[40];
    int ty = y + (ITEM_A - LETRA_A) / 2;
    if (col_x[1]) { texto_data(&datas[i], s, sizeof s); g_texto(jj, col_x[1] + 8, ty, s, C_FRACO); }
    if (col_x[2]) { texto_tipo(i, s, sizeof s); g_texto_n(jj, col_x[2] + 8, ty, s, 160 / LETRA_L - 1, C_FRACO); }
    if (tipos[i] != TIPO_PASTA) {
        texto_tamanho(tamanhos[i], s, sizeof s);
        g_texto(jj, lista.x + lista.l - 16 - g_largura_texto(s), ty, s, C_FRACO);
    }
}

static void cabecalho(void)
{
    int x = lista.x, y = CORPO_Y;
    g_retangulo(j, x, y, lista.l, CAB_A, COR_BRANCO);
    g_retangulo(j, x + 6, y + CAB_A - 1, lista.l - 12, 1, C_LINHA);
    static const char *const nome[4] = { "Nome", "Data de modificação", "Tipo", "Tamanho" };
    for (int k = 0; k < 4; k++) {
        if (!col_x[k]) continue;
        int tx = col_x[k] + (k ? 8 : 22);
        if (k == 3) tx = lista.x + lista.l - 16 - g_largura_texto(nome[3]);
        g_texto(j, tx, y + 7, nome[k], ordem_col == k ? C_TEXTO : C_FRACO);
        if (k) g_retangulo(j, col_x[k], y + 7, 1, 16, C_LINHA);
        if (ordem_col == k) {   /* a setinha da ordem */
            int ax = k == 3 ? tx - 14 : tx + g_largura_texto(nome[k]) + 8, ay = y + 13;
            for (int d = 0; d < 4; d++) {
                int yy = ordem_desc ? ay + d : ay + 3 - d;
                g_retangulo(j, ax + d, yy, 7 - 2 * d, 1, C_FRACO);
            }
        }
    }
    jan_mostrar_area(j, x, y, lista.l, CAB_A);
}

/* ---- barra de comandos */
enum { CMD_NOVO = 1, CMD_RENOMEAR, CMD_APAGAR, CMD_ORDENAR, CMD_RESTAURAR, CMD_APAGAR_VEZ, CMD_ESVAZIAR };
typedef struct { const char *texto; const char *const *icone; int acao; bool menu, ativo; int x, l; } cmd_t;
static cmd_t cmds[10];
static int qtd_cmds, cmd_sob = -1;

static bool na_lixeira(void);

static void cmd(const char *t, const char *const *ic, int a, bool menu, bool ativo)
{
    cmds[qtd_cmds++] = (cmd_t){ t, ic, a, menu, ativo, 0, 0 };
}

static void montar_cmds(void)
{
    int marcados = lista_qtd_marcados(&lista);
    qtd_cmds = 0;
    if (na_lixeira()) {
        cmd("Restaurar", ic_restaurar, CMD_RESTAURAR, false, marcados > 0);
        cmd("Apagar de vez", ic_lixo, CMD_APAGAR_VEZ, false, marcados > 0);
        cmd(NULL, NULL, 0, false, false);
        cmd("Esvaziar lixeira", ic_esvaziar, CMD_ESVAZIAR, false, lista.qtd > 0);
    } else {
        cmd("Novo", ic_mais, CMD_NOVO, true, true);
        cmd(NULL, NULL, 0, false, false);
        cmd("Renomear", ic_lapis, CMD_RENOMEAR, false, marcados == 1);
        cmd("Apagar", ic_lixo, CMD_APAGAR, false, marcados > 0);
    }
    cmd(NULL, NULL, 0, false, false);
    cmd("Ordenar", ic_ordenar, CMD_ORDENAR, true, lista.qtd > 1);
    int x = 10;
    for (int i = 0; i < qtd_cmds; i++) {
        cmds[i].x = x;
        cmds[i].l = cmds[i].texto ? 12 + 16 + 8 + g_largura_texto(cmds[i].texto) + (cmds[i].menu ? 18 : 12) : 17;
        x += cmds[i].l + 2;
    }
}

static void desenhar_cmds(void)
{
    montar_cmds();
    g_retangulo(j, 0, 0, LARG, CMD_A, C_BARRA);
    for (int i = 0; i < qtd_cmds; i++) {
        cmd_t *c = &cmds[i];
        if (!c->texto) { g_retangulo(j, c->x + 8, 12, 1, CMD_A - 24, 0xD0D0D0); continue; }
        if (cmd_sob == i && c->ativo) g_ret_redondo(j, c->x, 6, c->l, CMD_A - 12, 5, C_SOB, C_SOB);
        g_desenho(j, c->x + 12, 16, c->icone, 16, 16, c->ativo ? paleta : paleta_apagada, 1);
        g_texto(j, c->x + 36, 16, c->texto, c->ativo ? C_TEXTO : C_APAGADO);
        if (c->menu) {   /* a setinha do menu */
            int ax = c->x + c->l - 16, ay = 22;
            for (int d = 0; d < 3; d++) g_retangulo(j, ax + d, ay + d, 5 - 2 * d, 1, c->ativo ? C_FRACO : C_APAGADO);
        }
    }
    jan_mostrar_area(j, 0, 0, LARG, CMD_A);
}

static int cmd_em(int x, int y)
{
    if (y < 6 || y >= CMD_A - 6) return -1;
    for (int i = 0; i < qtd_cmds; i++) if (cmds[i].texto && x >= cmds[i].x && x < cmds[i].x + cmds[i].l) return i;
    return -1;
}

/* ---- barra de endereço: voltar, avançar, subir, atualizar, o caminho e a pesquisa */
enum { NAV_VOLTAR, NAV_AVANCAR, NAV_SUBIR, NAV_ATUALIZAR };
static int nav_sob = -1;
static struct { char texto[40]; char caminho[160]; int x, l; } segs[12];
static int qtd_segs, seg_sob = -1;

static bool nav_ativo(int k)
{
    if (k == NAV_VOLTAR) return hist_pos > 0;
    if (k == NAV_AVANCAR) return hist_pos < hist_n - 1;
    if (k == NAV_SUBIR) return compara_texto(pasta, "/") != 0;
    return true;
}

static int caminho_x0(void) { return 8 + 4 * 36 + 6; }
static int caminho_x1(void) { return LARG - 264; }
static int busca_x(void) { return LARG - 254; }

/* O nome de cada pedaço do caminho: a raiz é "SaguiOS", /usb é o pendrive
 * e as lixeiras são "Lixeira". */
static void montar_segs(void)
{
    qtd_segs = 0;
    char acc[160] = "";
    const char *p = pasta;
    copia_texto(segs[0].texto, "SaguiOS", sizeof segs[0].texto);
    copia_texto(segs[0].caminho, "/", sizeof segs[0].caminho);
    qtd_segs = 1;
    while (*p && qtd_segs < 12) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *f = p;
        while (*f && *f != '/') f++;
        int n = (int)(f - p);
        int k = (int)tam_texto(acc);
        if (k + n + 2 >= (int)sizeof acc) break;
        acc[k] = '/';
        memcpy(acc + k + 1, p, (size_t)n);
        acc[k + 1 + n] = 0;
        char nome[40];
        copia_texto(nome, p, (size_t)(n + 1 < 40 ? n + 1 : 40));
        if (compara_texto(acc, "/usb") == 0) {
            copia_texto(nome, tem_pendrive && !pendrive.interno && pendrive.rotulo[0] ? pendrive.rotulo : tem_pendrive && pendrive.interno ? "Disco" : "Pendrive", sizeof nome);
            qtd_segs = 0;   /* o pendrive é uma raiz própria */
        }
        if (compara_texto(nome, ".lixeira") == 0) copia_texto(nome, "Lixeira", sizeof nome);
        if (compara_texto(acc, "/.lixeira") == 0) qtd_segs = 0;
        copia_texto(segs[qtd_segs].texto, nome, sizeof segs[qtd_segs].texto);
        copia_texto(segs[qtd_segs].caminho, acc, sizeof segs[qtd_segs].caminho);
        qtd_segs++;
        p = f;
    }
}

static void seta_caminho(int x, int y)   /* o ">" entre os pedaços */
{
    for (int d = 0; d < 4; d++) { g_retangulo(j, x + d, y + d, 1, 1, C_FRACO); g_retangulo(j, x + d, y + 6 - d, 1, 1, C_FRACO); }
}

static void desenhar_endereco(void)
{
    int y = END_Y;
    g_retangulo(j, 0, y, LARG, END_A, C_BARRA);
    static const char *const *const ic[4] = { ic_esq, ic_dir, ic_cima, ic_atualizar };
    for (int k = 0; k < 4; k++) {
        int x = 8 + k * 36;
        bool at = nav_ativo(k);
        if (nav_sob == k && at) g_ret_redondo(j, x, y + 6, 32, 32, 6, C_SOB, C_SOB);
        g_desenho(j, x + 8, y + 14, ic[k], 16, 16, at ? paleta : paleta_apagada, 1);
    }
    /* o caminho */
    int x0 = caminho_x0(), x1 = caminho_x1();
    g_ret_redondo(j, x0, y + 6, x1 - x0, 32, 5, COR_BRANCO, COR_BRANCO);
    g_contorno_redondo(j, x0, y + 6, x1 - x0, 32, 5, 0xD6D6D6);
    g_desenho(j, x0 + 10, y + 14, compara_texto(pasta, "/usb") == 0 || comeca_com(pasta, "/usb/") ? ic_pendrive : na_lixeira() ? ic_lixeira : ic_pc, 16, 16, paleta, 1);
    montar_segs();
    /* cabe? senão, os primeiros pedaços somem */
    int ini = 0, larg = 0;
    for (int k = 0; k < qtd_segs; k++) larg += g_largura_texto(segs[k].texto) + 22;
    while (ini < qtd_segs - 1 && larg > x1 - x0 - 50) { larg -= g_largura_texto(segs[ini].texto) + 22; ini++; }
    int x = x0 + 34;
    if (ini) { g_texto(j, x, y + 14, "..", C_FRACO); x += 20; }
    for (int k = 0; k < qtd_segs; k++) {
        if (k < ini) { segs[k].l = 0; continue; }
        int l = g_largura_texto(segs[k].texto);
        segs[k].x = x - 4;
        segs[k].l = l + 8;
        if (seg_sob == k) g_ret_redondo(j, x - 4, y + 10, l + 8, 24, 4, C_SOB, C_SOB);
        g_texto_n(j, x, y + 14, segs[k].texto, (x1 - x - 8) / LETRA_L, C_TEXTO);
        x += l + 8;
        if (k < qtd_segs - 1) { seta_caminho(x + 2, y + 18); x += 14; }
        if (x > x1 - 20) break;
    }
    /* a pesquisa */
    int bx = busca_x(), bl = LARG - 8 - bx;
    g_ret_redondo(j, bx, y + 6, bl, 32, 5, COR_BRANCO, COR_BRANCO);
    g_contorno_redondo(j, bx, y + 6, bl, 32, 5, busca_foco ? 0xB8B8B8 : 0xD6D6D6);
    if (busca_foco) g_retangulo(j, bx + 4, y + 36, bl - 8, 2, C_MARCA);
    if (busca[0]) {
        int cabe = (bl - 44) / LETRA_L, n = (int)tam_texto(busca);
        const char *t = n > cabe ? busca + n - cabe : busca;
        g_texto(j, bx + 10, y + 14, t, C_TEXTO);
        if (busca_foco) g_retangulo(j, bx + 10 + (int)tam_texto(t) * LETRA_L, y + 14, 2, LETRA_A, C_TEXTO);
    } else {
        char t[64];
        formatar_texto(t, sizeof t, "Pesquisar em %s", qtd_segs ? segs[qtd_segs - 1].texto : "SaguiOS");
        g_texto_n(j, bx + 10, y + 14, t, (bl - 44) / LETRA_L, C_FRACO);
        if (busca_foco) g_retangulo(j, bx + 10, y + 14, 2, LETRA_A, C_TEXTO);
    }
    g_desenho(j, bx + bl - 28, y + 14, ic_lupa, 16, 16, paleta, 1);
    jan_mostrar_area(j, 0, y, LARG, END_A);
}

static int nav_em(int x, int y)
{
    if (y < END_Y + 6 || y >= END_Y + 38) return -1;
    for (int k = 0; k < 4; k++) if (x >= 8 + k * 36 && x < 8 + k * 36 + 32) return k;
    return -1;
}

static int seg_em(int x, int y)
{
    if (y < END_Y + 6 || y >= END_Y + 38) return -1;
    for (int k = 0; k < qtd_segs; k++) if (segs[k].l && x >= segs[k].x && x < segs[k].x + segs[k].l) return k;
    return -1;
}

/* ---- painel de navegação: atalhos e a árvore de pastas (H136)
 *
 * Em cima, atalhos (Temporários,
 * Programas, Lixeira), sempre à vista; depois a árvore do SaguiOS e a do
 * pendrive, em que cada pasta com subpastas tem uma setinha que abre e
 * fecha. Ir
 * para uma pasta abre a árvore até ela, e a pasta atual fica marcada. Com
 * muitas pastas abertas, o painel rola com a roda. */
#define LUGARES   64
#define LUGAR_A   28
typedef struct { char texto[40]; const char *const *icone; char caminho[160]; int y, nivel, seta; bool ejetar, atalho; } lugar_t;
static lugar_t lugares[LUGARES];
static int qtd_lugares, lugar_sob = -1, arv_topo;
static bool ejetar_sob;
static char abertos[24][160];   /* pastas abertas na árvore */
static int qtd_abertos;
static char painel_antes[400];  /* o último registro do painel (para os testes) */
static bool mostrar_atual;      /* depois de navegar: rola a árvore até a pasta atual */

static bool aberta(const char *c)
{
    for (int k = 0; k < qtd_abertos; k++) if (compara_texto(abertos[k], c) == 0) return true;
    return false;
}

static void abrir_no(const char *c)
{
    if (aberta(c)) return;
    if (qtd_abertos == 24) { for (int k = 1; k < 24; k++) copia_texto(abertos[k - 1], abertos[k], sizeof abertos[0]); qtd_abertos--; }
    copia_texto(abertos[qtd_abertos++], c, sizeof abertos[0]);
}

static void fechar_no(const char *c)
{
    for (int k = 0; k < qtd_abertos; k++)
        if (compara_texto(abertos[k], c) == 0) {
            for (int q = k + 1; q < qtd_abertos; q++) copia_texto(abertos[q - 1], abertos[q], sizeof abertos[0]);
            qtd_abertos--;
            return;
        }
}

/* Abre a árvore até a pasta (os pais dela; a própria pasta fica como está). */
static void abrir_ate(const char *c)
{
    if (comeca_com(c, "/usb")) abrir_no("/usb");
    else abrir_no("/");
    char acc[160];
    copia_texto(acc, c, sizeof acc);
    for (char *p = acc + 1; *p; p++)
        if (*p == '/') { *p = 0; if (compara_texto(acc, "/usb") != 0) abrir_no(acc); *p = '/'; }
}

static void filho_de(const char *pai, const char *nome, char *saida, int max)
{
    copia_texto(saida, pai, (size_t)max);
    if (compara_texto(pai, "/") != 0) junta_texto(saida, "/", (size_t)max);
    junta_texto(saida, nome, (size_t)max);
}

/* Tem subpastas que aparecem na árvore? (a /usb não conta na raiz: tem a sua) */
static bool tem_subpastas(const char *c)
{
    entrada_pasta_t e;
    for (int i = 0; listar(c, i, &e) == 1; i++) {
        if (e.tipo != TIPO_PASTA || e.nome[0] == '.') continue;
        if (compara_texto(c, "/") == 0 && compara_texto(e.nome, "usb") == 0) continue;
        return true;
    }
    return false;
}

static void lugar(const char *texto, const char *const *ic, const char *c, int nivel, int seta, bool ejetar, bool atalho)
{
    if (qtd_lugares >= LUGARES) return;
    lugar_t *l = &lugares[qtd_lugares++];
    copia_texto(l->texto, texto, sizeof l->texto);
    l->icone = ic;
    copia_texto(l->caminho, c, sizeof l->caminho);
    l->nivel = nivel;
    l->seta = seta;
    l->ejetar = ejetar;
    l->atalho = atalho;
}

static void ramos(const char *c, int nivel)
{
    entrada_pasta_t e;
    for (int i = 0; qtd_lugares < LUGARES && listar(c, i, &e) == 1; i++) {
        if (e.tipo != TIPO_PASTA || e.nome[0] == '.') continue;
        if (compara_texto(c, "/") == 0 && compara_texto(e.nome, "usb") == 0) continue;
        char f[160];
        filho_de(c, e.nome, f, sizeof f);
        bool sub = nivel < 8 && tem_subpastas(f);
        bool ab = sub && aberta(f);
        lugar(e.nome, ic_pasta, f, nivel, sub ? (ab ? 2 : 1) : 0, false, false);
        if (ab) ramos(f, nivel + 1);
    }
}

static void montar_lugares(void)
{
    qtd_lugares = 0;
    lugar("Temporários", ic_pasta, "/tmp", 0, 0, false, true);
    lugar("Programas", ic_pasta, "/bin", 0, 0, false, true);
    lugar("Lixeira", ic_lixeira, "/.lixeira", 0, 0, false, true);
    int raiz = qtd_lugares;
    bool ab = aberta("/");
    lugar("SaguiOS", ic_pc, "/", 0, ab ? 2 : 1, false, false);
    if (ab) ramos("/", 1);
    if (tem_pendrive) {
        const char *r = pendrive.interno ? "Disco" : pendrive.rotulo[0] ? pendrive.rotulo : "Pendrive";
        bool pab = pendrive.montado && aberta("/usb") && tem_subpastas("/usb");
        lugar(r, ic_pendrive, "/usb", 0, pendrive.montado && tem_subpastas("/usb") ? (pab ? 2 : 1) : 0, pendrive.montado && !pendrive.interno, false);
        if (pab) ramos("/usb", 1);
    }
    /* posições: os atalhos ficam parados; só a árvore rola */
    int y = CORPO_Y + 10;
    for (int k = 0; k < qtd_lugares; k++) {
        if (k == raiz) y += 14 - arv_topo * LUGAR_A;
        lugares[k].y = y;
        y += LUGAR_A + (k < raiz ? 4 : 0);
    }
}

#define ATALHOS 3
static int lugar_atual(void)
{
    if (na_lixeira()) return 2;
    for (int k = ATALHOS; k < qtd_lugares; k++) if (compara_texto(lugares[k].caminho, pasta) == 0) return k;   /* na árvore */
    for (int k = 0; k < ATALHOS; k++) if (compara_texto(lugares[k].caminho, pasta) == 0) return k;          /* um atalho */
    return -1;
}

/* A árvore rola por baixo dos atalhos: só aparece da divisória para baixo. */
static int topo_arvore(void) { return CORPO_Y + 10 + ATALHOS * (LUGAR_A + 4) + 6; }
static bool lugar_visivel(const lugar_t *l)
{
    int de_cima = l->atalho ? CORPO_Y + 2 : topo_arvore();
    return l->y >= de_cima && l->y + LUGAR_A <= ALT - STATUS_A - 2;
}

static void desenhar_seta(int x, int y, int seta)   /* > fechada, v aberta */
{
    for (int d = 0; d < 4; d++) {
        if (seta == 1) { g_retangulo(j, x + 2 + d, y + 4 + d, 1, 1, C_FRACO); g_retangulo(j, x + 2 + d, y + 10 - d, 1, 1, C_FRACO); }
        else { g_retangulo(j, x + 1 + d, y + 6 + d, 1, 1, C_FRACO); g_retangulo(j, x + 7 - d, y + 6 + d, 1, 1, C_FRACO); }
    }
}

static int x_do_nivel(const lugar_t *l) { return 8 + l->nivel * 14; }

static void desenhar_lateral(void)
{
    montar_lugares();
    if (mostrar_atual) {
        mostrar_atual = false;
        int k = lugar_atual();
        if (k >= ATALHOS) {
            int fundo = ALT - STATUS_A - 2;
            if (lugares[k].y + LUGAR_A > fundo) arv_topo += (lugares[k].y + LUGAR_A - fundo + LUGAR_A - 1) / LUGAR_A;
            else if (lugares[k].y < topo_arvore()) arv_topo -= (topo_arvore() - lugares[k].y + LUGAR_A - 1) / LUGAR_A;
            if (arv_topo < 0) arv_topo = 0;
            montar_lugares();
        }
    }
    int y0 = CORPO_Y, a = ALT - CORPO_Y - STATUS_A;
    g_retangulo(j, 0, y0, LATERAL, a, C_LATERAL);
    g_retangulo(j, LATERAL, y0, 1, a, C_LINHA);
    int atual = lugar_atual();
    char reg[400] = "";
    for (int k = 0; k < qtd_lugares; k++) {
        lugar_t *l = &lugares[k];
        if (!lugar_visivel(l)) continue;
        if (k == ATALHOS && l->y - 8 >= topo_arvore() - 6) g_retangulo(j, 16, l->y - 8, LATERAL - 32, 1, C_LINHA);
        if (k == atual) {
            g_ret_redondo(j, 4, l->y, LATERAL - 8, LUGAR_A - 2, 5, C_ATUAL, C_ATUAL);
            g_ret_redondo(j, 4, l->y + 7, 3, 12, 1, C_MARCA, C_MARCA);
        } else if (k == lugar_sob) g_ret_redondo(j, 4, l->y, LATERAL - 8, LUGAR_A - 2, 5, C_SOB, C_SOB);
        int x = x_do_nivel(l);
        if (l->seta) desenhar_seta(x, l->y + 6, l->seta);
        x += l->atalho ? 6 : 16;
        g_desenho(j, x, l->y + 5, l->icone, 16, 16, paleta, 1);
        int fim = LATERAL - (l->ejetar ? 40 : 10);
        g_texto_n(j, x + 22, l->y + 5, l->texto, (fim - x - 22) / LETRA_L, C_TEXTO);
        if (l->ejetar) {
            if (k == lugar_sob && ejetar_sob) g_ret_redondo(j, LATERAL - 36, l->y + 1, 24, 24, 4, 0xD4D4D4, 0xD4D4D4);
            g_desenho(j, LATERAL - 32, l->y + 5, ic_ejetar, 16, 16, paleta, 1);
        }
        char t[64];
        formatar_texto(t, sizeof t, "%s%s@%d", reg[0] ? ", " : "", l->texto, l->y + LUGAR_A / 2);
        junta_texto(reg, t, sizeof reg);
    }
    jan_mostrar_area(j, 0, y0, LATERAL + 1, a);
    if (compara_texto(reg, painel_antes) != 0) {   /* o que o painel mostra e onde (os testes clicam por aqui) */
        copia_texto(painel_antes, reg, sizeof painel_antes);
        /* em pedaços: o registro do kernel corta linhas compridas */
        int n = (int)tam_texto(reg);
        for (int i = 0; i < n;) {
            int k = n - i > 180 ? 180 : n - i;
            while (i + k < n && reg[i + k] != ',' && k > 1) k--;   /* corta numa vírgula */
            char pedaco[200];
            copia_texto(pedaco, reg + i, (size_t)k + 1);
            registrar("arquivos: painel%s: %s", i ? "+" : "", pedaco + (i && pedaco[0] == ',' ? 2 : 0));
            i += k;
        }
    }
}

/* O item do painel em (x, y); "seta" diz se foi na setinha, "ejetar" no botão do pendrive. */
static int lugar_em_seta(int x, int y, bool *ejetar, bool *seta)
{
    *ejetar = *seta = false;
    if (x < 4 || x >= LATERAL - 4) return -1;
    for (int k = 0; k < qtd_lugares; k++) {
        lugar_t *l = &lugares[k];
        if (!lugar_visivel(l) || y < l->y || y >= l->y + LUGAR_A - 2) continue;
        *ejetar = l->ejetar && x >= LATERAL - 36 && x < LATERAL - 12;
        *seta = l->seta && x >= x_do_nivel(l) - 2 && x < x_do_nivel(l) + 14;
        return k;
    }
    return -1;
}

static int lugar_em(int x, int y, bool *ejetar)
{
    bool seta;
    return lugar_em_seta(x, y, ejetar, &seta);
}

/* A roda sobre o painel rola a árvore. */
static void rolar_painel(int passos)
{
    int cabe = (ALT - STATUS_A - topo_arvore()) / LUGAR_A;
    int max = qtd_lugares - ATALHOS - cabe + 1;
    arv_topo += passos;
    if (arv_topo > max) arv_topo = max;
    if (arv_topo < 0) arv_topo = 0;
    desenhar_lateral();
}

/* ---- barra de status */
static char status_msg[160];

static void desenhar_status(void)
{
    int y = ALT - STATUS_A;
    g_retangulo(j, 0, y, LARG, STATUS_A, COR_BRANCO);
    g_retangulo(j, 0, y, LARG, 1, C_LINHA);
    char t[200];
    int n = formatar_texto(t, sizeof t, "%d %s", lista.qtd, lista.qtd == 1 ? "item" : "itens");
    int marcados = lista_qtd_marcados(&lista);
    if (marcados) {
        uint32_t soma = 0;
        bool so_arquivos = true;
        for (int i = 0; i < lista.qtd; i++) if (marc[i]) { soma += tamanhos[i]; if (tipos[i] == TIPO_PASTA) so_arquivos = false; }
        n += formatar_texto(t + n, sizeof t - (size_t)n, "    %d %s", marcados, marcados == 1 ? "item selecionado" : "itens selecionados");
        if (so_arquivos) { char s[24]; texto_tamanho(soma, s, sizeof s); n += formatar_texto(t + n, sizeof t - (size_t)n, "  %s", s); }
    }
    if (status_msg[0]) formatar_texto(t + n, sizeof t - (size_t)n, "    %s", status_msg);
    g_texto_n(j, 12, y + 6, t, (LARG - 24) / LETRA_L, C_FRACO);
    jan_mostrar_area(j, 0, y, LARG, STATUS_A);
}

static void desenhar_rodape(const char *msg)
{
    copia_texto(status_msg, msg, sizeof status_msg);
    desenhar_status();
}

/* A lista, com o aviso de pasta vazia. */
static void desenhar_lista(void)
{
    lista_desenhar(j, &lista);
    if (lista.qtd == 0) {
        const char *t = busca[0] ? "Nada corresponde à pesquisa." : na_lixeira() ? "A lixeira está vazia." : "Esta pasta está vazia.";
        g_texto(j, lista.x + (lista.l - g_largura_texto(t)) / 2, lista.y + 40, t, C_FRACO);
        jan_mostrar_area(j, lista.x, lista.y, lista.l, 80);
    }
}

static void desenhar_tudo(void)
{
    desenhar_cmds();
    desenhar_endereco();
    desenhar_lateral();
    cabecalho();
    desenhar_lista();
    desenhar_status();
}

/* Ordem dos itens: as pastas sempre primeiro, depois a coluna escolhida. */
static int comparar_itens(int a, int b)
{
    bool pa = tipos[a] == TIPO_PASTA, pb = tipos[b] == TIPO_PASTA;
    if (pa != pb) return pa ? -1 : 1;
    int r = 0;
    if (ordem_col == 1) {
        const data_t *x = &datas[a], *y = &datas[b];
        uint32_t ka = (uint32_t)x->ano << 26 | (uint32_t)x->mes << 22 | (uint32_t)x->dia << 17 | (uint32_t)x->hora << 12 | (uint32_t)x->minuto << 6 | x->segundo;
        uint32_t kb = (uint32_t)y->ano << 26 | (uint32_t)y->mes << 22 | (uint32_t)y->dia << 17 | (uint32_t)y->hora << 12 | (uint32_t)y->minuto << 6 | y->segundo;
        r = ka < kb ? -1 : ka > kb;
    } else if (ordem_col == 2) {
        char ta[40], tb[40];
        texto_tipo(a, ta, sizeof ta);
        texto_tipo(b, tb, sizeof tb);
        r = compara_texto(ta, tb);
    } else if (ordem_col == 3) r = tamanhos[a] < tamanhos[b] ? -1 : tamanhos[a] > tamanhos[b];
    if (r == 0) r = compara_texto(nomes[a], nomes[b]);
    return ordem_desc ? -r : r;
}

static bool contem(const char *s, const char *t)   /* sem diferença de maiúsculas */
{
    int n = (int)tam_texto(t);
    for (const char *p = s; *p; p++) {
        int k = 0;
        while (k < n && p[k]) {
            char a = p[k], b = t[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            k++;
        }
        if (k == n) return true;
    }
    return n == 0;
}

/* Lê a pasta (com o filtro da pesquisa), ordena e desenha tudo. */
static void carregar(void)
{
    entrada_pasta_t e;
    int n = 0;
    for (int i = 0; n < MAX_ITENS && listar(pasta, i, &e) == 1; i++) {
        if (e.nome[0] == '.') continue;   /* escondidos: a lixeira e o .origens */
        if (busca[0] && !contem(e.nome, busca)) continue;
        copia_texto(nomes[n], e.nome, 64);
        tipos[n] = (uint8_t)e.tipo;
        tamanhos[n] = e.tamanho;
        char c[220];
        caminho_de(n, c, sizeof c);
        if (data_arquivo(c, &datas[n]) < 0) memset(&datas[n], 0, sizeof datas[n]);
        n++;
    }
    /* ordena (inserção: no máximo 128 itens) */
    for (int i = 1; i < n; i++) {
        for (int k = i; k > 0 && comparar_itens(k - 1, k) > 0; k--) {
            char t[64]; uint8_t tp = tipos[k]; uint32_t tm = tamanhos[k]; data_t dt = datas[k];
            copia_texto(t, nomes[k], 64);
            copia_texto(nomes[k], nomes[k - 1], 64); tipos[k] = tipos[k - 1]; tamanhos[k] = tamanhos[k - 1]; datas[k] = datas[k - 1];
            copia_texto(nomes[k - 1], t, 64); tipos[k - 1] = tp; tamanhos[k - 1] = tm; datas[k - 1] = dt;
        }
    }
    lista.qtd = n;
    lista.sel = n ? 0 : -1;
    for (int i = 0; i < MAX_ITENS; i++) marc[i] = 0;
    if (n) marc[0] = 1;
    lista.editando = lista.laco = false;
    lista.topo = 0;
    lista.sob = 0;
    status_msg[0] = 0;
    desenhar_tudo();
    if (busca[0]) registrar("arquivos: busca \"%s\" em %s (%d itens)", busca, pasta, n);
    registrar("arquivos: pasta %s (%d itens)", pasta, n);
    char ordem[200] = "";
    for (int i = 0; i < n && i < 10; i++) { if (i) junta_texto(ordem, ", ", sizeof ordem); junta_texto(ordem, nomes[i], sizeof ordem); }
    if (n) registrar("arquivos: ordem: %s", ordem);
}

/* Vai para outra pasta, guardando o caminho para o voltar e o avançar. */
static void ir(const char *nova)
{
    if (compara_texto(nova, pasta) != 0 || hist_n == 0) {
        if (hist_pos < hist_n - 1) hist_n = hist_pos + 1;   /* depois de voltar, ir para outro lugar apaga o avançar */
        if (hist_n == 24) { for (int k = 1; k < 24; k++) copia_texto(hist[k - 1], hist[k], sizeof hist[0]); hist_n--; }
        copia_texto(hist[hist_n++], nova, sizeof hist[0]);
        hist_pos = hist_n - 1;
    }
    copia_texto(pasta, nova, sizeof pasta);
    busca[0] = 0;
    busca_foco = false;
    abrir_ate(pasta);
    mostrar_atual = true;
    carregar();
}

static void historico(int d)
{
    int p = hist_pos + d;
    if (p < 0 || p >= hist_n) return;
    info_arquivo_t inf;
    if (info_arquivo(hist[p], &inf) < 0 || inf.tipo != TIPO_PASTA) return;
    hist_pos = p;
    copia_texto(pasta, hist[p], sizeof pasta);
    busca[0] = 0;
    busca_foco = false;
    abrir_ate(pasta);
    mostrar_atual = true;
    registrar("arquivos: %s -> %s", d < 0 ? "voltar" : "avancar", pasta);
    carregar();
}

static void entrar(const char *nova)
{
    info_arquivo_t inf;
    if (info_arquivo(nova, &inf) < 0 || inf.tipo != TIPO_PASTA) return;
    ir(nova);
}


/* Confere os pendrives; devolve true se algo mudou. */
static bool atualizar_pendrive(void)
{
    info_disco_t d;
    bool achou = false;
    for (int i = 0; listar_discos(i, &d) == 1; i++) {
        /* Disco interno só se for a partição do SaguiOS: o resto do HD é de
         * quem testa, e o kernel nem monta (D062). */
        if (d.interno && compara_texto(d.rotulo, "SAGUIOS") != 0) continue;
        if (d.suportado) { achou = true; break; }
    }
    bool mudou = achou != tem_pendrive || (achou && (d.montado != pendrive.montado || compara_texto(d.nome, pendrive.nome) != 0));
    tem_pendrive = achou;
    if (achou) pendrive = d;
    return mudou;
}

static void voltar(void)   /* sobe para a pasta de cima */
{
    if (compara_texto(pasta, "/") == 0) return;
    char cima[160];
    copia_texto(cima, pasta, sizeof cima);
    char *b = acha_ultimo_char(cima, '/');
    if (b == cima) cima[1] = 0; else *b = 0;
    ir(cima);
}

/* Que programa abre este arquivo, pela extensão (dois cliques). O que não
 * estiver na lista abre no Editor, como antes. */
static bool termina_com(const char *s, const char *fim)
{
    unsigned a = tam_texto(s), b = tam_texto(fim);
    if (a < b) return false;
    for (unsigned i = 0; i < b; i++) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != fim[i]) return false;
    }
    return true;
}

static const char *programa_para(const char *nome)
{
    static const struct { const char *ext, *prog; } tabela[] = {
        { ".wav", "/bin/tocador" }, { ".mp3", "/bin/tocador" },
        { ".html", "/bin/cipo" }, { ".htm", "/bin/cipo" },
        { ".png", "/bin/visualizador" }, { ".jpg", "/bin/visualizador" }, { ".jpeg", "/bin/visualizador" },
        { ".gif", "/bin/visualizador" }, { ".webp", "/bin/visualizador" }, { ".bmp", "/bin/visualizador" },
        { ".sgi", "/bin/visualizador" }, { ".svg", "/bin/cipo" }, { ".mp4", "/bin/video" },
    };
    for (unsigned k = 0; k < sizeof tabela / sizeof tabela[0]; k++)
        if (termina_com(nome, tabela[k].ext)) return tabela[k].prog;
    return "/bin/editor";
}

static void abrir_com(int i, const char *programa)
{
    char c[200];
    caminho_de(i, c, sizeof c);
    const char *nome = programa;
    for (const char *p = programa; *p; p++) if (*p == '/') nome = p + 1;
    char *argv[3] = { (char *)nome, c, NULL };
    int pid = executar(programa, argv);
    registrar("arquivos: abrindo %s com %s (pid %d)", c, programa, pid);
    char msg[200];
    formatar_texto(msg, sizeof msg, pid > 0 ? "Abrindo %s..." : "Não consegui abrir %s", nomes[i]);
    desenhar_rodape(msg);
}

static int tipo_pacote(const char *n, int *ext);
static bool extrair(int i, bool em_pasta);
static char extraido[64];   /* a pasta que o último "Extrair para" criou */
static void caminho_nome(const char *nome, char *saida, size_t max);

static void abrir_item(int i)
{
    if (i < 0 || i >= lista.qtd) return;
    char c[200];
    caminho_de(i, c, sizeof c);
    if (tipos[i] == TIPO_PASTA) { entrar(c); return; }
    int ext, tp = tipos[i] == TIPO_PROGRAMA ? 0 : tipo_pacote(nomes[i], &ext);
    if (tp) {
        /* Abrir um pacote: extrai numa pasta com o nome dele e entra nela (o .gz sai ao lado) */
        if (extrair(i, tp != 4) && tp != 4) {
            char d[200];
            caminho_nome(extraido, d, sizeof d);
            entrar(d);
        }
        return;
    }
    char *argv[3];
    int pid;
    if (tipos[i] == TIPO_PROGRAMA) {
        /* Só programa com a marca de janela (e_flags do ELF, posta pelo cc)
         * abre direto; os de texto e os sem marca abrem num Terminal, senão
         * o que escrevem não aparece em lugar nenhum. */
        unsigned char cab[52];
        unsigned flags = 0;
        int fd = abrir(c, ABRIR_LER);
        if (fd >= 0) {
            if (ler(fd, cab, sizeof cab) == (int)sizeof cab)
                flags = (unsigned)cab[36] | ((unsigned)cab[37] << 8) | ((unsigned)cab[38] << 16) | ((unsigned)cab[39] << 24);
            fechar(fd);
        }
        /* Os programas do sistema (/bin) já sabem o que são: abrem direto. */
        if (flags == ELF_SAGUI_JANELA || comeca_com(c, "/bin/")) {
            argv[0] = nomes[i]; argv[1] = NULL;
            pid = executar(c, argv);
        } else {
            char *t[4] = { "terminal", "-e", c, NULL };
            pid = executar("/bin/terminal", t);
        }
        registrar("arquivos: executando %s (pid %d)", c, pid);
    } else {
        abrir_com(i, programa_para(nomes[i]));
        return;
    }
    char msg[200];
    formatar_texto(msg, sizeof msg, pid > 0 ? "Abrindo %s..." : "Não consegui abrir %s", nomes[i]);
    desenhar_rodape(msg);
}

/* ------------------------------------------------------------ criar e renomear na linha (D058)
 *
 * Como nos outros sistemas: "Nova pasta" e "Novo arquivo de texto" criam o
 * item com um nome livre ("Nova pasta", "Nova pasta (2)"...) e já abrem a
 * edição do nome na própria linha da lista, com o nome (sem a extensão)
 * marcado. Renomear (menu ou F2) usa a mesma edição. */

static void caminho_nome(const char *nome, char *saida, size_t max)
{
    copia_texto(saida, pasta, max);
    if (compara_texto(pasta, "/") != 0) junta_texto(saida, "/", max);
    junta_texto(saida, nome, max);
}

static bool existe(const char *nome)
{
    char c[200];
    info_arquivo_t inf;
    caminho_nome(nome, c, sizeof c);
    return info_arquivo(c, &inf) == 0;
}

/* "Nova pasta", "Nova pasta (2)"... ou "Novo arquivo.txt", "Novo arquivo (2).txt". */
static void nome_livre(const char *base, const char *ext, char *saida, int max)
{
    formatar_texto(saida, (size_t)max, "%s%s", base, ext);
    for (int n = 2; existe(saida) && n < 1000; n++) formatar_texto(saida, (size_t)max, "%s (%d)%s", base, n, ext);
}

static int indice_de(const char *nome)
{
    for (int i = 0; i < lista.qtd; i++) if (compara_texto(nomes[i], nome) == 0) return i;
    return -1;
}

/* Marca só o item i (depois de recarregar a pasta). */
static void selecionar(int i)
{
    if (i < 0) return;
    for (int k = 0; k < lista.qtd; k++) marc[k] = 0;
    marc[i] = 1;
    lista.sel = i;
    desenhar_lista();
    desenhar_cmds();
    desenhar_status();
}

/* ------------------------------------------------------------ pacotes: extrair e compactar
 *
 * O trabalho é dos programas de sempre (zip, unzip, tar, gzip, H93): o
 * Arquivos só monta o pedido, espera e mostra o resultado. Nada é
 * substituído: "Extrair aqui" com um arquivo que já existe pára e avisa, e
 * as pastas e pacotes novos ganham " (2)" se o nome já estiver em uso. */
enum { PK_NADA, PK_ZIP, PK_TAR, PK_TGZ, PK_GZ };

/* O tipo do pacote pelo nome, e quantas letras tem a extensão. */
static int tipo_pacote(const char *n, int *ext)
{
    if (termina_com(n, ".tar.gz")) { *ext = 7; return PK_TGZ; }
    if (termina_com(n, ".tgz")) { *ext = 4; return PK_TGZ; }
    if (termina_com(n, ".zip")) { *ext = 4; return PK_ZIP; }
    if (termina_com(n, ".tar")) { *ext = 4; return PK_TAR; }
    if (termina_com(n, ".gz")) { *ext = 3; return PK_GZ; }
    *ext = 0;
    return PK_NADA;
}

/* Roda um programa e espera; devolve o código de saída (-1 se não abriu). */
static int rodar_e_esperar(const char *prog, char *const argv[])
{
    int pid = executar(prog, argv);
    if (pid < 0) return -1;
    int codigo = -1;
    esperar(pid, &codigo);
    return codigo;
}

/* O nome do item sem a extensão do pacote ("fotos.tar.gz" -> "fotos"). */
static void base_do_pacote(int i, char *saida, int max)
{
    int ext;
    tipo_pacote(nomes[i], &ext);
    int n = (int)tam_texto(nomes[i]) - ext;
    if (n >= max) n = max - 1;
    memcpy(saida, nomes[i], (size_t)n);
    saida[n] = 0;
}

static bool extrair(int i, bool em_pasta)
{
    if (i < 0 || i >= lista.qtd) return false;
    int ext, tipo = tipo_pacote(nomes[i], &ext);
    if (tipo == PK_NADA || (tipo == PK_GZ && em_pasta)) return false;
    char pacote[200], base[64], novo[64] = "", destino[200];
    caminho_de(i, pacote, sizeof pacote);
    base_do_pacote(i, base, sizeof base);
    copia_texto(destino, pasta, sizeof destino);
    if (em_pasta) {
        nome_livre(base, "", novo, sizeof novo);
        caminho_nome(novo, destino, sizeof destino);
        if (criar_pasta(destino) < 0) { desenhar_rodape("Não deu para criar a pasta."); return false; }
    }
    desenhar_rodape("Extraindo...");
    int codigo;
    if (tipo == PK_ZIP) { char *argv[] = { "unzip", "-q", "-d", destino, pacote, NULL }; codigo = rodar_e_esperar("/bin/unzip", argv); }
    else if (tipo == PK_GZ) {
        /* um arquivo só: sai ao lado, sem o .gz (o original fica) */
        copia_texto(novo, base, sizeof novo);
        char *argv[] = { "gzip", "-d", "-k", pacote, NULL };
        codigo = rodar_e_esperar("/bin/gzip", argv);
    } else { char *argv[] = { "tar", "xf", pacote, "-C", destino, NULL }; codigo = rodar_e_esperar("/bin/tar", argv); }
    registrar("arquivos: extrair %s em %s: codigo %d", pacote, destino, codigo);
    carregar();
    if (novo[0]) selecionar(indice_de(novo));
    char msg[120];
    if (codigo == 0 && em_pasta) formatar_texto(msg, sizeof msg, "Extraído em %s/", novo);
    else if (codigo == 0) formatar_texto(msg, sizeof msg, "Extraído aqui%s%s", tipo == PK_GZ ? ": " : "", tipo == PK_GZ ? novo : "");
    else formatar_texto(msg, sizeof msg, "Não deu para extrair (algum arquivo já existe, ou o pacote está estragado).");
    desenhar_rodape(msg);
    copia_texto(extraido, novo, sizeof extraido);
    return codigo == 0;
}

/* Compacta os marcados num .zip (formato 0) ou .tar.gz (1), na pasta atual. */
static void compactar(int formato)
{
    char *argv[40];
    int k = 0;
    argv[k++] = formato ? "tar" : "zip";
    argv[k++] = formato ? "czf" : "-q";
    char nome[72], base[64];
    int primeiro = -1, qtd = 0;
    for (int i = 0; i < lista.qtd; i++) if (marc[i]) { if (primeiro < 0) primeiro = i; qtd++; }
    if (primeiro < 0) return;
    if (qtd == 1) {
        copia_texto(base, nomes[primeiro], sizeof base);
        if (tipos[primeiro] != TIPO_PASTA) { char *ponto = NULL; for (char *p = base + 1; *p; p++) if (*p == '.') ponto = p; if (ponto) *ponto = 0; }
    } else {
        /* vários: o nome da pasta onde estão */
        const char *b = pasta;
        for (const char *p = pasta; *p; p++) if (*p == '/' && p[1]) b = p + 1;
        copia_texto(base, compara_texto(pasta, "/") == 0 ? "SaguiOS" : b, sizeof base);
    }
    nome_livre(base, formato ? ".tar.gz" : ".zip", nome, sizeof nome);
    argv[k++] = nome;
    for (int i = 0; i < lista.qtd && k < 39; i++) if (marc[i]) argv[k++] = nomes[i];
    argv[k] = NULL;
    desenhar_rodape("Compactando...");
    /* os nomes ficam relativos à pasta atual dentro do pacote */
    char antes[200];
    if (pasta_atual(antes, sizeof antes) < 0) copia_texto(antes, "/", sizeof antes);
    mudar_pasta(pasta);
    int codigo = rodar_e_esperar(formato ? "/bin/tar" : "/bin/zip", argv);
    mudar_pasta(antes);
    registrar("arquivos: compactar %d item(ns) em %s/%s: codigo %d", qtd, compara_texto(pasta, "/") == 0 ? "" : pasta, nome, codigo);
    carregar();
    selecionar(indice_de(nome));
    char msg[120];
    if (codigo == 0) formatar_texto(msg, sizeof msg, "Compactado em %s", nome);
    else formatar_texto(msg, sizeof msg, "Não deu para compactar (código %d).", codigo);
    desenhar_rodape(msg);
}

/* Quantos caracteres marcar ao editar: o nome sem a extensão (nas pastas, tudo). */
static int parte_do_nome(int i)
{
    if (tipos[i] == TIPO_PASTA) return (int)tam_texto(nomes[i]);
    char *ponto = acha_ultimo_char(nomes[i], '.');
    return ponto && ponto != nomes[i] ? (int)(ponto - nomes[i]) : (int)tam_texto(nomes[i]);
}

static void renomear_na_linha(int i)
{
    if (i < 0 || i >= lista.qtd) return;
    registrar("arquivos: renomeando %s", nomes[i]);
    desenhar_rodape("Digite o nome. Enter confirma, Esc desiste.");
    lista_editar(j, &lista, i, parte_do_nome(i));
}

enum { NOVO_PASTA, NOVO_TEXTO, NOVO_PAGINA };

static void criar_novo(int tipo)
{
    char nome[64], c[200];
    nome_livre(tipo == NOVO_PASTA ? "Nova pasta" : tipo == NOVO_TEXTO ? "Novo arquivo" : "Nova página",
               tipo == NOVO_PASTA ? "" : tipo == NOVO_TEXTO ? ".txt" : ".html", nome, sizeof nome);
    caminho_nome(nome, c, sizeof c);
    int r;
    if (tipo == NOVO_PASTA) r = criar_pasta(c);
    else {
        r = abrir(c, ABRIR_ESCREVER | ABRIR_CRIAR);
        if (r >= 0) {
            if (tipo == NOVO_PAGINA)
                escrever_texto(r, "<!doctype html>\n<html>\n<head><meta charset=\"utf-8\"><title>Nova página</title></head>\n"
                                  "<body>\n<h1>Nova página</h1>\n</body>\n</html>\n");
            fechar(r);
            r = 0;
        }
    }
    if (r < 0) {
        char m[200];
        formatar_texto(m, sizeof m, "Não consegui criar %s:\n%s", nome, nome_erro(r));
        mensagem("Arquivos", m);
        return;
    }
    registrar("arquivos: criado %s", c);
    carregar();
    renomear_na_linha(indice_de(nome));
}

/* O nome editado na linha chegou: renomeia, se mudou e se pode. */
static void terminar_renomear(void)
{
    int i = lista.ed_item;
    if (i < 0 || i >= lista.qtd) return;
    char novo[64];
    copia_texto(novo, lista.ed_texto, sizeof novo);
    /* tira os espaços das pontas */
    int n = (int)tam_texto(novo);
    while (n > 0 && novo[n - 1] == ' ') novo[--n] = 0;
    char *ini = novo;
    while (*ini == ' ') ini++;
    if (!*ini || compara_texto(ini, nomes[i]) == 0) { desenhar_lista(); return; }
    char m[240];
    if (compara_texto(ini, ".") == 0 || compara_texto(ini, "..") == 0 || ini[0] == '.') {
        mensagem("Arquivos", "O nome não pode começar com ponto\n(esses ficam escondidos).");
        renomear_na_linha(i);
        return;
    }
    if (existe(ini)) {
        formatar_texto(m, sizeof m, "Já existe \"%s\" nesta pasta.", ini);
        mensagem("Arquivos", m);
        renomear_na_linha(i);
        return;
    }
    char de[200], para[200];
    caminho_de(i, de, sizeof de);
    caminho_nome(ini, para, sizeof para);
    int r = renomear(de, para);
    if (r < 0) {
        formatar_texto(m, sizeof m, "Não consegui renomear:\n%s", nome_erro(r));
        mensagem("Arquivos", m);
        desenhar_lista();
        return;
    }
    registrar("arquivos: renomeado %s -> %s", de, para);
    char guardado[64];
    copia_texto(guardado, ini, sizeof guardado);
    carregar();
    selecionar(indice_de(guardado));
}

/* ------------------------------------------------------------ lixeira (issue #25, parte 5) */

/* A lixeira do disco de "caminho": /usb/.lixeira no pendrive, /.lixeira no resto. */
static const char *lixeira_de(const char *caminho)
{
    return comeca_com(caminho, "/usb/") || compara_texto(caminho, "/usb") == 0 ? "/usb/.lixeira" : "/.lixeira";
}

static bool na_lixeira(void)
{
    return compara_texto(pasta, "/.lixeira") == 0 || compara_texto(pasta, "/usb/.lixeira") == 0;
}


/* Apaga um arquivo ou uma pasta com tudo dentro. */
static int apagar_tudo(const char *c, int nivel)
{
    info_arquivo_t inf;
    int r = info_arquivo(c, &inf);
    if (r < 0) return r;
    if (inf.tipo != TIPO_PASTA) return apagar(c);
    if (nivel > 32) return ERRO_LIMITE;
    entrada_pasta_t e;
    char filho[200];
    while (listar(c, 0, &e) == 1) {   /* sempre o primeiro: a lista encolhe */
        copia_texto(filho, c, sizeof filho);
        junta_texto(filho, "/", sizeof filho);
        junta_texto(filho, e.nome, sizeof filho);
        r = apagar_tudo(filho, nivel + 1);
        if (r < 0) return r;
    }
    return remover_pasta(c);
}

/* Procura "nome" no .origens da lixeira; devolve o caminho original. */
static bool origem_de(const char *lix, const char *nome, char *orig, int max)
{
    char arq[200], linha[260];
    formatar_texto(arq, sizeof arq, "%s/.origens", lix);
    int fd = abrir(arq, ABRIR_LER);
    if (fd < 0) return false;
    bool achou = false;
    while (ler_linha(fd, linha, sizeof linha) >= 0) {
        char *tab = acha_char(linha, '\t');
        if (!tab) continue;
        *tab = 0;
        if (compara_texto(linha, nome) == 0) { copia_texto(orig, tab + 1, (size_t)max); achou = true; }   /* a última vale */
    }
    fechar(fd);
    return achou;
}

static void mover_para_lixeira_c(const char *c)
{
    const char *lix = lixeira_de(c);
    const char *nome = acha_ultimo_char(c, '/') ? acha_ultimo_char(c, '/') + 1 : c;
    criar_pasta(lix);
    char destino[200];
    info_arquivo_t inf;
    formatar_texto(destino, sizeof destino, "%s/%s", lix, nome);
    for (int n = 2; info_arquivo(destino, &inf) == 0 && n < 1000; n++)   /* já tem um com esse nome */
        formatar_texto(destino, sizeof destino, "%s/%s~%d", lix, nome, n);
    int r = renomear(c, destino);
    if (r < 0) {
        char m[240];
        formatar_texto(m, sizeof m, "Não consegui pôr na lixeira:\n%s", nome_erro(r));
        mensagem("Arquivos", m);
        registrar("arquivos: lixeira falhou %s (%d)", c, r);
        return;
    }
    char arq[200], linha[300];
    formatar_texto(arq, sizeof arq, "%s/.origens", lix);
    int fd = abrir(arq, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR);
    if (fd >= 0) {
        formatar_texto(linha, sizeof linha, "%s\t%s\n", acha_ultimo_char(destino, '/') + 1, c);
        escrever_texto(fd, linha);
        fechar(fd);
    }
    registrar("arquivos: apagado %s (para a lixeira: %s)", c, destino);
}

static void mover_para_lixeira(int i, const char *c) { (void)i; mover_para_lixeira_c(c); }

/* Os itens a que uma ação se aplica: os marcados ou, sem nenhum, o da
 * seleção. */
static int alvos(int *saida)
{
    int n = 0;
    for (int i = 0; i < lista.qtd; i++) if (marc[i]) saida[n++] = i;
    if (!n && lista.sel >= 0 && lista.sel < lista.qtd) saida[n++] = lista.sel;
    return n;
}

/* O pendrive inteiro (/usb) e os programas e arquivos do sistema (/bin,
 * /sistema) não são apagados pelo Arquivos: marcar "usb" na raiz e apagar
 * de vez levaria tudo o que há no pendrive. */
static bool protegido(const char *c)
{
    static const char *const raizes[] = { "/usb", "/bin", "/sistema" };
    for (unsigned k = 0; k < sizeof raizes / sizeof raizes[0]; k++) {
        size_t n = tam_texto(raizes[k]);
        if (compara_texto_n(c, raizes[k], n) == 0 && (c[n] == 0 || (c[n] == '/' && k > 0))) return true;
    }
    return compara_texto(c, "/") == 0;
}

static void apagar_item_modo(bool de_vez)
{
    static int itens[MAX_ITENS];
    int n = alvos(itens);
    if (!n) return;
    char pergunta[240];
    de_vez = de_vez || na_lixeira();
    if (n == 1) {
        int i = itens[0];
        if (de_vez) formatar_texto(pergunta, sizeof pergunta, "Apagar de vez\n%s%s?\nNão dá para desfazer.", nomes[i], tipos[i] == TIPO_PASTA ? "/" : "");
        else formatar_texto(pergunta, sizeof pergunta, "Tem certeza que quer apagar\n%s%s?\nVai para a Lixeira.", nomes[i], tipos[i] == TIPO_PASTA ? "/" : "");
    } else if (de_vez) formatar_texto(pergunta, sizeof pergunta, "Apagar de vez os %d itens marcados?\nNão dá para desfazer.", n);
    else formatar_texto(pergunta, sizeof pergunta, "Tem certeza que quer apagar\nos %d itens marcados?\nVão para a Lixeira.", n);
    if (!confirmar("Apagar", pergunta)) return;
    for (int k = 0; k < n; k++) {
        int i = itens[k];
        char c[200];
        caminho_de(i, c, sizeof c);
        if (protegido(c)) { char m[200]; formatar_texto(m, sizeof m, "%s é do sistema ou é o pendrive:\nnão dá para apagar daqui.", c); mensagem("Arquivos", m); continue; }
        if (!de_vez) { mover_para_lixeira(i, c); continue; }
        int r = apagar_tudo(c, 0);
        if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui apagar %s:\n%s", nomes[i], nome_erro(r)); mensagem("Arquivos", m); continue; }
        registrar("arquivos: apagado de vez %s", c);
    }
    carregar();
}

static void apagar_item(void) { apagar_item_modo(false); }

static void restaurar_um(int i)
{
    char c[200], orig[200];
    caminho_de(i, c, sizeof c);
    if (!origem_de(pasta, nomes[i], orig, sizeof orig)) {   /* sem registro: volta para a raiz do disco */
        formatar_texto(orig, sizeof orig, "%s/%s", compara_texto(pasta, "/.lixeira") == 0 ? "" : "/usb", nomes[i]);
    }
    info_arquivo_t inf;
    if (info_arquivo(orig, &inf) == 0) {
        char m[260];
        formatar_texto(m, sizeof m, "Já existe %s.\nApague ou renomeie antes de restaurar.", orig);
        mensagem("Arquivos", m);
        return;
    }
    char pai[200];   /* a pasta de origem pode ter sido apagada: recria */
    copia_texto(pai, orig, sizeof pai);
    for (char *k = pai + 1; *k; k++) if (*k == '/') { *k = 0; criar_pasta(pai); *k = '/'; }
    int r = renomear(c, orig);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui restaurar:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
    registrar("arquivos: restaurado %s -> %s", c, orig);
}

static void restaurar_item(void)
{
    static int itens[MAX_ITENS];
    if (!na_lixeira()) return;
    int n = alvos(itens);
    for (int k = 0; k < n; k++) restaurar_um(itens[k]);
    if (n) carregar();
}

static void esvaziar_lixeira(void)
{
    if (!na_lixeira() || lista.qtd == 0) return;
    if (!confirmar("Esvaziar lixeira", "Apagar de vez tudo o que está na lixeira?")) return;
    char lix[32];
    copia_texto(lix, pasta, sizeof lix);
    int r = apagar_tudo(lix, 0);
    criar_pasta(lix);
    registrar("arquivos: lixeira esvaziada %s (%d)", lix, r);
    carregar();
}

static void abrir_lixeira(void)
{
    const char *lix = comeca_com(pasta, "/usb") && !na_lixeira() ? "/usb/.lixeira" : "/.lixeira";
    if (na_lixeira()) lix = pasta;
    criar_pasta(lix);
    ir(lix);
}

/* ------------------------------------------------------------ menu do botão direito (D058)
 *
 * Num item: abrir, abrir no Editor, renomear, apagar e os "Novo". No vazio:
 * só os "Novo", atualizar e voltar. Com vários marcados, o que vale para um
 * só (abrir, renomear) some. A escolha volta pelo índice; "acoes" diz qual é. */
enum { AC_ABRIR, AC_EDITOR, AC_RENOMEAR, AC_APAGAR, AC_PASTA, AC_TEXTO, AC_PAGINA, AC_ATUALIZAR, AC_VOLTAR,
       AC_RESTAURAR, AC_APAGAR_VEZ, AC_ESVAZIAR, AC_ORD_NOME, AC_ORD_DATA, AC_ORD_TIPO, AC_ORD_TAMANHO,
       AC_PROPRIEDADES, AC_EXTRAIR_AQUI, AC_EXTRAIR_PASTA, AC_ZIP, AC_TARGZ,
       AC_P_ABRIR, AC_P_JANELA, AC_P_EXPANDIR, AC_P_RECOLHER, AC_P_ESVAZIAR, AC_P_EJETAR, AC_P_NOVA, AC_P_RENOMEAR,
       AC_P_APAGAR, AC_P_SOBRE, AC_P_PROPRIEDADES };   /* AC_P_*: menu do painel (H139) */
static int acoes[24], qtd_acoes;

static void acao(char *itens, size_t max, const char *texto, int a)
{
    if (qtd_acoes) junta_texto(itens, "|", max);
    junta_texto(itens, texto, max);
    acoes[qtd_acoes++] = a;
}

static void menu_de_contexto(int x, int y)
{
    static char itens[700];
    itens[0] = 0;
    qtd_acoes = 0;
    int marcados = lista_qtd_marcados(&lista);
    bool no_item = lista.sel >= 0 && lista.sel < lista.qtd && marcados > 0;
    if (na_lixeira()) {
        if (no_item) {
            acao(itens, sizeof itens, marcados > 1 ? "Restaurar os marcados" : "Restaurar", AC_RESTAURAR);
            acao(itens, sizeof itens, "Apagar de vez...", AC_APAGAR_VEZ);
        }
        acao(itens, sizeof itens, "Esvaziar lixeira...", AC_ESVAZIAR);
        acao(itens, sizeof itens, "Voltar", AC_VOLTAR);
        jan_menu(j, x, y, itens);
        return;
    }
    if (no_item && marcados == 1) {
        /* O primeiro item diz com que programa abre (pela extensão). */
        const char *n = nomes[lista.sel];
        bool arquivo = tipos[lista.sel] != TIPO_PASTA && tipos[lista.sel] != TIPO_PROGRAMA;
        const char *prog = arquivo ? programa_para(n) : NULL;
        int ext0;
        bool pacote = arquivo && tipo_pacote(n, &ext0) != PK_NADA;
        const char *primeiro = pacote || !prog ? "Abrir"
            : compara_texto(prog, "/bin/tocador") == 0 ? "Abrir no Tocador"
            : compara_texto(prog, "/bin/cipo") == 0 ? "Abrir no Cipó"
            : compara_texto(prog, "/bin/visualizador") == 0 ? "Abrir no Visualizador"
            : compara_texto(prog, "/bin/video") == 0 ? "Abrir no Vídeo" : "Abrir no Editor";
        acao(itens, sizeof itens, primeiro, AC_ABRIR);
        if (arquivo && !pacote && compara_texto(primeiro, "Abrir no Editor") != 0) acao(itens, sizeof itens, "Abrir no Editor", AC_EDITOR);
        int ext, tp = arquivo ? tipo_pacote(n, &ext) : PK_NADA;
        if (tp != PK_NADA) {
            acao(itens, sizeof itens, "Extrair aqui", AC_EXTRAIR_AQUI);
            if (tp != PK_GZ) {
                char base[64], t[48];
                base_do_pacote(lista.sel, base, sizeof base);
                if (tam_texto(base) > 22) copia_texto(base + 19, "...", 4);
                formatar_texto(t, sizeof t, "Extrair para \"%s/\"", base);
                acao(itens, sizeof itens, t, AC_EXTRAIR_PASTA);
            }
        }
        acao(itens, sizeof itens, "Compactar em .zip", AC_ZIP);
        acao(itens, sizeof itens, "Compactar em .tar.gz", AC_TARGZ);
        acao(itens, sizeof itens, "Renomear", AC_RENOMEAR);
        acao(itens, sizeof itens, "Apagar...", AC_APAGAR);
    } else if (no_item) {
        char t[40];
        acao(itens, sizeof itens, "Compactar em .zip", AC_ZIP);
        acao(itens, sizeof itens, "Compactar em .tar.gz", AC_TARGZ);
        formatar_texto(t, sizeof t, "Apagar os %d itens...", marcados);
        acao(itens, sizeof itens, t, AC_APAGAR);
    }
    acao(itens, sizeof itens, "Nova pasta", AC_PASTA);
    acao(itens, sizeof itens, "Novo arquivo de texto", AC_TEXTO);
    acao(itens, sizeof itens, "Nova página HTML", AC_PAGINA);
    if (!no_item) acao(itens, sizeof itens, "Atualizar", AC_ATUALIZAR);
    acao(itens, sizeof itens, "Voltar", AC_VOLTAR);
    if (no_item && marcados == 1) acao(itens, sizeof itens, "Propriedades", AC_PROPRIEDADES);   /* por último (H139) */
    registrar("arquivos: menu: %s", itens);
    jan_menu(j, x, y, itens);
}

/* Ordenar por uma coluna; a mesma de novo inverte. */
static void ordenar(int col)
{
    static const char *const nome[4] = { "nome", "data", "tipo", "tamanho" };
    if (col == ordem_col) ordem_desc = !ordem_desc;
    else { ordem_col = col; ordem_desc = false; }
    registrar("arquivos: ordenar por %s%s", nome[col], ordem_desc ? " (decrescente)" : "");
    carregar();
}

static void executar_cmd(int i)
{
    cmd_t *c = &cmds[i];
    if (!c->ativo) return;
    registrar("arquivos: comando %s", c->texto);
    static char itens[160];
    itens[0] = 0;
    qtd_acoes = 0;
    switch (c->acao) {
    case CMD_NOVO:
        acao(itens, sizeof itens, "Pasta", AC_PASTA);
        acao(itens, sizeof itens, "Documento de texto", AC_TEXTO);
        acao(itens, sizeof itens, "Página HTML", AC_PAGINA);
        jan_menu(j, c->x, CMD_A - 6, itens);
        break;
    case CMD_ORDENAR:
        acao(itens, sizeof itens, ordem_col == 0 ? (ordem_desc ? "Nome (Z-A)" : "Nome (A-Z)") : "Nome", AC_ORD_NOME);
        acao(itens, sizeof itens, ordem_col == 1 ? "Data de modificação (de novo inverte)" : "Data de modificação", AC_ORD_DATA);
        acao(itens, sizeof itens, ordem_col == 2 ? "Tipo (de novo inverte)" : "Tipo", AC_ORD_TIPO);
        acao(itens, sizeof itens, ordem_col == 3 ? "Tamanho (de novo inverte)" : "Tamanho", AC_ORD_TAMANHO);
        jan_menu(j, c->x, CMD_A - 6, itens);
        break;
    case CMD_RENOMEAR: renomear_na_linha(lista.sel); break;
    case CMD_APAGAR: apagar_item(); break;
    case CMD_RESTAURAR: restaurar_item(); break;
    case CMD_APAGAR_VEZ: apagar_item_modo(true); break;
    case CMD_ESVAZIAR: esvaziar_lixeira(); break;
    }
}

static void remover_pendrive(void)
{
    int r = desmontar(pendrive.nome);
    registrar("arquivos: remover com seguranca -> %d", r);
    if (comeca_com(pasta, "/usb")) ir("/");
    atualizar_pendrive();
    desenhar_lateral();
    mensagem("Arquivos", r < 0 ? "Não consegui desmontar o pendrive." : "Pode remover o pendrive.");
}

static void abrir_lugar(int k)
{
    lugar_t *l = &lugares[k];
    if (compara_texto(l->caminho, "/.lixeira") == 0) { copia_texto(pasta, comeca_com(pasta, "/usb") ? pasta : "/", sizeof pasta); abrir_lixeira(); return; }
    if (compara_texto(l->caminho, "/usb") == 0) {
        int r = pendrive.montado ? 0 : montar(pendrive.nome);
        if (r < 0) { char m[120]; formatar_texto(m, sizeof m, "Não consegui montar o pendrive:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
        atualizar_pendrive();
        ir("/usb");
        registrar("arquivos: pendrive aberto");
        return;
    }
    ir(l->caminho);
}

/* ------------------------------------------------------------ arrastar (H137)
 *
 * Segurar um item marcado e mover o mouse arrasta todos os marcados. O nome
 * (ou "3 itens") vai junto do cursor, com "Mover para ..." quando há um
 * destino embaixo: uma pasta da lista, uma pasta ou atalho do painel, ou um
 * pedaço do caminho. Soltar move; entre a memória e o pendrive, copia (os
 * originais ficam). Soltar na Lixeira manda para a lixeira. */
static bool arr_pronto, arr_movendo;
static int arr_x0, arr_y0, arr_mx, arr_my;
static char arr_destino[160];        /* "" = nenhum destino válido embaixo */
static char arr_rotulo[48];          /* o nome do destino, para o "Mover para" */
static int fan_x, fan_y, fan_l, fan_a;   /* onde o fantasma foi desenhado (para apagar) */

static bool mesmo_disco(const char *a, const char *b)
{
    bool ua = comeca_com(a, "/usb/") || compara_texto(a, "/usb") == 0;
    bool ub = comeca_com(b, "/usb/") || compara_texto(b, "/usb") == 0;
    return ua == ub;
}

/* Copia um arquivo ou uma pasta inteira (entre discos). */
static int copiar_tudo(const char *de, const char *para, int nivel)
{
    info_arquivo_t inf;
    int r = info_arquivo(de, &inf);
    if (r < 0) return r;
    if (inf.tipo == TIPO_PASTA) {
        if (nivel > 32) return ERRO_LIMITE;
        r = criar_pasta(para);
        if (r < 0) return r;
        entrada_pasta_t e;
        for (int i = 0; listar(de, i, &e) == 1; i++) {
            char a[200], b[200];
            formatar_texto(a, sizeof a, "%s/%s", de, e.nome);
            formatar_texto(b, sizeof b, "%s/%s", para, e.nome);
            r = copiar_tudo(a, b, nivel + 1);
            if (r < 0) return r;
        }
        return 0;
    }
    int fe = abrir(de, ABRIR_LER);
    if (fe < 0) return fe;
    int fs = abrir(para, ABRIR_ESCREVER | ABRIR_CRIAR);
    if (fs < 0) { fechar(fe); return fs; }
    static char bloco[4096];
    int n;
    r = 0;
    while ((n = ler(fe, bloco, sizeof bloco)) > 0)
        if (escrever_tudo(fs, bloco, (unsigned)n) < 0) { r = ERRO_ES; break; }
    fechar(fe);
    fechar(fs);
    return r;
}

/* O destino embaixo de (x, y), se dá para soltar lá. */
static void achar_destino(int x, int y)
{
    arr_destino[0] = arr_rotulo[0] = 0;
    lista.sob = 0;
    int ls = -1, sg = -1;
    char c[160] = "";
    if (x >= lista.x && y >= lista.y && y < lista.y + lista.a) {
        int i = lista.topo + (y - lista.y - 2) / ITEM_A;
        if (i >= 0 && i < lista.qtd && tipos[i] == TIPO_PASTA && !marc[i]) {
            caminho_de(i, c, sizeof c);
            copia_texto(arr_rotulo, nomes[i], sizeof arr_rotulo);
            lista.sob = i + 1;
        }
    } else if (x < LATERAL + 1 && y >= CORPO_Y) {
        bool ej;
        ls = lugar_em(x, y, &ej);
        if (ls >= 0) { copia_texto(c, lugares[ls].caminho, sizeof c); copia_texto(arr_rotulo, lugares[ls].texto, sizeof arr_rotulo); }
    } else if (y >= END_Y && y < CORPO_Y) {
        sg = seg_em(x, y);
        if (sg >= 0) { copia_texto(c, segs[sg].caminho, sizeof c); copia_texto(arr_rotulo, segs[sg].texto, sizeof arr_rotulo); }
    }
    if (ls != lugar_sob) { lugar_sob = ls; ejetar_sob = false; }
    if (sg != seg_sob) seg_sob = sg;
    if (!c[0]) return;
    bool lixeira = compara_texto(c, "/.lixeira") == 0;
    if (!lixeira) {
        if (compara_texto(c, pasta) == 0) return;                     /* já está aqui */
        if (comeca_com(c, "/bin") || comeca_com(c, "/sistema")) return;   /* do sistema */
        if (comeca_com(c, "/usb") && !(tem_pendrive && pendrive.montado)) return;
        for (int i = 0; i < lista.qtd; i++) {                         /* uma pasta não vai para dentro dela mesma */
            if (!marc[i]) continue;
            char o[160];
            caminho_de(i, o, sizeof o);
            int n = (int)tam_texto(o);
            if (compara_texto_n(c, o, (size_t)n) == 0 && (c[n] == 0 || c[n] == '/')) return;
        }
    } else if (na_lixeira()) return;
    copia_texto(arr_destino, c, sizeof arr_destino);
}

static void apagar_fantasma(void)
{
    if (!fan_l) return;
    /* redesenha o que estava embaixo: cada parte que o fantasma tocou */
    if (fan_y < CMD_A) desenhar_cmds();
    if (fan_y + fan_a > END_Y && fan_y < CORPO_Y) desenhar_endereco();
    if (fan_x < LATERAL + 1 && fan_y + fan_a > CORPO_Y) desenhar_lateral();
    if (fan_x + fan_l > lista.x && fan_y < CORPO_Y + CAB_A && fan_y + fan_a > CORPO_Y) cabecalho();
    if (fan_x + fan_l > lista.x && fan_y + fan_a > lista.y) desenhar_lista();
    if (fan_y + fan_a > ALT - STATUS_A) desenhar_status();
    fan_l = 0;
}

static void desenhar_fantasma(int x, int y)
{
    int n = lista_qtd_marcados(&lista), um = -1;
    for (int i = 0; i < lista.qtd && um < 0; i++) if (marc[i]) um = i;
    char t1[64], t2[80];
    if (n == 1 && um >= 0) copia_texto(t1, nomes[um], sizeof t1);
    else formatar_texto(t1, sizeof t1, "%d itens", n);
    t2[0] = 0;
    if (arr_destino[0]) {
        bool lx = compara_texto(arr_destino, "/.lixeira") == 0;
        char um_c[160] = "";
        if (um >= 0) caminho_de(um, um_c, sizeof um_c);
        formatar_texto(t2, sizeof t2, "%s %s", lx ? "Mandar para a" : mesmo_disco(um_c, arr_destino) ? "Mover para" : "Copiar para", arr_rotulo);
    }
    int l1 = g_largura_texto(t1), l2 = t2[0] ? g_largura_texto(t2) : 0;
    int l = 16 + 22 + (l1 > l2 ? l1 : l2) + 12, a = t2[0] ? 46 : 28;
    if (l > 300) l = 300;
    x += 14; y += 10;
    if (x + l > LARG) x = LARG - l;
    if (y + a > ALT) y = ALT - a;
    g_ret_redondo(j, x, y, l, a, 5, 0xEAF3FF, 0xEAF3FF);
    g_contorno_redondo(j, x, y, l, a, 5, 0x8DB8E8);
    if (um >= 0) icone(j, x + 8, y + 6, um);
    g_texto_n(j, x + 30, y + 6, t1, (l - 38) / LETRA_L, C_TEXTO);
    if (t2[0]) g_texto_n(j, x + 10, y + 26, t2, (l - 18) / LETRA_L, arr_destino[0] ? C_MARCA : C_FRACO);
    jan_mostrar_area(j, x, y, l, a);
    fan_x = x; fan_y = y; fan_l = l; fan_a = a;
}

static void arrastar_para_fora(void);

static void arrastar_mover(int x, int y)
{
    if (arr_movendo && (x < 0 || y < 0 || x >= LARG || y >= ALT)) { arrastar_para_fora(); return; }
    if (!arr_movendo) {
        int dx = x - arr_x0, dy = y - arr_y0;
        if (dx * dx + dy * dy < 36) return;
        arr_movendo = true;
        devagar_i = -1;
        lista.pendente = 0;   /* arrastou: a seleção fica como está */
        registrar("arquivos: arrastando %d itens", lista_qtd_marcados(&lista));
    }
    arr_mx = x; arr_my = y;
    char antes[160];
    copia_texto(antes, arr_destino, sizeof antes);
    int lsob = lugar_sob, ssob = seg_sob, sob = lista.sob;
    achar_destino(x, y);
    bool mudou = compara_texto(antes, arr_destino) != 0 || lsob != lugar_sob || ssob != seg_sob || sob != lista.sob;
    if (mudou) {   /* o destaque mudou: redesenha tudo o que pode estar marcado */
        fan_l = 0;
        desenhar_lista();
        desenhar_lateral();
        desenhar_endereco();
    } else apagar_fantasma();
    desenhar_fantasma(x, y);
}

/* Move (ou copia, entre discos) os caminhos para a pasta "dest"; "/.lixeira"
 * manda para a lixeira (com a pergunta). Serve ao arrastar daqui, ao que
 * chega de outra janela (EV_SOLTAR) e ao "arquivos --soltar" que um ícone da
 * área de trabalho abre. Devolve quantos deram certo. */
static int soltar_caminhos(char (*cam)[200], int n, const char *dest)
{
    if (n <= 0) return 0;
    if (compara_texto(dest, "/.lixeira") == 0) {
        char pergunta[240];
        const char *nome = acha_ultimo_char(cam[0], '/') ? acha_ultimo_char(cam[0], '/') + 1 : cam[0];
        if (n == 1) formatar_texto(pergunta, sizeof pergunta, "Tem certeza que quer apagar\n%s?\nVai para a Lixeira.", nome);
        else formatar_texto(pergunta, sizeof pergunta, "Tem certeza que quer apagar\nos %d itens?\nVão para a Lixeira.", n);
        if (!confirmar("Apagar", pergunta)) return 0;
        int feitos = 0;
        for (int k = 0; k < n; k++) {
            if (protegido(cam[k])) continue;
            mover_para_lixeira_c(cam[k]);
            feitos++;
        }
        return feitos;
    }
    int feitos = 0;
    bool copiou = false;
    for (int k = 0; k < n; k++) {
        const char *de = cam[k];
        const char *nome = acha_ultimo_char(de, '/') ? acha_ultimo_char(de, '/') + 1 : de;
        if (protegido(de)) { char m[200]; formatar_texto(m, sizeof m, "%s é do sistema ou é o pendrive:\nnão dá para mover daqui.", de); mensagem("Arquivos", m); continue; }
        int dn = (int)tam_texto(de);   /* uma pasta não vai para dentro dela mesma */
        if (compara_texto_n(dest, de, (size_t)dn) == 0 && (dest[dn] == 0 || dest[dn] == '/')) continue;
        char para[220];
        formatar_texto(para, sizeof para, "%s%s%s", dest, compara_texto(dest, "/") == 0 ? "" : "/", nome);
        if (compara_texto(para, de) == 0) continue;   /* já está aqui */
        info_arquivo_t inf;
        if (info_arquivo(para, &inf) == 0) { char m[260]; formatar_texto(m, sizeof m, "Já existe %s.", para); mensagem("Arquivos", m); continue; }
        int r;
        bool copia = !mesmo_disco(de, dest);
        if (!copia) r = renomear(de, para);
        else { r = copiar_tudo(de, para, 0); copiou = true; }
        if (r < 0) { char m[240]; formatar_texto(m, sizeof m, "Não consegui %s %s:\n%s", copia ? "copiar" : "mover", nome, nome_erro(r)); mensagem("Arquivos", m); continue; }
        registrar("arquivos: %s %s -> %s", copia ? "copiado" : "movido", de, para);
        feitos++;
    }
    if (feitos && j) {
        char m[120];
        formatar_texto(m, sizeof m, "%d %s %s para %s", feitos, feitos == 1 ? "item" : "itens", copiou ? (feitos == 1 ? "copiado" : "copiados") : (feitos == 1 ? "movido" : "movidos"), dest);
        copia_texto(status_msg, m, sizeof status_msg);
    }
    return feitos;
}

static char soltos[64][200];

/* Os caminhos do último arrastar (de outra janela ou de um ícone). */
static int ler_soltos(void)
{
    static char buf[2048];
    if (arrasto_ler(buf, sizeof buf) < 0) return 0;
    int n = 0;
    for (char *p = buf; *p && n < 64;) {
        char *f = acha_char(p, '\n');
        if (f) *f = 0;
        if (*p) copia_texto(soltos[n++], p, sizeof soltos[0]);
        if (!f) break;
        p = f + 1;
    }
    return n;
}

static void arrastar_soltar(void)
{
    bool movendo = arr_movendo;
    arr_pronto = arr_movendo = false;
    if (!movendo) return;
    apagar_fantasma();
    char dest[160];
    copia_texto(dest, arr_destino, sizeof dest);
    arr_destino[0] = 0;
    lista.sob = 0;
    lugar_sob = seg_sob = -1;
    if (!dest[0]) { registrar("arquivos: arrastar: solto fora de um destino"); desenhar_tudo(); return; }
    if (compara_texto(dest, "/.lixeira") == 0) { registrar("arquivos: arrastado para a lixeira"); apagar_item(); return; }
    static int itens[MAX_ITENS];
    int n = alvos(itens);
    for (int k = 0; k < n && k < 64; k++) caminho_de(itens[k], soltos[k], sizeof soltos[0]);
    char guardada[sizeof status_msg];
    soltar_caminhos(soltos, n < 64 ? n : 64, dest);
    copia_texto(guardada, status_msg, sizeof guardada);
    carregar();
    if (guardada[0]) desenhar_rodape(guardada);
}

/* O cursor saiu da janela arrastando: o kernel leva daqui para frente. */
static void arrastar_para_fora(void)
{
    static char caminhos[2048];
    caminhos[0] = 0;
    int n = 0, um = -1;
    for (int i = 0; i < lista.qtd; i++) {
        if (!marc[i]) continue;
        char c[200];
        caminho_de(i, c, sizeof c);
        if (tam_texto(caminhos) + tam_texto(c) + 2 >= sizeof caminhos) break;
        if (n) junta_texto(caminhos, "\n", sizeof caminhos);
        junta_texto(caminhos, c, sizeof caminhos);
        if (um < 0) um = i;
        n++;
    }
    char rot[48];
    if (n == 1) copia_texto(rot, nomes[um], sizeof rot); else formatar_texto(rot, sizeof rot, "%d itens", n);
    apagar_fantasma();
    arr_pronto = arr_movendo = false;
    arr_destino[0] = 0;
    lista.sob = 0;
    lugar_sob = seg_sob = -1;
    desenhar_tudo();
    int r = arrastar(j->id, caminhos, rot);
    registrar("arquivos: arrastando para fora da janela (%d itens) -> %d", n, r);
}

/* EV_SOLTAR: arquivos de outra janela (ou desta, depois de sair e voltar). */
static void receber_soltos(const evento_t *e)
{
    int n = ler_soltos();
    if (!n) return;
    char dest[160];
    achar_destino(e->x, e->y);
    if (arr_destino[0]) copia_texto(dest, arr_destino, sizeof dest);
    else if (e->x >= lista.x && e->y >= CORPO_Y && e->y < ALT - STATUS_A && !na_lixeira()) copia_texto(dest, pasta, sizeof dest);   /* na lista: aqui */
    else { registrar("arquivos: recebidos %d itens fora de um destino", n); arr_destino[0] = 0; lista.sob = 0; lugar_sob = seg_sob = -1; desenhar_tudo(); return; }
    arr_destino[0] = 0;
    lista.sob = 0;
    lugar_sob = seg_sob = -1;
    registrar("arquivos: recebidos %d itens para %s", n, dest);
    soltar_caminhos(soltos, n, dest);
    char guardada[sizeof status_msg];
    copia_texto(guardada, status_msg, sizeof guardada);
    carregar();
    if (guardada[0]) desenhar_rodape(guardada);
}

/* Teclas na caixa de pesquisa: a lista filtra a cada letra. */
static void tecla_busca(unsigned t)
{
    int n = (int)tam_texto(busca);
    if (t == TECLA_ESC) { busca[0] = 0; busca_foco = false; carregar(); return; }
    if (t == '\n') { busca_foco = false; desenhar_endereco(); return; }
    if (t == '\b') { if (n) busca[n - 1] = 0; else return; }
    else if (t >= 0x20 && t < 0x100 && t != 0x7F && n < (int)sizeof busca - 1) { busca[n] = (char)t; busca[n + 1] = 0; }
    else return;
    carregar();
}

/* Movimento do mouse: o realce das barras e do painel. */
static void mover(const evento_t *e)
{
    int c = cmd_em(e->x, e->y);
    if (c != cmd_sob) { cmd_sob = c; desenhar_cmds(); }
    int nv = nav_em(e->x, e->y), sg = seg_em(e->x, e->y);
    if (nv != nav_sob || sg != seg_sob) { nav_sob = nv; seg_sob = sg; desenhar_endereco(); }
    bool ej;
    int l = lugar_em(e->x, e->y, &ej);
    if (l != lugar_sob || ej != ejetar_sob) { lugar_sob = l; ejetar_sob = ej; desenhar_lateral(); }
}

static void menu_do_painel(int k, int x, int y);   /* H139, mais abaixo */

/* Clique nas barras e no painel; devolve true se foi neles. */
static bool clique_fora_da_lista(const evento_t *e)
{
    bool estava = busca_foco;
    if (e->y < CMD_A) {
        int c = cmd_em(e->x, e->y);
        if (c >= 0 && (e->botoes & 1)) executar_cmd(c);
        return true;
    }
    if (e->y < CORPO_Y) {
        if (!(e->botoes & 1)) return true;
        int k = nav_em(e->x, e->y);
        if (k >= 0 && nav_ativo(k)) {
            if (k == NAV_VOLTAR) historico(-1);
            else if (k == NAV_AVANCAR) historico(1);
            else if (k == NAV_SUBIR) voltar();
            else { registrar("arquivos: atualizar"); carregar(); }
            return true;
        }
        int sg = seg_em(e->x, e->y);
        if (sg >= 0) { if (compara_texto(segs[sg].caminho, pasta) != 0) ir(segs[sg].caminho); return true; }
        if (e->x >= busca_x()) { busca_foco = true; desenhar_endereco(); registrar("arquivos: pesquisa"); return true; }
        busca_foco = false;
        if (estava) desenhar_endereco();
        return true;
    }
    if (estava) { busca_foco = false; desenhar_endereco(); }
    if (e->x < LATERAL + 1) {
        bool ej, seta;
        int k = lugar_em_seta(e->x, e->y, &ej, &seta);
        if (k >= 0 && (e->botoes & 2)) { menu_do_painel(k, e->x, e->y); return true; }
        if (k < 0 || !(e->botoes & 1)) return true;
        if (ej) remover_pendrive();
        else if (seta) {   /* abre ou fecha o ramo, sem sair da pasta */
            if (lugares[k].seta == 2) fechar_no(lugares[k].caminho); else abrir_no(lugares[k].caminho);
            registrar("arquivos: arvore %s %s", lugares[k].seta == 2 ? "fecha" : "abre", lugares[k].caminho);
            desenhar_lateral();
        } else abrir_lugar(k);
        return true;
    }
    if (e->y < CORPO_Y + CAB_A) {   /* cabeçalho: ordena pela coluna */
        if (!(e->botoes & 1)) return true;
        for (int k = 3; k >= 0; k--) {
            int x0 = k == 0 ? lista.x : col_x[k];
            if (!x0 && k) continue;
            if (e->x >= x0) { ordenar(k); break; }
        }
        return true;
    }
    if (e->y >= ALT - STATUS_A) return true;
    return false;
}

static void novo_tamanho(void)
{
    lista.x = LATERAL + 1;
    lista.y = CORPO_Y + CAB_A;
    lista.l = LARG - LATERAL - 1;
    lista.a = ALT - CORPO_Y - CAB_A - STATUS_A;
    calcular_colunas();
}

/* ------------------------------------------------------------ menus do painel e Propriedades (H139) */

static int menu_lugar = -1;         /* o item do painel do último menu */
static char menu_caminho[160];

static void medir(const char *c, int nivel, unsigned *arqs, unsigned *pastas, uint32_t *bytes)
{
    entrada_pasta_t e;
    for (int i = 0; listar(c, i, &e) == 1; i++) {
        if (e.nome[0] == '.') continue;
        char f[200];
        formatar_texto(f, sizeof f, "%s%s%s", c, compara_texto(c, "/") == 0 ? "" : "/", e.nome);
        if (compara_texto(f, "/usb") == 0) continue;   /* o pendrive é outro disco */
        if (e.tipo == TIPO_PASTA) { (*pastas)++; if (nivel < 24) medir(f, nivel + 1, arqs, pastas, bytes); }
        else { (*arqs)++; *bytes += e.tamanho; }
    }
}

static void tamanho_longo(uint32_t b, char *s, int max)
{
    char k[24];
    texto_tamanho(b, k, sizeof k);
    formatar_texto(s, (size_t)max, "%s (%u bytes)", k, b);
}

static void espaco(const char *ponto, char *s, int max)
{
    info_espaco_t v[2];
    s[0] = 0;
    int n = info_sistema(INFO_DISCOS_ESPACO, v, 2);
    for (int i = 0; i < n; i++)
        if (compara_texto(v[i].ponto, ponto) == 0)
            formatar_texto(s, (size_t)max, "\nEspaço: %u MB livres de %u MB", v[i].livre_kb / 1024, v[i].total_kb / 1024);
}

/* Propriedades de um caminho (arquivo, pasta, lixeira, pendrive ou o SaguiOS). */
static void propriedades(const char *c)
{
    char t[600], titulo[80], s[64];
    info_arquivo_t inf;
    if (compara_texto(c, "/.lixeira") == 0) {
        unsigned a = 0, p = 0; uint32_t b = 0;
        medir("/.lixeira", 0, &a, &p, &b);
        if (tem_pendrive && pendrive.montado) medir("/usb/.lixeira", 0, &a, &p, &b);
        tamanho_longo(b, s, sizeof s);
        formatar_texto(t, sizeof t, "Itens: %u arquivos, %u pastas\nTamanho: %s", a, p, s);
        registrar("arquivos: propriedades da lixeira (%u arquivos)", a);
        mensagem("Propriedades da Lixeira", t);
        return;
    }
    if (compara_texto(c, "/") == 0) {
        info_versao_t v;
        info_memoria_t mem;
        memset(&v, 0, sizeof v);
        info_sistema(INFO_VERSAO, &v, sizeof v);
        info_sistema(INFO_MEMORIA, &mem, sizeof mem);
        unsigned a = 0, p = 0; uint32_t b = 0;
        medir("/", 0, &a, &p, &b);
        char esp[80];
        espaco("/", esp, sizeof esp);
        tamanho_longo(b, s, sizeof s);
        formatar_texto(t, sizeof t, "Sistema: SaguiOS %s\nArquitetura: %s, %u bits\nMemória: %u MB livres de %u MB\n\nArquivos: %u arquivos, %u pastas\nTamanho: %s%s",
                       v.versao, v.bits == 64 ? "x86-64" : "x86", v.bits ? v.bits : 32, mem.livre_kb / 1024, mem.gerida_kb / 1024, a, p, s, esp);
        registrar("arquivos: propriedades do SaguiOS (versao %s, %u arquivos)", v.versao, a);
        mensagem("Propriedades do SaguiOS", t);
        return;
    }
    if (info_arquivo(c, &inf) < 0) return;
    const char *nome = acha_ultimo_char(c, '/') ? acha_ultimo_char(c, '/') + 1 : c;
    char local[160];
    copia_texto(local, c, sizeof local);
    char *b0 = acha_ultimo_char(local, '/');
    if (b0 == local) local[1] = 0; else if (b0) *b0 = 0;
    data_t d;
    char quando[40] = "não se sabe";
    if (data_arquivo(c, &d) == 0 && d.ano) texto_data(&d, quando, sizeof quando);
    if (compara_texto(c, "/usb") == 0) {
        unsigned a = 0, p = 0; uint32_t b = 0;
        medir("/usb", 0, &a, &p, &b);
        char esp[80];
        espaco("/usb", esp, sizeof esp);
        formatar_texto(titulo, sizeof titulo, "Propriedades de %s", tem_pendrive && pendrive.rotulo[0] ? pendrive.rotulo : "Pendrive");
        formatar_texto(t, sizeof t, "Unidade: %s %s\nFormato: %s\nArquivos: %u arquivos, %u pastas%s",
                       tem_pendrive ? pendrive.fabricante : "", tem_pendrive ? pendrive.produto : "", tem_pendrive ? pendrive.formato : "FAT32", a, p, esp);
    } else if (inf.tipo == TIPO_PASTA) {
        unsigned a = 0, p = 0; uint32_t b = 0;
        medir(c, 0, &a, &p, &b);
        tamanho_longo(b, s, sizeof s);
        formatar_texto(titulo, sizeof titulo, "Propriedades de %s", nome);
        formatar_texto(t, sizeof t, "Tipo: Pasta de arquivos\nLocal: %s\nContém: %u arquivos, %u pastas\nTamanho: %s\nModificado em: %s",
                       local, a, p, s, quando);
    } else {
        char tipo[40] = "Arquivo";
        int k = -1;
        for (int i = 0; i < lista.qtd; i++) if (compara_texto(nomes[i], nome) == 0) k = i;
        if (k >= 0) texto_tipo(k, tipo, sizeof tipo);
        tamanho_longo(inf.tamanho, s, sizeof s);
        formatar_texto(titulo, sizeof titulo, "Propriedades de %s", nome);
        formatar_texto(t, sizeof t, "Tipo: %s\nLocal: %s\nTamanho: %s\nModificado em: %s", tipo, local, s, quando);
    }
    registrar("arquivos: propriedades de %s", c);
    mensagem(titulo, t);
}

static void esvaziar_tudo(void)
{
    static const char *const lix[2] = { "/.lixeira", "/usb/.lixeira" };
    unsigned a = 0, p = 0; uint32_t b = 0;
    medir(lix[0], 0, &a, &p, &b);
    if (tem_pendrive && pendrive.montado) medir(lix[1], 0, &a, &p, &b);
    if (a + p == 0) { mensagem("Esvaziar lixeira", "A lixeira já está vazia."); return; }
    if (!confirmar("Esvaziar lixeira", "Apagar de vez tudo o que está na lixeira?")) return;
    for (int q = 0; q < 2; q++) {
        info_arquivo_t inf;
        if (info_arquivo(lix[q], &inf) < 0) continue;
        int r = apagar_tudo(lix[q], 0);
        criar_pasta(lix[q]);
        registrar("arquivos: lixeira esvaziada %s (%d)", lix[q], r);
    }
    carregar();
}

/* Renomear uma pasta da árvore (numa caixa: a árvore não tem edição na linha). */
static void renomear_lugar(const char *c)
{
    const char *nome = acha_ultimo_char(c, '/') ? acha_ultimo_char(c, '/') + 1 : c;
    char novo[64];
    copia_texto(novo, nome, sizeof novo);
    if (!pedir_texto("Renomear", "Novo nome da pasta:", novo, sizeof novo) || !novo[0] || compara_texto(novo, nome) == 0) return;
    if (acha_char(novo, '/') || novo[0] == '.') { mensagem("Arquivos", "O nome não pode ter / nem começar com ponto."); return; }
    char para[200];
    copia_texto(para, c, sizeof para);
    char *b = acha_ultimo_char(para, '/');
    if (!b) return;
    b[1] = 0;
    junta_texto(para, novo, sizeof para);
    int r = renomear(c, para);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui renomear:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
    registrar("arquivos: renomeado %s -> %s", c, para);
    int n = (int)tam_texto(c);   /* a pasta aberta estava dentro? segue o nome novo */
    if (compara_texto_n(pasta, c, (size_t)n) == 0 && (pasta[n] == 0 || pasta[n] == '/')) {
        char nova[160];
        formatar_texto(nova, sizeof nova, "%s%s", para, pasta + n);
        copia_texto(pasta, nova, sizeof pasta);
    }
    carregar();
}

static void nova_pasta_em(const char *c)
{
    char nome[64], f[200];
    copia_texto(nome, "Nova pasta", sizeof nome);
    info_arquivo_t inf;
    formatar_texto(f, sizeof f, "%s%s%s", c, compara_texto(c, "/") == 0 ? "" : "/", nome);
    for (int k = 2; info_arquivo(f, &inf) == 0 && k < 1000; k++) {
        formatar_texto(nome, sizeof nome, "Nova pasta (%d)", k);
        formatar_texto(f, sizeof f, "%s%s%s", c, compara_texto(c, "/") == 0 ? "" : "/", nome);
    }
    int r = criar_pasta(f);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui criar a pasta:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
    registrar("arquivos: criado %s", f);
    abrir_no(c);
    ir(c);
    renomear_na_linha(indice_de(nome));
}

static void abrir_nova_janela(const char *c)
{
    char *argv[3] = { "arquivos", (char *)c, NULL };
    int pid = executar("/bin/arquivos", argv);
    registrar("arquivos: nova janela em %s (pid %d)", c, pid);
}

static void menu_do_painel(int k, int x, int y)
{
    static char itens[300];
    itens[0] = 0;
    qtd_acoes = 0;
    menu_lugar = k;
    lugar_t *l = &lugares[k];
    copia_texto(menu_caminho, l->caminho, sizeof menu_caminho);
    bool lixeira = compara_texto(l->caminho, "/.lixeira") == 0;
    bool raiz = compara_texto(l->caminho, "/") == 0;
    bool pen = compara_texto(l->caminho, "/usb") == 0;
    bool sistema = comeca_com(l->caminho, "/bin") || comeca_com(l->caminho, "/sistema");
    acao(itens, sizeof itens, "Abrir", AC_P_ABRIR);
    if (!lixeira) acao(itens, sizeof itens, "Abrir em nova janela", AC_P_JANELA);
    if (l->seta == 1) acao(itens, sizeof itens, "Expandir", AC_P_EXPANDIR);
    if (l->seta == 2) acao(itens, sizeof itens, "Recolher", AC_P_RECOLHER);
    if (lixeira) acao(itens, sizeof itens, "Esvaziar lixeira...", AC_P_ESVAZIAR);
    if (pen && l->ejetar) acao(itens, sizeof itens, "Ejetar", AC_P_EJETAR);
    if (!lixeira && !sistema && (!pen || pendrive.montado)) acao(itens, sizeof itens, "Nova pasta aqui", AC_P_NOVA);
    if (!lixeira && !raiz && !pen && !sistema && !l->atalho) {
        acao(itens, sizeof itens, "Renomear...", AC_P_RENOMEAR);
        acao(itens, sizeof itens, "Apagar...", AC_P_APAGAR);
    }
    if (raiz) acao(itens, sizeof itens, "Sobre o SaguiOS", AC_P_SOBRE);
    acao(itens, sizeof itens, "Propriedades", AC_P_PROPRIEDADES);
    registrar("arquivos: menu do painel em %s: %s", l->caminho, itens);
    jan_menu(j, x, y, itens);
}

/* Uma escolha do menu do painel; devolve true se era dele. */
static bool acao_do_painel(int a)
{
    if (a < AC_P_ABRIR) return false;
    char c[160];
    copia_texto(c, menu_caminho, sizeof c);
    int k = menu_lugar;
    switch (a) {
    case AC_P_ABRIR: if (k >= 0 && k < qtd_lugares && compara_texto(lugares[k].caminho, c) == 0) abrir_lugar(k); else ir(c); break;
    case AC_P_JANELA: abrir_nova_janela(c); break;
    case AC_P_EXPANDIR: abrir_no(c); desenhar_lateral(); break;
    case AC_P_RECOLHER: fechar_no(c); desenhar_lateral(); break;
    case AC_P_ESVAZIAR: esvaziar_tudo(); break;
    case AC_P_EJETAR: remover_pendrive(); break;
    case AC_P_NOVA: nova_pasta_em(c); break;
    case AC_P_RENOMEAR: renomear_lugar(c); break;
    case AC_P_APAGAR: {
        static char um[1][200];
        copia_texto(um[0], c, sizeof um[0]);
        if (soltar_caminhos(um, 1, "/.lixeira")) {
            int n = (int)tam_texto(c);
            if (compara_texto_n(pasta, c, (size_t)n) == 0 && (pasta[n] == 0 || pasta[n] == '/')) {   /* estava dentro: sobe */
                char cima[160];
                copia_texto(cima, c, sizeof cima);
                char *b = acha_ultimo_char(cima, '/');
                if (b == cima) cima[1] = 0; else if (b) *b = 0;
                ir(cima);
            } else carregar();
        }
        break;
    }
    case AC_P_SOBRE: { char *argv[2] = { "sobre-o-sistema", NULL }; executar("/bin/sobre-o-sistema", argv); registrar("arquivos: sobre o SaguiOS"); break; }
    case AC_P_PROPRIEDADES: propriedades(c); break;
    }
    return true;
}

int principal(int argc, char **argv)
{
    /* Soltos num ícone da área de trabalho (H138): sem abrir a janela. */
    if (argc > 1 && compara_texto(argv[1], "--soltar-lixeira") == 0) {
        int n = ler_soltos();
        int f = soltar_caminhos(soltos, n, "/.lixeira");
        registrar("arquivos: soltos na lixeira: %d de %d", f, n);
        return 0;
    }
    if (argc > 2 && compara_texto(argv[1], "--soltar") == 0) {
        int n = ler_soltos();
        int f = soltar_caminhos(soltos, n, argv[2]);
        registrar("arquivos: soltos em %s: %d de %d", argv[2], f, n);
        if (f) { char m[120]; formatar_texto(m, sizeof m, "%d %s em %s.", f, f == 1 ? "item copiado" : "itens copiados", argv[2]); mensagem("Arquivos", m); }
        return 0;
    }
    if (argc > 1) copia_texto(pasta, argv[1], sizeof pasta);
    {   /* "Abrir com Arquivos" um arquivo: abre a pasta dele */
        info_arquivo_t inf;
        if (info_arquivo(pasta, &inf) == 0 && inf.tipo != TIPO_PASTA) {
            char *b = acha_ultimo_char(pasta, '/');
            if (b == pasta) pasta[1] = 0; else if (b) *b = 0;
        }
    }
    if (na_lixeira()) criar_pasta(pasta);   /* o ícone da Lixeira abre aqui antes de ela existir */
    j = jan_abrir("Arquivos", LARG, ALT);
    if (!j) return 1;
    jan_redimensionavel(j, 600, 380);
    lista = (lista_t){ .itens = nomes, .qtd = 0, .sel = -1, .topo = 0, .icone = icone, .marc = marc,
                       .alt_item = ITEM_A, .claro = true, .colunas = colunas };
    novo_tamanho();

    atualizar_pendrive();
    ir(pasta);
    jan_mostrar(j);
    registrar("arquivos: pronto");

    evento_t e;
    unsigned ultimo = tempo_ms();
    for (;;) {
        /* Clique devagar no nome já marcado: se o duplo clique não veio,
         * abre o nome para editar na linha (como o F2). */
        if (devagar_i >= 0 && tempo_ms() - devagar_ms >= DEVAGAR_MS) {
            int i = devagar_i;
            devagar_i = -1;
            if (i == lista.sel && !lista.editando && !na_lixeira() && lista_qtd_marcados(&lista) == 1) renomear_na_linha(i);
        }
        if (proximo_evento(j->id, &e, false) != 1) {
            /* Sem evento: de vez em quando confere se um pendrive entrou ou saiu. */
            if (tempo_ms() - ultimo > 700) {
                ultimo = tempo_ms();
                if (atualizar_pendrive()) {
                    desenhar_lateral();
                    info_arquivo_t inf;
                    if (comeca_com(pasta, "/usb") && info_arquivo(pasta, &inf) < 0) ir("/");
                }
            }
            dormir(50);
            continue;
        }
        if (e.tipo == EV_CLIQUE) devagar_i = -1;   /* outro clique (o segundo do duplo, inclusive) cancela */
        int c;
        while (recolher(&c) > 0) ;   /* programas abertos daqui que já terminaram */
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_REDIMENSIONAR) {                   /* H78: tudo acompanha a janela */
            jan_novo_tamanho(j, &e);
            larg_jan = j->larg;
            alt_jan = j->alt;
            novo_tamanho();
            desenhar_tudo();
            jan_mostrar(j);
            registrar("arquivos: tamanho %dx%d", LARG, ALT);
            continue;
        }
        /* D058: com um nome sendo editado na linha, o teclado e os cliques vão
         * primeiro para a lista (um clique fora termina a edição). */
        bool fechou_edicao = false;   /* o clique que fecha uma edição não abre outra */
        if (lista.editando && (e.tipo == EV_TECLA || e.tipo == EV_CLIQUE)) {
            int r = lista_tratar(j, &lista, &e);
            fechou_edicao = r == LISTA_EDITOU;
            if (r == LISTA_EDITOU) terminar_renomear();
            else if (r == LISTA_DESISTIU) desenhar_rodape("Nome mantido.");
            if (e.tipo == EV_TECLA || r == LISTA_NADA) continue;
        }
        if (e.tipo == EV_SOLTAR) { receber_soltos(&e); continue; }
        if (e.tipo == EV_MOVER && (e.botoes & 1) && arr_pronto) { arrastar_mover(e.x, e.y); continue; }
        if (e.tipo == EV_SOLTOU && arr_pronto) {
            bool movia = arr_movendo;
            arrastar_soltar();
            if (movia) continue;   /* sem arrastar, a lista ainda trata o soltar (o "pendente") */
        }
        if (e.tipo == EV_MOVER && !(e.botoes & 1)) mover(&e);
        if (e.tipo == EV_CLIQUE && clique_fora_da_lista(&e)) continue;
        if (e.tipo == EV_TECLA && busca_foco) { tecla_busca(e.tecla); continue; }
        if (e.tipo == EV_TECLA && e.tecla == 6) { busca_foco = true; desenhar_endereco(); continue; }   /* Ctrl+F */
        if (e.tipo == EV_TECLA && e.tecla == TECLA_F5) { carregar(); continue; }
        if (e.tipo == EV_TECLA && e.tecla == '\b') { voltar(); continue; }
        if (e.tipo == EV_TECLA && e.tecla == TECLA_DELETE) { apagar_item_modo(e.modificadores & 1); continue; }
        if (e.tipo == EV_TECLA && e.tecla == TECLA_F2) { renomear_na_linha(lista.sel); continue; }
        if (e.tipo == EV_MENU) {                            /* menu do botão direito (H81; Novo e Renomear: issue #25, parte 4) */
            registrar("arquivos: menu %u", e.tecla);
            if (e.tecla < (unsigned)qtd_acoes) {
                int a = acoes[e.tecla];
                if (a == AC_ABRIR && lista.sel >= 0) abrir_item(lista.sel);
                else if (a == AC_EDITOR && lista.sel >= 0) abrir_com(lista.sel, "/bin/editor");
                else if (a == AC_RENOMEAR) renomear_na_linha(lista.sel);
                else if (a == AC_APAGAR) apagar_item();
                else if (a == AC_PASTA) criar_novo(NOVO_PASTA);
                else if (a == AC_TEXTO) criar_novo(NOVO_TEXTO);
                else if (a == AC_PAGINA) criar_novo(NOVO_PAGINA);
                else if (a == AC_ATUALIZAR) carregar();
                else if (a == AC_VOLTAR) voltar();
                else if (a == AC_RESTAURAR) restaurar_item();
                else if (a == AC_APAGAR_VEZ) apagar_item_modo(true);
                else if (a == AC_ESVAZIAR) esvaziar_lixeira();
                else if (a >= AC_ORD_NOME && a <= AC_ORD_TAMANHO) ordenar(a - AC_ORD_NOME);
                else if (a == AC_PROPRIEDADES && lista.sel >= 0) { char c[200]; caminho_de(lista.sel, c, sizeof c); propriedades(c); }
                else if (a == AC_EXTRAIR_AQUI || a == AC_EXTRAIR_PASTA) extrair(lista.sel, a == AC_EXTRAIR_PASTA);
                else if (a == AC_ZIP || a == AC_TARGZ) compactar(a == AC_TARGZ);
                else acao_do_painel(a);
            }
            continue;
        }
        if (e.tipo == EV_RODA && e.x < LATERAL + 1 && e.y >= CORPO_Y) { rolar_painel((int)e.tecla); continue; }
        int r = lista_tratar(j, &lista, &e);
        if (lista.qtd == 0 && (e.tipo == EV_CLIQUE || e.tipo == EV_RODA)) desenhar_lista();
        if (e.tipo == EV_CLIQUE && (e.botoes & 2)) {
            desenhar_cmds();
            desenhar_status();
            menu_de_contexto(e.x, e.y);
            continue;
        }
        /* Apertou num item marcado: pode ser o começo de um arrastar (H137). */
        if (e.tipo == EV_CLIQUE && (e.botoes & 1) && e.cliques < 2 && !lista.laco && !lista.editando &&
            e.y >= lista.y && e.x >= lista.x) {
            int i = lista.topo + (e.y - lista.y - 2) / ITEM_A;
            if (i >= 0 && i < lista.qtd && marc[i] && !na_lixeira()) { arr_pronto = true; arr_movendo = false; arr_x0 = e.x; arr_y0 = e.y; }
        }
        if (r == LISTA_EDITOU) { terminar_renomear(); continue; }
        if (r == LISTA_DEVAGAR) { if (!fechou_edicao) { devagar_i = lista.sel; devagar_ms = tempo_ms(); } continue; }
        if (r == LISTA_ATIVOU) abrir_item(lista.sel);
        else if (r == LISTA_SELECIONOU) {
            status_msg[0] = 0;
            desenhar_cmds();
            desenhar_status();
            if (lista_qtd_marcados(&lista) > 1) registrar("arquivos: %d marcados", lista_qtd_marcados(&lista));
        }
    }
    jan_fechar(j);
    return 0;
}
