#!/usr/bin/env python3
"""H144 - Cipó: seletores do CSS Selectors 4 (motor compilado no computador).

Faixas de 10 px, cada uma verde (ou da cor pedida) se o seletor casou:
  - :first-of-type, :last-of-type, :nth-of-type(2), :nth-last-child(2)
    (contando só a mesma tag, ou do fim);
  - :only-child;
  - :not(.x, .y) com lista; :is(.y) com classe; :where() sem especificidade
    (perde para .c);
  - :has(> img), :has(.fundo) num neto, :has(+ .vizinho);
  - :is(div.q, section) com seletores compostos.
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import verificar, RAIZ, SAIDA  # noqa: E402
from h62_navegador import compilar_referencia  # noqa: E402
from PIL import Image  # noqa: E402

VERDE, AZUL, AMARELO, CIANO = (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255)


def main():
    os.makedirs(SAIDA, exist_ok=True)
    prog = os.path.join(SAIDA, "h144-teste_pagina")
    r = compilar_referencia(prog)
    verificar(r.returncode == 0, "o motor compila no computador" + ("" if r.returncode == 0 else ": " + r.stderr[-800:]))
    cru = os.path.join(SAIDA, "h144.cru")
    r = subprocess.run([prog, os.path.join(RAIZ, "tests", "navegador", "paginas", "seletores.html"), "400", cru],
                       capture_output=True, text=True, timeout=60)
    verificar(r.returncode == 0, "a página foi pintada")
    d = open(cru, "rb").read()
    larg, alt = struct.unpack("<II", d[:8])
    img = Image.frombytes("RGBX", (larg, alt), d[8:8 + larg * alt * 4], "raw", "BGRX").convert("RGB")
    img.save(os.path.join(SAIDA, "H144-seletores.png"))
    px = img.load()
    faixas = [
        (VERDE, ":first-of-type: o primeiro <p>"),
        (VERDE, "<span> entre os <p>"),
        (AMARELO, ":nth-of-type(2): o segundo <p>"),
        (CIANO, ":nth-last-child(2): o penúltimo filho (o terceiro <p>; vale mais que :last-of-type por vir depois)"),
        (VERDE, "<span> no fim"),
        (VERDE, ":only-child"),
        (VERDE, ":not(.x, .y) casa em .a"),
        (AZUL, ":not(.x, .y) não casa em .b.y; :is(.y) casa"),
        (VERDE, ":where(.c) perde para .c (especificidade zero)"),
        (VERDE, ":has(> img)"),
        (AZUL, ":has(.fundo) num neto"),
        (VERDE, ":has(+ .vizinho)"),
        ((0, 0, 0), "o vizinho"),
        (VERDE, ":is(div.q, section)"),
    ]
    # a faixa do .so fica dentro de um div de 10 px: mesma altura
    for k, (cor, txt) in enumerate(faixas):
        p = px[50, 5 + 10 * k]
        verificar(p == cor, f"{txt} ({p})")


if __name__ == "__main__":
    main()
