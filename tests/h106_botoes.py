#!/usr/bin/env python3
"""H106 - botões da barra de título e destaque do menu com a borda suavizada.

Antes o disco dos botões era feito de linhas de pixels inteiros (3, 5, 6, 7,
7, 8, 8, 8 de meia largura) e o X era uma escada de pixels. Agora são
desenhados com o suave.h (4 x 4 amostras por pixel).

Critérios (medidos nos pixels da captura):
  - a borda do disco de fechar tem pixels de mistura entre o vermelho e a
    barra de título (antes era só vermelho ou só barra);
  - o X tem pixels de mistura entre o branco e o vermelho;
  - o destaque do item do menu do botão direito tem o canto arredondado: a
    ponta é o fundo do menu e o pixel do meio da curva é mistura;
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


def entre(meio, a, b):
    return distancia(meio, a) > 12 and distancia(meio, b) > 12 and \
        distancia(a, meio) + distancia(meio, b) <= distancia(a, b) + 40


def main():
    with Maquina("h106", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n = len(m.serial())
        m.digitar("calculadora\n", 0.03)
        m.esperar_serial('"Calculadora"', 30)
        time.sleep(1.5)
        c = janela(m.serial()[n:], "Calculadora")
        png = m.captura("H106-janela")
        l = c["l"] + 4
        bx, by = c["fx"] + l - 2 - 20, c["fy"] + 4          # o disco de fechar, 16 x 16
        barra = cor_do_pixel(png, bx - 4, by + 8)
        vermelho = cor_do_pixel(png, bx + 2, by + 8)
        borda = [cor_do_pixel(png, bx + i, by + j) for i, j in ((1, 3), (3, 1), (12, 1), (14, 3), (1, 12), (3, 14))]
        mistos = [p for p in borda if entre(p, barra, vermelho)]
        verificar(len(mistos) >= 3, f"a borda do disco de fechar é suavizada ({len(mistos)} de 6 pixels de mistura; barra {barra}, vermelho {vermelho})")
        branco = (255, 255, 255)
        x_pts = [cor_do_pixel(png, bx + i, by + j) for i in range(4, 12) for j in range(4, 12)]
        verificar(sum(1 for p in x_pts if entre(p, vermelho, branco)) >= 6, "o X tem pixels de mistura (não é escada)")

        n = len(m.serial())
        m.clique(260, 596, botao=2)
        m.esperar_serial("[GUI] menu aberto (tipo", 10)
        x, y = map(int, re.findall(r"menu aberto \(tipo \d+, \d+ itens\) em \((\d+),(\d+)\)", m.serial()[n:])[-1])
        m.mouse_para(x + 80, y + 4 + 13)                     # sobre o 1º item: ele fica em destaque
        time.sleep(0.8)
        png = m.captura("H106-menu")
        iy = y + 4 + 1                                       # o destaque começa 1 pixel abaixo do item
        ponta = cor_do_pixel(png, x + 5, iy)
        dentro = cor_do_pixel(png, x + 40, iy + 12)
        fundo = cor_do_pixel(png, x + 40, y + 2)
        meio = cor_do_pixel(png, x + 6, iy + 2)          # (1, 2) do destaque: na curva de raio 6
        verificar(distancia(ponta, dentro) > 40, f"o destaque tem o canto redondo: ponta {ponta}, destaque {dentro}")
        verificar(entre(meio, fundo, dentro), f"a curva do destaque é suavizada: {meio} entre {fundo} e {dentro}")
        m.tecla("esc")


if __name__ == "__main__":
    main()
