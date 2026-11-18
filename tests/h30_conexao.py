#!/usr/bin/env python3
"""H30 - enumeração USB, hubs e conexão a quente.

Critério: o sistema lista fabricante, produto e classe do dispositivo;
conectar e remover o pendrive pelo monitor do QEMU gera os eventos de
conexão e remoção, registrados na serial.
Observação: o hub virtual do QEMU 8.2 é USB 1.1 e o QEMU recusa ligá-lo ao
EHCI (D024); o caminho do hub é validado no G470, onde todo dispositivo fica
atrás do hub interno do chipset.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_console, criar_pendrive  # noqa: E402


def main():
    pen = criar_pendrive("h30-pen", rotulo="QUENTE")
    with Maquina("h30", "ide", imagem=imagem_console(), extras=["-device", "usb-ehci,id=ehci"]) as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("[USB] conectado" not in m.serial(), "sem pendrive no boot")
        m.monitor(f"drive_add 0 if=none,id=quente,format=raw,file={pen}")
        m.monitor("device_add usb-storage,bus=ehci.0,drive=quente,id=pen-quente,removable=on")
        m.esperar_serial("[USB] conectado", 15)
        m.esperar_serial("[DISCO] usb0", 15)
        s = m.serial()
        linha = re.search(r"\[USB\] conectado: (.*)", s).group(1)
        verificar("QEMU" in linha and "USB HARDDRIVE" in linha and "armazenamento" in linha,
                  f"conectado com o sistema ligado: fabricante, produto e classe ({linha.strip()})")
        verificar('rotulo "QUENTE"' in s, "o formato e o rótulo são reconhecidos")
        out = m.comando("discos")
        verificar("QUENTE" in out and "não montado" in out, "discos mostra o pendrive conectado a quente")
        m.monitor("device_del pen-quente")
        m.esperar_serial("[USB] removido", 15)
        verificar("Pendrive removido" in m.serial(), "a remoção gera o evento na serial")
        out = m.comando("discos")
        # O HD do sistema (hd0, D062) continua na lista: o que some é o pendrive.
        verificar("QUENTE" not in out and "usb0" not in out, "depois de remover, discos não mostra mais o pendrive")
        # Conecta de novo, para ver que o ciclo se repete.
        m.monitor(f"drive_add 0 if=none,id=quente2,format=raw,file={pen}")
        m.monitor("device_add usb-storage,bus=ehci.0,drive=quente2,id=pen-quente2,removable=on")
        m.esperar_serial("usb1:", 15)
        verificar(True, "conectar de novo funciona (vira usb1)")
        m.captura("H30-conexao-a-quente")
    return 0


if __name__ == "__main__":
    sys.exit(main())
