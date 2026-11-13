/* registro [palavra] - mostra o diário do kernel (os últimos 64 KB das
 * mensagens que normalmente só saem pela porta serial). Com uma palavra, só
 * as linhas que a contêm (ex.: registro HDA). Para mandar ao Claude:
 *     registro > /usb/registro.txt
 * e copie o arquivo do pendrive para o PC. */
#include "sagui.h"

static char buf[65536 + 1];

static bool contem(const char *linha, int n, const char *p)
{
    int t = (int)tam_texto(p);
    for (int i = 0; i + t <= n; i++) {
        int k = 0;
        while (k < t) {
            char a = linha[i + k], b = p[k];
            if (a >= 'a' && a <= 'z') a = (char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (char)(b - 32);
            if (a != b) break;
            k++;
        }
        if (k == t) return true;
    }
    return false;
}

int principal(int argc, char **argv)
{
    int n = info_sistema(INFO_REGISTRO, buf, sizeof buf);
    if (n < 0) { imprimir_em(ERROS, "registro: %s\n", nome_erro(n)); return 1; }
    const char *filtro = argc > 1 ? argv[1] : NULL;
    for (int i = 0; i < n;) {
        int f = i;
        while (f < n && buf[f] != '\n') f++;
        if (!filtro || contem(buf + i, f - i, filtro)) {
            escrever_tudo(SAIDA, buf + i, (unsigned)(f - i));
            escrever_tudo(SAIDA, "\n", 1);
        }
        i = f + 1;
    }
    return 0;
}
