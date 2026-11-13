# Decisões do SaguiOS

Toda decisão que o PRD não fixa, com o motivo. Também registra como cada parte
central foi pensada (prova de autoria, PRD seção 2.1).

## D001. Compilador: GCC do sistema com `-m32` freestanding (H01)

**Decisão:** usar o GCC 13 do Ubuntu com `-m32 -ffreestanding -nostdlib -fno-pic
-fno-stack-protector -mgeneral-regs-only`, e o `ld` com `-m elf_i386`, em vez de
compilar um `i686-elf-gcc`.

**Motivo:** o PRD aceita as duas opções. O GCC do sistema já está instalado,
gera código i386 correto com essas flags e evita uma hora de compilação de
binutils + GCC. O risco conhecido (o compilador assumir coisas do Linux) é
coberto pelas flags: sem libc, sem PIC, sem proteção de pilha, sem SSE/x87
(`-mgeneral-regs-only` garante que o kernel nunca toca a FPU, o que combina com a
decisão do PRD de não usar ponto flutuante).

## D002. Painel lido por `status.js`, não só por `status.json` (H01)

**Decisão:** `tools/status.py` grava `dashboard/status.json` (fonte única) e uma
cópia `dashboard/status.js` (`window.SAGUI_STATUS = {...}`).

**Motivo:** o Eli abre o painel direto do disco (baixado por FTP). Navegadores
bloqueiam `fetch()` de arquivos locais (`file://`), mas carregam `<script src>`.
O painel tenta o `.js` e, se não houver, cai para `fetch("status.json")`.

## D003. Carregador de boot: como foi pensado (H02, H03)

**Problema.** A BIOS só traz 512 bytes para a memória, em modo real, e só a
BIOS sabe ler o pendrive. O kernel precisa estar acima de 1 MB e rodar em modo
protegido, onde a BIOS não existe mais.

**Divisão em dois estágios.** O MBR faz o mínimo que cabe em 356 bytes de
código: guarda o disco (DL), lê um *cabeçalho de boot* no LBA 1 e, com ele, o
estágio 2. Todo o resto vai para o estágio 2, que pode crescer até 32 KB. O
cabeçalho é a única posição fixa; os demais LBAs, tamanhos e endereços são
calculados pelo `tools/mkimagem.py`. Assim mexer no kernel nunca exige mexer
no MBR.

**Formato do MBR.** Os primeiros 90 bytes imitam o início de um setor com BPB
(salto curto + NOP + área zerada). Motivo: algumas BIOS, ao dar boot por USB,
"corrigem" a geometria escrevendo nessa área; se houvesse código ali, ele
seria corrompido. A tabela de partições tem uma única partição ativa (0x80)
cobrindo a imagem a partir do LBA 1, porque várias BIOS só tratam um pendrive
como disco rígido (e não como disquete) quando encontram uma partição ativa
válida. O tipo escolhido é 0xDA ("dados sem sistema de arquivos"), para que
nenhum sistema tente montar o pendrive de boot.

**Leitura por LBA.** Só `INT 13h AH=42h`. CHS seria mais compatível com BIOS
muito antigas, mas o G470 (2011) e as VMs têm as extensões, e CHS dependeria
da geometria inventada pela BIOS para o pendrive, uma fonte clássica de
problemas. O MBR verifica as extensões (AH=41h) e avisa na tela se faltarem.
Cada leitura tem 3 tentativas com reset do disco entre elas.

**Acima de 1 MB: unreal mode.** O estágio 2 lê blocos de 32 KB num buffer em
0x10000 e copia cada um para o destino final. Para copiar acima de 1 MB sem
sair do modo real (precisamos da BIOS para o próximo bloco), entramos no modo
protegido só pelo tempo de carregar DS e ES com um descritor de 4 GB e
voltamos. O processador mantém o limite em cache. Isso é refeito antes de cada
cópia, porque uma rotina da BIOS pode ter recarregado os segmentos. A outra
opção (carregar tudo abaixo de 0x9F000) limitaria kernel + ramdisk a ~570 KB,
e a fase 5 vai passar disso.

**Onde fica o ramdisk.** Logo depois do fim da BSS do kernel, alinhado em
4 KB. O build lê o símbolo `_kernel_fim` do ELF. Se o ramdisk ficasse logo
depois do *arquivo* do kernel, o kernel apagaria o começo do ramdisk ao zerar
a própria BSS.

**Integridade.** O cabeçalho guarda uma soma simples dos bytes do kernel e do
ramdisk; o estágio 2 confere antes de saltar. No G470 não há serial: um
"dados lidos estão corrompidos" na tela é muito mais útil que uma tela preta.

**Progresso na tela.** `[1/6]` a `[6/6]`, como pede a seção 8 do PRD.

**A20.** Primeiro verifica se já está ligada (comparando 0x000500 com
0x100500); depois tenta, em ordem, o serviço da BIOS, a porta 0x92 e o
controlador de teclado, conferindo após cada um. O método usado vai na
boot_info e aparece na tela do kernel, para sabermos qual funcionou no G470.

**VESA.** O estágio 2 já percorre a lista de modos e escolhe o melhor
(1024x768 > 800x600 > 640x480; 32 bits > 24 bits; exige framebuffer linear e
cor direta), mas só troca de modo se o bit `OPC_VESA` do cabeçalho estiver
ligado, o que o build fará a partir da fase 4. A troca é a última coisa antes
do modo protegido, porque depois dela o texto da BIOS some.

## D004. Tamanho dos arquivos de imagem (H04)

**Decisão:** o `saguios.img` tem no mínimo 1 MB (e depois cresce em múltiplos
de 32 KB); o disco do `saguios.vdi` é arredondado para múltiplos de 1 MB.

**Motivo:** a primeira imagem tinha 32 KB. BIOS e VirtualBox inventam uma
geometria (cilindros, cabeças, setores) a partir do tamanho do disco, e um
disco menor que um cilindro é um caso pouco testado neles. 1 MB evita isso e
continua abaixo da meta de 1,44 MB. A meta do PRD é medida pelos **bytes
úteis** (MBR + cabeçalho + estágio 2 + kernel + ramdisk), que o build grava em
`build/tamanho.txt` e o painel mostra.

## D005. `.vdi` gerado pelo `qemu-img`, testado no QEMU (H04)

**Decisão:** o `.vdi` é convertido com `qemu-img convert -f raw -O vdi`, como
pede o PRD, e o teste da H04 dá boot no próprio `.vdi` pelo QEMU
(`-drive format=vdi`).

**Motivo:** a VPS não tem virtualização aninhada, então não há VirtualBox
para testar aqui. Dar boot no `.vdi` prova que a conversão preservou a
imagem byte a byte; o teste no VirtualBox de verdade fica no roteiro do Eli.

## D006. Autotestes ligados pelo cabeçalho de boot (fase 2)

**Decisão:** o cabeçalho de boot tem um campo de 16 bytes (`CAB_TESTE`) com o
nome de um autoteste. O estágio 2 copia para a boot_info e o kernel, se o
campo não estiver vazio, roda `autoteste_executar(nome)` depois de iniciar.
Os testes geram imagens próprias com `mkimagem.py --teste NOME`.

**Motivo:** critérios como "dividir por zero mostra a tela de erro" ou
"alocar e liberar sem corromper" precisam de código que nunca deve rodar na
imagem do Eli. Recompilar o kernel para cada teste seria lento e testaria um
binário diferente do entregue. Assim o kernel testado é exatamente o mesmo;
só muda um campo do cabeçalho.

## D007. Interrupções: como foi pensado (H05)

**Um trecho por vetor, gerado por repetição.** A CPU não informa qual vetor
disparou, e só algumas exceções empilham código de erro. Cada um dos 256
vetores tem um trecho de 2-3 instruções, gerado por `%rep` no NASM, que iguala
o formato (empilha 0 quando não há código de erro) e empilha o número do
vetor. Um único trecho comum salva o resto. A tabela com o endereço de cada
trecho é exportada para o C, que monta a IDT num laço.

**O despachante devolve o quadro a retomar.** `tratar_interrupcao(quadro)`
retorna um ponteiro de quadro, e o assembly faz `mov esp, eax` antes de
restaurar os registradores. Hoje sempre devolve o mesmo quadro. Na fase 3, a
troca de processo vai ser só o escalonador devolver o quadro de outro
processo: não precisa de uma rotina separada de troca de contexto.

**Portas de interrupção, não de "trap".** IF é desligada na entrada, então o
kernel não é interrompido no meio de outra interrupção. É mais simples de
raciocinar e o kernel é pequeno o bastante para isso não atrasar nada.

**PIC.** Remapeado para 0x20-0x2F (os vetores 8-15 originais colidem com
exceções da CPU), tudo mascarado, cada driver habilita a sua linha. IRQ 7 e 15
espúrias são reconhecidas pelo registrador de serviço e não recebem EOI.

**Tela de erro.** Nome da exceção em português, código de erro, todos os
registradores e CR0/CR2/CR3, e um pedido para fotografar: no G470 a foto é a
única ferramenta de depuração.

**Texto em Latin-1 também no modo texto.** O kernel é compilado com
`-fexec-charset=ISO-8859-1`, então as mensagens podem ter acentos. A fonte
da placa VGA segue a página de código 437; o driver traduz o que existe nela
(ç, é, á, ó, ...) e troca pela letra sem acento o que não existe (ã, õ).

## D008. Memória física e alocador do kernel: como foi pensado (H06)

**Até onde o kernel gerencia.** O kernel é de 32 bits sem PAE, então nunca
enxerga memória acima de 4 GB (no G470, com ~8 GB, metade fica de fora; a
tela mostra os dois números). Indo além: na fase 3, com a paginação, os
programas vão morar a partir de 0x40000000 (PRD 4), e o kernel vai ficar
mapeado 1:1 (endereço virtual = físico) abaixo disso em todos os espaços de
endereço, para poder ler e escrever qualquer página física sem truques. A
faixa 0x38000000-0x3FFFFFFF fica reservada para o kernel mapear coisas que
estão em endereços físicos altos (framebuffer de vídeo, registradores do
USB). Por isso o gerenciador de páginas só entrega memória abaixo de
**0x38000000 (896 MB)**. É muito mais do que o SaguiOS precisa, e em troca
o kernel nunca precisa de mapeamento temporário para tocar numa página.

**Bitmap de páginas de 4 KB.** Um bit por página, 1 = ocupada. Começa tudo
ocupado; liberamos só as regiões "livres" do E820 (arredondadas para dentro,
para nunca entregar meia página de uma região reservada); depois marcamos de
novo como ocupados o primeiro 1 MB (BIOS, tabelas, nossas estruturas de
boot), o kernel com a BSS e o ramdisk. 896 MB / 4 KB = 229.376 bits = 28 KB
de bitmap, que fica na BSS. A busca olha 32 páginas de uma vez (uma palavra
inteira cheia = nada livre ali) e continua de onde parou da última vez.

**`kmalloc` em duas faixas.**
- *Pequenos (até 2 KB):* oito classes de tamanho em potência de 2 (16 a
  2048 bytes, cabeçalho incluído). Cada classe tem uma lista de blocos
  livres; quando esvazia, o alocador pega uma página e a corta em blocos
  daquela classe. Liberar devolve o bloco à lista da classe, em tempo
  constante. As páginas das classes não voltam para o bitmap: a memória de
  estruturas pequenas do kernel tende a ser reutilizada pela mesma classe.
- *Grandes (mais de 2 KB):* páginas contíguas direto do bitmap, devolvidas ao
  bitmap no `kfree`.

Cada bloco tem um cabeçalho de 8 bytes com uma assinatura e a classe (ou o
número de páginas). O `kfree` confere a assinatura e troca por outra ao
liberar: liberar duas vezes ou liberar um ponteiro que não veio do `kmalloc`
é detectado e registrado, em vez de corromper as listas em silêncio.

Outras opções pensadas: lista única de blocos com divisão e junção de
vizinhos (gasta menos memória, mas cada `kfree` precisa percorrer a lista e
um erro corrompe tudo) e alocador por "buddy" (mais complexo do que um
kernel deste tamanho precisa).

## D009. Teclado ABNT2: como foi pensado (H07)

**Do byte à tecla.** O driver só lê a porta 0x60 na IRQ 1 e entrega bytes a
uma máquina de estados pequena: prefixo 0xE0 (tecla estendida), soltar
(bit 7), modificadores (Shift, Ctrl, Alt, AltGr = Alt da direita, Caps Lock,
Num Lock) e, por último, a tradução pelo mapa. Bytes de resposta do teclado
(0xFA, 0xFE) e bytes que o controlador marca como vindos do mouse são
ignorados, porque na fase 4 os dois dividem o mesmo controlador.

**Um código só para tudo.** O buffer guarda inteiros de 16 bits: 1-255 é um
caractere Latin-1 já pronto (com acento), e de 0x100 em diante são teclas sem
caractere (setas, Home, F1...). Ctrl+letra vira o código de controle (Ctrl+C
= 3), como em terminais tradicionais. Assim o terminal, o editor e o jogo
leem do mesmo jeito.

**Mapa como tabela de pares.** `mapa[scan][0/1]` = sem e com Shift. As teclas
mortas são o próprio acento marcado com um bit alto. Caps Lock inverte o
Shift só nas letras e no ç. A tecla morta fica guardada; a próxima tecla
procura numa tabela pequena de combinações (acento, letras base, resultados).
Sem combinação, sai o acento e depois a letra, como pede o PRD; acento +
espaço dá o próprio acento.

**Teste.** O teste digita pelo monitor do QEMU, que nomeia teclas pela
posição física (a tecla do ';' americano é o ç no ABNT2), cobrindo todas as
linhas da tabela do PRD 4.4. Detalhe encontrado: o nome `altgr` do QEMU não
gera o Alt da direita; o certo é `alt_r`.

## D010. Plano da fase 3: espaços de endereço, processos e chamadas (H10 a H16)

**Mapa de endereços virtuais (igual em todos os processos):**

| Faixa | Uso |
| --- | --- |
| 0x00000000-0x37FFFFFF | Kernel: mapeamento 1:1 da memória física (só anel 0). A página 0 fica sem mapeamento, para pegar ponteiro nulo. |
| 0x38000000-0x3FFFFFFF | Kernel: janelas para dispositivos em endereços físicos altos (vídeo, USB), fases 4 a 6. |
| 0x40000000-0xBFFFFFFF | Programa do usuário: código a partir de 0x40000000 (PRD 4), dados, área que cresce com `aumentar_memoria`, e a pilha no topo. |

As tabelas de páginas do kernel (0 a 0x3FFFFFFF) são criadas uma vez no boot,
e todo diretório de processo aponta para as mesmas tabelas. Assim o kernel
aparece igual em todo espaço de endereço sem copiar nada, e uma janela nova
(ex.: o framebuffer) vale para todos os processos na hora.

Páginas de 4 KB em tudo, inclusive no kernel. Páginas de 4 MB gastariam
menos tabelas (1 MB de tabelas hoje), mas uma página grande que cubra regiões
de tipos diferentes de memória é um caso que o manual da Intel deixa
indefinido, e o mapa do G470 tem regiões reservadas no meio da RAM
(0x20000000 e 0x40000000).

**Chamadas de sistema reiniciáveis.** Como toda interrupção passa pelo
despachante que devolve o quadro a retomar (D007), uma chamada que precisa
esperar (ler de um pipe vazio, esperar um filho, ler do teclado) não precisa
de uma pilha "congelada" no meio do kernel: ela marca o processo como
bloqueado, **volta o EIP do processo 2 bytes** (o tamanho de `int 0x80`) e
devolve o quadro de outro processo. Quando o processo for acordado, ele
executa o `int 0x80` de novo, com os mesmos registradores, e desta vez a
condição está satisfeita. Chamadas que não devem ser repetidas (`dormir`)
gravam o resultado antes de bloquear e não voltam o EIP. Consequência: o
kernel nunca é interrompido no meio de uma chamada (a porta 0x80 desliga as
interrupções), e não existe troca de contexto dentro do kernel, só na saída
de uma interrupção. É bem menos código e menos lugar para erro.

**Herança de descritores.** Não há `fork`. `executar` cria o processo direto
a partir do ELF, e o filho herda só os descritores 0, 1 e 2 do pai (entrada,
saída e erro), além da pasta atual. O shell monta redirecionamentos e pipes
assim: guarda a própria saída com `duplicar`, coloca o arquivo ou o pipe no
lugar, chama `executar` e restaura. Herdar só 0-2 evita o problema clássico
de um processo segurar sem querer a ponta de escrita de um pipe e o leitor
nunca ver o fim do arquivo.

**Terminal.** A entrada 0 lê o teclado em modo "linha" (o kernel edita a
linha, ecoa, Backspace funciona, Ctrl+D no começo da linha = fim de arquivo)
ou em modo "bruto" (cada tecla; teclas especiais viram ESC + uma letra), que
o shell usa para ter histórico e completar com Tab.

**Chamadas extras além do PRD 4.5** (registradas aqui porque o PRD lista o
mínimo): `aumentar_memoria` (a memória dos programas, usada pelo `malloc`
da libsagui), `pasta_atual` (para o `pwd`), `renomear` (para o `mv`),
`modo_terminal` (linha/bruto, para o shell) e `limpar_tela`.

## D011. Processos e escalonador: como foi pensado (H11)

**Troca de contexto sem rotina própria.** Graças ao despachante que devolve o
quadro a retomar (D007), trocar de processo é: guardar o ponteiro do quadro
atual no processo, escolher outro, trocar o CR3 se o espaço for diferente,
ajustar o `esp0` da TSS (pilha do kernel do próximo programa no anel 3) e
devolver o quadro dele. O `iret` no fim do trecho comum faz o resto.

**O processo 0 é o próprio boot.** Em vez de criar uma tarefa ociosa, o
código que terminou a inicialização vira o processo 0. Ele só é escolhido
quando ninguém mais está pronto, e fica em `hlt`.

**Tarefas do kernel começam por um quadro montado à mão.** O `iret` para o
anel 0 desempilha só EIP, CS e EFLAGS; os dois campos seguintes do quadro
(`esp_usuario`, `ss_usuario`), que só existem para o anel 3, ficam no topo
da pilha quando a função começa. Usamos isso de propósito: um vira o
endereço de retorno (para uma rotina que encerra a tarefa) e o outro, o
argumento da função.

**Round-robin com fatia de 2 tiques (20 ms)**, procurando o próximo pronto
na tabela a partir do atual, dando a volta. Se o ocioso está rodando e alguém
fica pronto (ex.: uma tecla chegou), a troca acontece no tique seguinte, sem
esperar a fatia.

**Fim de processo em duas etapas.** Ao terminar, a memória do programa e os
arquivos são liberados na hora, mas a pilha do kernel e a entrada na tabela
ficam (estado zumbi) até o pai recolher o código de saída com `esperar`. Não
dá para liberar a pilha do kernel na hora: estamos rodando nela até o
escalonador devolver o quadro de outro processo. Órfãos passam para o
processo 0 e são recolhidos pelo próprio escalonador.

## D012. Anel 3 e chamadas de sistema: como foi pensado (H12)

**Entrar no anel 3 é só um quadro diferente.** O processo novo recebe um
quadro com CS e SS de usuário (seletores com nível 3), a pilha do programa e
o ponto de entrada. Quando o escalonador o escolhe, o mesmo `iret` de sempre
troca de anel. O EFLAGS começa com IOPL=0: `cli`, `sti`, `in` e `out` no
anel 3 geram proteção geral.

**A TSS só serve para uma coisa:** dizer à CPU qual pilha do kernel usar
quando um programa no anel 3 é interrompido. O escalonador atualiza o
`esp0` a cada troca.

**Erro de programa x erro do kernel.** O despachante olha o nível de
privilégio do CS salvo no quadro: exceção vinda do anel 3 encerra só aquele
processo, com uma linha explicando o motivo; vinda do anel 0, tela vermelha.

**Validação de ponteiros.** Todo ponteiro vindo de um programa passa por
`usuario_ok`: o intervalo inteiro tem que estar na área dos programas
(0x40000000 a 0xBFFFFFFF) e cada página tem que estar mapeada com o bit de
usuário (e de escrita, quando o kernel vai escrever). Textos são copiados
para o kernel conferindo cada página que o texto atravessa. Só depois disso
o kernel lê ou escreve, direto pelo endereço virtual, já que o CR3 em uso é
o do próprio processo.

**Testes sem carregador ELF.** A H12 vem antes do carregador (H13). Os
programas de teste são três trechos de assembly independentes de posição,
dentro do kernel, que o autoteste copia para a página 0x40000000 de um
espaço novo.

## D013. Carregador ELF e libsagui: como foi pensado (H13)

**O carregador só precisa dos segmentos.** Dos cabeçalhos ELF, o kernel lê
apenas a lista de segmentos PT_LOAD: endereço, bytes do arquivo, bytes na
memória e se é gravável. Cada segmento ganha páginas novas no espaço do
processo (somente leitura se o ELF não pedir escrita) e os bytes são
copiados pelo endereço físico, aproveitando o mapeamento 1:1 do kernel.
Seções, símbolos e relocação são ignorados: os programas são estáticos e
ligados em 0x40000000. Tudo é conferido contra o tamanho do arquivo e contra
os limites da área dos programas antes de qualquer cópia; um arquivo
estranho é recusado com `ERRO_FORMATO`.

**Argumentos na pilha.** Os textos são copiados para o kernel antes de
criar o processo (o espaço do pai pode mudar), e depois gravados no topo da
pilha do filho, seguidos do vetor de ponteiros, de argv e de argc. A
`_inicio` da libsagui lê `[esp]` e `[esp+4]`.

**libsagui.** Uma função por chamada de sistema; texto e memória; `aloca`
com blocos contíguos numa área que cresce com `aumentar_memoria` (busca do
primeiro que couber, juntando vizinhos livres durante a busca, um desenho
diferente do `kmalloc` do kernel de propósito: os dois têm necessidades
diferentes); `imprimir` com o mesmo método do formatador do kernel;
`ler_linha` byte a byte, para não consumir dados de um pipe que pertencem
ao próximo leitor.

**Build.** Cada `userland/programas/X.c` vira `/bin/X` e cada
`userland/testes/X.c` vira `/testes/X`. O ramdisk é montado a partir de uma
cópia de `rootfs/` com esses programas, então a pasta `rootfs/` do
repositório só tem os arquivos escritos à mão.

## D014. Camada gravável e arquivos abertos: como foi pensado (H14)

**Árvore na RAM com cópia na escrita.** No boot, cada entrada do SGFS vira
um nó numa árvore (pasta -> lista de filhos, em ordem alfabética, para o
`ls` sair ordenado sem ordenar). Um arquivo aponta para seus bytes dentro do
ramdisk; na primeira escrita ganha uma cópia própria no heap do kernel, que
cresce dobrando de tamanho. Criar, apagar e renomear só mexem na árvore.
Assim a "camada gravável por cima do SGFS" do PRD não precisa de duas
estruturas (uma de leitura e uma de escrita) combinadas a cada busca: há
uma só, e o SGFS é só o ponto de partida.

**Apagar um arquivo aberto.** O nó sai da pasta na hora, mas os dados ficam
até o último descritor fechar (o nó conta quantos arquivos abertos apontam
para ele).

**Três níveis: descritor -> arquivo aberto -> nó.** O descritor é só um
índice na tabela do processo. O "arquivo aberto" guarda tipo (nó, terminal,
ponta de pipe), modo e posição, e conta referências, porque `duplicar` e
`executar` fazem dois descritores (às vezes de processos diferentes)
compartilharem a mesma posição. É o que faz `echo a > x` seguido de outro
comando escrever no lugar certo.

**Terminal como arquivo.** Entrada 0 e saídas 1 e 2 do primeiro programa
são um arquivo aberto do tipo terminal; todos os outros herdam. O modo linha
edita no kernel; o modo bruto entrega tecla a tecla (D010).

**Proteções.** Não se remove pasta com conteúdo, nem a raiz, nem a pasta em
que algum processo está. Mover uma pasta para dentro dela mesma é recusado.

## D015. Pipes: como foi pensado (H15)

**Buffer circular de 512 bytes por pipe**, pequeno de propósito: qualquer
transferência real cruza a borda muitas vezes, e o teste de 20.000 bytes
exercita o bloqueio dezenas de vezes.

**Um canal só para leitores e escritores.** Os dois lados dormem no próprio
pipe. Ler ou escrever acorda todos, e cada um repete a chamada (D010) e
confere a condição de novo. Acordar alguém que ainda não pode continuar
custa uma volta a mais, mas dispensa duas filas e a lógica de quem acorda
quem.

**Escrita parcial.** Com espaço para só parte dos dados, o kernel escreve o
que cabe e devolve quanto foi; não bloqueia no meio. A `escrever_tudo` da
libsagui insiste no resto. Assim nenhuma chamada fica "pela metade" dentro
do kernel, o que combina com as chamadas reiniciáveis.

**Fim de arquivo** = pipe vazio e nenhum escritor. **Pipe quebrado** =
escrever sem nenhum leitor. Os dois dependem de contar as pontas abertas,
por isso o filho herda só 0, 1 e 2 (D010) e o shell fecha suas cópias das
pontas logo depois de criar os processos.

## D016. O shell sagsh: como foi pensado (H16)

**Só é interno o que precisa mudar o próprio shell:** `cd`/`entrar`,
`pwd`/`onde` (a pasta é do processo do shell), `clear`/`limpar`, `ajuda`,
`reiniciar` e `sair`. Todo o resto é programa em `/bin`, um arquivo `.c`
por comando. Os apelidos em português são resolvidos por uma tabela no
shell, que também alimenta o `ajuda` e o completar com Tab, então um nome
novo aparece nos três lugares de uma vez.

**Pipes e redirecionamento sem fork.** Para cada etapa de um pipeline, o
shell coloca em 0 e 1 o que aquela etapa deve usar (arquivo, ponta de pipe
ou o terminal guardado), chama `executar` e passa para a próxima; no fim
restaura 0 e 1 e fecha as pontas que sobraram nele. Só depois disso espera
os filhos: se esperasse cada um logo após criá-lo, o primeiro poderia
travar com o pipe cheio antes de o segundo existir.

**Edição da linha no próprio shell**, com o terminal em modo bruto:
histórico de 32 linhas (setas), Tab que completa comandos (com apelidos) ou
nomes de arquivos (acrescentando `/` em pastas), Ctrl+C descarta a linha,
Ctrl+L limpa a tela. A tela de texto apaga com `\b`, então trocar a linha
pelo histórico é "apagar n caracteres e escrever a nova".

**Segundo plano (`&`).** O shell não espera e mostra o PID. Antes de cada
prompt recolhe, sem bloquear (`esperar` com a opção nova), os filhos que já
terminaram, e avisa. Assim `contador 10 &` roda enquanto outros comandos
são usados, que é a demonstração de multitarefa pedida no PRD.

**O kernel reabre o shell** se ele for fechado com `sair`.

**Um detalhe do compilador.** O primeiro `falha divisao` não dividia: o
GCC troca `1 / x` por comparações (o resultado só pode ser -1, 0 ou 1) e a
CPU nunca executa a divisão. O programa agora divide 12345 por zero.

## D017. Vídeo VESA: como foi pensado (H17)

**Buffer de trás sempre em 32 bits (0x00RRGGBB).** A placa pode usar 24 ou
32 bits e qualquer ordem de cores (o VBE informa a posição de cada canal).
Em vez de cada rotina de desenho conhecer o formato, tudo é desenhado num
buffer na RAM com um formato só, e a conversão acontece na cópia para a
placa. Quando o formato da placa já é igual (o caso do QEMU e o esperado no
G470), a cópia é um `memcpy` por linha.

**Copiar só o que mudou.** As rotinas de desenho aumentam um "retângulo
sujo"; `video_mostrar` copia só ele. Um caractere digitado no terminal
copia 8x16 pixels, e não a tela inteira de 3 MB.

**O framebuffer entra pela janela do kernel** (0x38000000, D010): como as
tabelas dessa faixa são compartilhadas, a tela fica acessível em qualquer
processo sem trocar nada.

**O cursor do mouse é desenhado na cópia**, por cima, direto na placa, e
nunca no buffer de trás. Mover o cursor é só refazer, a partir do buffer de
trás, os pixels de onde ele estava. Assim nada que é desenhado precisa saber
que existe um cursor.

**Sem gráfico, volta ao texto.** Se o estágio 2 não achou um modo aceitável,
ou o `4F02` falhou, `vesa_ativo` fica 0 e o console continua no modo texto
(aviso na tela a partir da H18).

## D018. Fonte e terminal gráfico: como foi pensado (H18)

**A fonte é desenhada em texto.** `tools/fonte/sagui8x16.txt` tem cada
caractere como uma grade de `#` e `.`, legível e editável por qualquer
pessoa. O `tools/mkfonte.py` converte para C no build. Estilo: traços de 2
pixels, letras em 7 colunas (a 8ª separa), maiúsculas com 10 linhas,
minúsculas com 7, descendentes de 2.

**As acentuadas são compostas, não desenhadas uma a uma.** O gerador junta
a letra base com um acento de 2 linhas: nas maiúsculas, nas linhas 0 e 1 da
célula (que a fonte deixa livres); nas minúsculas, logo acima da altura do
"x"; no i, sobre um "ı" sem ponto; a cedilha embaixo. São 60 letras do
Latin-1 a partir de 7 peças, e todas ficam coerentes entre si.

**Terminal = grade de células.** Cada célula guarda caractere e cores, para
o cursor poder ser desenhado (cores trocadas) e apagado sem perder o que
estava embaixo. Rolar = mover os pixels uma linha de texto no buffer de trás.

**Sequências de controle próprias e mínimas:** `ESC [ D` (esquerda),
`ESC [ C` (direita) e `ESC [ K` (apagar até o fim da linha), nos dois motores
(texto e gráfico). É o mínimo para o shell editar no meio da linha, pedido
do Eli no teste da fase 3.

**Programa em segundo plano escrevendo enquanto o shell edita.** O terminal
sabe quem o pôs em modo bruto. Se outro processo escreve nesse meio-tempo,
o texto começa numa linha nova e o shell recebe `ESC R`, que o faz
redesenhar o prompt e o que já estava digitado (também pedido do Eli).

**Testes leem a tela gráfica pela própria fonte.** Como a grade é exata e a
fonte é nossa, o harness reconhece cada célula de 8x16 da captura comparando
com os bitmaps da fonte: é uma leitura exata, sem OCR, e por isso os testes
das fases anteriores continuam valendo no modo gráfico.

**Boot reordenado.** Interrupções, memória e paginação são ligadas antes de
qualquer texto, porque o terminal gráfico precisa do `kmalloc` e da janela do
kernel. As mensagens são impressas depois, já no modo gráfico.

## D019. Mouse PS/2: como foi pensado (H19)

**Mesmo controlador do teclado, portas separadas na leitura.** O i8042
avisa no bit 5 do estado se o byte que está esperando é do mouse. O driver
do teclado passou a **parar sem ler** quando vê esse bit (antes ele lia e
descartava, o que roubaria pacotes do mouse), e o do mouse só lê bytes com
esse bit ligado. A inicialização do mouse é feita com as interrupções
desligadas, para nenhum dos dois tratadores pegar as respostas (0xFA) que o
próprio driver está esperando.

**Pacotes de 3 bytes com ressincronização.** O bit 3 do primeiro byte é
sempre 1; se um byte se perder, o driver descarta até achar um byte que
possa ser o primeiro. Pacotes com os bits de estouro são ignorados (o valor
do deslocamento não é confiável).

**O cursor é do vídeo, não do mouse.** O driver só informa a posição; o
desenho da seta é feito na cópia para a tela (D017), por cima de tudo.

**Fila de eventos.** Cada pacote vira um evento (posição, deslocamento,
botões e quais mudaram) numa fila, com um canal para quem quiser dormir
esperando por ele. Na fase 5 o gerenciador de janelas consome essa fila.

## D020. Um dono só para o controlador do teclado e do touchpad (H19-fix)

**O que aprendemos no G470.** O controlador i8042 tem uma fila de saída de
um único byte, compartilhada pelo teclado e pelo touchpad. Se um byte não
for lido, nada mais passa. Na H19, o teclado e o mouse liam o controlador
cada um por conta própria, e o teclado passou a deixar na fila os bytes do
mouse para o driver do mouse. No G470 a interrupção do mouse não chegou, e
o byte parado travou o teclado.

**Decisão.** Um só módulo (`i8042.c`) lê a fila e entrega cada byte a quem
é de direito. Ele esvazia a fila na IRQ 1, na IRQ 12 e a cada tique do
relógio. A terceira é a rede de segurança: mesmo que uma interrupção não
chegue no hardware real, nenhum byte fica parado mais de 10 ms. Custa uma
leitura de porta a cada tique.

**Configuração defensiva.** A fila é esvaziada antes de pedir o byte de
configuração; o valor lido é conferido (bits reservados), com um valor
seguro se falhar; a interrupção e a tradução do teclado ficam **sempre**
ligadas; a IRQ 12 só é ligada se o touchpad responder; se ele não responder
(nem depois de um reset), a porta do mouse é desligada. O aviso na tela
mostra os números (configuração lida e gravada, respostas do touchpad) para
que uma foto diga o que o hardware respondeu.

## D021. Plano da fase 5: janelas e programas gráficos (H20 a H28)

**O kernel desenha a área de trabalho e as molduras; os programas desenham
o conteúdo.** Isso é o que o PRD pede (seção 4) e mantém os programas
gráficos como programas comuns no anel 3.

**Memória compartilhada para o conteúdo da janela.** `criar_janela` aloca
páginas físicas contíguas para os pixels da janela e as mapeia no espaço do
programa (a partir de 0xA0000000). O programa desenha direto nelas, sem
chamada de sistema por pixel, e chama `atualizar_janela` para pedir ao
kernel que mostre. O kernel lê as mesmas páginas pelo mapeamento 1:1 (D008).
Essas páginas levam uma marca na tabela de páginas (um bit livre para o
sistema) para que destruir o espaço do processo não as libere: quem as
devolve é o gerenciador de janelas, ao fechar a janela.

**Eventos por janela.** Cada janela tem uma fila: clique (com contagem de
cliques, para o duplo clique), botão solto, movimento, tecla e "fechar". O
kernel só entrega a cada programa os eventos das janelas dele.
`proximo_evento` pode esperar (dorme no canal da janela) ou não (o jogo
precisa continuar se não houver tecla).

**Gerenciador como tarefa do kernel.** Uma tarefa do kernel acorda a cada
tique, consome a fila do mouse e a do teclado, cuida de arrastar, focar e
fechar janelas, entrega os eventos e redesenha. A composição é feita só no
retângulo que mudou: fundo (de uma cópia pronta da área de trabalho), as
janelas de baixo para cima, a barra superior por último.

**Modo gráfico x terminal de tela cheia.** Com a área de trabalho no ar, o
console de tela cheia deixa de desenhar (o texto do kernel vai só para a
serial), e o shell passa a rodar dentro do programa Terminal. Os testes das
fases 2 a 4 continuam rodando com uma opção de build (`--console`) que
mantém o terminal de tela cheia.

## D022. Gerenciador de janelas: detalhes (H21)

**Arrastar, focar, fechar ficam no kernel.** Clique na barra de título
começa a arrastar; clique em qualquer lugar traz a janela para cima e dá o
foco; clique no X manda "fechar" ao programa, que decide terminar. O kernel
não mata o programa: assim um editor pode perguntar "salvar antes de
fechar?" (H25).

**Quem recebe o quê.** Clique na área de conteúdo vai para a janela, com
coordenadas relativas a ela; o soltar e os movimentos com botão apertado vão
para a mesma janela que recebeu o clique (mesmo que o mouse saia dela), o
que permite arrastar coisas dentro de um programa; movimento sem botão vai
para a janela com foco, se o mouse estiver sobre ela; teclas vão para a
janela com foco. Duplo clique é detectado pelo kernel (dois cliques em
450 ms no mesmo lugar) e chega como `cliques = 2`.

**Tarefa do kernel que dorme de verdade.** O gerenciador acorda a cada tique
para consumir as filas do mouse e do teclado. Para dormir ele bloqueia e cede
a vez na hora, por uma interrupção de software só do kernel (0x81); sem isso
o processo 0 nunca mais rodava (erro 11).

## D023. Calculadora em ponto fixo (H26)

Os números são inteiros de 64 bits que guardam o valor vezes 10.000 (quatro
casas decimais). Soma e subtração são diretas; multiplicação divide o
produto por 10.000; divisão multiplica o dividendo por 10.000 antes. Antes
de multiplicar, o programa confere se o resultado cabe (dividindo o maior
inteiro possível por um dos fatores) e avisa "Número grande demais". Num x86
de 32 bits, `/` e `%` entre inteiros de 64 bits viram chamadas a rotinas da
biblioteca do compilador, que não usamos; a calculadora faz a divisão longa
bit a bit, como no papel. Efeito colateral bom: `0,1 + 0,2` dá exatamente
`0,3`, sem o erro de arredondamento do ponto flutuante.

## D024. Hub USB no QEMU (fase 6)

**O que o PRD pede:** testar no QEMU com um hub virtual entre o controlador
EHCI e o pendrive, porque no G470 (chipset Intel série 6) cada controlador
EHCI tem um hub interno de alta velocidade ("rate matching hub") e todo
dispositivo fica atrás dele.

**O que o QEMU 8.2 permite:** o `usb-hub` do QEMU é um hub de velocidade
plena (USB 1.1) e o QEMU recusa ligá-lo numa porta de alta velocidade do EHCI
("speed mismatch"); a opção de controladores companheiros (UHCI) levaria o
hub e o pendrive para o controlador UHCI, que o G470 nem tem. Não existe hub
de alta velocidade no QEMU.

**Decisão:** o suporte a hub é escrito pela especificação USB 2.0
(capítulo 11: descritor de hub, energia, estado e reset das portas,
mudanças de conexão) e é usado sempre que um dispositivo de classe hub
aparecer, inclusive para a detecção de conexão e remoção. No QEMU os testes
ligam o pendrive direto na porta do EHCI; o caminho do hub só é exercitado
no G470, e o roteiro da fase 6 pede atenção a ele. Dispositivos de
velocidade plena ou baixa atrás de um hub (que exigiriam "split
transactions") não são suportados: pendrives são de alta velocidade.

## D025. USB: como foi pensado (H29 a H31)

**Síncrono e sem interrupções do controlador.** Cada transferência monta os
descritores (qTD), pendura uma cabeça de fila (QH) no anel assíncrono, espera
o controlador terminar e tira a QH (confirmando com o "doorbell" da
especificação). Só uma transferência por vez, com as interrupções da CPU
desligadas durante a espera. É mais lento que um driver com filas e
interrupções, mas para um pendrive de dados é suficiente, e não existe
nenhuma corrida entre o kernel e o controlador para depurar sem serial no
G470.

**O tempo vem do próprio controlador.** Com as interrupções desligadas o
relógio do sistema para; os atrasos exigidos pela especificação USB (50 ms
de reset de porta, por exemplo) e os limites de espera usam o contador de
microquadros do EHCI (FRINDEX, 8 por milissegundo).

**Memória para o controlador.** O EHCI lê e escreve a memória por endereço
físico. O driver separa uma página para a QH-cabeça, a QH de trabalho, o
pacote de setup e até 8 qTDs, e 64 KB contíguos de buffer. Como o kernel
enxerga a memória física 1:1 (D008), o mesmo número serve para a CPU e para
o controlador.

**Alternância de dados (data toggle).** Cada endpoint bulk alterna 0/1 a cada
pacote. Como a QH é recriada a cada transferência, o driver guarda a
alternância de cada endpoint e a calcula para cada qTD (cada qTD, menos o
último, leva um múltiplo do tamanho de pacote). Depois de um STALL e do
CLEAR_FEATURE, a alternância volta a 0, como manda a especificação.

**Pedindo o controlador à BIOS.** A BIOS do G470 usa o EHCI para ler o
pendrive de boot e emular teclado USB. Antes de reiniciar o controlador, o
driver segue o procedimento de passagem de controle da especificação EHCI
(semáforos da capacidade USBLEGSUP) e desliga as interrupções SMI. Como o
SaguiOS já está todo na RAM, não há nada a perder.

**Conexão a quente por consulta.** Uma tarefa do kernel olha, duas vezes por
segundo, o bit "conexão mudou" das portas do controlador e de cada hub.

## D026. FAT32: como foi pensado (H32, H33)

**Caminhos com nomes longos de verdade.** Cada nome é procurado na pasta
juntando as entradas de nome longo (LFN, em UTF-16) que antecedem a entrada
curta 8.3, conferidas pela soma de verificação da especificação. Sem nome
longo válido, vale o nome curto (respeitando as marcas de minúsculas que o
Windows grava). O texto do SaguiOS é Latin-1, que é exatamente o começo do
Unicode: a conversão é direta; o que não couber vira '_'. A comparação de
nomes ignora maiúsculas e minúsculas, inclusive acentuadas.

**Gravação.** Todo arquivo ou pasta criado ganha nome longo e um nome curto
único gerado (NOME~1.EXT). Clusters novos são procurados a partir da dica de
"próximo livre" do setor FSInfo; as duas cópias da FAT são sempre gravadas
juntas; o número de clusters livres no FSInfo é atualizado ao desmontar.
Datas e horas vêm do relógio CMOS.

**Cache pequena e explícita.** Os setores da FAT e das pastas passam por uma
cache de 16 setores, gravada no fim de cada operação (criar, escrever,
apagar...) e no desmontar. Os dados dos arquivos, quando cobrem setores
inteiros, vão direto para o pendrive. Assim, mesmo se o pendrive for
arrancado, o que foi feito até a operação anterior já está gravado.

## D027. Textos em UTF-8 vindos do pendrive (H32)

O SaguiOS guarda e mostra texto em Latin-1 (PRD 4.4), mas os arquivos que
chegam num pendrive gravado no Windows atual ou no Linux costumam estar em
UTF-8, em que "ã" ocupa dois bytes. O kernel não mexe no conteúdo (lê os
bytes como estão). Quem mostra texto converte: a libsagui tem
`utf8_para_latin1`, que troca cada sequência UTF-8 válida de um caractere
que exista no Latin-1 pelo byte correspondente e deixa o resto como está
(um texto que já seja Latin-1 quase nunca forma sequências UTF-8 válidas, então
não é estragado). O `cat` converte enquanto copia (guardando um pedaço de
sequência que tenha ficado no fim de um bloco) e o Editor converte ao abrir.
O que o SaguiOS grava continua em Latin-1, que o Bloco de Notas do Windows
abre como "ANSI".

## D028. Estruturas EHCI no formato de 64 bits e todas as partições (correção da fase 6)

O EHCI tem dois formatos de qTD/QH: o de 32 bits e o de 64 bits (apêndice B
da especificação), e quem escolhe é o controlador (bit 0 do HCCPARAMS), não o
sistema. Em vez de dois formatos no código, o SaguiOS usa sempre o tamanho
maior (qTD de 64 bytes, QH de 96) com as palavras altas zeradas: num
controlador de 32 bits as palavras a mais são simplesmente ignoradas, e num de
64 bits o endereço alto 0 está certo porque toda a memória usada fica abaixo de
896 MB (D008). Custa poucos bytes por fila e elimina um caminho que só o
hardware real exercitaria.

Partições: um pendrive pode ter o SaguiOS no começo (0xDA) e uma FAT32 depois,
que foi o que o Eli fez. As quatro entradas do MBR são examinadas; vale a
primeira FAT32 com BPB válido.

Também (erro 19): **um anel assíncrono por controlador.** Com dois EHCI (o
G470 tem dois), um anel compartilhado é percorrido pelos dois e a mesma
transferência sai nos dois barramentos. Cada controlador ganhou a sua página
de estruturas; o buffer de dados segue compartilhado, porque só uma
transferência acontece por vez.

## D029. Bip sem travar ninguém (B3)

O alto-falante do PC fica ligado à saída do canal 2 do PIT. O kernel
programa esse canal como onda quadrada (modo 3) com divisor 1.193.182 / Hz e
liga os bits 0 e 1 da porta 0x61. Para o som não travar quem pede, a chamada
`SIS_BIP` recebe uma lista de notas (frequência e duração), copia para uma
fila no kernel e volta na hora; quem toca é o tique do relógio (10 ms), que
desconta a nota atual e começa a próxima. Uma lista nova substitui a que
estiver tocando (num jogo, o efeito mais novo importa mais), a não ser que o
programa peça para enfileirar. A duração tem a resolução do tique: 10 ms.

Como saber se tocou num PC onde não dá para ouvir (testes, ou um notebook
que não liga o sinal ao alto-falante): na primeira nota do boot o kernel lê a
saída do canal 2 (bit 5 da porta 0x61) durante 5 ms medidos pelo contador do
canal 0 e registra no log quantas vezes ela virou. Medir por voltas de laço
não serve: no QEMU as leituras de porta são muito mais rápidas que no PC, e a
primeira versão viu 0 viradas.

## D030. Teclas seguradas para jogos (B4)

Os eventos de janela só avisam quando uma tecla é apertada (com a repetição
automática do teclado depois de meio segundo). Um jogo em primeira pessoa
precisa saber se a seta *continua* apertada, e andar e girar ao mesmo tempo.
O driver de teclado passou a manter um mapa de 256 bits, um por código de
varredura (128 a 255 para as teclas estendidas, como as setas), ligado ao
apertar e desligado ao soltar. A chamada `SIS_TECLAS_SEGURADAS` copia esse
mapa, mas só para o processo dono da janela em foco; os outros recebem tudo
zerado, para um programa em segundo plano não ler o que se digita em outra
janela. Ctrl e espaço sozinhos (que não geram caractere) também aparecem no
mapa, o que permite atirar com eles.

## Sagui 3D (B4)

Um jogo em primeira pessoa com visual 2,5D, escrito para este projeto em
`userland/sagui3d/` (seis arquivos, ~2.400 linhas, binário de 57 KB). As
fases são arquivos de texto em `/jogos/sagui3d/`. Abaixo, como cada parte
foi pensada.

### Projeção, ordem de desenho, recorte e o "olho de peixe"

Parti de semelhança de triângulos. Com o olho na origem olhando para a
frente, um ponto a uma distância `f` à frente e `l` para o lado aparece na
tela a `l * FOCO / f` pixels do centro; uma altura `h` (acima do olho)
aparece `h * FOCO / f` pixels acima do horizonte. `FOCO` = 176 pixels numa
vista de 320 dá uns 85 graus de visão.

O efeito "olho de peixe" aparece quando se usa a distância em linha reta até
a parede: nas bordas da tela essa distância é maior que no centro e uma
parede reta encolhe dos lados. Aqui `f` é sempre a distância medida *ao
longo da direção do olhar* (a coordenada "fundo" depois de girar o mundo
para a câmera), que é exatamente o que a semelhança de triângulos pede. Com
ela, os pontos de uma parede reta caem numa reta na tela.

A ordem de desenho saiu da escolha de só ter salas **convexas**. De dentro
de uma sala convexa, cada direção bate em exatamente um lado dela; então os
lados de uma sala nunca se cobrem na tela e podem ser desenhados em qualquer
ordem. Para o que está além, cada coluna da tela guarda uma "fresta": a
faixa de linhas ainda não pintada. Começando pela sala do sagui, cada lado
virado para ele pinta, nas colunas que ocupa, o teto acima e o chão abaixo;
se for parede, pinta a parede e fecha a coluna; se for passagem, pinta só os
degraus (o pedaço entre os dois tetos e entre os dois chãos) e estreita a
fresta para o vão. A sala vizinha é então desenhada só naquelas colunas e
só dentro da fresta. Com isso, não existe ordenação de paredes por
distância: o que está na frente é desenhado primeiro e o que está atrás só
aparece pelo buraco que ficou. O desenho para quando todas as frestas fecham
(ou depois de 32 salas em cadeia, como proteção).

Recorte: um lado com uma ponta atrás do olho é cortado no plano a meio palmo
à frente, só para achar as colunas que ele ocupa. Dentro dessas colunas, a
posição exata da parede em cada coluna vem do cruzamento do raio daquela
coluna com o segmento (reta contra reta), que dá o "fundo" e a posição ao
longo da parede para escolher a coluna da textura. As distâncias na tela são
limitadas a ±4096 pixels antes de somar ao centro (a primeira versão deixava
uma faixa da tela sem desenho quando uma parede quase encostava no olho,
porque o número não cabia em 32 bits).

Chão e teto: numa linha da tela a `d` pixels do horizonte, o plano de altura
`h` está a `fundo = (h - olho) * FOCO / d`. `FOCO / d` é calculado uma vez
por linha; com o fundo e a direção de cada coluna sai o ponto do mapa e o
pixel da textura. **Decisão: chão e teto continuam texturizados**, porque o
QEMU, que é bem mais lento que o G470, desenhou entre 80 e 110 quadros por
segundo; não foi preciso trocar por cor sólida.

Tudo é inteiro com 16 bits de fração (os programas não usam a FPU, que o
kernel não salva na troca de processos). Multiplicações passam por 64 bits;
a divisão de 64 por 32 bits usa a própria instrução `idiv`, conferindo antes
se o resultado cabe. O seno sai de uma tabela de um quarto de volta montada
na abertura do jogo girando um vetor de um passo pequeno 1024 vezes.

### Salas com alturas diferentes

Cada sala tem a altura do chão, a do teto, a luz (0 a 15) e as texturas de
piso, forro e paredes; o forro pode ser "céu" (salas a céu aberto, com o
contorno das árvores da mata no horizonte, que gira com o sagui). Dois
lados que usam os mesmos dois pontos, um em cada sala, formam uma passagem.
A diferença de chão vira um degrau desenhado; uma escada é uma fileira de
salas estreitas com o chão 16 palmos mais alto a cada uma; uma plataforma é
uma sala mais alta. O sagui sobe até 24 palmos de uma vez e o olho
acompanha o chão subindo devagar (e descendo um pouco mais rápido). Formas
côncavas, como uma galeria com um pilar no meio ou um pátio com um tablado,
são feitas juntando salas convexas em volta. A luz de cada sala escurece
com a distância na direção de um verde quase preto, como a sombra dentro da
mata.

O arquivo de fase é texto: `ponto`, `sala` (propriedades e a lista de
pontos), `lado` (textura própria de uma parede), `secreta` (passagem que
parece parede de cipó), e uma linha para cada coisa (`sagui`, `banana`,
`cacho`, `pote`, `porteira`, `macaco`). O jogo confere cada sala (convexa,
pontos definidos) e diz a linha do erro. `tools/sagui3d_mapa.py` faz as
mesmas verificações e desenha a planta vista de cima, para desenhar fases.

### Texturas e personagens

Os personagens e objetos são pixel art escrita como texto no código, uma
letra por pixel com uma paleta própria: o macaco-prego (24x32, com o boné
escuro que dá nome à espécie, rosto creme e rabo enrolado; tonto, os olhos
viram espirais e três estrelinhas giram na cabeça), a banana, o cacho
dourado, a casca, a jabuticaba, o pote de jabuticabas, o coração e o sagui
do SaguiOS. O estilingue é desenhado pela metade e espelhado, com as
mãozinhas cinzentas do sagui; o elástico é traçado em linha reta, solto ou
puxado com a semente no meio. A porteira é montada em código: mourões,
placa com a banana e, fechada, três tábuas com uma trava em diagonal.

As texturas de 32x32 são geradas por regras na abertura do jogo, o que
economiza espaço e deixa cada regra explicada: pedra em fiadas de 8 pixels
com três pedras de larguras sorteadas por fiada (somando 32 para emendar
sem costura) e juntas desencontradas, borda clara em cima e escura embaixo
e musgo em algumas; cipó como a pedra com duas ramas em onda (pela tabela
de seno) e folhas alternadas; madeira em quatro tábuas com veios e pregos;
grama com florzinhas amarelas e brancas; terra com pedrinhas; lajota em
placas com rejunte; folhas (teto de mata fechada) e rocha.

### Como os macacos-prego decidem para onde andar

A cada 4 tiques (30 ms cada), cada macaco que está quieto ou caçando olha
para o sagui: se ele está a menos de 640 palmos e a linha reta entre os dois
passa só por passagens com vão na altura dos olhos (o mesmo passeio de sala
em sala que o desenho usa, saindo de cada sala convexa pelo primeiro lado
que a linha cruza), o macaco o vê e guarda onde o viu. Caçando, anda em
linha reta até o último lugar onde o viu, um pouco mais devagar que o sagui
andando. Se bate numa parede, desvia 45 graus para um lado sorteado por meio
segundo e tenta de novo, o que basta para contornar quinas e pilares sem um
mapa de caminhos. Chegando lá sem ver o sagui, fica de guarda.

Encostar no sagui tira uma vida: o macaco "pega uma banana do bolso" e sai
correndo com ela por uns segundos, e o sagui fica um tempo sem poder ser
pego de novo. Cada semente faz o macaco parar um instante; a terceira o
deixa tonto (estrelinhas) e depois ele foge para longe do sagui até voltar
para a mata (some do mapa e conta como "espantado"). Pisar numa casca de
banana faz o macaco deslizar um pouco, girar e ficar tonto uns segundos,
e depois ele volta a caçar. Ninguém se machuca.

### O que foi feito diferente de propósito

- O tema e as regras são do SaguiOS: bananas como mecânica central (pontos,
  vidas a cada cinco, cascas como armadilha, porteira que pede um mínimo de
  bananas, um cacho dourado escondido por fase), estilingue de jabuticaba,
  macacos que roubam bananas em vez de atacar.
- A barra de status não tem retrato do personagem: só ícones (banana com
  "atuais/mínimo da porteira", cascas, sementes, corações, fase e pontos).
- Os inimigos não morrem: ficam tontos e fogem.
- Os mapas são polígonos convexos com passagens descobertas pelos pontos em
  comum, num arquivo de texto próprio; as fases são áreas da Mata Atlântica
  (uma clareira, um rio seco, um templo) com céu e árvores.
- O motor desenha coluna por coluna com frestas, sem nenhuma ordenação de
  paredes; o escurecimento vai para o verde da mata, não para o preto.
- Nenhum código, tutorial ou repositório de jogo foi consultado: as contas
  partiram da geometria descrita acima.

## D031. Configurações no pendrive, montado sozinho quando é do SaguiOS (H37)

O SaguiOS roda da RAM e nunca grava no disco interno, então as configurações
moram no pendrive de dados, em `/usb/.saguios/configuracoes.txt`: texto
simples, `chave = valor`, reescrito inteiro a cada mudança (são poucas linhas,
e um desligamento no meio não deixa linha pela metade). O kernel guarda a
tabela na memória e os programas leem e gravam por duas chamadas de sistema;
assim o próprio kernel (a área de trabalho, o relógio) também usa os valores.

Para as configurações valerem desde o boot, um pendrive que **já tem** a pasta
`.saguios` é montado sozinho ao ser conectado: ela só existe se o usuário já
montou esse pendrive e mudou alguma configuração, então montar não surpreende
ninguém. Os outros pendrives continuam esperando o usuário montar, como na
1.0. A espiada para saber se a pasta existe é só leitura (desmonta sem gravar).

Mudanças feitas sem pendrive valem até desligar; se um pendrive for montado
depois, elas são gravadas junto na próxima mudança.

## D032. Aparência e ícones em colunas (H38)

A aba Aparência troca o degradê do fundo (Brasil, Mata, Céu, Noite, Terra) e
mostra ou esconde o sagui do canto. O kernel guarda o fundo pronto num
buffer; quando as configurações mudam (um contador de "geração" aumenta), a
tarefa das janelas redesenha o fundo e a tela. O tamanho dos ícones, previsto
no PRD, ficou de fora: com 8 ou 9 ícones, ícones maiores não cabem em 768
linhas. Em vez disso, os ícones voltaram ao espaçamento folgado da 1.0 e
continuam numa segunda coluna quando não cabem mais na primeira.

Acertar o relógio grava direto no relógio da placa-mãe (CMOS), com o bit SET
ligado durante a escrita para o chip não avançar no meio, no mesmo formato
(BCD ou binário, 12 ou 24 horas) que ele já usa. O fuso horário é guardado
nas configurações para o relógio automático pela internet (fase 4).

## D033. Som: HD Audio por consulta, misturador no tique (H40 a H42)

**Driver.** O controlador HD Audio conversa com o codec por duas filas na
memória (CORB para os comandos, RIRB para as respostas). O driver não usa
interrupções: manda o comando e espera o ponteiro do RIRB andar. Um detalhe
achado no QEMU: com o aviso de resposta (bit 0 do RIRBCTL) desligado, o
controlador do QEMU para de ler a fila depois de cada resposta; com ele ligado
e as interrupções globais desligadas, o aviso só marca um registrador, que o
driver limpa a cada resposta. Funciona igual nos dois casos no hardware real.

O caminho do som é descoberto pelo grafo do codec, sem tabela de modelos: para
cada pino que a configuração do fabricante diz ser linha, alto-falante ou fone
e estar ligado a algo, uma busca em profundidade pelas listas de conexão acha
um DAC. No caminho, os seletores recebem a conexão certa, os amplificadores
ficam sem mudo no passo de 0 dB (alto sem distorcer), o pino liga a saída (e o
reforço de fone) e, se tiver, o amplificador externo (EAPD), que muitos
notebooks precisam para o alto-falante tocar.

**Misturador.** Um anel de 64 KB que o DMA lê sem parar, em 48 kHz, 16 bits,
estéreo. A cada tique (10 ms), o kernel escreve o pedaço que falta para ficar
60 ms à frente da posição de leitura: soma as fontes dos programas e a fonte de
notas, aplica o volume e corta em 16 bits. Cada programa tem um anel próprio de
~340 ms e manda quadros sem esperar (a chamada devolve quantos cabem), então
nenhum programa trava por causa do som.

**O bip pela placa.** As listas de notas do B3 (comando `bip`, Sagui Pega
Banana, Sagui 3D) viram ondas quadradas suaves na placa quando ela existe; sem
placa, continuam no alto-falante do PC. Os jogos ganharam a placa de som sem
mudar uma linha.

**Testes.** O QEMU grava a saída da placa num `.wav`; o teste mede a frequência
(cruzamentos do zero) e a mistura (algoritmo de Goertzel para cada frequência).
O canto do sagui (`/sons/sagui.wav`) é sintetizado por `tools/mksom.py`.

## D034. Rede: tudo por consulta, e a pendência do G470 (H43 a H47)

A placa Intel PRO/1000 (a das máquinas virtuais) trabalha com dois anéis de
descritores na memória; o driver não usa interrupções: a tarefa da rede acorda
a cada tique (10 ms), tira o que chegou e cuida dos relógios (DHCP, ARP, TCP).
Com 10 ms de folga, o atraso extra é pequeno e o código fica muito mais simples
e previsível. A pilha guarda os endereços IPv4 "na ordem da rede" (o primeiro
número no byte mais baixo), o que evita converter em cada comparação.

O DNS fica fora do kernel (na `libsagui`), como manda o PRD: é só uma pergunta
UDP para o servidor que o DHCP informou. Os nomes "comprimidos" das respostas
são pulados sem montar o texto, porque só o endereço interessa.

**H46 (rede no G470) ficou pendente.** Depende da foto do comando `pci` no
notebook: se o chip de rede tiver manual público, ganha driver próprio; se não,
o plano B é um adaptador USB-Ethernet da classe CDC (especificação pública). O
adaptador USB que o QEMU simula é de velocidade plena (USB 1.1) e o driver EHCI
do SaguiOS só fala com dispositivos de alta velocidade, então esse caminho não
dá para testar no QEMU como o PRD supunha: ele será testado com o adaptador de
verdade.
