```
Fase 1 da 3.0 pronta: kernel em 64 bits, programas de 32 sem mudança. Teste do Eli no G470 (docs/teste-v3.0-fase1.md)
```

# Status do SaguiOS

_Atualizado em 2026-11-16T07:18:41_

- **Situação:** Aguardando teste do Eli: fase 1
- **Fase atual:** 1
- **História atual:** -
- **Progresso geral:** 66.7%
- **Bloqueio:** nenhum
- **Imagem:** 6535680 bytes úteis (sem teto: só medida)
- **Linhas de código:** boot 1077, kernel 16118, userland 68063

## Fases

| Fase | Nome | Progresso | Estado |
| --- | --- | --- | --- |
| 1 | Kernel em 64 bits, programas de 32 sem mudança | 100.0% | aguardando teste |
| 2 | Programas em 64 bits | 0.0% | a fazer |

## Histórias

| ID | Título | Estado | Minutos |
| --- | --- | --- | --- |
| H127 | Boot em modo longo | aguardando teste | 15 |
| H128 | Base do kernel em 64 bits | aguardando teste | 1 |
| H129 | Memória em 64 bits | aguardando teste | 1 |
| H130 | Processos e troca de contexto em 64 bits | aguardando teste | 1 |
| H131 | Programas de 32 bits em modo de compatibilidade | aguardando teste | 1 |
| H132 | Drivers em 64 bits | aguardando teste | 1 |
| H133 | Janelas e interface em 64 bits | aguardando teste | 1 |
| H134 | A suíte inteira em 64 bits | aguardando teste | 116 |
| H135 | Chamadas de sistema de 64 bits | a fazer |  |
| H136 | libsagui e programas em 64 bits | a fazer |  |
| H137 | O compilador `cc` gera x86-64 | a fazer |  |
| H138 | Entrega da 3.0 | a fazer |  |

## Últimas ações

- 2026-11-16T07:18:41: Fase 1 entregue. Aguardando teste do Eli.
- 2026-11-16T07:18:40: H134 concluída: suíte inteira (88 testes) passa com a imagem de 64 bits; roteiro do G470 e imagem em entrega/v3.0-fase1
- 2026-11-16T05:22:52: Início da H134: A suíte inteira em 64 bits
- 2026-11-16T05:22:52: Erro corrigido: h10 falhou na suíte de 64 bits: etapa de paginação não aparecia
- 2026-11-16T05:22:52: H133 concluída: janelas e pixels compartilhados (0xA0000000) iguais à 2.x; H20-H28, H74-H92 passam
- 2026-11-16T05:22:52: Início da H133: Janelas e interface em 64 bits
- 2026-11-16T05:22:52: H132 concluída: drivers (EHCI, e1000, HDA, vídeo, FAT32, ACPI) com conversões explícitas por uintptr_t; DMA continua abaixo de 4 GB
- 2026-11-16T05:22:52: Início da H132: Drivers em 64 bits
- 2026-11-16T04:23:52: H131 concluída: ELF32 no modo de compatibilidade (CS 0x1B, L=0 D=1), int 0x80 igual, argv lido como uint32_t; H12-H16 H24 passam
- 2026-11-16T04:23:52: H130 concluída: tarefas do kernel e troca de contexto com iretq, FXSAVE por processo, pilha do kernel de 16 KB; H11 H80 passam
- 2026-11-16T04:23:52: H129 concluída: paginação de 4 níveis com o primeiro GB compartilhado, PAT/WC, DMA abaixo de 4 GB; H06 H10 passam
- 2026-11-16T04:23:52: H128 concluída: GDT com código 64/32, TSS64 com IST, IDT de 16 bytes, quadro_t de 64 bits; H03 H05 H09 H17 H18 passam
- 2026-11-16T04:23:52: Início da H131: Programas de 32 bits em modo de compatibilidade
- 2026-11-16T04:23:52: Início da H130: Processos e troca de contexto em 64 bits
- 2026-11-16T04:23:52: Início da H129: Memória em 64 bits
