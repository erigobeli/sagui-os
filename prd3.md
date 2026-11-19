# SaguiOS 3.0: 64 bits

> **Situação:** trabalho local no branch `v3/64bits` (não publicado). Só vai para o
> GitHub depois do teste do Eli no G470.

## 1. Objetivo

O SaguiOS rodando em **64 bits (x86-64, "long mode")** no Lenovo G470, no QEMU e no
VirtualBox, **com tudo funcionando do mesmo jeito que na 2.x**: área de trabalho e
janelas, Terminal, Arquivos, Editor, rede (e1000 e o adaptador USB), HTTPS, Cipó,
som, Tocador (WAV e MP3), vídeo, Sagui Code, compilador, jogos e pendrives. Os
testes automáticos da 2.x continuam passando.

## 2. Regras

As mesmas da 2.x (`AGENTS.md`, D041): núcleo 100% do zero, a partir dos manuais da
Intel/AMD (Intel SDM vol. 3, AMD64 APM vol. 2) e das especificações já usadas;
português; comentários explicando o porquê; teste por história; nada no disco
interno do G470 sem confirmação.

## 3. Estratégia: nunca quebrar o sistema no meio

1. **Primeiro o kernel em 64 bits, com os programas de 32 bits sem mudança.** No
   modo longo, o processador roda código de 32 bits em "modo de compatibilidade"
   (segmento de código com L=0, D=1). Os programas atuais (libsagui, Cipó, cc...)
   continuam ELF32 e fazem `int 0x80` como hoje; o kernel atende essas chamadas com
   ponteiros de 32 bits. Ao fim da fase 1, o sistema inteiro roda em 64 bits e
   todos os testes passam, sem recompilar nenhum programa.
2. **Depois, os programas em 64 bits.** libsagui e programas compilados também em
   x86-64 (ELF64, `syscall`/`sysret`), o compilador `cc` ganhando o gerador de
   código x86-64, e os dois tipos de programa rodando juntos.
3. A imagem de 32 bits continua sendo gerada (`make ARQ=32`) até a 3.0 ser
   aprovada no G470.

## 4. Histórias

### Fase 1: kernel em 64 bits, programas de 32 bits sem mudança

**H127. Boot em modo longo.** O estágio 2 confere o CPUID (bit LM), monta tabelas de
página de 4 níveis com a identidade dos primeiros GB, liga PAE, EFER.LME e a
paginação, salta para um segmento de 64 bits e entra no kernel ELF64. *Aceite:* no
QEMU e no VirtualBox, o kernel de 64 bits escreve na tela e na serial; numa CPU sem
64 bits, uma mensagem clara.

**H128. Base do kernel em 64 bits.** GDT com segmentos de 64 bits e de 32 bits
(compatibilidade), TSS de 64 bits (RSP0, IST), IDT de 64 bits, exceções com o
quadro de 64 bits, PIC/PIT, relógio, console de texto e gráfico (VESA). *Aceite:*
H03, H05, H09, H17 e H18 passam com o kernel novo.

**H129. Memória em 64 bits.** Memória física (E820), paginação de 4 níveis para o
kernel e para cada processo, heap do kernel, mapeamento de MMIO (framebuffer com
escrita combinada, PAT), DMA sempre abaixo de 4 GB. *Aceite:* H06 e H10 passam.

**H130. Processos e troca de contexto em 64 bits.** Estrutura de processo com os
registradores de 64 bits, FXSAVE/FXRSTOR, escalonador, tarefas do kernel. *Aceite:*
H11 e H80 passam.

**H131. Programas de 32 bits em modo de compatibilidade.** Carregador ELF32 num
espaço de endereçamento abaixo de 4 GB, `int 0x80` com os números e os ponteiros de
32 bits de hoje (todas as chamadas da 2.x), sinais/erros como antes. *Aceite:* H12,
H13, H14, H15, H16 e H24 passam com os binários atuais.

**H132. Drivers em 64 bits.** PCI, ATA, EHCI/USB (armazenamento e rede CDC), FAT32,
SGFS, i8042, HDA, e1000, ACPI (desligar/reiniciar), vídeo nativo do G470, CMOS,
aleatório. *Aceite:* os testes de cada driver passam (H29 a H48, H79, H84...).

**H133. Janelas e interface em 64 bits.** Gerenciador de janelas, vidro, doca,
menus, área de cópia, notificações. *Aceite:* H20 a H28, H74 a H81, H87, H89, H92.

**H134. A suíte inteira em 64 bits.** `tests/rodar_todos.py` passa com a imagem de
64 bits; o CI ganha a imagem de 64 bits. Roteiro de teste no G470. *Aceite:* todos os
testes passam; o Eli confirma no G470.

> **PARADA:** teste do Eli no G470 com a imagem de 64 bits.

### Fase 2: programas em 64 bits

**H135. Chamadas de sistema de 64 bits.** `syscall`/`sysret` (MSRs STAR, LSTAR,
FMASK), ABI de 64 bits documentada, carregador ELF64. *Aceite:* um programa de
teste de 64 bits roda ao lado dos de 32.

**H136. libsagui e programas em 64 bits.** A libsagui e os programas compilam em
x86-64 (o build gera as duas versões); os programas do sistema passam a ser de 64
bits. *Aceite:* a suíte passa com os programas de 64 bits.

**H137. O compilador `cc` gera x86-64.** Um gerador de código novo no `cc`
(registradores de 64 bits, convenção de chamada própria documentada), mantendo o de
32 bits. *Aceite:* os testes do compilador (H64 a H68) passam nos dois modos.

**H138. Entrega da 3.0.** Imagem em `entrega/v3.0/`, roteiro de teste, decisões,
README. *Aceite:* o Eli confirma no G470.

> **PARADA OBRIGATÓRIA: fim da versão 3.0.**
