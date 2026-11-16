# Teste da versão 2.2.0

Imagem: `entrega/v2.2.0/saguios.img` (pendrive) ou `saguios.vdi` (VirtualBox).
Grave com o **balenaEtcher** ou com o Rufus em modo DD e dê boot no G470 (F12, **USB HDD**).

É a última versão de 32 bits. Ela junta o que entrou depois da 2.1; a 3.0 (64 bits) só
vem depois deste teste. Se o seu pendrive tinha uma partição FAT32 criada depois do
SaguiOS, gravar a imagem nova apaga essa partição: copie antes o que estiver nela.

## 1. Sobre o SaguiOS (H94)

1. Menu do sagui > **Sobre o SaguiOS**: deve dizer **2.2.0** e **32 bits**.
2. Botão **Créditos e licenças**: a lista tem a GPL 3 e as licenças de terceiros
   (fontes, certificados, Cipó...). Escolha uma e veja o texto.
3. Botão **Histórico**: mostra as versões (as três eras e o que mudou na 2.2.0).

## 2. Doca e ícones (D055)

1. A doca tem cantos redondos, e os ícones estão todos no mesmo estilo e centrados:
   Arquivos, Terminal, Cipó, Sagui Code (`</>`), Jogos (roxo), Sobre (cara do sagui).
2. Os mesmos ícones aparecem na área de trabalho, maiores.

## 3. Janelas (Robson)

1. Arraste uma janela até a **borda de cima**: maximiza. Até a **esquerda** ou a
   **direita**: metade da tela. Até um **canto**: um quarto. Durante o arraste aparece
   uma prévia translúcida.
2. Arraste de novo a janela encaixada: ela volta ao tamanho de antes.
3. Atalhos: **Windows + setas** (metade, maximizar, voltar), **Windows + D** (minimiza
   todas), **Alt+Tab**, **Alt+F4**, e a **tecla Windows sozinha** abre o menu do sagui.
4. Redimensione uma janela pelo canto: ela não deve "embaralhar" enquanto redesenha.

## 4. Jogos (H92)

No menu do sagui, **Jogos** (ou `jogos` no Terminal). Abra cada um:

1. **Caça-Bananas**: clique abre a casa, botão direito põe a bandeirinha.
2. **Paciência Banana**: arraste as cartas; Ctrl+Z desfaz.
3. **CHIP-8**: abra uma das ROMs (são todas feitas aqui).
4. **Atividades JClic**: a atividade de exemplo (quebra-cabeça, caça-palavras,
   cruzadinha).

## 5. Terminal

1. Comandos novos, cada um com o nome do Unix e um em português (veja `ajuda`):
   `seq 1 5`, `cal`, `echo 2+3 | bc`, `ls bin | sort | head -5` (ou `inicio`),
   `tree /sistema`, `echo oi | base64`.
2. **Compactação**: `tar` e `gzip` de uma pasta, e o caminho de volta:
   `tar -cf /usb/t.tar /sistema/ajuda`, `gzip /usb/t.tar`, `gunzip /usb/t.tar.gz`,
   `tar -tf /usb/t.tar`. Se tiver um `.zip` feito no Windows, `unzip -l /usb/arquivo.zip`.
3. **Teclado espanhol**: Configurações > Teclado, troque para espanhol e digite `ñ`;
   depois volte para ABNT2.

## 6. Cipó (Robson)

Abra uma página com muitas imagens (por exemplo, a Wikipédia) e role a página: deve
estar mais leve que na 2.1 (as imagens não são decodificadas de novo a cada mudança).

## 7. O que já se sabe (não precisa reportar)

- O **Tocador** trava a tela ao abrir uma música do pendrive e repete o som ao trocar de
  música (issue #27).
- Vídeo 1080p em câmera lenta no G470 (issue #28).

## O que me mandar

Fotos de cada parte e, se algo der errado, `registro > /usb/registro.txt`.
