#!/usr/bin/env python3
"""
H38: ícones arrastáveis, grade, tamanhos e menu Iniciar com busca.

Critério: um ícone arrastado muda de lugar e abre no lugar novo; a grade e o
tamanho mudam pelo menu; a busca do Iniciar acha programa e arquivo.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from qemu import Maquina, verificar  # noqa: E402


def menu(m):
    x, y = re.findall(r"menu aberto com \d+ itens em \((\d+),(\d+)\)", m.serial())[-1]
    return int(x), int(y)


def main():
    with Maquina("h38", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.arrastar(68, 72, 500, 660)                              # Arquivos
        m.esperar_serial("[GUI] icone 0 movido para", 10)
        time.sleep(0.5)
        m.captura("H38-1-icone-arrastado")
        x, y = re.findall(r"icone 0 movido para \((\d+),(\d+)\)", m.serial())[-1]
        m.clique(int(x) + 48, int(y) + 32, duplo=True)
        m.esperar_serial("arquivos: pronto", 20)
        verificar(True, "o ícone abre no lugar novo")
        m.tecla("esc")

        m.clique(600, 700, botao=2)
        m.esperar_serial("[GUI] menu aberto com 7 itens", 10)
        x, y = menu(m)
        m.clique(x + 20, y + 4 + 4 * 24 + 9 + 12)                 # Ícones (item 4)
        m.esperar_serial("[GUI] menu aberto com 5 itens", 10)
        time.sleep(0.4)
        x, y = menu(m)
        m.captura("H38-2-menu-icones")
        m.clique(x + 20, y + 4 + 2 * 24 + 12)                     # Grandes
        m.esperar_serial("[GUI] icones: tamanho 96", 10)
        time.sleep(0.5)
        m.captura("H38-3-icones-grandes")
        verificar(True, "tamanho muda pelo menu")

        m.clique(20, 10)                                          # sagui da barra
        m.esperar_serial('[GUI] iniciar: busca ""', 10)
        time.sleep(0.3)
        m.captura("H38-4-iniciar")
        m.digitar("calc", 0.08)
        m.esperar_serial('[GUI] iniciar: busca "calc", 1 resultados', 10)
        m.digitar("\n", 0.05)
        m.esperar_serial("[GUI] iniciar: abrir /bin/calculadora", 10)
        verificar(True, "busca acha e abre programa")
        time.sleep(1)

        m.clique(20, 10)
        m.esperar_serial('[GUI] iniciar: busca ""', 10)
        m.digitar("leia", 0.08)
        m.esperar_serial('[GUI] iniciar: busca "leia", 1 resultados', 10)
        time.sleep(0.3)
        m.captura("H38-5-busca-arquivo")
        m.digitar("\n", 0.05)
        m.esperar_serial("[GUI] iniciar: abrir /leia-me.txt", 10)
        m.esperar_serial("editor: aberto /leia-me.txt", 20)
        verificar(True, "busca acha arquivo e abre no Editor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
