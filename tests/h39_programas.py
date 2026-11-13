#!/usr/bin/env python3
"""H39 - remover programas.

Critério: um programa copiado para /usb/programas aparece na área de trabalho
e roda pelo Terminal pelo nome; a aba Programas das Configurações o remove
(com confirmação) e o ícone some; um programa do sistema pode ter o ícone
escondido.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, janela  # noqa: E402

X0 = 190


def main():
    pen = criar_pendrive("h39-pen", rotulo="DADOS")
    with Maquina("h39", "ide", extras=["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar")
        m.comando("mkdir /usb/programas")
        m.comando("cp /bin/ola /usb/programas/meuola")
        m.esperar_serial("programas instalados no pendrive: 1", 10)
        verificar(True, "o programa copiado para /usb/programas aparece na área de trabalho")
        out = m.comando("meuola funcionou")
        verificar("funcionou" in out, "o Terminal acha o programa instalado pelo nome")
        time.sleep(0.5)
        m.captura("H39-1-instalado")
        m.comando("configuracoes programas")
        m.esperar_serial("configuracoes: aberto na aba Programas", 15)
        time.sleep(0.8)
        j = janela(m.serial(), "Configurações")
        m.captura("H39-2-aba-programas")
        m.clique(j["x"] + X0 + 220 + 45, j["y"] + 76 + 13)       # Remover
        m.esperar_serial('"Remover programa"', 10)
        time.sleep(0.5)
        m.tecla("ret")                                           # Sim
        m.esperar_serial("removido meuola -> 0", 10)
        m.esperar_serial("programas instalados no pendrive: 0", 10)
        verificar(True, "Remover apaga o programa e o ícone some")
        time.sleep(0.5)
        m.clique(j["x"] + X0 + 104 + 42, j["y"] + 132 + 60 + 13)  # Esconder: Banana
        m.esperar_serial("ocultos = banana", 10)
        verificar(True, "o ícone de um programa do sistema pode ser escondido")
        time.sleep(1.5)
        m.captura("H39-3-banana-escondida")
    import subprocess
    r = subprocess.run(["mdir", "-b", "-i", pen + "@@1M", "::/programas"], capture_output=True, text=True)
    verificar("meuola" not in r.stdout.lower(), "mtools: o arquivo do programa saiu do pendrive")
    return 0


if __name__ == "__main__":
    sys.exit(main())
