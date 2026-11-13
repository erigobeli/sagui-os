/* Textos: são vetores de char terminados em zero; a libsagui tem as funções. */
#include "sagui.h"

static void maiusculas(char *s)
{
    for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
}

static int conta(const char *s, char c)
{
    int n = 0;
    while (*s) if (*s++ == c) n++;
    return n;
}

int principal(int argc, char **argv)
{
    char buf[40];
    const char *frase = "o sagui pulou no galho";
    (void)argc; (void)argv;
    copia_texto(buf, "Sagui", sizeof buf);
    junta_texto(buf, "OS", sizeof buf);
    imprimir("%s %d\n", buf, (int)tam_texto(buf));
    maiusculas(buf);
    imprimir("%s\n", buf);
    imprimir("%d %d %d\n", compara_texto("abc", "abc"), compara_texto("abc", "abd") < 0, compara_texto("b", "a") > 0);
    imprimir("%d %d\n", conta(frase, 'o'), conta(frase, 'z'));
    char *g = acha_char(frase, 'g');
    imprimir("%s | %s\n", g, acha_ultimo_char(frase, 'g'));
    imprimir("%d %d\n", comeca_com(frase, "o sagui"), comeca_com(frase, "sagui"));
    imprimir("%d %d\n", texto_para_int("1234") + 1, texto_para_int("-56"));
    char juntos[] = "um" " dois" " tres";
    imprimir("%s %d\n", juntos, (int)sizeof juntos);
    imprimir("aspas \" barra \\ tab[\t] hexa \x41 octal \101\n");
    char curto[4] = "abc";
    curto[1] = 0;
    imprimir("%s %d\n", curto, (int)tam_texto(curto));
    imprimir("%c%c%c\n", "xyz"[0], "xyz"[2], *"q");
    const char *acento = "ação";
    imprimir("%d\n", (int)tam_texto(acento));     /* 4 letras: o texto fica em Latin-1 */
    return 0;
}
