/* fetch - exibe informações do sistema no estilo neofetch (SaguiOS).
 *
 * Mostra: nome do sistema, versão, tempo ligado, memória, processos,
 * resolução de vídeo e data/hora. Tudo usando as chamadas de sistema
 * próprias do SaguiOS (int 0x80) e a libsagui. Sem libc.
 *
 * Uso: fetch
 */

#include "sagui.h"

#ifndef MAX_PROCESSOS
#define MAX_PROCESSOS 64
#endif

/* Arte ASCII do sagui (desenhada no projeto). */
static const char *arte_sagui[] = {
    "       ___       ",
    "      /   \\      ",
    "     /  o o\\     ",
    "    (  >  < )    ",
    "     \\  -- /     ",
    "      /    \\     ",
    "     /  /\\  \\    ",
    "    /  /  \\  \\   ",
    "   /  /    \\  \\  ",
    NULL
};

/* Escreve uma linha label: valor no terminal (fd 1). */
static void imprime_linha(const char *label, const char *valor)
{
    char linha[128];
    int n = 0;
    for (int i = 0; label[i]; i++) linha[n++] = label[i];
    for (int i = 0; i < 16 - n && n < 127; i++) linha[n++] = ' ';
    if (n > 16) n = 16;
    linha[n++] = ':';
    linha[n++] = ' ';
    for (int i = 0; valor[i] && n < 127; i++) linha[n++] = valor[i];
    linha[n] = 0;
    escrever_texto(SAIDA, linha);
    escrever_texto(SAIDA, "\n");
}

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

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* Cabeçalho com arte ASCII. */
    escrever_texto(SAIDA, "\n");
    for (int i = 0; arte_sagui[i]; i++) {
        escrever_texto(SAIDA, arte_sagui[i]);
        escrever_texto(SAIDA, "\n");
    }

    /* Versão do sistema. */
    info_versao_t iv;
    if (info_sistema(INFO_VERSAO, &iv, sizeof iv) == 0) {
        char versao[64];
        copia_texto(versao, iv.versao, sizeof versao);
        imprime_linha("Sistema", "SaguiOS");
        imprime_linha("Versao", versao);

        char img[32];
        texto_int(img, sizeof img, iv.imagem_bytes);
        imprime_linha("Imagem", junta_texto(img, " bytes", sizeof(img) + 8));

        char tempo[32];
        unsigned h = iv.segundos_ligado / 3600;
        unsigned m = (iv.segundos_ligado % 3600) / 60;
        unsigned s = iv.segundos_ligado % 60;
        formatar_texto(tempo, sizeof tempo, "%02u:%02u:%02u", h, m, s);
        imprime_linha("Ligado a", tempo);

        char res[32];
        formatar_texto(res, sizeof res, "%ux%u", iv.largura, iv.altura);
        imprime_linha("Video", res);
    } else {
        imprime_linha("Sistema", "SaguiOS");
        imprime_linha("Versao", "desconhecida");
    }

    /* Memória. */
    info_memoria_t mem;
    if (info_sistema(INFO_MEMORIA, &mem, sizeof mem) == 0) {
        char total[32], livre[32], usada[32];
        texto_int(total, sizeof total, mem.ram_total_kb);
        texto_int(livre, sizeof livre, mem.livre_kb);
        texto_int(usada, sizeof usada, mem.usada_kb);

        char t2[48], l2[48], u2[48];
        junta_texto(t2, total, sizeof t2);
        junta_texto(t2, " KB", sizeof t2);
        junta_texto(l2, livre, sizeof l2);
        junta_texto(l2, " KB", sizeof l2);
        junta_texto(u2, usada, sizeof u2);
        junta_texto(u2, " KB", sizeof u2);

        imprime_linha("Memoria total", t2);
        imprime_linha("Memoria livre", l2);
        imprime_linha("Memoria usada", u2);
    }

    /* Conta processos. */
    info_processo_t procs[MAX_PROCESSOS];
    int qtd = info_sistema(INFO_PROCESSOS, procs, sizeof procs);
    if (qtd > 0) {
        char nproc[16];
        texto_int(nproc, sizeof nproc, qtd);
        imprime_linha("Processos", nproc);
    }

    /* Data e hora. */
    data_t dt;
    if (ler_data(&dt) == 0) {
        char data[32];
        formatar_texto(data, sizeof data, "%02u/%02u/%u %02u:%02u:%02u",
                       dt.dia, dt.mes, dt.ano, dt.hora, dt.minuto, dt.segundo);
        imprime_linha("Data/hora", data);
    }

    escrever_texto(SAIDA, "\n");
    return 0;
}
