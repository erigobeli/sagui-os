#!/usr/bin/env python3
"""H45 - DHCP e DNS.

Critério: o DHCP do QEMU entrega endereço, máscara, roteador e DNS sozinho no
boot; o comando rede mostra; o DNS resolve um nome (a rede do QEMU repassa a
pergunta ao DNS da VPS: teste com internet); um nome que não existe dá erro
claro.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402
from h43_e1000 import REDE  # noqa: E402


def main():
    with Maquina("h45", "ide", extras=REDE) as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco 10.0.2.15, mascara 255.255.255.0, roteador 10.0.2.2, DNS 10.0.2.3", 20)
        verificar(True, "DHCP: 10.0.2.15, máscara, roteador e DNS")
        time.sleep(1)
        out = m.comando("rede")
        verificar("Endereço:  10.0.2.15" in out and "DNS:       10.0.2.3" in out and "automático (DHCP)" in out,
                  "o comando rede mostra a configuração")
        m.captura("H45-rede")
        out = m.comando("dns example.com", tempo=30)
        verificar("example.com = " in out, f"o DNS resolve example.com ({out.strip().splitlines()[-1] if out.strip() else ''})")
        out = m.comando("dns nao-existe.sagui-teste.invalid", tempo=30)
        verificar("nome não encontrado" in out, "um nome que não existe dá 'nome não encontrado'")
        out = m.comando("ping google.com 1", tempo=30)
        verificar("PING google.com (" in out, "ping google.com resolve o nome antes de pingar")
    return 0


if __name__ == "__main__":
    sys.exit(main())
