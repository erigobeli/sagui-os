# Roteiro de teste: versão 2.0, fase 4 (TCP e HTTPS)

**Arquivos:** `entrega/v2-fase4/saguios.vdi` e `entrega/v2-fase4/saguios.img`. Teste primeiro na VM, com a rede configurada como na fase 3 (Intel PRO/1000, NAT).

## O que mudou

- **TCP próprio:** conexões confiáveis, com reenvio e com os pedaços fora de ordem guardados (10 MB chegam certos mesmo perdendo 5% dos pacotes).
- **Criptografia e TLS 1.3 próprios:** SHA-2, AES-GCM, ChaCha20-Poly1305, X25519, RSA, ECDSA, Ed25519 e a validação de certificados, escritos do zero (com ajuda de subagentes no Claude Fable 5.2) e conferidos com os vetores de teste oficiais.
- **Números aleatórios** do próprio kernel, sem depender do RDRAND (que o G470 não tem).
- **`baixar <url> [arquivo]`:** baixa por `http://` e `https://`.
- **24 certificados raiz** em `/sistema/certificados` (lista da Mozilla, créditos em `CREDITOS.md`).
- **Relógio pela internet:** `relogio internet`, e em Configurações > Relógio, "Acertar sozinho pela rede" e "Acertar agora".
- **Terminal novo** (adiantado da fase 5): cores, molduras, abas (o `+` em cima), histórico com Shift + PgUp, copiar arrastando o mouse e colar com o botão direito.

> **Importante:** o HTTPS confere a validade dos certificados pela data. Se o relógio estiver errado, todo site vai dar "ainda não é válido" ou "vencido". Primeiro rode `relogio internet` (ou ligue o automático).

## Etapa 1: VirtualBox

| O que fazer | O que deve acontecer |
| --- | --- |
| `relogio internet` | "Relógio acertado pela internet: ..." com a hora certa de Brasília |
| `cd /tmp` e `baixar https://pt.wikipedia.org/wiki/Sagui sagui.html` | "Gravado sagui.html: ... bytes" |
| `baixar https://example.com/` | Grava `pagina.html`; `cat pagina.html` mostra "Example Domain" |
| `baixar https://expired.badssl.com/` | Erro (esse site só fala TLS 1.2, que o SaguiOS não usa: aparece um aviso de negociação) |
| No Terminal: clicar no `+` | Uma aba nova com outro sagsh |
| Arrastar o mouse sobre um texto e clicar com o botão direito | O texto é colado na linha de comando |

## Etapa 2: G470

A rede com cabo do G470 ainda depende da história H46 (foto do `pci`). Por enquanto, confira no G470 que nada quebrou (boot, área de trabalho, som, Terminal novo).

Quando testar, me diga **"v2 fase 4 ok"** ou o que aconteceu.
