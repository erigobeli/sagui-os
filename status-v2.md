```
Eli, a fase 1 da versão 2.0 (Base e Configurações) está pronta para testar quando puder: entrega/v2-fase1/ e roteiro em docs/teste-v2-fase1.md. Pedido especial: a foto do comando pci no G470. Sigo para a fase 2 (som).
```

# Status do SaguiOS 2.0

_Atualizado em 2026-11-13T01:15:43_

- **Situação:** Trabalhando na H41: Som para os programas
- **Fase atual:** 2
- **História atual:** H41
- **Progresso geral:** 14.0%
- **Bloqueio:** nenhum
- **Imagem:** 664064 bytes úteis (sem teto: só medida)
- **Linhas de código:** boot 903, kernel 11156, userland 6920

## Fases

| Fase | Nome | Progresso | Estado |
| --- | --- | --- | --- |
| 1 | Base e Configurações | 100.0% | aguardando teste |
| 2 | Som | 33.3% | em andamento |
| 3 | Rede | 0.0% | a fazer |
| 4 | TCP e HTTPS | 0.0% | a fazer |
| 5 | Terminal e SSH | 0.0% | a fazer |
| 6 | Sagui Navegador | 0.0% | a fazer |
| 7 | Compilador C | 0.0% | a fazer |
| 8 | Sagui Code | 0.0% | a fazer |
| 9 | Visual moderno | 0.0% | a fazer |
| 10 | Instalação no HD (opcional, a decidir) | 0% | a fazer |

## Histórias

| ID | Título | Estado | Minutos |
| --- | --- | --- | --- |
| H35 | Comando `pci` e inventário de hardware | aguardando teste | 5 |
| H36 | Desligar e reiniciar | aguardando teste | 6 |
| H37 | Configurações guardadas | aguardando teste | 3 |
| H38 | App Configurações | aguardando teste | 6 |
| H39 | Remover programas | aguardando teste | 5 |
| H40 | Driver Intel HD Audio | feito | 7 |
| H41 | Som para os programas | em andamento |  |
| H42 | Tocador e sons do sistema | a fazer |  |
| H43 | Driver Intel PRO/1000 | a fazer |  |
| H44 | Ethernet, ARP, IPv4, ICMP e UDP | a fazer |  |
| H45 | DHCP e DNS | a fazer |  |
| H46 | Rede no G470 | a fazer |  |
| H47 | Rede na interface | a fazer |  |
| H48 | TCP e sockets | a fazer |  |
| H49 | HTTP e relógio pela internet | a fazer |  |
| H50 | Números aleatórios e resumos | a fazer |  |
| H51 | Cifras e troca de chaves | a fazer |  |
| H52 | Assinaturas e certificados | a fazer |  |
| H53 | TLS 1.3 | a fazer |  |
| H54 | Certificados raiz e créditos | a fazer |  |
| H55 | Terminal completo | a fazer |  |
| H56 | Transporte SSH | a fazer |  |
| H57 | Login e sessão | a fazer |  |
| H58 | SSH de verdade | a fazer |  |
| H59 | Descompactação e PNG | a fazer |  |
| H60 | JPEG | a fazer |  |
| H61 | Leitor de HTML | a fazer |  |
| H62 | Janela do navegador | a fazer |  |
| H63 | Sites de aceite | a fazer |  |
| H64 | A linguagem | a fazer |  |
| H65 | Frente do compilador | a fazer |  |
| H66 | Fundo do compilador | a fazer |  |
| H67 | Compilar dentro do sistema | a fazer |  |
| H68 | Prova de fogo | a fazer |  |
| H69 | JSON e cliente do OpenRouter | a fazer |  |
| H70 | O laço do agente | a fazer |  |
| H71 | Instruções e contexto | a fazer |  |
| H72 | Interface | a fazer |  |
| H73 | Aceite com IA de verdade | a fazer |  |
| H74 | Vidro | a fazer |  |
| H75 | Forma | a fazer |  |
| H76 | Movimento e tema | a fazer |  |
| H77 | Leve no G470 | a fazer |  |

## Últimas ações

- 2026-11-13T01:15:43: Início da H41: Som para os programas
- 2026-11-13T01:15:43: H40 concluída: Driver Intel HD Audio: CORB/RIRB, descoberta do caminho pino-DAC no codec, fluxo de saída 48 kHz/16 bits por DMA; tom de 440 Hz conferido no .wav do QEMU
- 2026-11-13T01:08:35: Início da H40: Driver Intel HD Audio
- 2026-11-13T01:08:35: Fase 1 entregue. Aguardando teste do Eli.
- 2026-11-13T00:54:44: H39 concluída: Programas em /usb/programas viram ícone e comando; aba Programas remove com confirmação e esconde ícones do sistema
- 2026-11-13T00:50:05: Início da H39: Remover programas
- 2026-11-13T00:50:05: H38 concluída: App Configurações com Relógio (acerta o CMOS, fuso), Aparência (5 fundos, sagui do canto) e Sobre o computador; ícones em duas colunas (D032)
- 2026-11-13T00:43:52: Início da H38: App Configurações
- 2026-11-13T00:43:52: H37 concluída: Tabela de configurações no kernel, arquivo /usb/.saguios/configuracoes.txt, montagem automática do pendrive do SaguiOS (D031), comando config
- 2026-11-13T00:41:00: Início da H37: Configurações guardadas
- 2026-11-13T00:40:59: H36 concluída: Desligar pelo ACPI (_S5_ da DSDT), reiniciar pelo reset do ACPI/0xCF9/teclado, pendrives gravados antes, botão de energia na barra e janela Energia
- 2026-11-13T00:35:06: Início da H36: Desligar e reiniciar
- 2026-11-13T00:35:06: H35 concluída: Comando pci com classe, fabricante, modelo e o que o SaguiOS já usa; relatório em /usb/hardware.txt
- 2026-11-13T00:30:04: Início da H35: Comando `pci` e inventário de hardware
- 2026-11-13T00:30:03: Modo contínuo: o Eli pediu para seguir fase após fase sem esperar os testes; cada fase vira uma imagem em entrega/v2-faseX/.
