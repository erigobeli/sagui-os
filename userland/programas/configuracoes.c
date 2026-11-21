/* configuracoes - o app de Configurações do SaguiOS (H38).
 *
 * À esquerda, a busca e as páginas, em grupos e com ícones de cor; à
 * direita, a página: em cima o caminho ("Configurações › Som", que volta ao
 * Início com um clique) e, embaixo, as opções em cartões, cada uma numa
 * linha com o nome e a explicação à esquerda e o controle à direita
 * (interruptor, botões de escolha, volume deslizante, caixa de texto). A
 * página Início tem um cartão por área, com o estado de cada uma, e leva a
 * ela com um clique. A busca filtra as páginas pelo nome e por palavras das
 * opções de cada uma ("fuso" acha Data e hora); Enter abre a primeira.
 *
 * As escolhas vão para as configurações do sistema (config_gravar, H37),
 * guardadas no pendrive; o kernel percebe a mudança e redesenha o que
 * depender delas (o fundo da área de trabalho, por exemplo).
 *
 * Cada controle desenhado é registrado no log com o nome e o retângulo
 * ("configuracoes: controle tema.claro 412 160 70 26"): os testes clicam
 * pelo nome, sem depender da posição.
 *
 * Os textos passam por traduzir() (D073). Os nomes curtos das páginas
 * (relogio, aparencia, som...) continuam valendo no argumento
 * (configuracoes teclado) e no log. */
#include "sagui.h"
#include "gui.h"

#define L 780
#define A 540
#define LADO 220                       /* lateral */
#define CX (LADO + 24)                 /* começo do conteúdo */
#define CL (L - CX - 24)               /* largura do conteúdo */
#define TOPO_CONT 76                   /* onde os cartões começam */

#define C_FUNDO    0xF3F5F7
#define C_LADO     0xEAEDF0
#define C_CARTAO   0xFFFFFF
#define C_FILETE   0xE1E4E8
#define C_TEXTO    0x1E2328
#define C_FRACO    0x6A737C
#define C_SEL_LADO 0xD9DEE3
#define C_VERDE    0x0B7A3E
#define C_ERRO     0xC0392B

static jan_t *j;
static char recado[120];
static int recado_cor;

/* ------------------------------------------------------------ páginas */

enum { P_INICIO, P_SISTEMA, P_RELOGIO, P_APARENCIA, P_TECLADO, P_REDE, P_SOM, P_PROGRAMAS, P_IA, QTD_P };
/* Nome curto (argumento e log), título, grupo, cor do ícone, letra do ícone
 * e palavras que a busca acha. */
static const char *const p_curto[QTD_P] = { "inicio", "sobre", "relogio", "aparencia", "teclado", "rede", "som", "programas", "ia" };
static const char *const p_nome[QTD_P] = { "Início", "Sobre o computador", "Data e hora", "Aparência", "Idioma e teclado",
                                           "Rede", "Som", "Programas", "IA (Sagui Code)" };
/* Nome no log (o mesmo de antes da página Início; os testes esperam). */
static const char *const p_log[QTD_P] = { "Início", "Sobre o computador", "Relógio", "Aparência", "Teclado", "Rede", "Som", "Programas", "IA" };
static const int p_grupo[QTD_P] = { 0, 0, 0, 1, 1, 2, 2, 3, 3 };
static const char *const grupos[4] = { "", "Personalização", "Conexões e som", "Programas" };
static const uint32_t p_cor[QTD_P] = { 0x0B7A3E, 0x5A6470, 0xD9822B, 0x7B4FC9, 0x2A6FD8, 0x1E8FB8, 0xD0457A, 0x3C8C5A, 0x9A5B2E };
static const char *const p_palavras[QTD_P] = {
    "inicio resumo",
    "sobre computador processador memoria tela versao dispositivos pci",
    "data hora relogio fuso automatico internet",
    "aparencia fundo tema escuro claro sagui doca barra efeitos vidro sombra animacao",
    "idioma lingua teclado layout abnt2 espanhol ingles",
    "rede internet ip dhcp manual dns roteador mascara endereco cabo",
    "som volume audio alto falante sons sistema",
    "programas instalados remover icone esconder pendrive",
    "ia inteligencia artificial sagui code modelo chave openrouter limite",
};
static int pagina;
static char busca[32];
static bool busca_foco;

/* ------------------------------------------------------------ controles */

/* Cada controle clicável da tela: nome e retângulo. Refeito a cada desenho. */
typedef struct { char nome[32]; int x, y, l, a; } ctl_t;
static ctl_t ctls[96];
static int qtd_ctls;

static void ctl(const char *nome, int x, int y, int l, int a)
{
    if (qtd_ctls >= (int)(sizeof ctls / sizeof ctls[0])) return;
    ctl_t *c = &ctls[qtd_ctls++];
    copia_texto(c->nome, nome, sizeof c->nome);
    c->x = x; c->y = y; c->l = l; c->a = a;
    registrar("configuracoes: controle %s %d %d %d %d", nome, x, y, l, a);
}

static const char *ctl_em(int x, int y)
{
    for (int i = qtd_ctls - 1; i >= 0; i--)
        if (x >= ctls[i].x && x < ctls[i].x + ctls[i].l && y >= ctls[i].y && y < ctls[i].y + ctls[i].a) return ctls[i].nome;
    return NULL;
}

static bool comeca(const char *s, const char *p) { return compara_texto_n(s, p, tam_texto(p)) == 0; }

/* Cartão: retângulo branco de cantos redondos, com filete. */
static void cartao(int x, int y, int l, int a)
{
    g_ret_redondo(j, x, y, l, a, 10, C_FILETE, C_FILETE);
    g_ret_redondo(j, x + 1, y + 1, l - 2, a - 2, 9, C_CARTAO, C_CARTAO);
}

/* Linha de opção dentro de um cartão: nome e explicação à esquerda. */
#define LINHA_A 56
static void linha(int y, const char *nome, const char *expl, bool filete)
{
    g_texto(j, CX + 18, y + (expl ? 11 : 20), traduzir(nome), C_TEXTO);
    if (expl) g_texto(j, CX + 18, y + 30, traduzir(expl), C_FRACO);
    if (filete) g_retangulo(j, CX + 16, y + LINHA_A - 1, CL - 32, 1, C_FILETE);
}

/* Interruptor (40 x 22) encostado à direita da linha. */
static void interruptor(const char *nome, int y, bool ligado)
{
    int x = CX + CL - 18 - 44, yy = y + (LINHA_A - 22) / 2;
    uint32_t c = ligado ? C_VERDE : 0xB4BBC2;
    g_ret_redondo(j, x, yy, 44, 22, 11, c, c);
    int kx = ligado ? x + 24 : x + 3;
    g_ret_redondo(j, kx, yy + 3, 16, 16, 8, C_CARTAO, C_CARTAO);
    ctl(nome, x, yy, 44, 22);
}

/* Botões de escolha (só um vale), à direita da linha. Os nomes dos
 * controles são "<base>.<chave>". */
static void escolha(const char *base, int y, int n, const char *const *rotulos, const char *const *chaves, int sel)
{
    int w[6], total = 0;
    for (int i = 0; i < n; i++) { w[i] = g_largura_texto(traduzir(rotulos[i])) + 24; total += w[i]; }
    int x = CX + CL - 18 - total - 4, yy = y + (LINHA_A - 30) / 2;
    g_ret_redondo(j, x, yy, total + 4, 30, 8, 0xE6E9EC, 0xE6E9EC);
    for (int i = 0, xi = x + 2; i < n; xi += w[i], i++) {
        if (i == sel) {
            g_ret_redondo(j, xi, yy + 2, w[i], 26, 7, C_FILETE, C_FILETE);
            g_ret_redondo(j, xi + 1, yy + 3, w[i] - 2, 24, 6, C_CARTAO, C_CARTAO);
        }
        g_texto(j, xi + 12, yy + 7, traduzir(rotulos[i]), i == sel ? C_TEXTO : C_FRACO);
        char nome[32];
        formatar_texto(nome, sizeof nome, "%s.%s", base, chaves[i]);
        ctl(nome, xi, yy + 2, w[i], 26);
    }
}

/* Botão de cantos redondos: o principal é verde. */
static void botao(const char *nome, int x, int y, const char *texto, bool principal)
{
    int l = g_largura_texto(traduzir(texto)) + 28;
    if (principal) g_ret_redondo(j, x, y, l, 30, 7, C_VERDE, C_VERDE);
    else {
        g_ret_redondo(j, x, y, l, 30, 7, C_FILETE, C_FILETE);
        g_ret_redondo(j, x + 1, y + 1, l - 2, 28, 6, C_CARTAO, C_CARTAO);
    }
    g_texto(j, x + 14, y + 7, traduzir(texto), principal ? COR_BRANCO : C_TEXTO);
    ctl(nome, x, y, l, 30);
}
static int largura_botao(const char *texto) { return g_largura_texto(traduzir(texto)) + 28; }

/* Caixa de texto com nome (os testes acham pelo nome). */
static void caixa(const char *nome, caixa_t *c, int x, int y, int l)
{
    c->x = x; c->y = y; c->l = l;
    caixa_desenhar(j, c);
    ctl(nome, x, y, l, LETRA_A + 8);
}

/* Ícones das páginas: desenhos de 16 x 16 em branco ('#') sobre a cor. */
static const char *const ic_casa[16] = {
    "................",
    "................",
    "................",
    ".......##.......",
    ".....######.....",
    "...##########...",
    "..############..",
    "....########....",
    "....########....",
    "....###..###....",
    "....###..###....",
    "....###..###....",
    "....###..###....",
    "................",
    "................",
    "................",
};
static const char *const ic_info[16] = {
    "................",
    "....########....",
    "...##......##...",
    "..#....##....#..",
    "..#....##....#..",
    ".#............#.",
    ".#....###.....#.",
    ".#.....##.....#.",
    ".#.....##.....#.",
    ".#.....##.....#.",
    "..#...####...#..",
    "..#..........#..",
    "...##......##...",
    "....########....",
    "................",
    "................",
};
static const char *const ic_relogio[16] = {
    "................",
    "....########....",
    "...##......##...",
    "..#....##....#..",
    "..#....##....#..",
    ".#.....##.....#.",
    ".#.....##.....#.",
    ".#.....#####..#.",
    ".#............#.",
    ".#............#.",
    "..#..........#..",
    "..#..........#..",
    "...##......##...",
    "....########....",
    "................",
    "................",
};
static const char *const ic_paleta[16] = {
    "................",
    "....#######.....",
    "..###########...",
    ".####.###.####..",
    ".#############..",
    "####.##########.",
    "################",
    "#######...######",
    "######.....#####",
    "#######...######",
    ".##############.",
    ".#####.#######..",
    "..###...####....",
    "................",
    "................",
    "................",
};
static const char *const ic_teclado[16] = {
    "................",
    "................",
    "................",
    "################",
    "#..............#",
    "#.##.##.##.##..#",
    "#..............#",
    "#.##.##.##.##..#",
    "#..............#",
    "#...########...#",
    "#..............#",
    "################",
    "................",
    "................",
    "................",
    "................",
};
static const char *const ic_globo[16] = {
    "................",
    "....########....",
    "...##..##..##...",
    "..#...#..#...#..",
    ".#...#....#...#.",
    ".##############.",
    ".#..#......#..#.",
    ".#..#......#..#.",
    ".##############.",
    ".#...#....#...#.",
    "..#...#..#...#..",
    "...##..##..##...",
    "....########....",
    "................",
    "................",
    "................",
};
static const char *const ic_som[16] = {
    "................",
    "................",
    ".......#........",
    "......##...#....",
    ".....###....#...",
    "#####.##..#..#..",
    "#####.##...#.#..",
    "#####.##...#.#..",
    "#####.##...#.#..",
    "#####.##..#..#..",
    ".....###....#...",
    "......##...#....",
    ".......#........",
    "................",
    "................",
    "................",
};
static const char *const ic_apps[16] = {
    "................",
    "................",
    "...####..####...",
    "...####..####...",
    "...####..####...",
    "...####..####...",
    "................",
    "................",
    "...####..####...",
    "...####..####...",
    "...####..####...",
    "...####..####...",
    "................",
    "................",
    "................",
    "................",
};
static const char *const ic_ia[16] = {
    "................",
    ".......#........",
    ".......#........",
    "......###.......",
    "......###.......",
    "....#######.....",
    "..###########...",
    "....#######.....",
    "......###.......",
    "......###....#..",
    ".......#....###.",
    ".......#.....#..",
    "................",
    "................",
    "................",
    "................",
};
static const char *const *const p_ic[QTD_P] = { ic_casa, ic_info, ic_relogio, ic_paleta, ic_teclado, ic_globo, ic_som, ic_apps, ic_ia };
static uint32_t paleta_branca(char c) { return c == '#' ? COR_BRANCO : 0xFF000000; }

/* Ícone de uma página: quadrado redondo de cor com o desenho em branco
 * (em dobro a partir de 32 pixels). */
static void icone(int p, int x, int y, int tam)
{
    g_ret_redondo(j, x, y, tam, tam, tam / 4, p_cor[p], p_cor[p]);
    int esc = tam >= 32 ? 2 : 1, d = 16 * esc;
    g_desenho(j, x + (tam - d) / 2, y + (tam - d) / 2, p_ic[p], 16, 16, paleta_branca, esc);
}

/* ------------------------------------------------------------ recado */

static void mostrar_mensagem(const char *fmt, int r)
{
    /* r: 1 guardado no pendrive, 0 só até desligar (sem aviso), <0 erro */
    if (r == 1) formatar_texto(recado, sizeof recado, "%s %s", traduzir(fmt), traduzir("Guardado no pendrive."));
    else formatar_texto(recado, sizeof recado, "%s", traduzir(fmt));
    recado_cor = r < 0 && r != -100 ? 1 : 0;
    int y = A - 34;
    g_retangulo(j, CX, y, CL, 24, C_FUNDO);
    if (recado[0]) {
        int l = g_largura_texto(recado) + 40;
        uint32_t f = recado_cor ? 0xFBE3E1 : 0xDFF1E6, c = recado_cor ? C_ERRO : C_VERDE;
        g_ret_redondo(j, CX, y, l, 24, 12, f, f);
        g_ret_redondo(j, CX + 10, y + 8, 8, 8, 4, c, c);
        g_texto(j, CX + 26, y + 4, recado, c);
    }
    jan_mostrar_area(j, CX, y, CL, 24);
}

/* ------------------------------------------------------------ Data e hora */

static caixa_t c_dia, c_mes, c_ano, c_hora, c_min, c_fuso;
static int y_agora;

static void numero_em(caixa_t *c, int v, int digitos)
{
    char t[8];
    formatar_texto(t, sizeof t, digitos == 4 ? "%04d" : "%02d", v);
    caixa_definir(c, t);
}

static void relogio_agora(void)
{
    data_t d;
    ler_data(&d);
    char t[48];
    formatar_texto(t, sizeof t, "%02u/%02u/%u  %02u:%02u:%02u", d.dia, d.mes, d.ano, d.hora, d.minuto, d.segundo);
    int x = CX + CL - 18 - 20 * LETRA_L;
    g_retangulo(j, x, y_agora + 20, 20 * LETRA_L, LETRA_A, C_CARTAO);
    g_texto(j, CX + CL - 18 - g_largura_texto(t), y_agora + 20, t, C_TEXTO);
    jan_mostrar_area(j, x, y_agora + 20, 20 * LETRA_L, LETRA_A);
}

static bool automatico_ligado(void)
{
    char aut[8];
    return config_ler("relogio.automatico", aut, sizeof aut) >= 0 && compara_texto(aut, "sim") == 0;
}

static void relogio_desenhar(void)
{
    int y = TOPO_CONT;
    cartao(CX, y, CL, 3 * LINHA_A);
    y_agora = y;
    linha(y, "Agora", NULL, true);
    relogio_agora();
    y += LINHA_A;
    linha(y, "Acertar sozinho pela rede", "Quando a rede liga, pergunta a hora a um servidor.", true);
    interruptor("relogio.automatico", y, automatico_ligado());
    y += LINHA_A;
    linha(y, "Acertar agora pela internet", NULL, false);
    botao("relogio.internet", CX + CL - 18 - largura_botao("Acertar agora"), y + 13, "Acertar agora", false);
    y += LINHA_A + 16;

    cartao(CX, y, CL, 96);
    g_texto(j, CX + 18, y + 12, traduzir("Acertar à mão"), C_TEXTO);
    g_texto(j, CX + 18, y + 34, traduzir("Dia    Mês    Ano        Hora   Minuto"), C_FRACO);
    data_t d;
    ler_data(&d);
    int yc = y + 54;
    numero_em(&c_dia, d.dia, 2); numero_em(&c_mes, d.mes, 2); numero_em(&c_ano, d.ano, 4);
    numero_em(&c_hora, d.hora, 2); numero_em(&c_min, d.minuto, 2);
    caixa("relogio.dia", &c_dia, CX + 18, yc, 40);
    caixa("relogio.mes", &c_mes, CX + 74, yc, 40);
    caixa("relogio.ano", &c_ano, CX + 130, yc, 56);
    caixa("relogio.hora", &c_hora, CX + 218, yc, 40);
    caixa("relogio.minuto", &c_min, CX + 274, yc, 40);
    botao("relogio.aplicar", CX + CL - 18 - largura_botao("Acertar o relógio"), yc - 3, "Acertar o relógio", true);
    y += 96 + 16;

    cartao(CX, y, CL, LINHA_A + 8);
    linha(y, "Fuso horário", "Horas de UTC (Brasília: -3).", false);
    char fuso[16];
    if (config_ler("relogio.fuso", fuso, sizeof fuso) < 0) copia_texto(fuso, "-3", sizeof fuso);
    caixa_definir(&c_fuso, fuso);
    int bx = CX + CL - 18 - largura_botao("Salvar fuso");
    caixa("relogio.fuso", &c_fuso, bx - 70, y + 16, 60);
    botao("relogio.salvar_fuso", bx, y + 13, "Salvar fuso", false);
}

static void redesenhar(void);

static void relogio_clique(const char *n)
{
    if (!compara_texto(n, "relogio.aplicar")) {
        data_t d = { 0 };
        d.dia = (unsigned char)texto_para_int(c_dia.texto);
        d.mes = (unsigned char)texto_para_int(c_mes.texto);
        d.ano = (unsigned short)texto_para_int(c_ano.texto);
        d.hora = (unsigned char)texto_para_int(c_hora.texto);
        d.minuto = (unsigned char)texto_para_int(c_min.texto);
        int r = ajustar_data(&d);
        registrar("configuracoes: acertar relogio -> %d", r);
        mostrar_mensagem(r < 0 ? "Data ou hora inválida." : "Relógio acertado.", r < 0 ? -1 : -100);
        relogio_agora();
    } else if (!compara_texto(n, "relogio.automatico")) {
        bool a = automatico_ligado();
        int r = config_gravar("relogio.automatico", a ? "nao" : "sim");
        registrar("configuracoes: relogio automatico %s", a ? "desligado" : "ligado");
        redesenhar();
        mostrar_mensagem(a ? "Relógio automático desligado." : "Vai acertar sozinho quando a rede ligar.", r);
    } else if (!compara_texto(n, "relogio.internet")) {
        char *argv[] = { "relogio", "internet", NULL };
        int pid = executar("/bin/relogio", argv);
        int codigo = 1;
        if (pid > 0) esperar(pid, &codigo);
        registrar("configuracoes: acertar pela internet -> %d", codigo);
        mostrar_mensagem(codigo == 0 ? "Relógio acertado pela internet." : "Não consegui falar com o servidor de hora.", codigo == 0 ? -100 : -1);
        relogio_agora();
    } else if (!compara_texto(n, "relogio.salvar_fuso")) {
        int f = texto_para_int(c_fuso.texto);
        if (f < -12 || f > 14) { mostrar_mensagem("Fuso inválido (de -12 a 14).", -1); return; }
        char t[8];
        formatar_texto(t, sizeof t, "%d", f);
        int r = config_gravar("relogio.fuso", t);
        registrar("configuracoes: fuso %s -> %d", t, r);
        mostrar_mensagem("Fuso salvo.", r);
    }
}

/* ------------------------------------------------------------ Aparência */

#define QTD_FUNDOS 7
/* "sol" e "mata-atlantica" vieram do pull request #1 (Robson): o Brasil com
 * sol é desenhado pelo kernel; a Mata Atlântica é uma imagem feita por IA. */
static const char *const fundos[QTD_FUNDOS] = { "brasil", "sol", "mata-atlantica", "mata", "ceu", "noite", "terra" };
static const char *const nomes_fundos[QTD_FUNDOS] = { "Brasil", "Com sol", "M. Atlântica", "Mata", "Céu", "Noite", "Terra" };
static const uint32_t amostras[QTD_FUNDOS][3] = {
    { 0x0B7A3E, 0xE0B92A, 0x14337E }, { 0x2E8A4A, 0xFFE89A, 0x1C5AA8 }, { 0xE8D49A, 0x5E8A4A, 0x3A7AB8 },
    { 0x0E3B22, 0x2E7D32, 0x0A2416 }, { 0x1E5AA8, 0x6FB3E8, 0xD8EEF8 },
    { 0x0B1026, 0x2B1E5A, 0x060814 }, { 0x7A3E14, 0xD98A3A, 0x3A1E0C },
};
#define QTD_TEMAS 4
static const char *const temas[QTD_TEMAS] = { "escuro", "claro", "mata", "azul" };
static const char *const nomes_temas[QTD_TEMAS] = { "Escuro", "Claro", "Mata", "Azul" };

static int config_indice(const char *chave, const char *const *valores, int n, int padrao)
{
    char v[16];
    if (config_ler(chave, v, sizeof v) < 0) return padrao;
    for (int i = 0; i < n; i++) if (compara_texto(v, valores[i]) == 0) return i;
    return padrao;
}

static void aparencia_desenhar(void)
{
    int y = TOPO_CONT;
    char atual[16];
    if (config_ler("aparencia.fundo", atual, sizeof atual) < 0) copia_texto(atual, "brasil", sizeof atual);
    cartao(CX, y, CL, 170);
    g_texto(j, CX + 18, y + 12, traduzir("Fundo da área de trabalho"), C_TEXTO);
    int tw = (CL - 36 - 3 * 12) / 4;
    for (int i = 0; i < QTD_FUNDOS; i++) {
        int x = CX + 18 + (i % 4) * (tw + 12), yy = y + 38 + (i / 4) * 64;
        bool esc = compara_texto(atual, fundos[i]) == 0;
        if (esc) g_ret_redondo(j, x - 3, yy - 3, tw + 6, 44, 9, COR_SELECAO, COR_SELECAO);
        g_ret_redondo(j, x, yy, tw, 38, 7, amostras[i][0], amostras[i][0]);
        g_retangulo(j, x + 6, yy + 13, tw - 12, 12, amostras[i][1]);
        g_retangulo(j, x + 6, yy + 25, tw - 12, 6, amostras[i][2]);
        g_texto(j, x, yy + 42, traduzir(nomes_fundos[i]), esc ? C_TEXTO : C_FRACO);
        char nome[32];
        formatar_texto(nome, sizeof nome, "fundo.%s", fundos[i]);
        ctl(nome, x, yy, tw, 58);
    }
    y += 170 + 16;

    cartao(CX, y, CL, 4 * LINHA_A);
    linha(y, "Tema", "Cores das janelas e barras.", true);
    escolha("tema", y, QTD_TEMAS, nomes_temas, temas, config_indice("aparencia.tema", temas, QTD_TEMAS, 0));
    y += LINHA_A;
    char v[12];
    bool mostra = config_ler("aparencia.sagui", v, sizeof v) < 0 || compara_texto(v, "nao") != 0;
    linha(y, "Mostrar o sagui", "O sagui no canto da área de trabalho.", true);
    interruptor("aparencia.sagui", y, mostra);
    y += LINHA_A;
    static const char *const barras[2] = { "doca", "tarefas" }, *const nomes_barras[2] = { "Doca", "Barra em cima" };
    linha(y, "Janelas abertas", "Na doca ou na barra de cima.", true);
    escolha("barra", y, 2, nomes_barras, barras, config_indice("aparencia.barra", barras, 2, 0));
    y += LINHA_A;
    static const char *const efeitos[3] = { "auto", "completos", "leves" }, *const nomes_efeitos[3] = { "Automático", "Completos", "Leves" };
    linha(y, "Efeitos", "Vidro, sombra, animação.", false);
    escolha("efeitos", y, 3, nomes_efeitos, efeitos, config_indice("aparencia.efeitos", efeitos, 3, 0));
}

static void aparencia_clique(const char *n)
{
    if (comeca(n, "fundo.")) {
        int r = config_gravar("aparencia.fundo", n + 6);
        registrar("configuracoes: fundo %s -> %d", n + 6, r);
        redesenhar();
        mostrar_mensagem("Fundo trocado.", r);
    } else if (comeca(n, "tema.")) {
        int r = config_gravar("aparencia.tema", n + 5);
        registrar("configuracoes: tema %s", n + 5);
        redesenhar();
        mostrar_mensagem("Tema trocado.", r);
    } else if (comeca(n, "barra.")) {
        int r = config_gravar("aparencia.barra", n + 6);
        registrar("configuracoes: barra %s", n + 6);
        redesenhar();
        mostrar_mensagem(compara_texto(n + 6, "doca") == 0 ? "A doca voltou." : "Janelas na barra de cima.", r);
    } else if (comeca(n, "efeitos.")) {
        int r = config_gravar("aparencia.efeitos", n + 8);
        registrar("configuracoes: efeitos %s", n + 8);
        redesenhar();
        mostrar_mensagem("Aparência trocada.", r);
    } else if (!compara_texto(n, "aparencia.sagui")) {
        char v[8];
        bool mostra = config_ler("aparencia.sagui", v, sizeof v) < 0 || compara_texto(v, "nao") != 0;
        int r = config_gravar("aparencia.sagui", mostra ? "nao" : "sim");
        registrar("configuracoes: sagui %s -> %d", mostra ? "nao" : "sim", r);
        redesenhar();
        mostrar_mensagem(mostra ? "O sagui foi descansar." : "O sagui voltou.", r);
    }
}

/* ------------------------------------------------------------ Idioma e teclado (H90, H124) */

/* O layout é a configuração "teclado.layout"; o driver do teclado percebe a
 * mudança sozinho na próxima tecla. O idioma é "sistema.idioma" (D073):
 * vale para os programas abertos depois e, já, para esta janela. */
#define QTD_IDIOMAS 3
static const char *const idiomas[QTD_IDIOMAS] = { "pt", "es", "en" };
/* Cada língua com o próprio nome, para quem não lê a língua atual achar a sua. */
static const char *const nomes_idiomas[QTD_IDIOMAS] = { "Português", "Español", "English" };

static bool teclado_es(void)
{
    char v[16];
    return config_ler("teclado.layout", v, sizeof v) >= 0 && compara_texto(v, "es") == 0;
}

static void teclado_desenhar(void)
{
    int y = TOPO_CONT;
    cartao(CX, y, CL, LINHA_A);
    linha(y, "Idioma do sistema", "Vale para os programas abertos depois.", false);
    int sel = 0;
    for (int i = 0; i < QTD_IDIOMAS; i++) if (compara_texto(idioma(), idiomas[i]) == 0) sel = i;
    escolha("idioma", y, QTD_IDIOMAS, nomes_idiomas, idiomas, sel);
    y += LINHA_A + 16;
    bool es = teclado_es();
    static const char *const lay[2] = { "abnt2", "es" }, *const nomes_lay[2] = { "ABNT2", "Espanhol" };
    cartao(CX, y, CL, LINHA_A + 70);
    linha(y, "Layout do teclado", NULL, true);
    escolha("teclado", y, 2, nomes_lay, lay, es ? 1 : 0);
    y += LINHA_A;
    g_texto(j, CX + 18, y + 12, traduzir(es ? "ñ à direita do L; ´ ¨ ` ^ são teclas mortas;" : "ç à direita do L; ´ ` ~ ^ ¨ são teclas mortas;"), C_FRACO);
    g_texto(j, CX + 18, y + 30, traduzir(es ? "AltGr+2 = @, AltGr+4 = ~ (morta)." : "AltGr+Q = /, AltGr+W = ?."), C_FRACO);
    g_texto(j, CX + 18, y + 48, traduzir("No Terminal: teclado [es | abnt2]"), C_FRACO);
}

static void desenhar_tudo(void);

static void teclado_clique(const char *n)
{
    if (comeca(n, "idioma.")) {
        int r = config_gravar("sistema.idioma", n + 7);
        registrar("configuracoes: idioma %s", n + 7);
        idioma_recarregar();
        desenhar_tudo();
        mostrar_mensagem("Idioma trocado.", r);
    } else if (comeca(n, "teclado.")) {
        bool es = compara_texto(n + 8, "es") == 0;
        int r = config_gravar("teclado.layout", es ? "es" : "abnt2");
        registrar("configuracoes: teclado %s", es ? "es" : "abnt2");
        redesenhar();
        mostrar_mensagem(es ? "Teclado espanhol." : "Teclado ABNT2.", r);
    }
}

/* ------------------------------------------------------------ Rede (H47) */

static caixa_t c_ip, c_masc, c_rot, c_dns;
static bool rede_manual;
static int y_estado_rede;

static void rede_estado_desenhar(void)
{
    info_rede_t r;
    char t[64], ip[16];
    int y = y_estado_rede;
    g_retangulo(j, CX + 70, y + 8, CL - 90, 44, C_CARTAO);
    if (rede_info(&r) < 0) {
        g_texto(j, CX + 70, y + 12, traduzir("Nenhuma placa de rede com driver."), C_TEXTO);
    } else {
        g_texto_n(j, CX + 70, y + 12, r.placa, (CL - 100) / LETRA_L, C_TEXTO);
        ip_para_texto(r.ip, ip);
        if (!r.cabo) copia_texto(t, traduzir("Cabo desligado."), sizeof t);
        else if (!r.configurado) copia_texto(t, traduzir("Pedindo endereço ao roteador..."), sizeof t);
        else formatar_texto(t, sizeof t, traduzir("Conectado: %s"), ip);
        uint32_t c = r.configurado ? C_VERDE : C_FRACO;
        g_ret_redondo(j, CX + 70, y + 36, 8, 8, 4, c, c);
        g_texto(j, CX + 84, y + 32, t, c);
    }
    jan_mostrar_area(j, CX + 70, y + 8, CL - 90, 44);
}

static void caixa_rede(const char *nome, caixa_t *c, int y, const char *rotulo, const char *chave, unsigned padrao)
{
    g_texto(j, CX + 18, y + 4, traduzir(rotulo), rede_manual ? C_TEXTO : C_FRACO);
    char v[24];
    if (config_ler(chave, v, sizeof v) < 0) ip_para_texto(padrao, v);
    caixa_definir(c, v);
    caixa(nome, c, CX + 140, y, 170);
}

static void rede_desenhar(void)
{
    int y = TOPO_CONT;
    cartao(CX, y, CL, 60);
    icone(P_REDE, CX + 18, y + 12, 36);
    y_estado_rede = y;
    rede_estado_desenhar();
    y += 60 + 16;
    char modo[12];
    rede_manual = config_ler("rede.modo", modo, sizeof modo) >= 0 && compara_texto(modo, "manual") == 0;
    cartao(CX, y, CL, LINHA_A + 4 * 32 + 60);
    static const char *const modos[2] = { "dhcp", "manual" }, *const nomes_modos[2] = { "Automático (DHCP)", "Manual" };
    linha(y, "Endereço do computador", NULL, true);
    escolha("rede.modo", y, 2, nomes_modos, modos, rede_manual ? 1 : 0);
    y += LINHA_A + 12;
    info_rede_t r;
    if (rede_info(&r) < 0) memset(&r, 0, sizeof r);
    caixa_rede("rede.ip", &c_ip, y, "Endereço:", "rede.ip", r.ip);
    caixa_rede("rede.mascara", &c_masc, y + 32, "Máscara:", "rede.mascara", r.mascara);
    caixa_rede("rede.roteador", &c_rot, y + 64, "Roteador:", "rede.roteador", r.roteador);
    caixa_rede("rede.dns", &c_dns, y + 96, "DNS:", "rede.dns", r.dns);
    if (rede_manual) botao("rede.aplicar", CX + CL - 18 - largura_botao("Aplicar"), y + 136, "Aplicar", true);
    else g_texto(j, CX + 18, y + 140, traduzir("No automático, o roteador escolhe estes números."), C_FRACO);
}

static void rede_clique(const char *n)
{
    if (comeca(n, "rede.modo.")) {
        bool manual = compara_texto(n + 10, "manual") == 0;
        if (manual == rede_manual) return;
        int r = config_gravar("rede.modo", manual ? "manual" : "dhcp");
        registrar("configuracoes: rede %s", manual ? "manual" : "automatica");
        redesenhar();
        mostrar_mensagem(manual ? "Modo manual: preencha e aplique." : "Endereço automático (DHCP).", r);
    } else if (!compara_texto(n, "rede.aplicar") && rede_manual) {
        caixa_t *cs[] = { &c_ip, &c_masc, &c_rot, &c_dns };
        static const char *const chaves[] = { "rede.ip", "rede.mascara", "rede.roteador", "rede.dns" };
        for (int i = 0; i < 4; i++)
            if (!texto_para_ip(cs[i]->texto)) { mostrar_mensagem("Endereço inválido (use 192.168.0.10).", -1); return; }
        int r = 0;
        for (int i = 0; i < 4; i++) r = config_gravar(chaves[i], cs[i]->texto);
        config_gravar("rede.modo", "manual");   /* reaplica com os valores novos */
        registrar("configuracoes: rede manual %s", c_ip.texto);
        mostrar_mensagem("Rede configurada.", r);
    }
}

/* ------------------------------------------------------------ Som (H41) */

static int vol_x, vol_l, vol_y;

static void volume_desenhar(int vol)
{
    g_retangulo(j, vol_x - 10, vol_y, vol_l + 20 + 48, 24, C_CARTAO);
    g_ret_redondo(j, vol_x, vol_y + 10, vol_l, 4, 2, 0xD5DAE0, 0xD5DAE0);
    g_ret_redondo(j, vol_x, vol_y + 10, vol_l * vol / 100 < 4 ? 4 : vol_l * vol / 100, 4, 2, C_VERDE, C_VERDE);
    int kx = vol_x + vol_l * vol / 100 - 9;
    g_ret_redondo(j, kx, vol_y + 3, 18, 18, 9, C_FILETE, C_FILETE);
    g_ret_redondo(j, kx + 1, vol_y + 4, 16, 16, 8, C_CARTAO, C_CARTAO);
    char t[8];
    formatar_texto(t, sizeof t, "%d%%", vol);
    g_texto(j, vol_x + vol_l + 16, vol_y + 4, t, C_TEXTO);
    jan_mostrar_area(j, vol_x - 10, vol_y, vol_l + 20 + 48, 24);
}

static void som_desenhar(void)
{
    int y = TOPO_CONT;
    int vol = som_volume(-1);
    if (vol < 0) {
        cartao(CX, y, CL, LINHA_A + 8);
        linha(y, "Nenhuma placa de som HD Audio encontrada.", "Os sons saem pelo alto-falante do PC (bip).", false);
        return;
    }
    cartao(CX, y, CL, 3 * LINHA_A);
    linha(y, "Volume", NULL, true);
    vol_l = 220; vol_x = CX + CL - 18 - 48 - vol_l - 40; vol_y = y + 16;
    botao("som.menos", vol_x - 40, y + 13, "-", false);
    volume_desenhar(vol);
    ctl("som.volume", vol_x - 8, vol_y, vol_l + 16, 24);
    botao("som.mais", vol_x + vol_l + 56, y + 13, "+", false);
    y += LINHA_A;
    char sis[8];
    bool ligados = config_ler("som.sistema", sis, sizeof sis) < 0 || compara_texto(sis, "nao") != 0;
    linha(y, "Sons do sistema", "No início e nos avisos.", true);
    interruptor("som.sistema", y, ligados);
    y += LINHA_A;
    linha(y, "Testar o som", NULL, false);
    botao("som.testar", CX + CL - 18 - largura_botao("Tocar"), y + 13, "Tocar", false);
}

static void volume_para(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    vol = som_volume(vol);
    registrar("configuracoes: volume %d", vol);
    static const nota_t clique[] = { { 880, 40 } };
    tocar(clique, 1, BIP_SUBSTITUIR);
    volume_desenhar(vol);
    char p[8];
    config_ler("sistema.persistente", p, sizeof p);
    mostrar_mensagem("Volume ajustado.", compara_texto(p, "sim") == 0 ? 1 : 0);
}

static void som_clique(const char *n, int x)
{
    int vol = som_volume(-1);
    if (vol < 0) return;
    if (!compara_texto(n, "som.menos")) volume_para(vol - 10);
    else if (!compara_texto(n, "som.mais")) volume_para(vol + 10);
    else if (!compara_texto(n, "som.volume")) volume_para((x - vol_x) * 100 / vol_l);
    else if (!compara_texto(n, "som.sistema")) {
        char sis[8];
        bool ligados = config_ler("som.sistema", sis, sizeof sis) < 0 || compara_texto(sis, "nao") != 0;
        int r = config_gravar("som.sistema", ligados ? "nao" : "sim");
        registrar("configuracoes: sons do sistema %s", ligados ? "desligados" : "ligados");
        redesenhar();
        mostrar_mensagem(ligados ? "Sons do sistema desligados." : "Sons do sistema ligados.", r);
    } else if (!compara_texto(n, "som.testar")) {
        static const nota_t melodia[] = { { 523, 120 }, { 659, 120 }, { 784, 120 }, { 1047, 300 } };
        tocar(melodia, 4, BIP_SUBSTITUIR);
        registrar("configuracoes: testar o som");
    }
}

/* ------------------------------------------------------------ Programas (H39) */

#define MAX_INST 6
static char inst[MAX_INST][NOME_MAX];
static int qtd_inst;
/* Programas do sistema que podem ter o ícone escondido (os de /bin com ícone). */
#define QTD_SIST 7
static const char *const sist[QTD_SIST] = { "arquivos", "terminal", "editor", "calculadora", "banana", "sagui3d", "sobre-o-sistema" };
static const char *const nomes_sist[QTD_SIST] = { "Arquivos", "Terminal", "Editor", "Calculadora", "Banana", "Sagui 3D", "Sobre" };

static bool esta_oculto(const char *nome)
{
    char lista[96];
    if (config_ler("programas.ocultos", lista, sizeof lista) < 0) return false;
    size_t n = tam_texto(nome);
    for (const char *p = lista; *p;) {
        while (*p == ',' || *p == ' ') p++;
        const char *fim = p;
        while (*fim && *fim != ',') fim++;
        if ((size_t)(fim - p) == n && compara_texto_n(p, nome, n) == 0) return true;
        p = fim;
    }
    return false;
}

static void programas_desenhar(void)
{
    int y = TOPO_CONT;
    qtd_inst = 0;
    entrada_pasta_t e;
    for (int i = 0; qtd_inst < MAX_INST && listar("/usb/programas", i, &e) == 1; i++)
        if (e.tipo != TIPO_PASTA) copia_texto(inst[qtd_inst++], e.nome, NOME_MAX);
    int ai = 40 + (qtd_inst ? qtd_inst : 1) * 40;
    cartao(CX, y, CL, ai);
    g_texto(j, CX + 18, y + 12, traduzir("Instalados no pendrive (/usb/programas)"), C_TEXTO);
    int yy = y + 40;
    if (qtd_inst == 0) g_texto(j, CX + 18, yy + 8, traduzir("Nenhum. Copie um programa para lá."), C_FRACO);
    for (int i = 0; i < qtd_inst; i++, yy += 40) {
        g_retangulo(j, CX + 16, yy, CL - 32, 1, C_FILETE);
        g_texto(j, CX + 18, yy + 12, inst[i], C_TEXTO);
        char nome[40];
        formatar_texto(nome, sizeof nome, "remover.%d", i);
        botao(nome, CX + CL - 18 - largura_botao("Remover"), yy + 5, "Remover", false);
    }
    y += ai + 16;
    int linhas = (QTD_SIST + 1) / 2;
    cartao(CX, y, CL, 40 + linhas * 40);
    g_texto(j, CX + 18, y + 12, traduzir("Ícones do sistema na área de trabalho"), C_TEXTO);
    for (int i = 0; i < QTD_SIST; i++) {
        int col = i % 2, x = CX + 18 + col * (CL / 2), ly = y + 40 + (i / 2) * 40;
        g_texto(j, x, ly + 10, traduzir(nomes_sist[i]), C_TEXTO);
        bool on = !esta_oculto(sist[i]);
        int sx = x + CL / 2 - 36 - 44;
        uint32_t c = on ? C_VERDE : 0xB4BBC2;
        g_ret_redondo(j, sx, ly + 7, 44, 22, 11, c, c);
        g_ret_redondo(j, on ? sx + 24 : sx + 3, ly + 10, 16, 16, 8, C_CARTAO, C_CARTAO);
        char nome[40];
        formatar_texto(nome, sizeof nome, "icone.%s", sist[i]);
        ctl(nome, sx, ly + 7, 44, 22);
    }
}

static void programas_clique(const char *n)
{
    if (comeca(n, "remover.")) {
        int i = texto_para_int(n + 8);
        if (i < 0 || i >= qtd_inst) return;
        char pergunta[96], caminho[NOME_MAX + 20];
        formatar_texto(pergunta, sizeof pergunta, traduzir("Remover o programa %s do pendrive?"), inst[i]);
        if (!confirmar(traduzir("Remover programa"), pergunta)) { registrar("configuracoes: remover %s cancelado", inst[i]); return; }
        formatar_texto(caminho, sizeof caminho, "/usb/programas/%s", inst[i]);
        int r = apagar(caminho);
        registrar("configuracoes: removido %s -> %d", inst[i], r);
        redesenhar();
        mostrar_mensagem(r < 0 ? "Não consegui remover." : "Programa removido.", r < 0 ? -1 : -100);
    } else if (comeca(n, "icone.")) {
        int i = -1;
        for (int k = 0; k < QTD_SIST; k++) if (compara_texto(n + 6, sist[k]) == 0) i = k;
        if (i < 0) return;
        /* Monta a lista nova: todos os ocultos, trocando o estado deste. */
        char lista[96] = "";
        for (int k = 0; k < QTD_SIST; k++) {
            bool o = esta_oculto(sist[k]);
            if (k == i) o = !o;
            if (!o) continue;
            size_t t = tam_texto(lista);
            formatar_texto(lista + t, sizeof lista - t, t ? ",%s" : "%s", sist[k]);
        }
        int r = config_gravar("programas.ocultos", lista);
        registrar("configuracoes: ocultos = %s -> %d", lista, r);
        redesenhar();
        mostrar_mensagem(esta_oculto(sist[i]) ? "Ícone escondido." : "Ícone de volta.", r);
    }
}

/* ------------------------------------------------------------ Sobre o computador */

/* Nome do processador pela instrução CPUID (que os programas podem usar). */
static void nome_processador(char *t, int max)
{
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u));
    if (a < 0x80000004u) { copia_texto(t, "processador x86", (size_t)max); return; }
    char nome[49];
    unsigned *p = (unsigned *)nome;
    for (unsigned f = 0; f < 3; f++) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000002u + f));
        p[f * 4] = a; p[f * 4 + 1] = b; p[f * 4 + 2] = c; p[f * 4 + 3] = d;
    }
    nome[48] = 0;
    const char *s = nome;
    while (*s == ' ') s++;
    copia_texto(t, s, (size_t)max);
}

static void par(int y, const char *nome, const char *valor, bool filete)
{
    g_texto(j, CX + 18, y + 12, traduzir(nome), C_FRACO);
    g_texto_n(j, CX + 170, y + 12, valor, (CL - 190) / LETRA_L, C_TEXTO);
    if (filete) g_retangulo(j, CX + 16, y + 39, CL - 32, 1, C_FILETE);
}

static void sobre_desenhar(void)
{
    int y = TOPO_CONT;
    char t[96], cpu[49];
    info_versao_t v;
    info_memoria_t m;
    info_sistema(INFO_VERSAO, &v, sizeof v);
    info_sistema(INFO_MEMORIA, &m, sizeof m);
    nome_processador(cpu, sizeof cpu);
    registrar("configuracoes: processador %s, memoria %u MB", cpu, m.ram_total_kb / 1024);
    cartao(CX, y, CL, 64);
    icone(P_INICIO, CX + 18, y + 14, 36);
    formatar_texto(t, sizeof t, "SaguiOS %s", v.versao);
    g_texto(j, CX + 70, y + 14, t, C_TEXTO);
    g_texto(j, CX + 71, y + 14, t, C_TEXTO);
    g_texto(j, CX + 70, y + 34, traduzir("Um sistema operacional escrito do zero."), C_FRACO);
    y += 64 + 16;
    cartao(CX, y, CL, 4 * 40);
    par(y, "Processador", cpu, true);
    formatar_texto(t, sizeof t, traduzir("%u MB no total, %u MB livres"), m.ram_total_kb / 1024, m.livre_kb / 1024);
    par(y + 40, "Memória", t, true);
    formatar_texto(t, sizeof t, "%ux%u", v.largura, v.altura);
    par(y + 80, "Tela", t, true);
    formatar_texto(t, sizeof t, "%u min", v.segundos_ligado / 60);
    par(y + 120, "Ligado há", t, false);
    y += 160 + 16;
    int ad = A - 48 - y;
    cartao(CX, y, CL, ad);
    g_texto(j, CX + 18, y + 12, traduzir("Dispositivos (comando pci para detalhes)"), C_TEXTO);
    static char pci[8192];
    int n = info_sistema(INFO_PCI, pci, sizeof pci - 1);
    if (n < 0) n = 0;
    pci[n] = 0;
    /* As linhas "BB:DD.F  vvvv:mmmm  Classe" viram uma lista de classes. */
    int yy = y + 36;
    for (char *l = pci; *l && yy < y + ad - 20;) {
        char *fim = l;
        while (*fim && *fim != '\n') fim++;
        char guardado = *fim;
        *fim = 0;
        if (l[2] == ':' && l[5] == '.' && tam_texto(l) > 20) {
            g_ret_redondo(j, CX + 20, yy + 6, 5, 5, 2, C_FRACO, C_FRACO);
            g_texto_n(j, CX + 32, yy, l + 20, (CL - 50) / LETRA_L, C_TEXTO);
            yy += 18;
        }
        *fim = guardado;
        if (!guardado) break;
        l = fim + 1;
    }
}

/* ------------------------------------------------------------ IA (H69) */

/* O Sagui Code conversa com modelos pelo OpenRouter. Aqui ficam o modelo, o
 * limite de gasto por sessão e a chave. A chave vai para
 * /usb/.saguios/chaves.txt (nunca para configuracoes.txt) e nunca aparece na
 * tela: a caixa mostra só asteriscos. */
static caixa_t c_modelo, c_limite, c_chave;
static char chave_digitada[200];

static bool chave_guardada(void)
{
    int fd = abrir("/usb/.saguios/chaves.txt", ABRIR_LER);
    if (fd < 0) return false;
    char buf[512];
    int n = ler(fd, buf, sizeof buf - 1);
    fechar(fd);
    if (n <= 0) return false;
    buf[n] = 0;
    bool tem = false;
    for (char *p = buf; *p; p++) if (comeca_com(p, "openrouter") && (p == buf || p[-1] == '\n')) tem = true;
    memset(buf, 0, sizeof buf);
    return tem;
}

static void ia_desenhar(void)
{
    int y = TOPO_CONT;
    char v[128];
    cartao(CX, y, CL, 3 * 64 + 56);
    g_texto(j, CX + 18, y + 12, traduzir("Modelo (do OpenRouter)"), C_TEXTO);
    caixa_definir(&c_modelo, config_ler("ia.modelo", v, sizeof v) > 0 ? v : "anthropic/claude-sonnet-4.5");
    caixa("ia.modelo", &c_modelo, CX + 18, y + 32, CL - 36);
    y += 64;
    g_retangulo(j, CX + 16, y, CL - 32, 1, C_FILETE);
    g_texto(j, CX + 18, y + 12, traduzir("Limite de gasto por sessão (centavos de dólar)"), C_TEXTO);
    caixa_definir(&c_limite, config_ler("ia.limite_centavos", v, sizeof v) > 0 ? v : "100");
    caixa("ia.limite", &c_limite, CX + 18, y + 32, 100);
    y += 64;
    g_retangulo(j, CX + 16, y, CL - 32, 1, C_FILETE);
    bool tem = chave_guardada();
    g_texto(j, CX + 18, y + 12, traduzir(tem ? "Chave: guardada no pendrive. Para trocar, cole outra:" : "Chave: ainda não há. Cole a chave do OpenRouter:"), C_TEXTO);
    if (tem) g_ret_redondo(j, CX + CL - 30, y + 14, 10, 10, 5, C_VERDE, C_VERDE);
    chave_digitada[0] = 0;
    caixa_definir(&c_chave, "");
    caixa("ia.chave", &c_chave, CX + 18, y + 32, CL - 36);
    y += 64;
    g_texto(j, CX + 18, y + 10, traduzir("(a chave nunca aparece na tela nem no log)"), C_FRACO);
    botao("ia.guardar", CX + CL - 18 - largura_botao("Guardar"), y + 4, "Guardar", true);
}

/* Troca (ou acrescenta) a linha "openrouter = ..." de chaves.txt. */
static int gravar_chave(const char *chave)
{
    info_arquivo_t i;
    if (info_arquivo("/usb/.saguios", &i) < 0) return -1;
    static char antigo[2048], novo[2600];
    int n = 0, fd = abrir("/usb/.saguios/chaves.txt", ABRIR_LER);
    if (fd >= 0) { n = ler(fd, antigo, sizeof antigo - 1); fechar(fd); if (n < 0) n = 0; }
    antigo[n] = 0;
    int k = 0;
    for (char *p = antigo; *p;) {
        char *fim = acha_char(p, '\n');
        if (fim) *fim = 0;
        if (*p && !comeca_com(p, "openrouter")) k += formatar_texto(novo + k, sizeof novo - (size_t)k, "%s\n", p);
        if (!fim) break;
        p = fim + 1;
    }
    k += formatar_texto(novo + k, sizeof novo - (size_t)k, "openrouter = %s\n", chave);
    fd = abrir("/usb/.saguios/chaves.txt", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    int r = fd >= 0 ? escrever_tudo(fd, novo, (unsigned)k) : fd;
    if (fd >= 0) fechar(fd);
    memset(antigo, 0, sizeof antigo);
    memset(novo, 0, sizeof novo);
    return r < 0 ? r : 0;
}

/* A caixa da chave: o texto de verdade fica aqui; a caixa só mostra '*'. */
static bool ia_tecla(const evento_t *e)
{
    if (e->tipo != EV_TECLA || !c_chave.foco) return false;
    int n = (int)tam_texto(chave_digitada);
    if (e->tecla == 8 && n > 0) chave_digitada[--n] = 0;
    else if (e->tecla >= 33 && e->tecla < 127 && n < (int)sizeof chave_digitada - 1) { chave_digitada[n++] = (char)e->tecla; chave_digitada[n] = 0; }
    else if (e->tecla == 22) {
        /* Ctrl+V: a chave é longa demais para digitar. Cola só as letras
         * visíveis e nunca registra o que veio da área de cópia. */
        char c[sizeof chave_digitada];
        int k = area_copia_ler(c, sizeof c - 1);
        for (int i = 0; i < k && n < (int)sizeof chave_digitada - 1; i++)
            if ((unsigned char)c[i] >= 33 && (unsigned char)c[i] < 127) chave_digitada[n++] = c[i];
        chave_digitada[n] = 0;
        memset(c, 0, sizeof c);
    }
    char estrelas[48];
    int m = n < 40 ? n : 40;
    memset(estrelas, '*', (size_t)m);
    estrelas[m] = 0;
    caixa_definir(&c_chave, estrelas);
    c_chave.foco = true;
    caixa_desenhar(j, &c_chave);
    return true;
}

static void ia_clique(const char *n)
{
    if (compara_texto(n, "ia.guardar") != 0) return;
    int r = config_gravar("ia.modelo", c_modelo.texto);
    if (texto_para_int(c_limite.texto) > 0) config_gravar("ia.limite_centavos", c_limite.texto);
    if (chave_digitada[0]) {
        if (gravar_chave(chave_digitada) < 0) { mostrar_mensagem("Sem pendrive com .saguios: a chave não foi guardada.", -1); memset(chave_digitada, 0, sizeof chave_digitada); return; }
        memset(chave_digitada, 0, sizeof chave_digitada);
        registrar("configuracoes: chave do OpenRouter guardada");
    }
    registrar("configuracoes: ia modelo %s, limite %s", c_modelo.texto, c_limite.texto);
    redesenhar();
    mostrar_mensagem("IA configurada.", r);
}

/* ------------------------------------------------------------ Início */

/* Um cartão por área, com o estado dela; o clique leva à página. */
static void inicio_cartao(int p, int x, int y, int l, int a, const char *estado)
{
    cartao(x, y, l, a);
    icone(p, x + 16, y + 16, 32);
    g_texto(j, x + 60, y + 16, traduzir(p_nome[p]), C_TEXTO);
    g_texto(j, x + 61, y + 16, traduzir(p_nome[p]), C_TEXTO);
    g_texto_n(j, x + 60, y + 36, estado, (l - 76) / LETRA_L, C_FRACO);
    char nome[32];
    formatar_texto(nome, sizeof nome, "ir.%s", p_curto[p]);
    ctl(nome, x, y, l, a);
}

static void inicio_desenhar(void)
{
    int y = TOPO_CONT;
    char t[64];
    info_versao_t v;
    info_memoria_t m;
    info_sistema(INFO_VERSAO, &v, sizeof v);
    info_sistema(INFO_MEMORIA, &m, sizeof m);
    /* Faixa do computador. */
    g_ret_redondo(j, CX, y, CL, 84, 12, C_VERDE, 0x0E5C34);
    g_texto(j, CX + 22, y + 18, traduzir("Este computador"), 0xCFE9D8);
    formatar_texto(t, sizeof t, "SaguiOS %s", v.versao);
    for (int k = 0; k < 2; k++) g_texto(j, CX + 22 + k, y + 40, t, COR_BRANCO);
    formatar_texto(t, sizeof t, traduzir("%u MB de memória, tela %ux%u"), m.ram_total_kb / 1024, v.largura, v.altura);
    g_texto(j, CX + 22, y + 58, t, 0xCFE9D8);
    ctl("ir.sobre", CX, y, CL, 84);
    y += 84 + 16;
    int cw = (CL - 16) / 2, ch = 70;
    info_rede_t r;
    char rede[48];
    if (rede_info(&r) < 0) copia_texto(rede, traduzir("Sem placa de rede"), sizeof rede);
    else if (!r.cabo) copia_texto(rede, traduzir("Cabo desligado"), sizeof rede);
    else if (!r.configurado) copia_texto(rede, traduzir("Conectando..."), sizeof rede);
    else { char ip[16]; ip_para_texto(r.ip, ip); formatar_texto(rede, sizeof rede, traduzir("Conectado: %s"), ip); }
    char fundo[16], aparencia[48];
    if (config_ler("aparencia.fundo", fundo, sizeof fundo) < 0) copia_texto(fundo, "brasil", sizeof fundo);
    int f = 0;
    for (int i = 0; i < QTD_FUNDOS; i++) if (compara_texto(fundo, fundos[i]) == 0) f = i;
    formatar_texto(aparencia, sizeof aparencia, "%s, %s", traduzir(nomes_temas[config_indice("aparencia.tema", temas, QTD_TEMAS, 0)]),
                   traduzir(nomes_fundos[f]));
    char som[32];
    int vol = som_volume(-1);
    if (vol < 0) copia_texto(som, traduzir("Bip do PC"), sizeof som);
    else formatar_texto(som, sizeof som, traduzir("Volume %d%%"), vol);
    data_t d;
    ler_data(&d);
    char hora[32];
    formatar_texto(hora, sizeof hora, "%02u/%02u/%u %02u:%02u", d.dia, d.mes, d.ano, d.hora, d.minuto);
    char ling[32];
    int li = 0;
    for (int i = 0; i < QTD_IDIOMAS; i++) if (compara_texto(idioma(), idiomas[i]) == 0) li = i;
    formatar_texto(ling, sizeof ling, "%s, %s", nomes_idiomas[li], teclado_es() ? "ES" : "ABNT2");
    char progs[32];
    entrada_pasta_t e;
    int np = 0;
    for (int i = 0; listar("/usb/programas", i, &e) == 1; i++) if (e.tipo != TIPO_PASTA) np++;
    formatar_texto(progs, sizeof progs, traduzir("%d no pendrive"), np);
    const int ps[6] = { P_REDE, P_APARENCIA, P_SOM, P_RELOGIO, P_TECLADO, P_PROGRAMAS };
    const char *est[6] = { rede, aparencia, som, hora, ling, progs };
    for (int i = 0; i < 6; i++) inicio_cartao(ps[i], CX + (i % 2) * (cw + 16), y + (i / 2) * (ch + 14), cw, ch, est[i]);
}

/* ------------------------------------------------------------ janela */

static bool pagina_visivel(int p)
{
    if (!busca[0]) return true;
    char b[32];
    int n = 0;
    for (int i = 0; busca[i] && n < 31; i++) b[n++] = (char)minuscula_latin1((unsigned char)busca[i]);
    b[n] = 0;
    /* no nome (minúsculo) ou nas palavras */
    char nome[48];
    copia_texto(nome, traduzir(p_nome[p]), sizeof nome);
    for (int i = 0; nome[i]; i++) nome[i] = (char)minuscula_latin1((unsigned char)nome[i]);
    for (const char *s = nome; *s; s++) if (compara_texto_n(s, b, (size_t)n) == 0) return true;
    for (const char *s = p_palavras[p]; *s; s++) if (compara_texto_n(s, b, (size_t)n) == 0) return true;
    return false;
}

static void desenhar_lado(void)
{
    g_retangulo(j, 0, 0, LADO, A, C_LADO);
    g_retangulo(j, LADO - 1, 0, 1, A, C_FILETE);
    /* busca */
    g_ret_redondo(j, 12, 14, LADO - 24, 32, 8, busca_foco ? C_VERDE : C_FILETE, busca_foco ? C_VERDE : C_FILETE);
    g_ret_redondo(j, 13, 15, LADO - 26, 30, 7, C_CARTAO, C_CARTAO);
    g_contorno_redondo(j, 24, 22, 11, 11, 5, C_FRACO);   /* lupa */
    g_linha(j, 33, 31, 37, 35, C_FRACO);
    g_linha(j, 34, 31, 38, 35, C_FRACO);
    if (busca[0]) g_texto_n(j, 46, 22, busca, (LADO - 70) / LETRA_L, C_TEXTO);
    else g_texto(j, 46, 22, traduzir("Buscar"), 0x9AA3AC);
    if (busca_foco) g_retangulo(j, 46 + (int)tam_texto(busca) * LETRA_L, 22, 2, LETRA_A, C_TEXTO);
    ctl("busca", 12, 14, LADO - 24, 32);
    /* páginas, por grupo */
    int y = 60, grupo = -1, achadas = 0;
    for (int p = 0; p < QTD_P; p++) {
        if (!pagina_visivel(p)) continue;
        achadas++;
        if (p_grupo[p] != grupo) {
            grupo = p_grupo[p];
            if (grupos[grupo][0]) { y += 6; g_texto(j, 18, y, traduzir(grupos[grupo]), C_FRACO); y += 22; }
        }
        if (p == pagina) g_ret_redondo(j, 8, y, LADO - 16, 34, 8, C_SEL_LADO, C_SEL_LADO);
        icone(p, 16, y + 6, 22);
        g_texto(j, 48, y + 9, traduzir(p_nome[p]), C_TEXTO);
        char nome[32];
        formatar_texto(nome, sizeof nome, "ir.%s", p_curto[p]);
        ctl(nome, 8, y, LADO - 16, 34);
        y += 38;
    }
    if (!achadas) g_texto(j, 18, y + 8, traduzir("Nada encontrado."), C_FRACO);
    /* onde ficam as escolhas */
    char per[8];
    config_ler("sistema.persistente", per, sizeof per);
    bool pen = compara_texto(per, "sim") == 0;
    g_ret_redondo(j, 18, A - 30, 8, 8, 4, pen ? C_VERDE : 0xD9822B, pen ? C_VERDE : 0xD9822B);
    g_texto(j, 32, A - 34, traduzir(pen ? "Guardado no pendrive" : "Só até desligar"), C_FRACO);
}

static void desenhar_pagina(void)
{
    g_retangulo(j, LADO, 0, L - LADO, A, C_FUNDO);
    /* Caminho: "Configurações › Página"; no Início, só o título. */
    int x = CX, y = 22;
    if (pagina != P_INICIO) {
        const char *raiz = traduzir("Configurações");
        g_texto(j, x, y, raiz, C_FRACO);
        ctl("ir.inicio", x, y - 4, g_largura_texto(raiz), 24);
        x += g_largura_texto(raiz) + 8;
        g_texto(j, x, y, ">", C_FRACO);
        x += 16;
    }
    const char *tit = traduzir(pagina == P_INICIO ? "Configurações" : p_nome[pagina]);
    for (int k = 0; k < 2; k++) g_texto(j, x + k, y, tit, C_TEXTO);
    g_retangulo(j, CX, 52, CL, 1, C_FILETE);
    switch (pagina) {
    case P_INICIO:    inicio_desenhar(); break;
    case P_SISTEMA:   sobre_desenhar(); break;
    case P_RELOGIO:   relogio_desenhar(); break;
    case P_APARENCIA: aparencia_desenhar(); break;
    case P_TECLADO:   teclado_desenhar(); break;
    case P_REDE:      rede_desenhar(); break;
    case P_SOM:       som_desenhar(); break;
    case P_PROGRAMAS: programas_desenhar(); break;
    case P_IA:        ia_desenhar(); break;
    }
    if (recado[0]) mostrar_mensagem(recado, recado_cor ? -1 : -100);
}

static void desenhar_tudo(void)
{
    qtd_ctls = 0;
    desenhar_lado();
    desenhar_pagina();
    jan_mostrar(j);
}

/* Redesenha mantendo o recado de agora (quem chama põe o novo depois). */
static void redesenhar(void)
{
    recado[0] = 0;
    desenhar_tudo();
}

static void ir_para(int p)
{
    if (p < 0 || p >= QTD_P) return;
    pagina = p;
    recado[0] = 0;
    busca_foco = false;
    registrar("configuracoes: aba %s", p_log[p]);
    desenhar_tudo();
}

/* Pelo nome curto ("som", "teclado"), sem diferença de maiúsculas, ou
 * pelas 3 primeiras letras do nome do log ("IA", "Relógio"). */
static int pagina_pelo_nome(const char *arg)
{
    char s[24], l[24];
    int n = 0;
    for (; arg[n] && n < 23; n++) s[n] = (char)minuscula_latin1((unsigned char)arg[n]);
    s[n] = 0;
    for (int p = 0; p < QTD_P; p++) if (compara_texto(s, p_curto[p]) == 0) return p;
    if (compara_texto(s, "idioma") == 0) return P_TECLADO;
    if (compara_texto(s, "sistema") == 0) return P_SISTEMA;
    for (int p = 0; p < QTD_P && n >= 2; p++) {
        int k = 0;
        for (; p_log[p][k] && k < 23; k++) l[k] = (char)minuscula_latin1((unsigned char)p_log[p][k]);
        l[k] = 0;
        if (compara_texto_n(s, l, n < 3 ? (size_t)n : 3) == 0) return p;
    }
    return -1;
}

/* Todas as caixas de texto da página atual. */
static int caixas_da_pagina(caixa_t **cs)
{
    int n = 0;
    if (pagina == P_RELOGIO) { cs[n++] = &c_dia; cs[n++] = &c_mes; cs[n++] = &c_ano; cs[n++] = &c_hora; cs[n++] = &c_min; cs[n++] = &c_fuso; }
    else if (pagina == P_REDE && rede_manual) { cs[n++] = &c_ip; cs[n++] = &c_masc; cs[n++] = &c_rot; cs[n++] = &c_dns; }
    else if (pagina == P_IA) { cs[n++] = &c_modelo; cs[n++] = &c_limite; }
    return n;
}

static void clique(const evento_t *e)
{
    const char *n = ctl_em(e->x, e->y);
    /* as caixas de texto recebem o clique para pegar o foco */
    caixa_t *cs[8];
    int nc = caixas_da_pagina(cs);
    for (int i = 0; i < nc; i++) caixa_tratar(j, cs[i], e);
    if (pagina == P_IA) { c_chave.foco = false; caixa_tratar(j, &c_chave, e); }
    bool na_busca = n && compara_texto(n, "busca") == 0;
    if (na_busca != busca_foco) { busca_foco = na_busca; qtd_ctls = 0; desenhar_lado(); desenhar_pagina(); jan_mostrar(j); n = ctl_em(e->x, e->y); }
    if (!n || na_busca) return;
    registrar("configuracoes: clique %s", n);
    if (comeca(n, "ir.")) { int p = pagina_pelo_nome(n + 3); if (p >= 0 && p != pagina) ir_para(p); return; }
    switch (pagina) {
    case P_RELOGIO:   relogio_clique(n); break;
    case P_APARENCIA: aparencia_clique(n); break;
    case P_TECLADO:   teclado_clique(n); break;
    case P_REDE:      rede_clique(n); break;
    case P_SOM:       som_clique(n, e->x); break;
    case P_PROGRAMAS: programas_clique(n); break;
    case P_IA:        ia_clique(n); break;
    }
}

static void tecla(const evento_t *e)
{
    if (busca_foco) {
        int n = (int)tam_texto(busca);
        if (e->tecla == TECLA_ESC) { busca[0] = 0; busca_foco = false; }
        else if (e->tecla == '\n') {
            for (int p = 0; p < QTD_P; p++) if (pagina_visivel(p)) { busca[0] = 0; ir_para(p); return; }
        } else if (e->tecla == '\b') { if (n) busca[n - 1] = 0; }
        else if (e->tecla >= 32 && e->tecla < 256 && n < (int)sizeof busca - 1) { busca[n] = (char)e->tecla; busca[n + 1] = 0; }
        else return;
        registrar("configuracoes: busca \"%s\"", busca);
        qtd_ctls = 0;
        desenhar_lado();
        desenhar_pagina();
        jan_mostrar(j);
        return;
    }
    if (pagina == P_IA && ia_tecla(e)) return;
    caixa_t *cs[8];
    int nc = caixas_da_pagina(cs);
    for (int i = 0; i < nc; i++) caixa_tratar(j, cs[i], e);
}

int principal(int argc, char **argv)
{
    j = jan_abrir(traduzir("Configurações"), L, A);
    if (!j) return 1;
    if (argc > 1) { int p = pagina_pelo_nome(argv[1]); if (p >= 0) pagina = p; }
    desenhar_tudo();
    registrar("configuracoes: aberto na aba %s", p_log[pagina]);
    evento_t e;
    unsigned ultimo = tempo_ms();
    for (;;) {
        if (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) break;
            if (e.tipo == EV_CLIQUE && (e.botoes & 1)) clique(&e);
            else if (e.tipo == EV_TECLA) tecla(&e);
            continue;
        }
        if (tempo_ms() - ultimo >= 1000) {
            ultimo = tempo_ms();
            if (pagina == P_RELOGIO) relogio_agora();
            if (pagina == P_REDE) rede_estado_desenhar();
        }
        dormir(40);
    }
    jan_fechar(j);
    return 0;
}
