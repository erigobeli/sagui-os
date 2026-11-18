/* idioma.c - os textos dos programas em português, espanhol ou inglês (D073, H124).
 *
 * O código do SaguiOS é escrito em português, e os textos que aparecem para
 * quem usa também. Para mostrá-los em outra língua, o programa passa o texto
 * por traduzir(): traduzir("Relógio") devolve "Reloj" com o espanhol escolhido, "Clock"
 * com o inglês e o próprio "Relógio" com o português (ou quando a frase ainda
 * não foi traduzida). Assim o texto em português continua no código, legível,
 * e serve de chave para a tradução.
 *
 * A língua é a configuração "sistema.idioma" (pt, es ou en; o padrão é pt). As
 * traduções ficam em arquivos de texto simples, /sistema/idiomas/es.txt e
 * en.txt, uma por linha:
 *
 *     texto em português = tradução
 *
 * Linhas vazias e as que começam com # são ignoradas. O separador é o primeiro
 * " = " (igual com um espaço de cada lado); um " = " que faça parte do texto é
 * escrito com uma barra, "\=". Os arquivos são UTF-8, como qualquer arquivo de
 * texto de hoje, e viram Latin-1 na leitura, que é como os programas guardam
 * texto (D027).
 *
 * Os textos com %s, %d etc. são passados ao formatar_texto: se a tradução não
 * tiver as mesmas marcas, na mesma ordem, o programa poderia ler um argumento
 * que não existe. Por isso uma tradução assim é ignorada (vale o português) e
 * o problema vai para o log do sistema.
 *
 * O arquivo é lido uma vez, no primeiro traduzir() do programa; trocar a língua nas
 * Configurações vale para os programas abertos depois. Quem quiser perceber a
 * troca sem reabrir (o shell, a cada comando) chama idioma_recarregar(). */
#include "sagui.h"

#define MAX_TRADUCOES 1500

typedef struct { const char *pt, *outro; } traducao_t;

static char        escolhido[4];   /* "" = ainda não leu a configuração */
static uint8_t    *texto;          /* o arquivo inteiro; as entradas apontam para dentro dele */
static traducao_t *tabela;
static int         qtd;

static void ler_configuracao(char lingua[4])
{
    char v[16];
    if (config_ler("sistema.idioma", v, sizeof v) >= 0 && (compara_texto(v, "es") == 0 || compara_texto(v, "en") == 0))
        copia_texto(lingua, v, 4);
    else
        copia_texto(lingua, "pt", 4);
}

/* Tira as barras de "\=" no lugar. */
static void desfazer_barras(char *s)
{
    char *d = s;
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] == '=') continue;
        *d++ = *s;
    }
    *d = 0;
}

/* Acha a próxima marca de formato (%-20s, %d...) a partir de s: devolve o
 * começo dela e põe o tamanho em *n; NULL se não houver mais. "%%" não conta,
 * porque não consome argumento. */
static const char *proxima_marca(const char *s, int *n)
{
    for (; *s; s++) {
        if (*s != '%') continue;
        if (s[1] == '%') { s++; continue; }
        const char *f = s + 1;
        while (*f == '-' || *f == '0' || *f == '*' || *f == '.' || (*f >= '1' && *f <= '9')) f++;
        if (!*f) return NULL;
        *n = (int)(f - s) + 1;
        return s;
    }
    return NULL;
}

static bool mesmas_marcas(const char *a, const char *b)
{
    for (;;) {
        int na = 0, nb = 0;
        a = proxima_marca(a, &na);
        b = proxima_marca(b, &nb);
        if (!a || !b) return !a && !b;
        if (na != nb || compara_texto_n(a, b, (size_t)na) != 0) return false;
        a += na;
        b += nb;
    }
}

static void carregar(void)
{
    if (compara_texto(escolhido, "pt") == 0) return;
    char caminho[40];
    formatar_texto(caminho, sizeof caminho, "/sistema/idiomas/%s.txt", escolhido);
    int n = ler_arquivo_inteiro(caminho, &texto);
    if (n < 0) { registrar("idioma: sem %s (%s)", caminho, nome_erro(n)); return; }
    n = (int)utf8_para_latin1((char *)texto, (size_t)n, NULL);
    texto[n] = 0;
    tabela = aloca(MAX_TRADUCOES * sizeof *tabela);
    if (!tabela) return;
    char *linha = (char *)texto;
    while (*linha && qtd < MAX_TRADUCOES) {
        char *fim = acha_char(linha, '\n');
        char *proxima = fim ? fim + 1 : linha + tam_texto(linha);
        if (fim) *fim = 0;
        /* Tira espaços e o \r (arquivo salvo no Windows) do fim e do começo. */
        size_t t = tam_texto(linha);
        while (t > 0 && eh_espaco(linha[t - 1])) linha[--t] = 0;
        while (eh_espaco(*linha)) linha++;
        char *sep = linha;
        while (*linha != '#' && (sep = acha_char(sep, '=')) != NULL)
            if (sep > linha && sep[-1] == ' ' && sep[1] == ' ') break;
            else sep++;
        if (*linha && *linha != '#' && sep) {
            sep[-1] = 0;
            char *pt = linha, *outro = sep + 2;
            desfazer_barras(pt);
            desfazer_barras(outro);
            if (!mesmas_marcas(pt, outro)) registrar("idioma: %s: marcas de formato diferentes em \"%s\"", escolhido, pt);
            else if (*outro) tabela[qtd++] = (traducao_t){ pt, outro };
        }
        linha = proxima;
    }
    registrar("idioma: %s, %d traduções", escolhido, qtd);
}

const char *idioma(void)
{
    if (!escolhido[0]) {
        ler_configuracao(escolhido);
        carregar();
    }
    return escolhido;
}

void idioma_recarregar(void)
{
    char agora[4];
    ler_configuracao(agora);
    if (escolhido[0] && compara_texto(agora, escolhido) == 0) return;
    if (tabela) libera(tabela);
    if (texto) libera(texto);
    tabela = NULL;
    texto = NULL;
    qtd = 0;
    copia_texto(escolhido, agora, sizeof escolhido);
    carregar();
}

const char *traduzir(const char *texto_pt)
{
    idioma();
    for (int i = 0; i < qtd; i++)
        if (compara_texto(tabela[i].pt, texto_pt) == 0) return tabela[i].outro;
    return texto_pt;
}
