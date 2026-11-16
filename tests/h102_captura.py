#!/usr/bin/env python3
"""H102 - captura de tela com a tecla Print Screen (issue #25, parte 5).

Critérios:
  - a tecla Print Screen abre o /bin/captura, que salva um PNG em
    /usb/Capturas quando há pendrive montado;
  - o PNG abre no PIL (confere o zlib e o CRC de cada pedaço) e tem o tamanho
    da tela;
  - a captura bate com a tela do QEMU (pelo menos 97% dos pontos iguais; o
    cursor do mouse não entra na captura);
  - "captura /tmp/t.png" no terminal salva onde se pede.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, mtools, SAIDA  # noqa: E402
from PIL import Image  # noqa: E402


def main():
    pen = criar_pendrive("h102-pen", 64, "H102")
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h102", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        out = m.comando("montar usb0")
        verificar("montado" in out, "pendrive montado em /usb")
        time.sleep(1)
        tela = Image.open(m.captura("H102-tela", guardar=False)).convert("RGB")   # a tela antes da tecla
        antes = len(m.serial())
        m.tecla("print")
        m.esperar_serial("captura: salva /usb/Capturas/", 60)
        r = re.search(r"captura: salva (\S+) \((\d+)x(\d+), (\d+) bytes\)", m.serial()[antes:])
        verificar(r, f"Print Screen salva a tela no pendrive ({r.group(1) if r else '?'})")
        nome_cap = r.group(1)
        time.sleep(1)
        m.captura("H102-aviso")
        time.sleep(2.5)   # o aviso da captura fecha sozinho
        out = m.comando("captura /tmp/t.png")
        verificar("Tela salva em /tmp/t.png" in out, "captura pelo terminal salva no caminho pedido")
        m.comando("desmontar usb0")

    local = os.path.join(SAIDA, "h102-captura.png")
    mtools(pen, "mcopy", "-o", "p:" + nome_cap[len("/usb"):], local)
    png = Image.open(local)
    png.load()   # decodifica tudo: confere o zlib e os CRC dos pedaços
    verificar(png.size == tela.size, f"o PNG da captura abre no PIL com o tamanho da tela {png.size}")
    a, b = png.convert("RGB"), tela
    iguais = sum(1 for x in range(0, a.size[0], 7) for y in range(0, a.size[1], 7)
                 if a.getpixel((x, y)) == b.getpixel((x, y)))
    total = len(range(0, a.size[0], 7)) * len(range(0, a.size[1], 7))
    verificar(iguais >= total * 0.97, f"a captura bate com a tela do QEMU ({iguais} de {total} pontos iguais)")


if __name__ == "__main__":
    main()
