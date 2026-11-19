# Teste da 3.0, fase 1: o kernel em 64 bits

Imagem: `entrega/v3.0-fase1/saguios.img` (pendrive) ou `saguios.vdi` (VirtualBox).
Confira o `SHA256SUMS.txt` antes (veja `docs/como-gravar.md`).

**O que mudou:** o kernel do SaguiOS agora roda em 64 bits (modo longo, x86-64). Os
programas continuam os mesmos da 2.1, de 32 bits, rodando no "modo de compatibilidade" do
processador. Por fora, **tudo tem que funcionar exatamente como na 2.1**. A única diferença
visível é na tela de inicialização (veja o passo 1).

> Gravar a imagem apaga o pendrive inteiro (inclusive uma partição FAT32 criada depois do
> SaguiOS). Copie antes o que estiver nele. O SaguiOS nunca grava no disco interno do G470.

## 0. VirtualBox primeiro (opcional, recomendado)

A VM da 2.x foi criada como **Other/Unknown (32 bits)**, e nesse tipo o VirtualBox esconde o
modo de 64 bits do processador. Em **Configurações > Geral > Básico**, troque a versão para
**Other/Unknown (64-bit)** (o resto continua igual: BIOS, sem EFI, disco IDE). Se esquecer,
o SaguiOS mostra:

    ERRO: este processador nao tem 64 bits (x86-64).

e para ali. Isso também é um teste: a mensagem tem que aparecer clara, sem tela preta.

## 1. Boot no G470

Grave o `saguios.img` com o balenaEtcher (ou Rufus em modo DD) e dê boot (F12, **USB HDD**).

1. O estágio 2 mostra as linhas `[1/6]` a `[5/6]` como sempre, e a última agora é
   **`[6/6] entrando no modo longo (64 bits)`**. Se travar aí (tela parada com essa linha),
   tire uma foto: é o ponto exato da troca para os 64 bits.
2. Na tela de inicialização (aparece rapidinho antes da área de trabalho; uma foto em
   sequência ajuda), a primeira etapa diz
   **`Modo longo (64 bits): GDT e IDT próprias...`** e a terceira, **`Paginação de 4
   níveis...`**.
3. A área de trabalho abre com o Terminal, como na 2.1.

Se em vez disso aparecer a tela vermelha "O SaguiOS encontrou um erro", fotografe a tela
inteira: ela mostra os registradores (agora com a linha `R8=... R9=...` e a indicação
"modo longo, 64 bits").

## 2. O mesmo roteiro da 2.1

Repita o essencial do teste da 2.1 (`docs/teste-v2.1.md`), porque todos os drivers foram
recompilados para 64 bits:

1. **Teclado e touchpad:** digitar no Terminal (acentos e ç), mover e clicar.
2. **Janelas:** abrir Arquivos, Editor, Calculadora e um jogo pela doca; arrastar,
   redimensionar, minimizar; botão direito; copiar e colar entre Editor e Terminal.
3. **Pendrive de dados (USB):** espetar, ver a notificação, `ls /usb`, copiar um arquivo
   para lá e de volta (`cp`), `desmontar`. É o teste mais importante: o controlador USB do
   G470 trabalha com endereços físicos, e é exatamente aí que um kernel de 64 bits costuma
   errar.
4. **Rede pelo cabo** (se tiver) ou pelo adaptador USB: `rede`, `ping 8.8.8.8`,
   `baixar https://example.com`, e o **Cipó** abrindo a Wikipédia.
5. **Som:** `bip`, `tocar /usb/musica.wav` e o Tocador com um MP3.
6. **Programas pesados:** `cc` (compilar um programa no Editor e rodar), Sagui Code (se
   tiver a chave), `top` (q sai) e `ps`.
7. **Desligar e reiniciar** pelo menu ou com `poweroff` / `reiniciar`.
8. **Vídeo nativo** (opcional, como na 2.1): `video.nativo = sim` no pendrive de dados.

## 3. Comparação de velocidade (opcional)

Se notar algo mais lento ou mais rápido que na 2.1 (abrir o Cipó, compilar, arrastar
janelas), anote: o kernel de 64 bits deveria ser tão rápido quanto o de 32.

## O que me mandar

Fotos da tela de inicialização (passo 1) e de qualquer erro, e o diário do kernel:
`registro > /usb/registro.txt`. Se algo falhar e o sistema continuar de pé, mande também
`usb > /usb/usb.txt` e `registro HDA > /usb/som.txt`.

## Se precisar voltar para os 32 bits

A imagem de 32 bits continua sendo gerada (`make ARQ=32`, em `build32/`) até a 3.0 ser
aprovada. A 2.1 em `entrega/v2.1/` também continua valendo.
