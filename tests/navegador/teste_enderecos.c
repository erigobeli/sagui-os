/* teste_enderecos.c - confere endereco_resolver com os exemplos da RFC 3986
 * (seção 5.4.1, base http://a/b/c/d;p?q) e alguns casos do navegador, e a
 * codificação dos formulários. Imprime "ok" ou as diferenças. */
#include "../../userland/navegador/navegador.h"

static int falhas;

static void caso(const char *base, const char *rel, const char *esperado)
{
    char out[1024];
    endereco_resolver(base, rel, out, sizeof out);
    if (strcmp(out, esperado) != 0) { printf("FALHA: %s + %s = %s (esperado %s)\n", base, rel, out, esperado); falhas++; }
}

int main(void)
{
    const char *b = "http://a/b/c/d;p?q";
    caso(b, "g", "http://a/b/c/g");
    caso(b, "./g", "http://a/b/c/g");
    caso(b, "g/", "http://a/b/c/g/");
    caso(b, "/g", "http://a/g");
    caso(b, "//g", "http://g");
    caso(b, "?y", "http://a/b/c/d;p?y");
    caso(b, "g?y", "http://a/b/c/g?y");
    caso(b, "#s", "http://a/b/c/d;p?q#s");
    caso(b, "g#s", "http://a/b/c/g#s");
    caso(b, "g?y#s", "http://a/b/c/g?y#s");
    caso(b, ";x", "http://a/b/c/;x");
    caso(b, "", "http://a/b/c/d;p?q");
    caso(b, "..", "http://a/b/");
    caso(b, "../", "http://a/b/");
    caso(b, "../g", "http://a/b/g");
    caso(b, "../..", "http://a/");
    caso(b, "../../g", "http://a/g");
    caso(b, "../../../g", "http://a/g");
    caso("https://pt.wikipedia.org/wiki/Sagui", "//upload.wikimedia.org/x.png", "https://upload.wikimedia.org/x.png");
    caso("https://pt.wikipedia.org/wiki/Sagui", "/wiki/Primatas", "https://pt.wikipedia.org/wiki/Primatas");
    caso("https://pt.wikipedia.org/wiki/Sagui", "#Taxonomia", "https://pt.wikipedia.org/wiki/Sagui#Taxonomia");
    caso("http://10.0.2.2:8000/teste1.html", "imagens/sagui.png", "http://10.0.2.2:8000/imagens/sagui.png");
    caso("http://10.0.2.2:8000", "pagina2.html", "http://10.0.2.2:8000/pagina2.html");
    caso("/usb/paginas/a.html", "b.html", "/usb/paginas/b.html");
    caso("https://example.com/", "https://outro.org/x", "https://outro.org/x");
    char out[256];
    endereco_codificar("sagui a\xe7\xe3o & 1+1=2", out, sizeof out);
    if (strcmp(out, "sagui+a%C3%A7%C3%A3o+%26+1%2B1%3D2") != 0) { printf("FALHA codificar: %s\n", out); falhas++; }
    endereco_para_rede("https://pt.wikipedia.org/wiki/Mico-le\xe3o#topo", out, sizeof out);
    if (strcmp(out, "https://pt.wikipedia.org/wiki/Mico-le%C3%A3o") != 0) { printf("FALHA para_rede: %s\n", out); falhas++; }
    if (!falhas) printf("ok: 25 endereços resolvidos e 2 codificações certas\n");
    return falhas != 0;
}
