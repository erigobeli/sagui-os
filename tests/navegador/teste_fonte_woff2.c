/* teste_fonte_woff2.c - a tabela glyf transformada do WOFF2
 * (userland/navegador/fontes/fonte.c, woff2_glyf) com tamanhos que somados
 * passam de 4 GB. Antes, "deslocamento + tamanho" dava a volta nos 32 bits,
 * passava na conferência e os ponteiros das sete partes apontavam para fora
 * da tabela; agora a tabela é recusada (W3C WOFF2, seção 5.1: os tamanhos
 * das partes vêm do arquivo e não merecem confiança).
 * Uso: teste_fonte_woff2 (sem argumentos); sai com 0 se tudo deu certo. */
#include "../../userland/navegador/fontes/fonte.c"

/* fonte.c não abre arquivos nesse caminho; os nomes só precisam existir. */
int abrir(const char *caminho, int modo) { (void)caminho; (void)modo; return -1; }
int ler(int fd, void *buf, unsigned n) { (void)fd; (void)buf; (void)n; return -1; }
int fechar(int fd) { (void)fd; return -1; }

static void por32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static int tentar(const char *nome, const uint32_t tam[7], int aceitar)
{
    /* cabeçalho (36 bytes) e 64 bytes de partes de verdade */
    unsigned n = 36 + 64;
    uint8_t *d = calloc(1, n);
    d[4] = 0; d[5] = 1;                      /* um glifo */
    for (int i = 0; i < 7; i++) por32(d + 8 + i * 4, tam[i]);
    fonte_t *f = calloc(1, sizeof *f);
    int r = woff2_glyf(f, d, n);
    fonte_fechar(f);
    free(d);
    printf("%s: %s\n", nome, r ? "aceita" : "recusada");
    return (r != 0) == aceitar;
}

int main(void)
{
    int ok = 1;
    /* 36 + 4 + 0xFFFFFFFC dá 36 em 32 bits: cabia na conferência antiga. */
    const uint32_t volta1[7] = { 4, 0xFFFFFFFCu, 0, 0, 0, 0, 0 };
    /* a volta na última parte, depois de seis partes válidas */
    const uint32_t volta2[7] = { 2, 2, 2, 2, 2, 2, 0xFFFFFFF0u };
    /* tamanho maior que o arquivo, sem dar a volta */
    const uint32_t grande[7] = { 4, 4, 4, 4, 4, 4, 1000 };
    /* controle: um glifo vazio (zero contornos) e partes que cabem */
    const uint32_t valida[7] = { 2, 0, 0, 0, 0, 0, 0 };
    ok &= tentar("glifo vazio válido", valida, 1);
    ok &= tentar("parte 2 dá a volta nos 32 bits", volta1, 0);
    ok &= tentar("parte 7 dá a volta nos 32 bits", volta2, 0);
    ok &= tentar("parte 7 maior que a tabela", grande, 0);
    return ok ? 0 : 1;
}
