// Gerado por tools/status.py a partir de status.json. Não editar.
window.SAGUI_STATUS = {
  "titulo": "SaguiOS 3.0 — 64 bits",
  "atualizado": "2026-11-16T07:18:41",
  "situacao": "Aguardando teste do Eli: fase 1",
  "fase_atual": 1,
  "historia_atual": null,
  "bloqueio": null,
  "mensagem_fase": "Fase 1 da 3.0 pronta: kernel em 64 bits, programas de 32 sem mudança. Teste do Eli no G470 (docs/teste-v3.0-fase1.md)",
  "raiz": "../../",
  "links": [
    {
      "texto": "PRD 2.0",
      "url": "../../prd2.md"
    },
    {
      "texto": "Painel da versão 1.0",
      "url": "../index.html"
    }
  ],
  "tabela_titulo": "Histórias da 3.0",
  "tabela_coluna": "Capacidade",
  "tamanho_inicial": 575488,
  "fases": [
    {
      "numero": 1,
      "nome": "Kernel em 64 bits, programas de 32 sem mudança",
      "estado": "aguardando teste",
      "progresso": 100.0
    },
    {
      "numero": 2,
      "nome": "Programas em 64 bits",
      "estado": "a fazer",
      "progresso": 0.0
    }
  ],
  "historias": [
    {
      "id": "H100",
      "fase": 1,
      "titulo": "Boot em modo longo",
      "objetivo": "O estágio 2 confere o CPUID (bit LM), monta tabelas de página de 4 níveis com a identidade dos primeiros GB, liga PAE, EFER.LME e a paginação, salta para um segmento de 64 bits e entra no kernel ELF64.",
      "aceite": "no QEMU e no VirtualBox, o kernel de 64 bits escreve na tela e na serial; numa CPU sem 64 bits, uma mensagem clara.",
      "estado": "aguardando teste",
      "bonus": false,
      "inicio": "2026-11-16T04:09:03",
      "fim": "2026-11-16T04:23:52",
      "minutos": 15,
      "resultado": "estágio 2 entra no modo longo quando o kernel é ELF64; mensagem clara em CPU sem 64 bits"
    },
    {
      "id": "H101",
      "fase": 1,
      "titulo": "Base do kernel em 64 bits",
      "objetivo": "GDT com segmentos de 64 bits e de 32 bits (compatibilidade), TSS de 64 bits (RSP0, IST), IDT de 64 bits, exceções com o quadro de 64 bits, PIC/PIT, relógio, console de texto e gráfico (VESA).",
      "aceite": "H03, H05, H09, H17 e H18 passam com o kernel novo.",
      "estado": "aguardando teste",
      "bonus": false,
      "inicio": "2026-11-16T04:23:52",
      "fim": "2026-11-16T04:23:52",
      "minutos": 1,
      "resultado": "GDT com código 64/32, TSS64 com IST, IDT de 16 bytes, quadro_t de 64 bits; H03 H05 H09 H17 H18 passam"
    },
    {
      "id": "H102",
      "fase": 1,
      "titulo": "Memória em 64 bits",
      "objetivo": "Memória física (E820), paginação de 4 níveis para o kernel e para cada processo, heap do kernel, mapeamento de MMIO (framebuffer com escrita combinada, PAT), DMA sempre abaixo de 4 GB.",
      "aceite": "H06 e H10 passam.",
      "estado": "aguardando teste",
      "bonus": false,
      "inicio": "2026-11-16T04:23:52",
      "fim": "2026-11-16T04:23:52",
      "minutos": 1,
      "resultado": "paginação de 4 níveis com o primeiro GB compartilhado, PAT/WC, DMA abaixo de 4 GB; H06 H10 passam"
    },
    {
      "id": "H103",
      "fase": 1,
      "titulo": "Processos e troca de contexto em 64 bits",
      "objetivo": "Estrutura de processo com os registradores de 64 bits, FXSAVE/FXRSTOR, escalonador, tarefas do kernel.",
      "aceite": "H11 e H80 passam.",
      "estado": "aguardando teste",
      "bonus": false,
      "inicio": "2026-11-16T04:23:52",
      "fim": "2026-11-16T04:23:52",
      "minutos": 1,
      "resultado": "tarefas do kernel e troca de contexto com iretq, FXSAVE por processo, pilha do kernel de 16 KB; H11 H80 passam"
    },
    {
      "id": "H104",
      "fase": 1,
      "titulo": "Programas de 32 bits em modo de compatibilidade",
      "objetivo": "Carregador ELF32 num espaço de endereçamento abaixo de 4 GB, `int 0x80` com os números e os ponteiros de 32 bits de hoje (todas as chamadas da 2.x), sinais/erros como antes.",
      "aceite": "H12, H13, H14, H15, H16 e H24 passam com os binários atuais.",
      "estado": "aguardando teste",
      "bonus": false,
      "inicio": "2026-11-16T04:23:52",
      "fim": "2026-11-16T04:23:52",
      "minutos": 1,
      "resultado": "ELF32 no modo de compatibilidade (CS 0x1B, L=0 D=1), int 0x80 igual, argv lido como uint32_t; H12-H16 H24 passam"
    },
    {
      "id": "H105",
      "fase": 1,
      "titulo": "Drivers em 64 bits",
      "objetivo": "PCI, ATA, EHCI/USB (armazenamento e rede CDC), FAT32, SGFS, i8042, HDA, e1000, ACPI (desligar/reiniciar), vídeo nativo do G470, CMOS, aleatório.",
      "aceite": "os testes de cada driver passam (H29 a H48, H79, H84...).",
      "estado": "aguardando teste",
      "bonus": false,
      "inicio": "2026-11-16T05:22:52",
      "fim": "2026-11-16T05:22:52",
      "minutos": 1,
      "resultado": "drivers (EHCI, e1000, HDA, vídeo, FAT32, ACPI) com conversões explícitas por uintptr_t; DMA continua abaixo de 4 GB"
    },
    {
      "id": "H106",
      "fase": 1,
      "titulo": "Janelas e interface em 64 bits",
      "objetivo": "Gerenciador de janelas, vidro, doca, menus, área de cópia, notificações.",
      "aceite": "H20 a H28, H74 a H81, H87, H89, H92.",
      "estado": "aguardando teste",
      "bonus": false,
      "inicio": "2026-11-16T05:22:52",
      "fim": "2026-11-16T05:22:52",
      "minutos": 1,
      "resultado": "janelas e pixels compartilhados (0xA0000000) iguais à 2.x; H20-H28, H74-H92 passam"
    },
    {
      "id": "H107",
      "fase": 1,
      "titulo": "A suíte inteira em 64 bits",
      "objetivo": "`tests/rodar_todos.py` passa com a imagem de 64 bits; o CI ganha a imagem de 64 bits. Roteiro de teste no G470.",
      "aceite": "todos os testes passam; o Eli confirma no G470.",
      "estado": "aguardando teste",
      "bonus": false,
      "inicio": "2026-11-16T05:22:52",
      "fim": "2026-11-16T07:18:40",
      "minutos": 116,
      "resultado": "suíte inteira (88 testes) passa com a imagem de 64 bits; roteiro do G470 e imagem em entrega/v3.0-fase1"
    },
    {
      "id": "H108",
      "fase": 2,
      "titulo": "Chamadas de sistema de 64 bits",
      "objetivo": "`syscall`/`sysret` (MSRs STAR, LSTAR, FMASK), ABI de 64 bits documentada, carregador ELF64.",
      "aceite": "um programa de teste de 64 bits roda ao lado dos de 32.",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H109",
      "fase": 2,
      "titulo": "libsagui e programas em 64 bits",
      "objetivo": "A libsagui e os programas compilam em x86-64 (o build gera as duas versões); os programas do sistema passam a ser de 64 bits.",
      "aceite": "a suíte passa com os programas de 64 bits.",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H110",
      "fase": 2,
      "titulo": "O compilador `cc` gera x86-64",
      "objetivo": "Um gerador de código novo no `cc` (registradores de 64 bits, convenção de chamada própria documentada), mantendo o de 32 bits.",
      "aceite": "os testes do compilador (H64 a H68) passam nos dois modos.",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H111",
      "fase": 2,
      "titulo": "Entrega da 3.0",
      "objetivo": "Imagem em `entrega/v3.0/`, roteiro de teste, decisões, README.",
      "aceite": "o Eli confirma no G470.",
      "estado": "a fazer",
      "bonus": false
    }
  ],
  "tanenbaum": [
    {
      "conceito": "Desligar e reiniciar",
      "implementa": "ACPI e menu na barra de cima",
      "historias": "H36",
      "estado": "a fazer"
    },
    {
      "conceito": "Configurações",
      "implementa": "App com relógio, aparência, som, rede, IA e programas, guardadas no pendrive",
      "historias": "H37 a H39",
      "estado": "a fazer"
    },
    {
      "conceito": "Som",
      "implementa": "Driver Intel HD Audio, mistura e volume",
      "historias": "H40 a H42",
      "estado": "a fazer"
    },
    {
      "conceito": "Rede",
      "implementa": "Placa de rede, IPv4, DHCP, DNS, ping",
      "historias": "H43 a H47",
      "estado": "a fazer"
    },
    {
      "conceito": "TCP e HTTP",
      "implementa": "Conexões confiáveis, downloads, relógio pela internet",
      "historias": "H48, H49",
      "estado": "a fazer"
    },
    {
      "conceito": "Criptografia e HTTPS",
      "implementa": "SHA-2, AES-GCM, ChaCha20, X25519, RSA, ECDSA, X.509 e TLS 1.3 próprios",
      "historias": "H50 a H54",
      "estado": "a fazer"
    },
    {
      "conceito": "Terminal e SSH",
      "implementa": "Terminal completo e cliente SSH que roda vim e htop",
      "historias": "H55 a H58",
      "estado": "a fazer"
    },
    {
      "conceito": "Navegador",
      "implementa": "HTML, PNG, JPEG; Wikipédia e páginas simples",
      "historias": "H59 a H63",
      "estado": "a fazer"
    },
    {
      "conceito": "Compilador C",
      "implementa": "Compila programas dentro do próprio SaguiOS",
      "historias": "H64 a H68",
      "estado": "a fazer"
    },
    {
      "conceito": "Sagui Code",
      "implementa": "Agente de programação com IA pelo OpenRouter",
      "historias": "H69 a H73",
      "estado": "a fazer"
    },
    {
      "conceito": "Visual moderno",
      "implementa": "Vidro, desfoque, cantos arredondados, animações",
      "historias": "H74 a H77",
      "estado": "a fazer"
    }
  ],
  "acoes": [
    {
      "data": "2026-11-16T07:18:41",
      "texto": "Fase 1 entregue. Aguardando teste do Eli."
    },
    {
      "data": "2026-11-16T07:18:40",
      "texto": "H107 concluída: suíte inteira (88 testes) passa com a imagem de 64 bits; roteiro do G470 e imagem em entrega/v3.0-fase1"
    },
    {
      "data": "2026-11-16T05:22:52",
      "texto": "Início da H107: A suíte inteira em 64 bits"
    },
    {
      "data": "2026-11-16T05:22:52",
      "texto": "Erro corrigido: h10 falhou na suíte de 64 bits: etapa de paginação não aparecia"
    },
    {
      "data": "2026-11-16T05:22:52",
      "texto": "H106 concluída: janelas e pixels compartilhados (0xA0000000) iguais à 2.x; H20-H28, H74-H92 passam"
    },
    {
      "data": "2026-11-16T05:22:52",
      "texto": "Início da H106: Janelas e interface em 64 bits"
    },
    {
      "data": "2026-11-16T05:22:52",
      "texto": "H105 concluída: drivers (EHCI, e1000, HDA, vídeo, FAT32, ACPI) com conversões explícitas por uintptr_t; DMA continua abaixo de 4 GB"
    },
    {
      "data": "2026-11-16T05:22:52",
      "texto": "Início da H105: Drivers em 64 bits"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "H104 concluída: ELF32 no modo de compatibilidade (CS 0x1B, L=0 D=1), int 0x80 igual, argv lido como uint32_t; H12-H16 H24 passam"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "H103 concluída: tarefas do kernel e troca de contexto com iretq, FXSAVE por processo, pilha do kernel de 16 KB; H11 H80 passam"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "H102 concluída: paginação de 4 níveis com o primeiro GB compartilhado, PAT/WC, DMA abaixo de 4 GB; H06 H10 passam"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "H101 concluída: GDT com código 64/32, TSS64 com IST, IDT de 16 bytes, quadro_t de 64 bits; H03 H05 H09 H17 H18 passam"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "Início da H104: Programas de 32 bits em modo de compatibilidade"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "Início da H103: Processos e troca de contexto em 64 bits"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "Início da H102: Memória em 64 bits"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "Início da H101: Base do kernel em 64 bits"
    },
    {
      "data": "2026-11-16T04:23:52",
      "texto": "H100 concluída: estágio 2 entra no modo longo quando o kernel é ELF64; mensagem clara em CPU sem 64 bits"
    },
    {
      "data": "2026-11-16T04:09:03",
      "texto": "Início da H100: Boot em modo longo"
    },
    {
      "data": "2026-11-16T04:05:27",
      "texto": "Plano da 3.0 (64 bits) escrito em prd3.md"
    }
  ],
  "erros": [
    {
      "data": "2026-11-16T05:22:52",
      "sintoma": "h10 falhou na suíte de 64 bits: etapa de paginação não aparecia",
      "causa": "o texto novo da etapa ('Paginação de 4 níveis; programas...') perdeu 'um espaço de endereço', que o teste procura",
      "correcao": "texto da etapa volta a dizer 'um espaço de endereço por processo'"
    }
  ],
  "capturas": [],
  "linhas": {
    "boot": 1077,
    "kernel": 16118,
    "userland": 68063,
    "ferramentas": 13407
  },
  "imagem": {
    "bytes_util": 6535680,
    "bytes_arquivo": 6553600,
    "meta_bytes": null
  },
  "progresso_geral": 66.7
};
