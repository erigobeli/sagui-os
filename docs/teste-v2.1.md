# Teste da versão 2.1

Imagem: `entrega/v2.1/saguios.img` (pendrive) ou `saguios.vdi` (VirtualBox).
Grave com o **balenaEtcher** ou com o Rufus em modo DD e dê boot no G470 (F12, **USB HDD**).

> A imagem passou de 2 MB (o Cipó traz as fontes de letra). Se o seu pendrive tinha uma
> partição FAT32 criada depois do SaguiOS, gravar a imagem nova apaga essa partição:
> copie antes o que estiver nela.

## 1. Visual (H81)

1. Configurações > Aparência: troque o **tema** (Escuro, Claro, Mata, Azul) e os **fundos**
   (Brasil, Com sol, M. Atlântica, Mata, Céu, Noite, Terra). Tudo muda na hora.
2. Em "Embaixo", troque **Doca** por **Barra em cima**: as janelas viram botões na barra de
   cima. Clicar num botão traz a janela; clicar de novo minimiza.
3. Botão direito: na área vazia (menu com Novo Terminal, Arquivos, Trocar o fundo...), na
   barra de título de uma janela (Minimizar, Maximizar, Fechar), dentro do Terminal (Colar,
   Interromper, Limpar a tela, abas), do Editor e do Arquivos.
4. O mouse deve estar mais suave (200 pacotes por segundo). Diga se o touchpad estranhou.

## 2. Cipó, o navegador (H82)

1. Abra o **Cipó** pela doca. A página inicial tem sites que funcionam.
2. Abra uma página com CSS moderno e JavaScript (por exemplo, a Wikipédia).
3. Se tiver um vídeo MP4 pequeno no pendrive: `video /usb/arquivo.mp4` (o som depende do HDA,
   que no G470 ainda está em investigação).

## 3. Programas novos (H83)

No Terminal: `top` (q sai), `grep` (por exemplo `ls bin | grep a`), `uname -a`, `fetch`,
`df` (ou `espaco`) com e sem o pendrive de dados, `triangulos` e `chuva` (Esc fecha),
e `poweroff` (desliga; se o ACPI não desligar, aparece a tela "Já pode desligar").

## 4. Ctrl+C e som (H79)

1. `contador 100` e **Ctrl+C**: o prompt volta.
2. `tocar /usb/musica.wav`: se não sair som, o programa desiste em 3 segundos.
3. Mande o diário do som: `registro HDA > /usb/som.txt`.

## 5. Vídeo nativo (H84, opcional)

Siga `docs/teste-v2.1-video.md` (precisa de `video.nativo = sim` no pendrive de dados). Se a
tela ficar preta, desligue segurando o botão e ligue sem o pendrive de dados.

## O que me mandar

Fotos de cada parte, `som.txt` e, se algo der errado, `registro > /usb/registro.txt`.
