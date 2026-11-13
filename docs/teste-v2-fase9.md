# Roteiro de teste: versão 2.0, fase 9 (Visual moderno)

**Arquivos:** `entrega/v2-fase9/saguios.vdi` e `entrega/v2-fase9/saguios.img`.

## O que mudou

- **Vidro:** as barras de título, a barra de cima, a doca e o menu deixam ver, desfocado, o que está atrás.
- **Sombras** em volta das janelas, **cantos arredondados** e botões redondos: fechar (vermelho) e **minimizar** (o traço).
- **Doca** embaixo: Arquivos, Terminal, Navegador, Editor e Configurações sempre ali, e depois os programas abertos (o ponto amarelo marca os abertos). Clicar traz a janela ou abre o programa; parar o mouse em cima mostra o nome.
- **Menu do sagui:** clique na cabecinha do sagui, no canto de cima, para ver todos os programas e "Desligar...".
- **Ícone de som** na barra de cima (abre o volume).
- **Redimensionar e maximizar** (o seu pedido): arraste a borda da direita, a de baixo ou o canto para mudar o tamanho; o botão quadrado (ou duplo clique na barra de título) maximiza, e clicar de novo volta. Funciona no Terminal (ganha linhas e colunas, e o `ssh` avisa o servidor), no Navegador, no Editor e no Arquivos. Calculadora e jogos continuam com tamanho fixo.
- **Animações** curtas ao abrir, fechar, minimizar e restaurar.
- **Tema claro ou escuro** e **Efeitos** (automático, completos ou leves) em Configurações > Aparência.
- Efeitos **automáticos**: se o computador for lento para o vidro, o SaguiOS usa os efeitos leves sozinho.

## Etapa 1: VirtualBox

| O que fazer | O que deve acontecer |
| --- | --- |
| Ligar | A doca aparece embaixo; o Terminal abre com barra de vidro e sombra |
| Arrastar o Terminal sobre os ícones | A barra de título mostra os ícones desfocados atrás; sem rastros |
| Botão `_` do Terminal | Ele encolhe para a doca; clicando no Terminal da doca, ele volta |
| Clicar na cabecinha do sagui > Calculadora | A calculadora abre com uma animação |
| Arrastar o canto de baixo à direita do Terminal | Aparece um contorno com o tamanho; ao soltar, o Terminal fica do tamanho novo |
| Botão quadrado do Navegador | Ocupa a tela toda até a doca; a página se ajeita na largura nova |
| Dentro de um `ssh`, maximizar o Terminal e rodar `stty size` | Mostra o número novo de linhas e colunas |
| Configurações > Aparência > Tema | A barra de cima, a doca e as molduras ficam claras |
| Configurações > Aparência > Efeitos: Leves | Barras sólidas, sem sombra |

## Etapa 2: G470

O mais importante desta fase: **abrir e arrastar janelas no G470 tem de ficar fluido.** Na partida, o SaguiOS mede a velocidade; se ficar lento mesmo assim, troque para "Efeitos: Leves" e me conte. Se puder, mande uma foto da tela e diga se ele escolheu "completos" ou "leves" sozinho (o comando `config aparencia.efeitos` mostra a escolha guardada; a automática aparece no log).

Quando testar, me diga **"v2 fase 9 ok"** ou o que aconteceu.
