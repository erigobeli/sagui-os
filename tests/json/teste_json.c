/* teste_json.c - lê um JSON e escreve de novo com o escritor da libsagui
 * (os números saem como vieram). Saída 1 = o leitor recusou (e o motivo). */
#include "json.h"

static void escrever(json_escritor_t *e, const json_t *j)
{
    switch (j->tipo) {
    case JSON_NULO: je_nulo(e); break;
    case JSON_BOOL: je_bool(e, j->valor != 0); break;
    case JSON_NUMERO: je_bruto(e, j->texto, j->tam); break;
    case JSON_TEXTO: je_texto_n(e, j->texto, j->tam); break;
    case JSON_LISTA:
        je_lista(e);
        for (json_t *c = j->filho; c; c = c->prox) escrever(e, c);
        je_fim_lista(e);
        break;
    case JSON_OBJETO:
        je_objeto(e);
        for (json_t *c = j->filho; c; c = c->prox) { je_chave(e, c->nome); escrever(e, c); }
        je_fim_objeto(e);
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    static char d[1 << 22];
    unsigned n = (unsigned)fread(d, 1, sizeof d, f);
    fclose(f);
    char erro[128];
    json_t *j = json_ler(d, n, erro, sizeof erro);
    if (!j) { printf("ERRO: %s\n", erro); return 1; }
    json_escritor_t e;
    je_iniciar(&e);
    escrever(&e, j);
    fwrite(e.d, 1, e.n, stdout);
    if (argc > 2) {   /* consultas: campo.campo[0]... só um nível: nome */
        json_t *c = json_campo(j, argv[2]);
        fprintf(stderr, "campo %s: tipo %d int %d texto %s\n", argv[2], c ? c->tipo : -1, json_int(c, -99), json_texto(c));
    }
    je_liberar(&e);
    json_liberar(j);
    return 0;
}
