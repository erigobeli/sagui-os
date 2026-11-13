#!/usr/bin/env python3
"""H35 - comando pci e inventário de hardware.

Critério: no QEMU, com placa de rede Intel, som Intel HD Audio e EHCI, o
comando pci lista os três com o nome da classe e diz o que o SaguiOS já usa;
com um pendrive montado, o relatório é gravado em /usb/hardware.txt.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, mtype  # noqa: E402

V2 = ["-nic", "user,model=e1000", "-device", "intel-hda", "-device", "hda-duplex"]


def main():
    pen = criar_pendrive("h35-pen", rotulo="DADOS")
    with Maquina("h35", "ide", extras=V2 + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar")
        out = m.comando("pci")
        verificar("Dispositivos PCI:" in out, "pci lista o barramento")
        verificar("Rede com cabo (Ethernet)" in out and "PRO/1000" in out, "placa de rede Intel PRO/1000 reconhecida")
        verificar("Som (HD Audio)" in out, "controlador de som HD Audio reconhecido")
        verificar("USB 2.0 (EHCI)" in out and "driver EHCI do SaguiOS" in out, "EHCI listado como já suportado")
        verificar("ainda sem driver" in out or "driver Intel PRO/1000 do SaguiOS" in out,
                  "o relatório diz quem cuida de cada placa (ou que ainda não tem driver)")
        verificar("gravado em /usb/hardware.txt" in out, "relatório gravado no pendrive")
        m.captura("H35-pci")
        m.comando("desmontar")
    texto = mtype(pen, "/hardware.txt").decode("latin-1")
    verificar("PRO/1000" in texto, "mtools: /hardware.txt tem o relatório")
    return 0


if __name__ == "__main__":
    sys.exit(main())
