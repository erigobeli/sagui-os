/* componentes - demonstração do kit gráfico da libsagui (H22): rótulo,
 * botão, caixa de texto, área de texto com rolagem, lista e diálogos. Cada
 * ação também é escrita na saída, para os testes automáticos. */
#include "sagui.h"
#include "gui.h"

static char itens[16][64] = { "Arquivos", "Terminal", "Editor", "Calculadora", "Sagui Pega Banana",
                              "Sobre o SaguiOS", "Pendrive", "Processos", "Memória", "Relógio",
                              "Pipes", "Paginação" };

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    jan_t *j = jan_abrir("Componentes da libsagui", 560, 400);
    if (!j) return 1;
    rotulo(j, 16, 12, "Rótulo: o kit gráfico roda no próprio programa (anel 3).", COR_TEXTO);
    botao_t contar = { 16, 40, 140, 28, "Clique aqui", false };
    botao_t apagar = { 16, 352, 160, 28, "Apagar texto...", false };
    botao_t aviso = { 190, 352, 120, 28, "Mensagem", false };
    caixa_t caixa;
    memset(&caixa, 0, sizeof caixa);
    caixa.x = 16; caixa.y = 82; caixa.l = 300;
    caixa_definir(&caixa, "Digite aqui");
    area_t area;
    area_iniciar(&area, 16, 120, 300, 220);
    char texto[1400];
    int n = 0;
    for (int i = 1; i <= 30; i++)
        n += formatar_texto(texto + n, sizeof texto - (size_t)n, "Linha %d da área de texto, com rolagem.\n", i);
    area_definir(&area, texto, n);
    lista_t lista = { 332, 120, 212, 220, itens, 12, 0, 0, NULL, NULL };
    int cliques = 0;
    char buf[64];

    botao_desenhar(j, &contar);
    botao_desenhar(j, &apagar);
    botao_desenhar(j, &aviso);
    rotulo(j, 170, 46, "cliques: 0  ", COR_TEXTO_FRACO);
    caixa_desenhar(j, &caixa);
    area_desenhar(j, &area);
    lista_desenhar(j, &lista);
    jan_mostrar(j);
    registrar("componentes: pronto\n");

    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (botao_tratar(j, &contar, &e)) {
            formatar_texto(buf, sizeof buf, "cliques: %d", ++cliques);
            g_retangulo(j, 170, 46, 200, 16, COR_FUNDO);
            rotulo(j, 170, 46, buf, COR_TEXTO);
            jan_mostrar_area(j, 170, 46, 200, 16);
            registrar("componentes: botao clicado %d\n", cliques);
        }
        if (botao_tratar(j, &apagar, &e)) {
            bool sim = confirmar("Confirmar", "Tem certeza?\nIsto apaga o texto da área.");
            registrar("componentes: confirmar -> %s\n", sim ? "sim" : "nao");
            if (sim) { area_definir(&area, "", 0); area_desenhar(j, &area); }
        }
        if (botao_tratar(j, &aviso, &e)) {
            mensagem("Mensagem", "Olá! Esta é uma caixa de mensagem\ndo SaguiOS.");
            registrar("componentes: mensagem fechada\n");
        }
        if (caixa_tratar(j, &caixa, &e))
            registrar("componentes: caixa enter: %s\n", caixa.texto);
        if (area_tratar(j, &area, &e))
            registrar("componentes: area mudou (%d letras)\n", area.tam);
        else if (e.tipo == EV_TECLA && area.foco)
            registrar("componentes: area topo %d\n", area.topo);
        if (e.tipo == EV_TECLA && (caixa.foco || area.foco)) continue;   /* teclas: só para quem tem o foco */
        int r = lista_tratar(j, &lista, &e);
        if (r == LISTA_SELECIONOU) registrar("componentes: lista selecionou %s\n", itens[lista.sel]);
        if (r == LISTA_ATIVOU) registrar("componentes: lista ativou %s\n", itens[lista.sel]);
    }
    jan_fechar(j);
    return 0;
}
