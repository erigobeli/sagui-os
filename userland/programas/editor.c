/* editor [arquivo] - o Editor de texto do SaguiOS (H25).
 *
 * Uma barra de botões (Novo, Abrir, Salvar, Salvar como) e a área de texto
 * do kit gráfico. Ctrl+S salva. O título mostra o nome do arquivo e um "*"
 * quando há alteração não salva; fechar com alterações pergunta se quer
 * salvar. O texto é Latin-1, como em todo o sistema.
 *
 * H67: "Compilar e rodar" salva, chama o cc e, se deu certo, abre o
 * programa; se deu erro, mostra a mensagem do compilador e leva o cursor
 * para a linha do erro. "Instalar" compila direto para /usb/programas, e o
 * programa ganha um ícone na área de trabalho.
 *
 * Visual: em cima, a aba do arquivo (um ponto verde quando há alteração) e
 * o "+" de novo (até 6 abas, cada uma com o seu arquivo; o "x" fecha a
 * aba, Ctrl+W e o botão do meio também; Ctrl+Tab passa para a próxima;
 * o botão direito numa aba tem Nova aba, Fechar e Fechar as outras; no
 * texto, Recortar, Copiar, Colar e Selecionar tudo); embaixo delas, os menus Arquivo, Editar e Exibir, e, nos
 * programas em C, os botões de compilar à direita. O texto ocupa a janela
 * toda, sem moldura; embaixo, a barra de status com a linha, a coluna, o
 * total de letras e o tipo do arquivo (Exibir liga e desliga). */
#include "sagui.h"
#include "gui.h"

/* Tamanho inicial; a janela pode mudar (H78). */
static int larg_jan = 700, alt_jan = 480;
#define LARG larg_jan
#define ALT  alt_jan
#define ABAS_A   36                       /* faixa da aba */
#define MENU_A   34                       /* menus e botões */
#define BARRA    (ABAS_A + MENU_A)
#define STATUS_A 26
#define COR_FAIXA   0xE8EBEE
#define COR_FILETE  0xE1E4E8
#define COR_STATUS  0xF6F7F8

static jan_t *j;
/* Cada aba é um texto aberto. "area" e "caminho" são os da aba atual. */
#define MAX_ABAS 6
typedef struct { area_t a; char cam[160]; } doc_t;
static doc_t docs[MAX_ABAS];
static int qtd_docs, atual;
#define area    (docs[atual].a)
#define caminho (docs[atual].cam)
static bool modificado_mostrado = true;
static bool status_ligado = true;
static int menu_aberto = -1;              /* 0 Arquivo, 1 Editar, 2 Exibir, 3 botão direito no texto, 4 numa aba */
static int aba_do_menu;                   /* a aba onde o botão direito abriu o menu 4 */
static int realce = -1;                   /* o menu de cima com o fundo marcado (aberto) */
static int alt_status(void) { return status_ligado ? STATUS_A : 0; }

static const char *nome_de(int i)
{
    const char *c = docs[i].cam;
    if (!c[0]) return "sem nome";
    const char *b = acha_ultimo_char(c, '/');
    return b ? b + 1 : c;
}
static const char *nome_curto(void) { return nome_de(atual); }

static void desenhar_barra(void);

static void atualizar_titulo(void)
{
    if (modificado_mostrado == area.modificado) return;
    modificado_mostrado = area.modificado;
    char t[96];
    formatar_texto(t, sizeof t, "Editor - %s%s", nome_curto(), area.modificado ? " *" : "");
    titulo_janela(j->id, t);
    desenhar_barra();
    jan_mostrar_area(j, 0, 0, LARG, ABAS_A);
    registrar("editor: titulo %s\n", t);
}

/* "Compilar e rodar" e "Instalar" só aparecem para programas em C: para um
 * texto qualquer (uma nota, um chaves.txt) o Editor é um editor de texto
 * simples, e os botões de compilar só confundiriam. */
static bool eh_programa_c(void)
{
    size_t n = tam_texto(caminho);
    return n > 2 && caminho[n - 2] == '.' && (caminho[n - 1] == 'c' || caminho[n - 1] == 'C');
}

/* ------------------------------------------------------------ desenho */

#define ABA_X 8
/* Largura de cada aba: até 210, menos quando há muitas. */
static int aba_l(void) { int l = (LARG - ABA_X - 40) / (qtd_docs ? qtd_docs : 1) - 4; return l > 210 ? 210 : l; }
static int aba_x(int i) { return ABA_X + i * (aba_l() + 4); }
static int mais_x(void) { return aba_x(qtd_docs) + 4; }
static const char *const menus[3] = { "Arquivo", "Editar", "Exibir" };
static int menu_x(int i) { int x = 8; for (int k = 0; k < i; k++) x += g_largura_texto(menus[k]) + 22; return x; }
static int menu_l(int i) { return g_largura_texto(menus[i]) + 22; }
/* Botões de compilar (só em C), encostados à direita da faixa dos menus. */
#define INST_L 84
#define COMP_L 156
static int inst_x(void) { return LARG - 10 - INST_L; }
static int comp_x(void) { return inst_x() - 8 - COMP_L; }
#define BOT_Y (ABAS_A + 5)
#define BOT_A 24

static void desenhar_barra(void)
{
    /* Faixa da aba: a aba do arquivo, branca, se emenda com os menus. */
    g_retangulo(j, 0, 0, LARG, ABAS_A, COR_FAIXA);
    int al = aba_l();
    for (int i = 0; i < qtd_docs; i++) {
        int x = aba_x(i);
        bool sel = i == atual;
        if (sel) g_ret_redondo(j, x, 6, al, ABAS_A - 6 + 10, 8, COR_BRANCO, COR_BRANCO);
        else if (i + 1 != atual && i + 1 < qtd_docs) g_retangulo(j, x + al + 1, 13, 1, 16, 0xC9CED4);   /* divisória */
        char nome[32];
        int n = (int)tam_texto(nome_de(i)), cabe = (al - 44) / LETRA_L;
        if (cabe > 30) cabe = 30;
        if (n > cabe) n = cabe < 0 ? 0 : cabe;
        copia_texto(nome, nome_de(i), (size_t)n + 1);
        g_texto(j, x + 14, 13, nome, sel ? COR_TEXTO : COR_TEXTO_FRACO);
        if (docs[i].a.modificado) g_ret_redondo(j, x + al - 22, 17, 8, 8, 4, COR_DESTAQUE, COR_DESTAQUE);   /* não salvo */
        else {                                                     /* "x": fecha a aba */
            uint32_t cx = sel ? COR_TEXTO_FRACO : 0x9AA3AC;
            for (int k = 0; k < 8; k++) {
                g_retangulo(j, x + al - 22 + k, 17 + k, 1, 1, cx);
                g_retangulo(j, x + al - 22 + 7 - k, 17 + k, 1, 1, cx);
            }
        }
    }
    int px = mais_x();                                             /* "+": aba nova */
    g_retangulo(j, px + 6, 20, 12, 2, COR_TEXTO_FRACO);
    g_retangulo(j, px + 11, 15, 2, 12, COR_TEXTO_FRACO);
    /* Menus. */
    g_retangulo(j, 0, ABAS_A, LARG, MENU_A, COR_BRANCO);
    g_retangulo(j, 0, BARRA - 1, LARG, 1, COR_FILETE);
    for (int i = 0; i < 3; i++) {
        if (realce == i) g_ret_redondo(j, menu_x(i) - 2, BOT_Y, menu_l(i), BOT_A, 5, COR_FAIXA, COR_FAIXA);
        g_texto(j, menu_x(i) + 9, ABAS_A + 9, menus[i], COR_TEXTO);
    }
    if (eh_programa_c() && comp_x() > menu_x(2) + menu_l(2) + 8) {
        int cx = comp_x(), ix = inst_x();
        g_ret_redondo(j, cx, BOT_Y, COMP_L, BOT_A, 6, COR_DESTAQUE, COR_DESTAQUE);
        for (int k = 0; k < 7; k++) g_retangulo(j, cx + 12 + k, BOT_Y + 5 + k, 1, 14 - 2 * k, COR_BRANCO);   /* triângulo */
        g_texto(j, cx + 26, BOT_Y + 4, "Compilar e rodar", COR_BRANCO);
        g_ret_redondo(j, ix, BOT_Y, INST_L, BOT_A, 6, COR_FILETE, COR_FILETE);
        g_ret_redondo(j, ix + 1, BOT_Y + 1, INST_L - 2, BOT_A - 2, 5, COR_BRANCO, COR_BRANCO);
        g_texto(j, ix + (INST_L - g_largura_texto("Instalar")) / 2, BOT_Y + 4, "Instalar", COR_TEXTO);
    }
}

/* Linha e coluna do cursor, contadas de 1. */
static void linha_coluna_cursor(int *l, int *c)
{
    *l = 1; *c = 1;
    for (int i = 0; i < area.cursor && i < area.tam; i++) {
        if (area.buf[i] == '\n') { (*l)++; *c = 1; } else (*c)++;
    }
}

static void desenhar_status(bool forcar)
{
    static int ul = -1, uc = -1, ut = -1;
    if (!status_ligado) return;
    int l, c;
    linha_coluna_cursor(&l, &c);
    if (!forcar && l == ul && c == uc && area.tam == ut) return;
    ul = l; uc = c; ut = area.tam;
    int y = ALT - STATUS_A;
    g_retangulo(j, 0, y, LARG, STATUS_A, COR_STATUS);
    g_retangulo(j, 0, y, LARG, 1, COR_FILETE);
    char t[64];
    formatar_texto(t, sizeof t, "Lin %d, Col %d", l, c);
    g_texto(j, 14, y + 5, t, COR_TEXTO_FRACO);
    formatar_texto(t, sizeof t, "%d caracteres", area.tam);
    g_retangulo(j, 150, y + 7, 1, 12, COR_FILETE);
    g_texto(j, 162, y + 5, t, COR_TEXTO_FRACO);
    const char *tipo = eh_programa_c() ? "C   |   Latin-1   |   LF" : "Texto   |   Latin-1   |   LF";
    if (LARG > 420) g_texto(j, LARG - 14 - g_largura_texto(tipo), y + 5, tipo, COR_TEXTO_FRACO);
    jan_mostrar_area(j, 0, y, LARG, STATUS_A);
}

/* A área de texto ocupa o que sobra entre os menus e o status. */
static void posicionar_area(void)
{
    area.x = 0; area.y = BARRA; area.l = LARG; area.a = ALT - BARRA - alt_status();
}

static void redesenhar_tudo(void)
{
    g_retangulo(j, 0, 0, LARG, ALT, COR_BRANCO);
    posicionar_area();
    desenhar_barra();
    area_desenhar(j, &area);
    desenhar_status(true);
    jan_mostrar(j);
}

static void forcar_titulo(void)
{
    modificado_mostrado = !area.modificado;
    atualizar_titulo();
    desenhar_barra();
    jan_mostrar_area(j, 0, 0, LARG, BARRA);
    desenhar_status(true);
}

static bool abrir_arquivo(const char *c)
{
    int fd = abrir(c, ABRIR_LER);
    if (fd < 0) {
        char m[200];
        formatar_texto(m, sizeof m, "Não consegui abrir\n%s\n(%s)", c, nome_erro(fd));
        mensagem("Editor", m);
        return false;
    }
    char *buf = aloca(65536);
    int n = 0, r;
    while (n < 65535 && (r = ler(fd, buf + n, (unsigned)(65535 - n))) > 0) n += r;
    fechar(fd);
    n = (int)utf8_para_latin1(buf, (size_t)n, NULL);   /* textos de fora costumam vir em UTF-8 (D027) */
    area_definir(&area, buf, n);
    libera(buf);
    copia_texto(caminho, c, sizeof caminho);
    registrar("editor: aberto %s (%d bytes)\n", caminho, n);
    forcar_titulo();
    area_desenhar(j, &area);
    return true;
}

static bool salvar_como(void);

static bool salvar(void)
{
    if (!caminho[0]) return salvar_como();
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0 || escrever_tudo(fd, area.buf, (unsigned)area.tam) != area.tam) {
        if (fd >= 0) fechar(fd);
        char m[200];
        formatar_texto(m, sizeof m, "Não consegui salvar\n%s", caminho);
        mensagem("Editor", m);
        return false;
    }
    fechar(fd);
    area.modificado = false;
    registrar("editor: salvo %s (%d bytes)\n", caminho, area.tam);
    forcar_titulo();
    return true;
}

/* ------------------------------------------------------------ compilar (H67) */

/* Roda o cc com a saída num pipe; devolve o código de saída. */
static int rodar_cc(const char *fonte, const char *saida, char *msg, int max)
{
    int p[2];
    if (criar_pipe(p) < 0) { copia_texto(msg, "não consegui criar o pipe", (size_t)max); return -1; }
    int g1 = duplicar(SAIDA, 15), g2 = duplicar(ERROS, 16);   /* g < 0: a saída não estava aberta */
    duplicar(p[1], SAIDA);
    duplicar(p[1], ERROS);
    char *argv[] = { "cc", (char *)fonte, "-o", (char *)saida, NULL };
    int pid = executar("/bin/cc", argv);
    if (g1 >= 0) { duplicar(g1, SAIDA); fechar(g1); } else fechar(SAIDA);
    if (g2 >= 0) { duplicar(g2, ERROS); fechar(g2); } else fechar(ERROS);
    fechar(p[1]);
    int n = 0;
    msg[0] = 0;
    if (pid < 0) { fechar(p[0]); formatar_texto(msg, (size_t)max, "não consegui rodar o cc (%s)", nome_erro(pid)); return -1; }
    /* Lê até o pipe acabar, ou até o cc terminar (se o Editor não tinha a
     * saída de erros aberta, uma ponta do pipe fica com ele e o pipe nunca
     * "acaba": por isso também olha se o cc já terminou). */
    int codigo = 0;
    bool terminou = false;
    for (;;) {
        int d = disponivel(p[0]);
        if (d > 0) {
            char buf[256];
            int k = ler(p[0], buf, sizeof buf);
            for (int i = 0; i < k && n < max - 1; i++) msg[n++] = buf[i];
            msg[n] = 0;
            continue;
        }
        if (d < 0 || terminou) break;
        int c, r = recolher(&c);
        if (r == pid) { terminou = true; codigo = c; continue; }   /* uma última leitura do que sobrou */
        dormir(10);
    }
    fechar(p[0]);
    if (!terminou) esperar(pid, &codigo);
    return codigo;
}

/* Leva o cursor para a linha "arquivo:LINHA:coluna" da mensagem. */
static void ir_para_erro(const char *msg)
{
    const char *p = msg;
    while (*p && *p != ':') p++;                        /* depois do nome do arquivo */
    if (*p != ':') return;
    int linha = texto_para_int(p + 1);
    if (linha <= 0) return;
    int pos = 0;
    for (int l = 1; l < linha && pos < area.tam; pos++) if (area.buf[pos] == '\n') l++;
    area.cursor = pos;
    int vis = area_linhas_visiveis(&area);
    area.topo = linha - 1 - vis / 2 > 0 ? linha - 1 - vis / 2 : 0;
    area_desenhar(j, &area);
}

static void compilar(bool instalar)
{
    if (!caminho[0] || area.modificado) { if (!salvar()) return; }
    char saida[160], msg[2048];
    const char *nome = nome_curto();
    char base[64];
    copia_texto(base, nome, sizeof base);
    size_t t = tam_texto(base);
    if (t > 2 && base[t - 2] == '.' && base[t - 1] == 'c') base[t - 2] = 0;
    if (instalar) {
        info_arquivo_t i;
        /* Pendrive conectado mas não montado: monta o primeiro (como o Arquivos). */
        info_disco_t d;
        for (int k = 0; listar_discos(k, &d) == 1; k++)
            if (d.suportado && !d.montado) { montar(d.nome); break; }
        criar_pasta("/usb/programas");                  /* se já existe, tudo bem */
        if (info_arquivo("/usb/programas", &i) < 0) { mensagem("Instalar", "Conecte o pendrive para instalar o programa."); return; }
        formatar_texto(saida, sizeof saida, "/usb/programas/%s", base);
    } else formatar_texto(saida, sizeof saida, "/tmp/%s", base);
    registrar("editor: compilando %s -> %s", caminho, saida);
    int r = rodar_cc(caminho, saida, msg, sizeof msg);
    if (r != 0) {
        registrar("editor: erro de compilacao (%d)", r);
        ir_para_erro(msg);
        mensagem("Erro ao compilar", msg[0] ? msg : "o compilador falhou sem dizer por quê");
        return;
    }
    if (instalar) {
        char m[220];
        formatar_texto(m, sizeof m, "Instalado em %s.\nO ícone aparece na área de trabalho.", saida);
        registrar("editor: instalado %s", saida);
        mensagem("Instalar", m);
        return;
    }
    /* Programa de texto (o cc marca no e_flags): roda dentro de um Terminal,
     * para a pessoa ver o que ele escreve. Programa com janela: direto. */
    unsigned char cab[40] = { 0 };
    int fd = abrir(saida, ABRIR_LER);
    if (fd >= 0) { ler(fd, cab, sizeof cab); fechar(fd); }
    unsigned flags = (unsigned)cab[36] | ((unsigned)cab[37] << 8) | ((unsigned)cab[38] << 16) | ((unsigned)cab[39] << 24);
    int pid;
    if (flags == ELF_SAGUI_TEXTO) {
        char *argv[] = { "terminal", "-e", saida, NULL };
        pid = executar("/bin/terminal", argv);
    } else {
        char *argv[] = { base, NULL };
        pid = executar(saida, argv);
    }
    registrar("editor: compilou e abriu %s%s (processo %d)", flags == ELF_SAGUI_TEXTO ? "no Terminal " : "", saida, pid);
}

static bool salvar_como(void)
{
    char c[160];
    copia_texto(c, caminho[0] ? caminho : "/tmp/", sizeof c);
    if (!pedir_texto("Salvar como", "Salvar o arquivo em (ex.: /tmp/nota.txt):", c, sizeof c) || !c[0])
        return false;
    copia_texto(caminho, c, sizeof caminho);
    return salvar();
}

/* Antes de jogar o texto fora: pergunta se quer salvar. Devolve false se a
 * pessoa quis salvar e não deu certo (aí não descarta nada). */
static bool pode_descartar(void)
{
    if (!area.modificado) return true;
    char p[120];
    formatar_texto(p, sizeof p, "\"%s\" tem alterações não salvas.\nSalvar antes de fechar?", nome_curto());
    int r = escolher_entre("Editor", p, "Salvar", "Não salvar", "Cancelar");
    registrar("editor: pergunta de salvar: %d", r);
    if (r == 1) return salvar();
    return r == 2;
}

/* Menus da faixa de cima. Os itens de C só aparecem para programas em C. */
static void abrir_menu(int i)
{
    menu_aberto = realce = i;
    desenhar_barra();
    jan_mostrar_area(j, 0, ABAS_A, LARG, MENU_A);
    const char *itens = i == 0 ? (eh_programa_c() ? "Nova aba (Ctrl+N)|Abrir... (Ctrl+O)|Salvar (Ctrl+S)|Salvar como...|Compilar e rodar (Ctrl+R)|Instalar no pendrive|Fechar a aba (Ctrl+W)"
                                                 : "Nova aba (Ctrl+N)|Abrir... (Ctrl+O)|Salvar (Ctrl+S)|Salvar como...|Fechar a aba (Ctrl+W)")
                      : i == 1 ? "Recortar (Ctrl+X)|Copiar (Ctrl+C)|Colar (Ctrl+V)|Selecionar tudo (Ctrl+A)"
                      : status_ligado ? "Esconder a barra de status" : "Mostrar a barra de status";
    jan_menu(j, menu_x(i) - 2, BARRA - 4, itens);
}

static void redesenhar_tudo(void);

static void trocar_aba(int i)
{
    if (i < 0 || i >= qtd_docs) return;
    area.foco = false;
    atual = i;
    area.foco = true;
    modificado_mostrado = !area.modificado;
    atualizar_titulo();
    redesenhar_tudo();
    registrar("editor: aba %d de %d (%s)", atual + 1, qtd_docs, nome_curto());
}

/* Aba nova, vazia. Devolve false quando já há MAX_ABAS. */
static bool nova_aba(void)
{
    if (qtd_docs >= MAX_ABAS) { mensagem("Editor", "Dá para ter até 6 abas abertas.\nFeche uma para abrir outra."); return false; }
    doc_t *d = &docs[qtd_docs];
    memset(d, 0, sizeof *d);
    if (!area_iniciar(&d->a, 0, BARRA, LARG, ALT - BARRA - alt_status())) return false;
    d->a.lisa = true;
    qtd_docs++;
    trocar_aba(qtd_docs - 1);
    return true;
}

/* Fecha a aba i (pergunta antes se há alteração). Devolve true se a
 * última aba fechou (o Editor termina). */
static bool fechar_aba(int i)
{
    if (i != atual) trocar_aba(i);
    if (!pode_descartar()) return false;
    libera(docs[i].a.buf);
    for (int k = i; k + 1 < qtd_docs; k++) docs[k] = docs[k + 1];
    qtd_docs--;
    registrar("editor: aba fechada, ficam %d", qtd_docs);
    if (qtd_docs == 0) return true;
    atual = i < qtd_docs ? i : qtd_docs - 1;
    trocar_aba(atual);
    return false;
}

static void novo(void) { nova_aba(); }

/* A aba atual está vazia e sem nome? Então o arquivo aberto vai nela. */
static bool aba_vazia(void) { return !caminho[0] && area.tam == 0 && !area.modificado; }

static void abrir_pedindo(void)
{
    char c[160] = "/";
    if (!pedir_texto("Abrir", "Caminho do arquivo:", c, sizeof c)) return;
    bool nova = !aba_vazia();
    if (nova && !nova_aba()) return;
    if (!abrir_arquivo(c) && nova) fechar_aba(atual);
}

/* Editar: os mesmos atalhos da área de texto. */
static void tecla_na_area(unsigned t)
{
    evento_t e;
    memset(&e, 0, sizeof e);
    e.tipo = EV_TECLA; e.tecla = t;
    area.foco = true;
    area_tratar(j, &area, &e);
}

static void compilar_e_voltar(bool instalar)
{
    compilar(instalar);
    area.foco = true;
    area_desenhar(j, &area);
    desenhar_status(false);
}

int principal(int argc, char **argv)
{
    j = jan_abrir("Editor - sem nome", LARG, ALT);
    if (!j || !area_iniciar(&docs[0].a, 0, BARRA, LARG, ALT - BARRA - STATUS_A)) return 1;
    qtd_docs = 1;
    area.lisa = true;
    jan_redimensionavel(j, 580, 200);
    area.foco = true;
    if (argc > 1) abrir_arquivo(argv[1]);
    else forcar_titulo();
    redesenhar_tudo();
    registrar("editor: pronto\n");

    evento_t e;
    bool sair = false;
    while (!sair) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (realce >= 0 && (e.tipo == EV_MENU || e.tipo == EV_MOVER || e.tipo == EV_CLIQUE || e.tipo == EV_TECLA)) {
            realce = -1;                                   /* o menu fechou (com ou sem escolha) */
            desenhar_barra();
            jan_mostrar_area(j, 0, ABAS_A, LARG, MENU_A);
        }
        if (e.tipo == EV_FECHAR) {                         /* cada aba com alteração pergunta */
            bool ok = true;
            for (int i = 0; i < qtd_docs && ok; i++)
                if (docs[i].a.modificado) { trocar_aba(i); ok = pode_descartar(); }
            if (ok) break;
            continue;
        }
        if (e.tipo == EV_REDIMENSIONAR) {                   /* H78: a área de texto acompanha a janela */
            jan_novo_tamanho(j, &e);
            larg_jan = j->larg;
            alt_jan = j->alt;
            redesenhar_tudo();
            registrar("editor: tamanho %dx%d", LARG, ALT);
            continue;
        }
        bool c = eh_programa_c();
        if (e.tipo == EV_TECLA && e.tecla == 19) { salvar(); continue; }                 /* Ctrl+S */
        if (e.tipo == EV_TECLA && e.tecla == 14) { novo(); continue; }                   /* Ctrl+N */
        if (e.tipo == EV_TECLA && e.tecla == 15) { abrir_pedindo(); continue; }          /* Ctrl+O */
        if (e.tipo == EV_TECLA && e.tecla == 23) { if (fechar_aba(atual)) break; continue; }   /* Ctrl+W */
        if (e.tipo == EV_TECLA && e.tecla == '\t' && (e.modificadores & 2)) { trocar_aba((atual + 1) % qtd_docs); continue; }   /* Ctrl+Tab */
        if (c && e.tipo == EV_TECLA && e.tecla == 18 && (e.modificadores & 2)) { compilar(false); continue; }   /* Ctrl+R */
        if (e.tipo == EV_CLIQUE && (e.botoes & 1) && e.y < BARRA) {
            if (e.y < ABAS_A) {                              /* abas: trocar, "x" fecha; "+" abre outra */
                int px = mais_x(), al = aba_l();
                if (e.x >= px && e.x < px + 24) { novo(); continue; }
                for (int i = 0; i < qtd_docs; i++) {
                    int x = aba_x(i);
                    if (e.x < x || e.x >= x + al) continue;
                    if (e.x >= x + al - 28 && e.x < x + al - 10) { if (fechar_aba(i)) sair = true; }
                    else if (i != atual) trocar_aba(i);
                    break;
                }
            } else {
                for (int i = 0; i < 3; i++)
                    if (e.x >= menu_x(i) - 2 && e.x < menu_x(i) - 2 + menu_l(i)) { abrir_menu(i); break; }
                if (c && e.y >= BOT_Y && e.y < BOT_Y + BOT_A) {
                    if (e.x >= comp_x() && e.x < comp_x() + COMP_L) compilar_e_voltar(false);
                    else if (e.x >= inst_x() && e.x < inst_x() + INST_L) compilar_e_voltar(true);
                }
            }
            continue;
        }
        /* Botão direito (H81). Numa aba: o que fazer com as abas. No texto:
         * editar o que está ali (os itens sem seleção não fazem nada). */
        if (e.tipo == EV_CLIQUE && (e.botoes & 4) && e.y < ABAS_A) {   /* botão do meio numa aba: fecha */
            for (int i = 0; i < qtd_docs; i++)
                if (e.x >= aba_x(i) && e.x < aba_x(i) + aba_l()) { if (fechar_aba(i)) sair = true; break; }
            continue;
        }
        if (e.tipo == EV_CLIQUE && (e.botoes & 2) && e.y < ABAS_A) {
            aba_do_menu = -1;
            for (int i = 0; i < qtd_docs; i++) if (e.x >= aba_x(i) && e.x < aba_x(i) + aba_l()) aba_do_menu = i;
            menu_aberto = 4;
            jan_menu(j, e.x, e.y, aba_do_menu < 0 ? "Nova aba (Ctrl+N)" : "Nova aba (Ctrl+N)|Fechar a aba (Ctrl+W)|Fechar as outras abas");
            continue;
        }
        if (e.tipo == EV_CLIQUE && (e.botoes & 2) && e.y >= BARRA) {
            menu_aberto = 3;
            jan_menu(j, e.x, e.y, c ? "Recortar (Ctrl+X)|Copiar (Ctrl+C)|Colar (Ctrl+V)|Selecionar tudo (Ctrl+A)|Compilar e rodar (Ctrl+R)"
                                    : "Recortar (Ctrl+X)|Copiar (Ctrl+C)|Colar (Ctrl+V)|Selecionar tudo (Ctrl+A)");
            continue;
        }
        if (e.tipo == EV_MENU) {
            int m = menu_aberto;
            menu_aberto = -1;
            registrar("editor: menu %d %u", m, e.tecla);
            if (m == 3) {
                static const unsigned teclas[4] = { 24, 3, 22, 1 };
                if (e.tecla < 4) { tecla_na_area(teclas[e.tecla]); atualizar_titulo(); }
                else if (e.tecla == 4) compilar_e_voltar(false);
            } else if (m == 4) {
                if (e.tecla == 0) novo();
                else if (e.tecla == 1 && aba_do_menu >= 0) { if (fechar_aba(aba_do_menu)) sair = true; }
                else if (e.tecla == 2 && aba_do_menu >= 0) {   /* fecha as outras, de trás para a frente */
                    int fica = aba_do_menu;
                    for (int i = qtd_docs - 1; i >= 0; i--) {
                        if (i == fica) continue;
                        int antes = qtd_docs;
                        fechar_aba(i);
                        if (qtd_docs == antes) break;                 /* não deu para descartar: para aqui */
                        if (i < fica) fica--;
                    }
                    trocar_aba(fica);
                }
            } else if (m == 0) {
                unsigned t = e.tecla;
                if (!c && t >= 4) t += 2;                             /* sem os itens de C */
                if (t == 0) novo();
                else if (t == 1) abrir_pedindo();
                else if (t == 2) salvar();
                else if (t == 3) salvar_como();
                else if (t == 4 || t == 5) compilar_e_voltar(t == 5);
                else if (t == 6 && fechar_aba(atual)) sair = true;
            } else if (m == 1) {
                static const unsigned teclas[4] = { 24, 3, 22, 1 };
                if (e.tecla < 4) tecla_na_area(teclas[e.tecla]);
                atualizar_titulo();
            } else if (m == 2) {
                status_ligado = !status_ligado;
                redesenhar_tudo();
            }
            desenhar_status(false);
            continue;
        }
        area_tratar(j, &area, &e);
        atualizar_titulo();
        desenhar_status(false);
    }
    registrar("editor: fechado\n");
    jan_fechar(j);
    return 0;
}
