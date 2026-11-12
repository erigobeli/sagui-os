# PRD 2.0: SaguiOS conectado

> Continuação do [`prd.md`](prd.md). A versão 1.0 está pronta e aprovada no Lenovo G470 (tags `v1.0` e `v1.1`).
> Hardware alvo: **máquina virtual primeiro** (VirtualBox e QEMU), **Lenovo G470 para confirmar** cada fase.
> Modelo recomendado para executar: **Claude Fable 5.2** (seção 10).

---

## 0. Como usar este documento (leia primeiro, agente)

1. Este PRD é a especificação da versão 2.0. As regras do `prd.md` (seções 2 e 2.1) continuam valendo, com os acréscimos da seção 2 abaixo.
2. Siga a ordem das fases e das histórias. Antes de cada história, releia a seção dela e os critérios de aceite. Só marque como feita quando todos os critérios passarem nos testes automáticos.
3. O acompanhamento é pelo **`status-v2.md`** e pelo painel **`dashboard/v2/index.html`**, gerados por `SAGUI_PROJETO=v2 tools/status.py` (seção 9). O painel da versão 1.0 fica como histórico e não é mais alterado.
4. Trabalhe em modo autônomo **dentro de cada fase**. Ao terminar uma fase, **pare** e avise o Eli (seção 8). Só comece a próxima depois que ele liberar.
5. Toda decisão que este PRD não cobre: escolha a opção mais simples que funcione na VM e no G470, anote em `docs/decisoes.md` (numeração continua a partir de D031) e siga em frente.
6. Ao retomar depois de uma pausa, leia `status-v2.md` primeiro.

---

## 1. Objetivo

Transformar o SaguiOS num sistema que o Eli consiga **usar para trabalhar**: dar boot, abrir um terminal, entrar num servidor por SSH, navegar em páginas simples, criar programas dentro do próprio sistema e pedir ajuda a uma IA que escreve, compila e roda esses programas. Tudo escrito do zero, como na versão 1.0.

Ao final da versão 2.0, o SaguiOS:

1. **Desliga e reinicia** pelo menu e tem um app de **Configurações** (relógio, aparência, som, rede, IA, remover programas), com as configurações guardadas no pendrive de dados.
2. **Toca som de verdade** (Intel HD Audio), além do bip.
3. **Entra na rede** com cabo: DHCP, DNS, `ping`.
4. **Fala HTTPS** com TCP, criptografia e TLS 1.3 próprios, validando certificados, e acerta o relógio pela internet.
5. Tem um **terminal completo** e um **cliente SSH** próprio, capaz de rodar `vim` e `htop` num servidor remoto.
6. Tem o **Sagui Navegador**, que abre páginas simples (Wikipédia, páginas de texto, documentação).
7. **Compila programas em C dentro do próprio sistema**, com um compilador escrito do zero.
8. Tem o **Sagui Code**: um agente de programação no terminal que conversa com modelos de IA pelo **OpenRouter**, usando ferramentas para ler, escrever, compilar e rodar programas.
9. Tem um **visual moderno**: transparência, desfoque, cantos arredondados e animações.

**Tamanho:** não há mais teto. O sistema pode crescer o que precisar, mas o tamanho é medido a cada fase e mostrado no painel, porque "leve" continua sendo uma marca do SaguiOS.

### 1.1 Fora do escopo (dizer isso no vídeo)

- **Vídeos (YouTube etc.) e sites que dependem de JavaScript** (redes sociais, e-mail na web, bancos). O navegador não terá motor de JavaScript nem decodificadores de vídeo. Isso seriam milhões de linhas.
- **Wi-Fi.** As placas sem fio dependem de firmware fechado do fabricante, o que quebraria a regra do "tudo do zero". A rede é por cabo.
- **Programas do Windows, do Linux ou do macOS.** O caminho do SaguiOS são programas nativos, criados no próprio sistema (fases 7 e 8).
- **Usar o plano do Claude ou do ChatGPT no lugar da chave de API.** Fica para depois, e só se os termos dos planos permitirem.
- **Instalação no HD.** Fica para uma fase opcional, a decidir depois da 2.0 (seção 6, fase 10).

---

## 2. Regras do desafio (acréscimos à versão 1.0)

As seções 2 e 2.1 do `prd.md` continuam valendo. Acréscimos:

### Pode

- Ler as **especificações oficiais** de: Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS, NTP (RFCs da IETF); HTTP/1.1 (RFC 9110 e 9112); TLS 1.3 (RFC 8446); SHA-2 (FIPS 180-4); HMAC e HKDF (RFC 2104 e 5869); AES (FIPS 197) e GCM (NIST SP 800-38D); ChaCha20-Poly1305 (RFC 8439); X25519 (RFC 7748); Ed25519 (RFC 8032); RSA (RFC 8017); ECDSA (FIPS 186); X.509 (RFC 5280) e DER (ITU X.690); SSH (RFC 4251 a 4254, 4344, 5647, 6668, 8709, 8731); DEFLATE, zlib e gzip (RFC 1951, 1950, 1952); PNG (especificação W3C); JPEG (ITU T.81); HTML (especificação WHATWG, só a parte de análise e a semântica dos elementos); ACPI; Intel High Definition Audio; manuais das placas de rede (Intel 8254x e o da placa do G470, se houver); classe USB CDC (ECM/NCM); a API do OpenRouter (documentação pública).
- Usar os **vetores de teste oficiais** que vêm nas especificações para conferir a criptografia e a descompactação.
- Usar na VPS, **só para testes**, ferramentas e servidores prontos: `openssl` (para gerar certificados de teste e rodar um servidor TLS local), `sshd` (servidor SSH local), servidores HTTP em Python, `curl`. Nada disso vai para dentro do sistema.
- Incluir no sistema os **certificados raiz públicos** necessários, tirados da lista de autoridades certificadoras da Mozilla (licença MPL 2.0, que permite redistribuir), com os créditos em `CREDITOS.md`. São dados públicos, não código.

### Não pode (além das regras da 1.0)

- Abrir código-fonte de **bibliotecas de criptografia e TLS** (OpenSSL, BoringSSL, LibreSSL, mbed TLS, wolfSSL, libsodium, BearSSL, TweetNaCl e semelhantes), de **pilhas de rede**, de **clientes e servidores SSH** (OpenSSH, Dropbear, PuTTY), de **navegadores e motores de HTML**, de **compiladores** (GCC, Clang, TCC, chibicc, lcc e semelhantes) e de **agentes de programação** (Claude Code, OpenCode, Aider e semelhantes). Só as especificações e a documentação pública das APIs.
- Copiar código de tutoriais de criptografia, compiladores ou redes.
- Fazer engenharia reversa de qualquer programa.

### 2.1 Autoria e licenças

- Todo código dentro da imagem continua sendo escrito neste projeto.
- Todo dado de terceiros que entrar na imagem (por enquanto, só os certificados raiz) é listado em **`CREDITOS.md`** com origem, licença e motivo.
- **Criptografia própria não é auditada.** O sistema diz isso no "Sobre" e na documentação: serve para o projeto (abrir páginas, SSH, conversar com a IA), não para banco. Mesmo assim, ela é feita com cuidado: conferida com os vetores oficiais, com contas em tempo constante onde a especificação pede e com validação de certificado de verdade (nunca "aceitar qualquer um").
- Registrar em `docs/decisoes.md` como cada parte central foi pensada (seção 2.1 do `prd.md`), com atenção especial a: gerador de números aleatórios, TLS, SSH, compilador e o laço do Sagui Code.

---

## 3. Hardware alvo e ambiente de teste

### 3.1 Máquina virtual primeiro

O Eli testa cada fase **primeiro na VM** e só depois liga o G470. A rede, em especial, é testada na VM para não depender do notebook.

| Peça | QEMU (testes automáticos) | VirtualBox (teste do Eli) |
| --- | --- | --- |
| Rede | `-nic user,model=e1000` (com DHCP e DNS do próprio QEMU) | Placa **Intel PRO/1000 MT Desktop**, modo NAT |
| Som | `-device intel-hda -device hda-duplex`, gravando a saída num `.wav` | Áudio **Intel HD Audio** |
| USB | EHCI, como na 1.0 | USB 2.0 (EHCI) |

### 3.2 Lenovo G470: premissas a confirmar na fase 1

| Peça | Premissa | Como confirmar |
| --- | --- | --- |
| Rede com cabo | Chip Ethernet no barramento PCI (provavelmente Atheros) | Comando `pci` (H35) |
| Som | Controlador Intel HD Audio, com um codec de outro fabricante | Comando `pci` e a lista de codecs do driver (H40) |
| Gerador aleatório no processador | **Não tem** (a instrução RDRAND só existe a partir da geração seguinte à do G470) | Teste do CPUID no H50 |

**Risco conhecido, avisar o Eli já na fase 1:** se o fabricante do chip de rede do G470 não publicar o manual, não dá para escrever o driver sem copiar de outro sistema, o que é proibido. Plano B: um **adaptador USB-Ethernet** que siga a classe padrão **USB CDC (ECM ou NCM)**, cuja especificação é pública, usando o driver USB que o SaguiOS já tem. O agente diz qual adaptador comprar (que funcione no modo CDC) assim que o comando `pci` mostrar o chip do G470.

### 3.3 Testes na VPS

- Igual à 1.0: QEMU sem janela, serial para log, monitor para teclas e capturas.
- **Rede:** os testes automáticos usam servidores locais na VPS (HTTP em Python, TLS com `openssl s_server` e uma autoridade certificadora de teste, `sshd` local), alcançados pela rede do QEMU. Assim os testes não dependem da internet. Um teste separado, marcado como "internet", confere sites reais quando a VPS tiver acesso.
- **Som:** o QEMU grava o que o SaguiOS tocou num arquivo `.wav`; o teste confere a frequência do som gravado.
- **IA:** os testes automáticos do Sagui Code usam um **servidor falso da API** na VPS, que responde sempre igual. Isso torna o teste repetível e não gasta créditos. Uma execução com a API real é feita uma vez por fase e registrada no diário.

---

## 4. Arquitetura (decisões fixas da 2.0)

1. **Rede no kernel, aplicações fora.** Os drivers de placa, ARP, IP, UDP e TCP ficam no kernel. DNS, HTTP, TLS, SSH e o Sagui Code são bibliotecas e programas em ring 3. Os programas usam a rede por novas chamadas de sistema de *sockets* (seção 4.1).
2. **Criptografia numa biblioteca de usuário própria** (`libsagui-cripto`), usada pelo TLS e pelo SSH. O gerador de números aleatórios fica no kernel (chamada `SIS_ALEATORIO`), alimentado por variações de tempo do hardware (contador de ciclos da CPU contra interrupções), e expandido com ChaCha20.
3. **Texto em UTF-8 nos programas novos.** A versão 1.0 usa Latin-1. Na 2.0, o terminal, o navegador e o Sagui Code trabalham em UTF-8, e a fonte ganha os caracteres necessários para o terminal remoto (desenho de caixas, setas, blocos). Os programas antigos continuam funcionando.
4. **Configurações em arquivo de texto no pendrive de dados**, em `/usb/.saguios/configuracoes.txt`, num formato próprio e simples (`chave = valor`). A chave de API fica num arquivo separado, `/usb/.saguios/chaves.txt`, que nunca entra na imagem nem no git. Sem pendrive montado, o sistema usa os valores padrão e avisa que não vai lembrar das mudanças.
5. **Compilador com duas metades.** A "frente" lê C e monta uma representação intermediária própria; o "fundo" gera código x86 e o arquivo ELF do SaguiOS. Assim, uma linguagem própria pode ganhar uma frente nova no futuro, reaproveitando o fundo.
6. **Sagui Code é um programa comum do SaguiOS**, que usa a rede, o TLS, o leitor de JSON e o compilador. As ferramentas dele são operações do próprio sistema, nunca comandos de outro sistema.

### 4.1 Chamadas de sistema novas (mínimo)

| Grupo | Chamadas |
| --- | --- |
| Energia | desligar, reiniciar |
| Som | abrir saída de som, enviar amostras, volume |
| Rede | criar socket (TCP ou UDP), conectar, enviar, receber, fechar, consultar endereço da máquina e do DNS |
| Diversas | números aleatórios, informações de hardware (PCI) |

---

## 5. Repositório e documentação

Igual à 1.0 (seção 5 do `prd.md`), com estes acréscimos:

```
prd2.md                 este documento
status-v2.md            resumo vivo da versão 2.0
CREDITOS.md             dados de terceiros dentro da imagem (certificados raiz)
dashboard/v2/           painel da versão 2.0 (o da 1.0 fica como histórico)
docs/v2/historias/      uma página por história da 2.0
docs/v2/capturas/       capturas da 2.0
docs/c-do-sagui.md      a linguagem C aceita pelo compilador do SaguiOS (manual)
docs/sagui-code.md      como o Sagui Code funciona e como configurar
userland/cripto/        libsagui-cripto
userland/compilador/    o compilador
userland/navegador/     o Sagui Navegador
userland/saguicode/     o Sagui Code
entrega/v2-faseX/       imagens de cada fase da 2.0
```

- As histórias continuam a numeração da 1.0: **H35 em diante**. Commit ao fim de cada história: `HXX: descrição`. Tag `v2-faseX` ao entregar cada fase.

---

## 6. Fases e histórias

Cada história vale o mesmo peso no progresso. **Cada fase termina numa parada obrigatória** (seção 8).

| Fase | Objetivo | Histórias |
| --- | --- | --- |
| 1. Base e Configurações | Hardware listado, desligar e reiniciar, app Configurações | H35 a H39 |
| 2. Som | Driver Intel HD Audio, sons do sistema e dos jogos | H40 a H42 |
| 3. Rede | Placa de rede, IP, DHCP, DNS, `ping` | H43 a H47 |
| 4. TCP e HTTPS | TCP, HTTP, criptografia, TLS 1.3, relógio pela internet | H48 a H54 |
| 5. Terminal e SSH | Terminal completo e cliente SSH | H55 a H58 |
| 6. Sagui Navegador | Páginas simples com texto, links e imagens | H59 a H63 |
| 7. Compilador C | Compilar programas dentro do SaguiOS | H64 a H68 |
| 8. Sagui Code | Agente de programação com IA pelo OpenRouter | H69 a H73 |
| 9. Visual moderno | Transparência, desfoque, cantos arredondados, animações | H74 a H77 |
| 10. Instalação no HD (opcional) | A decidir com o Eli depois da fase 9 | — |

### Fase 1: Base e Configurações

**H35. Comando `pci` e inventário de hardware.** Lista os dispositivos PCI com classe, fabricante e modelo (com nomes conhecidos para os mais comuns) e grava o relatório em `/usb/hardware.txt`.
*Aceite:* no QEMU aparecem a placa de rede, o controlador de som e os EHCI; no `docs/teste-v2-fase1.md` o Eli recebe o pedido de fotografar a saída no G470, que define o driver de rede (H46).

**H36. Desligar e reiniciar.** Leitura das tabelas ACPI (o suficiente para desligar), reinício pelo método ACPI com o controlador de teclado como reserva. Menu na barra de cima com **Desligar** e **Reiniciar**, perguntando antes se há pendrive montado (desmonta antes de desligar). Comandos `desligar` e `reiniciar`.
*Aceite:* o QEMU termina sozinho ao desligar; reiniciar volta ao boot; com pendrive montado, os dados ficam gravados.

**H37. Configurações guardadas.** Formato `chave = valor` em `/usb/.saguios/configuracoes.txt` (seção 4, item 4), com leitura no boot e gravação ao mudar.
*Aceite:* mudar uma configuração, reiniciar com o mesmo pendrive e ela continua igual; sem pendrive, aviso e valores padrão.

**H38. App Configurações.** Janela com abas: **Relógio** (data, hora, fuso; o automático chega na fase 4), **Aparência** (cor ou desenho do fundo, tamanho dos ícones), **Sobre o computador** (memória, processador, dispositivos). Abas novas entram nas fases seguintes (Som, Rede, IA). Ajustar o espaçamento dos ícones da área de trabalho, que ficou apertado com o Sagui 3D.
*Aceite:* mudar o relógio pelo app muda o relógio da barra e do `data`; captura de cada aba.

**H39. Remover programas.** Programas instalados ficam em `/usb/programas/` (persistentes) e aparecem na área de trabalho e no Terminal; a aba **Programas** das Configurações lista os instalados e remove com confirmação. Os programas que vêm com o sistema podem ser ocultados, não apagados.
*Aceite:* instalar um programa de teste copiando para `/usb/programas/`, ele aparece; remover pelas Configurações, ele some e o arquivo é apagado.

> **PARADA OBRIGATÓRIA: fim da fase 1.** Pedir ao Eli a foto do `pci` no G470.

### Fase 2: Som

**H40. Driver Intel HD Audio.** Controlador (comandos ao codec pelas filas da especificação), descoberta do codec e do caminho até a saída (alto-falante e fone), reprodução por DMA com lista de buffers.
*Aceite:* o QEMU grava num `.wav` uma senoide de 440 Hz tocada pelo SaguiOS; o teste mede a frequência gravada.

**H41. Som para os programas.** Chamadas de sistema de som, mistura de vários programas tocando ao mesmo tempo, volume. Aba **Som** nas Configurações.
*Aceite:* dois programas tocando juntos aparecem misturados no `.wav`; volume em 0 grava silêncio.

**H42. Tocador e sons do sistema.** Comando `tocar arquivo.wav`, sons do sistema (início, notificação, pendrive conectado) com opção de desligar, e o Sagui 3D e o Sagui Pega Banana usando o som novo quando houver placa (o bip continua como reserva).
*Aceite:* captura do `.wav` com os sons do jogo; no G470, o Eli ouve o som de início.

> **PARADA OBRIGATÓRIA: fim da fase 2.**

### Fase 3: Rede

**H43. Driver Intel PRO/1000.** Placa de rede usada pelo QEMU e pelo VirtualBox: inicialização, filas de envio e recepção, interrupções, endereço físico (MAC), estado do cabo.
*Aceite:* o teste vê pacotes indo e voltando pela rede do QEMU.

**H44. Ethernet, ARP, IPv4, ICMP e UDP.** A pilha básica no kernel e o comando `ping`.
*Aceite:* `ping 10.0.2.2` (o QEMU) responde; tempo e perda mostrados.

**H45. DHCP e DNS.** Endereço automático no boot e resolução de nomes; comando `rede` (endereço, máscara, rota, DNS) e `ping google.com`.
*Aceite:* no QEMU, o DHCP pega o endereço sozinho e um nome resolve pelo DNS do QEMU; teste "internet" com um nome real.

**H46. Rede no G470.** Driver da placa do G470, se o manual for público (decidido com a foto do H35); se não for, driver **USB CDC (ECM/NCM)** para um adaptador USB-Ethernet (seção 3.2). Registrar a decisão em `docs/decisoes.md`.
*Aceite:* no G470 (ou com o adaptador), `ping` responde; teste automático do caminho escolhido no QEMU (o QEMU tem adaptador USB de rede padrão).

**H47. Rede na interface.** Aba **Rede** nas Configurações (estado, endereço, DHCP ou manual) e ícone de rede na barra de cima.
*Aceite:* capturas com a rede ligada e com o cabo desligado.

> **PARADA OBRIGATÓRIA: fim da fase 3.**

### Fase 4: TCP e HTTPS

**H48. TCP e sockets.** Abertura e fechamento de conexão, números de sequência, confirmações, retransmissão, janela, e as chamadas de sistema de sockets para os programas.
*Aceite:* um programa do SaguiOS baixa 10 MB de um servidor da VPS sem perder nem trocar nenhum byte (conferido por hash), inclusive com perda de pacotes simulada.

**H49. HTTP e relógio pela internet.** Cliente HTTP/1.1 (com respostas em pedaços e redirecionamentos) numa biblioteca de usuário, comando `baixar <url> [arquivo]`, e NTP com a opção **Relógio automático** nas Configurações.
*Aceite:* `baixar http://...` de um servidor da VPS grava o arquivo certo no pendrive; o relógio acerta pelo NTP de teste.

**H50. Números aleatórios e resumos.** Gerador do kernel (seção 4, item 2) com teste estatístico simples; SHA-256, SHA-384, SHA-512, HMAC e HKDF.
*Aceite:* todos os vetores oficiais passam; documentação de como a aleatoriedade é coletada no G470 sem RDRAND.

**H51. Cifras e troca de chaves.** AES-128 e AES-256 em GCM, ChaCha20-Poly1305, X25519.
*Aceite:* vetores oficiais; tempo constante onde a especificação recomenda (registrar como foi feito).

**H52. Assinaturas e certificados.** Números grandes, RSA (verificação PKCS#1 v1.5 e PSS), ECDSA P-256 e P-384, leitura de certificados X.509 em DER, validação de cadeia, nome e validade.
*Aceite:* vetores oficiais; uma cadeia de teste válida passa e cinco cadeias erradas (nome errado, vencida, assinatura falsa, autoridade desconhecida, cadeia quebrada) são recusadas com a mensagem certa.

**H53. TLS 1.3.** Cliente completo (troca de chaves X25519, cifras da H51, verificação do certificado pela H52, alertas), usado pelo `baixar https://...`.
*Aceite:* conecta no servidor TLS de teste da VPS; recusa o servidor com certificado errado; teste "internet" com sites reais.

**H54. Certificados raiz e créditos.** Os poucos certificados raiz necessários para os sites de aceite e para o OpenRouter, em `/sistema/certificados/`, com `CREDITOS.md`; comando para ver a lista.
*Aceite:* `baixar https://pt.wikipedia.org/` funciona (teste "internet"); `CREDITOS.md` lista cada certificado com origem e licença.

> **PARADA OBRIGATÓRIA: fim da fase 4.**

### Fase 5: Terminal e SSH

**H55. Terminal completo.** Emulação das sequências de terminal usadas por programas de servidor (cursor, apagar, cores de 16 e 256, regiões de rolagem, tela alternativa, títulos), UTF-8 e fonte com desenho de caixas. Abas e copiar e colar no Terminal.
*Aceite:* um arquivo de teste com as sequências desenha a tela esperada (comparação com captura de referência).

**H56. Transporte SSH.** Troca de chaves `curve25519-sha256`, chave do servidor `ssh-ed25519` (com SHA-512), cifra `aes128-ctr` com `hmac-sha2-256` ou `aes256-gcm`, e o arquivo de servidores conhecidos (aviso na primeira conexão e alerta forte se a chave mudar).
*Aceite:* conecta no `sshd` local da VPS; recusa conexão quando a chave do servidor muda.

**H57. Login e sessão.** Autenticação por senha e por chave Ed25519 (comando `ssh-chave` gera o par no pendrive), terminal remoto com tamanho da janela, `exit` limpo.
*Aceite:* login das duas formas; redimensionar a janela muda o tamanho no servidor.

**H58. SSH de verdade.** Comando `ssh usuario@servidor`.
*Aceite:* no teste automático, `htop` e `vim` rodam no servidor de teste e as capturas mostram a tela certa; no roteiro, o Eli entra num servidor dele.

> **PARADA OBRIGATÓRIA: fim da fase 5.**

### Fase 6: Sagui Navegador

**H59. Descompactação e PNG.** DEFLATE, zlib e gzip, e o decodificador de PNG (todas as profundidades de cor e transparência).
*Aceite:* conjunto de PNGs de teste gerados na VPS decodificados iguais pixel a pixel.

**H60. JPEG.** Decodificador JPEG (sequencial e progressivo).
*Aceite:* JPEGs de teste comparados com a referência dentro da tolerância da especificação.

**H61. Leitor de HTML.** Leitura tolerante a erros, árvore de elementos, entidades, conjunto de caracteres, links e formulários simples (só leitura de busca, como a da Wikipédia).
*Aceite:* páginas de teste com HTML mal formado montam a árvore esperada.

**H62. Janela do navegador.** Barra de endereço, voltar e avançar, favoritos, rolagem, layout em blocos (títulos, parágrafos, listas, citações, tabelas simples, imagens), cores básicas do HTML, `display: none` e pouco mais de CSS (registrar o que for suportado).
*Aceite:* páginas de teste servidas pela VPS renderizadas como as capturas de referência.

**H63. Sites de aceite.** Abrir, com texto legível, links e imagens: um artigo da **Wikipédia em português**, `example.com`, uma página de documentação simples e o `README.md` do SaguiOS no GitHub (versão em texto).
*Aceite:* teste "internet" com capturas de cada site; roteiro para o Eli na VM e no G470.

> **PARADA OBRIGATÓRIA: fim da fase 6.**

### Fase 7: Compilador C do SaguiOS

**H64. A linguagem.** Definir em `docs/c-do-sagui.md` o C aceito: tipos inteiros, `char`, ponteiros, vetores, `struct`, `union`, `enum`, `typedef`, funções, laços, `switch`, `#include`, `#define` simples, e a `libsagui`. Sem ponto flutuante na primeira versão (como os programas atuais). Esse documento também é o manual que o Sagui Code manda para a IA.
*Aceite:* manual revisado com exemplos; lista do que fica de fora.

**H65. Frente do compilador.** Pré-processador, leitura, verificação de tipos e mensagens de erro **em português, com linha, coluna e uma dica** (pensadas para a IA conseguir se corrigir sozinha).
*Aceite:* 50 programas de teste com erros dão as mensagens esperadas; 50 programas válidos passam.

**H66. Fundo do compilador.** Representação intermediária própria, geração de código x86 de 32 bits, montador e ligador próprios, ELF do SaguiOS ligado à `libsagui`.
*Aceite:* os 50 programas válidos rodam no SaguiOS com a saída esperada.

**H67. Compilar dentro do sistema.** Comando `cc arquivo.c -o programa`, botão **Compilar e rodar** no Editor e instalação do resultado em `/usb/programas/`.
*Aceite:* no QEMU, digitar um programa no Editor, compilar e rodar sem sair do SaguiOS.

**H68. Prova de fogo.** Compilar **dentro do SaguiOS** programas da 1.0 (pelo menos `wc`, `cat`, `calculadora` e `banana`) e comparar o comportamento com as versões feitas pelo GCC.
*Aceite:* os programas compilados pelo SaguiOS passam nos mesmos testes da 1.0.

> **PARADA OBRIGATÓRIA: fim da fase 7.**

### Fase 8: Sagui Code

**H69. JSON e cliente do OpenRouter.** Leitor e escritor de JSON e cliente da API de conversa do OpenRouter (com chamada de ferramentas), por HTTPS. Aba **IA** nas Configurações: chave (guardada em `/usb/.saguios/chaves.txt`), modelo e limite de gasto por sessão.
*Aceite:* com o servidor falso da VPS, uma pergunta e uma resposta; com a API real, uma conversa registrada no diário.

**H70. O laço do agente.** Ferramentas: `ler_arquivo`, `escrever_arquivo`, `editar_arquivo` (trocar um trecho), `listar_pasta`, `executar_comando`, `compilar` e `rodar`. Confirmação do usuário antes de sobrescrever, apagar ou rodar comandos; limite de passos por pedido.
*Aceite:* com o servidor falso, o laço completo (escrever, compilar com erro, corrigir, compilar, rodar) funciona e cada pedido de confirmação aparece.

**H71. Instruções e contexto.** Texto de instruções do Sagui Code, manual da `libsagui` e de `docs/c-do-sagui.md` enviados na conversa, uso de cache de prompt quando o modelo escolhido suportar, e corte ou resumo das partes antigas quando a conversa crescer.
*Aceite:* conversa longa de teste não passa do limite configurado; o gasto estimado aparece ao fim de cada pedido.

**H72. Interface.** `saguicode` no Terminal: resposta aparecendo enquanto é gerada, histórico, salvar e abrir conversas no pendrive, comandos `/modelo`, `/gasto`, `/limpar`.
*Aceite:* capturas de uma sessão completa.

**H73. Aceite com IA de verdade.** Cinco pedidos de referência (por exemplo: relógio analógico, conversor de temperatura, lista de tarefas, jogo da velha, visualizador de imagens do pendrive) feitos com a API real e um modelo escolhido pelo Eli. Registrar quantos deram certo de primeira, quantos precisaram de correção e o gasto.
*Aceite:* pelo menos 4 de 5 terminam com o programa rodando; capturas e números no diário.

> **PARADA OBRIGATÓRIA: fim da fase 8.**

### Fase 9: Visual moderno

**H74. Vidro.** Janelas e barras com transparência e desfoque do que está atrás, e sombras. O desfoque é recalculado só onde a tela mudou.
*Aceite:* capturas; tempo de quadro medido no QEMU e no G470.

**H75. Forma.** Cantos arredondados, barra de cima nova (menu, relógio, rede, som, energia) e uma barra de aplicativos com os programas abertos e fixados.
*Aceite:* capturas; clicar na barra alterna entre os programas.

**H76. Movimento e tema.** Animações de abrir, fechar e minimizar, ícones redesenhados e tema claro ou escuro nas Configurações.
*Aceite:* capturas dos dois temas.

**H77. Leve no G470.** Opção **Efeitos leves** (sem desfoque) e escolha automática se o computador for lento.
*Aceite:* no G470, abrir e arrastar janelas fica fluido (o Eli confirma no teste).

> **PARADA OBRIGATÓRIA: fim da fase 9 e da versão 2.0.**

### Fase 10 (opcional, a decidir depois): Instalação no HD

Só depois da 2.0 e com o Eli decidindo. Exige driver SATA (AHCI) e regras de segurança rígidas: o instalador só grava num disco ou partição **que o Eli escolher e confirmar digitando o nome**, nunca por padrão, e nunca toca no HD do G470 sem essa confirmação (lá estão os dados dele, em ext3).

### Futuro

- **Linguagem própria em português**, como uma frente nova do compilador (seção 4, item 5). Boa candidata para o Fable 5.2, que pode desenhar a linguagem e escrever um manual muito bom para outras IAs.
- **Outros provedores no Sagui Code:** Anthropic direto e OpenAI, além do OpenRouter.

---

## 7. Testes automáticos

Igual à seção 7 do `prd.md`, com:

- Testes da 2.0 em `tests/` com nomes `h35_...` em diante, rodando no `make test` junto com os da 1.0.
- **Os 37 testes da 1.0 continuam passando** ao fim de cada fase.
- Testes que precisam de internet de verdade ficam marcados e rodam separados (`make test-internet`).
- Tempo máximo por teste: 5 minutos (a criptografia sem KVM é lenta).

---

## 8. Testes com o Eli

Igual à seção 8 do `prd.md`, com:

- Imagens em `entrega/v2-faseX/` e roteiro em `docs/teste-v2-faseX.md`, sempre **VM primeiro** (com a configuração da seção 3.1), G470 depois.
- O Eli responde **"v2 fase X ok"**, e o agente roda `SAGUI_PROJETO=v2 tools/confirmar-hardware.sh "v2 fase X ok"`.
- Fases com rede: o roteiro explica como ligar o cabo no G470 (ou o adaptador USB do plano B).
- Fase 8: o roteiro explica como criar a chave do OpenRouter, colocar crédito e onde guardar a chave no pendrive. **A chave nunca aparece em captura, log, commit ou mensagem.**

---

## 9. Painel

- `dashboard/v2/index.html`, alimentado por `dashboard/v2/status.json` (gerado por `SAGUI_PROJETO=v2 tools/status.py`).
- Mostra: progresso geral e por fase, histórias, **tabela de capacidades** (o que o SaguiOS 2.0 sabe fazer, marcado conforme fica pronto), últimas ações, erros, capturas, linhas de código e **tamanho da imagem sem teto**, com a evolução fase a fase.
- Link para o painel da 1.0.

---

## 10. Modo autônomo, modelo e economia

- Igual à seção 10 do `prd.md`.
- **Modelo:** recomenda-se o **Claude Fable 5.2** para a versão 2.0 inteira. As fases mais exigentes, onde ele faz mais diferença, são a 4 (criptografia e TLS), a 5 (SSH), a 7 (compilador) e a 8 (Sagui Code).
- Antes de mudanças grandes no kernel (rede, som, compositor), criar uma tag no git.
- Quando um erro se repetir mais de 3 vezes com a mesma abordagem, parar, registrar e trocar de estratégia.

---

## 11. Entregáveis

1. `entrega/v2-fase1/` a `entrega/v2-fase9/` e `entrega/v2-final/`, com `.img`, `.vdi` e SHA-256.
2. `CREDITOS.md`, `docs/c-do-sagui.md`, `docs/sagui-code.md`.
3. `docs/resumo-v2.md`: o que foi feito, tempo e ciclos de correção por fase, linhas de código, tamanho da imagem por fase, o que funcionou no G470 e o que ficou de fora.
4. README atualizado com as capturas da 2.0.

---

## 12. Definição de sucesso

| Nível | O que acontece |
| --- | --- |
| Bom | Desliga pelo menu, Configurações guardadas, som tocando |
| Muito bom | `ping google.com` respondendo na VM e no G470 |
| Ótimo | HTTPS próprio abrindo a Wikipédia; SSH num servidor de verdade |
| Excelente | Programas em C compilados dentro do próprio SaguiOS |
| Completo | O Sagui Code cria, compila, corrige e roda programas pedidos em português |
| Bônus | Visual moderno fluido no G470 |
