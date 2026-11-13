/* teste_pagina.c - lê um HTML, monta o layout numa largura dada e desenha a
 * página inteira (até 8000 pixels de altura) num arquivo de pixels crus,
 * igual ao que a janela do navegador mostraria. As imagens (src relativo) são
 * lidas da pasta do HTML. Uso: teste_pagina pagina.html largura saida.cru */
#include "../../userland/navegador/navegador.h"

static uint8_t *ler_arquivo(const char *nome, uint32_t *n)
{
    FILE *f = fopen(nome, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*n + 1);
    if (fread(d, 1, *n, f) != *n) { fclose(f); free(d); return NULL; }
    fclose(f);
    return d;
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    uint32_t n;
    char *d = (char *)ler_arquivo(argv[1], &n);
    if (!d) return 2;
    int largura = atoi(argv[2]);
    documento_t *doc = html_ler(d, n, -1);
    pagina_t *p = pagina_criar(doc);
    pagina_layout(p, largura);
    /* Imagens locais. */
    char pasta[512];
    copia_texto(pasta, argv[1], sizeof pasta);
    char *barra = strrchr(pasta, '/');
    if (barra) barra[1] = 0; else pasta[0] = 0;
    int carregadas = 0;
    for (int i = 0; i < p->nimgs; i++) {
        char caminho[1024];
        snprintf(caminho, sizeof caminho, "%s%s", pasta, p->imgs[i].src);
        uint32_t ni;
        uint8_t *di = ler_arquivo(caminho, &ni);
        char erro[128];
        if (di) { p->imgs[i].img = imagem_ler(di, ni, erro, sizeof erro); free(di); }
        if (p->imgs[i].img) carregadas++;
    }
    if (carregadas) pagina_layout(p, largura);
    int alt = p->altura < 8000 ? p->altura : 8000;
    jan_t j = { 0, malloc((size_t)largura * (size_t)alt * 4), largura, alt };
    pagina_desenhar(p, &j, 0, 0, largura, alt, 0, -1);
    FILE *s = fopen(argv[3], "wb");
    fwrite(&largura, 4, 1, s);
    fwrite(&alt, 4, 1, s);
    fwrite(j.px, 4, (size_t)largura * (size_t)alt, s);
    fclose(s);
    printf("titulo: %s\npecas: %d\nlinks: %d\nimagens: %d (%d carregadas)\naltura: %d\nnos: %d\n", doc->titulo, p->qtd,
           p->nlinks, p->nimgs, carregadas, p->altura, doc->qtd_nos);
    return 0;
}
