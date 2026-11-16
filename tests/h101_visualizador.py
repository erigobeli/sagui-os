#!/usr/bin/env python3
"""H101 - Visualizador de imagens (issue #25, parte 5).

Critérios:
  - PNG, JPEG, WebP e BMP feitos aqui (metade vermelha, metade azul) abrem
    com o tamanho certo e as cores no lugar, conferidas na captura;
  - a seta para a direita passa para a próxima imagem da pasta, em ordem
    alfabética e dando a volta;
  - "+" amplia para 125%;
  - o .sgi dos papéis de parede (/arte/mata-atlantica.sgi) abre com 1024x768
    e a foto aparece na janela;
  - capturas para a revisão a olho.
"""
import io
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, cor_do_pixel  # noqa: E402
from PIL import Image  # noqa: E402


def janela(serial, titulo):
    m = re.findall(rf'criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial)[-1]
    return dict(l=int(m[0]), a=int(m[1]), x=int(m[2]) + 2, y=int(m[3]) + 24)


def bytes_de(im, formato, **kw):
    b = io.BytesIO()
    im.save(b, formato, **kw)
    return b.getvalue()


def cor_perto(a, b, tol):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def main():
    im = Image.new("RGB", (300, 200), (220, 30, 30))
    im.paste((30, 60, 220), (150, 0, 300, 200))
    pen = criar_pendrive("h101-pen", 64, "H101", pastas=("fotos", "estragadas"), arquivos={
        "fotos/foto.png": bytes_de(im, "PNG"),
        "fotos/foto.jpg": bytes_de(im, "JPEG", quality=92),
        "fotos/foto.webp": bytes_de(im, "WEBP", quality=90),
        "fotos/foto.bmp": bytes_de(im, "BMP"),
        # Cabeçalho SGI1 malformado: 32768 x 32769 (o total * 4 daria a volta
        # nos 32 bits) e um RLE que repete cores até o fim do arquivo.
        "estragadas/gigante.sgi": b"SGI1" + (32768).to_bytes(2, "little") + (32769).to_bytes(2, "little")
                                  + bytes(768) + bytes([0xFF, 0x01]) * 4000,
    })
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h101", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        out = m.comando("montar usb0")
        verificar("montado" in out, "pendrive montado em /usb")

        m.digitar("visualizador /usb/fotos/foto.png\n", 0.04)
        m.esperar_serial("visualizador: abriu /usb/fotos/foto.png (300x200", 60)
        verificar(True, "o PNG abre com 300x200")
        time.sleep(1)
        v = janela(m.serial(), "Visualizador")
        area_a = v["a"] - 34 - 24
        ox, oy = v["x"] + (v["l"] - 300) // 2, v["y"] + 34 + (area_a - 200) // 2

        def conferir(nome, tol):
            cap = Image.open(m.captura(f"H101-{nome}", guardar=nome == "png")).convert("RGB")
            esq, dir_ = cap.getpixel((ox + 75, oy + 100)), cap.getpixel((ox + 225, oy + 100))
            verificar(cor_perto(esq, (220, 30, 30), tol) and cor_perto(dir_, (30, 60, 220), tol),
                      f"{nome}: cores no lugar ({esq}, {dir_})")

        conferir("png", 4)
        for ext in ["webp", "bmp", "jpg"]:   # depois de foto.png, em ordem alfabética, dando a volta
            n0 = len(m.serial())
            m.tecla("right")
            fim = time.time() + 30
            while f"abriu /usb/fotos/foto.{ext} (300x200" not in m.serial()[n0:] and time.time() < fim:
                time.sleep(0.2)
            verificar(f"abriu /usb/fotos/foto.{ext} (300x200" in m.serial()[n0:], f"seta para a direita abre foto.{ext}")
            time.sleep(0.5)
            conferir(ext, 6 if ext == "bmp" else 40)
        m.tecla("shift-equal")
        m.esperar_serial("visualizador: zoom 125%", 10)
        verificar(True, "+ amplia para 125%")
        m.tecla("esc")
        m.esperar_serial("(visualizador) terminou com codigo 0", 20)

        n0 = len(m.serial())
        m.digitar("visualizador /arte/mata-atlantica.sgi\n", 0.03)
        m.esperar_serial("visualizador: abriu /arte/mata-atlantica.sgi (1024x768", 30)
        verificar(True, "o .sgi dos papéis de parede abre com 1024x768")
        time.sleep(1.5)
        png = m.captura("H101-sgi")
        a = janela(m.serial()[n0:], "Visualizador")
        cores = {cor_do_pixel(png, a["x"] + a["l"] // 2 + dx, a["y"] + a["a"] // 2 + dy)
                 for dx in (-60, 0, 60) for dy in (-40, 0, 40)}
        verificar(len(cores) >= 4, f"a foto aparece na janela ({len(cores)} cores diferentes em 9 pontos)")
        m.tecla("esc")
        time.sleep(1)

        # Um .sgi com tamanho absurdo no cabeçalho é recusado, sem estourar a memória.
        n0 = len(m.serial())
        m.digitar("visualizador /usb/estragadas/gigante.sgi\n", 0.03)
        m.esperar_serial("visualizador: erro em /usb/estragadas/gigante.sgi", 30)
        time.sleep(1)
        s = m.serial()[n0:]
        verificar("Prote" not in s and "Falta de p" not in s,
                  "o .sgi malformado (32768 x 32769) é recusado sem erro de memória")


if __name__ == "__main__":
    main()
