# Status do SaguiOS

_Atualizado em 2026-11-16T05:22:52_

- **Situação:** Trabalhando na H107: A suíte inteira em 64 bits
- **Fase atual:** 1
- **História atual:** H107
- **Progresso geral:** 58.3%
- **Bloqueio:** nenhum
- **Imagem:** 6535680 bytes úteis (sem teto: só medida)
- **Linhas de código:** boot 1077, kernel 16118, userland 68063

## Fases

| Fase | Nome | Progresso | Estado |
| --- | --- | --- | --- |
| 1 | Kernel em 64 bits, programas de 32 sem mudança | 87.5% | em andamento |
| 2 | Programas em 64 bits | 0.0% | a fazer |

## Histórias

| ID | Título | Estado | Minutos |
| --- | --- | --- | --- |
| H100 | Boot em modo longo | feito | 15 |
| H101 | Base do kernel em 64 bits | feito | 1 |
| H102 | Memória em 64 bits | feito | 1 |
| H103 | Processos e troca de contexto em 64 bits | feito | 1 |
| H104 | Programas de 32 bits em modo de compatibilidade | feito | 1 |
| H105 | Drivers em 64 bits | feito | 1 |
| H106 | Janelas e interface em 64 bits | feito | 1 |
| H107 | A suíte inteira em 64 bits | em andamento |  |
| H108 | Chamadas de sistema de 64 bits | a fazer |  |
| H109 | libsagui e programas em 64 bits | a fazer |  |
| H110 | O compilador `cc` gera x86-64 | a fazer |  |
| H111 | Entrega da 3.0 | a fazer |  |

## Últimas ações

- 2026-11-16T05:22:52: Início da H107: A suíte inteira em 64 bits
- 2026-11-16T05:22:52: Erro corrigido: h10 falhou na suíte de 64 bits: etapa de paginação não aparecia
- 2026-11-16T05:22:52: H106 concluída: janelas e pixels compartilhados (0xA0000000) iguais à 2.x; H20-H28, H74-H92 passam
- 2026-11-16T05:22:52: Início da H106: Janelas e interface em 64 bits
- 2026-11-16T05:22:52: H105 concluída: drivers (EHCI, e1000, HDA, vídeo, FAT32, ACPI) com conversões explícitas por uintptr_t; DMA continua abaixo de 4 GB
- 2026-11-16T05:22:52: Início da H105: Drivers em 64 bits
- 2026-11-16T04:23:52: H104 concluída: ELF32 no modo de compatibilidade (CS 0x1B, L=0 D=1), int 0x80 igual, argv lido como uint32_t; H12-H16 H24 passam
- 2026-11-16T04:23:52: H103 concluída: tarefas do kernel e troca de contexto com iretq, FXSAVE por processo, pilha do kernel de 16 KB; H11 H80 passam
- 2026-11-16T04:23:52: H102 concluída: paginação de 4 níveis com o primeiro GB compartilhado, PAT/WC, DMA abaixo de 4 GB; H06 H10 passam
- 2026-11-16T04:23:52: H101 concluída: GDT com código 64/32, TSS64 com IST, IDT de 16 bytes, quadro_t de 64 bits; H03 H05 H09 H17 H18 passam
- 2026-11-16T04:23:52: Início da H104: Programas de 32 bits em modo de compatibilidade
- 2026-11-16T04:23:52: Início da H103: Processos e troca de contexto em 64 bits
- 2026-11-16T04:23:52: Início da H102: Memória em 64 bits
- 2026-11-16T04:23:52: Início da H101: Base do kernel em 64 bits
- 2026-11-16T04:23:52: H100 concluída: estágio 2 entra no modo longo quando o kernel é ELF64; mensagem clara em CPU sem 64 bits
