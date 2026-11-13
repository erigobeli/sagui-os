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
            bool eh_pasta = e.tipo == TIPO_PASTA;
            if ((passo == 0) != eh_pasta) continue;
            copia_texto(nomes[n], e.nome, 64);
            tipos[n] = (uint8_t)e.tipo;
            tamanhos[n] = e.tamanho;
            n++;
        }
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

static void apagar_item(void)
{
    int i = lista.sel;
    if (i < 0 || i >= lista.qtd) return;
    char c[200], pergunta[200];
    caminho_de(i, c, sizeof c);
    formatar_texto(pergunta, sizeof pergunta, "Tem certeza que quer apagar\n%s%s?", nomes[i], tipos[i] == TIPO_PASTA ? "/" : "");
    if (!confirmar("Apagar", pergunta)) return;
    int r = tipos[i] == TIPO_PASTA ? remover_pasta(c) : apagar(c);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui apagar:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
    registrar("arquivos: apagado %s", c);
    carregar();
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
    lista = (lista_t){ LATERAL + 6, 70, LARG - LATERAL - 14, ALT - 70 - 30, nomes, 0, -1, 0, icone };
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
