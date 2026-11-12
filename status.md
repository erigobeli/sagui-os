# Status do SaguiOS

_Atualizado em 2026-11-12T14:10:45_

- **Situação:** Fase 7 confirmada no hardware. Seguindo para a fase 8.
- **Fase atual:** 7
- **História atual:** -
- **Progresso geral:** 100.0%
- **Bloqueio:** nenhum
- **Imagem:** 575488 bytes úteis (meta 1474560 bytes = 1,44 MB)
- **Linhas de código:** boot 903, kernel 9614, userland 6317

## Fases

| Fase | Nome | Progresso | Estado |
| --- | --- | --- | --- |
| 1 | Boot | 100.0% | feito |
| 2 | Kernel e terminal | 100.0% | feito |
| 3 | Processos e programas | 100.0% | feito |
| 4 | Gráfico e mouse | 100.0% | feito |
| 5 | Janelas e programas | 100.0% | feito |
| 6 | Pendrive de dados | 100.0% | feito |
| 7 | Bônus (B3 bip, B4 Sagui 3D) | 100.0% | feito |

## Histórias

| ID | Título | Estado | Minutos |
| --- | --- | --- | --- |
| H01 | Preparar o ambiente | feito | 2 |
| H02 | Estágio 1 (MBR) | feito | 8 |
| H03 | Estágio 2 e modo protegido | feito | 1 |
| H04 | Imagem para pendrive e VirtualBox | feito | 4 |
| H05 | Interrupções e relógio | feito | 4 |
| H06 | Memória física | feito | 4 |
| H07 | Teclado PS/2 com ABNT2 | feito | 6 |
| H08 | Sistema de arquivos SGFS | feito | 2 |
| H09 | Terminal provisório do kernel | feito | 2 |
| H10 | Paginação | feito | 3 |
| H11 | Processos e escalonador | feito | 3 |
| H12 | Ring 3 e chamadas de sistema | feito | 4 |
| H13 | Carregador ELF e libsagui | feito | 5 |
| H14 | Arquivos e pastas graváveis | feito | 4 |
| H15 | Pipes | feito | 2 |
| H16 | Shell e comandos como programas | feito | 14 |
| H17 | Framebuffer VESA | feito | 2 |
| H18 | Fonte própria | feito | 8 |
| H19 | Mouse / touchpad PS/2 | feito | 54 |
| H20 | Área de trabalho | feito | 9 |
| H21 | Gerenciador de janelas | feito | 7 |
| H22 | Kit gráfico da libsagui | feito | 3 |
| H23 | Arquivos | feito | 4 |
| H24 | Terminal em janela | feito | 3 |
| H25 | Editor de texto | feito | 3 |
| H26 | Calculadora | feito | 2 |
| H27 | Sagui Pega Banana | feito | 2 |
| H28 | Acabamento | feito | 2 |
| H29 | PCI e controlador EHCI | feito | 1 |
| H30 | Enumeração USB, hubs e conexão a quente | feito | 1 |
| H31 | Armazenamento em massa | feito | 1 |
| H32 | FAT32: leitura | feito | 2 |
| H33 | FAT32: gravação | feito | 2 |
| H34 | Integração do pendrive | feito | 5 |
| B1 | Bônus: enxergar o HD interno | Cancelado pelo Eli: o SaguiOS nunca grava no HD, então listar as partições não acrescenta nada ao projeto. |  |
| B2 | Bônus: boot por UEFI | Cancelado pelo Eli: o objetivo do SaguiOS é rodar em computadores antigos, com BIOS tradicional. |  |
| B3 | Bônus: bip | feito | 5 |
| B4 | Bônus: Sagui 3D | feito | 43 |

## Últimas ações

- 2026-11-04T00:19:30: Eli confirmou: funcionou tudo perfeitamente (bônus)
- 2026-11-03T23:45:56: Fase 7 entregue. Aguardando teste do Eli.
- 2026-11-03T23:45:56: B4 concluída: Sagui 3D: motor 2,5D próprio, três fases em texto, macacos-prego, sementes, cascas, porteira da banana, cacho dourado, sons com o bip; teste automático e capturas
- 2026-11-03T23:32:48: B4 Sagui 3D: motor 2,5D, três fases, macacos-prego, bananas, cascas, porteira e cacho dourado; teste automático passando
- 2026-11-03T23:02:29: Início da B4: Bônus: Sagui 3D
- 2026-11-03T23:02:23: B3 concluída: Comando bip, sons no Sagui Pega Banana, fila de notas tocada pelo tique do relógio (D029); canal 2 conferido no QEMU
- 2026-11-03T22:57:35: Início da B3: Bônus: bip
- 2026-11-03T22:57:35: Fase 6 aprovada pelo Eli. Fase bônus replanejada: B1 e B2 cancelados pelo Eli, B3 mantido, B4 (Sagui 3D) novo.
- 2026-11-03T22:57:09: Eli confirmou: fase 6 ok
- 2026-11-03T21:56:30: Fase 6 entregue. Aguardando teste do Eli.
- 2026-11-03T21:55:57: Erro corrigido: G470: com dois pendrives, o de dados não aparece; usb mostra hub do EHCI 0 parado na configuração
- 2026-11-03T21:36:41: Fase 6 entregue. Aguardando teste do Eli.
- 2026-11-03T21:26:31: Erro corrigido: FAT32 criada depois da partição do SaguiOS não é vista
- 2026-11-03T21:26:31: Erro corrigido: G470: pendrive não aparece; usb mostra porta ocupada sem pendrive
- 2026-11-03T21:21:11: Bloqueio: G470: o pendrive não aparece (fase 6). Correção em andamento: H30-fix
