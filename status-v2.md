```
Eli, a fase 8 (Sagui Code) está pronta: entrega/v2-fase8/ e docs/teste-v2-fase8.md. O teste final (5 pedidos com a IA de verdade, H73) precisa da sua chave do OpenRouter.
```

# Status do SaguiOS 2.0

_Atualizado em 2026-11-14T01:37:55_

- **Situação:** Trabalhando na H80: Ponto flutuante nos programas
- **Fase atual:** 11
- **História atual:** H80
- **Progresso geral:** 90.2%
- **Bloqueio:** H46 (rede no G470): a placa interna é Atheros AR8152 (sem manual público); o Eli está comprando o TP-Link UE300 (RTL8153, USB-A). H73 espera a chave do OpenRouter. Som do G470: esperando o 'registro HDA'.
- **Imagem:** 2427904 bytes úteis (sem teto: só medida)
- **Linhas de código:** boot 920, kernel 15061, userland 29066

## Fases

| Fase | Nome | Progresso | Estado |
| --- | --- | --- | --- |
| 1 | Base e Configurações | 100.0% | aguardando teste |
| 2 | Som | 100.0% | aguardando teste |
| 3 | Rede | 80.0% | aguardando teste |
| 4 | TCP e HTTPS | 100.0% | aguardando teste |
| 5 | Terminal e SSH | 100.0% | aguardando teste |
| 6 | Sagui Navegador | 100.0% | aguardando teste |
| 7 | Compilador C | 100.0% | aguardando teste |
| 8 | Sagui Code | 80.0% | aguardando teste |
| 9 | Visual moderno | 100.0% | aguardando teste |
| 10 | Instalação no HD (opcional, a decidir) | 0% | a fazer |
| 11 | Versão 2.1: comunidade, Cipó e vídeo | 50.0% | em andamento |

## Histórias

| ID | Título | Estado | Minutos |
| --- | --- | --- | --- |
| H35 | Comando `pci` e inventário de hardware | aguardando teste | 5 |
| H36 | Desligar e reiniciar | aguardando teste | 6 |
| H37 | Configurações guardadas | aguardando teste | 3 |
| H38 | App Configurações | aguardando teste | 6 |
| H39 | Remover programas | aguardando teste | 5 |
| H40 | Driver Intel HD Audio | aguardando teste | 7 |
| H41 | Som para os programas | aguardando teste | 2 |
| H42 | Tocador e sons do sistema | aguardando teste | 2 |
| H43 | Driver Intel PRO/1000 | aguardando teste | 1 |
| H44 | Ethernet, ARP, IPv4, ICMP e UDP | aguardando teste |  |
| H45 | DHCP e DNS | aguardando teste |  |
| H46 | Rede no G470 | em andamento |  |
| H47 | Rede na interface | aguardando teste |  |
| H48 | TCP e sockets | aguardando teste |  |
| H49 | HTTP e relógio pela internet | aguardando teste |  |
| H50 | Números aleatórios e resumos | aguardando teste | 2 |
| H51 | Cifras e troca de chaves | aguardando teste | 1 |
| H52 | Assinaturas e certificados | aguardando teste | 1 |
| H53 | TLS 1.3 | aguardando teste |  |
| H54 | Certificados raiz e créditos | aguardando teste | 1 |
| H55 | Terminal completo | aguardando teste | 1 |
| H56 | Transporte SSH | aguardando teste | 1 |
| H57 | Login e sessão | aguardando teste | 1 |
| H58 | SSH de verdade | aguardando teste | 1 |
| H59 | Descompactação e PNG | aguardando teste | 1 |
| H60 | JPEG | aguardando teste | 1 |
| H61 | Leitor de HTML | aguardando teste | 1 |
| H62 | Janela do navegador | aguardando teste | 10 |
| H63 | Sites de aceite | aguardando teste | 6 |
| H64 | A linguagem | aguardando teste | 1 |
| H65 | Frente do compilador | aguardando teste | 1 |
| H66 | Fundo do compilador | aguardando teste | 1 |
| H67 | Compilar dentro do sistema | aguardando teste | 1 |
| H68 | Prova de fogo | aguardando teste | 1 |
| H69 | JSON e cliente do OpenRouter | aguardando teste | 1 |
| H70 | O laço do agente | aguardando teste | 1 |
| H71 | Instruções e contexto | aguardando teste | 1 |
| H72 | Interface | aguardando teste | 1 |
| H73 | Aceite com IA de verdade | a fazer |  |
| H74 | Vidro | aguardando teste | 1 |
| H75 | Forma | aguardando teste | 1 |
| H76 | Movimento e tema | aguardando teste | 1 |
| H77 | Leve no G470 | aguardando teste | 1 |
| H78 | Redimensionar e maximizar janelas (pedido do Eli) | aguardando teste | 1 |
| H79 | Correções do teste no G470 | feito |  |
| H80 | Ponto flutuante nos programas | feito | 1 |
| H81 | Visual do Robson somado ao da 2.0 | feito |  |
| H82 | Cipó, o navegador (motor do #6) | a fazer |  |
| H83 | Pequenos da comunidade (#3, #4, #7) | a fazer |  |
| H84 | Driver de vídeo Intel HD 3000 (G470) | feito |  |
| H85 | Créditos e versão 2.1 | a fazer |  |

## Últimas ações

- 2026-11-14T01:37:55: H84 concluída: vídeo nativo do G470 (desligado por padrão), esperando o teste do Eli
- 2026-11-14T01:37:55: H81 concluída: visual do Robson: ícones, letras, fundos, temas Mata e Azul, menus, barra de tarefas
- 2026-11-13T23:22:18: H80 concluída: FXSAVE/FXRSTOR por processo; programas com SSE2; três processos fazendo contas juntos sem se estragar
- 2026-11-13T23:22:18: Início da H80: Ponto flutuante nos programas
- 2026-11-13T23:17:06: Versão 2.1 planejada (H80 a H85): aproveitar os PRs da comunidade com crédito e o driver de vídeo do G470
- 2026-11-13T23:17:06: H79 concluída: vidro do menu, escrita combinada, registro, programas de texto no Terminal, menu do botão direito
- 2026-11-13T22:20:36: Início da H46: Rede no G470
- 2026-11-13T21:49:17: Bloqueio: H46 (rede no G470): a placa interna é Atheros AR8152 (sem manual público); o Eli está comprando o TP-Link UE300 (RTL8153, USB-A). H73 espera a chave do OpenRouter. Som do G470: esperando o 'registro HDA'.
- 2026-11-13T21:34:04: Correções do teste do Eli no G470: vidro do menu, escrita combinada no vídeo, registro, diagnóstico do som, programas de texto no Terminal, menu do botão direito nos ícones
- 2026-11-13T19:56:57: Fase 8 entregue. Aguardando teste do Eli.
- 2026-11-13T19:56:57: Bloqueio: H46 (rede no G470) espera a foto do pci ou o adaptador USB RTL8153; H73 (Sagui Code com IA de verdade) espera a chave do OpenRouter e a escolha do modelo pelo Eli
- 2026-11-13T19:56:57: H72 concluída: saguicode no Terminal: stream, histórico, /modelo /gasto /limite /limpar /salvar /abrir
- 2026-11-13T19:56:57: H71 concluída: instruções + manual + cabeçalhos com cache; conversa longa cortada abaixo do limite; gasto por pedido
- 2026-11-13T19:56:57: H69 concluída: JSON, POST, cliente do OpenRouter com stream e ferramentas, aba IA; servidor falso ok (conversa real espera a chave do Eli)
- 2026-11-13T19:56:57: Início da H72: Interface
