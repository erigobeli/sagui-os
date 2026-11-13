# Roteiro de teste: versão 2.0, fase 2 (Som)

**Arquivos:** `entrega/v2-fase2/saguios.vdi` e `entrega/v2-fase2/saguios.img`. Teste primeiro na VM.

## O que mudou

- **Driver de som Intel HD Audio**, feito do zero. Na inicialização aparece a linha `[ ok ] Som: Intel HD Audio, codec ..., saidas: ...`.
- **Sons do sistema:** um acorde ao abrir a área de trabalho e um "plim" em cada aviso (pendrive conectado, montado...). Dá para desligar em Configurações > Som.
- **Configurações > Som:** volume (botões - e +, guardado no pendrive), sons do sistema ligados ou desligados, "Testar o som".
- **Terminal:** `som` (mostra a placa e o volume), `som teste 440` (tom de teste), `som volume 50`, `tocar arquivo.wav` (WAV de 8 ou 16 bits, mono ou estéreo, em qualquer taxa) e `tocar /sons/sagui.wav` (o canto do sagui, sintetizado aqui).
- **O bip, o Sagui Pega Banana e o Sagui 3D tocam pela placa de som** quando ela existe (antes só pelo alto-falante do PC).

## Etapa 1: VirtualBox

Configurações da VM > **Áudio**: habilitado, controlador **Intel HD Audio**.

| O que fazer | O que deve acontecer |
| --- | --- |
| Ligar a VM | Um acorde curto quando a área de trabalho aparece |
| `som teste 440` | Um lá contínuo por 1 segundo |
| `tocar /sons/sagui.wav` | Assobios agudos (o "canto do sagui") |
| Configurações > Som > `-` e `+` | O volume muda (o clique de cada botão mostra) |
| Jogar o Sagui 3D | Os sons do jogo saem pelo som do computador |
| Copiar um `.wav` do PC para o pendrive e `tocar /usb/arquivo.wav` | Toca (só WAV PCM; MP3 não) |

## Etapa 2: Lenovo G470

| O que fazer | O que deve acontecer |
| --- | --- |
| Ligar e esperar a área de trabalho | Acorde de início pelos alto-falantes |
| `som teste 440` com e sem fone de ouvido | Toca nos alto-falantes; com fone, no fone |
| Linha `[ ok ] Som:` na inicialização | **Fotografe**: ela mostra o codec do G470 e as saídas achadas |

Se não sair som no G470: rode `som`, fotografe a linha `Som:` do boot e me diga se tocou no fone, no alto-falante ou em nenhum.

Quando testar, me diga **"v2 fase 2 ok"** ou o que aconteceu.
