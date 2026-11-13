# Roteiro de teste: versão 2.0, fase 1 (Base e Configurações)

**Arquivos:** `entrega/v2-fase1/saguios.vdi` (VirtualBox) e `entrega/v2-fase1/saguios.img` (pendrive).
Como gravar e configurar: `docs/como-gravar.md`. Teste primeiro na VM.

## O que mudou

- **Botão de energia** na barra de cima (à esquerda da data): abre a janela **Energia** com **Desligar**, **Reiniciar** e **Cancelar**. Antes de desligar ou reiniciar, o pendrive montado é gravado e liberado. No Terminal: `desligar` e `reiniciar`.
- **Configurações** (ícone novo de engrenagem, ou `configuracoes` no Terminal):
  - **Relógio:** acertar data e hora (grava no relógio da placa-mãe) e o fuso horário.
  - **Aparência:** cinco fundos (Brasil, Mata, Céu, Noite, Terra) e mostrar ou esconder o sagui do canto.
  - **Programas:** programas instalados no pendrive (remover) e esconder ícones do sistema.
  - **Sobre o computador:** processador, memória, tela e dispositivos.
- **As configurações ficam no pendrive de dados** (`/usb/.saguios/configuracoes.txt`). Um pendrive que já tem configurações do SaguiOS é **montado sozinho** ao conectar, então elas valem desde o começo.
- **Programas instalados:** um programa copiado para `/usb/programas/` vira ícone na área de trabalho e comando no Terminal.
- **Comando `pci`:** lista o hardware e diz o que o SaguiOS já usa. Com pendrive montado, grava `/usb/hardware.txt`.
- **Comando `config`:** lê e muda configurações pelo Terminal.
- Os ícones da área de trabalho voltaram ao espaçamento folgado e continuam numa segunda coluna.

## Etapa 1: VirtualBox

Configuração da VM para a versão 2.0 (vale para as próximas fases):

- **Rede:** Configurações > Rede > Adaptador 1 > **Intel PRO/1000 MT Desktop**, modo **NAT**.
- **Áudio:** Configurações > Áudio > Controlador **Intel HD Audio**.

| O que fazer | O que deve acontecer |
| --- | --- |
| Clicar no botão de energia (⏻) na barra de cima | Janela Energia |
| **Desligar** | A VM desliga sozinha (não fica tela preta parada) |
| Ligar de novo, **Reiniciar** pela janela ou `reiniciar` | Volta ao boot |
| Abrir **Configurações > Aparência**, escolher **Noite** | O fundo muda na hora |
| **Configurações > Relógio**: mudar a hora e "Acertar o relógio" | A hora da barra muda |
| **Sobre o computador** | Processador, memória, tela, dispositivos |
| `pci` no Terminal | Placa de rede Intel, som HD Audio e USB listados |

## Etapa 2: Lenovo G470

| O que fazer | O que deve acontecer |
| --- | --- |
| **`pci` no Terminal, com o pendrive de dados montado. Fotografe a tela** | A foto diz qual é o chip de rede e de som do G470 (define o driver de rede, H46) |
| Desligar pelo botão de energia | O notebook desliga sozinho |
| Reiniciar | Volta ao boot |
| Configurações: trocar o fundo, montar o pendrive (duplo clique) e trocar de novo; reiniciar com o pendrive espetado | O pendrive é montado sozinho e o fundo escolhido volta |

Se o notebook não desligar sozinho, aparece "O SaguiOS terminou. Pode desligar o computador." (os dados do pendrive já foram gravados). Fotografe e me diga.

Quando testar, me diga **"v2 fase 1 ok"** ou o que aconteceu.
