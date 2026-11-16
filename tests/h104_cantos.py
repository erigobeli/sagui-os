#!/usr/bin/env python3
"""H104 - cantos arredondados com a borda suavizada nas janelas e nos menus.

Antes, o canto de raio 8 só pulava pixels inteiros (5, 3, 2, 1, 1 por
linha) e a borda ficava em escada. Agora cada pixel da ponta entra na
proporção do quanto fica dentro do círculo (8 x 8 amostras).

Critérios (medidos nos pixels da captura):
  - canto de cima da janela: a ponta é o fundo, o miolo é a barra de título
    e o pixel do meio da curva é uma mistura das duas cores;
  - canto de baixo da janela: também redondo (a ponta é o fundo, não a
    borda), e o pixel do meio da curva é mistura;
  - menu do botão direito: o mesmo nos cantos de cima;
  - capturas para a revisão a olho.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, cor_do_pixel  # noqa: E402


def distancia(a, b):
    return sum(abs(x - y) for x, y in zip(a, b))


def mistura(meio, fora, dentro):
    """meio fica entre as duas pontas: diferente de cada uma e perto da reta."""
    return distancia(meio, fora) > 12 and distancia(meio, dentro) > 12 and \
        distancia(fora, meio) + distancia(meio, dentro) <= distancia(fora, dentro) + 40


def main():
    with Maquina("h104", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n = len(m.serial())
        m.digitar("calculadora\n", 0.03)
        m.esperar_serial('"Calculadora"', 30)
        time.sleep(1.5)
        c = janela(m.serial()[n:], "Calculadora")
        png = m.captura("H104-janela")
        fx, fy = c["fx"], c["fy"]
        fa = c["a"] + 24 + 2

        fora, dentro, meio = cor_do_pixel(png, fx, fy), cor_do_pixel(png, fx + 12, fy + 2), cor_do_pixel(png, fx + 2, fy + 2)   # (12, 2): barra, antes do título
        verificar(distancia(fora, dentro) > 60, f"o canto de cima é redondo: ponta {fora}, barra {dentro}")
        verificar(mistura(meio, fora, dentro), f"a curva de cima é suavizada: {meio} entre {fora} e {dentro}")

        ponta, borda = cor_do_pixel(png, fx, fy + fa - 1), cor_do_pixel(png, fx + 12, fy + fa - 1)
        verificar(distancia(ponta, borda) > 40, f"o canto de baixo é redondo: ponta {ponta}, borda {borda}")
        meio_b = cor_do_pixel(png, fx + 3, fy + fa - 2)    # linha 1, coluna 3 do canto: 156 de 256 dentro
        borda_b = cor_do_pixel(png, fx + 12, fy + fa - 2)
        verificar(mistura(meio_b, ponta, borda_b), f"a curva de baixo é suavizada: {meio_b} entre {ponta} e {borda_b}")

        # O menu do botão direito na área vazia.
        n = len(m.serial())
        m.clique(260, 596, botao=2)   # abaixo do Terminal, sobre o fundo claro
        m.esperar_serial("[GUI] menu aberto (tipo", 10)
        x, y = map(int, re.findall(r"menu aberto \(tipo \d+, \d+ itens\) em \((\d+),(\d+)\)", m.serial()[n:])[-1])
        m.mouse_para(760, 300)                             # o cursor sai de cima do canto
        time.sleep(0.8)
        png = m.captura("H104-menu")
        fora, dentro, meio = cor_do_pixel(png, x, y), cor_do_pixel(png, x + 10, y + 10), cor_do_pixel(png, x + 2, y + 2)
        verificar(distancia(fora, dentro) > 30, f"o canto do menu é redondo: ponta {fora}, menu {dentro}")
        verificar(mistura(meio, fora, dentro), f"a curva do menu é suavizada: {meio} entre {fora} e {dentro}")
        m.tecla("esc")


if __name__ == "__main__":
    main()
