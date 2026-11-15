/* markdown - leitor de markdown com HTML/CSS básico e JavaScript mínimo (SaguiOS).
 *
 * Lê um arquivo .md, converte para blocos simples (cabeçalhos, parágrafos,
 * negrito, itálico, listas, links, código) e renderiza com CSS 1 na janela.
 *
 * Inclui um interpretador JavaScript "antigo" mínimo:
 *   - alert("texto") - mostra caixa de mensagem
 *   - document.write("texto") - registra texto no sistema
 *   - var x = valor - variáveis simples
 *
 * Uso: markdown [arquivo.md]
 * Sem arquivo, abre uma página de exemplo embutida.
 *
 * Sem libc, apenas syscalls int 0x80 e libsagui. */

#include "sagui.h"
#include "gui.h"

#define LARG_JAN 720
#define ALT_JAN  500
#define BARRA_SUP 34

/* ------------------------------------------------------------ cores CSS 1 */
#define CSS_FUNDO      0xFFFFFF
#define CSS_TEXTO      0x000000
#define CSS_LINK       0x0000EE
#define CSS_H1         0x000000
#define CSS_H2         0x000000
#define CSS_H3         0x000000
#define CSS_CODIGO     0xF0F0F0
#define CSS_BORDA      0xCCCCCC

/* ------------------------------------------------------------ tipos de bloco */
typedef enum {
    BLOCO_TEXTO,
    BLOCO_H1, BLOCO_H2, BLOCO_H3,
    BLOCO_NEGRITO, BLOCO_ITALICO,
    BLOCO_LISTA,
    BLOCO_CODIGO,
    BLOCO_LINK,
    BLOCO_IMG,
    BLOCO_LINHA,
    BLOCO_TITULO,
} bloco_tipo_t;

typedef struct bloco {
    bloco_tipo_t tipo;
    char   *texto;
    char   *href;
    int    nivel;
    struct bloco *prox;
} bloco_t;

typedef struct {
    bloco_t *primeiro;
    bloco_t *ultimo;
    int     qtd;
} documento_t;

/* ------------------------------------------------------------ markdown -> blocos */
static bloco_t *bloco_novo(bloco_tipo_t tipo, const char *texto, const char *href, int nivel)
{
    bloco_t *b = aloca(sizeof(bloco_t));
    if (!b) return NULL;
    b->tipo = tipo;
    b->texto = texto ? copia_texto(aloca(tam_texto(texto) + 1), texto, tam_texto(texto) + 1) : NULL;
    b->href = href ? copia_texto(aloca(tam_texto(href) + 1), href, tam_texto(href) + 1) : NULL;
    b->nivel = nivel;
    b->prox = NULL;
    return b;
}

static void documento_adicionar(documento_t *doc, bloco_t *b)
{
    if (!doc->primeiro) doc->primeiro = b;
    else doc->ultimo->prox = b;
    doc->ultimo = b;
    doc->qtd++;
}

static void limpar_espacos(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s) {
        char *fim = s + tam_texto(s) - 1;
        while (fim > s && (*fim == ' ' || *fim == '\t' || *fim == '\n' || *fim == '\r')) fim--;
        fim[1] = 0;
    }
    /* move para o início se havia espaços à esquerda */
    char *d = s;
    while (*s) *d++ = *s++;
    *d = 0;
}

static documento_t *converter_markdown(const char *texto)
{
    documento_t *doc = aloca(sizeof(documento_t));
    if (!doc) return NULL;
    doc->primeiro = doc->ultimo = NULL;
    doc->qtd = 0;

    char linha[1024];
    int pos = 0;
    bool em_codigo = false;
    char codigo_buf[4096];
    int codigo_pos = 0;

    while (texto[pos]) {
        int li = 0;
        while (texto[pos] && texto[pos] != '\n' && li < (int)sizeof linha - 1) {
            linha[li++] = texto[pos++];
        }
        linha[li] = 0;
        if (texto[pos] == '\n') pos++;

        if (li >= 3 && linha[0] == '`' && linha[1] == '`' && linha[2] == '`') {
            if (!em_codigo) { em_codigo = true; codigo_pos = 0; }
            else { em_codigo = false;
                bloco_t *b = bloco_novo(BLOCO_CODIGO, codigo_buf, NULL, 0);
                if (b) documento_adicionar(doc, b);
            }
            continue;
        }
        if (em_codigo) {
            int k = 0;
            while (linha[k] && codigo_pos < (int)sizeof codigo_buf - 1) codigo_buf[codigo_pos++] = linha[k++];
            codigo_buf[codigo_pos++] = '\n';
            continue;
        }

        if (li >= 3 && linha[0] == '-' && linha[1] == '-' && linha[2] == '-') {
            bloco_t *b = bloco_novo(BLOCO_LINHA, NULL, NULL, 0);
            if (b) documento_adicionar(doc, b);
            continue;
        }

        int nivel = 0;
        if (li >= 2 && linha[0] == '#' && linha[1] == ' ') { nivel = 1; li -= 2; memmove(linha, linha + 2, li + 1); }
        else if (li >= 3 && linha[0] == '#' && linha[1] == '#' && linha[2] == ' ') { nivel = 2; li -= 3; memmove(linha, linha + 3, li + 1); }
        else if (li >= 4 && linha[0] == '#' && linha[1] == '#' && linha[2] == '#' && linha[3] == ' ') { nivel = 3; li -= 4; memmove(linha, linha + 4, li + 1); }

        if (nivel) {
            limpar_espacos(linha);
            bloco_t *b = bloco_novo(nivel == 1 ? BLOCO_H1 : nivel == 2 ? BLOCO_H2 : BLOCO_H3, linha, NULL, nivel);
            if (b) documento_adicionar(doc, b);
            continue;
        }

        if (li >= 2 && linha[0] == '-' && linha[1] == ' ') {
            char *conteudo = linha + 2;
            limpar_espacos(conteudo);
            bloco_t *b = bloco_novo(BLOCO_LISTA, conteudo, NULL, 0);
            if (b) documento_adicionar(doc, b);
            continue;
        }

        if (li == 0) continue;

        limpar_espacos(linha);
        bloco_t *b = bloco_novo(BLOCO_TEXTO, linha, NULL, 0);
        if (b) documento_adicionar(doc, b);
    }

    if (em_codigo && codigo_pos > 0) {
        codigo_buf[codigo_pos] = 0;
        bloco_t *b = bloco_novo(BLOCO_CODIGO, codigo_buf, NULL, 0);
        if (b) documento_adicionar(doc, b);
    }

    return doc;
}

/* ------------------------------------------------------------ renderização */
static int linha_y = 0;

static uint32_t cor_base(bool negrito, bool italico);

static void desenhar_texto_inline(jan_t *j, int x, int y, const char *texto, uint32_t cor)
{
    (void)cor;
    int cx = x, cy = y;
    const char *p = texto;
    bool negrito = false, italico = false;
    while (*p) {
        if (p[0] == '*' && p[1] == '*') { negrito = !negrito; p += 2; continue; }
        if (p[0] == '*') { italico = !italico; p++; continue; }
        if (*p == '\n') { cx = x; cy += LETRA_A; continue; }
        uint32_t c = cor_base(negrito, italico);
        g_texto_n(j, cx, cy, p, 1, c);
        cx += LETRA_L;
        if (cx + LETRA_L > LARG_JAN - 10) { cx = x; cy += LETRA_A; }
        p++;
    }
    linha_y = cy + LETRA_A;
    jan_mostrar_area(j, x, y, LARG_JAN - x, LETRA_A + 4);
}

static uint32_t cor_base(bool negrito, bool italico)
{
    if (negrito) return 0x000000;
    if (italico) return 0x444444;
    return CSS_TEXTO;
}

static void desenhar_link(jan_t *j, int x, int y, const char *texto)
{
    int l = (int)tam_texto(texto) * LETRA_L;
    g_texto(j, x, y, texto, CSS_LINK);
    g_linha(j, x, y + LETRA_A - 1, x + l, y + LETRA_A - 1, CSS_LINK);
    linha_y = y + LETRA_A + 4;
    jan_mostrar_area(j, x, y, l + 4, LETRA_A + 4);
}

static void desenhar_codigo(jan_t *j, int x, int y, const char *texto)
{
    int l = (int)tam_texto(texto) * LETRA_L;
    if (l > LARG_JAN - 40) l = LARG_JAN - 40;
    int linhas = (int)tam_texto(texto) * LETRA_L / l + 1;
    int a = linhas * LETRA_A + 8;
    g_retangulo(j, x, y, l + 8, a, CSS_CODIGO);
    g_moldura(j, x, y, l + 8, a, CSS_BORDA);
    g_texto(j, x + 4, y + 4, texto, CSS_TEXTO);
    linha_y = y + a + 4;
    jan_mostrar_area(j, x, y, l + 12, a + 4);
}

static void renderizar_documento(jan_t *j, documento_t *doc)
{
    g_retangulo(j, 0, BARRA_SUP, LARG_JAN, ALT_JAN - BARRA_SUP, CSS_FUNDO);
    linha_y = BARRA_SUP + 10;

    for (bloco_t *b = doc->primeiro; b; b = b->prox) {
        int x = 20;
        switch (b->tipo) {
        case BLOCO_H1:
            g_texto(j, x, linha_y, b->texto, CSS_H1);
            g_texto(j, x + 1, linha_y + 1, b->texto, CSS_H1);
            linha_y += LETRA_A * 2 + 10;
            jan_mostrar_area(j, x, linha_y - LETRA_A * 2 - 10, LARG_JAN - 40, LETRA_A * 2 + 10);
            break;
        case BLOCO_H2:
            g_texto(j, x, linha_y, b->texto, CSS_H2);
            linha_y += LETRA_A + 6;
            jan_mostrar_area(j, x, linha_y - LETRA_A - 6, LARG_JAN - 40, LETRA_A + 6);
            break;
        case BLOCO_H3:
            g_texto(j, x, linha_y, b->texto, CSS_H3);
            linha_y += LETRA_A + 4;
            jan_mostrar_area(j, x, linha_y - LETRA_A - 4, LARG_JAN - 40, LETRA_A + 4);
            break;
        case BLOCO_NEGRITO:
            desenhar_texto_inline(j, x, linha_y, b->texto, CSS_TEXTO);
            break;
        case BLOCO_ITALICO:
            desenhar_texto_inline(j, x, linha_y, b->texto, CSS_TEXTO);
            break;
        case BLOCO_LISTA:
            g_texto(j, x, linha_y, "- ", CSS_TEXTO);
            g_texto(j, x + 16, linha_y, b->texto, CSS_TEXTO);
            linha_y += LETRA_A + 2;
            jan_mostrar_area(j, x, linha_y - LETRA_A - 2, LARG_JAN - 40, LETRA_A + 2);
            break;
        case BLOCO_CODIGO:
            desenhar_codigo(j, x, linha_y, b->texto);
            break;
        case BLOCO_LINK:
            desenhar_link(j, x, linha_y, b->texto);
            break;
        case BLOCO_TEXTO:
            desenhar_texto_inline(j, x, linha_y, b->texto, CSS_TEXTO);
            break;
        case BLOCO_LINHA:
            g_linha(j, x, linha_y, LARG_JAN - 20, linha_y, CSS_BORDA);
            linha_y += 10;
            jan_mostrar_area(j, x, linha_y - 10, LARG_JAN - 40, 10);
            break;
        case BLOCO_IMG:
            g_retangulo(j, x, linha_y, 60, 20, CSS_BORDA);
            g_texto(j, x + 4, linha_y + 2, "[img]", CSS_TEXTO);
            linha_y += 30;
            jan_mostrar_area(j, x, linha_y - 30, LARG_JAN - 40, 30);
            break;
        case BLOCO_TITULO:
            g_texto(j, x, linha_y, b->texto, CSS_TEXTO);
            linha_y += LETRA_A + 10;
            jan_mostrar_area(j, x, linha_y - LETRA_A - 10, LARG_JAN - 40, LETRA_A + 10);
            break;
        }
    }
}

/* ------------------------------------------------------------ JavaScript mínimo */
#define MAX_VAR 32

typedef struct { char nome[32]; char valor[256]; } variavel_t;
static variavel_t variaveis[MAX_VAR];
static int qtd_var = 0;

static void js_definir_var(const char *nome, const char *valor)
{
    for (int i = 0; i < qtd_var; i++)
        if (compara_texto(variaveis[i].nome, nome) == 0) {
            copia_texto(variaveis[i].valor, valor, sizeof variaveis[i].valor);
            return;
        }
    if (qtd_var < MAX_VAR) {
        copia_texto(variaveis[qtd_var].nome, nome, sizeof variaveis[qtd_var].nome);
        copia_texto(variaveis[qtd_var].valor, valor, sizeof variaveis[qtd_var].valor);
        qtd_var++;
    }
}

static int js_executar(const char *cmd)
{
    if (comeca_com(cmd, "alert(")) {
        const char *inicio = cmd + 6;
        while (*inicio == ' ') inicio++;
        if (*inicio == '"') inicio++;
        char texto[256];
        int k = 0;
        while (*inicio && *inicio != '"' && k < 255) texto[k++] = *inicio++;
        texto[k] = 0;
        mensagem("JavaScript", texto);
        return 0;
    }
    if (comeca_com(cmd, "document.write(")) {
        const char *inicio = cmd + 15;
        while (*inicio == ' ') inicio++;
        if (*inicio == '"') inicio++;
        char texto[256];
        int k = 0;
        while (*inicio && *inicio != '"' && k < 255) texto[k++] = *inicio++;
        texto[k] = 0;
        registrar("js: document.write(%s)", texto);
        return 0;
    }
    if (comeca_com(cmd, "var ")) {
        const char *inicio = cmd + 4;
        char nome[32];
        int k = 0;
        while (*inicio && *inicio != ' ' && *inicio != '=' && k < 31) nome[k++] = *inicio++;
        nome[k] = 0;
        while (*inicio && *inicio != '=') inicio++;
        if (*inicio == '=') inicio++;
        while (*inicio == ' ') inicio++;
        char valor[256];
        k = 0;
        if (*inicio == '"') inicio++;
        while (*inicio && *inicio != '"' && k < 255) valor[k++] = *inicio++;
        valor[k] = 0;
        js_definir_var(nome, valor);
        return 0;
    }
    return -1;
}

static const char *acha_tag_fechamento(const char *p, const char *tag)
{
    while (*p) {
        if (compara_texto_n(p, tag, tam_texto(tag)) == 0) return p;
        p++;
    }
    return NULL;
}

static void js_executar_blocos(const char *texto)
{
    const char *p = texto;
    while ((p = acha_char(p, '<')) != NULL) {
        if (compara_texto_n(p, "<script>", 8) == 0) {
            p += 8;
            const char *fim = acha_char(p, '<');
            if (fim && compara_texto_n(fim, "</script>", 9) == 0) {
                char bloco[1024];
                int n = (int)(fim - p);
                if (n > 1023) n = 1023;
                memcpy(bloco, p, n);
                bloco[n] = 0;
                char *linha = bloco;
                while (*linha) {
                    char *fim_linha = acha_char(linha, '\n');
                    if (fim_linha) *fim_linha = 0;
                    char *l = linha;
                    while (*l == ' ' || *l == '\t') l++;
                    if (tam_texto(l) > 0) js_executar(l);
                    if (fim_linha) linha = fim_linha + 1;
                    else break;
                }
                p = fim + 9;
            } else break;
        } else p++;
    }
}

/* ------------------------------------------------------------ ler arquivo */
static char *ler_arquivo_inteiro(const char *caminho, unsigned *tamanho)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return NULL;
    int n = 0, cap = 4096;
    char *buf = aloca(cap);
    if (!buf) { fechar(fd); return NULL; }
    int l;
    while ((l = ler(fd, buf + n, cap - n - 1)) > 0) {
        n += l;
        if (n + 1 >= cap) {
            int nc = cap * 2;
            char *nb = realoca(buf, nc);
            if (!nb) { libera(buf); fechar(fd); return NULL; }
            buf = nb; cap = nc;
        }
    }
    buf[n] = 0;
    fechar(fd);
    if (tamanho) *tamanho = n;
    return buf;
}

/* ------------------------------------------------------------ desenho */
static void desenhar_cabecalho(jan_t *j, const char *titulo)
{
    g_retangulo(j, 0, 0, LARG_JAN, 32, 0x2563EB);
    g_moldura(j, 0, 0, LARG_JAN, 32, 0x1E40AF);
    g_texto(j, 16, 8, "Sagui Markdown", 0xFFFFFF);
    g_texto(j, 200, 8, titulo, 0xE2E8F0);
    jan_mostrar_area(j, 0, 0, LARG_JAN, 32);
}

static void desenhar_rodape(jan_t *j)
{
    g_retangulo(j, 0, ALT_JAN - 24, LARG_JAN, 24, 0xF1F5F9);
    g_moldura(j, 0, ALT_JAN - 24, LARG_JAN, 24, 0xCBD5E1);
    g_texto(j, 16, ALT_JAN - 18, "ESC: sair   Q: recarregar", 0x64748B);
    jan_mostrar_area(j, 0, ALT_JAN - 24, LARG_JAN, 24);
}

/* ------------------------------------------------------------ loop */
int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    jan_t *j = jan_abrir("Sagui Markdown", LARG_JAN, ALT_JAN);
    if (!j) return 1;

    char *texto = NULL;
    char titulo[128];
    if (argc >= 2) {
        texto = ler_arquivo_inteiro(argv[1], NULL);
        if (!texto) {
            mensagem("Erro", "Não consegui abrir o arquivo.");
            jan_fechar(j);
            return 1;
        }
        const char *ult = acha_ultimo_char(argv[1], '/');
        copia_texto(titulo, ult && ult[1] ? ult + 1 : argv[1], sizeof titulo);
    } else {
        static const char *exemplo =
            "# Bem-vindo ao Markdown do SaguiOS\n"
            "\n"
            "Este é um **leitor de markdown** escrito do zero para o SaguiOS, com suporte a:\n"
            "\n"
            "- Cabeçalhos (#, ##, ###)\n"
            "- **Negrito** e *itálico*\n"
            "- Listas com bullet points\n"
            "- Blocos de código\n"
            "- Links [clicáveis]\n"
            "- Regras horizontais ---\n"
            "\n"
            "## Código de exemplo\n"
            "\n"
            "```\n"
            "sagui:/$ ls -la\n"
            "total 42\n"
            "```\n"
            "\n"
            "---\n"
            "\n"
            "Visite o [SaguiOS](https://github.com/gabriel-rb-devc/sagui-fork) no GitHub.\n";
        texto = (char *)exemplo;
        copia_texto(titulo, "Exemplo embutido", sizeof titulo);
    }

    js_executar_blocos(texto);

    documento_t *doc = converter_markdown(texto);
    if (!doc) {
        mensagem("Erro", "Não consegui processar o markdown.");
        if (argc >= 2) libera(texto);
        jan_fechar(j);
        return 1;
    }

    desenhar_cabecalho(j, titulo);
    renderizar_documento(j, doc);
    desenhar_rodape(j);
    jan_mostrar(j);

    bool rodando = true;
    while (rodando) {
        evento_t e;
        int r = proximo_evento(j->id, &e, true);
        if (r == 1) {
            if (e.tipo == EV_FECHAR) rodando = false;
            else if (e.tipo == EV_TECLA) {
                if (e.tecla == TECLA_ESC || e.tecla == 'q' || e.tecla == 'Q') rodando = false;
                else if (e.tecla == 'r' || e.tecla == 'R') {
                    documento_t *novo = converter_markdown(texto);
                    if (novo) {
                        g_retangulo(j, 0, BARRA_SUP, LARG_JAN, ALT_JAN - BARRA_SUP - 24, CSS_FUNDO);
                        linha_y = BARRA_SUP + 10;
                        renderizar_documento(j, novo);
                        desenhar_rodape(j);
                        jan_mostrar(j);
                        for (bloco_t *bb = doc->primeiro; bb;) {
                            bloco_t *prox = bb->prox;
                            libera(bb->texto);
                            libera(bb->href);
                            libera(bb);
                            bb = prox;
                        }
                        libera(doc);
                        doc = novo;
                    }
                }
            }
        }
    }

    for (bloco_t *b = doc->primeiro; b;) {
        bloco_t *prox = b->prox;
        libera(b->texto);
        libera(b->href);
        libera(b);
        b = prox;
    }
    libera(doc);
    if (argc >= 2) libera(texto);
    jan_fechar(j);
    return 0;
}
