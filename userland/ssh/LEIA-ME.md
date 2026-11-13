# userland/ssh — o cliente SSH do SaguiOS

Biblioteca do cliente SSH 2 (H56 a H58). Foi escrita neste projeto a partir
das especificações, sem consultar o código de nenhuma outra implementação:

| Assunto | Documento |
|---|---|
| Arquitetura e tipos de dados (`string`, `mpint`, `name-list`) | RFC 4251 |
| Autenticação (`none`, `publickey`, `password`) | RFC 4252 |
| Transporte (identificação, pacotes, negociação, chaves) | RFC 4253 |
| Conexão (canais, janelas, terminal, shell) | RFC 4254 |
| `aes128-ctr`, `aes256-ctr` | RFC 4344 |
| `hmac-sha2-256` | RFC 6668 |
| Chaves e assinaturas `ssh-ed25519` | RFC 8709 |
| `curve25519-sha256` | RFC 8731 (e RFC 5656, seção 4, para as mensagens) |
| `rsa-sha2-256` | RFC 8332 |
| Base64 | RFC 4648 |

A criptografia vem toda de `userland/cripto`. Como ela e como o TLS, esta
biblioteca não depende de libc, de libgcc nem do núcleo, não usa ponto
flutuante nem divisão de 64 bits e não reserva memória.

**Aviso:** vale o mesmo aviso da cripto. É código próprio, não auditado,
conferido contra o OpenSSH nos testes. Serve para o projeto, não para
guardar segredo de Estado.

## Arquivos

| Arquivo | O que tem |
|---|---|
| `ssh.h` | A interface pública (a única coisa que os programas incluem) |
| `ssh_interno.h` | Números das mensagens, estados e as funções que as partes dividem |
| `pacotes.c` | Pacote binário (tamanho, enchimento, cifra, MAC), leitor/escritor de campos, erros |
| `troca.c` | Identificação, negociação, troca de chaves, assinatura do servidor, derivação |
| `autenticacao.c` | `ssh-userauth` |
| `canal.c` | `ssh-connection`: o canal da sessão, janelas, pendências, ler/escrever |
| `ssh.c` | `ssh_conectar`, `ssh_fechar` e as consultas |
| `chaves.c` | Base64, impressão digital, chave pública e arquivo de chave privada |

## Como se usa

Quem chama entrega um `Ssh` (55 800 bytes, com todos os buffers dentro: fora
da pilha!) e um `SshAmbiente`, a tabela de funções para falar com o mundo:
`enviar`, `receber` (sem esperar: 0 = nada por enquanto, negativo = acabou),
`esperar`, `agora_ms` e `aleatorio`. É o mesmo desenho do `TlsAmbiente`.

```c
static Ssh ssh;

SshOpcoes op = { .conferir_chave = minha_conferencia };
if (ssh_conectar(&ssh, &amb, &op) != SSH_OK) erro(ssh_erro(&ssh));

int r = ssh_autenticar_nenhum(&ssh, usuario);          /* descobre os métodos */
if (r == SSH_RECUSADO && tenho_chave)  r = ssh_autenticar_chave(&ssh, usuario, semente);
if (r == SSH_RECUSADO && servidor_aceita("password"))  /* ver ssh_metodos() */
    r = ssh_autenticar_senha(&ssh, usuario, senha);
if (r != SSH_OK) ...

ssh_abrir_sessao(&ssh, "xterm-256color", colunas, linhas, 0);   /* 0 = shell */

for (;;) {
    int n = ssh_ler(&ssh, buf, sizeof buf);     /* > 0 dados; 0 nada; < 0 acabou */
    if (n > 0) mostrar(buf, n);
    if (n < 0) break;                           /* SSH_FECHADO: ver ssh_saida() */
    if (ha_teclas) feito += ssh_escrever(&ssh, teclas + feito, nteclas - feito);
    if (tamanho_mudou) ssh_mudar_tamanho(&ssh, colunas, linhas);
    if (n == 0) dormir_um_pouco();
}
ssh_fechar(&ssh);
```

Três coisas a saber:

- `ssh_ler` é quem atende o protocolo inteiro (janelas, pedidos do servidor,
  sinal de vida, troca de chaves). Tem de ser chamada sempre, mesmo que o
  programa só queira escrever.
- `ssh_escrever` pode aceitar **menos** do que foi pedido, inclusive zero
  (janela do servidor cheia, ou troca de chaves em curso). O resto fica com
  quem chamou, para tentar de novo depois de um `ssh_ler`. Ela não espera a
  janela abrir de propósito: se esperasse sem ler, e o programa remoto
  estivesse devolvendo o que recebe (um `cat`), os dois lados travariam.
- Os erros matam a conexão e ficam explicados em `ssh_erro()`. A exceção é
  `SSH_RECUSADO` na autenticação: a conexão continua e dá para tentar outro
  método (`ssh_metodos()` diz quais).

## O transporte, passo a passo

**Identificação.** Cada lado manda uma linha `SSH-2.0-programa`. O servidor
pode mandar outras linhas antes; valem só as que começam com `SSH-`. Lemos
byte a byte para não engolir o começo do primeiro pacote. Aceitamos `2.0` e
`1.99`; linha com mais de 255 letras ou com byte zero é recusada.

**Pacote.** Tudo o mais anda em pacotes:

```
uint32 tamanho | byte enchimento | carga ... | enchimento aleatório | MAC
```

O tamanho não conta a si mesmo nem o MAC; o conjunto tem de ser múltiplo do
bloco da cifra (16 com AES, 8 antes das chaves) e levar de 4 a 255 bytes de
enchimento. Tudo menos o MAC é cifrado com AES em modo contador, e o MAC é
HMAC-SHA-256 sobre o número de sequência (32 bits, que começa em zero e
nunca é reiniciado, nem nas trocas de chaves) seguido do pacote em claro.

**Conferência na chegada** (`ssh_i_receber`). O pacote chega em dois tempos:
primeiro um bloco, para decifrar o tamanho; depois o resto e o MAC. Só
pedimos à rede o que falta do pacote atual, de modo que nunca fica na mão um
pedaço do pacote seguinte (que pode vir cifrado com outras chaves). A ordem
das verificações:

1. o tamanho: pacote inteiro de 16 a 35 000 bytes e múltiplo do bloco. É o
   único campo usado antes do MAC (neste modo de MAC não há outro jeito), e
   por isso o crivo é apertado;
2. o MAC, comparado em tempo constante. Se não confere, a conexão acaba com
   DISCONNECT de motivo 5 e nada do pacote é usado;
3. o enchimento (pelo menos 4, e tem de sobrar pelo menos o byte do tipo).

Os campos das mensagens são lidos por um `Leitor` que nunca passa do fim do
pacote: se faltar byte ele marca erro e devolve zeros, e cada mensagem
confere uma vez no fim (em geral exigindo também que não sobre nada).

**Negociação.** Cada lado manda um KEXINIT com as suas listas; vale, em
cada categoria, o primeiro nome da lista do cliente que o servidor também
tenha. As nossas listas:

| Categoria | O que oferecemos |
|---|---|
| Troca de chaves | `curve25519-sha256`, `curve25519-sha256@libssh.org` (são a mesma coisa) |
| Chave do servidor | `ssh-ed25519`, `rsa-sha2-256` |
| Cifra | `aes128-ctr`, `aes256-ctr` |
| MAC | `hmac-sha2-256` |
| Compressão | `none` |

Se não houver nome em comum, o erro diz qual categoria falhou e o que o
servidor oferecia. O pacote de "palpite" que um servidor pode mandar junto
do KEXINIT é descartado quando o palpite estava errado.

**Troca de chaves.** Sorteamos uma chave efêmera X25519 e mandamos a parte
pública (Q_C). O servidor responde com a chave dele (K_S), a efêmera dele
(Q_S) e uma assinatura. Os dois lados calculam

```
K = X25519(privada efêmera, pública efêmera do outro)
H = SHA-256( V_C, V_S, I_C, I_S, K_S, Q_C, Q_S, K )
```

onde V são as linhas de identificação e I as cargas dos dois KEXINIT, cada
um como `string`, e K como `mpint` (os 32 bytes do X25519 lidos com o byte
mais significativo primeiro). A assinatura do servidor é sobre H: de uma vez
prova que ele tem a chave privada de K_S e que ninguém mexeu na negociação
(um bit trocado no KEXINIT, que viaja em claro, muda o H e a assinatura
deixa de conferir; há um teste disso). Um resultado todo zero do X25519
(ponto inválido) é recusado. Chaves RSA só de 2048 a 4096 bits.

**Quem é o servidor.** Só depois de a assinatura conferir chamamos a função
`conferir_chave` de quem usa a biblioteca, com o tipo da chave, o "blob"
(a chave no formato do protocolo) e a impressão digital `SHA256:...`, igual
à do `ssh-keygen -l`. A biblioteca não guarda servidores conhecidos: decidir
se confia (e avisar com força se a chave mudou) é do programa. Se a resposta
é "não", a conexão acaba ali, antes de o nome do usuário ser enviado.

**Derivação.** O H da primeira troca vira o identificador da sessão. Cada
chave é `SHA-256(K || H || letra || identificador)`: letras A e B para os
vetores iniciais (do cliente e do servidor), C e D para as cifras, E e F
para os MACs. Um SHA-256 já dá os 32 bytes da maior chave, então a regra de
esticar da RFC nunca é usada. Passamos a mandar com as chaves novas logo
depois do nosso NEWKEYS e a receber com as novas depois do NEWKEYS dele.

**Trocas seguintes.** O servidor pode mandar um KEXINIT a qualquer momento
(o OpenSSH faz isso depois de certo volume ou tempo). A troca corre inteira
dentro de quem estava lendo, de uma vez, porque entre o KEXINIT e o NEWKEYS
nenhum lado pode mandar outra coisa. A chave do servidor tem de ser a mesma
da primeira vez. Nós também pedimos chaves novas depois de 1 GB (ajustável
em `SshOpcoes.trocar_apos_bytes`) ou de 1 hora: o KEXINIT sai, continuamos
lendo normalmente (o servidor ainda pode ter dados a caminho) e, até a
troca terminar, `ssh_escrever` devolve 0 e tudo que o canal precisaria
mandar fica anotado (ver "pendências", abaixo).

**Mensagens de qualquer hora.** IGNORE e DEBUG são descartadas; DISCONNECT
encerra com o texto do servidor em `ssh_erro()`; a um tipo desconhecido
respondemos UNIMPLEMENTED com o número de sequência dele.

**Violações.** Em qualquer erro de protocolo mandamos um DISCONNECT com o
motivo da RFC (2 protocolo, 3 troca de chaves, 5 MAC, 9 chave do servidor,
11 aplicação) e a mesma frase que fica em `ssh_erro()`, e a conexão morre:
toda chamada seguinte devolve o mesmo código.

## Autenticação

Cada pedido leva usuário, serviço (`ssh-connection`) e método.

- `none`: serve para o servidor responder com a lista de métodos.
- `publickey` com `ssh-ed25519`: mandamos a chave pública e a assinatura de
  uma vez (sem a consulta prévia, que é opcional). Assina-se o identificador
  da sessão seguido do próprio pedido, com `ed25519_assinar` e a semente de
  32 bytes que quem chama entrega; assim a assinatura não serve em outra
  conexão.
- `password`: a senha vai dentro do pacote cifrado. Se o servidor responder
  que a senha venceu e tem de ser trocada (PASSWD_CHANGEREQ), a conexão é
  encerrada com uma mensagem dizendo isso.

O texto de aviso do servidor (banner) fica em `ssh_aviso()`, já sem
caracteres de controle (um servidor mal-intencionado poderia usar sequências
de escape para enganar quem lê). "Sucesso parcial" (o servidor exige dois
métodos) aparece como `SSH_RECUSADO` com a explicação e a lista do que
falta.

## O canal e as janelas

Há um canal só: o nosso número 0, do tipo `session`. `ssh_abrir_sessao`
abre o canal, pede o terminal (`pty-req`, com o TERM e o tamanho dados e
sem modos especiais) e depois o `shell` ou um `exec`, esperando a resposta
de cada pedido.

Cada lado anuncia uma janela: quantos bytes de dados aceita receber. O
outro desconta o que manda e para em zero, até receber um WINDOW_ADJUST.

- **Do servidor para nós.** Anunciamos 1 MB e pacotes de até 32 768 bytes.
  Os dados são entregues por `ssh_ler` direto do pacote que chegou, sem
  buffer intermediário: enquanto quem chama não consome o pacote atual, o
  próximo não é lido da rede (e o TCP segura o servidor). Quando metade da
  janela foi consumida, ela é reaberta. Servidor que manda mais que a
  janela, ou depois de ter dito EOF, é desconectado.
- **De nós para o servidor.** `ssh_escrever` manda no máximo o que cabe na
  janela dele, no pacote máximo dele e em 16 384 bytes, e devolve quanto
  foi.

A saída de erro do programa remoto (dados "estendidos") é entregue junto com
a saída normal, como num terminal.

**Fim.** `ssh_enviar_fim` manda EOF (o programa remoto vê o fim da entrada)
e ainda dá para ler. Quando o servidor manda CLOSE, devolvemos o nosso CLOSE
e `ssh_ler` passa a devolver `SSH_FECHADO`; `ssh_saida()` diz o código de
saída (`exit-status`) ou o sinal (`exit-signal`). `ssh_fechar` manda um
DISCONNECT de despedida e apaga as chaves.

**Pedidos do servidor.** Pedidos globais que querem resposta (como o
`keepalive@openssh.com`, com que o servidor confere se estamos vivos)
recebem REQUEST_FAILURE, que é a resposta certa para o que não conhecemos e
basta para ele; pedidos no canal recebem CHANNEL_FAILURE; tentativas de
abrir canais para o nosso lado recebem OPEN_FAILURE.

**Pendências.** Enquanto uma troca de chaves pedida por nós não termina, só
pacotes da troca podem sair. Por isso tudo que o canal manda por conta
própria (reabrir a janela, as respostas acima, devolver o CLOSE, o aviso de
tamanho novo, o EOF) é anotado e sai em `ssh_i_pendencias`, na ordem, assim
que pode. A fila de respostas tem 16 lugares; um servidor que faça mais
pedidos que isso durante uma única troca é desconectado.

## Chaves do usuário (para a ferramenta `ssh-chave`)

Uma chave Ed25519 é uma semente de 32 bytes aleatórios. Gerar um par é
sortear a semente; o resto sai dela:

- `ssh_chave_blob` dá a chave pública no formato do protocolo (RFC 8709):
  `string "ssh-ed25519"` seguido de `string` com os 32 bytes (51 no total).
- `ssh_chave_publica_linha` dá a linha para colar no `authorized_keys` do
  servidor: `ssh-ed25519 <blob em base64> <comentário>`.
- `ssh_chave_publica_ler` faz o caminho de volta (serve também para linhas
  de um arquivo de servidores conhecidos) e `ssh_digital` dá a impressão
  digital.
- `ssh_chave_privada_escrever` / `ssh_chave_privada_ler` cuidam do arquivo
  da chave privada.

### O arquivo de chave privada

É um formato **nosso** (não é o do OpenSSH), de texto, uma informação por
linha:

```
SaguiOS chave SSH 1
tipo: ssh-ed25519
semente: <os 32 bytes da semente, em base64>
publica: <o blob da chave pública, em base64>
comentario: <texto livre>
```

Regras da leitura: a primeira linha tem de ser exatamente o cabeçalho;
`tipo` e `semente` são obrigatórias; `publica` é opcional, mas se existe é
recalculada da semente e comparada, de modo que um arquivo estragado é
recusado em vez de virar outra chave; linhas com nomes desconhecidos são
ignoradas (para versões futuras); fins de linha LF ou CR LF.

**O arquivo não é cifrado.** Quem tem o arquivo tem a chave.

## Limitações

- Só cliente; um canal de sessão por conexão; sem encaminhamento de portas,
  de X11 ou de agente; sem `subsystem` (não há SFTP).
- Chave de usuário só Ed25519; sem `keyboard-interactive` (servidores que só
  oferecem senha por esse método não aceitam a nossa senha) e sem troca de
  senha vencida.
- Só os algoritmos da tabela acima. Não há `chacha20-poly1305`, AES-GCM,
  MACs "etm", Diffie-Hellman clássico, ECDSA nem `ssh-rsa` com SHA-1.
- Não implementa as extensões do OpenSSH descritas fora das RFCs (entre
  elas a troca de chaves "estrita" e o `ext-info`). O ataque de truncamento
  de prefixo que motivou a troca estrita depende de cifras que não temos
  (ChaCha20-Poly1305 ou CBC com MAC "etm"); com CTR e MAC sobre o pacote em
  claro, mexer nos números de sequência faz o MAC falhar.
- O campo do tamanho é decifrado e usado antes do MAC (é do desenho deste
  modo de MAC). O crivo limita o estrago: um tamanho adulterado quase sempre
  cai fora das regras e encerra a conexão; no pior caso ficamos esperando
  até 35 000 bytes que nunca fecham um MAC válido.
- A biblioteca não guarda servidores conhecidos nem lê arquivos: isso é do
  programa.
- Saída e saída de erro do programa remoto chegam misturadas.
- A conta de X25519 e a assinatura Ed25519 são em tempo constante (ver a
  cripto); a conferência RSA não é, e não precisa (só dados públicos).
- **Não testado:** o login por senha que dá certo. O servidor de teste roda
  sem ser root e não consegue conferir senhas; só o caminho da recusa foi
  exercitado. O pacote da senha segue a RFC 4252, seção 8, mas a primeira
  prova de verdade será contra um servidor real.

## Testes

`python3 tests/h56_h58_ssh.py` compila a biblioteca com as opções do SaguiOS
(confere que não ficou devendo símbolo e o uso da pilha), liga ao programa
`tests/ssh/teste_ssh.c` (o único que usa libc e soquetes) e conversa com o
`sshd` do sistema, rodando como o usuário atual numa porta alta de
127.0.0.1, com chaves e configuração temporárias em `build/testes/h56-ssh`.
