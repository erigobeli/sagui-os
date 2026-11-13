# Roteiro de teste: versão 2.0, fase 6 (Sagui Navegador)

**Arquivos:** `entrega/v2-fase6/saguios.vdi` e `entrega/v2-fase6/saguios.img`. Teste primeiro na VM, com a rede como na fase 3 (Intel PRO/1000, NAT).

## O que mudou

- **Sagui Navegador** (`navegador` no Terminal, ou `navegador endereço`): um navegador escrito do zero, com HTTPS pelo TLS 1.3 próprio.
  - Lê HTML (inclusive mal formado), UTF-8 e Latin-1, entidades (`&eacute;`), títulos, parágrafos, listas, citações, tabelas, caixas à direita (as fichas da Wikipédia), links, imagens **PNG e JPEG** (inclusive JPEG progressivo) e um pouco de CSS (cores, fundo, centralizar, negrito, `display: none`).
  - Barra de endereço (o primeiro clique seleciona tudo), voltar `<`, avançar `>`, recarregar `R`, **+Fav** e **Favoritos** (guardados no pendrive).
  - Formulários de busca, como o da Wikipédia (com acentos).
  - Abre também arquivos do SaguiOS (`/leia-me.txt`) e texto puro (`.md`, `.txt`).
  - Teclado: setas, PgUp/PgDn, Home/End e espaço rolam; Backspace volta; Ctrl+L vai para o endereço.
- Ainda não tem: JavaScript, CSS de arquivos externos, GIF/SVG/WebP, vídeo, roda do mouse (role pela barra da direita ou pelo teclado).

> Antes de abrir sites, acerte o relógio (`relogio internet` ou o automático nas Configurações): o HTTPS confere a data dos certificados.

## Etapa 1: VirtualBox

| O que fazer | O que deve acontecer |
| --- | --- |
| `navegador` | Abre a página inicial do Sagui Navegador, com links prontos |
| Clicar em **Wikipédia: Sagui** | Em alguns segundos o artigo aparece; a ficha com as fotos fica à direita e as imagens vão chegando uma a uma |
| Rolar até o fim (PgDn, ou arrastar a barra da direita) | O texto inteiro do artigo, com as seções e as referências |
| Clicar num link azul do artigo | Abre o outro artigo; `<` volta e `>` avança |
| No campo **Busca** do topo da Wikipédia, digitar `Mico-leão-dourado` e Enter | Abre o artigo do mico-leão-dourado |
| Clicar no endereço, digitar `example.com` e Enter | A página do example.com |
| `raw.githubusercontent.com/erigobeli/sagui-os/main/README.md` | O README do SaguiOS em texto |
| **+Fav** numa página e depois **Favoritos** | A página aparece na lista; clicando nela, abre |
| Um site seu, ou de documentação | Me conte o que ficou ruim (manda foto): é assim que o navegador melhora |

## Etapa 2: G470

A rede com cabo do G470 ainda depende da história H46 (foto do `pci`). Por enquanto, confira no G470 que o navegador abre a página inicial e o `/leia-me.txt`.

Quando testar, me diga **"v2 fase 6 ok"** ou o que aconteceu.
