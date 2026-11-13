# Roteiro de teste: versão 2.0, fase 7 (Compilador C)

**Arquivos:** `entrega/v2-fase7/saguios.vdi` e `entrega/v2-fase7/saguios.img`. Esta imagem também traz o visual novo da fase 9 (vidro, doca, redimensionar), feito em paralelo. Para instalar programas, conecte o pendrive do SaguiOS.

## O que mudou

- **O SaguiOS compila programas em C dentro dele mesmo**, com um compilador escrito do zero: lê o C, monta uma representação própria, gera o código x86, monta e liga com a `libsagui`.
- **`cc arquivo.c -o programa`** no Terminal.
- **Erros em português**, com a linha, a coluna, o trecho com um `^` e uma **dica** do que fazer.
- **Editor:** botões **Compilar e rodar** (ou Ctrl+R) e **Instalar**. Com erro, o cursor vai para a linha do erro; Instalar põe o programa em `/usb/programas` e ele ganha um ícone na área de trabalho.
- O manual da linguagem está em `docs/c-do-sagui.md` (e dentro do sistema, em `/sistema/ajuda/c-do-sagui.md`).
- Prova de fogo: `wc`, `cat`, a Calculadora e a Banana da versão 1.0, compilados pelo compilador do SaguiOS, passam nos mesmos testes das versões feitas pelo GCC.
- Ainda não tem: ponto flutuante (`float`/`double`). O manual diz o que mais fica de fora.

## Etapa 1: VirtualBox

| O que fazer | O que deve acontecer |
| --- | --- |
| Abrir o Editor e digitar o programa abaixo, **sem o `;`** do `imprimir` | |
| Salvar como `/tmp/ola.c` e clicar em **Compilar e rodar** | Uma mensagem com `ola.c:5:...: erro: faltou ';'`, a linha com `^` e a dica; o cursor vai para a linha 5 |
| Pôr o `;` e **Compilar e rodar** de novo | O Terminal mostra "Olá do Eli!" |
| **Instalar** (com o pendrive) | "Instalado em /usb/programas/ola"; o ícone aparece na área de trabalho |
| No Terminal: `cc /tmp/ola.c -o /tmp/ola2` e depois `/tmp/ola2` | Compila e roda |
| No Terminal: `cc` | Mostra como usar |

```c
#include "sagui.h"

int principal(int argc, char **argv)
{
    imprimir("Olá do Eli!\n");
    return 0;
}
```

Se quiser se divertir: abra `/sistema/ajuda/c-do-sagui.md` no Navegador (`navegador /sistema/ajuda/c-do-sagui.md`) e experimente os exemplos.

## Etapa 2: G470

O mesmo do VirtualBox, no notebook: compile e rode o `ola.c`. Me conte quanto tempo a compilação leva lá.

Quando testar, me diga **"v2 fase 7 ok"** ou o que aconteceu.
