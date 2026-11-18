#!/usr/bin/env python3
"""H114 - interface mais leve (issue #25, parte 6): o menu Iniciar redesenha
só as células que mudam quando o mouse passa.

Antes, cada célula sob o mouse refazia o corpo do menu e, pelo vidro, o menu
inteiro com a sombra. Agora só a célula que perdeu o destaque e a que ganhou.
Critério (falsificável): depois de passar o mouse pela grade inteira, a tela
é igual, pixel a pixel, à do menu desenhado do zero com o mouse na mesma
célula. Se sobrar destaque velho em alguma célula, ou faltar o novo, difere.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, SAIDA  # noqa: E402

GRADE_X, GRADE_Y, CEL_L, CEL_A = 4 + (620 - 5 * 108) // 2, 24 + 4 + 106, 108, 92


def celula(k):
    return GRADE_X + (k % 5) * CEL_L + CEL_L // 2, GRADE_Y + (k // 5) * CEL_A + CEL_A // 2


def diferentes(a, b):
    """Pixels diferentes entre duas capturas, sem a barra de cima (o relógio)."""
    r = subprocess.run(["compare", "-metric", "AE", "-extract", "1024x744+0+24", a, b, "null:"],
                       capture_output=True, text=True)
    return int(float(r.stderr.split()[0]))


def main():
    with Maquina("h114", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        m.clique(40, 12)
        m.esperar_serial("[GUI] menu aberto", 10)
        time.sleep(1)
        for volta in range(2):                  # passa pela grade inteira, duas vezes
            for k in list(range(20)) + list(range(19, -1, -1)):
                m.mouse_mover(*celula(k))
        alvo = 7
        m.mouse_mover(*celula(alvo))
        time.sleep(1)
        depois = m.captura("H114-1-depois-de-passar")
        # O mesmo menu desenhado do zero: fecha, abre de novo pela tecla
        # Windows (o mouse fica onde está) e mexe um pixel para o destaque.
        m.tecla("esc")
        time.sleep(1)
        m.tecla("meta_l")
        time.sleep(1.2)
        x, y = celula(alvo)
        m.mouse_mover(x + 1, y)
        m.mouse_mover(x, y)
        time.sleep(1)
        zero = m.captura("H114-2-do-zero")
        n = diferentes(depois, zero)
        verificar(n == 0, f"depois de passar o mouse pela grade, o menu é igual ao desenhado do zero ({n} pixels diferentes)")


if __name__ == "__main__":
    main()
