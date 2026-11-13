/* config - lê e muda as configurações do sistema (H37).
 *   config                  mostra se há onde guardar
 *   config chave            mostra o valor
 *   config chave valor...   muda o valor (o resto da linha vira o valor) */
#include "sagui.h"

int principal(int argc, char **argv)
{
    char v[96];
    if (argc < 2) {
        config_ler("sistema.persistente", v, sizeof v);
        imprimir(compara_texto(v, "sim") == 0 ? "As configurações estão sendo guardadas no pendrive (/usb/.saguios).\n"
                                              : "Sem pendrive montado: as mudanças valem até desligar.\n");
        imprimir("uso: config <chave> [valor]\n");
        return 0;
    }
    if (argc == 2) {
        int r = config_ler(argv[1], v, sizeof v);
        if (r < 0) { imprimir_em(ERROS, "config: %s não está definida\n", argv[1]); return 1; }
        imprimir("%s = %s\n", argv[1], v);
        return 0;
    }
    char valor[96] = "";
    for (int i = 2; i < argc; i++) {
        size_t n = tam_texto(valor);
        formatar_texto(valor + n, sizeof valor - n, i > 2 ? " %s" : "%s", argv[i]);
    }
    int r = config_gravar(argv[1], valor);
    if (r < 0) { imprimir_em(ERROS, "config: %s\n", nome_erro(r)); return 1; }
    imprimir("%s = %s%s\n", argv[1], valor, r == 1 ? "" : "  (sem pendrive: vale até desligar)");
    return 0;
}
