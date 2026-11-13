# Roteiro de teste: versão 2.0, fase 5 (Terminal e SSH)

**Arquivos:** `entrega/v2-fase5/saguios.vdi` e `entrega/v2-fase5/saguios.img`. Teste primeiro na VM, com a rede como na fase 3 (Intel PRO/1000, NAT). Para a chave ficar guardada, conecte o pendrive do SaguiOS (o que tem a pasta `.saguios`).

## O que mudou

- **`ssh usuario@servidor`** (e `ssh -p porta ...`, `ssh usuario@servidor comando`): um cliente SSH 2 escrito do zero (com ajuda de um subagente no Claude Fable 5.2), só a partir das RFCs.
- **`ssh-chave`:** cria a sua chave Ed25519 no pendrive (`/usb/.saguios/ssh/chave`) e mostra a linha pública, para pôr no `~/.ssh/authorized_keys` do servidor. `ssh-chave nova` troca a chave.
- **Servidores conhecidos:** na primeira vez o SaguiOS mostra a impressão digital do servidor e pergunta se você confia; se depois a chave mudar, ele recusa com um aviso em vermelho.
- **Terminal:** setas, Home/End, PgUp/PgDn e Delete funcionam no vim, no htop, no nano etc. do servidor.

## Etapa 1: VirtualBox

Use um servidor seu (a VPS, por exemplo). Confira antes a impressão digital dele, no próprio servidor:
`ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub`

| O que fazer | O que deve acontecer |
| --- | --- |
| `ssh seu_usuario@ip_do_servidor` | "O SaguiOS ainda não conhece o servidor..." com a impressão digital: compare com a do `ssh-keygen` acima e responda `s` |
| Digitar a senha | Não aparece nada enquanto digita; entra no shell do servidor. **Este é o primeiro teste de login por senha num servidor de verdade (o teste automático não consegue testar a senha aceita).** |
| `htop` | A tela do htop, com cores e barras; `q` sai |
| `vim teste.txt`, digitar, usar as setas, `:wq` | Edita e grava |
| `exit` | "Conexão com ... encerrada (código 0)" e volta para o `sagui:/$` |
| `ssh-chave` | Mostra a linha `ssh-ed25519 AAAA... saguios` |
| No servidor, acrescentar essa linha ao `~/.ssh/authorized_keys` | |
| `ssh seu_usuario@ip_do_servidor` de novo | Não pergunta nada sobre o servidor e entra **sem pedir senha** |

Se o servidor só aceitar login por "keyboard-interactive" (alguns aceitam senha só por esse método), o SaguiOS vai dizer que o servidor não aceita senha: me avise que eu acrescento.

## Etapa 2: G470

A rede com cabo do G470 ainda depende da história H46 (foto do `pci`). Por enquanto, confira no G470 que nada quebrou.

Quando testar, me diga **"v2 fase 5 ok"** ou o que aconteceu.
