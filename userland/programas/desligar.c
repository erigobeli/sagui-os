/* desligar.c - diálogo para desligar ou reiniciar o computador */
#include "sagui.h"
#include "gui.h"

int principal(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "--agora") == 0 || compara_texto(argv[i], "-f") == 0) {
            desligar();
            return 0;
        }
        if (compara_texto(argv[i], "--reiniciar") == 0 || compara_texto(argv[i], "-r") == 0) {
            reiniciar();
            return 0;
        }
    }

    jan_t *j = jan_abrir("Desligar o SaguiOS", 340, 150);
    if (!j) {
        escrever(1, "Desligando o SaguiOS...\n", 24);
        dormir(100);
        desligar();
        return 0;
    }

    g_retangulo(j, 0, 0, 340, 36, 0x18202A);
    g_texto(j, 16, 10, "Encerrar o sistema", 0xFFDF4A);

    g_retangulo(j, 0, 36, 340, 114, COR_FUNDO);
    g_texto(j, 16, 56, "O que você deseja fazer?", COR_TEXTO);

    botao_t bt_desligar  = {  16, 96, 92, 28, "Desligar",  false };
    botao_t bt_reiniciar = { 124, 96, 92, 28, "Reiniciar", false };
    botao_t bt_cancelar  = { 232, 96, 92, 28, "Cancelar",  false };

    botao_desenhar(j, &bt_desligar);
    botao_desenhar(j, &bt_reiniciar);
    botao_desenhar(j, &bt_cancelar);
    jan_mostrar(j);

    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) == 1) {
            if (e.tipo == EV_FECHAR) break;
            if (e.tipo == EV_TECLA && (e.tecla == TECLA_ESC || e.tecla == 'c' || e.tecla == 'C')) break;

            if (botao_tratar(j, &bt_desligar, &e)) {
                g_retangulo(j, 0, 36, 340, 114, COR_FUNDO);
                g_texto(j, 16, 70, "Desligando o sistema...", COR_TEXTO);
                jan_mostrar(j);
                dormir(150);
                desligar();
                break;
            }
            if (botao_tratar(j, &bt_reiniciar, &e)) {
                g_retangulo(j, 0, 36, 340, 114, COR_FUNDO);
                g_texto(j, 16, 70, "Reiniciando o sistema...", COR_TEXTO);
                jan_mostrar(j);
                dormir(150);
                reiniciar();
                break;
            }
            if (botao_tratar(j, &bt_cancelar, &e)) break;
        }
    }

    jan_fechar(j);
    return 0;
}

