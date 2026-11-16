// Gerado por tools/status.py a partir de status.json. Não editar.
window.SAGUI_STATUS = {
  "titulo": "SaguiOS 3.0 — 64 bits",
  "atualizado": "2026-11-16T04:05:27",
  "situacao": "Planejamento da 3.0 (trabalho local, não publicado)",
  "fase_atual": 1,
  "historia_atual": null,
  "bloqueio": null,
  "mensagem_fase": "",
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
      "estado": "em andamento",
      "progresso": 0.0
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
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H101",
      "fase": 1,
      "titulo": "Base do kernel em 64 bits",
      "objetivo": "GDT com segmentos de 64 bits e de 32 bits (compatibilidade), TSS de 64 bits (RSP0, IST), IDT de 64 bits, exceções com o quadro de 64 bits, PIC/PIT, relógio, console de texto e gráfico (VESA).",
      "aceite": "H03, H05, H09, H17 e H18 passam com o kernel novo.",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H102",
      "fase": 1,
      "titulo": "Memória em 64 bits",
      "objetivo": "Memória física (E820), paginação de 4 níveis para o kernel e para cada processo, heap do kernel, mapeamento de MMIO (framebuffer com escrita combinada, PAT), DMA sempre abaixo de 4 GB.",
      "aceite": "H06 e H10 passam.",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H103",
      "fase": 1,
      "titulo": "Processos e troca de contexto em 64 bits",
      "objetivo": "Estrutura de processo com os registradores de 64 bits, FXSAVE/FXRSTOR, escalonador, tarefas do kernel.",
      "aceite": "H11 e H80 passam.",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H104",
      "fase": 1,
      "titulo": "Programas de 32 bits em modo de compatibilidade",
      "objetivo": "Carregador ELF32 num espaço de endereçamento abaixo de 4 GB, `int 0x80` com os números e os ponteiros de 32 bits de hoje (todas as chamadas da 2.x), sinais/erros como antes.",
      "aceite": "H12, H13, H14, H15, H16 e H24 passam com os binários atuais.",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H105",
      "fase": 1,
      "titulo": "Drivers em 64 bits",
      "objetivo": "PCI, ATA, EHCI/USB (armazenamento e rede CDC), FAT32, SGFS, i8042, HDA, e1000, ACPI (desligar/reiniciar), vídeo nativo do G470, CMOS, aleatório.",
      "aceite": "os testes de cada driver passam (H29 a H48, H79, H84...).",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H106",
      "fase": 1,
      "titulo": "Janelas e interface em 64 bits",
      "objetivo": "Gerenciador de janelas, vidro, doca, menus, área de cópia, notificações.",
      "aceite": "H20 a H28, H74 a H81, H87, H89, H92.",
      "estado": "a fazer",
      "bonus": false
    },
    {
      "id": "H107",
      "fase": 1,
      "titulo": "A suíte inteira em 64 bits",
      "objetivo": "`tests/rodar_todos.py` passa com a imagem de 64 bits; o CI ganha a imagem de 64 bits. Roteiro de teste no G470.",
      "aceite": "todos os testes passam; o Eli confirma no G470.",
      "estado": "a fazer",
      "bonus": false
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
      "data": "2026-11-16T04:05:27",
      "texto": "Plano da 3.0 (64 bits) escrito em prd3.md"
    }
  ],
  "erros": [],
  "capturas": [],
  "linhas": {
    "boot": 920,
    "kernel": 15444,
    "userland": 68063,
    "ferramentas": 13313
  },
  "imagem": {
    "bytes_util": 0,
    "bytes_arquivo": 0,
    "meta_bytes": null
  },
  "progresso_geral": 0.0
};
