#!/usr/bin/env python3
"""H43 - placa de rede Intel PRO/1000.

Critério: o kernel acha a placa do QEMU (-nic model=e1000), lê o endereço
físico, vê o cabo ligado e troca pacotes com a rede do QEMU (o DHCP termina,
o que exige mandar e receber).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402

REDE = ["-nic", "user,model=e1000,mac=52:54:00:12:34:56"]


def main():
    with Maquina("h43", "ide", extras=REDE) as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        verificar("[REDE] Intel PRO/1000" in s, "placa Intel PRO/1000 encontrada")
        verificar("endereco fisico 52:54:00:12:34:56" in s, "endereço físico lido da placa")
        verificar("cabo ligado" in s, "cabo ligado (enlace)")
        m.esperar_serial("[REDE] DHCP: endereco", 20)
        verificar(True, "pacotes enviados e recebidos (o DHCP terminou)")
        time.sleep(1)
        out = m.comando("rede")
        n = re.search(r"Pacotes:\s+(\d+) recebidos, (\d+) enviados", out)
        verificar(n and int(n.group(1)) > 0 and int(n.group(2)) > 0, "contadores de pacotes nos dois sentidos")
    return 0


if __name__ == "__main__":
    sys.exit(main())
