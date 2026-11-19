# Histórico de versões do SaguiOS

Este arquivo conta a história do SaguiOS, versão por versão. Ele também vai
dentro do sistema: é o que aparece no botão "Histórico" da janela "Sobre o
SaguiOS" (`/sistema/ajuda/versoes.md`).

Cada mudança nova entra primeiro na seção "Em desenvolvimento"; quando uma
versão é lançada, o que estava lá vira a seção dessa versão.

## As três eras

1. **1.0: criado inteiramente por IA.** Um sistema operacional escrito do
   zero pelo Claude (Anthropic), sob a direção do Eli Rigobeli, sem código de
   outros sistemas e sem bibliotecas prontas.
2. **2.x: a comunidade chega.** O projeto passa a aceitar contribuições da
   comunidade e, nos aplicativos, bibliotecas de terceiros, desde que livres,
   com licença compatível com a GPL 3 e com crédito. O núcleo continua do
   zero.
3. **3.0: 64 bits e evolução contínua.** O kernel passa a rodar em
   64 bits (x86-64), com os programas de antes funcionando sem mudança, e o
   projeto passa a seguir o versionamento MAIOR.MENOR.CORREÇÃO, crescendo com
   a comunidade.

## Em desenvolvimento

Nada ainda: o que entrar no `main` depois da 3.0.0 aparece aqui.

## Versões lançadas

### 3.0.0 (novembro de 2026): 64 bits

- O **kernel roda em 64 bits** (x86-64, modo longo): o estágio 2 confere se o
  processador tem 64 bits (e avisa na tela se não tiver), o kernel tem GDT,
  IDT e TSS de 64 bits e paginação de 4 níveis (D076 a D078).
- Os **programas de antes rodam sem mudança**, em 32 bits, no modo de
  compatibilidade do processador (D079). Por fora, tudo funciona como na 2.2.
- A janela "Sobre o SaguiOS" mostra **x86-64, 64 bits**.
- Testada no Lenovo G470: boot, teclado, touchpad, janelas, pendrive de
  dados, rede, som, tocador e vídeo.
- Tem tudo o que entrou na 2.2.0, abaixo.

### 2.2.0 (novembro de 2026): a última de 32 bits

Lançada junto com a 3.0.0, para PCs sem 64 bits. É o mesmo sistema da 3.0.0
com o kernel de 32 bits (`make ARQ=32`); os programas são os mesmos. Foi
testada no QEMU (a suíte inteira) e não no G470, onde quem foi testada foi a
3.0.0. Correções que valham para os 32 bits podem sair como 2.2.x.

- **Teclado espanhol** e troca do layout do teclado nas Configurações (H90,
  D051), do fork SaguiOS-Extended de Alejandro Anuncibay.
- **Comandos de texto ao estilo Unix** (H91), também do fork do Alejandro:
  `head`, `tail`, `sort`, `uniq`, `cut`, `tr`, `seq`, `rev`, `tac`, `nl`,
  `diff`, `find`, `sed`, `tree`, `cal`, `bc`, `expr`, `base64`, `md5sum`, `od`
  e `xxd`, cada um com o nome do Unix e um apelido em português.
- **Compactação** (H93, D052): `gzip`/`gunzip`, `tar` e `zip`/`unzip`, com um
  DEFLATE escrito aqui, na `libsagui`; a ideia veio do fork do Alejandro.
- **A janela "Sobre o SaguiOS" nova** (H94): versão, arquitetura, memória e
  créditos; os botões "Créditos e licenças" (todos os textos de licença dentro
  da imagem, em `/sistema/licencas`) e "Histórico" (este arquivo).
- **Jogos com tema de banana** (H92, D053), do fork do Alejandro:
  Caça-Bananas, Paciência Banana, um emulador de CHIP-8 e o visor de
  atividades JClic, num menu "Jogos".
- Do Robson de Lima Barbosa: **encaixe das janelas** ao arrastar para a borda
  e atalhos de teclado; redimensionar sem a janela embaralhar enquanto o
  programa redesenha; o Cipó sem decodificar de novo as imagens quando a
  página muda; a **doca com cantos redondos** e ícones novos dos programas,
  num estilo só.
- Correções do teste no G470: **Ctrl+V cola a chave da IA** nas
  Configurações; o Editor só mostra "Compilar e rodar" e "Instalar" para
  programas em C; o menu do Arquivos não repete "Abrir no Editor".
- **Ícones desenhados em código** (D055), no estilo do Robson: todos os
  programas com o mesmo fundo de cantos redondos, centrados, e o build gera
  os tamanhos 64 e 40 sem depender de imagem pronta.
- Também do Robson (issue #25): **menu Iniciar** com todos os programas,
  busca sem acento e categorias (D056); **laço de seleção** na área de
  trabalho (D057); no Arquivos, **vários marcados**, "Novo" no menu e
  renomear na própria linha (D058); o **Visualizador de imagens** (D059);
  **captura de tela** com a tecla Print Screen (D060); a **Lixeira**, com
  restaurar e esvaziar (D061); e **cantos arredondados** com borda suavizada
  nas janelas e nos menus.
- Menu Iniciar em telas de 640x480, com páginas ("<" e ">"); o ícone da
  Lixeira fica cheio também quando só a lixeira do pendrive tem coisas; o
  kernel não escreve mais além da memória da janela quando um programa
  muda as opções de uma janela maior que a tela.
- Renomear no lugar: um segundo clique, devagar, no nome de um ícone ou de um
  arquivo abre a edição ali mesmo (Robson). A interface ficou mais leve: o
  menu redesenha só o que muda, e o vidro e as sombras custam menos.
- Quatro comandos novos de Gabriel: `dirname` (`pasta-de`), `hostname`
  (`nome-maquina`), `du` (`disco-usado`) e `clear` (`limpar`), o apelido
  `exit` para o comando interno `sair`, `uptime` (`tempo-ligado`) e o
  apelido `reboot` para `reiniciar`.
- O kernel enxerga os **discos rígidos IDE/ATA** (`hd0`, `hd1`...), sem
  montar nenhum (D062). É a base do instalador (issue #45).
- **Gravar direto no disco fica travado no kernel** (D063): só depois que a
  pessoa digita o nome do disco numa caixa desenhada pelo próprio kernel,
  que mostra o disco e o que o programa diz que vai fazer.
- **Instalador do SaguiOS** (D064): um assistente que copia o sistema do
  pendrive para um disco, por enquanto apagando o disco inteiro, e cria a
  partição dos seus arquivos. Ligado pelo disco, ela monta sozinha.
- O instalador e as unidades (pendrives e o Disco) ficam **no canto de cima
  à direita da área de trabalho**, como no Linux, e o instalador entra no
  menu Iniciar; depois de instalado, ele some dos dois (D065).
- O kernel lê e confere a **tabela de partições** de cada disco: MBR com as
  partições lógicas, GPT, disco formatado sem tabela, entradas sobrepostas
  ou passando do fim. A caixa de confirmação e o instalador mostram o que
  ele leu (D066).
- No **Sagui Code** (janela), o texto da conversa se seleciona arrastando o
  mouse, com destaque azul, e **Ctrl+C copia** o trecho; depois de um clique
  na conversa, Ctrl+A seleciona tudo. A caixa do pedido continua com o seu
  próprio Ctrl+C/Ctrl+V (H123).
- **SaguiOS em espanhol e em inglês** (primeira parte, D073): a aba "Idioma e
  teclado" das Configurações escolhe a língua (`sistema.idioma`); a `ajuda` e
  as mensagens do shell e as próprias Configurações já saem traduzidas. As
  traduções são arquivos de texto em `/sistema/idiomas`, fáceis de completar.
  A ideia veio do fork SaguiOS-Extended de Alejandro Anuncibay.
- O **Tocador não trava mais a tela** ao abrir uma música do pendrive: lê o
  arquivo aos poucos enquanto toca, e ao trocar de música o som da anterior
  para na hora, sem ficar repetindo um pedaço (issue #27, D070). Ler do
  pendrive ficou mais rápido quando o arquivo está em clusters seguidos.
- **Tocador de vídeo** (D071, issues #28 e #29): a imagem segue o relógio do
  som e, quando a máquina não dá conta, descarta os quadros que pode (o som
  não para mais nem o vídeo fica em câmera lenta por causa deles); a cor é
  convertida com SSE2; e há controles: tocar/pausar, tempo, barra de
  progresso que pula ao clicar, volume, maximizar e **tela cheia** (F, F11
  ou duplo clique; Esc sai).

### 2.1 (novembro de 2026): a primeira com a comunidade

A primeira versão com contribuições da comunidade, trazidas para o código
atual com os autores nos commits (`CONTRIBUIDORES.md`). Com ela as regras
mudaram (D041): **o núcleo continua sempre do zero** (kernel, drivers, rede,
TLS, janelas, `libsagui`, shell, compilador e os programas básicos), mas os
**aplicativos podem usar código livre de terceiros**, com licença compatível
com a GPL 3, numa pasta própria em `terceiros/` e com crédito.

- **Cipó**, o navegador: CSS moderno, JavaScript com DOM, fontes TrueType,
  WOFF e WOFF2, Canvas, SVG, WebGL, imagens GIF e WebP e vídeo H.264/AAC
  (motor de Robson de Lima Barbosa; o nome veio de M4R4G0N).
- **Visual novo**: ícones suavizados, letras suavizadas, fundos "Com sol" e
  "Mata Atlântica", menus do botão direito e barra de tarefas (Robson); o
  tema "Azul" (Gabriel).
- `poweroff` e a tela "pode desligar" (Clayton); `top`, `grep`, `fetch`,
  `uname`, `df` e duas demonstrações (Gabriel).
- **Tocador** de músicas, com um decodificador de **MP3** escrito do zero pela
  norma (D047); vídeo H.264 no perfil High (D048); copiar e colar entre os
  programas.
- **Integração contínua** no GitHub Actions, de DevLJSP (pull request #10):
  todo pull request é compilado e testado.
- Para o Lenovo G470: **rede por um adaptador USB** (CDC Ethernet), vídeo na
  resolução nativa do painel (opcional) e correções do som.
- Ponto flutuante nos programas, Ctrl+C no Terminal e o **Sagui Code** numa
  janela própria.

### 2.0 (novembro de 2026): o SaguiOS conectado

Ainda escrita inteiramente por IA, seguindo o `prd2.md`, em nove fases:

- Configurações, desligar e reiniciar pelo ACPI e a lista do hardware.
- **Som** pela placa Intel HD Audio, com os sons do sistema e dos jogos.
- **Rede**: placa de rede, IP, DHCP, DNS e `ping`; depois TCP, HTTP e o
  relógio acertado pela internet.
- **Criptografia e HTTPS** com TLS 1.3, tudo escrito aqui.
- Terminal completo e um cliente **SSH**.
- O primeiro **navegador**, com texto, links e imagens PNG e JPEG.
- Um **compilador de C** que roda dentro do próprio SaguiOS.
- O **Sagui Code**, um agente de programação com IA.
- **Visual moderno**: transparência, sombras, doca, temas, janelas que mudam
  de tamanho.

### 1.0 (novembro de 2026): criado inteiramente por IA

Escrita do zero pelo Claude (Anthropic) para o projeto do Eli Rigobeli,
seguindo o `prd.md`: sem código de outros sistemas, sem GRUB e sem
bibliotecas prontas. Cada fase foi testada pelo Eli num Lenovo G470 de 2011.

1. **Boot**: carregador próprio em dois estágios, modo protegido e o kernel em
   C.
2. **Kernel e terminal**: interrupções, relógio, memória, teclado ABNT2 com
   acentos e o sistema de arquivos SGFS.
3. **Processos e programas**: paginação, multitarefa, programas no anel 3,
   chamadas de sistema, pipes e o shell `sagsh`.
4. **Gráfico e mouse**: vídeo VESA, uma fonte de letras própria e o touchpad.
5. **Janelas e programas**: área de trabalho, Arquivos, Terminal, Editor,
   Calculadora e o jogo Sagui Pega Banana.
6. **Pendrives**: USB 2.0 (EHCI) com hubs e conexão a quente, e FAT32 com
   leitura e gravação.
7. **Bônus**: o bip pelo alto-falante e o **Sagui 3D**, um jogo em primeira
   pessoa feito do zero.

A imagem de boot da 1.0 tinha 575.488 bytes.

## Como os números das versões funcionam a partir da 3.0

A partir da 3.0, o SaguiOS segue o versionamento **MAIOR.MENOR.CORREÇÃO**
(o "versionamento semântico"):

- só **correções** de erros: muda o último número (3.0.0 vira 3.0.1);
- **coisas novas** (um programa, um comando, um driver): muda o número do
  meio (3.0.1 vira 3.1.0);
- mudanças na **base** do sistema (o que pode fazer programas antigos pararem
  de funcionar): muda o primeiro número (3.4.2 vira 4.0.0).

Os números não têm limite: depois da 3.9 vem a 3.10, e pode haver uma 3.0.12.
As versões são lançadas de tempos em tempos, cada uma juntando muitas
mudanças; o que já está no código mas ainda não foi lançado fica na seção
"Em desenvolvimento" deste arquivo.
