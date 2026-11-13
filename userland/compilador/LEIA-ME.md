# userland/compilador — o compilador C do SaguiOS

O comando `cc` (fase 7 da versão 2.0, histórias H64 a H66): lê um programa
em C e grava um executável do SaguiOS, sem sair do sistema. Foi escrito
neste projeto, de cabeça, a partir de documentos públicos, **sem consultar
o código de nenhum compilador, montador ou ligador** (GCC, Clang, TCC,
chibicc, lcc, NASM, binutils...) nem de tutoriais de compiladores:

| Assunto | Documento |
|---|---|
| A linguagem (gramática, conversões, pré-processador) | rascunho público do padrão C11 (N1570) |
| Codificação das instruções (ModRM, SIB, códigos) | manual da Intel, volume 2 |
| Objetos e executáveis | especificação ELF (System V ABI) e o suplemento do i386 |
| Convenção de chamada, tamanhos e alinhamentos | System V ABI i386 |
| O que o SaguiOS aceita carregar | `kernel/nucleo/carregador.c` e `userland/programa.ld` |

A linguagem aceita está em [`docs/c-do-sagui.md`](../../docs/c-do-sagui.md).

**Aviso:** é um compilador pequeno e sem otimização. O código que ele gera é
correto (os testes comparam com o GCC), mas umas 3 a 5 vezes mais lento.

## O caminho de um programa

```
 arquivo.c
    │  lexico.c       texto -> tokens (com arquivo, linha e coluna)
    │  preproc.c      #include, #define, #if: uma lista só de tokens
    │  declaracao.c   declarações, funções e comandos   ┐
    │  expressao.c    expressões: árvore com os tipos   ├─ a FRENTE (entende C)
    │  tipos.c        os tipos e as regras deles        │
    │  gerar.c        árvore -> representação intermediária ┘
    ▼
 representação intermediária (ri.h): funções = listas de instruções simples
    │  x86.c          escolhe as instruções do processador  ┐
    │  montador.c     instruções -> bytes                   ├─ o FUNDO (entende x86 e ELF)
    │  ligador.c      junta com a libsagui e grava o ELF    ┘
    ▼
 executável do SaguiOS
```

`base.c` tem o chão comum (memória, nomes, mensagens de erro) e a função
`cc_compilar`, que comanda as etapas. `cc.c` é o comando do SaguiOS: só o
"ambiente" (arquivos e memória) em volta do núcleo. O núcleo não depende do
sistema: o mesmo código, com outro ambiente (`tests/compilador/cc_linux.c`),
roda no computador de desenvolvimento, e os testes conferem que os dois
produzem executáveis **byte a byte iguais**.

| Arquivo | Linhas (aprox.) | O que tem |
|---|---|---|
| `compilador.h` | 55 | a interface: `CcAmbiente`, `CcOpcoes`, `cc_compilar` |
| `ri.h` | 120 | a representação intermediária: a fronteira entre as duas metades |
| `interno.h` | 360 | o que as partes dividem |
| `base.c` | 370 | reserva de memória, tabela de nomes, formatação, erros, `cc_compilar` |
| `lexico.c` | 340 | tokens; textos convertidos de UTF-8 para Latin-1 |
| `preproc.c` | 720 | diretivas e macros |
| `tipos.c` | 230 | tipos, tamanhos, comparação, nome por extenso |
| `expressao.c` | 890 | expressões, conferência de tipos, contas com constantes, dicas |
| `declaracao.c` | 1240 | declarações, structs, inicializadores, comandos, funções |
| `gerar.c` | 540 | árvore -> RI, constantes de endereço, listagem (`--ri`) |
| `x86.c` | 540 | RI -> instruções; quadro da pilha; chamadas |
| `montador.c`, `montador.h` | 230 | as instruções em bytes |
| `ligador.c` | 280 | leitura de objeto ELF, coleta de seções, endereços, o executável |
| `cc.c` | 115 | o comando `cc` |

Ao todo, umas 6000 linhas de C.

## Como cada parte foi pensada

### Memória e erros (`base.c`)

O compilador pede pedaços de 64 KB ao ambiente e corta fatias deles; nunca
devolve uma fatia sozinha, só tudo no fim. Um compilador vive segundos e
quase tudo o que cria vale até o final; controlar cada fatia só daria
lugar para erro. Listas que crescem mudam para um lugar com o dobro do
tamanho.

A compilação **para no primeiro erro**. A decisão é de propósito: quem lê a
mensagem (uma pessoa aprendendo, ou a IA do Sagui Code) conserta uma coisa
de cada vez, e depois do primeiro erro um compilador costuma despejar erros
falsos, consequência do primeiro. Parar é um salto direto de volta ao
`cc_compilar` (`__builtin_setjmp`/`__builtin_longjmp`, que o GCC dá sem
biblioteca), que então devolve toda a memória.

Toda mensagem tem `arquivo:linha:coluna`, a linha do código, um `^` e uma
**dica** de conserto. As colunas contam letras, não bytes (um `ã` em UTF-8
são dois bytes e uma coluna); onde a linha tem tabulação, o espaço antes do
`^` também tem. As dicas vêm de três lugares: o texto fixo de cada erro; uma
tabela "nome do C tradicional -> nome da libsagui" (`printf` -> `imprimir`),
porque é o erro mais provável de uma IA treinada em C comum; e, para um nome
desconhecido, o nome declarado mais parecido (distância de edição até 2).

### Tokens e pré-processador

O leitor de tokens guarda em cada token de onde ele veio. Textos e letras
entre aspas já saem convertidos para Latin-1, a codificação do SaguiOS (o
build do sistema faz o mesmo com `-fexec-charset`).

O pré-processador trabalha com tokens, não com texto. Uma macro é uma lista
de tokens; expandir é empilhar essa lista como "fonte" de leitura, acima do
arquivo. Enquanto a lista de uma macro está na pilha, o nome dela não é
expandido de novo, e o token que tentou fica marcado para sempre: é a regra
do padrão que impede `#define x x` de girar sem fim. Os argumentos de uma
macro com parâmetros são expandidos sozinhos antes de entrar no corpo
(empilhando o argumento com uma marca de fim), menos quando vizinhos de `#`
ou `##`. O `##` escreve os dois pedaços num rascunho e lê de novo como um
token.

O resultado é o programa inteiro numa lista de tokens na memória. Isso
custa alguns bytes por token, mas dá ao analisador o que ele mais precisa:
olhar adiante e voltar.

### O analisador (`declaracao.c`, `expressao.c`)

"De cima para baixo": uma função por construção da linguagem. Duas partes
fogem do desenho mais óbvio:

- **Operadores de dois lados** são lidos por um laço só, guiado por uma
  tabela de precedências, em vez de uma função por nível. Dez níveis de
  funções a cada parêntese gastariam a pilha, que no SaguiOS tem 64 KB (o
  `cc` ainda troca para uma pilha de 1 MB pedida com `aloca`, e há um limite
  de aninhamento com mensagem própria).
- **Declaradores** (`int (*f[3])(int)`) são lidos de fora para dentro: ao
  achar parênteses, o analisador pula até o `)` correspondente, lê primeiro
  os sufixos de fora (`(int)`), e só então volta para ler o de dentro. Com
  a lista de tokens na memória, "voltar" é só mudar um índice.

Os nomes ficam numa tabela única; cada nome carrega o significado atual
(variável, tipo, macro, struct). Declarar algo num bloco pendura o
significado antigo no novo, e fechar o bloco devolve o antigo: procurar um
nome é olhar um ponteiro.

As expressões viram uma árvore em que **as regras do C já foram aplicadas**:
as conversões automáticas aparecem como nós, `p + i` já vem multiplicado
pelo tamanho do elemento, `v[i]` já é `*(v + i)`, `a > b` já é `b < a`, e
contas só com números já foram feitas (por isso "expressão constante" é
"a árvore virou um número"). Os comandos não viram árvore: `if`, `while`,
`for` e `switch` são traduzidos na hora em rótulos e saltos.

Inicializadores são lidos para uma lista de "ponha tal valor a tantos bytes
do começo"; a mesma lista serve para variáveis globais (vira bytes e
relocações) e locais (vira instruções de guardar), e é ela que descobre o
tamanho de `int v[] = {...}`.

Onde o C permite e o GCC só avisa, este compilador **recusa**: ponteiro
recebendo inteiro, ponteiros de tipos diferentes sem conversão, função
usada sem declaração. Como não há avisos, o que seria aviso importante
virou erro.

### A representação intermediária (`ri.h`)

É a fronteira que o PRD pede (seção 4, item 5): a frente entrega um módulo
com símbolos, e cada função é uma lista de instruções de no máximo três
operandos (`t3 = soma t1, 4`; `guarda.i8 [t3], t5`; `salta_se_zero t5,
r7`). O fundo não sabe o que é um struct ou um `for`. `cc --ri` mostra a
lista.

Escolhas:

- Os temporários têm 32 ou 64 bits. Tipos menores só existem **na
  memória**: carregar estende, guardar corta, e há uma instrução para
  "cortar e estender" (as conversões para `char` e `short`).
- Um operando pode ser "o endereço do local 3 mais 8": variáveis e campos
  de struct não gastam instrução para ter o endereço calculado.
- Structs não cabem em temporário: o "valor" de um struct é o endereço
  dele, e copiar é uma instrução de copiar bloco.
- Regra dos temporários: depois de receber um valor, um temporário só é
  lido mais adiante na lista. A frente respeita isso por construção, e o
  fundo ganha o direito de reaproveitar a casa de um temporário na pilha
  olhando só a ordem da lista.

### Código x86 (`x86.c`)

O plano mais simples que funciona: cada temporário mora numa casa do quadro
da função; cada instrução da RI traz os operandos para EAX (ECX e EDX
ajudam), faz a conta e guarda o resultado. Não há alocação de
registradores. Dois atalhos baratos tiram a maior parte do desperdício:

1. se um resultado é usado uma vez só, e pela instrução seguinte, ele fica
   em EAX e nem vai para a pilha;
2. uma comparação seguida do salto que a testa vira `cmp` + `jcc`.

Com isso os executáveis saem com tamanho parecido com os do GCC `-O2` (o
`wc` tem 7,9 KB contra 7,1 KB; a Calculadora, 24 KB contra 20 KB).

A área dos argumentos das chamadas é reservada uma vez, no começo da
função: chamar é escrever os argumentos em `[esp]`, `[esp+4]`... e dar o
`call`. O quadro é arredondado para a pilha ficar alinhada em 16 bytes nas
chamadas. A convenção é a cdecl do System V para i386, a mesma do GCC,
porque o programa chama a libsagui compilada por ele: argumentos na pilha,
resultado em EAX (EDX:EAX para 64 bits), struct devolvido num endereço
escondido passado como primeiro argumento (e retirado da pilha por quem foi
chamado, com `ret 4`). Um cuidado que a convenção não garante: depois de
chamar uma função que devolve `char`, `short` ou `bool`, o resultado é
estendido de novo, porque só os bits de baixo de EAX são certos.

Contas de 64 bits: soma, subtração, bits e comparações são feitas em duas
metades (`add`/`adc`, `sub`/`sbb`). Multiplicação, divisão e deslocamento
chamam rotinas de `userland/libsagui/longo64.c`; a divisão de lá é a
"longa", bit a bit, porque o SaguiOS não tem a biblioteca do GCC.

### O montador (`montador.c`)

Não existe texto de assembly no caminho: o gerador chama uma função por
instrução e ela escreve os bytes. O arquivo é a tabela dos formatos do
manual da Intel para as instruções usadas (umas 40), com o tratamento do
byte ModRM num lugar só, incluindo as duas exceções do formato (`ESP` como
base exige o byte SIB; `[EBP]` sem deslocamento não existe). Saltos usam
sempre distância de 32 bits, remendada no fim da função. Quando entra um
endereço ainda desconhecido, ficam 4 bytes com o que somar e uma relocação
anotada.

### O ligador (`ligador.c`)

O programa precisa da libsagui, que é compilada pelo GCC no build. O
desenho: o build junta todos os objetos da libsagui num arquivo só,
`/sistema/lib/libsagui.o` (um objeto ELF relocável, 200 KB; como a
biblioteca é compilada com `-ffunction-sections -fdata-sections`, cada
função e cada dado continuam numa seção própria). O ligador:

1. lê esse objeto para a mesma forma em que o `x86.c` entrega o programa
   (seções com bytes, símbolos, relocações `R_386_32` e `R_386_PC32`);
2. casa os nomes entre os objetos;
3. parte da seção de `_inicio` e segue as relocações: só entra no programa
   o que for alcançado. Sem essa coleta, todo "olá, mundo" carregaria o TLS
   e o SSH (com ela, tem 6,5 KB);
4. dá endereços: código e dados de leitura a partir de `0x40000000`, dados
   que mudam numa página nova (o carregador do kernel dá permissão por
   página), como em `userland/programa.ld`;
5. corrige as relocações e grava o ELF com um ou dois segmentos `PT_LOAD`,
   sem tabela de seções (o carregador não usa).

Se o programa define um nome que a libsagui também tem (`mensagem`, por
exemplo), vale o do programa, desde que nenhuma parte usada da biblioteca
dependa do dela; se depender, é erro com explicação.

Os cabeçalhos que o `cc` lê em `/sistema/inc` são **os mesmos arquivos** do
build (`sagui.h`, `gui.h`, `http.h`, `abi.h`), sem versão adaptada: o
compilador aceita e ignora `__attribute__`, e trata `__builtin_va_list` e
companhia (um `va_list` é um `char *` que anda pela pilha, igual ao do GCC
no i386).

## Como é testado

`tests/h64_h66_compilador.py`:

- 70 programas com um erro cada (`tests/compilador/erros`): a mensagem
  inteira é comparada com a esperada, e a forma (posição, `^`, dica) é
  conferida;
- 50 programas válidos (`tests/compilador/validos`) compilam; rodam no
  Linux ligados a uma libsagui em que só a camada de chamadas de sistema
  foi trocada; e rodam **no SaguiOS (QEMU)**, com a saída e o código de
  saída esperados. As saídas esperadas foram conferidas contra os mesmos
  programas compilados pelo GCC (`--conferir-gcc`);
- o `cc` dentro do SaguiOS compila cinco deles, um programa em dois arquivos
  e um com erro; os executáveis feitos lá são byte a byte iguais aos feitos
  fora;
- os programas completos do manual (`docs/c-do-sagui.md`) compilam.

Fora do teste automático, durante o desenvolvimento: 41 dos 43 programas de
`userland/programas` compilam com o `cc` (os dois que não, `falha` e
`configuracoes`, usam assembly dentro do C); e uma imagem com o `sagsh`, o
Terminal, o Editor, o Arquivos, a Calculadora, a Banana, `wc`, `cat`, `ls`,
`cp` e outros trocados pelas versões feitas pelo `cc` passou nos testes
H14, H15, H16, H22 a H27.

## O que falta (ideias)

- Avisos (variável não usada, `=` em condição, formato do `imprimir`).
- Vários erros por compilação, com recuperação.
- Guardar variáveis em registradores; hoje tudo mora na pilha.
- Ponto flutuante (o kernel precisaria salvar o estado da FPU).
- Uma frente para uma linguagem própria em português, reaproveitando do
  `ri.h` para baixo.
