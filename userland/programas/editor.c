/* editor [arquivo] - o Editor de texto do SaguiOS (H25).
 *
 * Uma barra de botões (Novo, Abrir, Salvar, Salvar como) e a área de texto
 * do kit gráfico. Ctrl+S salva. O título mostra o nome do arquivo e um "*"
 * quando há alteração não salva; fechar com alterações pergunta se quer
 * salvar. O texto é Latin-1, como em todo o sistema. */
#include "sagui.h"
#include "gui.h"

/* Tamanho inicial; a janela pode mudar (H78). */
static int larg_jan = 700, alt_jan = 480;
#define LARG larg_jan
#define ALT  alt_jan
#define BARRA 40

static jan_t *j;
static area_t area;
static char caminho[160];
static bool modificado_mostrado = true;

static const char *nome_curto(void)
{
    if (!caminho[0]) return "sem nome";
    const char *b = acha_ultimo_char(caminho, '/');
    return b ? b + 1 : caminho;
}

static void atualizar_titulo(void)
{
    if (modificado_mostrado == area.modificado) return;
    modificado_mostrado = area.modificado;
    char t[96];
    formatar_texto(t, sizeof t, "Editor - %s%s", nome_curto(), area.modificado ? " *" : "");
    titulo_janela(j->id, t);
    registrar("editor: titulo %s\n", t);
}

static void forcar_titulo(void)
{
    modificado_mostrado = !area.modificado;
    atualizar_titulo();
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
    if (confirmar("Editor", "O texto tem alterações não salvas.\nSalvar antes?"))
        return salvar();
    return true;
}

int principal(int argc, char **argv)
{
    j = jan_abrir("Editor - sem nome", LARG, ALT);
    if (!j || !area_iniciar(&area, 6, BARRA + 4, LARG - 12, ALT - BARRA - 10)) return 1;
    botao_t bt[4] = {
        { 8, 7, 80, 26, "Novo", false }, { 96, 7, 80, 26, "Abrir", false },
        { 184, 7, 80, 26, "Salvar", false }, { 272, 7, 110, 26, "Salvar como", false },
    };
    jan_redimensionavel(j, 420, 200);
    g_retangulo(j, 0, 0, LARG, BARRA, 0xE6E9EC);
    g_retangulo(j, 0, BARRA - 1, LARG, 1, COR_BORDA);
    rotulo(j, 400, 12, "Ctrl+S salva", COR_TEXTO_FRACO);
    for (int i = 0; i < 4; i++) botao_desenhar(j, &bt[i]);
    area.foco = true;
    if (argc > 1) abrir_arquivo(argv[1]);
    else forcar_titulo();
    area_desenhar(j, &area);
    jan_mostrar(j);
    registrar("editor: pronto\n");

    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) {
            if (pode_descartar()) break;
            continue;
        }
        if (e.tipo == EV_REDIMENSIONAR) {                   /* H78: a área de texto acompanha a janela */
            jan_novo_tamanho(j, &e);
            larg_jan = j->larg;
            alt_jan = j->alt;
            g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
            g_retangulo(j, 0, 0, LARG, BARRA, 0xE6E9EC);
            g_retangulo(j, 0, BARRA - 1, LARG, 1, COR_BORDA);
            rotulo(j, 400, 12, "Ctrl+S salva", COR_TEXTO_FRACO);
            for (int i = 0; i < 4; i++) botao_desenhar(j, &bt[i]);
            area.l = LARG - 12;
            area.a = ALT - BARRA - 10;
            area_desenhar(j, &area);
            jan_mostrar(j);
            registrar("editor: tamanho %dx%d", LARG, ALT);
            continue;
        }
        if (e.tipo == EV_TECLA && e.tecla == 19) {          /* Ctrl+S */
            salvar();
            continue;
        }
        if (botao_tratar(j, &bt[0], &e) && pode_descartar()) {
            area_definir(&area, "", 0);
            caminho[0] = 0;
            forcar_titulo();
            area_desenhar(j, &area);
        }
        if (botao_tratar(j, &bt[1], &e) && pode_descartar()) {
            char c[160] = "/";
            if (pedir_texto("Abrir", "Caminho do arquivo:", c, sizeof c)) abrir_arquivo(c);
        }
        if (botao_tratar(j, &bt[2], &e)) salvar();
        if (botao_tratar(j, &bt[3], &e)) salvar_como();
        area_tratar(j, &area, &e);
        atualizar_titulo();
    }
    registrar("editor: fechado\n");
    jan_fechar(j);
    return 0;
}
