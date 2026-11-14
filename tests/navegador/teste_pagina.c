/* teste_pagina.c - lê um HTML, diagrama com o motor do Cipó numa largura dada
 * e pinta a página inteira (até 8000 pixels de altura) num arquivo de pixels
 * crus, igual ao que a janela do navegador mostraria. Os recursos (CSS,
 * imagens, fontes) com endereço relativo são lidos da pasta do HTML.
 * Uso: teste_pagina pagina.html largura saida.cru [url]
 *
 * Desde a 2.1 (H82) o desenho é o do motor (userland/navegador/motor), o mesmo
 * código compilado aqui com o sagui.h simulado. Sem JavaScript: as páginas do
 * teste de pixels não têm scripts. */
#include "../../userland/navegador/navegador.h"

static char pasta[512];

static uint8_t *ler_arquivo(const char *nome, unsigned *n)
{
    FILE *f = fopen(nome, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (unsigned)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*n + 1);
    if (fread(d, 1, *n, f) != *n) { fclose(f); free(d); return NULL; }
    fclose(f);
    d[*n] = 0;
    return d;
}

/* "http://teste/a/b.png" ou "imagens/b.png": o arquivo da pasta da página. */
static uint8_t *baixar(const char *url, unsigned *n)
{
    const char *p = url;
    if (comeca_com(p, "http://") || comeca_com(p, "https://")) {
        p = strchr(p + 8, '/');
        if (!p) return NULL;
        p++;
    }
    char caminho[1200];
    snprintf(caminho, sizeof caminho, "%s%s", pasta, p);
    return ler_arquivo(caminho, n);
}

static const uint32_t *sem_tela(int id, int *l, int *a) { (void)id; (void)l; (void)a; return NULL; }

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    unsigned n;
    char *d = (char *)ler_arquivo(argv[1], &n);
    if (!d) return 2;
    int largura = atoi(argv[2]);
    copia_texto(pasta, argv[1], sizeof pasta);
    char *barra = strrchr(pasta, '/');
    if (barra) barra[1] = 0; else pasta[0] = 0;
    motor_ganchos_t g = { baixar, imagem_decodificar, sem_tela };
    motor_t *m = motor_novo(&g);
    motor_ler(m, d, (int)n, argc > 4 ? argv[4] : "http://teste/pagina.html");
    /* Como o navegador: diagrama, e a cada leva de fontes e imagens, de novo. */
    int alt = motor_layout(m, largura, 606);
    while (motor_imagens(m, 2)) alt = motor_layout(m, largura, 606);
    int h = alt < 8000 ? alt : 8000;
    if (h < 1) h = 1;
    uint32_t *px = malloc((size_t)largura * (size_t)h * 4);
    motor_pintar(m, px, largura, largura, 0, h);
    FILE *s = fopen(argv[3], "wb");
    fwrite(&largura, 4, 1, s);
    fwrite(&h, 4, 1, s);
    fwrite(px, 4, (size_t)largura * (size_t)h, s);
    fclose(s);
    int nos, cmds, imgs;
    motor_numeros(m, &nos, &cmds, &imgs);
    printf("titulo: %s\ncomandos: %d\nlinks: %d\nimagens: %d (%d carregadas)\naltura: %d\nnos: %d\n", motor_titulo(m), cmds,
           motor_links(m), motor_imagens_total(m), imgs, alt, nos);
    return 0;
}
