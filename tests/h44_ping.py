#!/usr/bin/env python3
"""H44 - Ethernet, ARP, IPv4, ICMP e UDP.

Critério: ping 10.0.2.2 (o roteador da rede do QEMU) responde, com tempo e
perda; o ARP aprendeu o endereço físico do roteador.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402
from h43_e1000 import REDE  # noqa: E402


def main():
    with Maquina("h44", "ide", extras=REDE) as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 20)
        time.sleep(1)
        out = m.comando("ping 10.0.2.2 3", tempo=30)
        verificar("resposta de 10.0.2.2: seq=1" in out, "o roteador responde ao ping")
        verificar("3 enviados, 3 respostas, 0% de perda" in out, "três respostas, sem perda")
        m.captura("H44-ping")
        out = m.comando("ping 10.0.2.99 1", tempo=30)
        verificar("sem resposta" in out and "100% de perda" in out, "um endereço que não existe dá perda (sem travar)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
