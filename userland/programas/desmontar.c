/* desmontar [disco] - grava tudo o que estiver pendente e libera o pendrive
 * (ou o disco rígido, que fica na máquina). */
#include "sagui.h"

int principal(int argc, char **argv)
{
    char nome[16] = "";
    bool interno = false;
    info_disco_t d;
    for (int i = 0; listar_discos(i, &d) == 1; i++)
        if (argc > 1 ? compara_texto(d.nome, argv[1]) == 0 : d.montado) {
            copia_texto(nome, d.nome, sizeof nome);
            interno = d.interno;   /* disco rígido (hd0...): não se tira da máquina */
            break;
        }
    if (argc > 1 && !nome[0]) copia_texto(nome, argv[1], sizeof nome);
    if (!nome[0]) { imprimir_em(ERROS, "desmontar: nenhum disco montado\n"); return 1; }
    int r = desmontar(nome);
    if (r < 0) { imprimir_em(ERROS, "desmontar: %s: %s\n", nome, nome_erro(r)); return 1; }
    if (interno) imprimir("Disco %s desmontado: tudo gravado.\n", nome);
    else imprimir("Pode remover o pendrive.\n");
    return 0;
}
