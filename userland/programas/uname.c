/* uname (sistema) - diz que sistema é este (H83, vindo do pull request #7,
 * do Gabriel).
 *
 *   uname       SaguiOS
 *   uname -a    nome, versão, arquitetura e há quanto tempo está ligado
 *   uname -r    só a versão
 *   uname -m    só a arquitetura
 *
 * O nome é o costume dos sistemas Unix: os scripts perguntam "uname" para
 * saber em que sistema estão rodando. A versão vem do kernel
 * (info_sistema INFO_VERSAO). A arquitetura é i686: o SaguiOS é compilado
 * para os processadores x86 de 32 bits a partir do Pentium Pro (-march=i686
 * no Makefile). */
#include "sagui.h"

int principal(int argc, char **argv)
{
    bool tudo = false, versao = false, arquitetura = false;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-a") == 0) tudo = true;
        else if (compara_texto(argv[i], "-r") == 0) versao = true;
        else if (compara_texto(argv[i], "-m") == 0) arquitetura = true;
        else if (compara_texto(argv[i], "-s") == 0) { /* só o nome: o padrão */ }
        else { imprimir_em(ERROS, "uso: uname [-a | -s | -r | -m]\n"); return 1; }
    }
    info_versao_t v;
    if (info_sistema(INFO_VERSAO, &v, sizeof v) < 0) {
        copia_texto(v.versao, "?", sizeof v.versao);
        v.segundos_ligado = 0;
    }

    if (tudo) {
        unsigned s = v.segundos_ligado;
        imprimir("SaguiOS %s i686 (ligado há %u:%02u:%02u)\n", v.versao, s / 3600, s / 60 % 60, s % 60);
    } else if (versao || arquitetura) {
        if (versao) imprimir("%s%s", v.versao, arquitetura ? " " : "\n");
        if (arquitetura) imprimir("i686\n");
    } else {
        imprimir("SaguiOS\n");
    }
    return 0;
}
