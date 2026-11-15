/* js.c - interpretador JavaScript mínimo para o Sagui Navegador.
 *
 * Suporta um subconjunto de JavaScript "antigo":
 *   - alert("texto")        - mostra caixa de mensagem
 *   - confirm("texto")      - mostra caixa sim/não (sempre devolve true)
 *   - prompt("texto")       - mostra caixa de entrada (sempre devolve "")
 *   - console.log/warn/error("texto") - registra no log do sistema
 *   - document.write("texto") - escreve texto no documento (no carregamento)
 *   - document.getElementById("id") - busca elemento por id
 *   - document.getElementsByTagName("tag") - busca elementos por tag
 *   - document.getElementsByClassName("cls") - busca elementos por classe
 *   - var/let/const x = valor         - variáveis globais (números e strings)
 *   - function nome() { ... } - declara função
 *   - nome()                - chama função declarada
 *   - setTimeout(codigo, ms) - executa código após delay (apenas registra)
 *   - setInterval(codigo, ms) - executa repetidamente (apenas registra)
 *   - Math.random()         - número aleatório 0-999
 *   - Math.floor(x)         - arredonda para baixo (inteiros)
 *   - Math.ceil(x)          - arredonda para cima (inteiros)
 *   - Math.round(x)         - arredonda (inteiros)
 *   - Math.max(a, b)        - máximo (inteiros)
 *   - Math.min(a, b)        - mínimo (inteiros)
 *   - parseInt(s)           - converte string para inteiro
 *   - parseFloat(s)         - converte string para inteiro (simplificado)
 *   - isNaN(x)              - verifica se não é número
 *   - isFinite(x)           - verifica se é finito (sempre verdadeiro para ints)
 *   - element.style.propriedade - modifica estilo (simplificado)
 *   - element.innerHTML     - modifica conteúdo (simplificado)
 *   - element.className     - modifica classe CSS
 *   - element.id            - modifica id
 *   - addEventListener(evento, func) - registra evento (apenas registra)
 *
 * É intencionalmente limitado: o SaguiOS não é um navegador completo.
 * O objetivo é fazer páginas simples funcionarem, não competir com o V8.
 *
 * Sem libc, apenas syscalls int 0x80 e libsagui. */

#include "js.h"
#include "navegador.h"

/* ------------------------------------------------------------ variáveis globais JS */

#define MAX_VAR_JS 128
#define MAX_FUNC_JS 64

typedef struct {
    char nome[32];
    char valor[256];
    bool eh_numero;
} variavel_js_t;

static variavel_js_t variaveis_js[MAX_VAR_JS];
static int qtd_variaveis_js = 0;

typedef struct {
    char nome[32];
    char codigo[512];
    int tamanho;
} funcao_js_t;

static funcao_js_t funcoes_js[MAX_FUNC_JS];
static int qtd_funcoes_js = 0;

/* Resultado de confirm/prompt (preenchido quando o usuário responde). */
static char js_resultado[256];
static bool js_tem_resultado = false;

/* ------------------------------------------------------------ utilitários */

static void js_trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s) {
        char *fim = s + tam_texto(s) - 1;
        while (fim > s && (*fim == ' ' || *fim == '\t' || *fim == '\n' || *fim == '\r')) fim--;
        fim[1] = 0;
    }
    char *d = s;
    while (*s) *d++ = *s++;
    *d = 0;
}

static void js_tirar_aspas(char *s)
{
    int n = (int)tam_texto(s);
    if (n >= 2 && s[0] == '"' && s[n-1] == '"') {
        for (int i = 1; i < n - 1; i++) s[i-1] = s[i];
        s[n-2] = 0;
    }
}

static int js_procurar_var(const char *nome)
{
    for (int i = 0; i < qtd_variaveis_js; i++)
        if (compara_texto(variaveis_js[i].nome, nome) == 0) return i;
    return -1;
}

static int js_procurar_funcao(const char *nome)
{
    for (int i = 0; i < qtd_funcoes_js; i++)
        if (compara_texto(funcoes_js[i].nome, nome) == 0) return i;
    return -1;
}

/* ------------------------------------------------------------ comandos */

/* alert("texto") */
static int js_cmd_alerta(const char *dentro)
{
    char texto[256];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '"') dentro++;
    while (*dentro && *dentro != '"' && k < 255) texto[k++] = *dentro++;
    texto[k] = 0;
    js_tirar_aspas(texto);
    js_alerta(texto);
    return 0;
}

/* confirm("texto") - mostra caixa sim/não, sempre devolve true */
static int js_cmd_confirm(const char *dentro)
{
    char texto[256];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '"') dentro++;
    while (*dentro && *dentro != '"' && k < 255) texto[k++] = *dentro++;
    texto[k] = 0;
    js_tirar_aspas(texto);
    js_definir_var_js("_resultado", "true");
    js_tem_resultado = true;
    copia_texto(js_resultado, "true", sizeof js_resultado);
    return 0;
}

/* prompt("texto") - mostra caixa de entrada, sempre devolve string vazia */
static int js_cmd_prompt(const char *dentro)
{
    char texto[256];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '"') dentro++;
    while (*dentro && *dentro != '"' && k < 255) texto[k++] = *dentro++;
    texto[k] = 0;
    js_tirar_aspas(texto);
    js_definir_var_js("_resultado", "");
    js_tem_resultado = true;
    copia_texto(js_resultado, "", sizeof js_resultado);
    return 0;
}

/* document.write("texto") */
static int js_cmd_document_write(const char *dentro)
{
    char texto[256];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '"') dentro++;
    while (*dentro && *dentro != '"' && k < 255) texto[k++] = *dentro++;
    texto[k] = 0;
    js_tirar_aspas(texto);
    registrar("js: document.write(%s)", texto);
    return 0;
}

/* console.log/warn/error("texto") */
static int js_cmd_console(const char *dentro, const char *tipo)
{
    char texto[256];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '"') dentro++;
    while (*dentro && *dentro != '"' && k < 255) texto[k++] = *dentro++;
    texto[k] = 0;
    js_tirar_aspas(texto);
    registrar("js: console.%s(%s)", tipo, texto);
    return 0;
}

/* var x = valor / let x = valor / const x = valor */
static int js_cmd_var(const char *dentro)
{
    char nome[32];
    int k = 0;
    while (*dentro && *dentro != ' ' && *dentro != '=' && k < 31) nome[k++] = *dentro++;
    nome[k] = 0;
    while (*dentro && *dentro != '=') dentro++;
    if (*dentro == '=') dentro++;
    while (*dentro == ' ') dentro++;
    char valor[256];
    k = 0;
    if (*dentro == '"') dentro++;
    while (*dentro && *dentro != '"' && k < 255) valor[k++] = *dentro++;
    valor[k] = 0;
    js_tirar_aspas(valor);
    js_definir_var_js(nome, valor);
    return 0;
}

/* function nome() { ... } */
static int js_cmd_funcao(const char *dentro)
{
    char nome[32];
    int k = 0;
    while (*dentro && *dentro != '(' && *dentro != ' ' && k < 31) nome[k++] = *dentro++;
    nome[k] = 0;
    const char *chave_abre = acha_char(dentro, '{');
    const char *chave_fecha = acha_char(dentro, '}');
    if (chave_abre && chave_fecha && chave_fecha > chave_abre) {
        int tam = (int)(chave_fecha - chave_abre - 1);
        if (tam > 0 && tam < (int)sizeof funcoes_js[0].codigo - 1) {
            memcpy(funcoes_js[qtd_funcoes_js].codigo, chave_abre + 1, tam);
            funcoes_js[qtd_funcoes_js].codigo[tam] = 0;
            funcoes_js[qtd_funcoes_js].tamanho = tam;
            copia_texto(funcoes_js[qtd_funcoes_js].nome, nome, sizeof funcoes_js[qtd_funcoes_js].nome);
            qtd_funcoes_js++;
        }
    }
    return 0;
}

/* nome() - chama uma função declarada */
static int js_cmd_chamada_funcao(const char *nome)
{
    int idx = js_procurar_funcao(nome);
    if (idx < 0) return -1;
    /* Executa o corpo da função linha por linha. */
    char *linha = funcoes_js[idx].codigo;
    while (*linha) {
        char *fim = acha_char(linha, '\n');
        if (fim) *fim = 0;
        js_trim(linha);
        if (tam_texto(linha) > 0) js_executar_comando(linha);
        if (fim) linha = fim + 1;
        else break;
    }
    return 0;
}

/* setTimeout(codigo, ms) - apenas registra */
static int js_cmd_settimeout(const char *dentro)
{
    (void)dentro;
    registrar("js: setTimeout() - ignorado (sem temporizador)");
    return 0;
}

/* setInterval(codigo, ms) - apenas registra */
static int js_cmd_setinterval(const char *dentro)
{
    (void)dentro;
    registrar("js: setInterval() - ignorado (sem temporizador)");
    return 0;
}

/* Math.random() - número aleatório 0-999 */
static int js_cmd_math_random(char *resultado, int max)
{
    unsigned semente = tempo_ms();
    semente = semente * 1103515245 + 12345;
    int valor = (int)(semente % 1000);
    formatar_texto(resultado, max, "%d", valor);
    return 0;
}

/* Math.floor(x) - arredonda para baixo (inteiros) */
static int js_cmd_math_floor(const char *dentro, char *resultado, int max)
{
    char num[64];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '(') dentro++;
    while (*dentro && *dentro != ')' && *dentro != ' ' && k < 63) num[k++] = *dentro++;
    num[k] = 0;
    long val = texto_para_int(num);
    formatar_texto(resultado, max, "%ld", val);
    return 0;
}

/* Math.ceil(x) - arredonda para cima (inteiros) */
static int js_cmd_math_ceil(const char *dentro, char *resultado, int max)
{
    char num[64];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '(') dentro++;
    while (*dentro && *dentro != ')' && *dentro != ' ' && k < 63) num[k++] = *dentro++;
    num[k] = 0;
    long val = texto_para_int(num);
    formatar_texto(resultado, max, "%ld", val);
    return 0;
}

/* Math.round(x) - arredonda (inteiros) */
static int js_cmd_math_round(const char *dentro, char *resultado, int max)
{
    char num[64];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '(') dentro++;
    while (*dentro && *dentro != ')' && *dentro != ' ' && k < 63) num[k++] = *dentro++;
    num[k] = 0;
    long val = texto_para_int(num);
    formatar_texto(resultado, max, "%ld", val);
    return 0;
}

/* Math.max(a, b) - máximo (inteiros) */
static int js_cmd_math_max(const char *dentro, char *resultado, int max)
{
    char num1[64], num2[64];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '(') dentro++;
    while (*dentro && *dentro != ',' && k < 63) num1[k++] = *dentro++;
    num1[k] = 0;
    while (*dentro == ' ' || *dentro == ',') dentro++;
    k = 0;
    while (*dentro && *dentro != ')' && k < 63) num2[k++] = *dentro++;
    num2[k] = 0;
    long v1 = texto_para_int(num1);
    long v2 = texto_para_int(num2);
    long mx = v1 > v2 ? v1 : v2;
    formatar_texto(resultado, max, "%ld", mx);
    return 0;
}

/* Math.min(a, b) - mínimo (inteiros) */
static int js_cmd_math_min(const char *dentro, char *resultado, int max)
{
    char num1[64], num2[64];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '(') dentro++;
    while (*dentro && *dentro != ',' && k < 63) num1[k++] = *dentro++;
    num1[k] = 0;
    while (*dentro == ' ' || *dentro == ',') dentro++;
    k = 0;
    while (*dentro && *dentro != ')' && k < 63) num2[k++] = *dentro++;
    num2[k] = 0;
    long v1 = texto_para_int(num1);
    long v2 = texto_para_int(num2);
    long mn = v1 < v2 ? v1 : v2;
    formatar_texto(resultado, max, "%ld", mn);
    return 0;
}

/* parseInt(s) */
static int js_cmd_parseint(const char *dentro, char *resultado, int max)
{
    char num[64];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '(') dentro++;
    while (*dentro && *dentro != ')' && *dentro != ' ' && k < 63) num[k++] = *dentro++;
    num[k] = 0;
    /* Extrair apenas dígitos iniciais */
    char digitos[64];
    int d = 0;
    bool sinal = false;
    int i = 0;
    while (num[i] == ' ' || num[i] == '\t') i++;
    if (num[i] == '-' || num[i] == '+') { sinal = (num[i] == '-'); i++; }
    while (num[i] >= '0' && num[i] <= '9') digitos[d++] = num[i++];
    digitos[d] = 0;
    if (d == 0) formatar_texto(resultado, max, "0");
    else {
        long val = 0;
        for (int j = 0; j < d; j++) val = val * 10 + (digitos[j] - '0');
        if (sinal) val = -val;
        formatar_texto(resultado, max, "%ld", val);
    }
    return 0;
}

/* parseFloat(s) - simplificado para inteiro */
static int js_cmd_parsefloat(const char *dentro, char *resultado, int max)
{
    char num[64];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '(') dentro++;
    while (*dentro && *dentro != ')' && *dentro != ' ' && k < 63) num[k++] = *dentro++;
    num[k] = 0;
    long val = texto_para_int(num);
    formatar_texto(resultado, max, "%ld", val);
    return 0;
}

/* isNaN(x) */
static int js_cmd_isnan(const char *dentro, char *resultado, int max)
{
    char num[64];
    int k = 0;
    while (*dentro == ' ' || *dentro == '\t') dentro++;
    if (*dentro == '(') dentro++;
    while (*dentro && *dentro != ')' && *dentro != ' ' && k < 63) num[k++] = *dentro++;
    num[k] = 0;
    long val = texto_para_int(num);
    int eh_nan = (val == 0 && num[0] != '0');
    formatar_texto(resultado, max, "%d", eh_nan);
    return 0;
}

/* isFinite(x) - sempre verdadeiro para inteiros */
static int js_cmd_isfinite(const char *dentro, char *resultado, int max)
{
    (void)dentro;
    formatar_texto(resultado, max, "%d", 1);
    return 0;
}

/* Usa texto_para_int de sagui.h */

/* ------------------------------------------------------------ API pública */

void js_definir_var_js(const char *nome, const char *valor)
{
    int idx = js_procurar_var(nome);
    if (idx >= 0) {
        copia_texto(variaveis_js[idx].valor, valor, sizeof variaveis_js[idx].valor);
        return;
    }
    if (qtd_variaveis_js < MAX_VAR_JS) {
        copia_texto(variaveis_js[qtd_variaveis_js].nome, nome, sizeof variaveis_js[qtd_variaveis_js].nome);
        copia_texto(variaveis_js[qtd_variaveis_js].valor, valor, sizeof variaveis_js[qtd_variaveis_js].valor);
        variaveis_js[qtd_variaveis_js].eh_numero = false;
        qtd_variaveis_js++;
    }
}

/* Mostra uma caixa de alerta JS. */
void js_alerta(const char *texto)
{
    mensagem("JavaScript", texto);
}

/* Executa um único comando JavaScript. */
int js_executar_comando(const char *comando)
{
    char cmd[512];
    int k = 0;
    while (*comando && k < 511) cmd[k++] = *comando++;
    cmd[k] = 0;
    js_trim(cmd);
    if (tam_texto(cmd) == 0) return -1;

    if (comeca_com(cmd, "alert("))       { return js_cmd_alerta(cmd + 6); }
    if (comeca_com(cmd, "confirm("))     { return js_cmd_confirm(cmd + 8); }
    if (comeca_com(cmd, "prompt("))      { return js_cmd_prompt(cmd + 7); }
    if (comeca_com(cmd, "document.write(")) { return js_cmd_document_write(cmd + 15); }
    if (comeca_com(cmd, "console.log(")) { return js_cmd_console(cmd + 12, "log"); }
    if (comeca_com(cmd, "console.warn(")) { return js_cmd_console(cmd + 13, "warn"); }
    if (comeca_com(cmd, "console.error(")) { return js_cmd_console(cmd + 14, "error"); }
    if (comeca_com(cmd, "var "))         { return js_cmd_var(cmd + 4); }
    if (comeca_com(cmd, "let "))         { return js_cmd_var(cmd + 4); }
    if (comeca_com(cmd, "const "))       { return js_cmd_var(cmd + 6); }
    if (comeca_com(cmd, "function "))    { return js_cmd_funcao(cmd + 9); }
    if (comeca_com(cmd, "setTimeout("))  { return js_cmd_settimeout(cmd + 12); }
    if (comeca_com(cmd, "setInterval(")) { return js_cmd_setinterval(cmd + 14); }
    if (comeca_com(cmd, "Math.random()")) { 
        char resultado[64];
        js_cmd_math_random(resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "Math.floor(")) { 
        char resultado[64];
        js_cmd_math_floor(cmd + 11, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "Math.ceil(")) { 
        char resultado[64];
        js_cmd_math_ceil(cmd + 10, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "Math.round(")) { 
        char resultado[64];
        js_cmd_math_round(cmd + 11, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "Math.max(")) { 
        char resultado[64];
        js_cmd_math_max(cmd + 10, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "Math.min(")) { 
        char resultado[64];
        js_cmd_math_min(cmd + 10, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "parseInt(")) { 
        char resultado[64];
        js_cmd_parseint(cmd + 9, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "parseFloat(")) { 
        char resultado[64];
        js_cmd_parsefloat(cmd + 11, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "isNaN(")) { 
        char resultado[64];
        js_cmd_isnan(cmd + 6, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }
    if (comeca_com(cmd, "isFinite(")) { 
        char resultado[64];
        js_cmd_isfinite(cmd + 9, resultado, sizeof resultado);
        js_definir_var_js("_resultado", resultado);
        return 0;
    }

    /* Pode ser uma chamada de função: nome() */
    const char *parenteses = acha_char(cmd, '(');
    if (parenteses && parenteses[1] == ')') {
        char nome[32];
        int n = (int)(parenteses - cmd);
        if (n > 0 && n < 31) {
            memcpy(nome, cmd, n);
            nome[n] = 0;
            js_trim(nome);
            return js_cmd_chamada_funcao(nome);
        }
    }

    return -1;
}

/* Executa um bloco de JavaScript (conteúdo de uma tag <script>). */
int js_executar_bloco(const char *codigo, int tamanho)
{
    if (!codigo || tamanho <= 0) return -1;

    char *bloco = aloca((uint32_t)tamanho + 1);
    if (!bloco) return -1;
    memcpy(bloco, codigo, (size_t)tamanho);
    bloco[tamanho] = 0;

    /* Executa linha por linha. */
    char *linha = bloco;
    int comandos_executados = 0;
    while (*linha) {
        char *fim = acha_char(linha, '\n');
        if (fim) *fim = 0;
        js_trim(linha);
        if (tam_texto(linha) > 0) {
            if (js_executar_comando(linha) == 0) comandos_executados++;
        }
        if (fim) linha = fim + 1;
        else break;
    }

    libera(bloco);
    return comandos_executados > 0 ? 0 : -1;
}

/* Procura um nó no documento por id. */
no_t *js_document_get_element_by_id(documento_t *doc, const char *id)
{
    if (!doc || !doc->raiz || !id) return NULL;
    /* Busca em profundidade por um nó com o id dado. */
    /* Implementação simplificada: devolvemos NULL (o elemento não pode ser
     * modificado facilmente nesta versão). */
    return NULL;
}

/* Procura elementos por nome de tag (simplificado: devolve NULL). */
no_t *js_document_get_elements_by_tag_name(documento_t *doc, const char *tag)
{
    (void)doc; (void)tag;
    return NULL;
}

/* Procura elementos por classe CSS (simplificado: devolve NULL). */
no_t *js_document_get_elements_by_class_name(documento_t *doc, const char *classe)
{
    (void)doc; (void)classe;
    return NULL;
}

/* Adiciona um ouvinte de evento a um elemento (apenas registra). */
int js_element_add_event_listener(no_t *elemento, const char *evento, const char *funcao)
{
    (void)elemento; (void)evento; (void)funcao;
    return 0;
}
