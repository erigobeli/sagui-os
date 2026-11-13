/* som - a placa de som pelo Terminal (H40, H41).
 *   som                     diz se há placa de som e o volume
 *   som teste [Hz [ms]]     toca uma senoide (padrão: 440 Hz por 1 s)
 *   som volume [0-100]      mostra ou muda o volume (guardado nas configurações)
 *
 * A senoide é calculada em inteiros com a aproximação de Bhaskara para o seno
 * (uma fração de polinômios, precisa a menos de 0,2% para um tom de teste),
 * porque os programas do SaguiOS não usam números de ponto flutuante. */
#include "sagui.h"

/* seno(graus) * 1000, de 0 a 359, pela aproximação de Bhaskara I. */
static int seno_mil(int graus)
{
    int sinal = 1;
    graus %= 360;
    if (graus >= 180) { graus -= 180; sinal = -1; }
    int x = graus * (180 - graus);
    return sinal * (4000 * x) / (40500 - x);
}

static int numero(const char *s, int padrao)
{
    if (!s || *s < '0' || *s > '9') return padrao;
    return texto_para_int(s);
}

int tocar_tom(int freq, int ms, int volume_onda)
{
    int f = som_abrir();
    if (f < 0) return f;
    static short bloco[2 * 960];          /* 20 ms */
    int total = SOM_TAXA / 1000 * ms, feitos = 0;
    unsigned fase = 0;                     /* em 1/1000 de grau */
    unsigned passo = (unsigned)freq * 360000u / SOM_TAXA;
    while (feitos < total) {
        int n = total - feitos < 960 ? total - feitos : 960;
        for (int i = 0; i < n; i++) {
            int v = seno_mil((int)(fase / 1000)) * volume_onda / 1000;
            bloco[2 * i] = (short)v;
            bloco[2 * i + 1] = (short)v;
            fase = (fase + passo) % 360000u;
        }
        int enviados = 0;
        while (enviados < n * 4) {
            int r = som_enviar(f, (char *)bloco + enviados, (unsigned)(n * 4 - enviados));
            if (r < 0) { som_fechar(f); return r; }
            enviados += r;
            if (enviados < n * 4) dormir(10);   /* a fonte está cheia: espera o misturador consumir */
        }
        feitos += n;
    }
    dormir(400);                           /* deixa tocar o que ficou na fonte */
    som_fechar(f);
    return 0;
}

int principal(int argc, char **argv)
{
    int vol = som_volume(-1);
    if (argc < 2) {
        if (vol < 0) imprimir("Sem placa de som HD Audio: o SaguiOS usa o bip do alto-falante do PC.\n");
        else imprimir("Placa de som HD Audio ligada. Volume: %d%%\n", vol);
        imprimir("uso: som teste [Hz [ms]] | som volume [0-100]\n");
        return 0;
    }
    if (vol < 0) { imprimir_em(ERROS, "som: sem placa de som HD Audio\n"); return 1; }
    if (compara_texto(argv[1], "volume") == 0) {
        if (argc > 2) vol = som_volume(numero(argv[2], vol));
        imprimir("Volume: %d%%\n", vol);
        return 0;
    }
    if (compara_texto(argv[1], "teste") == 0) {
        int freq = numero(argc > 2 ? argv[2] : NULL, 440), ms = numero(argc > 3 ? argv[3] : NULL, 1000);
        if (freq < 20 || freq > 20000 || ms < 10 || ms > 60000) { imprimir_em(ERROS, "som: valores fora da faixa\n"); return 1; }
        imprimir("Tocando %d Hz por %d ms...\n", freq, ms);
        registrar("som: teste %d Hz %d ms", freq, ms);
        int r = tocar_tom(freq, ms, 12000);
        if (r < 0) { imprimir_em(ERROS, "som: %s\n", nome_erro(r)); return 1; }
        return 0;
    }
    imprimir_em(ERROS, "uso: som teste [Hz [ms]] | som volume [0-100]\n");
    return 1;
}
