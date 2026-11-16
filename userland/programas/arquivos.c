/* arquivos [pasta] - o gerenciador de arquivos do SaguiOS (H23).
 *
 * Mostra uma pasta como lista com ícones (pastas primeiro). Duplo clique:
 * pasta entra, arquivo de texto abre no Editor, programa executa. Botões
 * para voltar, criar pasta e apagar (com confirmação). */
#include "sagui.h"
#include "gui.h"

static int larg_jan = 720, alt_jan = 440;   /* a janela pode mudar de tamanho (H78) */
#define LARG larg_jan
#define LATERAL 140   /* lateral com "SaguiOS" e "Pendrive" (H34) */
#define ALT  alt_jan
#define MAX_ITENS 128

static jan_t *j;
static char pasta[160] = "/";
static char nomes[MAX_ITENS][64];
static uint8_t tipos[MAX_ITENS];
static uint32_t tamanhos[MAX_ITENS];
static lista_t lista;
static uint8_t marc[MAX_ITENS];   /* H91: itens marcados (laço, Ctrl, Shift) */
static info_disco_t pendrive;
static bool tem_pendrive;
static botao_t bt_lado_raiz = { 8, 76, LATERAL - 16, 28, "SaguiOS", false };
static botao_t bt_lado_pen = { 8, 112, LATERAL - 16, 28, "Pendrive", false };
static botao_t bt_remover = { 8, 148, LATERAL - 16, 44, "", false };

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
    case 'P': return 0x202428;
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

static void desenhar_topo(void)
{
    g_retangulo(j, 0, 40, LARG, 26, COR_FUNDO);
    char t[200];
    formatar_texto(t, sizeof t, "Pasta: %s", pasta);
    g_texto_n(j, 10, 46, t, (LARG - 20) / LETRA_L, COR_TEXTO);
    jan_mostrar_area(j, 0, 40, LARG, 26);
}

static void desenhar_rodape(const char *msg)
{
    g_retangulo(j, 0, ALT - 24, LARG, 24, 0xE6E9EC);
    g_retangulo(j, 0, ALT - 24, LARG, 1, COR_BORDA);
    g_texto_n(j, 10, ALT - 20, msg, (LARG - 20) / LETRA_L, COR_TEXTO_FRACO);
    jan_mostrar_area(j, 0, ALT - 24, LARG, 24);
}

/* Lê a pasta: primeiro as pastas, depois o resto (o kernel já devolve em
 * ordem alfabética). */
static void carregar(void)
{
    entrada_pasta_t e;
    int n = 0;
    for (int passo = 0; passo < 2; passo++)
        for (int i = 0; n < MAX_ITENS && listar(pasta, i, &e) == 1; i++) {
            bool eh_pasta = e.tipo == TIPO_PASTA;
            if ((passo == 0) != eh_pasta) continue;
            copia_texto(nomes[n], e.nome, 64);
            tipos[n] = (uint8_t)e.tipo;
            tamanhos[n] = e.tamanho;
            n++;
        }
    lista.qtd = n;
    lista.sel = n ? 0 : -1;
    for (int i = 0; i < MAX_ITENS; i++) marc[i] = 0;
    if (n) marc[0] = 1;
    lista.editando = lista.laco = false;
    lista.topo = 0;
    desenhar_topo();
    lista_desenhar(j, &lista);
    char msg[64];
    formatar_texto(msg, sizeof msg, "%d itens. Duplo clique para abrir.", n);
    desenhar_rodape(msg);
    registrar("arquivos: pasta %s (%d itens)", pasta, n);
}

static void entrar(const char *nova)
{
    info_arquivo_t inf;
    if (info_arquivo(nova, &inf) < 0 || inf.tipo != TIPO_PASTA) return;
    copia_texto(pasta, nova, sizeof pasta);
    carregar();
}

/* A lateral mostra o pendrive (se houver) e, montado, o botão de remover. */
static void desenhar_lateral(void)
{
    g_retangulo(j, 0, 66, LATERAL, ALT - 66 - 24, 0xEAEDF0);
    g_retangulo(j, LATERAL - 1, 66, 1, ALT - 66 - 24, COR_BORDA);
    botao_desenhar(j, &bt_lado_raiz);
    if (tem_pendrive) {
        static char rotulo[24];
        copia_texto(rotulo, pendrive.rotulo[0] ? pendrive.rotulo : "Pendrive", sizeof rotulo);
        bt_lado_pen.texto = rotulo;
        botao_desenhar(j, &bt_lado_pen);
        if (pendrive.montado) {
            bt_remover.texto = "";
            botao_desenhar(j, &bt_remover);
            g_texto(j, 8 + 12, 148 + 6, "Remover com", COR_TEXTO);
            g_texto(j, 8 + 20, 148 + 22, "segurança", COR_TEXTO);
        }
    }
    jan_mostrar_area(j, 0, 66, LATERAL, ALT - 66 - 24);
}

/* Confere os pendrives; devolve true se algo mudou. */
static bool atualizar_pendrive(void)
{
    info_disco_t d;
    bool achou = false;
    for (int i = 0; listar_discos(i, &d) == 1; i++)
        if (d.suportado) { achou = true; break; }
    bool mudou = achou != tem_pendrive || (achou && (d.montado != pendrive.montado || compara_texto(d.nome, pendrive.nome) != 0));
    tem_pendrive = achou;
    if (achou) pendrive = d;
    return mudou;
}

static void voltar(void)
{
    if (compara_texto(pasta, "/") == 0) return;
    char *b = acha_ultimo_char(pasta, '/');
    if (b == pasta) pasta[1] = 0; else *b = 0;
    carregar();
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
        { ".png", "/bin/cipo" }, { ".jpg", "/bin/cipo" }, { ".jpeg", "/bin/cipo" }, { ".gif", "/bin/cipo" },
        { ".webp", "/bin/cipo" }, { ".svg", "/bin/cipo" }, { ".mp4", "/bin/video" },
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

static void abrir_item(int i)
{
    if (i < 0 || i >= lista.qtd) return;
    char c[200];
    caminho_de(i, c, sizeof c);
    if (tipos[i] == TIPO_PASTA) { entrar(c); return; }
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

/* ------------------------------------------------------------ criar e renomear na linha (H91)
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
    lista_desenhar(j, &lista);
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
    if (!*ini || compara_texto(ini, nomes[i]) == 0) { lista_desenhar(j, &lista); return; }
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
        lista_desenhar(j, &lista);
        return;
    }
    registrar("arquivos: renomeado %s -> %s", de, para);
    char guardado[64];
    copia_texto(guardado, ini, sizeof guardado);
    carregar();
    selecionar(indice_de(guardado));
}

/* Os itens a que uma ação se aplica: os marcados ou, sem nenhum, o da
 * seleção. */
static int alvos(int *saida)
{
    int n = 0;
    for (int i = 0; i < lista.qtd; i++) if (marc[i]) saida[n++] = i;
    if (!n && lista.sel >= 0 && lista.sel < lista.qtd) saida[n++] = lista.sel;
    return n;
}

static void apagar_item(void)
{
    static int itens[MAX_ITENS];
    int n = alvos(itens);
    if (!n) return;
    char pergunta[200];
    if (n == 1) formatar_texto(pergunta, sizeof pergunta, "Tem certeza que quer apagar\n%s%s?", nomes[itens[0]], tipos[itens[0]] == TIPO_PASTA ? "/" : "");
    else formatar_texto(pergunta, sizeof pergunta, "Tem certeza que quer apagar\nos %d itens marcados?", n);
    if (!confirmar("Apagar", pergunta)) return;
    for (int k = 0; k < n; k++) {
        int i = itens[k];
        char c[200];
        caminho_de(i, c, sizeof c);
        int r = tipos[i] == TIPO_PASTA ? remover_pasta(c) : apagar(c);
        if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui apagar %s:\n%s", nomes[i], nome_erro(r)); mensagem("Arquivos", m); continue; }
        registrar("arquivos: apagado %s", c);
    }
    carregar();
}

/* ------------------------------------------------------------ menu do botão direito (H91)
 *
 * Num item: abrir, abrir no Editor, renomear, apagar e os "Novo". No vazio:
 * só os "Novo", atualizar e voltar. Com vários marcados, o que vale para um
 * só (abrir, renomear) some. A escolha volta pelo índice; "acoes" diz qual é. */
enum { AC_ABRIR, AC_EDITOR, AC_RENOMEAR, AC_APAGAR, AC_PASTA, AC_TEXTO, AC_PAGINA, AC_ATUALIZAR, AC_VOLTAR };
static int acoes[16], qtd_acoes;

static void acao(char *itens, size_t max, const char *texto, int a)
{
    if (qtd_acoes) junta_texto(itens, "|", max);
    junta_texto(itens, texto, max);
    acoes[qtd_acoes++] = a;
}

static void menu_de_contexto(int x, int y)
{
    static char itens[300];
    itens[0] = 0;
    qtd_acoes = 0;
    int marcados = lista_qtd_marcados(&lista);
    bool no_item = lista.sel >= 0 && lista.sel < lista.qtd && marcados > 0;
    if (no_item && marcados == 1) {
        /* O primeiro item diz com que programa abre (pela extensão). */
        const char *n = nomes[lista.sel];
        bool arquivo = tipos[lista.sel] != TIPO_PASTA && tipos[lista.sel] != TIPO_PROGRAMA;
        const char *prog = arquivo ? programa_para(n) : NULL;
        const char *primeiro = !prog ? "Abrir"
            : compara_texto(prog, "/bin/tocador") == 0 ? "Abrir no Tocador"
            : compara_texto(prog, "/bin/cipo") == 0 ? "Abrir no Cipó"
            : compara_texto(prog, "/bin/video") == 0 ? "Abrir no Vídeo" : "Abrir no Editor";
        acao(itens, sizeof itens, primeiro, AC_ABRIR);
        if (arquivo && compara_texto(primeiro, "Abrir no Editor") != 0) acao(itens, sizeof itens, "Abrir no Editor", AC_EDITOR);
        acao(itens, sizeof itens, "Renomear", AC_RENOMEAR);
        acao(itens, sizeof itens, "Apagar...", AC_APAGAR);
    } else if (no_item) {
        char t[40];
        formatar_texto(t, sizeof t, "Apagar os %d itens...", marcados);
        acao(itens, sizeof itens, t, AC_APAGAR);
    }
    acao(itens, sizeof itens, "Nova pasta", AC_PASTA);
    acao(itens, sizeof itens, "Novo arquivo de texto", AC_TEXTO);
    acao(itens, sizeof itens, "Nova página HTML", AC_PAGINA);
    if (!no_item) acao(itens, sizeof itens, "Atualizar", AC_ATUALIZAR);
    acao(itens, sizeof itens, "Voltar", AC_VOLTAR);
    jan_menu(j, x, y, itens);
}

int principal(int argc, char **argv)
{
    if (argc > 1) copia_texto(pasta, argv[1], sizeof pasta);
    j = jan_abrir("Arquivos", LARG, ALT);
    if (!j) return 1;
    botao_t bt[3] = {
        { 8, 7, 90, 26, "Voltar", false }, { 106, 7, 110, 26, "Nova pasta", false }, { 224, 7, 90, 26, "Apagar", false },
    };
    jan_redimensionavel(j, 480, 300);
    g_retangulo(j, 0, 0, LARG, 40, 0xE6E9EC);
    g_retangulo(j, 0, 39, LARG, 1, COR_BORDA);
    for (int i = 0; i < 3; i++) botao_desenhar(j, &bt[i]);
    lista = (lista_t){ .x = LATERAL + 6, .y = 70, .l = LARG - LATERAL - 14, .a = ALT - 70 - 30, .itens = nomes,
                       .qtd = 0, .sel = -1, .topo = 0, .icone = icone, .marc = marc };

    atualizar_pendrive();
    desenhar_lateral();
    carregar();
    jan_mostrar(j);
    registrar("arquivos: pronto");

    evento_t e;
    unsigned ultimo = tempo_ms();
    for (;;) {
        if (proximo_evento(j->id, &e, false) != 1) {
            /* Sem evento: de vez em quando confere se um pendrive entrou ou saiu. */
            if (tempo_ms() - ultimo > 700) {
                ultimo = tempo_ms();
                if (atualizar_pendrive()) {
                    desenhar_lateral();
                    info_arquivo_t inf;
                    if (comeca_com(pasta, "/usb") && info_arquivo(pasta, &inf) < 0) { copia_texto(pasta, "/", sizeof pasta); carregar(); }
                }
            }
            dormir(50);
            continue;
        }
        int c;
        while (recolher(&c) > 0) ;   /* programas abertos daqui que já terminaram */
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_REDIMENSIONAR) {                   /* H78: a lista acompanha a janela */
            jan_novo_tamanho(j, &e);
            larg_jan = j->larg;
            alt_jan = j->alt;
            g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
            g_retangulo(j, 0, 0, LARG, 40, 0xE6E9EC);
            g_retangulo(j, 0, 39, LARG, 1, COR_BORDA);
            for (int i = 0; i < 3; i++) botao_desenhar(j, &bt[i]);
            lista.l = LARG - LATERAL - 14;
            lista.a = ALT - 70 - 30;
            desenhar_lateral();
            carregar();
            jan_mostrar(j);
            registrar("arquivos: tamanho %dx%d", LARG, ALT);
            continue;
        }
        /* H91: com um nome sendo editado na linha, o teclado e os cliques vão
         * primeiro para a lista (um clique fora termina a edição). */
        if (lista.editando && (e.tipo == EV_TECLA || e.tipo == EV_CLIQUE)) {
            int r = lista_tratar(j, &lista, &e);
            if (r == LISTA_EDITOU) terminar_renomear();
            else if (r == LISTA_DESISTIU) desenhar_rodape("Nome mantido.");
            if (e.tipo == EV_TECLA || r == LISTA_NADA) continue;
        }
        if (botao_tratar(j, &bt_lado_raiz, &e)) { copia_texto(pasta, "/", sizeof pasta); carregar(); }
        if (tem_pendrive && botao_tratar(j, &bt_lado_pen, &e)) {
            int r = pendrive.montado ? 0 : montar(pendrive.nome);
            if (r < 0) { char m[120]; formatar_texto(m, sizeof m, "Não consegui montar o pendrive:\n%s", nome_erro(r)); mensagem("Arquivos", m); }
            else { atualizar_pendrive(); desenhar_lateral(); copia_texto(pasta, "/usb", sizeof pasta); carregar(); registrar("arquivos: pendrive aberto"); }
        }
        if (tem_pendrive && pendrive.montado && botao_tratar(j, &bt_remover, &e)) {
            int r = desmontar(pendrive.nome);
            registrar("arquivos: remover com seguranca -> %d", r);
            if (comeca_com(pasta, "/usb")) { copia_texto(pasta, "/", sizeof pasta); carregar(); }
            atualizar_pendrive();
            desenhar_lateral();
            mensagem("Arquivos", r < 0 ? "Não consegui desmontar o pendrive." : "Pode remover o pendrive.");
        }
        if (botao_tratar(j, &bt[0], &e)) voltar();
        if (botao_tratar(j, &bt[1], &e)) criar_novo(NOVO_PASTA);
        if (botao_tratar(j, &bt[2], &e)) apagar_item();
        if (e.tipo == EV_TECLA && e.tecla == '\b') { voltar(); continue; }
        if (e.tipo == EV_TECLA && e.tecla == TECLA_DELETE) { apagar_item(); continue; }
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
            }
            continue;
        }
        int r = lista_tratar(j, &lista, &e);
        if (e.tipo == EV_CLIQUE && (e.botoes & 2)) {
            menu_de_contexto(e.x, e.y);
            continue;
        }
        if (r == LISTA_EDITOU) { terminar_renomear(); continue; }
        if (r == LISTA_ATIVOU) abrir_item(lista.sel);
        else if (r == LISTA_SELECIONOU && lista_qtd_marcados(&lista) > 1) {
            char msg[64];
            formatar_texto(msg, sizeof msg, "%d itens marcados.", lista_qtd_marcados(&lista));
            desenhar_rodape(msg);
            registrar("arquivos: %d marcados", lista_qtd_marcados(&lista));
        } else if (r == LISTA_SELECIONOU && lista.sel >= 0) {
            char msg[100];
            if (tipos[lista.sel] == TIPO_PASTA) formatar_texto(msg, sizeof msg, "%s/ (pasta)", nomes[lista.sel]);
            else formatar_texto(msg, sizeof msg, "%s: %u bytes%s", nomes[lista.sel], tamanhos[lista.sel],
                                tipos[lista.sel] == TIPO_PROGRAMA ? " (programa)" : "");
            desenhar_rodape(msg);
        }
    }
    jan_fechar(j);
    return 0;
}
