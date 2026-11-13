/* uname - exibe informações do sistema (SaguiOS).
 *
 * Sem argumentos mostra o nome do sistema. Com -a mostra tudo:
 *   sistema, versão, release, tempo ligado, memória e vídeo.
 *
 * Usa apenas syscalls int 0x80 + libsagui. Sem libc.
 */

#include "sagui.h"

/* Protótipos de auxiliares locais (definidos depois de principal). */
static void texto_int(char *buf, unsigned max, int v);
static void formato_texto(char *dst, size_t max, const char *fmt, ...);

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    bool tudo = false;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-a") == 0) tudo = true;
    }

    info_versao_t iv;
    bool tem_versao = (info_sistema(INFO_VERSAO, &iv, sizeof iv) == 0);

    if (!tudo) {
        imprimir("SaguiOS\n");
        return 0;
    }

    imprimir("Sistema: SaguiOS\n");
    if (tem_versao) {
        imprimir("Versao: ");
        imprimir(iv.versao);
        imprimir("\n");
        char release[64];
        formato_texto(release, sizeof release, "%u bytes", iv.imagem_bytes);
        imprimir("Release: ");
        imprimir(release);
        imprimir("\n");

        unsigned h = iv.segundos_ligado / 3600;
        unsigned m = (iv.segundos_ligado % 3600) / 60;
        unsigned s = iv.segundos_ligado % 60;
        char tempo[32];
        formato_texto(tempo, sizeof tempo, "%02u:%02u:%02u", h, m, s);
        imprimir("Tempo ligado: ");
        imprimir(tempo);
        imprimir("\n");

        char video[32];
        formato_texto(video, sizeof video, "%ux%u", iv.largura, iv.altura);
        imprimir("Video: ");
        imprimir(video);
        imprimir("\n");
    }

    info_memoria_t mem;
    if (info_sistema(INFO_MEMORIA, &mem, sizeof mem) == 0) {
        char t[32], l[32], u[32];
        texto_int(t, sizeof t, mem.ram_total_kb);
        texto_int(l, sizeof l, mem.livre_kb);
        texto_int(u, sizeof u, mem.usada_kb);
        char tt[48], ll[48], uu[48];
        junta_texto(tt, t, sizeof tt);
        junta_texto(tt, " KB total", sizeof tt);
        junta_texto(ll, l, sizeof ll);
        junta_texto(ll, " KB livre", sizeof ll);
        junta_texto(uu, u, sizeof uu);
        junta_texto(uu, " KB usada", sizeof uu);
        imprimir("Memoria: ");
        imprimir(tt);
        imprimir(" / ");
        imprimir(ll);
        imprimir(" / ");
        imprimir(uu);
        imprimir("\n");
    }

    return 0;
}

/* Versão local de texto_int (libsagui nao exporta esse nome publicamente). */
static void texto_int(char *buf, unsigned max, int v)
{
    if (v == 0) { copia_texto(buf, "0", max); return; }
    char tmp[16];
    int n = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)(-v) : (unsigned)v;
    while (u && n < 15) { tmp[n++] = (char)('0' + u % 10); u /= 10; }
    if (neg) tmp[n++] = '-';
    size_t k = 0;
    while (n) { if (k < max - 1) buf[k++] = tmp[--n]; }
    buf[k] = 0;
}

/* Versão local de formato_texto (wrapper do formatar_texto da libsagui). */
static void formato_texto(char *dst, size_t max, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    formatar_lista(dst, max, fmt, ap);
    va_end(ap);
}
