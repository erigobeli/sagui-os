# userland/tls: certificados X.509 e cliente TLS 1.3 do SaguiOS

Com esta pasta um programa do SaguiOS abre uma conexão segura (HTTPS, por
exemplo): negocia as chaves com o servidor, confere se o certificado dele é
de verdade e cifra a conversa. Tudo foi escrito neste projeto; a criptografia
vem da `userland/cripto`.

**Aviso.** Assim como a criptografia, este código não foi auditado. Ele segue
as especificações, é rígido com o que vem de fora e passa em testes contra o
`openssl` e contra servidores de verdade, mas serve para o projeto (abrir
páginas, conversar com serviços), não para banco. Em especial, **não consulta
revogação de certificados** (veja "Limitações").

## Arquivos

| Arquivo | O que tem |
|---|---|
| `tls.h` | A interface pública: certificados, ambiente, conexão |
| `tls_interno.h` | O que os arquivos dividem entre si (fatias, DER, registros) |
| `der.c` | Fatias (leitura com conferência de tamanho), texto e o leitor de DER |
| `x509.c` | Leitura do certificado, conferência de assinatura, nome do servidor, validação da cadeia |
| `chaves.c` | HKDF-Expand-Label e a troca de chave de um sentido |
| `registros.c` | Camada de registros (cifra, nonce, enchimento), alertas, remontagem das mensagens |
| `aperto.c` | O aperto de mão: ClientHello até o Finished, escada de segredos |
| `tls.c` | Depois do aperto: ler, escrever, fechar, KeyUpdate, bilhetes de sessão |

Uns 34 KB de código de máquina, nenhuma variável global.

## Como usar

```c
static Tls conexao;                 /* ~72 KB: variável estática ou memória do programa, nunca na pilha */

TlsAmbiente amb = { meu_ctx, enviar, receber, esperar, agora_ms, aleatorio, data_atual };
TlsOpcoes   op  = { "pt.wikipedia.org", raizes, nraizes, 15000, "http/1.1" };

if (tls_conectar(&conexao, &amb, &op) != TLS_OK)
    mostrar(tls_erro(&conexao));    /* "nome não confere: ...", "certificado vencido: ..." */
tls_escrever(&conexao, pedido, n);
r = tls_ler(&conexao, buf, sizeof buf);     /* > 0 bytes; 0 nada ainda; TLS_FECHADO; TLS_CORTADO; TLS_ERRO */
tls_fechar(&conexao);
```

- **O ambiente** são seis funções que quem chama fornece (ver `tls.h`). A
  biblioteca não chama o sistema: no teste elas são soquetes do Linux; no
  SaguiOS, o TCP, o relógio e o gerador de números aleatórios do núcleo.
  `receber` **não pode esperar**: devolve 0 se não há nada.
- **As raízes de confiança** são certificados em DER, na memória de quem
  chama (`X509Der`). Só precisam existir durante o `tls_conectar`.
- `tls_conectar` espera (com limite de tempo) até o aperto de mão acabar.
  `tls_ler` nunca espera. `tls_escrever` só volta depois de mandar tudo (ou
  de o envio ficar parado mais que o limite).
- `TLS_FECHADO` é o fim limpo (o servidor mandou o aviso `close_notify`).
  `TLS_CORTADO` quer dizer que a conexão caiu sem o aviso: os dados podem
  estar pela metade, e quem chama decide se aceita (um HTTP com
  `Content-Length` completo pode aceitar).
- Pilha: a cadeia de chamadas mais funda (`tls_conectar` → validação →
  conferência de assinatura da cripto) fica perto de 10 KB.
- A validação de certificados também pode ser usada sozinha:
  `x509_ler`, `x509_validar_cadeia`, `x509_nome_confere`.

## Testes

`python3 tests/h52_h53_tls.py` compila esta pasta com as opções do SaguiOS
(`-Werror`, sem libc), confere que ela só depende da `userland/cripto`, liga
com `tests/tls/teste_tls.c` (o único lugar com soquetes e libc) e então:

- fabrica com o comando `openssl` autoridades e certificados de teste (RSA,
  P-256, P-384, Ed25519, assinaturas PKCS#1, PSS e ECDSA) e confere leitura,
  nomes, cadeias boas e cada tipo de cadeia ruim, com a mensagem esperada;
- estraga certificados ao acaso (mil vezes) e lê todas as raízes de
  `/etc/ssl/certs`;
- conversa com o `openssl s_server -tls1_3` em todos os conjuntos de cifras e
  tipos de chave, sobe e desce centenas de KB, com a rede entregando 3 bytes
  por vez, com KeyUpdate, com pedido de certificado de cliente, com ALPN;
- põe uma "ponte" no meio que troca bits e corta a conexão;
- se há internet, busca uma página em `example.com` e em `pt.wikipedia.org`
  confiando só na raiz necessária, exportada do sistema na hora.

---

# Como cada parte foi feita (registro de autoria)

Tudo foi derivado das especificações, sem abrir o código de nenhuma outra
implementação de TLS, X.509 ou ASN.1: RFC 8446 (TLS 1.3), RFC 5280
(certificados), X.690 (DER), RFC 8017, 5480, 5758 e 8410 (identificadores dos
algoritmos), RFC 6125 (conferência do nome), RFC 6066 (SNI) e RFC 7301
(ALPN). As escolhas abaixo são as deste projeto.

## Fatias: a regra da casa para dados de fora (`der.c`)

Tudo que chega pela rede é lido por meio de uma **fatia**: um ponteiro e a
quantidade de bytes que ainda restam. As leituras (`fatia_ler8/16/24`,
`fatia_cortar`, `fatia_vetor`) conferem o tamanho antes e andam a fatia;
cortar um pedaço produz outra fatia, que não enxerga além do próprio fim.
Nenhum código do TLS ou dos certificados faz conta com ponteiro por conta
própria. Um tamanho mentiroso dentro de uma mensagem vira um erro de leitura
e nunca um acesso fora do buffer. O mesmo tipo serve para os vetores do TLS
("tamanho + bytes") e para o DER.

## O leitor de DER (`der.c`)

O DER escreve cada elemento como etiqueta, tamanho e conteúdo, e para cada
valor só existe uma escrita certa. O leitor tem uma função central,
`der_proximo`, que separa um elemento da frente da fatia. Ela é pequena e
rígida de propósito:

- só etiquetas de um byte (número até 30), as únicas que aparecem em
  certificados;
- tamanho na forma curta (um byte) ou longa (até 4 bytes), mas **sempre a
  mais curta possível**: forma longa com zero à esquerda, ou para um valor
  que cabia na curta, é recusada; tamanho indefinido (0x80) também;
- o conteúdo tem de caber no que resta da fatia.

Em cima dela há leitores com a regra de cada tipo: `der_esperar` (exige uma
etiqueta), `der_numero` e `der_inteiro_grande` (INTEGER não negativo, sem
zero sobrando na frente), `der_booleano` (só 00 ou FF), `der_bits` (BIT
STRING com a contagem de bits de enchimento). Não há árvore, nem tabela de
tipos, nem reserva de memória: quem lê uma estrutura chama esses leitores na
ordem em que os campos aparecem na definição ASN.1 e, no fim, exige que a
fatia tenha acabado ("nada sobra"). Os campos opcionais são decididos
espiando a próxima etiqueta (`der_espiar`).

Os identificadores de objeto (OIDs) não são decodificados em números: como o
DER só tem uma escrita para cada OID, basta comparar os bytes com tabelas
pequenas que estão no começo do `x509.c` (montadas à mão a partir dos números
das RFCs: o primeiro byte é 40·a + b, os demais vão de 7 em 7 bits). Famílias
de OIDs que só diferem no último byte (os `...WithRSAEncryption`, os
`ecdsa-with-SHA...`, os SHA-2, as extensões 2.5.29.x) são tratadas como
"prefixo + um byte".

## O certificado (`x509.c`)

`x509_ler` percorre o certificado uma vez e preenche uma estrutura `X509` de
uns 200 bytes só com **ponteiros para dentro do DER** (parte assinada, nomes
do emissor e do dono crus, chave, assinatura, lista do subjectAltName) e
alguns valores pequenos. Só são copiados o nome comum (para as mensagens) e
as datas.

- **Algoritmo de assinatura.** Aparece duas vezes (dentro e fora da parte
  assinada) e as duas têm de ser iguais byte a byte. São reconhecidos RSA
  PKCS#1 v1.5 e RSA-PSS com SHA-256/384/512, ECDSA com SHA-256/384/512 e
  Ed25519. No PSS os parâmetros são lidos por inteiro e só se aceita a forma
  que a cripto confere: resumo dito às claras, MGF1 com o mesmo resumo e sal
  do tamanho do resumo. O que não é reconhecido (SHA-1, MD5) **não é erro de
  leitura**: fica marcado como desconhecido e só impede o uso daquela
  assinatura. Isso importa porque várias raízes antigas do sistema são
  assinadas por elas mesmas com SHA-1, e a assinatura de uma raiz nunca é
  conferida.
- **Chave.** RSA (módulo e expoente, sem o zero de sinal), P-256 e P-384 (o
  ponto sem compressão, com o tamanho certo para a curva) e Ed25519. Outras
  chaves (P-521, por exemplo) ficam como "desconhecida".
- **Datas.** UTCTime (ano de dois dígitos: 50 a 99 é 19xx) e GeneralizedTime,
  só nas formas que a RFC 5280 admite (com segundos, terminadas em `Z`). A
  data vira **14 letras, `AAAAMMDDhhmmss`**, e comparar datas vira comparar
  texto: nenhuma conta de calendário, nenhuma divisão.
- **Extensões.** São entendidas: basicConstraints (autoridade, limite de
  caminho), keyUsage, subjectAltName, extKeyUsage. certificatePolicies é
  aceita mesmo crítica (não exigimos política nenhuma). Qualquer outra
  extensão **crítica** marca o certificado, e a validação o recusa, como
  manda a RFC. Extensão repetida é erro.

## A validação da cadeia (`x509_validar_cadeia`)

O caminho é montado **de baixo para cima**. Começa no certificado do servidor
e repete:

1. confere a validade (datas) e as extensões críticas do certificado atual;
2. se ele é, byte a byte, um dos certificados de confiança, acabou;
3. procura entre as raízes de confiança uma cujo nome de dono seja igual ao
   nome do emissor do atual e cuja chave confira a assinatura dele: se achar,
   acabou (a raiz também tem de estar na validade);
4. senão, procura do mesmo jeito entre os outros certificados que o servidor
   mandou; o achado vira o atual e sobe-se um degrau.

Consequências desse desenho:

- A **ordem** em que o servidor mandou a cadeia não importa, e certificados
  sobrando são ignorados. A raiz pode vir ou não na cadeia.
- As raízes são tentadas **antes** da cadeia em cada degrau. Isso resolve
  sozinho o caso das assinaturas cruzadas: se o servidor manda uma raiz nova
  assinada por uma raiz velha que não temos, paramos na raiz nova, que temos
  (aconteceu de verdade no teste com `example.com`).
- Cada certificado da cadeia entra no caminho uma vez só, então não há como
  ficar dando voltas; e são no máximo 10.
- Quem assina tem de ser autoridade: basicConstraints com cA, keyUsage (se
  houver) com keyCertSign, e o limite de caminho (pathLen) maior ou igual ao
  número de intermediárias abaixo dele. Para as raízes de confiança sem
  basicConstraints (certificados antigos, versão 1) vale a palavra de quem
  as instalou.
- Os nomes de emissor e dono são comparados **byte a byte**, não pelas regras
  de equivalência da RFC 5280. Só erra para o lado seguro.
- A folha tem de servir para servidor: keyUsage (se houver) com assinatura
  digital, extKeyUsage (se houver) com serverAuth.

Quando nenhum candidato a emissor serve, a função lembra o **motivo mais
grave** entre os candidatos descartados (assinatura inválida, não é
autoridade, limite de caminho, algoritmo desconhecido) e a mensagem sai
dele. Sem candidato nenhum: "autoassinado", "cadeia quebrada" (paramos ainda
na folha: falta a intermediária) ou "autoridade desconhecida" (chegamos a uma
autoridade, mas não a uma de confiança).

**O nome** é conferido por último (`x509_nome_confere`), contra as entradas
de DNS do subjectAltName, sem diferenciar maiúsculas e ignorando o ponto
final. O curinga só vale como o rótulo mais à esquerda **inteiro**
(`*.exemplo.org`), cobre exatamente um rótulo, e o resto precisa ter pelo
menos dois rótulos (`*.org` não vale). Um nome só de números e pontos é
tratado como endereço IPv4 e conferido contra as entradas de endereço. O nome
comum (CN) só é consultado se o certificado não tiver subjectAltName.

## A escada de segredos (`aperto.c`, `chaves.c`)

O cliente manda no ClientHello a metade dele de uma troca x25519; o
ServerHello traz a metade do servidor. Do segredo em comum saem todas as
chaves, por HKDF, numa escada de três degraus em que o sal de cada
HKDF-Extract vem do degrau anterior:

    Early Secret     = Extract(sal = zeros,              material = zeros)
    Handshake Secret = Extract(sal = Derivar(Early),     material = segredo x25519)
    Master Secret    = Extract(sal = Derivar(Handshake), material = zeros)

("Derivar" é o Expand-Label com o rótulo `derived` e o resumo do texto
vazio. O primeiro degrau existe para chaves pré-combinadas, que não usamos,
mas tem de ser calculado para os valores baterem.)

De cada degrau saem **segredos de tráfego**, um para cada sentido, sempre
pela mesma função, a HKDF-Expand-Label (`tls_expandir_rotulo`): é o
HKDF-Expand com um "info" formado pelo tamanho pedido, pelo rótulo com o
prefixo `tls13 ` e por um contexto. O rótulo separa os usos; o contexto é o
**transcrito**, o resumo de todas as mensagens do aperto até aquele ponto:

- do Handshake Secret, com o transcrito até o ServerHello: `c hs traffic` e
  `s hs traffic` (cifram o resto do aperto de mão);
- do Master Secret, com o transcrito até o Finished do servidor:
  `c ap traffic` e `s ap traffic` (cifram os dados).

De um segredo de tráfego saem a chave (`key`) e o IV (`iv`) do sentido
(`tls_sentido_armar`), a chave do Finished (`finished`) e, no KeyUpdate, o
segredo seguinte (`traffic upd`).

**O transcrito.** O resumo usado depende do conjunto de cifras, que só se
conhece no ServerHello, e o ClientHello já tem de entrar na conta. Em vez de
guardar o ClientHello para resumir depois, a conexão mantém **dois resumos
andando juntos** (SHA-256 e SHA-384) e, a partir do ServerHello, consulta só
o que vale. Para tirar o valor "até agora" sem encerrar a conta, finaliza-se
uma cópia do contexto.

**As provas do servidor.** No CertificateVerify, o servidor assina, com a
chave do certificado, 64 espaços + `TLS 1.3, server CertificateVerify` + um
zero + o transcrito até o Certificate; a conferência usa a mesma
`x509_conferir` dos certificados (ECDSA, RSA-PSS ou Ed25519; RSA PKCS#1 é
recusado aqui, como manda a RFC). A chave da folha é copiada para a conexão
ao validar a cadeia, porque a cadeia mora no buffer de mensagens, que é
reaproveitado. O Finished é um HMAC do transcrito com a chave `finished`; o
do servidor é comparado em tempo constante, e o nosso é mandado ainda com a
chave de aperto.

**Outros detalhes do aperto.** O ClientHello oferece só TLS 1.3, só x25519 e
os três conjuntos de cifras (ChaCha20-Poly1305 primeiro, por ser o mais
rápido na nossa máquina). O servidor só pode responder com o que foi
oferecido: versão, grupo, conjunto, eco do id de sessão e extensões são
conferidos, e extensão não pedida é erro. A marca do HelloRetryRequest (o
SHA-256 do texto "HelloRetryRequest" no lugar do "random") é calculada, não
copiada; como só oferecemos x25519, um pedido desses termina com uma mensagem
clara. Se o servidor pede certificado de cliente, respondemos com a lista
vazia. Uma mensagem não pode ficar atravessada numa troca de chaves: o buffer
de mensagens tem de estar vazio nesses pontos. Os segredos do aperto vivem
numa estrutura na pilha de `tls_conectar`, zerada no fim.

## A camada de registros (`registros.c`)

Um registro tem 5 bytes de cabeçalho (tipo, versão, tamanho) e até 16 KB de
conteúdo.

- **Recepção sem sobra.** A conexão pede à rede **exatamente** o que falta do
  registro atual: primeiro os 5 bytes do cabeçalho, depois o corpo do tamanho
  anunciado. Assim nunca sobra um pedaço do registro seguinte para guardar em
  outro lugar; há um buffer só, o registro é decifrado nele mesmo, e os dados
  de aplicação são entregues direto dele. Como `receber` não espera, a
  montagem do registro continua de onde parou na chamada seguinte.
- **Cifra.** Com chaves, todo registro se apresenta como "dados de aplicação"
  e o conteúdo é selado com o AEAD do conjunto; o cabeçalho entra como dado
  extra (autenticado, não cifrado). O **nonce** é o IV do sentido com o
  número do registro (64 bits) misturado por OU-exclusivo nos 8 últimos
  bytes; o número começa em zero a cada chave nova e nunca vai na rede.
- **Tipo verdadeiro e enchimento.** Dentro do texto decifrado, tiram-se os
  zeros do fim; o último byte que não é zero é o tipo verdadeiro. Registro só
  de zeros é erro.
- **Limites.** 2^14 bytes às claras, 2^14 + 256 cifrado, 2^14 depois de
  aberto; passou disso, alerta `record_overflow`.
- **Marca errada** (bit trocado no caminho, chave errada) recusa o registro
  inteiro: a cripto confere a marca antes de produzir qualquer byte.
- **Troca de cifra de fachada.** O registro `change_cipher_spec` de um byte,
  que os servidores mandam por compatibilidade, é ignorado durante o aperto
  de mão (e é erro fora dele). Nós também mandamos um, às claras, antes do
  nosso primeiro registro cifrado, e um id de sessão de 32 bytes, para a
  conversa parecer TLS 1.2 a equipamentos do caminho.
- **Mensagens do aperto.** Formam um fluxo que os registros cortam onde
  querem. Os registros de aperto são juntados num buffer de 36 KB e as
  mensagens inteiras são tiradas dele; a cadeia de certificados é validada
  ali mesmo, sem cópia.
- **Alertas.** O `close_notify` recebido vira `TLS_FECHADO`; qualquer outro
  alerta é fatal e vira uma mensagem em português com o código. Quando somos
  nós que desistimos, vai um alerta de cortesia (uma tentativa só de envio).
  `tls_fechar` manda o `close_notify`.
- **Depois do aperto.** NewSessionTicket é ignorado. KeyUpdate troca a chave
  de recepção; se o servidor pede, respondemos com o nosso KeyUpdate (ainda
  na chave antiga) e trocamos a de envio.

## Limitações

Ditas com todas as letras, para ninguém se enganar:

- **Sem consulta de revogação.** Não há OCSP, nem "OCSP stapling", nem listas
  de revogação (CRL). Um certificado roubado e revogado pela autoridade
  **continua sendo aceito** até vencer.
- **Sem retomada de sessão**: os bilhetes (NewSessionTicket) são jogados fora
  e não há PSK. Todo aperto de mão é completo.
- **Sem 0-RTT** (dados antecipados).
- **Sem certificado de cliente**: se o servidor pede, respondemos que não
  temos; se ele exigir, a conexão é recusada por ele.
- **Só cliente, só TLS 1.3.** Servidores que só falam TLS 1.2 ou mais antigo
  são recusados com uma mensagem clara.
- **Só x25519** na troca de chaves. Um servidor que só aceite P-256/P-384
  (raro) recusa a conexão; não há segunda tentativa (HelloRetryRequest).
- **Assinaturas e chaves**: as da cripto. Sem P-521, sem Ed448, sem chaves
  `rsa_pss_pss` (certificado cuja chave é declarada como só-PSS), sem
  RSA-PSS com sal de tamanho diferente do resumo, sem SHA-1. RSA até 4096
  bits.
- **Restrições de nome** (nameConstraints) não são processadas: marcada como
  crítica (o normal), a autoridade que a traz é recusada; não crítica, é
  ignorada. Políticas de certificado não são avaliadas.
- **Nomes internacionais** (IDN) têm de chegar já na forma `xn--`. Endereços
  IPv6 no certificado não são conferidos. O nome comum só é usado quando não
  há subjectAltName.
- **Limite de caminho**: os certificados "emitidos para si mesmos" no meio da
  cadeia contam como intermediárias (a RFC os desconta); só erra para o lado
  de recusar.
- **Tamanhos**: mensagens do aperto de até 36 KB (cadeias maiores são
  recusadas), até 10 certificados por cadeia, nome do servidor de até 255
  letras.
- Não é oferecida a extensão de limite de tamanho de registro, nem compressão
  de certificados, nem ECH. Não pedimos KeyUpdate por conta própria (uma
  conexão teria de passar de centenas de GB para precisar).
- `tls_escrever` espera até mandar tudo. Um programa que escreva muito sem
  ler, com um servidor que faça o mesmo, pode ficar parado até o limite de
  tempo.
- Uma conexão ocupa uns 72 KB e não pode ser usada por duas tarefas ao mesmo
  tempo.
