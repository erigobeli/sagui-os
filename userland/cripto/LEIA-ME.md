# userland/cripto: a criptografia do SaguiOS

Biblioteca de criptografia escrita neste projeto, para o cliente TLS 1.3 e o
cliente SSH do sistema. Não depende de nada: nem da libsagui, nem do núcleo,
nem da biblioteca do compilador. Um programa usa incluindo `cripto.h` e
ligando os `.c` desta pasta.

**Aviso.** Criptografia própria não é auditada. Ela é conferida com os vetores
oficiais e cuida do tempo constante onde há segredo, mas serve para o projeto
(abrir páginas, SSH, conversar com a IA), não para banco.

## Arquivos

| Arquivo | O que tem |
|---|---|
| `cripto.h` | A interface pública: tipos, contextos e funções |
| `interno.h` | O que os arquivos dividem entre si (leitura de bytes, números grandes) |
| `utilidades.c` | Copiar, zerar e comparar em tempo constante |
| `resumos.c` | SHA-256, SHA-384, SHA-512, HMAC, HKDF |
| `aes.c` | AES-128/256, modo contador (CTR), AES-GCM |
| `chacha.c` | ChaCha20, Poly1305, AEAD ChaCha20-Poly1305 |
| `curva25519.c` | X25519 e Ed25519 |
| `numeros.c` | Números grandes, Montgomery, conferência RSA (PKCS#1 v1.5 e PSS) |
| `ecdsa.c` | Conferência ECDSA em P-256 e P-384, leitura do DER |

## Como usar (regras gerais)

- Funções que podem falhar devolvem `CRIPTO_OK` (0) ou `CRIPTO_ERRO` (-1),
  **inclusive as de verificar**: 0 é assinatura válida. Compare sempre com
  `CRIPTO_OK`.
- Não há `malloc`: quem chama passa contextos e buffers. A função que mais usa
  pilha é a `ecdsa_verificar` (uns 4 KB de quadro, uns 6 KB na cadeia toda).
- Tamanhos são `u32`. Números grandes (RSA, ECDSA) entram em bytes com o mais
  significativo primeiro; X25519 e Ed25519 usam a ordem inversa, como nas RFCs.
- O decifrar dos dois AEAD confere a marca antes de produzir qualquer byte e
  zera a saída se ela não bate.
- A biblioteca não tem gerador de números aleatórios: chaves e nonces vêm de
  quem chama.

## Testes

`python3 tests/h50_cripto.py` compila estes arquivos com as opções do SaguiOS
(32 bits, `-ffreestanding -nostdlib -mgeneral-regs-only`, `-Werror`), confere
que nenhum símbolo fica faltando (nada de `memcpy` nem de `__udivdi3`), liga
com `tests/cripto/teste_cripto.c` e confere os resultados com os vetores
oficiais (`tests/cripto/vetores.py`) e com o `openssl` da máquina.

---

# Como cada parte foi feita (registro de autoria)

Tudo foi derivado das especificações, sem abrir o código de nenhuma outra
biblioteca: FIPS 180-4, FIPS 197, SP 800-38A/38D, RFC 2104, 5869, 8439, 7748,
8032, 8017 e FIPS 186. As escolhas abaixo são as deste projeto; várias são
diferentes das habituais justamente porque saíram da conta e não de um modelo.

## SHA-2, HMAC e HKDF (`resumos.c`)

Implementação direta da FIPS 180-4: bloco de 64 bytes e 64 rodadas no SHA-256,
bloco de 128 bytes e 80 rodadas com palavras de 64 bits no SHA-512. O SHA-384
é o SHA-512 com outros valores iniciais e a saída cortada, então os dois
dividem o contexto. As tabelas de constantes não foram copiadas de lugar
nenhum: um script calculou, com inteiros grandes, a parte fracionária das
raízes cúbicas dos primeiros 64 (ou 80) primos e das raízes quadradas dos
primeiros 8 (do 9º ao 16º no SHA-384), que é a definição que a norma dá. O
tamanho total é contado em bytes num número de 64 bits; no SHA-512 os três
bits que sobem ao virar bits vão para a metade alta do campo de 128 bits.

O HMAC guarda dois resumos já "abertos" (um com chave^ipad, outro com
chave^opad) em vez da chave: a mensagem entra no de dentro e, no fim, o
resultado dele entra no de fora. O HKDF abre o HMAC da PRK uma vez e copia o
contexto a cada bloco de saída.

Num processador de 32 bits, somar, girar e deslocar 64 bits o compilador faz
sozinho; só a divisão pediria a biblioteca dele, e não há nenhuma divisão de
64 bits na biblioteca inteira.

## AES (`aes.c`): sem tabelas, fatiado em bits

O AES costuma ser feito com uma tabela de 256 bytes para o SubBytes. Como o
índice da tabela depende da chave, o tempo de acesso (cache) vaza a chave.
Aqui não há tabela: a FIPS 197 define o SubBytes como o **inverso em GF(2^8)
seguido de uma transformação afim**, e é essa conta que é feita.

- **Representação.** O estado de 16 bytes vira 8 palavras: a palavra `k` tem
  o bit `k` de todos os bytes, e o bit `i` da palavra pertence ao byte `i` do
  estado. Um E ou um OU-exclusivo entre palavras age nos 16 bytes de uma vez.
  Como a palavra tem 32 bits e o bloco só usa 16, a metade de cima leva um
  segundo bloco: CTR e GCM cifram dois blocos por passada.
- **SubBytes.** Em fatias, a palavra `k` é o coeficiente de x^k. A
  multiplicação em GF(2^8) são os 64 produtos `a[i] & b[j]` somados no grau
  `i+j`, e a redução usa x^8 = x^4 + x^3 + x + 1 (cada grau de 14 a 8 é
  dobrado para baixo). O quadrado é só espalhar os coeficientes (grau i vai
  para 2i). O inverso é x^254, com quatro multiplicações:
  x^3 = x^2·x; x^15 = (x^3)^4·x^3; x^252 = (x^15)^16·x^12; x^254 = x^252·x^2.
  Depois a transformação afim é um OU-exclusivo de cinco palavras giradas e a
  constante 0x63 é uma negação das palavras 0, 1, 5 e 6.
- **ShiftRows.** Os bytes da linha `r` são os bits r, r+4, r+8, r+12; andar
  uma coluna é deslocar 4 bits, com máscaras para a volta ficar dentro de cada
  metade de 16 bits.
- **MixColumns.** Cada byte vira 2·(a0+a1) + a1 + a2 + a3; "o byte n linhas
  abaixo" é a palavra girada n bits dentro de cada grupo de 4, e multiplicar
  por 2 é subir cada fatia um grau, voltando a de cima nos graus 0, 1, 3 e 4.
- **Chave.** A expansão é a da seção 5.2 da norma; o SubWord usa a mesma troca
  de bytes fatiada (num bloco de rascunho), então também não tem tabela. As
  chaves de rodada ficam guardadas já fatiadas e repetidas nas duas metades.
- Só existe o sentido de cifrar (CTR e GCM não usam o outro). AES-192 não é
  oferecido.

**Modo contador.** O contexto guarda o contador e o resto do fluxo, para
poder cifrar aos pedaços (SSH). No SSH o contador é o bloco inteiro de 128
bits; no GCM, só os 4 últimos bytes.

**GCM e o GHASH.** Um elemento de GF(2^128) fica em 4 palavras de 32 bits, na
ordem dos bytes. O GCM numera os bits "ao contrário": o bit mais alto do
primeiro byte é o coeficiente de x^0. Então multiplicar por x é deslocar para
a direita, e o bit que cai (x^128) volta como x^7+x^2+x+1, que nessa
numeração é o byte E1 no começo. A multiplicação Y·H é a da escola, bit a
bit: 128 voltas em que o bit de Y vira uma máscara (tudo 0 ou tudo 1), V é
somado sob a máscara e depois multiplicado por x. Não há tabela de múltiplos
de H nem desvio: é mais lento que os métodos com tabela, mas é em tempo
constante e Y e H são segredos. O nonce de 12 bytes vira o contador inicial
direto; outros tamanhos passam pelo GHASH, como manda a SP 800-38D.

## ChaCha20 e Poly1305 (`chacha.c`)

O ChaCha20 é a RFC 8439 ao pé da letra: 16 palavras, dez voltas duplas
(colunas e diagonais), soma do estado original. Só somas, giros e
OU-exclusivos: tempo constante por construção.

O Poly1305 faz contas módulo 2^130 - 5. O acumulador e o `r` ficam em **5
pedaços de 26 bits**: o produto de dois pedaços tem 52 bits, e os cinco
produtos de uma coluna somados cabem em 64 bits, deixando os vai-uns para o
fim. Como 2^130 = 5 (mod p), os termos que passariam do quinto pedaço entram
já multiplicados por 5 (`5·r` é calculado antes). No fim, o valor é reduzido
de verdade: calcula-se h+5 e, se isso alcança 2^130, é porque h >= p e a
resposta é h+5-2^130; a escolha é por máscara. Depois soma-se `s` módulo
2^128. Os vetores 5 a 11 do apêndice A.3 da RFC (casos de borda dessa redução)
passam.

O AEAD tira a chave do Poly1305 do bloco de contador 0, cifra a partir do
contador 1 e autentica extras, texto cifrado, enchimentos e tamanhos.

## Curve25519 (`curva25519.c`)

**O corpo.** Um elemento módulo p = 2^255 - 19 fica em **9 pedaços: oito de
29 bits e o último de 23** (8·29 + 23 = 255), em palavras de 32 bits. É uma
representação própria deste projeto. Com 3 bits de folga por palavra, as 9
parcelas de uma coluna do produto (58 bits cada) somam no máximo uns 61 bits
e cabem numa variável de 64 sem acertar vai-uns no meio. Depois do produto de
17 colunas, acertam-se os vai-uns em pedaços de 29 bits e a metade alta é
dobrada sobre a baixa: as colunas altas valem 2^261 vezes as baixas, e
2^261 = 2^6·2^255 = 64·19 = **1216** (mod p). O que passar do bit 255 no
último pedaço volta ao primeiro vezes 19. Os pedaços ficam "quase acertados"
(abaixo de 2^29 + 19) entre as contas; a subtração soma antes 4p, pedaço a
pedaço, para nunca ficar negativa. Só ao virar bytes o número é reduzido para
[0, p), com a mesma ideia do Poly1305 (soma 19, olha se chegou a 2^255,
escolhe por máscara). O quadrado calcula cada produto cruzado uma vez só.

**Inverso e raiz.** 1/x = x^(p-2). Chamando f(k) = x^(2^k - 1), vale
f(a+b) = f(a)^(2^b)·f(b); a cadeia 1, 2, 4, 5, 10, 20, 40, 50, 100, 200, 250
dá f(250), e p-2 = (2^250-1)·32 + 11, logo o inverso é f(250)^32 · x^11. A
potência da raiz quadrada é (p-5)/8 = (2^250-1)·4 + 1.

**X25519.** A escada de Montgomery da RFC 7748, só com a coordenada u: dois
pontos vizinhos, uma duplicação e uma soma diferencial por bit, 255 passos
sempre. A troca dos pontos conforme o bit é feita por máscara. O resultado
todo zero (ponto de ordem pequena) é detectado juntando os bytes com OU e
devolve erro, como o TLS 1.3 exige.

**Ed25519.** Pontos em coordenadas estendidas (X, Y, Z, T) com as fórmulas de
soma e duplicação da seção 5.1.4 da RFC 8032; a soma é completa (vale para
quaisquer dois pontos), então não há casos especiais. A multiplicação por
escalar é "dobra e soma sempre": 256 passos, em cada um calcula-se a soma e
escolhe-se por máscara se ela fica. Não usa tabela do ponto base: é mais
lenta (alguns milissegundos por assinatura), mas simples e em tempo constante.
As constantes (d, 2d, raiz de -1, ponto base) foram calculadas por script a
partir das definições (d = -121665/121666, y = 4/5 com x par).

As contas módulo L (a ordem do grupo) usam a divisão da escola em binário: o
acumulador dobra, recebe um bit e perde um L se passou, com a subtração feita
sempre e escolhida por máscara. São 512 voltas para reduzir um SHA-512; é
pouco diante da multiplicação de ponto. S = r + k·a é um produto de 8×8
palavras seguido dessa redução.

Ao assinar, a chave pública é recalculada da semente (não é recebida de fora,
para não haver como assinar com um par descasado). Ao conferir, exige-se
S < L, a chave pública tem de decodificar (y reduzido, ponto na curva), e
compara-se a codificação de [S]B - [k]A com os bytes de R, o que também
recusa um R não reduzido. É a equação sem o fator 8, permitida pela RFC.

## Números grandes e RSA (`numeros.c`)

Um número é um vetor de palavras de 32 bits, a menos significativa primeiro,
com até 128 palavras (4096 bits).

**Montgomery.** Para calcular a·b mod n sem dividir por n, cada número x é
guardado como x·R mod n, com R = 2^(32k). A operação básica devolve
a·b/R mod n. Ela intercala, para cada palavra de b: (1) somar a·b[i] ao
acumulador; (2) escolher q = t[0]·n0 mod 2^32, com n0 = -1/n mod 2^32, de
modo que t + q·n termine em 32 zeros; (3) somar q·n e descartar a palavra de
baixo. O acumulador nunca passa de 2n, e uma subtração no fim acerta. n0 sai
por Newton (x ← x·(2 - n·x) dobra os bits certos; n já é o próprio inverso
módulo 8). R mod n e R² mod n saem **sem divisão**: parte-se de 1 e dobra-se
módulo n, 32k vezes para R e mais 32k para R². A potência é quadrado e
multiplicação, do bit mais alto do expoente para o mais baixo.

**RSA.** Só conferência. Exige módulo ímpar de 1024 a 4096 bits, assinatura do
tamanho exato do módulo e menor que ele, expoente ímpar maior que 1.
No PKCS#1 v1.5 o bloco esperado (00 01 FF…FF 00, DigestInfo, resumo) é
**montado e comparado inteiro**, em vez de interpretar o bloco recebido: é
assim que se evitam as falsificações clássicas de enchimento curto. No PSS
segue-se a seção 9.1.2 da RFC 8017 com MGF1 do mesmo resumo e sal do tamanho
do resumo, incluindo o caso em que o módulo tem 8j+1 bits (o bloco tem um
byte a menos).

## ECDSA (`ecdsa.c`)

Só conferência, em P-256 e P-384. As contas do corpo e as da ordem do grupo
usam a mesma multiplicação de Montgomery genérica do RSA (8 ou 12 palavras),
sem a redução especial de cada primo do NIST: mais lento, mas um código só
para as duas curvas. Os parâmetros das curvas foram conferidos por script
(G está na curva, n·G é o infinito, p e n passam no teste de primalidade).

Pontos em coordenadas jacobianas (x = X/Z², y = Y/Z³). As fórmulas foram
derivadas da regra da corda e da tangente eliminando denominadores; na
duplicação, como a = -3, a inclinação fatora em 3(X - Z²)(X + Z²). A soma
trata os casos de pontos iguais (vira duplicação), opostos e infinito.
[u1]G + [u2]Q é feito com os dois escalares juntos: uma dobra por bit e a
soma de G, Q ou G+Q conforme o par de bits. Os inversos (1/s módulo n e 1/Z
módulo p) são potências a n-2 e p-2.

Confere-se: formato 04||X||Y, coordenadas menores que p, ponto na curva, r e
s entre 1 e n-1, resumo cortado nos bytes da esquerda se for maior que a
curva. A leitura do DER é rígida (nada sobra, inteiros não negativos).

## O que é em tempo constante e o que não é

**É** (nenhum desvio nem índice de memória depende de segredo):

- AES inteiro (cifra e expansão da chave), CTR e GCM, inclusive o GHASH;
- ChaCha20 e Poly1305;
- X25519; geração de chave e assinatura Ed25519 (inclusive as contas mod L);
- SHA-2, HMAC e HKDF (o tempo só depende dos tamanhos);
- a comparação das marcas dos AEAD (`cri_diferentes`), que lê todos os bytes
  e só então decide. O teste confere no código de máquina que ela não tem
  saída antecipada.

**Não é**, de propósito, porque só vê dados públicos:

- potência modular, RSA (conferência), ECDSA (conferência) e a leitura de
  DER: têm desvios conforme os bits dos números. **Não servem para assinar
  nem para nada com chave privada.**
- a conferência Ed25519 tem desvios nas checagens de formato (S < L, ponto
  válido).

**Ressalvas honestas:**

- "Tempo constante" aqui é uma propriedade do código C. O compilador poderia,
  em tese, transformar uma seleção por máscara num desvio; isso não foi
  verificado instrução por instrução, exceto na comparação.
- A multiplicação de 32×32 em 64 bits é tratada como de tempo fixo, o que
  vale nos processadores x86 a que o SaguiOS se destina.
- O tamanho das mensagens e dos dados extras não é segredo para nenhuma das
  rotinas.
- Segredos intermediários são zerados da pilha com uma escrita que o
  compilador não pode remover, mas cópias em registradores não são
  controladas.

## Limitações

- Sem AES-192, sem decifração de bloco AES (CBC não é possível).
- GCM com marca de 16 bytes apenas; tamanhos em `u32` (mensagens de até
  4 GB, bem acima de um registro de TLS).
- RSA e ECDSA: só conferência. Sem RSA-PSS com sal de tamanho diferente do
  resumo. Sem curvas além de P-256 e P-384 (sem P-521).
- Ed25519 puro apenas (sem Ed25519ph/ctx). Sem X448/Ed448.
- Velocidade: prioridade para clareza e tempo constante. Medido no computador
  de desenvolvimento (32 bits): AES-GCM perto de 5 MB/s, ChaCha20-Poly1305
  perto de 18 MB/s, X25519 abaixo de 1 ms, assinatura Ed25519 uns 5 ms.
