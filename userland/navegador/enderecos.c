/* enderecos.c - endereços da web no navegador (H62): resolver um link
 * relativo em relação à página (RFC 3986, o essencial: "//servidor/...",
 * "/caminho", "?consulta", "#ancora", "pasta/arquivo", "../"), e codificar
 * texto e endereços com %XX em UTF-8, que é o que os servidores esperam. */
#include "navegador.h"

bool endereco_tem_esquema(const char *u)
{
    return comeca_com(u, "http://") || comeca_com(u, "https://") || comeca_com(u, "sagui:");
}

/* Resolve "rel" em relação a "b" (RFC 3986, o essencial). */
void endereco_resolver(const char *b, const char *rel, char *out, int max)
{
    while (*rel == ' ' || *rel == '\t' || *rel == '\n') rel++;
    if (endereco_tem_esquema(rel) || rel[0] == 0) { copia_texto(out, rel[0] ? rel : b, (size_t)max); return; }
    if (comeca_com(rel, "javascript:") || comeca_com(rel, "mailto:") || comeca_com(rel, "data:")) { copia_texto(out, rel, (size_t)max); return; }
    char esquema[16] = "https:";
    const char *dp = acha_char(b, ':');
    if (dp && dp - b < 10) { memcpy(esquema, b, (size_t)(dp - b + 1)); esquema[dp - b + 1] = 0; }
    if (rel[0] == '/' && rel[1] == '/') { formatar_texto(out, (size_t)max, "%s%s", esquema, rel); return; }
    /* Separa servidor e caminho da base. */
    const char *inicio_caminho = b;
    if (comeca_com(b, "http://") || comeca_com(b, "https://")) {
        inicio_caminho = acha_char(b + 8, '/');
        if (!inicio_caminho) inicio_caminho = b + tam_texto(b);
    } else if (b[0] == '/') inicio_caminho = b;
    int tam_raiz = (int)(inicio_caminho - b);
    char caminho[MAX_URL_NAV];
    if (rel[0] == '/') copia_texto(caminho, rel, sizeof caminho);
    else if (rel[0] == '?' || rel[0] == '#') {
        copia_texto(caminho, inicio_caminho[0] ? inicio_caminho : "/", sizeof caminho);
        char *corte = NULL;
        for (char *p = caminho; *p; p++) if (*p == (rel[0] == '?' ? '?' : '#') || (rel[0] == '?' && *p == '#')) { corte = p; break; }
        if (rel[0] == '#') { char *q = acha_char(caminho, '#'); if (q) *q = 0; }
        else if (corte) *corte = 0;
        junta_texto(caminho, rel, sizeof caminho);
    } else {
        copia_texto(caminho, inicio_caminho[0] ? inicio_caminho : "/", sizeof caminho);
        char *q = acha_char(caminho, '?');
        if (q) *q = 0;
        q = acha_char(caminho, '#');
        if (q) *q = 0;
        char *barra = acha_ultimo_char(caminho, '/');
        if (barra) barra[1] = 0; else copia_texto(caminho, "/", sizeof caminho);
        junta_texto(caminho, rel, sizeof caminho);
    }
    /* Tira "./" e "x/../" do caminho (só da parte antes do '?'). */
    char limpo[MAX_URL_NAV];
    int k = 0;
    const char *p = caminho;
    const char *fim_caminho = p;
    while (*fim_caminho && *fim_caminho != '?' && *fim_caminho != '#') fim_caminho++;
    while (p < fim_caminho && k < (int)sizeof limpo - 1) {
        if (comeca_com(p, "/./")) { p += 2; continue; }
        if (comeca_com(p, "/../") || (p + 3 == fim_caminho && comeca_com(p, "/.."))) {
            while (k > 0 && limpo[k - 1] != '/') k--;
            if (k > 0) k--;
            p += 3;
            if (p >= fim_caminho) { limpo[k++] = '/'; }
            continue;
        }
        limpo[k++] = *p++;
    }
    limpo[k] = 0;
    junta_texto(limpo, fim_caminho, sizeof limpo);
    if (k == 0 || limpo[0] != '/') { char t[MAX_URL_NAV]; formatar_texto(t, sizeof t, "/%s", limpo); copia_texto(limpo, t, sizeof limpo); }
    /* (o formatar_texto do SaguiOS não tem "%.*s") */
    if (tam_raiz > max - 1) tam_raiz = max - 1;
    memcpy(out, b, (size_t)tam_raiz);
    copia_texto(out + tam_raiz, limpo, (size_t)(max - tam_raiz));
}

/* Latin-1 -> UTF-8 -> %XX (o que os servidores esperam num endereço). */
void endereco_codificar(const char *s, char *out, int max)
{
    static const char hexa[] = "0123456789ABCDEF";
    int k = 0;
    for (; *s && k < max - 7; s++) {
        uint8_t c = (uint8_t)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out[k++] = (char)c;
        else if (c == ' ') out[k++] = '+';
        else if (c < 0x80) { out[k++] = '%'; out[k++] = hexa[c >> 4]; out[k++] = hexa[c & 15]; }
        else {
            uint8_t a = (uint8_t)(0xC0 | (c >> 6)), b = (uint8_t)(0x80 | (c & 0x3F));
            out[k++] = '%'; out[k++] = hexa[a >> 4]; out[k++] = hexa[a & 15];
            out[k++] = '%'; out[k++] = hexa[b >> 4]; out[k++] = hexa[b & 15];
        }
    }
    out[k] = 0;
}

/* Endereços com acentos ou espaços (vindos de links Latin-1) viram %XX. */
void endereco_para_rede(const char *u, char *out, int max)
{
    static const char hexa[] = "0123456789ABCDEF";
    int k = 0;
    for (; *u && k < max - 7; u++) {
        uint8_t c = (uint8_t)*u;
        if (c == '#') break;                              /* o fragmento não vai para o servidor */
        if (c > 0x20 && c < 0x7F) { out[k++] = (char)c; continue; }
        if (c < 0x80) { out[k++] = '%'; out[k++] = hexa[c >> 4]; out[k++] = hexa[c & 15]; continue; }
        uint8_t a = (uint8_t)(0xC0 | (c >> 6)), b = (uint8_t)(0x80 | (c & 0x3F));
        out[k++] = '%'; out[k++] = hexa[a >> 4]; out[k++] = hexa[a & 15];
        out[k++] = '%'; out[k++] = hexa[b >> 4]; out[k++] = hexa[b & 15];
    }
    out[k] = 0;
}

