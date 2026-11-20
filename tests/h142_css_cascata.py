#!/usr/bin/env python3
"""H142 - Cipó: a cascata dos sites grandes e o calc() (motor compilado no
computador, numa largura de 946, a da janela).

Critérios:
  - @layer: fora de camada vale mais que dentro; entre camadas, vale a
    declarada por último em "@layer base, tema;"; com !important, o
    contrário (a primeira camada ganha até de fora de camada);
  - 6000 classes diferentes: a regra da última ainda casa (a tabela cresce);
  - 1200 variáveis no :root: as regras de depois continuam valendo, e
    var(--v20) acha o valor certo;
  - calc() com %, vw, *, /, parênteses, e clamp/min/max dentro dão a
    largura certa;
  - media queries de intervalo: >=, > estrito, <=, a forma
    "400px <= width <= 1000px" e em (16px).
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import verificar, RAIZ, SAIDA  # noqa: E402
from h62_navegador import compilar_referencia  # noqa: E402
from PIL import Image  # noqa: E402

LARG = 946


def main():
    os.makedirs(SAIDA, exist_ok=True)
    prog = os.path.join(SAIDA, "h142-teste_pagina")
    r = compilar_referencia(prog)
    verificar(r.returncode == 0, "o motor compila no computador" + ("" if r.returncode == 0 else ": " + r.stderr[-800:]))
    cru = os.path.join(SAIDA, "h142.cru")
    r = subprocess.run([prog, os.path.join(RAIZ, "tests", "navegador", "paginas", "cascata.html"), str(LARG), cru],
                       capture_output=True, text=True, timeout=120)
    verificar(r.returncode == 0, "a página foi pintada")
    d = open(cru, "rb").read()
    larg, alt = struct.unpack("<II", d[:8])
    img = Image.frombytes("RGBX", (larg, alt), d[8:8 + larg * alt * 4], "raw", "BGRX").convert("RGB")
    img.save(os.path.join(SAIDA, "H142-cascata.png"))
    px = img.load()

    verificar(px[100, 10] == (0, 0, 255), f"fora de camada vale mais que as camadas ({px[100, 10]})")
    verificar(px[100, 30] == (0, 0, 255), f"!important: a primeira camada (base) ganha de tudo ({px[100, 30]})")
    verificar(px[100, 50] == (0, 0, 255), f"a regra da classe 6000 casa ({px[100, 50]})")
    verificar(px[100, 65] == (0, 255, 0), f"depois de 1200 variáveis, a última regra vale ({px[100, 65]})")
    y = 60 + 20   # o .var começa em 60 e tem var(--v20) = 20px de altura
    verificar(px[100, y - 1] == (0, 255, 0) and px[100, y + 1] != (0, 255, 0), "var(--v20) achou 20px")

    def largura(y):
        x = 0
        while x < larg and px[x, y] == (0, 0, 0):
            x += 1
        return x

    esperado = [
        (y + 5, 946 - 2 * 44.5, "calc(100% - 2 * clamp(20px, calc((100vw - 768px) / 4), 128px))"),
        (y + 25, 300, "min(50%, 300px)"),
        (y + 45, 200, "max(10%, 200px)"),
        (y + 65, (946 - 40) / 3, "calc((100% - 40px) / 3)"),
        (y + 85, 100, "clamp(100px, 10vw, 120px)"),
    ]
    for yy, w, txt in esperado:
        medida = largura(yy)
        verificar(abs(medida - w) <= 1, f"{txt}: {medida} px (esperado {w:g})")
    ymq = y + 90   # depois do .calc5: quatro faixas de 10 px
    mq = [((0, 255, 0), "(width >= 900px) vale em 946"),
          ((0, 0, 255), "(width > 946px) não vale em 946 (estrito); (width <= 946px) vale"),
          ((0, 255, 0), "(400px <= width <= 1000px) vale"),
          ((0, 0, 255), "(60em < width) não vale (960px); (width < 60em) vale")]
    for k, (cor, txt) in enumerate(mq):
        p = px[100, ymq + 5 + 10 * k]
        verificar(p == cor, f"{txt} ({p})")


if __name__ == "__main__":
    main()
