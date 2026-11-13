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
 * programa ganha um ícone na área de trabalho. */
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
    char *argv[] = { base, NULL };
    int pid = executar(saida, argv);
    registrar("editor: compilou e abriu %s (processo %d)", saida, pid);
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
    botao_t bt[6] = {
        { 8, 7, 64, 26, "Novo", false }, { 78, 7, 64, 26, "Abrir", false },
        { 148, 7, 70, 26, "Salvar", false }, { 224, 7, 104, 26, "Salvar como", false },
        { 340, 7, 142, 26, "Compilar e rodar", false }, { 488, 7, 80, 26, "Instalar", false },
    };
    jan_redimensionavel(j, 580, 200);
    g_retangulo(j, 0, 0, LARG, BARRA, 0xE6E9EC);
    g_retangulo(j, 0, BARRA - 1, LARG, 1, COR_BORDA);
    if (LARG >= 690) rotulo(j, 580, 12, "Ctrl+S salva", COR_TEXTO_FRACO);
    for (int i = 0; i < 6; i++) botao_desenhar(j, &bt[i]);
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
            if (LARG >= 690) rotulo(j, 580, 12, "Ctrl+S salva", COR_TEXTO_FRACO);
            for (int i = 0; i < 6; i++) botao_desenhar(j, &bt[i]);
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
        if (botao_tratar(j, &bt[4], &e)) { compilar(false); area.foco = true; area_desenhar(j, &area); continue; }
        if (botao_tratar(j, &bt[5], &e)) { compilar(true); area.foco = true; area_desenhar(j, &area); continue; }
        if (e.tipo == EV_TECLA && e.tecla == 18 && (e.modificadores & 2)) { compilar(false); continue; }   /* Ctrl+R */
        area_tratar(j, &area, &e);
        atualizar_titulo();
    }
    registrar("editor: fechado\n");
    jan_fechar(j);
    return 0;
}
