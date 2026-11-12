// Gerado por tools/status.py a partir de status.json. Não editar.
window.SAGUI_STATUS = {
  "titulo": "SaguiOS: um sistema operacional escrito do zero por IA",
  "atualizado": "2026-11-12T14:10:45",
  "situacao": "Fase 7 confirmada no hardware. Seguindo para a fase 8.",
  "fase_atual": 7,
  "historia_atual": null,
  "bloqueio": "",
  "mensagem_fase": "",
  "fases": [
    {
      "numero": 1,
      "nome": "Boot",
      "estado": "feito",
      "progresso": 100.0,
      "confirmado": "2026-11-03T15:22:25"
    },
    {
      "numero": 2,
      "nome": "Kernel e terminal",
      "estado": "feito",
      "progresso": 100.0,
      "confirmado": "2026-11-03T16:05:16"
    },
    {
      "numero": 3,
      "nome": "Processos e programas",
      "estado": "feito",
      "progresso": 100.0,
      "confirmado": "2026-11-03T17:21:51"
    },
    {
      "numero": 4,
      "nome": "Gráfico e mouse",
      "estado": "feito",
      "progresso": 100.0,
      "confirmado": "2026-11-03T18:37:28"
    },
    {
      "numero": 5,
      "nome": "Janelas e programas",
      "estado": "feito",
      "progresso": 100.0,
      "confirmado": "2026-11-03T20:00:46"
    },
    {
      "numero": 6,
      "nome": "Pendrive de dados",
      "estado": "feito",
      "progresso": 100.0,
      "confirmado": "2026-11-03T22:57:09"
    },
    {
      "numero": 7,
      "nome": "Bônus (B3 bip, B4 Sagui 3D)",
      "estado": "feito",
      "progresso": 100.0,
      "confirmado": "2026-11-04T00:19:30",
      "opcional": true
    }
  ],
  "historias": [
    {
      "id": "H01",
      "fase": 1,
      "titulo": "Preparar o ambiente",
      "objetivo": "Instalar NASM, QEMU, ImageMagick, Python; validar o GCC freestanding; criar pastas, git, status.md e dashboard.",
      "aceite": "`make` roda sem erro com um alvo vazio; dashboard abre no navegador.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T14:12:34",
      "fim": "2026-11-03T14:14:17",
      "minutos": 2,
      "resultado": "Ambiente pronto: NASM, GCC -m32, QEMU, ImageMagick; estrutura, git e painel criados"
    },
    {
      "id": "H02",
      "fase": 1,
      "titulo": "Estágio 1 (MBR)",
      "objetivo": "Código de 512 bytes com assinatura 0x55AA, tabela de partição válida com partição ativa e área de BPB reservada.",
      "aceite": "No QEMU (IDE e USB), aparece a mensagem `SaguiOS: carregando...`.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T14:14:29",
      "fim": "2026-11-03T14:22:05",
      "minutos": 8,
      "resultado": "MBR de 512 bytes com BPB reservado e partição ativa; mensagem aparece em IDE e USB"
    },
    {
      "id": "H03",
      "fase": 1,
      "titulo": "Estágio 2 e modo protegido",
      "objetivo": "A20, mapa E820, leitura do kernel e do ramdisk, GDT, modo protegido, salto para o kernel com boot_info.",
      "aceite": "O kernel em C escreve em modo texto VGA `Ola! Eu sou o SaguiOS.` e a memória detectada.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T14:22:05",
      "fim": "2026-11-03T14:22:52",
      "minutos": 1,
      "resultado": "Estágio 2 completo e kernel em C em modo protegido; testado em IDE e USB com 512 e 128 MB"
    },
    {
      "id": "H04",
      "fase": 1,
      "titulo": "Imagem para pendrive e VirtualBox",
      "objetivo": "Build gera build/saguios.img com SHA-256 e build/saguios.vdi; docs/como-gravar.md.",
      "aceite": "Boot nos dois modos do QEMU; .vdi gerado; arquivos em entrega/fase1/; como-gravar.md com Rufus (DD) e balenaEtcher.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T14:22:52",
      "fim": "2026-11-03T14:27:10",
      "minutos": 4,
      "resultado": "Imagem .img e .vdi com SHA-256; boot do .img (IDE e USB) e do .vdi no QEMU; como-gravar.md"
    },
    {
      "id": "H05",
      "fase": 2,
      "titulo": "Interrupções e relógio",
      "objetivo": "IDT, exceções da CPU com nome e registradores, remapeamento do PIC, PIT a 100 Hz, contador de tempo.",
      "aceite": "Divisão por zero mostra a tela de erro do SaguiOS; o contador de segundos avança.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T15:24:57",
      "fim": "2026-11-03T15:28:28",
      "minutos": 4,
      "resultado": "IDT com 256 vetores, tela de erro, PIC remapeado, PIT a 100 Hz, CMOS"
    },
    {
      "id": "H06",
      "fase": 2,
      "titulo": "Memória física",
      "objetivo": "Bitmap de páginas de 4 KB a partir do E820 e alocador kmalloc/kfree.",
      "aceite": "Teste aloca e libera blocos de vários tamanhos sem corromper; diagnóstico mostra memória total e livre.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T15:28:28",
      "fim": "2026-11-03T15:32:06",
      "minutos": 4,
      "resultado": "Bitmap de páginas a partir do E820 e kmalloc por classes; autoteste de 450 blocos sem corrupção"
    },
    {
      "id": "H07",
      "fase": 2,
      "titulo": "Teclado PS/2 com ABNT2",
      "objetivo": "Driver no IRQ1, buffer, Shift, Caps Lock, Ctrl, teclas mortas e o mapa ABNT2.",
      "aceite": "Teclas enviadas pelo monitor do QEMU aparecem corretas, incluindo ç.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T15:32:06",
      "fim": "2026-11-03T15:37:48",
      "minutos": 6,
      "resultado": "Driver PS/2 com ABNT2 completo, teclas mortas, AltGr, Caps Lock; 25 grupos de teclas conferidos byte a byte"
    },
    {
      "id": "H08",
      "fase": 2,
      "titulo": "Sistema de arquivos SGFS",
      "objetivo": "tools/mksgfs.py e leitor no kernel: listar pasta, abrir e ler arquivo.",
      "aceite": "Teste interno lista as entradas do rootfs/ e lê o leia-me.txt inteiro.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T15:37:48",
      "fim": "2026-11-03T15:39:25",
      "minutos": 2,
      "resultado": "mksgfs.py + leitor no kernel; lista igual ao rootfs/ e leia-me.txt idêntico"
    },
    {
      "id": "H09",
      "fase": 2,
      "titulo": "Terminal provisório do kernel",
      "objetivo": "Prompt sagui:/$ com ajuda, ls, cd, cat, limpar, mem, data, reiniciar.",
      "aceite": "Roteiro digita cada comando no QEMU e confere pela captura e pela serial.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T15:39:25",
      "fim": "2026-11-03T15:41:51",
      "minutos": 2,
      "resultado": "Terminal provisório com ajuda, ls, cd, cat (paginado), limpar, mem, data, tempo, reiniciar"
    },
    {
      "id": "H10",
      "fase": 3,
      "titulo": "Paginação",
      "objetivo": "Diretório e tabelas de páginas, kernel em todos os espaços, page fault com mensagem clara.",
      "aceite": "Dois espaços de endereço com valores diferentes no mesmo endereço virtual; acesso inválido gera a tela de page fault.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T16:07:26",
      "fim": "2026-11-03T16:10:05",
      "minutos": 3,
      "resultado": "Paginação de 2 níveis, espaços por processo, tela de falta de página"
    },
    {
      "id": "H11",
      "fase": 3,
      "titulo": "Processos e escalonador",
      "objetivo": "PCB, troca de contexto em assembly, round-robin preemptivo no PIT, processo ocioso.",
      "aceite": "Duas tarefas imprimem A e B alternados na serial sem cooperar.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T16:10:05",
      "fim": "2026-11-03T16:12:46",
      "minutos": 3,
      "resultado": "Escalonador round-robin preemptivo; tarefas A e B alternam a cada 20 ms sem cooperar"
    },
    {
      "id": "H12",
      "fase": 3,
      "titulo": "Ring 3 e chamadas de sistema",
      "objetivo": "TSS, segmentos de usuário, ring 3, int 0x80, validação de ponteiros.",
      "aceite": "Programa em ring 3 escreve `Ola do ring 3!` e sai; `cli` no ring 3 encerra só aquele processo.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T16:12:46",
      "fim": "2026-11-03T16:16:35",
      "minutos": 4,
      "resultado": "Programas no anel 3, int 0x80 com validação de ponteiros; cli e acesso ao kernel encerram só o processo"
    },
    {
      "id": "H13",
      "fase": 3,
      "titulo": "Carregador ELF e libsagui",
      "objetivo": "Leitor de ELF32 estático, pilha do usuário com argumentos, libsagui com printf.",
      "aceite": "O kernel carrega bin/ola do SGFS e ele imprime os argumentos.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T16:16:35",
      "fim": "2026-11-03T16:21:08",
      "minutos": 5,
      "resultado": "Carregador ELF32 e libsagui; /bin/ola imprime os argumentos"
    },
    {
      "id": "H14",
      "fase": 3,
      "titulo": "Arquivos e pastas graváveis",
      "objetivo": "Camada gravável na RAM sobre o SGFS, tabela de arquivos por processo, terminal como 0 e 1.",
      "aceite": "Teste em ring 3 cria, escreve, relê e apaga tmp/teste.txt; o mesmo com uma pasta.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T16:21:08",
      "fim": "2026-11-03T16:25:17",
      "minutos": 4,
      "resultado": "Árvore na RAM com cópia na escrita, descritores por processo, terminal como 0/1; 28 verificações no anel 3"
    },
    {
      "id": "H15",
      "fase": 3,
      "titulo": "Pipes",
      "objetivo": "pipe e duplicar, buffer no kernel, bloqueio de leitor e escritor, EOF.",
      "aceite": "Dois processos transferem por pipe um texto maior que o buffer sem perda nem duplicação.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T16:25:17",
      "fim": "2026-11-03T16:27:11",
      "minutos": 2,
      "resultado": "Pipes com bloqueio e fim de arquivo; 20.000 bytes num pipe de 512, nos dois sentidos"
    },
    {
      "id": "H16",
      "fase": 3,
      "titulo": "Shell e comandos como programas",
      "objetivo": "bin/sagsh como primeiro processo, redirecionamento, pipe, histórico, Tab, comandos e apelidos.",
      "aceite": "Roteiro executa todos os comandos e apelidos; falha derruba só o processo; capturas em docs/capturas/.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T16:27:11",
      "fim": "2026-11-03T16:41:25",
      "minutos": 14,
      "resultado": "sagsh no anel 3 com redirecionamento, pipes, histórico e Tab; 18 programas com apelidos; terminal provisório removido"
    },
    {
      "id": "H17",
      "fase": 4,
      "titulo": "Framebuffer VESA",
      "objetivo": "Troca para o modo VESA, double buffer e primitivas de desenho.",
      "aceite": "Captura com fundo colorido e formas; se o VESA falhar, volta ao modo texto e avisa.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T17:22:03",
      "fim": "2026-11-03T17:24:26",
      "minutos": 2,
      "resultado": "Framebuffer VESA 1024x768x32 com buffer duplo e primitivas de desenho"
    },
    {
      "id": "H18",
      "fase": 4,
      "titulo": "Fonte própria",
      "objetivo": "Fonte bitmap 8x16 própria com ASCII e acentos Latin-1; terminal gráfico.",
      "aceite": "Captura mostra `Ação, vovô, pão, ÇÃO, à é í ó ú` correto.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T17:24:26",
      "fim": "2026-11-03T17:32:50",
      "minutos": 8,
      "resultado": "Fonte 8x16 própria com acentos, terminal gráfico 128x48, edição com setas"
    },
    {
      "id": "H19",
      "fase": 4,
      "titulo": "Mouse / touchpad PS/2",
      "objetivo": "Porta auxiliar do i8042, IRQ12, pacotes de 3 bytes, cursor próprio, botões.",
      "aceite": "Movimentos do monitor do QEMU movem o cursor; clique registrado na serial.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T17:32:50",
      "fim": "2026-11-03T18:27:00",
      "minutos": 54,
      "resultado": "Mouse/touchpad PS/2; H19-fix: módulo i8042 único, teclado independente do touchpad"
    },
    {
      "id": "H20",
      "fase": 5,
      "titulo": "Área de trabalho",
      "objetivo": "Degradê nas cores do Brasil, mascote sagui em pixel art, barra superior com relógio.",
      "aceite": "Captura da área de trabalho em docs/capturas/.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T18:38:00",
      "fim": "2026-11-03T18:46:46",
      "minutos": 9,
      "resultado": "Área de trabalho com degradê do Brasil, mascote em pixel art, barra com relógio e ícones"
    },
    {
      "id": "H21",
      "fase": 5,
      "titulo": "Gerenciador de janelas",
      "objetivo": "Janelas com moldura, arrastar, foco; chamadas de janela para programas em ring 3.",
      "aceite": "Dois programas abrem janelas; o teste arrasta uma, fecha a outra e o programa termina.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T18:46:46",
      "fim": "2026-11-03T18:53:31",
      "minutos": 7,
      "resultado": "Gerenciador de janelas no kernel, memória compartilhada, eventos por janela; arrastar e fechar testados"
    },
    {
      "id": "H22",
      "fase": 5,
      "titulo": "Kit gráfico da libsagui",
      "objetivo": "Texto, botão, rótulo, caixa de texto, área de texto, lista, confirmação.",
      "aceite": "Demonstração com todos os componentes funcionando; captura.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T18:53:31",
      "fim": "2026-11-03T18:56:46",
      "minutos": 3,
      "resultado": "Kit gráfico na libsagui: botão, caixa, área de texto com rolagem, lista, diálogos"
    },
    {
      "id": "H23",
      "fase": 5,
      "titulo": "Arquivos",
      "objetivo": "Gerenciador de arquivos em ring 3 com ícones, duplo clique, voltar, criar e apagar pasta.",
      "aceite": "Navega até historia/, abre linux.txt no Editor, cria e apaga pasta em tmp/.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T19:03:23",
      "fim": "2026-11-03T19:07:26",
      "minutos": 4,
      "resultado": "Gerenciador de arquivos com ícones; abre no Editor, cria e apaga pastas"
    },
    {
      "id": "H24",
      "fase": 5,
      "titulo": "Terminal em janela",
      "objetivo": "Janela que roda o sagsh ligado por pipes; comandos que abrem os programas gráficos.",
      "aceite": "Captura do terminal em janela rodando ps e ls bin | conta.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T18:56:46",
      "fim": "2026-11-03T19:00:05",
      "minutos": 3,
      "resultado": "Terminal em janela rodando o sagsh por pipes; comandos que abrem os programas gráficos"
    },
    {
      "id": "H25",
      "fase": 5,
      "titulo": "Editor de texto",
      "objetivo": "Novo, abrir, salvar, salvar como, acentos, setas, Ctrl+S, asterisco, aviso ao fechar.",
      "aceite": "Cria tmp/nota.txt com `Olá, SaguiOS! Ação e coração.`, reabre pelo Arquivos e confere.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T19:00:05",
      "fim": "2026-11-03T19:03:23",
      "minutos": 3,
      "resultado": "Editor com Novo/Abrir/Salvar, Ctrl+S, título com *, aviso ao fechar"
    },
    {
      "id": "H26",
      "fase": 5,
      "titulo": "Calculadora",
      "objetivo": "Botões e teclado, ponto fixo, avisos de divisão por zero e número grande.",
      "aceite": "12,5 × 4 = 50; 7 ÷ 2 = 3,5; 1 ÷ 0 mostra aviso; captura.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T19:07:26",
      "fim": "2026-11-03T19:09:30",
      "minutos": 2,
      "resultado": "Calculadora em ponto fixo com botões e teclado; avisos de divisão por zero e número grande"
    },
    {
      "id": "H27",
      "fase": 5,
      "titulo": "Sagui Pega Banana",
      "objetivo": "Jogo autoral: sagui pega bananas, 3 vidas, velocidade crescente, recorde.",
      "aceite": "Teste joga com setas pelo monitor; capturas do jogo e do fim de jogo.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T19:09:30",
      "fim": "2026-11-03T19:11:50",
      "minutos": 2,
      "resultado": "Jogo Sagui Pega Banana com pixel art, vidas, velocidade crescente e recorde"
    },
    {
      "id": "H28",
      "fase": 5,
      "titulo": "Acabamento",
      "objetivo": "Ícones na área de trabalho e tela Sobre o SaguiOS.",
      "aceite": "Captura com três programas abertos; tamanho da imagem medido.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T19:11:50",
      "fim": "2026-11-03T19:14:10",
      "minutos": 2,
      "resultado": "Ícones que abrem os programas, tela Sobre; imagem com 428 KB (29% da meta)"
    },
    {
      "id": "H29",
      "fase": 6,
      "titulo": "PCI e controlador EHCI",
      "objetivo": "Varredura PCI, passagem de controle da BIOS, reset do EHCI, portas.",
      "aceite": "Diagnóstico lista controladores EHCI e portas com dispositivo no QEMU.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T20:16:48",
      "fim": "2026-11-03T20:16:48",
      "minutos": 1,
      "resultado": "PCI, EHCI assumido da BIOS, transferências; comando usb"
    },
    {
      "id": "H30",
      "fase": 6,
      "titulo": "Enumeração USB, hubs e conexão a quente",
      "objetivo": "Transferências de controle, descritores, endereços, hubs, conexão e remoção.",
      "aceite": "Com hub virtual, lista fabricante, produto e classe; eventos de conexão e remoção na serial.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T20:16:48",
      "fim": "2026-11-03T20:16:48",
      "minutos": 1,
      "resultado": "Enumeração, hubs (validação no G470), conexão e remoção a quente"
    },
    {
      "id": "H31",
      "fase": 6,
      "titulo": "Armazenamento em massa",
      "objetivo": "Bulk-Only Transport e SCSI: identificar, capacidade, ler e gravar.",
      "aceite": "Lê e grava setores numa imagem de teste e confere byte a byte na VPS.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T20:16:48",
      "fim": "2026-11-03T20:16:48",
      "minutos": 1,
      "resultado": "Bulk-Only + SCSI; setores lidos e gravados conferidos byte a byte na VPS"
    },
    {
      "id": "H32",
      "fase": 6,
      "titulo": "FAT32: leitura",
      "objetivo": "Partições MBR, FAT32 com nomes curtos e longos, montagem em /usb.",
      "aceite": "Pendrive de teste com pastas, acentos e nomes longos é lido por ls e cat.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T20:16:48",
      "fim": "2026-11-03T20:18:39",
      "minutos": 2,
      "resultado": "FAT32 com nomes longos e acentos montado em /usb; ls e cat funcionando"
    },
    {
      "id": "H33",
      "fase": 6,
      "titulo": "FAT32: gravação",
      "objetivo": "Criar, sobrescrever, aumentar, apagar; pastas; duas FATs; FSInfo; desmontar grava tudo.",
      "aceite": "Após desmontar, mtools e fsck.vfat confirmam integridade e conteúdo.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T20:18:39",
      "fim": "2026-11-03T20:20:50",
      "minutos": 2,
      "resultado": "Gravação FAT32 conferida com mtools e fsck.vfat"
    },
    {
      "id": "H34",
      "fase": 6,
      "titulo": "Integração do pendrive",
      "objetivo": "Notificação, ícone Pendrive, montar e remover com segurança, fluxo pelo terminal.",
      "aceite": "Roteiro completo pela interface e pelo terminal; VPS confere com mtools; capturas.",
      "estado": "feito",
      "bonus": false,
      "inicio": "2026-11-03T20:20:50",
      "fim": "2026-11-03T20:25:28",
      "minutos": 5,
      "resultado": "Notificação, ícone do pendrive, montar/remover pela interface e pelo terminal; conferido com mtools e fsck"
    },
    {
      "id": "B1",
      "fase": 7,
      "titulo": "Bônus: enxergar o HD interno",
      "objetivo": "Driver SATA somente leitura para listar o HD e partições.",
      "aceite": "discos mostra o HD e partições sem montar nem gravar.",
      "estado": "cancelado",
      "bonus": true,
      "inicio": null,
      "fim": null,
      "minutos": null,
      "resultado": "Cancelado pelo Eli: o SaguiOS nunca grava no HD, então listar as partições não acrescenta nada ao projeto."
    },
    {
      "id": "B2",
      "fase": 7,
      "titulo": "Bônus: boot por UEFI",
      "objetivo": "Carregador UEFI com GOP.",
      "aceite": "Boot em máquina UEFI.",
      "estado": "cancelado",
      "bonus": true,
      "inicio": null,
      "fim": null,
      "minutos": null,
      "resultado": "Cancelado pelo Eli: o objetivo do SaguiOS é rodar em computadores antigos, com BIOS tradicional."
    },
    {
      "id": "B3",
      "fase": 7,
      "titulo": "Bônus: bip",
      "objetivo": "Alto-falante do PC pelo canal 2 do PIT.",
      "aceite": "Bip no jogo e comando bip.",
      "estado": "feito",
      "bonus": true,
      "inicio": "2026-11-03T22:57:35",
      "fim": "2026-11-03T23:02:23",
      "minutos": 5,
      "resultado": "Comando bip, sons no Sagui Pega Banana, fila de notas tocada pelo tique do relógio (D029); canal 2 conferido no QEMU"
    },
    {
      "id": "B4",
      "fase": 7,
      "titulo": "Bônus: Sagui 3D",
      "objetivo": "Jogo autoral em primeira pessoa, visual 2,5D (salas com alturas próprias de chão e teto, paredes em qualquer ângulo), macacos-prego guardas, estilingue de jabuticaba, bananas, cascas, porteira da banana e cacho dourado, sons com o bip.",
      "aceite": "O jogo abre, o mapa carrega, o sagui anda, a semente atinge o inimigo, o macaco escorrega na casca, a saída só abre com o mínimo de bananas, a fase termina na saída; capturas; três fases em arquivos de texto.",
      "estado": "feito",
      "bonus": true,
      "inicio": "2026-11-03T23:02:29",
      "fim": "2026-11-03T23:45:56",
      "minutos": 43,
      "resultado": "Sagui 3D: motor 2,5D próprio, três fases em texto, macacos-prego, sementes, cascas, porteira da banana, cacho dourado, sons com o bip; teste automático e capturas"
    }
  ],
  "tanenbaum": [
    {
      "conceito": "Modo kernel e modo usuário",
      "implementa": "Kernel em ring 0, programas em ring 3",
      "historias": "H03, H12",
      "estado": "feito"
    },
    {
      "conceito": "Processos",
      "implementa": "Criação, término, estados, PID, pai e filho",
      "historias": "H11, H13",
      "estado": "feito"
    },
    {
      "conceito": "Escalonamento",
      "implementa": "Round-robin preemptivo no relógio",
      "historias": "H11",
      "estado": "feito"
    },
    {
      "conceito": "Comunicação entre processos",
      "implementa": "Pipes entre programas (ls | conta)",
      "historias": "H15",
      "estado": "feito"
    },
    {
      "conceito": "Espaços de endereço",
      "implementa": "Paginação com um espaço por processo",
      "historias": "H10",
      "estado": "feito"
    },
    {
      "conceito": "Gerenciamento de memória",
      "implementa": "Memória física, alocador do kernel, memória dos processos",
      "historias": "H06, H10, H13",
      "estado": "feito"
    },
    {
      "conceito": "Arquivos e diretórios",
      "implementa": "Abrir, ler, escrever, criar, apagar, na RAM e no pendrive FAT32",
      "historias": "H08, H14, H32, H33",
      "estado": "feito"
    },
    {
      "conceito": "Entrada e saída",
      "implementa": "Interrupções e drivers próprios: teclado, mouse, vídeo, relógio, USB",
      "historias": "H05, H07, H17, H19, H29-H31",
      "estado": "feito"
    },
    {
      "conceito": "Proteção",
      "implementa": "Um processo não acessa a memória do outro nem o kernel",
      "historias": "H12, H16",
      "estado": "feito"
    },
    {
      "conceito": "Chamadas de sistema",
      "implementa": "Processos, arquivos, diretórios e diversas",
      "historias": "H12",
      "estado": "feito"
    },
    {
      "conceito": "O shell",
      "implementa": "sagsh em ring 3, com redirecionamento e pipes",
      "historias": "H16",
      "estado": "feito"
    },
    {
      "conceito": "Carregamento de programas",
      "implementa": "Executáveis ELF lidos do sistema de arquivos",
      "historias": "H13",
      "estado": "feito"
    },
    {
      "conceito": "Interface gráfica (extra)",
      "implementa": "Gerenciador de janelas no kernel; programas gráficos em ring 3",
      "historias": "H20-H28",
      "estado": "feito"
    }
  ],
  "acoes": [
    {
      "data": "2026-11-04T00:19:30",
      "texto": "Eli confirmou: funcionou tudo perfeitamente (bônus)"
    },
    {
      "data": "2026-11-03T23:45:56",
      "texto": "Fase 7 entregue. Aguardando teste do Eli."
    },
    {
      "data": "2026-11-03T23:45:56",
      "texto": "B4 concluída: Sagui 3D: motor 2,5D próprio, três fases em texto, macacos-prego, sementes, cascas, porteira da banana, cacho dourado, sons com o bip; teste automático e capturas"
    },
    {
      "data": "2026-11-03T23:32:48",
      "texto": "B4 Sagui 3D: motor 2,5D, três fases, macacos-prego, bananas, cascas, porteira e cacho dourado; teste automático passando"
    },
    {
      "data": "2026-11-03T23:02:29",
      "texto": "Início da B4: Bônus: Sagui 3D"
    },
    {
      "data": "2026-11-03T23:02:23",
      "texto": "B3 concluída: Comando bip, sons no Sagui Pega Banana, fila de notas tocada pelo tique do relógio (D029); canal 2 conferido no QEMU"
    },
    {
      "data": "2026-11-03T22:57:35",
      "texto": "Início da B3: Bônus: bip"
    },
    {
      "data": "2026-11-03T22:57:35",
      "texto": "Fase 6 aprovada pelo Eli. Fase bônus replanejada: B1 e B2 cancelados pelo Eli, B3 mantido, B4 (Sagui 3D) novo."
    },
    {
      "data": "2026-11-03T22:57:09",
      "texto": "Eli confirmou: fase 6 ok"
    },
    {
      "data": "2026-11-03T21:56:30",
      "texto": "Fase 6 entregue. Aguardando teste do Eli."
    },
    {
      "data": "2026-11-03T21:55:57",
      "texto": "Erro corrigido: G470: com dois pendrives, o de dados não aparece; usb mostra hub do EHCI 0 parado na configuração"
    },
    {
      "data": "2026-11-03T21:36:41",
      "texto": "Fase 6 entregue. Aguardando teste do Eli."
    },
    {
      "data": "2026-11-03T21:26:31",
      "texto": "Erro corrigido: FAT32 criada depois da partição do SaguiOS não é vista"
    },
    {
      "data": "2026-11-03T21:26:31",
      "texto": "Erro corrigido: G470: pendrive não aparece; usb mostra porta ocupada sem pendrive"
    },
    {
      "data": "2026-11-03T21:21:11",
      "texto": "Bloqueio: G470: o pendrive não aparece (fase 6). Correção em andamento: H30-fix"
    },
    {
      "data": "2026-11-03T20:50:19",
      "texto": "Fase 6 entregue. Aguardando teste do Eli."
    },
    {
      "data": "2026-11-03T20:50:19",
      "texto": "Regressão final: 34/34 testes passaram"
    },
    {
      "data": "2026-11-03T20:25:28",
      "texto": "H34 concluída: Notificação, ícone do pendrive, montar/remover pela interface e pelo terminal; conferido com mtools e fsck"
    },
    {
      "data": "2026-11-03T20:25:27",
      "texto": "Erro corrigido: Acentos trocados em textos UTF-8 do pendrive"
    },
    {
      "data": "2026-11-03T20:20:50",
      "texto": "Início da H34: Integração do pendrive"
    },
    {
      "data": "2026-11-03T20:20:50",
      "texto": "H33 concluída: Gravação FAT32 conferida com mtools e fsck.vfat"
    },
    {
      "data": "2026-11-03T20:18:39",
      "texto": "Início da H33: FAT32: gravação"
    },
    {
      "data": "2026-11-03T20:18:39",
      "texto": "H32 concluída: FAT32 com nomes longos e acentos montado em /usb; ls e cat funcionando"
    },
    {
      "data": "2026-11-03T20:16:48",
      "texto": "Início da H32: FAT32: leitura"
    },
    {
      "data": "2026-11-03T20:16:48",
      "texto": "H31 concluída: Bulk-Only + SCSI; setores lidos e gravados conferidos byte a byte na VPS"
    },
    {
      "data": "2026-11-03T20:16:48",
      "texto": "H30 concluída: Enumeração, hubs (validação no G470), conexão e remoção a quente"
    },
    {
      "data": "2026-11-03T20:16:48",
      "texto": "H29 concluída: PCI, EHCI assumido da BIOS, transferências; comando usb"
    },
    {
      "data": "2026-11-03T20:16:48",
      "texto": "Início da H31: Armazenamento em massa"
    },
    {
      "data": "2026-11-03T20:16:48",
      "texto": "Início da H30: Enumeração USB, hubs e conexão a quente"
    },
    {
      "data": "2026-11-03T20:16:48",
      "texto": "Início da H29: PCI e controlador EHCI"
    },
    {
      "data": "2026-11-03T20:04:57",
      "texto": "Início da H29: PCI e controlador EHCI"
    },
    {
      "data": "2026-11-03T20:00:46",
      "texto": "Eli confirmou: fase 5 ok"
    },
    {
      "data": "2026-11-03T19:24:19",
      "texto": "Fase 5 entregue. Aguardando teste do Eli."
    },
    {
      "data": "2026-11-03T19:24:19",
      "texto": "Todos os testes passaram (H01 a H28, 28/28)"
    },
    {
      "data": "2026-11-03T19:14:10",
      "texto": "H28 concluída: Ícones que abrem os programas, tela Sobre; imagem com 428 KB (29% da meta)"
    },
    {
      "data": "2026-11-03T19:11:50",
      "texto": "Início da H28: Acabamento"
    },
    {
      "data": "2026-11-03T19:11:50",
      "texto": "H27 concluída: Jogo Sagui Pega Banana com pixel art, vidas, velocidade crescente e recorde"
    },
    {
      "data": "2026-11-03T19:09:30",
      "texto": "Início da H27: Sagui Pega Banana"
    },
    {
      "data": "2026-11-03T19:09:30",
      "texto": "H26 concluída: Calculadora em ponto fixo com botões e teclado; avisos de divisão por zero e número grande"
    },
    {
      "data": "2026-11-03T19:07:26",
      "texto": "Início da H26: Calculadora"
    },
    {
      "data": "2026-11-03T19:07:26",
      "texto": "H23 concluída: Gerenciador de arquivos com ícones; abre no Editor, cria e apaga pastas"
    },
    {
      "data": "2026-11-03T19:03:23",
      "texto": "Início da H23: Arquivos"
    },
    {
      "data": "2026-11-03T19:03:23",
      "texto": "H25 concluída: Editor com Novo/Abrir/Salvar, Ctrl+S, título com *, aviso ao fechar"
    },
    {
      "data": "2026-11-03T19:00:05",
      "texto": "Início da H25: Editor de texto"
    },
    {
      "data": "2026-11-03T19:00:05",
      "texto": "H24 concluída: Terminal em janela rodando o sagsh por pipes; comandos que abrem os programas gráficos"
    },
    {
      "data": "2026-11-03T18:56:46",
      "texto": "Início da H24: Terminal em janela"
    },
    {
      "data": "2026-11-03T18:56:46",
      "texto": "H22 concluída: Kit gráfico na libsagui: botão, caixa, área de texto com rolagem, lista, diálogos"
    },
    {
      "data": "2026-11-03T18:53:31",
      "texto": "Início da H22: Kit gráfico da libsagui"
    },
    {
      "data": "2026-11-03T18:53:31",
      "texto": "H21 concluída: Gerenciador de janelas no kernel, memória compartilhada, eventos por janela; arrastar e fechar testados"
    },
    {
      "data": "2026-11-03T18:53:30",
      "texto": "Erro corrigido: Rastros ao arrastar janelas"
    },
    {
      "data": "2026-11-03T18:46:46",
      "texto": "Início da H21: Gerenciador de janelas"
    },
    {
      "data": "2026-11-03T18:46:46",
      "texto": "H20 concluída: Área de trabalho com degradê do Brasil, mascote em pixel art, barra com relógio e ícones"
    },
    {
      "data": "2026-11-03T18:46:46",
      "texto": "Erro corrigido: Boot não terminava com a área de trabalho no ar"
    },
    {
      "data": "2026-11-03T18:38:00",
      "texto": "Início da H20: Área de trabalho"
    },
    {
      "data": "2026-11-03T18:37:28",
      "texto": "Eli confirmou: fase 4 ok"
    },
    {
      "data": "2026-11-03T18:27:00",
      "texto": "Fase 4 entregue. Aguardando teste do Eli."
    },
    {
      "data": "2026-11-03T18:27:00",
      "texto": "H19 concluída: Mouse/touchpad PS/2; H19-fix: módulo i8042 único, teclado independente do touchpad"
    },
    {
      "data": "2026-11-03T18:12:39",
      "texto": "Erro corrigido: G470: teclado parou e o touchpad não respondeu (fase 4)"
    },
    {
      "data": "2026-11-03T18:08:12",
      "texto": "Bloqueio: G470: teclado não responde na fase 4 e o touchpad não respondeu (correção em andamento: H19-fix)"
    },
    {
      "data": "2026-11-03T17:40:18",
      "texto": "Fase 4 entregue. Aguardando teste do Eli."
    }
  ],
  "erros": [
    {
      "data": "2026-11-03T21:55:57",
      "sintoma": "G470: com dois pendrives, o de dados não aparece; usb mostra hub do EHCI 0 parado na configuração",
      "causa": "os dois controladores EHCI compartilhavam o mesmo anel de transferências",
      "correcao": "uma página de estruturas por controlador; teste com dois EHCI no QEMU"
    },
    {
      "data": "2026-11-03T21:26:31",
      "sintoma": "FAT32 criada depois da partição do SaguiOS não é vista",
      "causa": "só a primeira partição do MBR era examinada",
      "correcao": "as quatro partições são examinadas; vale a primeira FAT32"
    },
    {
      "data": "2026-11-03T21:26:31",
      "sintoma": "G470: pendrive não aparece; usb mostra porta ocupada sem pendrive",
      "causa": "EHCI de 64 bits exige qTD/QH maiores (apêndice B); o driver usava o formato de 32 bits",
      "correcao": "qTD 64 bytes e QH 96 com palavras altas zeradas; diagnóstico no comando usb (D028)"
    },
    {
      "data": "2026-11-03T20:25:27",
      "sintoma": "Acentos trocados em textos UTF-8 do pendrive",
      "causa": "SaguiOS usa Latin-1; arquivos de fora vêm em UTF-8",
      "correcao": "Conversão UTF-8 -> Latin-1 no cat e no Editor"
    },
    {
      "data": "2026-11-03T18:53:30",
      "sintoma": "Rastros ao arrastar janelas",
      "causa": "Contorno de 1 px fora da área redesenhada",
      "correcao": "Área redesenhada inclui o contorno"
    },
    {
      "data": "2026-11-03T18:46:46",
      "sintoma": "Boot não terminava com a área de trabalho no ar",
      "causa": "Tarefa do gerenciador sempre pronta; processo 0 sem CPU",
      "correcao": "Interrupção 0x81 para tarefa do kernel ceder a vez"
    },
    {
      "data": "2026-11-03T18:12:39",
      "sintoma": "G470: teclado parou e o touchpad não respondeu (fase 4)",
      "causa": "Inicialização do mouse podia gravar configuração errada no controlador e deixar um byte do touchpad travando a fila do teclado",
      "correcao": "Módulo i8042 único, fila esvaziada também pelo relógio, configuração conferida, porta do mouse desligada se ele não responder"
    },
    {
      "data": "2026-11-03T16:41:24",
      "sintoma": "falha divisao não dividia",
      "causa": "GCC otimiza 1/x sem a instrução div",
      "correcao": "Dividendo 12345"
    },
    {
      "data": "2026-11-03T16:25:45",
      "sintoma": "Regressão: 'Ola do ring 3!' sumiu no teste da H12",
      "causa": "Programas criados sem o carregador ficaram sem descritores após a H14",
      "correcao": "Autoteste dá o terminal como 0, 1 e 2"
    },
    {
      "data": "2026-11-03T15:41:51",
      "sintoma": "cd ../arte chegava como cd ..;arte",
      "causa": "Harness digitava / pela tecla americana (no ABNT2 é ;)",
      "correcao": "Harness usa posições ABNT2"
    },
    {
      "data": "2026-11-03T15:39:25",
      "sintoma": "Saudação e memória quebravam linha",
      "causa": "Textos com mais de 80 colunas",
      "correcao": "Larguras fixas que somam 80"
    },
    {
      "data": "2026-11-03T15:39:25",
      "sintoma": "Árvore do SGFS saiu como %*s na tela",
      "causa": "formatar() não aceitava largura com *",
      "correcao": "Suporte a * no formatar()"
    }
  ],
  "capturas": [
    {
      "arquivo": "docs/capturas/H23-confirmar-apagar.png",
      "nome": "H23-confirmar-apagar.png",
      "data": "2026-11-03T23:45:38"
    },
    {
      "arquivo": "docs/capturas/H23-nota-reaberta.png",
      "nome": "H23-nota-reaberta.png",
      "data": "2026-11-03T23:45:30"
    },
    {
      "arquivo": "docs/capturas/H23-linux-no-editor.png",
      "nome": "H23-linux-no-editor.png",
      "data": "2026-11-03T23:45:22"
    },
    {
      "arquivo": "docs/capturas/H23-arquivos-raiz.png",
      "nome": "H23-arquivos-raiz.png",
      "data": "2026-11-03T23:45:18"
    },
    {
      "arquivo": "docs/capturas/H24-terminal-ps-conta.png",
      "nome": "H24-terminal-ps-conta.png",
      "data": "2026-11-03T23:44:16"
    },
    {
      "arquivo": "docs/capturas/H16-tab-historico.png",
      "nome": "H16-tab-historico.png",
      "data": "2026-11-03T23:43:39"
    },
    {
      "arquivo": "docs/capturas/H16-falha-protecao.png",
      "nome": "H16-falha-protecao.png",
      "data": "2026-11-03T23:43:34"
    },
    {
      "arquivo": "docs/capturas/H16-contador-e-ps.png",
      "nome": "H16-contador-e-ps.png",
      "data": "2026-11-03T23:43:22"
    },
    {
      "arquivo": "docs/capturas/H16-arquivos-redirecionamento-pipe.png",
      "nome": "H16-arquivos-redirecionamento-pipe.png",
      "data": "2026-11-03T23:43:19"
    },
    {
      "arquivo": "docs/capturas/H16-ajuda.png",
      "nome": "H16-ajuda.png",
      "data": "2026-11-03T23:43:02"
    },
    {
      "arquivo": "docs/capturas/B4-7-macaco-escorregou.png",
      "nome": "B4-7-macaco-escorregou.png",
      "data": "2026-11-03T23:42:31"
    },
    {
      "arquivo": "docs/capturas/B4-6-macaco-tonto.png",
      "nome": "B4-6-macaco-tonto.png",
      "data": "2026-11-03T23:42:28"
    },
    {
      "arquivo": "docs/capturas/B4-5-vitoria.png",
      "nome": "B4-5-vitoria.png",
      "data": "2026-11-03T23:42:20"
    },
    {
      "arquivo": "docs/capturas/B4-4-porteira-aberta.png",
      "nome": "B4-4-porteira-aberta.png",
      "data": "2026-11-03T23:42:15"
    },
    {
      "arquivo": "docs/capturas/B4-3-fase1-corredor.png",
      "nome": "B4-3-fase1-corredor.png",
      "data": "2026-11-03T23:42:07"
    },
    {
      "arquivo": "docs/capturas/B4-2-fase1-clareira.png",
      "nome": "B4-2-fase1-clareira.png",
      "data": "2026-11-03T23:42:04"
    },
    {
      "arquivo": "docs/capturas/B4-1-tela-de-inicio.png",
      "nome": "B4-1-tela-de-inicio.png",
      "data": "2026-11-03T23:42:02"
    },
    {
      "arquivo": "docs/capturas/B3-comando-bip.png",
      "nome": "B3-comando-bip.png",
      "data": "2026-11-03T23:41:42"
    },
    {
      "arquivo": "docs/capturas/H34-8-um-pendrive-so.png",
      "nome": "H34-8-um-pendrive-so.png",
      "data": "2026-11-03T23:41:29"
    },
    {
      "arquivo": "docs/capturas/H34-7-terminal.png",
      "nome": "H34-7-terminal.png",
      "data": "2026-11-03T23:41:19"
    },
    {
      "arquivo": "docs/capturas/H34-6-removido-sem-desmontar.png",
      "nome": "H34-6-removido-sem-desmontar.png",
      "data": "2026-11-03T23:41:09"
    },
    {
      "arquivo": "docs/capturas/H34-5-pendrive-removido.png",
      "nome": "H34-5-pendrive-removido.png",
      "data": "2026-11-03T23:41:03"
    },
    {
      "arquivo": "docs/capturas/H34-4-pode-remover.png",
      "nome": "H34-4-pode-remover.png",
      "data": "2026-11-03T23:41:02"
    },
    {
      "arquivo": "docs/capturas/H34-3-editor-salvou-no-pendrive.png",
      "nome": "H34-3-editor-salvou-no-pendrive.png",
      "data": "2026-11-03T23:40:58"
    },
    {
      "arquivo": "docs/capturas/H34-2-arquivos-no-pendrive.png",
      "nome": "H34-2-arquivos-no-pendrive.png",
      "data": "2026-11-03T23:40:52"
    },
    {
      "arquivo": "docs/capturas/H34-1-pendrive-conectado.png",
      "nome": "H34-1-pendrive-conectado.png",
      "data": "2026-11-03T23:40:49"
    },
    {
      "arquivo": "docs/capturas/H33-pendrive-gravado.png",
      "nome": "H33-pendrive-gravado.png",
      "data": "2026-11-03T23:40:37"
    },
    {
      "arquivo": "docs/capturas/H32-ls-pendrive.png",
      "nome": "H32-ls-pendrive.png",
      "data": "2026-11-03T23:39:47"
    },
    {
      "arquivo": "docs/capturas/H30-conexao-a-quente.png",
      "nome": "H30-conexao-a-quente.png",
      "data": "2026-11-03T23:39:21"
    },
    {
      "arquivo": "docs/capturas/H29-diagnostico-usb.png",
      "nome": "H29-diagnostico-usb.png",
      "data": "2026-11-03T23:39:05"
    },
    {
      "arquivo": "docs/capturas/H29-dois-controladores.png",
      "nome": "H29-dois-controladores.png",
      "data": "2026-11-03T23:38:58"
    },
    {
      "arquivo": "docs/capturas/H28-tres-programas.png",
      "nome": "H28-tres-programas.png",
      "data": "2026-11-03T23:38:49"
    },
    {
      "arquivo": "docs/capturas/H28-programas-abertos.png",
      "nome": "H28-programas-abertos.png",
      "data": "2026-11-03T23:38:42"
    },
    {
      "arquivo": "docs/capturas/H27-banana-fim-de-jogo.png",
      "nome": "H27-banana-fim-de-jogo.png",
      "data": "2026-11-03T23:38:25"
    },
    {
      "arquivo": "docs/capturas/H27-banana-jogando.png",
      "nome": "H27-banana-jogando.png",
      "data": "2026-11-03T23:38:15"
    },
    {
      "arquivo": "docs/capturas/H27-banana-inicio.png",
      "nome": "H27-banana-inicio.png",
      "data": "2026-11-03T23:38:13"
    },
    {
      "arquivo": "docs/capturas/H26-calculadora-divisao-por-zero.png",
      "nome": "H26-calculadora-divisao-por-zero.png",
      "data": "2026-11-03T23:38:02"
    },
    {
      "arquivo": "docs/capturas/H26-calculadora-3-5.png",
      "nome": "H26-calculadora-3-5.png",
      "data": "2026-11-03T23:37:55"
    },
    {
      "arquivo": "docs/capturas/H26-calculadora-50.png",
      "nome": "H26-calculadora-50.png",
      "data": "2026-11-03T23:37:54"
    },
    {
      "arquivo": "docs/capturas/H25-aviso-ao-fechar.png",
      "nome": "H25-aviso-ao-fechar.png",
      "data": "2026-11-03T23:37:34"
    },
    {
      "arquivo": "docs/capturas/H25-editor-reaberto.png",
      "nome": "H25-editor-reaberto.png",
      "data": "2026-11-03T23:37:31"
    },
    {
      "arquivo": "docs/capturas/H25-editor-digitando.png",
      "nome": "H25-editor-digitando.png",
      "data": "2026-11-03T23:37:25"
    },
    {
      "arquivo": "docs/capturas/H22-dialogo-confirmar.png",
      "nome": "H22-dialogo-confirmar.png",
      "data": "2026-11-03T23:36:51"
    },
    {
      "arquivo": "docs/capturas/H22-componentes-em-uso.png",
      "nome": "H22-componentes-em-uso.png",
      "data": "2026-11-03T23:36:49"
    },
    {
      "arquivo": "docs/capturas/H22-componentes.png",
      "nome": "H22-componentes.png",
      "data": "2026-11-03T23:36:39"
    },
    {
      "arquivo": "docs/capturas/H21-janela-fechada.png",
      "nome": "H21-janela-fechada.png",
      "data": "2026-11-03T23:36:33"
    },
    {
      "arquivo": "docs/capturas/H21-janela-arrastada.png",
      "nome": "H21-janela-arrastada.png",
      "data": "2026-11-03T23:36:29"
    },
    {
      "arquivo": "docs/capturas/H21-duas-janelas.png",
      "nome": "H21-duas-janelas.png",
      "data": "2026-11-03T23:36:27"
    },
    {
      "arquivo": "docs/capturas/H20-area-de-trabalho.png",
      "nome": "H20-area-de-trabalho.png",
      "data": "2026-11-03T23:36:13"
    },
    {
      "arquivo": "docs/capturas/H19-cursor-movido.png",
      "nome": "H19-cursor-movido.png",
      "data": "2026-11-03T23:35:56"
    },
    {
      "arquivo": "docs/capturas/H18-edicao-e-segundo-plano.png",
      "nome": "H18-edicao-e-segundo-plano.png",
      "data": "2026-11-03T23:35:40"
    },
    {
      "arquivo": "docs/capturas/H18-fonte-acentos.png",
      "nome": "H18-fonte-acentos.png",
      "data": "2026-11-03T23:35:35"
    },
    {
      "arquivo": "docs/capturas/H17-formas-ide.png",
      "nome": "H17-formas-ide.png",
      "data": "2026-11-03T23:35:12"
    },
    {
      "arquivo": "docs/capturas/H15-pipes.png",
      "nome": "H15-pipes.png",
      "data": "2026-11-03T23:34:57"
    },
    {
      "arquivo": "docs/capturas/H14-arquivos-gravaveis.png",
      "nome": "H14-arquivos-gravaveis.png",
      "data": "2026-11-03T23:34:51"
    },
    {
      "arquivo": "docs/capturas/H13-ola-argumentos.png",
      "nome": "H13-ola-argumentos.png",
      "data": "2026-11-03T23:34:39"
    },
    {
      "arquivo": "docs/capturas/H12-anel3-protecao.png",
      "nome": "H12-anel3-protecao.png",
      "data": "2026-11-03T23:34:33"
    },
    {
      "arquivo": "docs/capturas/H11-tarefas-A-B.png",
      "nome": "H11-tarefas-A-B.png",
      "data": "2026-11-03T23:34:27"
    },
    {
      "arquivo": "docs/capturas/H10-tela-falta-de-pagina.png",
      "nome": "H10-tela-falta-de-pagina.png",
      "data": "2026-11-03T23:34:20"
    },
    {
      "arquivo": "docs/capturas/H10-dois-espacos.png",
      "nome": "H10-dois-espacos.png",
      "data": "2026-11-03T23:34:14"
    },
    {
      "arquivo": "docs/capturas/H09-tela-inicial-usb.png",
      "nome": "H09-tela-inicial-usb.png",
      "data": "2026-11-03T23:34:08"
    },
    {
      "arquivo": "docs/capturas/H09-mem-data.png",
      "nome": "H09-mem-data.png",
      "data": "2026-11-03T23:34:05"
    },
    {
      "arquivo": "docs/capturas/H09-cat-sagui.png",
      "nome": "H09-cat-sagui.png",
      "data": "2026-11-03T23:34:03"
    },
    {
      "arquivo": "docs/capturas/H09-ls-cd.png",
      "nome": "H09-ls-cd.png",
      "data": "2026-11-03T23:34:01"
    },
    {
      "arquivo": "docs/capturas/H09-ajuda.png",
      "nome": "H09-ajuda.png",
      "data": "2026-11-03T23:33:59"
    },
    {
      "arquivo": "docs/capturas/H09-tela-inicial-ide.png",
      "nome": "H09-tela-inicial-ide.png",
      "data": "2026-11-03T23:33:59"
    },
    {
      "arquivo": "docs/capturas/H08-sgfs-lista.png",
      "nome": "H08-sgfs-lista.png",
      "data": "2026-11-03T23:33:52"
    },
    {
      "arquivo": "docs/capturas/H07-teclado-abnt2.png",
      "nome": "H07-teclado-abnt2.png",
      "data": "2026-11-03T23:33:46"
    },
    {
      "arquivo": "docs/capturas/H06-autoteste-memoria.png",
      "nome": "H06-autoteste-memoria.png",
      "data": "2026-11-03T23:33:36"
    },
    {
      "arquivo": "docs/capturas/H05-tela-de-erro-divisao.png",
      "nome": "H05-tela-de-erro-divisao.png",
      "data": "2026-11-03T23:33:16"
    },
    {
      "arquivo": "docs/capturas/H04-boot-vdi.png",
      "nome": "H04-boot-vdi.png",
      "data": "2026-11-03T23:33:10"
    },
    {
      "arquivo": "docs/capturas/H04-fix-memoria-6g.png",
      "nome": "H04-fix-memoria-6g.png",
      "data": "2026-11-03T23:32:53"
    },
    {
      "arquivo": "docs/capturas/H03-kernel-usb.png",
      "nome": "H03-kernel-usb.png",
      "data": "2026-11-03T23:32:41"
    },
    {
      "arquivo": "docs/capturas/H03-kernel-ide.png",
      "nome": "H03-kernel-ide.png",
      "data": "2026-11-03T23:32:28"
    },
    {
      "arquivo": "docs/capturas/H03-estagio2-usb.png",
      "nome": "H03-estagio2-usb.png",
      "data": "2026-11-03T23:32:22"
    },
    {
      "arquivo": "docs/capturas/H03-estagio2-ide.png",
      "nome": "H03-estagio2-ide.png",
      "data": "2026-11-03T23:32:16"
    },
    {
      "arquivo": "docs/capturas/H02-mbr-usb.png",
      "nome": "H02-mbr-usb.png",
      "data": "2026-11-03T23:32:09"
    },
    {
      "arquivo": "docs/capturas/H02-mbr-ide.png",
      "nome": "H02-mbr-ide.png",
      "data": "2026-11-03T23:32:03"
    },
    {
      "arquivo": "docs/capturas/G470-fase5-janelas.png",
      "nome": "G470-fase5-janelas.png",
      "data": "2026-11-03T20:00:46"
    },
    {
      "arquivo": "docs/capturas/G470-fase4-grafico-touchpad.png",
      "nome": "G470-fase4-grafico-touchpad.png",
      "data": "2026-11-03T18:37:28"
    },
    {
      "arquivo": "docs/capturas/G470-fase3-contador-ps.png",
      "nome": "G470-fase3-contador-ps.png",
      "data": "2026-11-03T17:21:50"
    },
    {
      "arquivo": "docs/capturas/G470-fase2-tela-inicial.png",
      "nome": "G470-fase2-tela-inicial.png",
      "data": "2026-11-03T16:05:16"
    },
    {
      "arquivo": "docs/capturas/G470-fase1-tela-final.png",
      "nome": "G470-fase1-tela-final.png",
      "data": "2026-11-03T15:22:25"
    }
  ],
  "linhas": {
    "boot": 903,
    "kernel": 9614,
    "userland": 6317,
    "ferramentas": 3602
  },
  "imagem": {
    "bytes_util": 575488,
    "bytes_arquivo": 1048576,
    "meta_bytes": 1474560
  },
  "progresso_geral": 100.0,
  "links": [
    {
      "texto": "Painel da versão 2.0",
      "url": "v2/index.html"
    }
  ]
};
