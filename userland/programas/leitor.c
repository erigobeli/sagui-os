/* leitor - o Leitor: mostra um arquivo Markdown formatado (títulos, negrito,
 * listas, tabelas, código, links), com o componente markdown.h da libsagui.
 *
 *   leitor arquivo.md
 *
 * Os .md da imagem do sistema já chegam em Latin-1 (o build converte); os
 * de fora (do pendrive, baixados) costumam vir em UTF-8: se o texto for
 * UTF-8 válido com letras acima de 127, ele é convertido para o Latin-1 da
 * tela (RFC 3629), e o que não existe no Latin-1 vira o mais parecido.
 *
 * Teclado: setas, PgUp, PgDn, Home e End rolam; Ctrl+C copia o texto todo;
 * Esc fecha. Um clique num link abre o link no Cipó. A janela muda de
 * tamanho, e o texto é refeito na largura nova. */
#include "sagui.h"
#include "gui.h"
#include "markdown.h"

static int ler_utf8(const uint8_t *s, int n, uint32_t *cp)
{
    uint8_t c = s[0];
    int k = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!k || k > n) return 0;
    uint32_t v = k == 1 ? c : k == 2 ? c & 0x1F : k == 3 ? c & 0x0F : c & 0x07;
    for (int i = 1; i < k; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        v = (v << 6) | (s[i] & 0x3F);
    }
    *cp = v;
    return k;
}

/* UTF-8 com acentos? Converte no mesmo lugar (o Latin-1 nunca é maior). */
static int para_latin1(uint8_t *s, int n)
{
    bool multi = false;
    for (int i = 0; i < n;) {
        uint32_t cp;
        int k = ler_utf8(s + i, n - i, &cp);
        if (!k) return n;          /* não é UTF-8: já é Latin-1 */
        if (k > 1) multi = true;
        i += k;
    }
    if (!multi) return n;
    int o = 0;
    for (int i = 0; i < n;) {
        uint32_t cp;
        int k = ler_utf8(s + i, n - i, &cp);
        i += k;
        if (cp < 0x100) { s[o++] = (uint8_t)cp; continue; }
        const char *t = cp == 0x2018 || cp == 0x2019 ? "'" : cp == 0x201C || cp == 0x201D ? "\"" :
                        cp == 0x2013 || cp == 0x2014 ? "-" : cp == 0x2026 ? "..." : cp == 0x2022 ? "\xB7" :
                        cp == 0x2192 ? "->" : cp == 0x2190 ? "<-" : cp == 0x2264 ? "<=" : cp == 0x2265 ? ">=" :
                        cp == 0x2713 || cp == 0x2714 ? "v" : cp == 0x2717 || cp == 0x2718 ? "x" : "?";
        while (*t && o < i) s[o++] = (uint8_t)*t++;
    }
    return o;
}

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: leitor arquivo.md\n"); return 1; }
    const char *caminho = argv[1];
    uint8_t *dados;
    int tam = ler_arquivo_inteiro(caminho, &dados);
    if (tam < 0) { imprimir_em(ERROS, "leitor: %s: %s\n", caminho, nome_erro(tam)); return 1; }
    tam = para_latin1(dados, tam);

    const char *nome = caminho;
    for (const char *p = caminho; *p; p++) if (*p == '/' && p[1]) nome = p + 1;
    char titulo[64];
    formatar_texto(titulo, sizeof titulo, "%s - Leitor", nome);
    int L = 780, A = 580;
    jan_t *j = jan_abrir(titulo, L, A);
    if (!j) return 1;
    jan_redimensionavel(j, 320, 200);
    md_t m;
    md_iniciar(&m, 1, 1, L - 2, A - 2);
    md_definir(&m, (const char *)dados, tam, false);
    libera(dados);
    md_desenhar(j, &m);
    jan_mostrar(j);
    char r[200];
    md_resumo(&m, r, sizeof r);
    registrar("leitor: %s: %s", caminho, r);
    registrar("leitor: titulo %s", m.titulo);

    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) break;
        if (e.tipo == EV_REDIMENSIONAR) {
            jan_novo_tamanho(j, &e);
            md_mudar_tamanho(&m, j->larg - 2, j->alt - 2);
            md_desenhar(j, &m);
            jan_mostrar(j);
            registrar("leitor: tamanho %dx%d, altura do texto %d", j->larg, j->alt, m.altura);
            continue;
        }
        md_tratar(j, &m, &e);
    }
    md_fim(&m);
    jan_fechar(j);
    return 0;
}
