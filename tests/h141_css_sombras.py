#!/usr/bin/env python3
"""H141 - Cipó: box-shadow e text-shadow (motor compilado no computador).

Critérios:
  - box-shadow com desfoque: escuro logo abaixo da caixa, sumindo até o fim
    do desfoque; dentro da caixa e acima dela, nada;
  - sem desfoque e com espalhamento: a sombra dura sai deslocada e maior que
    a caixa; os cantos redondos da caixa continuam redondos;
  - várias camadas: a de anel (espalhamento sem deslocamento) aparece em
    volta; a "inset" não pinta fora;
  - text-shadow: o texto ganha a cópia deslocada na cor da sombra; com
    "none", não.
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import verificar, RAIZ, SAIDA  # noqa: E402
from h62_navegador import compilar_referencia  # noqa: E402
from PIL import Image  # noqa: E402

LARG = 600


def main():
    os.makedirs(SAIDA, exist_ok=True)
    prog = os.path.join(SAIDA, "h141-teste_pagina")
    r = compilar_referencia(prog)
    verificar(r.returncode == 0, "o motor compila no computador" + ("" if r.returncode == 0 else ": " + r.stderr[-800:]))
    cru = os.path.join(SAIDA, "h141.cru")
    r = subprocess.run([prog, os.path.join(RAIZ, "tests", "navegador", "paginas", "sombras.html"), str(LARG), cru],
                       capture_output=True, text=True, timeout=60)
    verificar(r.returncode == 0, "a página foi pintada")
    dados = open(cru, "rb").read()
    larg, alt = struct.unpack("<II", dados[:8])
    img = Image.frombytes("RGBX", (larg, alt), dados[8:8 + larg * alt * 4], "raw", "BGRX").convert("RGB")
    img.save(os.path.join(SAIDA, "H141-sombras.png"))
    px = img.load()

    def lum(p):
        return sum(px[p]) // 3

    verificar(lum((140, 145)) < 200, f"logo abaixo da caixa a sombra escurece ({px[140, 145]})")
    verificar(lum((140, 145)) < lum((140, 151)) < 255, "e vai clareando ao longo do desfoque")
    verificar(px[140, 170] == (255, 255, 255), "depois do desfoque não sobra nada")
    verificar(px[140, 90] == (255, 255, 255), "dentro da caixa (fundo branco) a sombra não aparece")
    verificar(px[140, 34] == (255, 255, 255), "acima da caixa, nada (a sombra desceu 8 px)")
    verificar(px[503, 90] == (0xCC, 0, 0), f"sombra dura: deslocada 4 px e 2 px maior ({px[503, 90]})")
    verificar(px[499, 90] == (0x33, 0xAA, 0x77), "a caixa verde fica por cima da sombra")
    verificar(px[301, 41] != (0x33, 0xAA, 0x77), "o canto redondo continua redondo")
    verificar(px[37, 250] == (0, 0, 255) and px[140, 302] == (0, 0, 255), "a camada de anel (0 0 0 4px) aparece em volta")
    verificar(px[30, 250] == (255, 255, 255), "a camada inset não pinta do lado de fora")
    verm = sum(1 for x in range(300, LARG) for y in range(200, 255) if px[x, y][0] > 200 and px[x, y][1] < 80)
    verificar(verm > 100, f"text-shadow: {verm} pixels vermelhos atrás do texto")
    verm2 = sum(1 for x in range(300, LARG) for y in range(262, img.size[1]) if px[x, y][0] > 200 and px[x, y][1] < 80)
    verificar(verm2 == 0, "text-shadow: none não pinta sombra")


if __name__ == "__main__":
    main()
