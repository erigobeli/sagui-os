# O C do Sagui

Manual da linguagem aceita pelo compilador do SaguiOS (comando `cc`, fase 7
da versão 2.0) e da biblioteca `libsagui`. Serve para pessoas e é também o
texto que o Sagui Code envia a uma IA quando pede um programa: tudo o que
está aqui funciona; o que não está, considere que não existe.

> **Resumo para quem tem pressa (ou é uma IA):**
>
> 1. O programa começa em `int principal(int argc, char **argv)`, não em `main`.
> 2. **Não existe a biblioteca padrão do C** (`stdio.h`, `stdlib.h`, `string.h`...).
>    Use `#include "sagui.h"`: `imprimir` no lugar de `printf`, `aloca`/`libera`
>    no lugar de `malloc`/`free`, `tam_texto` no lugar de `strlen` (tabela na seção 9).
> 3. **Não há ponto flutuante** (`float`, `double`, `1.5`). Use inteiros.
> 4. Nomes de variáveis e funções sem acento; textos e comentários podem ter.
> 5. O compilador para no **primeiro erro**, com linha, coluna e uma dica.
>    Ele não dá avisos: o que compila pode ainda estar errado.
> 6. Compilar: `cc programa.c` (gera `programa`) ou `cc programa.c -o /tmp/teste`.

---

## 1. Um programa completo

```c
/* conta.c - soma os números passados na linha de comando */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int soma = 0;
    for (int i = 1; i < argc; i++) soma += texto_para_int(argv[i]);
    imprimir("soma: %d\n", soma);
    return 0;               /* o código de saída do programa */
}
```

```
sagui:/$ cc /usb/conta.c -o /tmp/conta
sagui:/$ /tmp/conta 10 20 12
soma: 42
```

`argv[0]` é o nome do programa; `argv[argc]` é `NULL`. O valor devolvido por
`principal` é o código de saída (0 = deu certo). `sair(codigo)` encerra de
qualquer lugar.

## 2. O comando `cc`

| Comando | O que faz |
| --- | --- |
| `cc prog.c` | compila e grava `prog` (o nome sem `.c`) na mesma pasta |
| `cc prog.c -o saida` | escolhe o nome do executável |
| `cc a.c b.c -o prog` | programa em vários arquivos (até 16) |
| `cc prog.c --conferir` | só procura erros; não grava nada |
| `cc prog.c -I pasta` | mais uma pasta onde procurar os `#include` |
| `cc prog.c --ri` | mostra a representação intermediária (para curiosos) |

O `cc` termina com código 0 se compilou e 1 se achou um erro. Para rodar o
resultado, escreva o caminho com uma barra (`/tmp/prog`, `./prog`) ou
copie-o para `/usb/programas/`, de onde o Terminal o acha só pelo nome.

Uma mensagem de erro tem sempre a mesma forma:

```
conta.c:7:36: erro: a função 'atoi' não foi declarada
    7 |     for (int i = 1; i < argc; i++) soma += atoi(argv[i]);
      |                                            ^
dica: o SaguiOS não tem a biblioteca padrão do C; no lugar de 'atoi' use texto_para_int (com #include "sagui.h")
```

`arquivo:linha:coluna`, o que está errado, a linha com um `^` no lugar e uma
dica do que fazer. Conserte e compile de novo: o próximo erro, se houver,
aparece então.

## 3. Tipos

| Tipo | Bytes | Valores |
| --- | --- | --- |
| `char`, `signed char` | 1 | -128 a 127 (`char` tem sinal) |
| `unsigned char` | 1 | 0 a 255 |
| `short` | 2 | -32768 a 32767 |
| `unsigned short` | 2 | 0 a 65535 |
| `int`, `long` | 4 | -2147483648 a 2147483647 |
| `unsigned`, `unsigned long` | 4 | 0 a 4294967295 |
| `long long` | 8 | ±9,2 × 10¹⁸ |
| `unsigned long long` | 8 | 0 a 1,8 × 10¹⁹ |
| `_Bool` (ou `bool`, de `sagui.h`) | 1 | 0 ou 1 |
| ponteiro (`tipo *`) | 4 | um endereço |
| `enum` | 4 | um `int` com nomes |

`sagui.h` dá os apelidos de tamanho fixo `int8_t`, `uint8_t`, `int16_t`,
`uint16_t`, `int32_t`, `uint32_t`, `int64_t`, `uint64_t`, mais `size_t`
(`unsigned int`), `bool`, `true`, `false` e `NULL`.

As regras são as do C: contas com tipos menores que `int` são feitas em
`int`; misturar `int` com `unsigned` faz a conta sem sinal; o que não cabe
no destino é cortado; estouro dá a volta (como no processador).

```c
unsigned char c = 250;
c += 10;                    /* 4: deu a volta em 256 */
int n = -1;
if (n < 1u) { }             /* FALSO: -1 vira 4294967295 na comparação sem sinal */
long long grande = 3000000000LL * 4;    /* use LL para a conta ser de 64 bits */
```

Números: decimais (`42`), hexadecimais (`0x2A`), octais (`052`), binários
(`0b101010`), com os sufixos `u`, `l`, `ll`, `ull`. Letras: `'a'`, `'\n'`,
`'\t'`, `'\r'`, `'\0'`, `'\\'`, `'\''`, `'\x41'`.

`long long`: somar, subtrair, comparar e mexer em bits é direto; multiplicar,
dividir e deslocar chama rotinas da libsagui (funciona, mas é mais lento).
`imprimir` não tem formato para 64 bits: imprima as metades ou converta os
dígitos em um laço (`n % 10`, `n / 10`).

### Ponteiros e vetores

```c
int v[5] = { 1, 2, 3 };         /* o resto fica zero */
int *p = v;                     /* o nome do vetor vale o endereço do 1º elemento */
p[1] = 20;  *(p + 2) = 30;      /* a mesma coisa de dois jeitos */
int m[3][4];                    /* 3 linhas de 4 colunas: m[linha][coluna] */
int n = sizeof v / sizeof v[0]; /* 5: quantos elementos */
char *nomes[] = { "um", "dois" };
void *qualquer = p;             /* void * aceita qualquer ponteiro, e volta sem conversão */
int (*funcao)(int, int);        /* ponteiro para função */
```

Somar 1 a um ponteiro anda um elemento (4 bytes num `int *`). Subtrair dois
ponteiros dá a distância em elementos. Um vetor passado a uma função chega
como ponteiro (o tamanho precisa ir em outro parâmetro). O tamanho de um
vetor é sempre uma constante: **não há vetores de tamanho variável**; para
um tamanho decidido na hora, use `aloca`.

O compilador é rigoroso com ponteiros: guardar um inteiro num ponteiro (ou
o contrário), ou um `int *` num `char *`, é **erro**, não aviso. Quando a
conversão for de propósito, escreva-a: `(char *)p`, `(unsigned)p`. As únicas
conversões automáticas são de e para `void *`, a constante `0`/`NULL`, e
entre ponteiros para inteiros do mesmo tamanho (`char *` e `unsigned char *`).

### struct, union, enum, typedef

```c
typedef struct {
    char nome[16];
    int  idade;
} pessoa_t;

struct no { int valor; struct no *prox; };      /* pode apontar para si mesmo */

union palavra { unsigned inteiro; unsigned char bytes[4]; };   /* campos na mesma memória */

enum cor { VERMELHO, VERDE, AZUL = 10, ROXO };  /* 0, 1, 10, 11 */

pessoa_t ana = { "Ana", 31 };
pessoa_t copia = ana;           /* copia o struct inteiro */
pessoa_t *p = &ana;
p->idade++;                     /* -> com ponteiro, ponto com o próprio struct */
```

Structs podem ser atribuídos, passados e devolvidos por valor (são copiados
inteiros; para structs grandes prefira ponteiro). Não dá para comparar
structs com `==`: compare os campos. O arranjo na memória é o do GCC para
x86 de 32 bits (cada campo alinhado ao seu tamanho, no máximo 4), então os
structs da libsagui (`evento_t`, `data_t`...) têm o formato certo.
Structs e unions sem nome dentro de outro struct são aceitos (os campos de
dentro valem direto). **Campos de bits (`int x : 3`) não são aceitos.**

`const` é conferido: atribuir a algo `const` é erro. `static` (em função: a
variável guarda o valor entre as chamadas; fora: o nome só vale no arquivo)
e `extern` funcionam. `volatile`, `register`, `restrict`, `inline`,
`_Noreturn` e `__attribute__((...))` são aceitos e ignorados.

### Inicializadores

```c
int v[] = { 5, 4, 3 };                          /* o tamanho sai da lista: 3 */
char texto[10] = "oi";                          /* o resto fica zero */
pessoa_t p = { .idade = 20, .nome = "Edu" };    /* pelo nome do campo */
int dias[7] = { [0] = 1, [6] = 1 };             /* pelo índice */
int m[2][2] = { { 1, 2 }, { 3, 4 } };
ret_t r = { .canto.x = 3, .lados = { 4, 5 } };  /* campos de dentro */
lista[n++] = (ponto_t){ 3, 4 };                 /* (tipo){...}: valor montado na hora */
```

Variáveis globais e `static` só aceitam valores que o compilador calcula:
números, textos, e endereços de outras globais ou de funções. Uma global sem
inicializador começa zerada; uma local sem inicializador começa com lixo.
`(tipo){...}` só vale dentro de funções.

## 4. Operadores

Todos os do C, com a precedência de sempre (na dúvida, parênteses):

| Grupo | Operadores |
| --- | --- |
| contas | `+ - * / %` (divisão inteira arredonda para o zero: `-7 / 2` é `-3`, `-7 % 2` é `-1`) |
| bits | `& \| ^ ~ << >>` (`>>` mantém o sinal em tipos com sinal) |
| comparação | `== != < <= > >=` (dão 1 ou 0) |
| lógicos | `&& \|\| !` (`&&` e `\|\|` param no meio quando o resultado já é certo) |
| atribuição | `= += -= *= /= %= &= \|= ^= <<= >>=`, `++ --` (antes ou depois) |
| outros | `?:`, vírgula, `sizeof`, `_Alignof`, `(tipo)valor`, `&x`, `*p`, `v[i]`, `s.campo`, `p->campo`, `f(x)` |

Dividir por zero encerra o programa (o kernel avisa). Os argumentos de uma
chamada são calculados da esquerda para a direita, mas não conte com isso:
não escreva `f(i++, i++)`.

## 5. Comandos

`if`/`else`, `while`, `do ... while`, `for` (com declaração dentro:
`for (int i = 0; ...)`), `switch`/`case`/`default`, `break`, `continue`,
`return`, `goto` e rótulos, blocos `{ }` com variáveis próprias. Declarações
podem aparecer em qualquer ponto de um bloco.

```c
switch (tecla) {            /* o valor é tratado como int */
case 'a':
case 'A':                   /* dois casos para o mesmo código */
    esquerda();
    break;                  /* sem break, a execução "cai" no caso de baixo */
case 'q':
    return 0;
default:
    bip(440, 50);
}
```

Cada `case` é uma constante inteira (número, letra, `enum`, `#define`). Uma
função não-`void` que chega ao fim sem `return` devolve 0.

## 6. Funções

```c
static int maior(int a, int b) { return a > b ? a : b; }   /* static: só deste arquivo */
int soma_vetor(const int *v, int n);                        /* protótipo: o corpo vem depois */
void ordena(int *v, int n, int (*compara)(int, int));       /* recebe uma função */
```

Uma função precisa ser declarada **antes** de ser usada (o corpo, ou só o
protótipo). `tipo f(void)` não recebe nada. Recursão funciona, mas a pilha
de um programa tem **64 KB**: não declare vetores grandes como variáveis
locais (use `static` ou `aloca`) nem faça recursão muito funda.

Funções com número variável de argumentos:

```c
static int soma(int quantos, ...)
{
    va_list ap;
    int s = 0;
    va_start(ap, quantos);                  /* o último parâmetro com nome */
    for (int i = 0; i < quantos; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}
```

Em `va_arg` use `int`, `unsigned`, ponteiros, `long long` ou structs (tipos
menores que `int` viajam como `int`). Para repassar a lista a `imprimir`,
use `formatar_lista(destino, tamanho, formato, ap)`.

## 7. Textos

Um texto é um vetor de `char` terminado por um byte zero. `"abc"` ocupa 4
bytes. Textos vizinhos se juntam (`"um " "dois"`). Sequências: `\n` `\t`
`\r` `\0` `\\` `\"` `\'` `\xHH` e octais (`\101`).

Os arquivos `.c` podem estar em UTF-8 (como chegam de fora) ou em Latin-1
(como o Editor do SaguiOS grava). O SaguiOS mostra as letras em **Latin-1**:
o compilador converte os textos entre aspas (`"ação"` ocupa 4 letras + o
zero). Letras que não existem no Latin-1 (emojis, `→`, `—`) viram `?`.
Textos lidos de fora (arquivos do pendrive, rede) podem chegar em UTF-8;
`utf8_para_latin1` converte.

Não existe atribuição de texto com `=` (isso copiaria só o ponteiro) nem
comparação com `==`: use `copia_texto` e `compara_texto`.

## 8. Pré-processador

```c
#include "sagui.h"              /* procura na pasta do arquivo e em /sistema/inc */
#include <gui.h>                /* só em /sistema/inc */
#define MAX 100
#define QUADRADO(x) ((x) * (x)) /* com parâmetros: use parênteses em volta de tudo */
#define REGISTRO(fmt, ...) imprimir("[" fmt "]\n", __VA_ARGS__)
#define TEXTO(x) #x             /* # transforma o argumento em texto */
#define JUNTA(a, b) a ## b      /* ## cola dois pedaços num nome só */
#undef MAX
#if VERSAO >= 2 && defined(COM_SOM)
#elif !defined(SIMPLES)
#else
#endif
#ifdef DEPURAR
#endif
#ifndef MEU_CABECALHO_H         /* a proteção de sempre para cabeçalhos */
#define MEU_CABECALHO_H
#endif
#error mensagem                 /* para a compilação de propósito */
#pragma once                    /* aceito; os outros #pragma são ignorados */
```

Uma linha terminada em `\` continua na de baixo. Nomes prontos: `__FILE__`,
`__LINE__` e `__SAGUI__` (vale 1: serve para um programa saber que está
sendo compilado pelo `cc` do SaguiOS). Um cabeçalho seu fica na mesma pasta
do `.c` que o inclui.

## 9. A libsagui

Tudo o que um programa usa do sistema vem de três cabeçalhos:

| Cabeçalho | O que traz |
| --- | --- |
| `sagui.h` | tipos, arquivos, processos, memória, texto, `imprimir`, tempo, som, rede, janelas (as chamadas cruas) |
| `gui.h` | o kit gráfico: janelas, desenho, botões, caixas de texto, listas, diálogos (inclui `sagui.h`) |
| `http.h` | baixar páginas por HTTP e HTTPS (inclui `sagui.h`) |

Quem vem do C tradicional:

| No C tradicional | No SaguiOS |
| --- | --- |
| `main` | `principal` |
| `printf(fmt, ...)` | `imprimir(fmt, ...)` |
| `fprintf(stderr, ...)` | `imprimir_em(ERROS, ...)` |
| `sprintf` / `snprintf` | `formatar_texto(destino, tamanho, fmt, ...)` |
| `puts`, `putchar` | `imprimir("%s\n", s)`, `imprimir("%c", c)` |
| `malloc`, `calloc`, `realloc`, `free` | `aloca`, `aloca_zerado`, `realoca`, `libera` |
| `strlen` | `tam_texto` |
| `strcmp`, `strncmp` | `compara_texto`, `compara_texto_n` |
| `strcpy`, `strncpy` | `copia_texto(destino, origem, tamanho_do_destino)` |
| `strcat` | `junta_texto(destino, origem, tamanho_do_destino)` |
| `strchr`, `strrchr` | `acha_char`, `acha_ultimo_char` |
| `atoi` | `texto_para_int` |
| `isspace` | `eh_espaco` |
| `memcpy`, `memmove`, `memset`, `memcmp` | iguais |
| `open`, `read`, `write`, `close`, `lseek` | `abrir`, `ler`, `escrever`, `fechar`, `posicionar` |
| `fgets(stdin)` | `ler_linha(ENTRADA, buf, tamanho)` |
| `exit` | `sair` |
| `sleep` | `dormir(milissegundos)` |
| `rand` | `bytes_aleatorios(&n, sizeof n)` |
| `time` | `tempo_ms()` (desde que ligou) ou `ler_data(&d)` |

Quase todas as funções devolvem **negativo em caso de erro** (`ERRO_NAO_EXISTE`,
`ERRO_SEM_MEMORIA`...); `nome_erro(codigo)` dá o texto do erro.

### 9.1 Saída e entrada

```c
int  imprimir(const char *fmt, ...);                 /* na saída do programa */
int  imprimir_em(int fd, const char *fmt, ...);      /* ENTRADA 0, SAIDA 1, ERROS 2, ou um arquivo */
int  formatar_texto(char *dst, size_t max, const char *fmt, ...);   /* sempre termina em zero */
int  formatar_lista(char *dst, size_t max, const char *fmt, va_list ap);
int  escrever_texto(int fd, const char *s);
int  escrever_tudo(int fd, const void *buf, unsigned n);
int  ler_linha(int fd, char *buf, unsigned max);     /* tamanho da linha, ou -1 no fim */
void registrar(const char *fmt, ...);                /* log do sistema: não aparece na tela */
const char *nome_erro(int erro);
```

Formatos: `%d` (int), `%u` (unsigned), `%x` (hexadecimal), `%c` (letra),
`%s` (texto), `%p` (ponteiro), `%%`. Entre o `%` e a letra: largura
(`%5d`), `-` para alinhar à esquerda (`%-10s`), `0` para preencher com
zeros (`%02d`) e `*` para a largura vir nos argumentos. **Não há** `%f`,
`%ld`, `%lld`, `%5.2f` nem precisão (`%.3s`).

```c
char linha[80];
imprimir("Seu nome: ");
if (ler_linha(ENTRADA, linha, sizeof linha) >= 0) imprimir("Olá, %s!\n", linha);
```

### 9.2 Texto e memória

```c
size_t tam_texto(const char *s);
int    compara_texto(const char *a, const char *b);            /* 0 = iguais; <0 e >0 como strcmp */
int    compara_texto_n(const char *a, const char *b, size_t n);
char  *copia_texto(char *dst, const char *src, size_t max);    /* max = tamanho do destino; sempre termina em zero */
char  *junta_texto(char *dst, const char *src, size_t max);
char  *acha_char(const char *s, int c);                        /* NULL se não tem */
char  *acha_ultimo_char(const char *s, int c);
bool   comeca_com(const char *s, const char *prefixo);
int    texto_para_int(const char *s);
bool   eh_espaco(int c);
size_t utf8_para_latin1(char *s, size_t n, size_t *sobra);
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int v, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

void *aloca(size_t n);              /* NULL se não há memória */
void *aloca_zerado(size_t n);
void *realoca(void *p, size_t n);
void  libera(void *p);
```

```c
int *v = aloca(n * sizeof(int));
if (!v) { imprimir_em(ERROS, "sem memória\n"); return 1; }
...
libera(v);
```

### 9.3 Arquivos e pastas

```c
int abrir(const char *caminho, int modo);       /* devolve o descritor (fd) */
int ler(int fd, void *buf, unsigned n);         /* bytes lidos; 0 = fim */
int escrever(int fd, const void *buf, unsigned n);
int fechar(int fd);
int posicionar(int fd, int desloc, int origem); /* POS_INICIO, POS_ATUAL, POS_FIM; devolve a posição */
int info_arquivo(const char *caminho, info_arquivo_t *info);   /* .tipo e .tamanho */
int apagar(const char *caminho);
int renomear(const char *origem, const char *destino);
int criar_pasta(const char *caminho);
int remover_pasta(const char *caminho);
int listar(const char *caminho, int indice, entrada_pasta_t *e);   /* 1 = achou; .nome .tipo .tamanho */
int mudar_pasta(const char *caminho);
int pasta_atual(char *buf, unsigned n);
```

Modos de `abrir` (juntam-se com `|`): `ABRIR_LER`, `ABRIR_ESCREVER`,
`ABRIR_CRIAR`, `ABRIR_TRUNCAR`, `ABRIR_ACRESCENTAR`. Tipos: `TIPO_ARQUIVO`,
`TIPO_PASTA`, `TIPO_PROGRAMA`. A pasta `/tmp` some ao desligar; o que deve
ficar vai para o pendrive, em `/usb` (se estiver montado).

```c
int fd = abrir("/usb/notas.txt", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
if (fd < 0) { imprimir_em(ERROS, "não abriu: %s\n", nome_erro(fd)); return 1; }
imprimir_em(fd, "linha %d\n", 1);
fechar(fd);

char buf[512];
int n;
fd = abrir("/usb/notas.txt", ABRIR_LER);
while ((n = ler(fd, buf, sizeof buf)) > 0) escrever(SAIDA, buf, n);
fechar(fd);

entrada_pasta_t e;
for (int i = 0; listar("/usb", i, &e) == 1; i++) imprimir("%s (%u bytes)\n", e.nome, e.tamanho);
```

### 9.4 Processos, tempo e diversos

```c
int  executar(const char *caminho, char *const argv[]);   /* argv termina em NULL; devolve o pid */
int  esperar(int pid, int *codigo);
int  recolher(int *codigo);            /* um filho que já terminou, sem esperar; 0 se nenhum */
void sair(int codigo);
int  meu_pid(void);
int  matar(int pid);
int  criar_pipe(int fds[2]);
int  duplicar(int antigo, int novo);
int  dormir(unsigned ms);
unsigned tempo_ms(void);               /* milissegundos desde que o sistema ligou */
int  ler_data(data_t *d);              /* .ano .mes .dia .hora .minuto .segundo .dia_semana */
int  bytes_aleatorios(void *destino, unsigned n);
int  config_ler(const char *chave, char *valor, int max);
int  config_gravar(const char *chave, const char *valor);
int  modo_terminal(int modo);          /* TERM_LINHA ou TERM_BRUTO (tecla a tecla) */
int  limpar_tela(void);
```

Som: `bip(frequencia, ms)`, `tocar(notas, n, modo)`, `silenciar()`, e a
placa de som (`som_abrir`, `som_enviar`, `som_fechar`, `som_volume`).
Rede: `dns_resolver`, `tcp_conectar`, `tcp_enviar_tudo`, `rede_receber`,
`rede_fechar`, `rede_info` (veja `sagui.h`); para páginas da web use `http.h`.

### 9.5 Janelas (`gui.h`)

Um programa com janela cria a janela, desenha, mostra e fica num laço
esperando eventos:

```c
#include "gui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    jan_t *j = jan_abrir("Contador", 240, 120);
    if (!j) return 1;
    botao_t mais = { 20, 60, 90, 30, "Somar", false };
    int n = 0;
    char texto[32];
    for (;;) {
        g_retangulo(j, 0, 0, j->larg, j->alt, COR_FUNDO);
        formatar_texto(texto, sizeof texto, "Cliques: %d", n);
        g_texto(j, 20, 20, texto, COR_TEXTO);
        botao_desenhar(j, &mais);
        jan_mostrar(j);

        evento_t e;
        if (proximo_evento(j->id, &e, true) != 1) continue;     /* espera o próximo */
        if (e.tipo == EV_FECHAR) break;
        if (botao_tratar(j, &mais, &e)) n++;
        if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) break;
    }
    jan_fechar(j);
    return 0;
}
```

```c
typedef struct { int id; uint32_t *px; int larg, alt; } jan_t;   /* px: os pixels, 0xRRGGBB */
jan_t *jan_abrir(const char *titulo, int largura, int altura);  /* até 1000 x 700 */
void   jan_fechar(jan_t *j);
void   jan_mostrar(jan_t *j);                                    /* manda o desenho para a tela */
void   jan_mostrar_area(jan_t *j, int x, int y, int l, int a);
void   g_retangulo(jan_t *j, int x, int y, int l, int a, uint32_t cor);   /* cheio */
void   g_moldura(jan_t *j, int x, int y, int l, int a, uint32_t cor);     /* só a borda */
void   g_linha(jan_t *j, int x0, int y0, int x1, int y1, uint32_t cor);
void   g_texto(jan_t *j, int x, int y, const char *t, uint32_t cor);      /* letras de 8 x 16 */
void   g_texto_n(jan_t *j, int x, int y, const char *t, int n, uint32_t cor);
int    g_largura_texto(const char *t);
void   g_relevo(jan_t *j, int x, int y, int l, int a, bool afundado);
void   g_desenho(jan_t *j, int x, int y, const char *const *linhas, int l, int a,
                 uint32_t (*paleta)(char), int escala);   /* desenho em texto; paleta devolve 0xFF000000 = transparente */
```

Para desenhar ponto a ponto, escreva direto: `j->px[y * j->larg + x] = cor;`
(confira antes se `x` e `y` estão dentro da janela). Nada aparece até o
`jan_mostrar`.

Eventos (`evento_t`): `.tipo` é `EV_CLIQUE`, `EV_SOLTOU`, `EV_MOVER`,
`EV_TECLA` ou `EV_FECHAR`; `.x` e `.y` são a posição do mouse dentro da
janela; `.botoes` (1 esquerdo, 2 direito); `.tecla` é a letra (Latin-1) ou
`TECLA_CIMA`, `TECLA_BAIXO`, `TECLA_ESQUERDA`, `TECLA_DIREITA`, `TECLA_HOME`,
`TECLA_END`, `TECLA_PGUP`, `TECLA_PGDN`, `TECLA_DELETE`, `TECLA_ESC`; Enter é
`'\n'`, apagar é `'\b'`. `proximo_evento(id, &e, false)` não espera (devolve
0 se não há evento): é o que os jogos usam, com `dormir(16)` no laço.
`teclas_seguradas(mapa)` e `SEGURADA(mapa, CV_ESQUERDA)` dizem que teclas
estão apertadas agora.

Componentes prontos (cada um é um struct, uma função que desenha e uma que
trata o evento):

```c
typedef struct { int x, y, l, a; const char *texto; bool apertado; } botao_t;
void botao_desenhar(jan_t *j, botao_t *b);
bool botao_tratar(jan_t *j, botao_t *b, const evento_t *e);      /* true = clicado */

typedef struct { int x, y, l; char texto[128]; int cursor; bool foco; } caixa_t;
void caixa_desenhar(jan_t *j, caixa_t *c);
bool caixa_tratar(jan_t *j, caixa_t *c, const evento_t *e);      /* true = Enter */
void caixa_definir(caixa_t *c, const char *texto);

void rotulo(jan_t *j, int x, int y, const char *texto, uint32_t cor);
bool confirmar(const char *titulo, const char *pergunta);        /* Sim / Não */
void mensagem(const char *titulo, const char *texto);            /* OK */
bool pedir_texto(const char *titulo, const char *pergunta, char *resposta, int max);
```

Há ainda `area_t` (texto de várias linhas com rolagem) e `lista_t` (lista
com seleção); veja `gui.h`. Cores do tema: `COR_FUNDO`, `COR_TEXTO`,
`COR_TEXTO_FRACO`, `COR_BORDA`, `COR_BOTAO`, `COR_DESTAQUE`, `COR_SELECAO`,
`COR_BRANCO`.

### 9.6 Web (`http.h`)

```c
#include "http.h"

static int recebe(void *ctx, const char *dados, unsigned n)
{
    (void)ctx;
    escrever(SAIDA, dados, n);      /* cada pedaço que chega */
    return 0;                       /* negativo interrompe */
}

int principal(int argc, char **argv)
{
    http_resposta_t r;
    if (argc < 2) return 1;
    if (http_obter(argv[1], recebe, NULL, &r) < 0) { imprimir_em(ERROS, "falhou: %s\n", r.erro); return 1; }
    imprimir("\nsituação %d, %u bytes, tipo %s\n", r.situacao, r.recebidos, r.tipo);
    return 0;
}
```

## 10. O que fica de fora

O compilador recusa (com mensagem) ou não conhece:

- **Ponto flutuante**: `float`, `double`, `long double`, números como `1.5`
  ou `1e3`, e a `math.h`. Use inteiros com escala (centavos, milésimos).
- **A biblioteca padrão do C**: nenhum `#include <stdio.h>` e semelhantes.
- **Vetores de tamanho variável** (`int v[n]` com `n` variável) e `alloca`.
- **Campos de bits** (`unsigned x : 3`).
- **Assembly** dentro do C (`asm`, `__asm__`).
- `_Generic`, `_Atomic`, `_Alignas`, `_Static_assert`, `_Thread_local`,
  números complexos.
- Extensões do GCC: `typeof`, expressões-bloco `({ ... })`, `case 1 ... 5`,
  funções dentro de funções, `&&rotulo`, `__builtin_*` (fora os do
  `va_list`). `__attribute__((...))` é aceito e ignorado.
- Definições de função no estilo antigo (K&R) e chamar função sem declarar.
- Letras e textos "largos" (`L'a'`, `L"..."`, `u8"..."`), trígrafos e dígrafos.
- `switch` sobre `long long` (o valor é cortado para `int`).
- `(tipo){...}` fora de função; inicializador de global que precise de conta
  em tempo de execução; diferença entre dois endereços como constante.
- `#include_next`, `#line`, `#ident`, `__DATE__`, `__TIME__`, `__func__`.
- Avisos: o compilador só dá erros. Variável não usada, `=` no lugar de
  `==` dentro de um `if`, formato do `imprimir` que não combina com os
  argumentos: nada disso é apontado.

Limites: 40 argumentos por chamada, 32 parâmetros por função, 16 arquivos
`.c` por programa, 24 `#include` aninhados, 64 blocos `{ }` aninhados, e
linhas de até uns 2000 caracteres nas mensagens.

Diferenças de comportamento para quem conhece o GCC: conversões entre
ponteiros de tipos diferentes e entre ponteiro e inteiro são **erro** sem o
`(tipo)`; o código gerado é simples e sem otimização (uns 3 a 5 vezes mais
lento que o do GCC com `-O2`, o que não se nota nos programas comuns); um
nome global do programa igual a um da libsagui (`mensagem`, `rotulo`) é
aceito se o programa não incluir o cabeçalho que o declara, mas é melhor
evitar.

## 11. Erros comuns (e a mensagem que aparece)

| O que foi escrito | Mensagem | Conserto |
| --- | --- | --- |
| `int main(void)` | o programa não tem a função 'principal' | `int principal(int argc, char **argv)` |
| `#include <stdio.h>` | não achei o arquivo 'stdio.h' do #include | `#include "sagui.h"` |
| `printf("oi")` | a função 'printf' não foi declarada | `imprimir("oi")` |
| `float x = 1.5;` | o tipo 'float' não é aceito | inteiros com escala |
| `int número;` | letra com acento (ou símbolo) dentro de um nome | `int numero;` |
| `char *p = 10;` | esperava o ponteiro 'char *', mas veio o inteiro 'int' | `(char *)10`, `&variavel` ou `NULL` |
| `nome = "ana";` (vetor) | não dá para atribuir a um vetor inteiro | `copia_texto(nome, "ana", sizeof nome)` |
| `p.x` com `p` ponteiro | 'ponto_t *' não tem campos | `p->x` |
| `int v[n];` | o tamanho de um vetor precisa ser uma constante inteira | `#define` ou `aloca` |
| falta `;` | faltou ';' antes de ... | o `^` mostra onde |
