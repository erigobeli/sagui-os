/* baixar - baixa uma página ou arquivo por HTTP (D032).
 *
 *   baixar http://exemplo.com/               mostra na tela
 *   baixar http://exemplo.com/ pagina.html   grava no arquivo
 *
 * Fala HTTP/1.0 com "Connection: close" (libsagui, http.c): o servidor manda
 * a resposta inteira e fecha, e o fim da conexão é o fim do arquivo.
 * HTTPS precisaria de criptografia (TLS), que o SaguiOS não tem. */
#include "sagui.h"

static char buf[4096];

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: baixar <http://endereço/caminho> [arquivo]\n"); return 1; }
    static char url[600], host[128], caminho[512];
    copia_texto(url, argv[1], sizeof url);
    for (int redirecoes = 0;; redirecoes++) {
        if (comeca_com(url, "https://")) {
            imprimir_em(ERROS, "baixar: https precisa de criptografia (TLS), que o SaguiOS não tem; tente http://\n");
            return 1;
        }
        int porta;
        if (!url_separar(url, host, sizeof host, &porta, caminho, sizeof caminho)) {
            imprimir_em(ERROS, "baixar: endereço inválido\n");
            return 1;
        }
        int fd = http_pedir(host, porta, caminho);
        if (fd < 0) { imprimir_em(ERROS, "baixar: %s:%d: %s\n", host, porta, nome_erro(fd)); return 1; }
        int extra;
        int cab = http_cabecalho(fd, buf, sizeof buf, &extra);
        if (cab < 0) { imprimir_em(ERROS, "baixar: resposta inválida do servidor\n"); fechar(fd); return 1; }
        int codigo = http_codigo(buf);
        static char destino[600];
        if (codigo >= 300 && codigo < 400 && redirecoes < 3 && http_campo(buf, "Location", destino, sizeof destino)) {
            fechar(fd);
            imprimir_em(ERROS, "baixar: redirecionado para %s\n", destino);
            if (comeca_com(destino, "/")) formatar_texto(url, sizeof url, "http://%s:%d%s", host, porta, destino);
            else copia_texto(url, destino, sizeof url);
            continue;
        }
        char *fim_linha = acha_char(buf, '\r');
        if (fim_linha) *fim_linha = 0;
        imprimir_em(ERROS, "%s\n", buf);

        int saida = SAIDA;
        if (argc > 2) {
            saida = abrir(argv[2], ABRIR_CRIAR | ABRIR_TRUNCAR | ABRIR_ESCREVER);
            if (saida < 0) { imprimir_em(ERROS, "baixar: %s: %s\n", argv[2], nome_erro(saida)); fechar(fd); return 1; }
        }
        /* Na tela, o texto UTF-8 de fora vira Latin-1; no arquivo, vai como veio. */
        unsigned total = 0;
        size_t sobra = 0;
        memmove(buf, buf + cab, (size_t)extra);
        int n = extra, r = 0;
        for (;;) {
            if (n > 0) {
                total += (unsigned)n - (unsigned)sobra;
                size_t k = (size_t)n, s = 0;
                if (saida == SAIDA) k = utf8_para_latin1(buf, (size_t)n, &s);
                if (escrever_tudo(saida, buf, (unsigned)k) < 0) break;
                if (s) buf[0] = buf[n - 1];
                sobra = s;
            }
            r = ler(fd, buf + sobra, sizeof buf - sobra);
            if (r <= 0) break;
            n = r + (int)sobra;
        }
        fechar(fd);
        if (r < 0) imprimir_em(ERROS, "baixar: a conexão caiu: %s\n", nome_erro(r));
        if (saida != SAIDA) { fechar(saida); imprimir_em(ERROS, "%u bytes gravados em %s\n", total, argv[2]); }
        return (r < 0 || codigo >= 400) ? 1 : 0;
    }
}
