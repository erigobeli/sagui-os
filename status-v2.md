```
Eli, a fase 2 da versão 2.0 (Som) está pronta para testar: entrega/v2-fase2/ e roteiro em docs/teste-v2-fase2.md. Sigo para a fase 3 (rede).
```

# Status do SaguiOS 2.0

_Atualizado em 2026-11-13T02:25:47_

- **Situação:** Trabalhando na H50: Números aleatórios e resumos
- **Fase atual:** 4
- **História atual:** H50
- **Progresso geral:** 32.6%
- **Bloqueio:** H46 (rede no G470) aguarda a foto do comando pci no notebook (D034); o resto da fase segue
- **Imagem:** 751104 bytes úteis (sem teto: só medida)
- **Linhas de código:** boot 903, kernel 12539, userland 10385

## Fases

| Fase | Nome | Progresso | Estado |
| --- | --- | --- | --- |
| 1 | Base e Configurações | 100.0% | aguardando teste |
| 2 | Som | 100.0% | aguardando teste |
| 3 | Rede | 80.0% | em andamento |
| 4 | TCP e HTTPS | 28.6% | em andamento |
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
| H40 | Driver Intel HD Audio | aguardando teste | 7 |
| H41 | Som para os programas | aguardando teste | 2 |
| H42 | Tocador e sons do sistema | aguardando teste | 2 |
| H43 | Driver Intel PRO/1000 | feito | 1 |
| H44 | Ethernet, ARP, IPv4, ICMP e UDP | feito |  |
| H45 | DHCP e DNS | feito |  |
| H46 | Rede no G470 | a fazer |  |
| H47 | Rede na interface | feito |  |
| H48 | TCP e sockets | feito |  |
| H49 | HTTP e relógio pela internet | a fazer |  |
| H50 | Números aleatórios e resumos | em andamento |  |
| H51 | Cifras e troca de chaves | feito | 1 |
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

- 2026-11-13T02:25:47: Início da H50: Números aleatórios e resumos
- 2026-11-13T02:25:47: H51 concluída: AES-128/256 (sem tabelas, tempo constante), CTR, GCM, ChaCha20-Poly1305, X25519 e Ed25519; vetores oficiais (escrito pelo subagente Fable 5.2)
- 2026-11-13T02:25:47: Início da H51: Cifras e troca de chaves
- 2026-11-13T02:25:47: H48 concluída: TCP cliente: três passos, anéis de 64 KB, reenvio com espera dobrando, três ACKs repetidos, estacionamento de pedaços fora de ordem (10 MB certos; com 5% de perda, 1 MB em 4,5 s em vez de 84 s); sockets TCP
- 2026-11-13T01:36:59: Bloqueio: H46 (rede no G470) aguarda a foto do comando pci no notebook (D034); o resto da fase segue
- 2026-11-13T01:36:58: H47 concluída: Aba Rede (estado, DHCP ou manual) e ícone de rede na barra (verde, amarelo, cinza com x vermelho)
- 2026-11-13T01:36:19: H45 concluída: Cliente DHCP no kernel, resolvedor de DNS próprio na libsagui; comandos rede, dns e ping por nome
- 2026-11-13T01:36:19: H44 concluída: Ethernet, ARP com fila de espera, IPv4, ICMP (eco), UDP com soma; comando ping
- 2026-11-13T01:36:19: H43 concluída: Driver Intel PRO/1000 (anéis de descritores, sem interrupções), endereço físico, cabo
- 2026-11-13T01:35:10: Início da H43: Driver Intel PRO/1000
- 2026-11-13T01:35:10: Fase 2 entregue. Aguardando teste do Eli.
- 2026-11-13T01:19:08: H42 concluída: Tocador de WAV (8/16 bits, mono/estéreo, qualquer taxa), canto do sagui, sons de início e de aviso, bip e jogos pela placa (D033)
- 2026-11-13T01:17:13: Início da H42: Tocador e sons do sistema
- 2026-11-13T01:17:13: H41 concluída: Misturador com até 8 programas + notas, volume com corte em 16 bits, chamadas SIS_SOM_*, aba Som nas Configurações; mistura conferida por Goertzel no .wav
- 2026-11-13T01:15:43: Início da H41: Som para os programas
