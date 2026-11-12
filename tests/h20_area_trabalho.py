#!/usr/bin/env python3
"""H20 - área de trabalho.

Critério: captura da área de trabalho em docs/capturas/ (fundo com degradê
em cores do Brasil, o mascote sagui em pixel art, barra superior com o nome
do sistema e o relógio CMOS).
"""
import datetime
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, cor_do_pixel, RAIZ  # noqa: E402


def cor_do_fundo(x, y):
    """Decodifica rootfs/arte/fundo.sgi e devolve a cor de um pixel."""
    d = open(os.path.join(RAIZ, "rootfs/arte/fundo.sgi"), "rb").read()
    largura = d[4] | d[5] << 8
    k = 8 + d[776 + y * largura + x] * 3
    return tuple(d[k:k + 3])


def main():
    for modo in ("ide", "usb"):
        with Maquina("h20", modo) as m:
            m.esperar_serial("area de trabalho no ar", 120)
            m.esperar_serial("[GUI] relogio:", 20)
            png = m.captura("H20-area-de-trabalho", guardar=(modo == "ide"))
            verificar("[GUI] fundo: /arte/fundo.sgi 1024x768" in m.serial(), f"[{modo}] papel de parede carregado")
            for x, y in ((1000, 40), (1000, 384), (1000, 760)):   # à direita do Terminal, que abre no boot
                verificar(cor_do_pixel(png, x, y) == cor_do_fundo(x, y), f"[{modo}] pixel ({x},{y}) igual ao do fundo.sgi")
            verificar(cor_do_pixel(png, 300, 2) == (0x18, 0x20, 0x2A), f"[{modo}] barra superior escura")
            verificar(cor_do_pixel(png, 836, 595) == (245, 245, 240), f"[{modo}] tufo branco do sagui no canto")
            pasta = cor_do_pixel(png, 47, 65)
            verificar(pasta[0] > 200 and pasta[1] > 140 and pasta[2] < 130, f"[{modo}] ícone da pasta (Arquivos) {pasta}")
            hora = re.findall(r"\[GUI\] relogio: (\S+) (\d\d/\d\d/\d{4})\s+(\d\d):(\d\d)", m.serial())[-1]
            agora = datetime.datetime.now(datetime.timezone.utc)
            verificar(hora[1] == agora.strftime("%d/%m/%Y"), f"[{modo}] relógio da barra com a data do CMOS: {' '.join(hora)}")
            verificar("Terminal provis" not in m.serial(), f"[{modo}] sem terminal de tela cheia")
    return 0


if __name__ == "__main__":
    sys.exit(main())
