#!/usr/bin/env python3
"""H108 - ícone da Lixeira quando só a lixeira do pendrive tem coisas.

O ícone fica "cheio" se /.lixeira ou /usb/.lixeira tiver arquivos, mas abria
sempre /.lixeira. Com coisas só no pendrive, o ícone cheio abria uma lixeira
vazia. Agora ele abre a lixeira que tem coisas.

Critérios:
  - com um arquivo em /usb/.lixeira e /.lixeira vazia, o ícone fica cheio e
    passa a abrir /usb/.lixeira;
  - abrir o ícone mostra o arquivo do pendrive no Arquivos;
  - captura para a revisão a olho.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu  # noqa: E402


def main():
    pen = criar_pendrive("h108-pen", 64, "H108", pastas=(".lixeira",), arquivos={
        ".lixeira/velho.txt": b"apagado no pendrive\n",
        ".lixeira/.origens": b"velho.txt\t/usb/velho.txt\n",
    })
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h108", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar usb0")
        m.esperar_serial("[GUI] lixeira abre /usb/.lixeira", 15)
        verificar("[GUI] lixeira cheia" in m.serial(), "com coisas só no pendrive, o ícone fica cheio")
        verificar(True, "e passa a abrir a lixeira do pendrive")
        # Com o pendrive, a Lixeira é o 10º ícone (sempre o último): 2ª coluna,
        # 3ª linha (7 por coluna em 1024x768, como no h103).
        time.sleep(0.5)
        n = len(m.serial())
        m.clique(20 + 110 + 30, 40 + 2 * 92 + 30, duplo=True)
        m.esperar_serial("[GUI] icone Lixeira -> /bin/arquivos", 10)
        m.esperar_serial("arquivos: pasta /usb/.lixeira (1 itens)", 30)
        verificar(True, "o duplo clique no ícone abre a lixeira do pendrive, com o arquivo")
        time.sleep(1)
        m.captura("H108-lixeira-pendrive")


if __name__ == "__main__":
    main()
