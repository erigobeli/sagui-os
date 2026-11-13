#!/usr/bin/env python3
"""H50 - gerador de números aleatórios do kernel.

Critério: dois boots dão números diferentes; em 64 KB, cada valor de byte
aparece entre 180 e 340 vezes (o esperado é 256); o log diz de onde vem a
aleatoriedade (sem RDRAND, como no G470: o QEMU sem KVM não tem RDRAND).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402


def amostra(nome):
    with Maquina(nome, "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        time.sleep(1)
        out = m.comando("/testes/aleatorio", tempo=60)
        return s, out


def main():
    s1, a = amostra("h50a")
    s2, b = amostra("h50b")
    verificar("[ALEATORIO] poco iniciado" in s1, "o poço é iniciado no boot (" + re.search(r"\[ALEATORIO\] (.*)", s1).group(1).strip() + ")")
    i1 = re.search(r"inicio ([0-9a-f]{32})", a).group(1)
    i2 = re.search(r"inicio ([0-9a-f]{32})", b).group(1)
    verificar(i1 != i2, f"dois boots dão números diferentes ({i1[:12]}... e {i2[:12]}...)")
    for out in (a, b):
        m = re.search(r"menor (\d+) maior (\d+)", out)
        verificar(180 <= int(m.group(1)) and int(m.group(2)) <= 340, f"64 KB bem distribuídos: cada byte entre {m.group(1)} e {m.group(2)} vezes (esperado 256)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
