```
Eli, as fases 3 (Rede) e 4 (TCP e HTTPS) estão prontas para testar: entrega/v2-fase3/ e entrega/v2-fase4/ (a mesma imagem), roteiros em docs/teste-v2-fase3.md e docs/teste-v2-fase4.md. Sigo para a fase 5 (SSH) e 6 (navegador).
```

# Status do SaguiOS 2.0

_Atualizado em 2026-11-13T07:49:03_

- **Situação:** Trabalhando na H62: Janela do navegador
- **Fase atual:** 6
- **História atual:** H62
- **Progresso geral:** 51.2%
- **Bloqueio:** H46 (rede no G470) aguarda a foto do comando pci no notebook (D034); o resto da fase segue
- **Imagem:** 898560 bytes úteis (sem teto: só medida)
- **Linhas de código:** boot 903, kernel 12704, userland 19149

## Fases

| Fase | Nome | Progresso | Estado |
| --- | --- | --- | --- |
| 1 | Base e Configurações | 100.0% | aguardando teste |
| 2 | Som | 100.0% | aguardando teste |
| 3 | Rede | 80.0% | aguardando teste |
| 4 | TCP e HTTPS | 100.0% | aguardando teste |
| 5 | Terminal e SSH | 25.0% | em andamento |
| 6 | Sagui Navegador | 40.0% | em andamento |
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
| H43 | Driver Intel PRO/1000 | aguardando teste | 1 |
| H44 | Ethernet, ARP, IPv4, ICMP e UDP | aguardando teste |  |
| H45 | DHCP e DNS | aguardando teste |  |
| H46 | Rede no G470 | a fazer |  |
| H47 | Rede na interface | aguardando teste |  |
| H48 | TCP e sockets | aguardando teste |  |
| H49 | HTTP e relógio pela internet | aguardando teste |  |
| H50 | Números aleatórios e resumos | aguardando teste | 2 |
| H51 | Cifras e troca de chaves | aguardando teste | 1 |
| H52 | Assinaturas e certificados | aguardando teste | 1 |
| H53 | TLS 1.3 | aguardando teste |  |
| H54 | Certificados raiz e créditos | aguardando teste | 1 |
| H55 | Terminal completo | feito | 1 |
| H56 | Transporte SSH | a fazer |  |
| H57 | Login e sessão | a fazer |  |
| H58 | SSH de verdade | a fazer |  |
| H59 | Descompactação e PNG | feito | 1 |
| H60 | JPEG | feito | 1 |
| H61 | Leitor de HTML | em andamento |  |
| H62 | Janela do navegador | em andamento |  |
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

- 2026-11-13T07:49:03: Início da H62: Janela do navegador
- 2026-11-13T07:49:03: H60 concluída: JPEG sequencial e progressivo, com reinício: 17 casos, erro médio <= 0,21
- 2026-11-13T07:49:03: H59 concluída: DEFLATE/zlib/gzip e PNG: 27 PNGs iguais pixel a pixel, zlib níveis 0-9
- 2026-11-13T07:49:03: Início da H61: Leitor de HTML
- 2026-11-13T07:49:03: Início da H60: JPEG
- 2026-11-13T07:49:03: Início da H59: Descompactação e PNG
- 2026-11-13T07:48:05: Fase 4 entregue. Aguardando teste do Eli.
- 2026-11-13T07:48:05: Fase 3 entregue. Aguardando teste do Eli.
- 2026-11-13T07:26:21: H54 concluída: 24 raízes públicas (Mozilla, MPL 2.0) em /sistema/certificados, escolhidas por tools/mkraizes.py, com CREDITOS.md
- 2026-11-13T07:26:21: Início da H54: Certificados raiz e créditos
- 2026-11-13T07:26:21: H53 concluída: TLS 1.3 dentro do SaguiOS (ponte com TCP, gerador do kernel e relógio UTC): baixar https:// funciona com example.com e a Wikipédia (ChaCha20-Poly1305); âncora de confiança por nome + chave
- 2026-11-13T02:57:46: H52 concluída: Leitor DER estrito, X.509 (RSA, P-256/384, Ed25519, PKCS#1/PSS/ECDSA), cadeia com datas, CA, pathLen e nome (curinga RFC 6125); 30 cadeias boas e ruins testadas (subagente Fable 5.2)
- 2026-11-13T02:57:46: Início da H52: Assinaturas e certificados
- 2026-11-13T02:50:51: H55 concluída: Terminal novo: CSI, cores 16/256/24 bits, região de rolagem, tela alternativa, OSC, UTF-8 com volta ao Latin-1, molduras e blocos na fonte (0x80-0x9F), abas, histórico (Shift+PgUp), copiar arrastando e colar com o botão direito
- 2026-11-13T02:50:51: Início da H55: Terminal completo
