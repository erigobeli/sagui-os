#!/usr/bin/env python3
"""H145 - Cipó: object-fit, object-position e aspect-ratio (motor compilado no
computador).

Uma imagem de 200x100 (metade esquerda vermelha, direita azul) em caixas de
100x100:
  - fill (o padrão): estica e enche a caixa;
  - contain: 100x50, centrada (faixas brancas em cima e embaixo);
  - cover: 200x100 centrada e cortada na caixa (nada pinta fora dela);
  - none com object-position: left top: só o vermelho aparece;
  - scale-down numa caixa maior que a imagem: fica no tamanho natural.
aspect-ratio: 2/1 numa caixa de 200 de largura dá 100 de altura; com
conteúdo mais alto, a caixa cresce; e um vídeo de largura 320 com 16/9 fica
com 180.
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import verificar, SAIDA  # noqa: E402
from h62_navegador import compilar_referencia  # noqa: E402
from PIL import Image  # noqa: E402

V, A, B = (255, 0, 0), (0, 0, 255), (255, 255, 255)

PAGINA = """<!doctype html><html><head><meta charset="utf-8"><style>
body{margin:0} img{display:block;position:absolute;width:100px;height:100px;top:0}
.r{position:absolute;left:0;width:200px;aspect-ratio:2/1;background:#00ff00}
</style></head><body>
<img src="duas.png" style="left:0">
<img src="duas.png" style="left:120px;object-fit:contain">
<img src="duas.png" style="left:240px;object-fit:cover">
<img src="duas.png" style="left:360px;object-fit:none;object-position:left top">
<img src="duas.png" style="left:480px;width:300px;height:200px;object-fit:scale-down">
<div class="r" style="top:220px"></div>
<div class="r" style="top:340px;background:#000000"><div style="height:150px;width:10px;background:#ffff00"></div></div>
<video style="position:absolute;left:300px;top:220px;width:320px;aspect-ratio:16 / 9;background:#00ffff"></video>
</body></html>
"""


def main():
    pasta = os.path.join(SAIDA, "h145")
    os.makedirs(pasta, exist_ok=True)
    im = Image.new("RGB", (200, 100), V)
    im.paste(A, (100, 0, 200, 100))
    im.save(os.path.join(pasta, "duas.png"))
    open(os.path.join(pasta, "pagina.html"), "w").write(PAGINA)
    prog = os.path.join(SAIDA, "h145-teste_pagina")
    r = compilar_referencia(prog)
    verificar(r.returncode == 0, "o motor compila no computador" + ("" if r.returncode == 0 else ": " + r.stderr[-800:]))
    cru = os.path.join(SAIDA, "h145.cru")
    r = subprocess.run([prog, os.path.join(pasta, "pagina.html"), "800", cru], capture_output=True, text=True, timeout=60)
    verificar(r.returncode == 0, "a página foi pintada")
    d = open(cru, "rb").read()
    larg, alt = struct.unpack("<II", d[:8])
    img = Image.frombytes("RGBX", (larg, alt), d[8:8 + larg * alt * 4], "raw", "BGRX").convert("RGB")
    img.save(os.path.join(SAIDA, "H145-imagens.png"))
    px = img.load()

    def perto(p, q):
        return all(abs(a - b) <= 40 for a, b in zip(p, q))

    verificar(perto(px[10, 50], V) and perto(px[90, 50], A) and perto(px[25, 3], V), "fill: estica e enche a caixa")
    verificar(px[170, 10] == B and px[170, 90] == B and perto(px[125, 50], V) and perto(px[215, 50], A),
              f"contain: 100x50 centrada ({px[170, 10]}, {px[125, 50]})")
    verificar(perto(px[245, 50], V) and perto(px[335, 50], A) and px[235, 50] == B and px[345, 50] == B,
              f"cover: cortada na caixa ({px[245, 50]}, {px[335, 50]}, fora: {px[235, 50]}, {px[345, 50]})")
    verificar(perto(px[365, 5], V) and perto(px[455, 95], V), "none + left top: só a metade vermelha cabe")
    verificar(perto(px[530, 50], V) and perto(px[630, 50], A) and px[530, 160] == B and px[530, 10] == B,
              f"scale-down: tamanho natural (200x100), centrada ({px[530, 10]}, {px[530, 160]})")
    verificar(px[100, 315] == (0, 255, 0) and px[100, 325] == B, f"aspect-ratio 2/1 com 200 de largura: 100 de altura ({px[100, 325]})")
    verificar(px[100, 485] == (0, 0, 0) and alt == 490, "com conteúdo de 150, a caixa cresce para 150")
    verificar(px[400, 395] != B and px[400, 405] == B, f"vídeo 320 de largura com 16/9: 180 de altura ({px[400, 405]})")


if __name__ == "__main__":
    main()
