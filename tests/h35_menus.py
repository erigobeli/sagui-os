#!/usr/bin/env python3
"""
H35: menus de contexto, papel de parede, minimizar e maximizar.

Critério: o botão direito abre o menu da área de trabalho; o papel de parede
muda pelo menu; o Terminal minimiza, volta pela barra e maximiza de verdade
(a grade acompanha a janela e o shell continua respondendo).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from qemu import Maquina, verificar, cor_do_pixel  # noqa: E402


def posicao_menu(m):
    x, y = re.findall(r"menu aberto com \d+ itens em \((\d+),(\d+)\)", m.serial())[-1]
    return int(x), int(y)


def main():
    with Maquina("h35", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        antes = m.captura("H35-0-inicio")
        cor_antes = cor_do_pixel(antes, 600, 700)

        m.clique(600, 620, botao=2)
        m.esperar_serial("[GUI] menu aberto com 8 itens", 10)
        m.captura("H35-1-menu-area")
        x, y = posicao_menu(m)
        m.clique(x + 20, y + 4 + 4 * 24 + 9 + 12)
        m.esperar_serial("[GUI] menu: Papel de parede", 10)
        m.esperar_serial("[GUI] menu aberto com 3 itens", 10)
        time.sleep(0.3)
        m.captura("H35-2-menu-fundos")
        x, y = posicao_menu(m)
        m.clique(x + 20, y + 4 + 2 * 24 + 12)
        m.esperar_serial("[GUI] papel de parede: Noite", 10)
        time.sleep(0.5)
        depois = m.captura("H35-3-noite")
        verificar(cor_do_pixel(depois, 600, 700) != cor_antes, "o fundo mudou na tela")

        m.clique(184 + 764 - 2 - 60 + 8, 82 + 12)                # minimizar
        m.esperar_serial("[GUI] janela 1 minimizada", 10)
        m.clique(130, 10)                                         # botão na barra
        m.esperar_serial("[GUI] janela 1 restaurada", 10)
        verificar(True, "Terminal minimiza e volta pela barra")

        m.clique(184 + 764 - 2 - 40 + 8, 82 + 12)                # maximizar
        m.esperar_serial("[GUI] janela 1 maximizada: 1020x718", 10)
        time.sleep(0.5)
        m.comando("echo cabe-na-tela")
        m.esperar_serial("cabe-na-tela", 10)
        m.captura("H35-4-maximizado")
        verificar(True, "Terminal maximizado continua respondendo")
        m.clique(1024 - 2 - 40 + 8, 24 + 12)
        m.esperar_serial("voltou ao tamanho: 760x456", 10)
        verificar(True, "volta ao tamanho original")

        m.clique(300, 92, botao=2)                                # título
        m.esperar_serial("[GUI] menu aberto com 3 itens em", 10)
        m.tecla("esc")
        time.sleep(0.3)
    return 0


if __name__ == "__main__":
    sys.exit(main())
