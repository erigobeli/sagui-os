/* arquivos [pasta] - o gerenciador de arquivos do SaguiOS (H23).
 *
 * Mostra uma pasta como lista com ícones (pastas primeiro). Duplo clique:
 * pasta entra, arquivo de texto abre no Editor, programa executa. Botões
 * para voltar, criar pasta e apagar (com confirmação). */
#include "sagui.h"
#include "gui.h"

#define LARG_INICIO 720
#define LARG (j->larg)
#define LATERAL 140   /* lateral com "SaguiOS" e "Pendrive" (H34) */
#define ALT_INICIO  440
#define ALT  (j->alt)
#define MAX_ITENS 128

static jan_t *j;
static char pasta[160] = "/";
static char nomes[MAX_ITENS][64];
static uint8_t tipos[MAX_ITENS];
static uint32_t tamanhos[MAX_ITENS];
static uint8_t marcas[MAX_ITENS];
static lista_t lista;
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
    default:  return 0xFF000000;
    }
}

static void icone(jan_t *jj, int x, int y, int i)
{
    const char *const *d = tipos[i] == TIPO_PASTA ? ic_pasta : tipos[i] == TIPO_PROGRAMA ? ic_programa : ic_arquivo;
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
            if (e.nome[0] == '.') continue;   /* lixeira e área de transferência */
            bool eh_pasta = e.tipo == TIPO_PASTA;
            if ((passo == 0) != eh_pasta) continue;
            copia_texto(nomes[n], e.nome, 64);
            tipos[n] = (uint8_t)e.tipo;
            tamanhos[n] = e.tamanho;
            n++;
        }
    memset(marcas, 0, sizeof marcas);
    lista.qtd = n;
    lista.sel = n ? 0 : -1;
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

static void abrir_item(int i)
{
    if (i < 0 || i >= lista.qtd) return;
    char c[200];
    caminho_de(i, c, sizeof c);
    if (tipos[i] == TIPO_PASTA) { entrar(c); return; }
    char *argv[3];
    int pid;
    if (tipos[i] == TIPO_PROGRAMA) {
        argv[0] = nomes[i]; argv[1] = NULL;
        pid = executar(c, argv);
        registrar("arquivos: executando %s (pid %d)", c, pid);
    } else {
        argv[0] = "editor"; argv[1] = c; argv[2] = NULL;
        pid = executar("/bin/editor", argv);
        registrar("arquivos: abrindo no editor %s (pid %d)", c, pid);
    }
    char msg[200];
    formatar_texto(msg, sizeof msg, pid > 0 ? "Abrindo %s..." : "Não consegui abrir %s", nomes[i]);
    desenhar_rodape(msg);
}

static void nova_pasta(void)
{
    char nome[64] = "";
    if (!pedir_texto("Nova pasta", "Nome da nova pasta:", nome, sizeof nome) || !nome[0]) return;
    char c[200];
    copia_texto(c, pasta, sizeof c);
    if (compara_texto(pasta, "/") != 0) junta_texto(c, "/", sizeof c);
    junta_texto(c, nome, sizeof c);
    int r = criar_pasta(c);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui criar a pasta:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
    registrar("arquivos: pasta criada %s", c);
    carregar();
}

#define LIXEIRA "/.lixeira"
#define ARQ_COPIA "/tmp/.copia"

static bool na_lixeira(void) { return compara_texto(pasta, LIXEIRA) == 0; }
static bool marcado(int i) { return marcas[i] || i == lista.sel; }

static int qtd_marcados(void)
{
    int n = 0;
    for (int i = 0; i < lista.qtd; i++) n += marcado(i);
    return n;
}

static bool existe(const char *c)
{
    info_arquivo_t inf;
    return info_arquivo(c, &inf) >= 0;
}

/* Tira tudo, de dentro para fora. */
static int remover_tudo(const char *c)
{
    entrada_pasta_t e;
    info_arquivo_t inf;
    int r = info_arquivo(c, &inf);
    if (r < 0) return r;
    if (inf.tipo != TIPO_PASTA) return apagar(c);
    while (listar(c, 0, &e) == 1) {
        char f[200];
        formatar_texto(f, sizeof f, "%s/%s", c, e.nome);
        r = remover_tudo(f);
        if (r < 0) return r;
    }
    return remover_pasta(c);
}

static int copiar_tudo(const char *de, const char *para)
{
    info_arquivo_t inf;
    int r = info_arquivo(de, &inf);
    if (r < 0) return r;
    if (inf.tipo == TIPO_PASTA) {
        entrada_pasta_t e;
        r = criar_pasta(para);
        if (r < 0) return r;
        for (int i = 0; listar(de, i, &e) == 1; i++) {
            char a[200], b[200];
            formatar_texto(a, sizeof a, "%s/%s", de, e.nome);
            formatar_texto(b, sizeof b, "%s/%s", para, e.nome);
            r = copiar_tudo(a, b);
            if (r < 0) return r;
        }
        return 0;
    }
    int o = abrir(de, ABRIR_LER);
    if (o < 0) return o;
    int d = abrir(para, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (d < 0) { fechar(o); return d; }
    static char b[4096];
    while ((r = ler(o, b, sizeof b)) > 0) {
        int w = escrever_tudo(d, b, (unsigned)r);
        if (w < 0) { r = w; break; }
    }
    fechar(o);
    fechar(d);
    return r < 0 ? r : 0;
}

static const char *nome_de(const char *c)
{
    const char *b = acha_ultimo_char(c, '/');
    return b ? b + 1 : c;
}

/* "um.txt" que já existe vira "um (2).txt". */
static void nome_livre(const char *pasta_d, const char *nome, char *saida, int max)
{
    const char *ponto = acha_ultimo_char(nome, '.');
    int base = ponto && ponto != nome ? (int)(ponto - nome) : (int)tam_texto(nome);
    formatar_texto(saida, (size_t)max, "%s/%s", compara_texto(pasta_d, "/") ? pasta_d : "", nome);
    for (int n = 2; existe(saida) && n < 100; n++) {
        char curto[64];
        copia_texto(curto, nome, (size_t)base + 1);
        formatar_texto(saida, (size_t)max, "%s/%s (%d)%s", compara_texto(pasta_d, "/") ? pasta_d : "", curto, n, nome + base);
    }
}

/* A lixeira fica na RAM; o pendrive não tem lixeira: lá, apagar é de vez. */
static int para_lixeira(const char *c)
{
    char d[200], info[230];
    nome_livre(LIXEIRA, nome_de(c), d, sizeof d);
    int r = renomear(c, d);
    if (r < 0) return r;
    formatar_texto(info, sizeof info, LIXEIRA "/.de-%s", nome_de(d));
    int fd = abrir(info, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd >= 0) { escrever_tudo(fd, c, (unsigned)tam_texto(c)); fechar(fd); }
    registrar("arquivos: lixeira %s", c);
    return 0;
}

static void apagar_item(void)
{
    int total = qtd_marcados();
    if (!total) return;
    bool de_vez = na_lixeira() || comeca_com(pasta, "/usb");
    char c[200], pergunta[200];
    int i0 = lista.sel;
    if (total > 1) formatar_texto(pergunta, sizeof pergunta, "%s %d itens%s?", de_vez ? "Apagar de vez" : "Mandar para a lixeira", total, "");
    else formatar_texto(pergunta, sizeof pergunta, "%s\n%s%s?", de_vez ? "Apagar de vez" : "Mandar para a lixeira", nomes[i0], tipos[i0] == TIPO_PASTA ? "/" : "");
    if (!confirmar("Apagar", pergunta)) return;
    for (int i = 0; i < lista.qtd; i++) {
        if (!marcado(i)) continue;
        caminho_de(i, c, sizeof c);
        int r = de_vez ? remover_tudo(c) : para_lixeira(c);
        if (r < 0) { char m[240]; formatar_texto(m, sizeof m, "Não consegui apagar %s:\n%s", nomes[i], nome_erro(r)); mensagem("Arquivos", m); break; }
        registrar("arquivos: apagado %s", c);
    }
    carregar();
}

static void restaurar_item(void)
{
    for (int i = 0; i < lista.qtd; i++) {
        if (!marcado(i)) continue;
        char c[200], info[230], origem[200];
        caminho_de(i, c, sizeof c);
        formatar_texto(info, sizeof info, LIXEIRA "/.de-%s", nomes[i]);
        int fd = abrir(info, ABRIR_LER), n = 0;
        if (fd >= 0) { n = ler(fd, origem, sizeof origem - 1); fechar(fd); }
        if (n <= 0) continue;
        origem[n] = 0;
        if (renomear(c, origem) >= 0) { apagar(info); registrar("arquivos: restaurado %s", origem); }
    }
    carregar();
}

/* Área de transferência: um arquivo com a operação e os caminhos, assim outra
 * janela do Arquivos também consegue colar. */
static void copiar_itens(bool recortar)
{
    int fd = abrir(ARQ_COPIA, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR), n = 0;
    if (fd < 0) return;
    escrever_tudo(fd, recortar ? "X\n" : "C\n", 2);
    for (int i = 0; i < lista.qtd; i++) {
        if (!marcado(i)) continue;
        char c[200];
        caminho_de(i, c, sizeof c);
        escrever_tudo(fd, c, (unsigned)tam_texto(c));
        escrever_tudo(fd, "\n", 1);
        n++;
    }
    fechar(fd);
    registrar("arquivos: copiado %d itens", n);
    char msg[64];
    formatar_texto(msg, sizeof msg, "%d itens %s.", n, recortar ? "recortados" : "copiados");
    desenhar_rodape(msg);
}

static void colar_itens(void)
{
    static char b[8192];
    int fd = abrir(ARQ_COPIA, ABRIR_LER);
    if (fd < 0) return;
    int n = ler(fd, b, sizeof b - 1);
    fechar(fd);
    if (n < 2) return;
    b[n] = 0;
    bool recortar = b[0] == 'X';
    for (char *l = b + 2; *l;) {
        char *f = acha_char(l, '\n');
        if (!f) break;
        *f = 0;
        char d[200];
        nome_livre(pasta, nome_de(l), d, sizeof d);
        int r = recortar ? renomear(l, d) : copiar_tudo(l, d);
        if (r == ERRO_ACESSO && recortar) {        /* entre a RAM e o pendrive */
            r = copiar_tudo(l, d);
            if (r >= 0) r = remover_tudo(l);
        }
        if (r < 0) { char m[240]; formatar_texto(m, sizeof m, "Não consegui colar %s:\n%s", nome_de(l), nome_erro(r)); mensagem("Arquivos", m); break; }
        registrar("arquivos: colado %s", d);
        l = f + 1;
    }
    if (recortar) apagar(ARQ_COPIA);
    carregar();
}

static botao_t bt[3] = {
    { 8, 7, 90, 26, "Voltar", false }, { 106, 7, 110, 26, "Nova pasta", false }, { 224, 7, 90, 26, "Apagar", false },
};

static void desenhar_tudo(void)
{
    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
    g_retangulo(j, 0, 0, LARG, 40, 0xE6E9EC);
    g_retangulo(j, 0, 39, LARG, 1, COR_BORDA);
    for (int i = 0; i < 3; i++) botao_desenhar(j, &bt[i]);
    lista.l = LARG - LATERAL - 14;
    lista.a = ALT - 70 - 30;
    desenhar_lateral();
    carregar();
    jan_mostrar(j);
}

/* Menu do botão direito na lista. */
static bool menu_em_item;

static void abrir_menu(const evento_t *e)
{
    int i = lista.topo + (e->y - lista.y - 2) / LISTA_ALTURA_ITEM;
    menu_em_item = e->y >= lista.y && i >= 0 && i < lista.qtd;
    if (menu_em_item) {
        if (!marcas[i]) { memset(marcas, 0, sizeof marcas); lista.sel = i; }
        lista_desenhar(j, &lista);
        jan_menu(j, e->x, e->y, na_lixeira() ? "Restaurar|Apagar de vez" : "Abrir|Copiar|Recortar|Apagar||Nova pasta|Colar");
    } else {
        jan_menu(j, e->x, e->y, "Nova pasta|Colar");
    }
}

int principal(int argc, char **argv)
{
    criar_pasta(LIXEIRA);
    if (argc > 1 && compara_texto(argv[1], "--esvaziar") == 0) {
        remover_tudo(LIXEIRA);
        criar_pasta(LIXEIRA);
        registrar("arquivos: lixeira esvaziada");
        return 0;
    }
    if (argc > 1) copia_texto(pasta, argv[1], sizeof pasta);
    j = jan_abrir_redimensionavel("Arquivos", LARG_INICIO, ALT_INICIO);
    if (!j) return 1;
    lista = (lista_t){ LATERAL + 6, 70, LARG - LATERAL - 14, ALT - 70 - 30, nomes, 0, -1, 0, marcas, icone };
    atualizar_pendrive();
    desenhar_tudo();
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
        if (e.tipo == EV_TAMANHO) { jan_tamanho(j, &e); desenhar_tudo(); continue; }
        if (e.tipo == EV_CLIQUE && (e.botoes & 2) && e.x >= LATERAL) { abrir_menu(&e); continue; }
        if (e.tipo == EV_MENU) {
            unsigned k = e.tecla;
            if (!menu_em_item) { if (k == 0) nova_pasta(); else colar_itens(); }
            else if (na_lixeira()) { if (k == 0) restaurar_item(); else apagar_item(); }
            else if (k == 0) abrir_item(lista.sel);
            else if (k == 1) copiar_itens(false);
            else if (k == 2) copiar_itens(true);
            else if (k == 3) apagar_item();
            else if (k == 4) nova_pasta();
            else colar_itens();
            continue;
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
        if (botao_tratar(j, &bt[1], &e)) nova_pasta();
        if (botao_tratar(j, &bt[2], &e)) apagar_item();
        if (e.tipo == EV_TECLA && e.tecla == '\b') { voltar(); continue; }
        if (e.tipo == EV_TECLA && e.tecla == TECLA_DELETE) { apagar_item(); continue; }
        if (e.tipo == EV_TECLA && e.tecla == 3) { copiar_itens(false); continue; }
        if (e.tipo == EV_TECLA && e.tecla == 24) { copiar_itens(true); continue; }
        if (e.tipo == EV_TECLA && e.tecla == 22) { colar_itens(); continue; }
        if (e.tipo == EV_TECLA && e.tecla == 1) {                 /* Ctrl+A */
            memset(marcas, 1, (size_t)lista.qtd);
            lista_desenhar(j, &lista);
            continue;
        }
        int r = lista_tratar(j, &lista, &e);
        if (r == LISTA_ATIVOU) abrir_item(lista.sel);
        else if (r == LISTA_SELECIONOU && lista.sel >= 0) {
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
