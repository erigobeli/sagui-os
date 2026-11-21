/* dialogo_arquivo.c - janela de abrir e salvar arquivo (libsagui).
 *
 *   bool escolher_arquivo(bool salvar, const char *titulo, char *caminho, int max);
 *
 * Na entrada, "caminho" diz onde começar: uma pasta, ou um arquivo (a pasta
 * dele, e o nome já escrito). Na saída, o caminho escolhido. Devolve false
 * se a pessoa cancelou.
 *
 * Em cima: voltar, subir, o caminho (cada pedaço é clicável) e Nova pasta.
 * À esquerda, os locais: SaguiOS (/), Temporários (/tmp) e o pendrive
 * (/usb), quando há um. No meio, a pasta: as pastas primeiro, depois os
 * arquivos do tipo escolhido, com o tamanho. Um clique num arquivo põe o
 * nome embaixo; dois cliques numa pasta entram nela, num arquivo confirmam.
 * Embaixo: o nome, o tipo (Todos, Texto, C, Markdown) e os botões. Ao
 * salvar, o nome sem extensão ganha a do tipo, e um arquivo que já existe
 * pede confirmação antes de ser substituído.
 *
 * Teclado: setas mudam a seleção, Enter confirma (ou entra na pasta),
 * Backspace com o nome vazio sobe uma pasta, Esc cancela. */
#include "sagui.h"
#include "gui.h"

#define L        660
#define A        440
#define TOPO_A   48
#define LADO_L   150
#define PE_A     88
#define LINHA_A  24
#define CAB_A    26                        /* cabeçalho da lista: Nome e Tamanho */
#define LISTA_Y  (TOPO_A + CAB_A)
#define LISTA_A  (A - TOPO_A - CAB_A - PE_A)
#define MAX_IT   160

#define C_FUNDO   0xFFFFFF
#define C_LADO    0xF3F5F7
#define C_FILETE  0xE1E4E8
#define C_SEL     0xDCEBFB
#define C_SEL_LADO 0xE2E7EC
#define C_FRACO   0x6A737C

typedef struct { char nome[64]; uint8_t tipo; uint32_t tam; } item_t;

static jan_t *j;
static bool modo_salvar;
static char pasta[160];
static item_t itens[MAX_IT];
static int qtd, sel, topo;
static caixa_t nome;
static int tipo_atual;                     /* índice em tipos[] */
static char historico[8][160];             /* para o "voltar" */
static int qtd_hist;

static const char *const tipos_nome[4] = { "Todos os arquivos", "Texto (.txt)", "Programa em C (.c)", "Markdown (.md)" };
static const char *const tipos_ext[4] = { "", ".txt", ".c", ".md" };

/* ------------------------------------------------------------ ícones */

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
static const char *const ic_disco[] = {
    "................", "................", "................", "..PPPPPPPPPPPP..",
    ".PBBBBBBBBBBBBP.", ".PBBBBBBBBBBBBP.", ".PCCCCCCCCCCCCP.", ".PCCCCCCCCCCGCP.",
    ".PCCCCCCCCCCCCP.", ".PPPPPPPPPPPPPP.", "................", "................",
    "................", "................", "................", "................",
};

static uint32_t paleta(char c)
{
    switch (c) {
    case 'P': return 0x5A6470;
    case 'Y': return 0xFFE070;
    case 'A': return 0xF2C230;
    case 'B': return 0xFFFFFF;
    case 'C': return 0xB4BBC2;
    case 'G': return 0x0B7A3E;
    default:  return 0xFF000000;
    }
}

/* ------------------------------------------------------------ pasta */

static bool termina_com_ext(const char *s, const char *fim)
{
    unsigned a = tam_texto(s), b = tam_texto(fim);
    if (!b) return true;
    if (a < b) return false;
    for (unsigned i = 0; i < b; i++) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != fim[i]) return false;
    }
    return true;
}

static void juntar(char *dest, int max, const char *dir, const char *n)
{
    formatar_texto(dest, max, "%s%s%s", dir, compara_texto(dir, "/") == 0 ? "" : "/", n);
}

static void carregar(void)
{
    entrada_pasta_t e;
    qtd = 0;
    for (int passo = 0; passo < 2; passo++)
        for (int i = 0; qtd < MAX_IT && listar(pasta, i, &e) == 1; i++) {
            if (e.nome[0] == '.') continue;
            bool p = e.tipo == TIPO_PASTA;
            if ((passo == 0) != p) continue;
            if (!p && !termina_com_ext(e.nome, tipos_ext[tipo_atual])) continue;
            copia_texto(itens[qtd].nome, e.nome, sizeof itens[qtd].nome);
            itens[qtd].tipo = (uint8_t)e.tipo;
            itens[qtd].tam = e.tamanho;
            qtd++;
        }
    sel = -1;
    topo = 0;
    registrar("arquivo-dialogo: pasta %s (%d itens)", pasta, qtd);
}

/* ------------------------------------------------------------ desenho */

static int vis(void) { return LISTA_A / LINHA_A; }

static bool tem_pendrive(void)
{
    info_arquivo_t inf;
    return info_arquivo("/usb", &inf) >= 0 && inf.tipo == TIPO_PASTA;
}

static const char *const locais_nome[3] = { "SaguiOS", "Temporários", "Pendrive" };
static const char *const locais_cam[3] = { "/", "/tmp", "/usb" };
static int qtd_locais(void) { return tem_pendrive() ? 3 : 2; }
static int local_y(int i) { return TOPO_A + 34 + i * 30; }

static void desenhar_topo(void)
{
    g_retangulo(j, 0, 0, L, TOPO_A, C_FUNDO);
    g_retangulo(j, 0, TOPO_A - 1, L, 1, C_FILETE);
    /* voltar e subir */
    uint32_t cv = qtd_hist ? 0x3A4048 : 0xB4BBC2, cs = compara_texto(pasta, "/") ? 0x3A4048 : 0xB4BBC2;
    for (int k = 0; k < 5; k++) { g_retangulo(j, 14 + k, 24 - k, 1, 1 + 2 * k, cv); }   /* < */
    g_retangulo(j, 18, 23, 10, 2, cv);
    for (int k = 0; k < 5; k++) { g_retangulo(j, 48 - k, 18 + k, 1 + 2 * k, 1, cs); }   /* ^ */
    g_retangulo(j, 47, 22, 2, 10, cs);
    /* caminho: uma caixa redonda com os pedaços separados por ">" */
    int bx = 70, bl = L - 70 - 136;
    g_ret_redondo(j, bx, 10, bl, 28, 6, C_FILETE, C_FILETE);
    g_ret_redondo(j, bx + 1, 11, bl - 2, 26, 5, C_LADO, C_LADO);
    int x = bx + 10;
    g_texto(j, x, 16, "SaguiOS", 0x3A4048);
    x += g_largura_texto("SaguiOS");
    char pedaco[64];
    const char *p = pasta + 1;
    while (*p && x < bx + bl - 40) {
        int n = 0;
        while (p[n] && p[n] != '/' && n < 63) { pedaco[n] = p[n]; n++; }
        pedaco[n] = 0;
        g_texto(j, x + 6, 16, ">", C_FRACO);
        g_texto(j, x + 20, 16, pedaco, 0x3A4048);
        x += 20 + g_largura_texto(pedaco);
        p += n;
        if (*p == '/') p++;
    }
    /* Nova pasta */
    int nx = L - 126;
    g_ret_redondo(j, nx, 10, 116, 28, 6, C_FILETE, C_FILETE);
    g_ret_redondo(j, nx + 1, 11, 114, 26, 5, C_FUNDO, C_FUNDO);
    g_desenho(j, nx + 8, 16, ic_pasta, 16, 16, paleta, 1);
    g_texto(j, nx + 28, 16, "Nova pasta", 0x3A4048);
}

static void desenhar_lado(void)
{
    g_retangulo(j, 0, TOPO_A, LADO_L, A - TOPO_A - PE_A, C_LADO);
    g_retangulo(j, LADO_L - 1, TOPO_A, 1, A - TOPO_A - PE_A, C_FILETE);
    g_texto(j, 14, TOPO_A + 10, "Locais", C_FRACO);
    for (int i = 0; i < qtd_locais(); i++) {
        int y = local_y(i);
        bool aqui = compara_texto(pasta, locais_cam[i]) == 0 ||
                    (i && tam_texto(pasta) > tam_texto(locais_cam[i]) && compara_texto_n(pasta, locais_cam[i], tam_texto(locais_cam[i])) == 0 &&
                     pasta[tam_texto(locais_cam[i])] == '/');
        if (aqui) g_ret_redondo(j, 6, y, LADO_L - 12, 26, 6, C_SEL_LADO, C_SEL_LADO);
        g_desenho(j, 14, y + 5, i == 2 ? ic_disco : ic_pasta, 16, 16, paleta, 1);
        g_texto(j, 38, y + 5, locais_nome[i], 0x1E2328);
    }
}

static void tamanho_texto(uint32_t t, char *s, int max)
{
    if (t < 1024) formatar_texto(s, max, "%u B", t);
    else if (t < 1024 * 1024) formatar_texto(s, max, "%u KB", (t + 1023) / 1024);
    else formatar_texto(s, max, "%u MB", (t + 1024 * 1024 - 1) / (1024 * 1024));
}

static void desenhar_lista(void)
{
    int x0 = LADO_L, l = L - LADO_L;
    g_retangulo(j, x0, TOPO_A, l, CAB_A + LISTA_A, C_FUNDO);
    g_texto(j, x0 + 40, TOPO_A + 6, "Nome", C_FRACO);
    g_texto(j, L - 100, TOPO_A + 6, "Tamanho", C_FRACO);
    g_retangulo(j, x0 + 10, TOPO_A + CAB_A - 1, l - 20, 1, C_FILETE);
    if (qtd == 0) g_texto(j, x0 + 40, LISTA_Y + 14, "Esta pasta está vazia.", C_FRACO);
    for (int k = 0; k < vis() && topo + k < qtd; k++) {
        int i = topo + k, y = LISTA_Y + 2 + k * LINHA_A;
        if (i == sel) g_ret_redondo(j, x0 + 6, y, l - 12, LINHA_A - 2, 5, C_SEL, C_SEL);
        g_desenho(j, x0 + 14, y + 3, itens[i].tipo == TIPO_PASTA ? ic_pasta : ic_arquivo, 16, 16, paleta, 1);
        int cabe = (L - 110 - (x0 + 40)) / LETRA_L;
        g_texto_n(j, x0 + 40, y + 3, itens[i].nome, cabe, 0x1E2328);
        if (itens[i].tipo != TIPO_PASTA) {
            char t[16];
            tamanho_texto(itens[i].tam, t, sizeof t);
            g_texto(j, L - 100, y + 3, t, C_FRACO);
        }
    }
    if (qtd > vis()) {                     /* barra fina: onde se está na pasta */
        int tr = LISTA_A - 8, pa = tr * vis() / qtd, py = (tr - pa) * topo / (qtd - vis());
        if (pa < 20) pa = 20;
        g_ret_redondo(j, L - 9, LISTA_Y + 4 + py, 5, pa, 2, 0xB4BBC2, 0xB4BBC2);
    }
}

#define BOT_L 104
static int ok_x(void) { return L - 16 - BOT_L; }
static int canc_x(void) { return ok_x() - 10 - BOT_L; }
#define PE_Y (A - PE_A)

static void desenhar_pe(void)
{
    g_retangulo(j, 0, PE_Y, L, PE_A, C_LADO);
    g_retangulo(j, 0, PE_Y, L, 1, C_FILETE);
    g_texto(j, 16, PE_Y + 16, "Nome:", 0x3A4048);
    caixa_desenhar(j, &nome);
    /* tipo: um botão que abre o menu dos tipos */
    int tx = nome.x + nome.l + 12, tl = L - 16 - tx;
    g_ret_redondo(j, tx, PE_Y + 10, tl, 26, 6, C_FILETE, C_FILETE);
    g_ret_redondo(j, tx + 1, PE_Y + 11, tl - 2, 24, 5, C_FUNDO, C_FUNDO);
    g_texto_n(j, tx + 10, PE_Y + 15, tipos_nome[tipo_atual], (tl - 32) / LETRA_L, 0x1E2328);
    for (int k = 0; k < 4; k++) g_retangulo(j, tx + tl - 20 + k, PE_Y + 20 + k, 8 - 2 * k, 1, C_FRACO);   /* v */
    /* botões */
    const char *rot = modo_salvar ? "Salvar" : "Abrir";
    g_ret_redondo(j, ok_x(), PE_Y + 48, BOT_L, 28, 6, COR_DESTAQUE, COR_DESTAQUE);
    g_texto(j, ok_x() + (BOT_L - g_largura_texto(rot)) / 2, PE_Y + 54, rot, COR_BRANCO);
    g_ret_redondo(j, canc_x(), PE_Y + 48, BOT_L, 28, 6, C_FILETE, C_FILETE);
    g_ret_redondo(j, canc_x() + 1, PE_Y + 49, BOT_L - 2, 26, 5, C_FUNDO, C_FUNDO);
    g_texto(j, canc_x() + (BOT_L - g_largura_texto("Cancelar")) / 2, PE_Y + 54, "Cancelar", 0x1E2328);
}

static void desenhar_tudo(void)
{
    desenhar_topo();
    desenhar_lado();
    desenhar_lista();
    desenhar_pe();
    jan_mostrar(j);
}

/* ------------------------------------------------------------ ações */

static void ir_para(const char *nova, bool guardar)
{
    info_arquivo_t inf;
    if (info_arquivo(nova, &inf) < 0 || inf.tipo != TIPO_PASTA) return;
    if (guardar && compara_texto(nova, pasta) != 0) {
        if (qtd_hist == 8) { for (int i = 1; i < 8; i++) copia_texto(historico[i - 1], historico[i], 160); qtd_hist--; }
        copia_texto(historico[qtd_hist++], pasta, 160);
    }
    copia_texto(pasta, nova, sizeof pasta);
    carregar();
    desenhar_tudo();
}

static void subir(void)
{
    if (compara_texto(pasta, "/") == 0) return;
    char p[160];
    copia_texto(p, pasta, sizeof p);
    char *b = acha_ultimo_char(p, '/');
    if (b == p) p[1] = 0; else *b = 0;
    ir_para(p, true);
}

static void voltar(void)
{
    if (!qtd_hist) return;
    char p[160];
    copia_texto(p, historico[--qtd_hist], sizeof p);
    ir_para(p, false);
}

static void selecionar(int i)
{
    if (i < 0 || i >= qtd) return;
    sel = i;
    if (sel < topo) topo = sel;
    if (sel >= topo + vis()) topo = sel - vis() + 1;
    if (itens[i].tipo != TIPO_PASTA) {
        caixa_definir(&nome, itens[i].nome);
        nome.cursor = (int)tam_texto(nome.texto);
    }
    desenhar_lista();
    desenhar_pe();
    jan_mostrar(j);
}

static void nova_pasta(void)
{
    char n[64] = "Nova pasta";
    if (!pedir_texto("Nova pasta", "Nome da pasta:", n, sizeof n) || !n[0]) return;
    char c[160];
    juntar(c, sizeof c, pasta, n);
    int r = criar_pasta(c);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui criar a pasta\n%s\n(%s)", c, nome_erro(r)); mensagem("Nova pasta", m); return; }
    registrar("arquivo-dialogo: pasta criada %s", c);
    ir_para(c, true);
}

/* Confirma: devolve true quando "saida" tem o caminho final. */
static bool confirmar_escolha(char *saida, int max)
{
    if (!nome.texto[0]) {
        if (sel >= 0 && itens[sel].tipo == TIPO_PASTA) { char c[160]; juntar(c, sizeof c, pasta, itens[sel].nome); ir_para(c, true); }
        return false;
    }
    char n[128];
    copia_texto(n, nome.texto, sizeof n);
    if (n[0] == '/') copia_texto(saida, n, (size_t)max);    /* escreveram o caminho inteiro */
    else juntar(saida, max, pasta, n);
    info_arquivo_t inf;
    bool existe = info_arquivo(saida, &inf) >= 0;
    if (existe && inf.tipo == TIPO_PASTA) { char c[160]; copia_texto(c, saida, sizeof c); caixa_definir(&nome, ""); ir_para(c, true); return false; }
    if (modo_salvar) {
        if (!existe && tipo_atual && !acha_ultimo_char(n, '.')) {   /* sem extensão: a do tipo */
            int k = (int)tam_texto(saida);
            formatar_texto(saida + k, max - k, "%s", tipos_ext[tipo_atual]);
            existe = info_arquivo(saida, &inf) >= 0;
        }
        if (existe) {
            char m[220];
            const char *b = acha_ultimo_char(saida, '/');
            formatar_texto(m, sizeof m, "\"%s\" já existe.\nSubstituir o arquivo?", b ? b + 1 : saida);
            if (!confirmar(modo_salvar ? "Salvar como" : "Abrir", m)) return false;
        }
        return true;
    }
    if (!existe) { char m[200]; formatar_texto(m, sizeof m, "Não achei o arquivo\n%s", saida); mensagem("Abrir", m); return false; }
    return true;
}

static int item_em(int x, int y)
{
    if (x < LADO_L || y < LISTA_Y || y >= LISTA_Y + LISTA_A) return -1;
    int i = topo + (y - LISTA_Y - 2) / LINHA_A;
    return i < qtd ? i : -1;
}

/* ------------------------------------------------------------ a janela */

bool escolher_arquivo(bool salvar, const char *titulo, char *caminho, int max)
{
    modo_salvar = salvar;
    qtd_hist = 0;
    memset(&nome, 0, sizeof nome);
    /* Onde começar: a pasta do caminho (ou ele, se for pasta), e o nome. */
    info_arquivo_t inf;
    copia_texto(pasta, "/", sizeof pasta);
    if (caminho && caminho[0] == '/') {
        if (info_arquivo(caminho, &inf) >= 0 && inf.tipo == TIPO_PASTA) copia_texto(pasta, caminho, sizeof pasta);
        else {
            const char *b = acha_ultimo_char(caminho, '/');
            int n = (int)(b - caminho);
            if (n == 0) n = 1;
            if (n < (int)sizeof pasta) { for (int i = 0; i < n; i++) pasta[i] = caminho[i]; pasta[n] = 0; }
            if (info_arquivo(pasta, &inf) < 0 || inf.tipo != TIPO_PASTA) copia_texto(pasta, "/", sizeof pasta);
            caixa_definir(&nome, b + 1);
        }
    }
    tipo_atual = 0;
    for (int t = 1; t < 4; t++) if (nome.texto[0] && termina_com_ext(nome.texto, tipos_ext[t])) tipo_atual = t;
    if (!salvar) tipo_atual = 0;
    nome.cursor = (int)tam_texto(nome.texto);
    nome.x = 70; nome.y = PE_Y + 10; nome.l = 330; nome.foco = true;

    j = jan_abrir(titulo, L, A);
    if (!j) return false;
    carregar();
    desenhar_tudo();
    registrar("arquivo-dialogo: aberto (%s) em %s", salvar ? "salvar" : "abrir", pasta);

    bool ok = false;
    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_MENU) {                         /* o menu dos tipos */
            if (e.tecla < 4) { tipo_atual = (int)e.tecla; carregar(); desenhar_tudo(); registrar("arquivo-dialogo: tipo %s", tipos_nome[tipo_atual]); }
            continue;
        }
        if (e.tipo == EV_TECLA) {
            unsigned t = e.tecla;
            if (t == TECLA_ESC) break;
            if (t == '\n') { if (confirmar_escolha(caminho, max)) { ok = true; break; } desenhar_tudo(); continue; }
            if (t == TECLA_CIMA) { selecionar(sel <= 0 ? 0 : sel - 1); continue; }
            if (t == TECLA_BAIXO) { selecionar(sel + 1 < qtd ? sel + 1 : sel); continue; }
            if (t == '\b' && !nome.texto[0]) { subir(); continue; }
            caixa_tratar(j, &nome, &e);
            continue;
        }
#ifdef EV_RODA
        if (e.tipo == EV_RODA) {
            topo += 3 * (int)e.tecla;
            if (topo > qtd - vis()) topo = qtd - vis();
            if (topo < 0) topo = 0;
            desenhar_lista();
            jan_mostrar(j);
            continue;
        }
#endif
        if (e.tipo != EV_CLIQUE || !(e.botoes & 1)) continue;
        int x = e.x, y = e.y;
        if (y < TOPO_A) {
            if (x >= 6 && x < 32) voltar();
            else if (x >= 36 && x < 62) subir();
            else if (x >= L - 126 && x < L - 10) nova_pasta();
            else if (x >= 70 && x < L - 136) {               /* um pedaço do caminho: vai para ele */
                int px = 80 + g_largura_texto("SaguiOS");
                if (x < px) { ir_para("/", true); continue; }
                const char *p = pasta + 1;
                while (*p) {
                    int n = 0;
                    while (p[n] && p[n] != '/') n++;
                    char ped[64];
                    int k = n < 63 ? n : 63;
                    for (int i = 0; i < k; i++) ped[i] = p[i];
                    ped[k] = 0;
                    int fim = px + 20 + g_largura_texto(ped);
                    if (x < fim) {
                        char c[160];
                        int ate = (int)(p - pasta) + n;
                        for (int i = 0; i < ate && i < 159; i++) c[i] = pasta[i];
                        c[ate < 159 ? ate : 159] = 0;
                        ir_para(c, true);
                        break;
                    }
                    px = fim;
                    p += n;
                    if (*p == '/') p++;
                }
            }
            continue;
        }
        if (y >= PE_Y) {
            int tx = nome.x + nome.l + 12;
            if (y >= PE_Y + 48 && y < PE_Y + 76 && x >= ok_x() && x < ok_x() + BOT_L) {
                if (confirmar_escolha(caminho, max)) { ok = true; break; }
                desenhar_tudo();
            } else if (y >= PE_Y + 48 && y < PE_Y + 76 && x >= canc_x() && x < canc_x() + BOT_L) break;
            else if (y >= PE_Y + 10 && y < PE_Y + 36 && x >= tx) jan_menu(j, tx, PE_Y + 38, "Todos os arquivos|Texto (.txt)|Programa em C (.c)|Markdown (.md)");
            else caixa_tratar(j, &nome, &e);
            continue;
        }
        if (x < LADO_L) {
            for (int i = 0; i < qtd_locais(); i++)
                if (y >= local_y(i) && y < local_y(i) + 26) { ir_para(locais_cam[i], true); break; }
            continue;
        }
        int i = item_em(x, y);
        if (i < 0) continue;
        if (e.cliques == 2) {
            if (itens[i].tipo == TIPO_PASTA) { char c[160]; juntar(c, sizeof c, pasta, itens[i].nome); ir_para(c, true); }
            else { selecionar(i); if (confirmar_escolha(caminho, max)) { ok = true; break; } desenhar_tudo(); }
        } else selecionar(i);
    }
    jan_fechar(j);
    registrar("arquivo-dialogo: %s%s", ok ? "escolhido " : "cancelado", ok ? caminho : "");
    return ok;
}
