# Roteiro de teste: versão 2.0, fase 8 (Sagui Code)

**Arquivos:** `entrega/v2-fase8/saguios.vdi` e `entrega/v2-fase8/saguios.img`. Use o pendrive do SaguiOS (com a pasta `.saguios`): é lá que ficam a chave, as conversas e os programas.

## O que mudou

- **`saguicode`** no Terminal: um agente de programação que conversa com um modelo de IA pelo **OpenRouter**. Você pede um programa em português; ele escreve o arquivo, compila com o `cc` do SaguiOS, lê os erros, corrige, compila de novo e roda.
- Antes de **sobrescrever** um arquivo, **editar** ou **rodar** qualquer coisa, ele pede licença: `s` (sim), `n` (não) ou `t` (tudo nesta sessão).
- A resposta aparece enquanto é gerada; no fim de cada pedido, os tokens e o **gasto** em dólares.
- Comandos: `/modelo`, `/gasto`, `/limite 2` (dólares por sessão), `/limpar`, `/salvar nome`, `/abrir nome`, `/sair`.
- **Configurações > IA:** modelo, limite de gasto e a chave (com asteriscos; ela nunca aparece na tela).
- Os programas ficam em `/usb/projetos`.

## Antes: a chave do OpenRouter (no seu PC)

1. Crie uma conta em **openrouter.ai** e coloque um pouco de crédito (5 dólares bastam para muitos testes).
2. Em **Keys**, crie uma chave. Ela começa com `sk-or-v1-`.
3. Escolha o modelo. Sugestões: `anthropic/claude-sonnet-4.5` (muito bom em C) ou um mais barato, como `google/gemini-2.5-flash` ou `deepseek/deepseek-chat`. O nome exato está na página do modelo no OpenRouter.
4. **No SaguiOS:** Configurações > IA, cole a chave (aparecem só asteriscos), escreva o modelo e clique em Guardar. Ou, no PC, crie no pendrive o arquivo `.saguios/chaves.txt` com a linha:
   ```
   openrouter = sk-or-v1-...
   ```
   **Nunca mande a chave por mensagem, nem em foto.**

## Etapa 1: VirtualBox

| O que fazer | O que deve acontecer |
| --- | --- |
| No Terminal: `saguicode` | "Sagui Code - ... Modelo: ..." e o `>` |
| `faça um programa que mostre a tabuada do 7` | Ele escreve `tabuada.c`, compila e roda; aparecem os pedidos de licença e, no fim, a tabuada e o gasto |
| `/gasto` | Tokens e dólares da sessão |
| `faça um relógio analógico numa janela` | Ele tenta; se der erro de compilação, corrige sozinho. A janela do relógio abre |
| `/salvar relogio` e depois `/sair` | A conversa fica em `/usb/.saguios/conversas/relogio.json` |

**Os cinco pedidos de referência (H73):** depois, peça um de cada vez:
1. um relógio analógico numa janela;
2. um conversor de temperatura (Celsius e Fahrenheit, só com inteiros);
3. uma lista de tarefas que grava num arquivo;
4. um jogo da velha para dois jogadores na janela;
5. um visualizador das imagens PNG do pendrive.

Para cada um, me conte: deu certo de primeira? Precisou corrigir? Quanto gastou (o número do fim)? Uma foto da tela ajuda muito. Com isso eu registro no diário do projeto (pelo menos 4 de 5 precisam terminar com o programa rodando).

## Etapa 2: G470

A rede do G470 ainda depende da H46 (foto do `pci`). Sem rede, o Sagui Code abre mas não consegue falar com a IA: confira só que ele abre e mostra a mensagem de erro de conexão.

Quando testar, me diga **"v2 fase 8 ok"** ou o que aconteceu.
