/* sobre - versão e créditos do SaguiOS. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    static info_processo_t v[64];
    int procs = info_sistema(INFO_PROCESSOS, v, 64);
    info_memoria_t m;
    info_sistema(INFO_MEMORIA, &m, sizeof m);
    info_versao_t iv;
    if (info_sistema(INFO_VERSAO, &iv, sizeof iv) < 0) copia_texto(iv.versao, "?", sizeof iv.versao);
    imprimir("SaguiOS %s: um sistema operacional escrito do zero por IA.\n", iv.versao);
    imprimir("Kernel de 32 bits com paginação, processos no anel 3, multitarefa\n");
    imprimir("preemptiva, chamadas de sistema, arquivos na RAM e pipes.\n");
    imprimir("Escrito por Claude (Anthropic) para o projeto do Eli Rigobeli, sem código de\n");
    imprimir("outros sistemas no núcleo. Contribuições da comunidade: veja CONTRIBUIDORES.md.\n");
    imprimir("Agora: %d processos, %u MB livres de memória.\n", procs, m.livre_kb / 1024);
    imprimir("Leia /sobre.txt e /historia/saguios.txt. O mascote está em /arte/sagui.txt.\n");
    return 0;
}
