# Roteiro: resolução nativa do painel no G470 (H84)

O SaguiOS normalmente usa o modo VESA da BIOS (1024x768), que a placa estica
para a tela de 1366x768 do G470: por isso o texto fica um pouco borrado. A H84
tem um driver para a placa Intel HD Graphics 3000 que liga a resolução
**nativa** (1366x768, imagem nítida). Ele vem **desligado**: só liga se você
pedir, e só vale com o pendrive de dados conectado.

**Arquivo:** a imagem desta entrega (grave no pendrive de boot como sempre).
Você também precisa do **pendrive de dados** (o das configurações).

## Ligar

1. Dê boot no G470 pelo pendrive do SaguiOS, com o pendrive de dados conectado.
2. No Terminal:
   ```
   montar
   config video.nativo sim
   ```
   (Se o pendrive já foi montado sozinho, o `montar` só avisa.)
3. Reinicie (`reiniciar`), de novo com o pendrive de dados conectado.

## O que deve acontecer

- Durante a partida, a tela pisca preta por um instante e a área de trabalho
  aparece ocupando a tela toda, com letras **nítidas** e mais espaço dos lados
  (ícones à esquerda, doca no meio, sagui no canto direito).
- No Terminal, `registro VIDEO` termina com
  `nativo: resolucao nativa do painel ligada, 1366x768`.
- Confira: abrir, mover e maximizar janelas (o Terminal maximizado deve
  ocupar a largura toda), o mouse chegar até a borda direita e de baixo.

## Se a tela ficar estranha (riscada, deslocada, preta ou piscando)

1. Se ainda der para usar: no Terminal,
   ```
   registro VIDEO > /usb/video.txt
   ```
   e me mande o arquivo `video.txt` do pendrive de dados.
2. Se não der para ver nada: desligue segurando o botão, **tire o pendrive de
   dados** e ligue de novo: sem ele o driver não liga e volta o VESA de
   sempre. O diário da tentativa some ao desligar, então me conte o que
   apareceu na tela (uma foto ajuda muito).
3. Se o driver **não agiu** (a tela continua como antes), `registro VIDEO`
   tem uma linha `nativo: nao agi: ...` com o motivo. Mande o arquivo também.

## Como voltar ao normal

- `config video.nativo nao` e reiniciar; ou
- tirar o pendrive de dados; ou
- apagar a linha `video.nativo = sim` do arquivo
  `.saguios/configuracoes.txt` do pendrive de dados (em outro computador).
