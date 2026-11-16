/* hostname (nome-maquina) - mostra ou muda o nome do computador.
 *
 *   nome-maquina           mostra o nome atual (padrão: sagui)
 *   nome-maquina NOVO      muda o nome para NOVO
 *
 * O nome fica guardado nas configurações do sistema (chave
 * "sistema.hostname"), que vão para o pendrive se houver um montado
 * (config_gravar, H37); sem pendrive, vale até desligar. Não há chamada de
 * sistema própria para isso: usamos config_ler e config_gravar da libsagui.
 *
 * O nome pode ter letras, números e hífen, até 63 caracteres (como um nome
 * de host na rede). */
#include "sagui.h"

#define CHAVE_HOSTNAME "sistema.hostname"
#define PADRAO_HOSTNAME "sagui"

static int uso(void)
{
    imprimir_em(ERROS, "uso: nome-maquina [NOVO]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    if (argc > 2) return uso();

    char atual[64];
    int r = config_ler(CHAVE_HOSTNAME, atual, sizeof atual);
    if (r < 0) copia_texto(atual, PADRAO_HOSTNAME, sizeof atual);

    if (argc == 1) {
        /* Só mostra. */
        imprimir("%s\n", atual);
        return 0;
    }

    /* Muda o nome. Valida antes de gravar. */
    const char *novo = argv[1];
    size_t nn = tam_texto(novo);
    if (nn == 0 || nn > 63) {
        imprimir_em(ERROS, "nome-maquina: o nome deve ter de 1 a 63 caracteres\n");
        return 1;
    }
    for (size_t i = 0; i < nn; i++) {
        char ch = novo[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-')) {
            imprimir_em(ERROS, "nome-maquina: use só letras, números e hífen\n");
            return 1;
        }
    }

    r = config_gravar(CHAVE_HOSTNAME, novo);
    if (r < 0) { imprimir_em(ERROS, "nome-maquina: %s\n", nome_erro(r)); return 1; }
    imprimir("nome-maquina: %s%s\n", novo, r == 1 ? " (guardado no pendrive)" : " (vale até desligar)");
    return 0;
}
