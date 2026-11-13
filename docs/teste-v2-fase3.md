# Roteiro de teste: versão 2.0, fase 3 (Rede)

**Arquivos:** `entrega/v2-fase3/saguios.vdi` e `entrega/v2-fase3/saguios.img`. Teste primeiro na VM.

## O que mudou

- **Driver de rede Intel PRO/1000** e uma **pilha de rede própria**: Ethernet, ARP, IPv4, ICMP, UDP e DHCP (o SaguiOS pede o endereço ao roteador sozinho).
- Linha nova na inicialização: `[ ok ] Rede: Intel PRO/1000 (...)`.
- **Ícone de rede** na barra de cima: verde (conectado), amarelo (pedindo endereço), cinza com um x vermelho (sem cabo).
- **Configurações > Rede:** estado, endereço automático (DHCP) ou manual (endereço, máscara, roteador, DNS).
- **Terminal:** `rede` (placa, endereço, roteador, DNS, pacotes), `ping google.com`, `dns example.com`.
- Também já está no sistema o **TCP** (conexões confiáveis), base da fase 4.

## Etapa 1: VirtualBox

Configurações da VM > **Rede** > Adaptador 1: **Intel PRO/1000 MT Desktop**, modo **NAT**.

| O que fazer | O que deve acontecer |
| --- | --- |
| Ligar a VM | `[ ok ] Rede: ...` e, alguns segundos depois, o ícone de rede fica verde |
| `rede` | Endereço (no NAT do VirtualBox costuma ser 10.0.2.15), roteador e DNS |
| `ping google.com` | Respostas com o tempo em ms (se o seu PC ou a rede bloquearem ping, aparece "sem resposta": o importante é o nome virar um IP) |
| `dns wikipedia.org` | Mostra o IP |
| Desligar o cabo da VM (Dispositivos > Rede > Conectar cabo de rede) | O ícone fica cinza com x; religando, volta ao verde |
| Configurações > Rede > manual, com outro endereço da mesma rede | `rede` mostra o endereço novo |

## Etapa 2: Lenovo G470

**A rede com cabo do G470 ainda não tem driver** (história H46). Ela depende da foto do comando `pci` que pedi na fase 1: com ela eu sei qual é o chip de rede do notebook. Se o fabricante não publicar o manual do chip, a saída é um adaptador USB-Ethernet barato que siga o padrão aberto USB CDC; eu digo qual comprar.

No G470, por enquanto: confira que nada quebrou (boot, área de trabalho, som da fase 2) e me mande a foto do `pci` com o pendrive de dados montado (`/usb/hardware.txt` também serve).

Quando testar, me diga **"v2 fase 3 ok"** ou o que aconteceu.
