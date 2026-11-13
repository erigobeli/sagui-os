```
Eli, a fase 2 da versão 2.0 (Som) está pronta para testar: entrega/v2-fase2/ e roteiro em docs/teste-v2-fase2.md. Sigo para a fase 3 (rede).
```

# Status do SaguiOS 2.0

_Atualizado em 2026-11-13T02:50:51_

- **Situação:** Trabalhando na H55: Terminal completo
- **Fase atual:** 5
- **História atual:** H55
- **Progresso geral:** 39.5%
- **Bloqueio:** H46 (rede no G470) aguarda a foto do comando pci no notebook (D034); o resto da fase segue
- **Imagem:** 805888 bytes úteis (sem teto: só medida)
- **Linhas de código:** boot 903, kernel 12697, userland 13767

## Fases

| Fase | Nome | Progresso | Estado |
| --- | --- | --- | --- |
| 1 | Base e Configurações | 100.0% | aguardando teste |
| 2 | Som | 100.0% | aguardando teste |
| 3 | Rede | 80.0% | em andamento |
| 4 | TCP e HTTPS | 57.1% | em andamento |
| 5 | Terminal e SSH | 25.0% | em andamento |
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
| H49 | HTTP e relógio pela internet | feito |  |
| H50 | Números aleatórios e resumos | feito | 2 |
| H51 | Cifras e troca de chaves | feito | 1 |
| H52 | Assinaturas e certificados | a fazer |  |
| H53 | TLS 1.3 | a fazer |  |
| H54 | Certificados raiz e créditos | a fazer |  |
| H55 | Terminal completo | feito | 1 |
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

- 2026-11-13T02:50:51: H55 concluída: Terminal novo: CSI, cores 16/256/24 bits, região de rolagem, tela alternativa, OSC, UTF-8 com volta ao Latin-1, molduras e blocos na fonte (0x80-0x9F), abas, histórico (Shift+PgUp), copiar arrastando e colar com o botão direito
- 2026-11-13T02:50:51: Início da H55: Terminal completo
- 2026-11-13T02:50:51: H49 concluída: Cliente HTTP/1.1 (tamanho, pedaços, redirecionamento), comando baixar, relogio internet por NTP com fuso, relógio automático depois do DHCP
- 2026-11-13T02:27:43: H50 concluída: Gerador do kernel: jitter do RDTSC em cada interrupção e no boot (sem RDRAND), ChaCha20 com troca de chave a cada pedido (D035); SHA-2/HMAC/HKDF na libsagui-cripto
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
