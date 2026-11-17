#!/usr/bin/env python3
"""H107 - menu Iniciar numa tela de 640x480.

O menu tinha altura fixa de 544 px e passava do fim da tela em 640x480
(rodapé com Sobre e Desligar fora da tela). Agora a grade tem tantas linhas
quantas cabem (até 4), e os programas que sobram vão para a página seguinte,
com "<" e ">" no rodapé.

Critérios:
  - a placa de vídeo com 1 MB só aceita 640x480: o sistema liga nela;
  - o menu abre e mostra "1 de N" com N > 1 (19 programas não cabem em 2 linhas);
  - ">" leva para a página 2, e um ícone dela abre o programa;
  - "Desligar", no rodapé, está dentro da tela (o clique chega nele);
  - capturas para a revisão a olho.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, FalhaTeste  # noqa: E402

MENU_X, MENU_Y = 4, 24 + 4
GRADE_X, GRADE_Y = MENU_X + (620 - 5 * 108) // 2, MENU_Y + 106


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def main():
    extras = ["-vga", "none", "-device", "VGA,vgamem_mb=1"]
    with Maquina("h107", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        verificar("[VIDEO] 640x480" in m.serial(), "o sistema liga em 640x480")
        linhas = min(4, max(1, (480 - MENU_Y - 8 - 106 - 70) // 92))
        alt = 106 + linhas * 92 + 70
        rodape = MENU_Y + alt - 50
        verificar(MENU_Y + alt <= 480, f"o menu cabe na tela ({linhas} linhas, {alt} px)")

        n = len(m.serial())
        m.clique(40, 12)
        esperar(m, "[GUI] menu aberto", 10, n)
        time.sleep(0.6)
        m.captura("H107-menu-pagina1")
        n = len(m.serial())
        m.clique(MENU_X + 150 + 92 + 16, rodape + 20)          # ">"
        esperar(m, "[GUI] menu: pagina 2 de", 10, n)
        paginas = int(re.findall(r"menu: pagina 2 de (\d+)", m.serial()[n:])[-1])
        verificar(paginas >= 2, f"os programas que não cabem vão para a página 2 (de {paginas})")
        time.sleep(0.6)
        m.captura("H107-menu-pagina2")
        n = len(m.serial())
        m.clique(GRADE_X + 54, GRADE_Y + 40)                   # o 1º ícone da página 2
        esperar(m, "[GUI] menu: ", 10, n)
        nome = re.findall(r"\[GUI\] menu: ([^\n]+)", m.serial()[n:])[-1]
        verificar("pagina" not in nome, f"um ícone da página 2 abre o programa ({nome.strip()})")
        time.sleep(2)

        n = len(m.serial())
        m.clique(40, 12)
        esperar(m, "[GUI] menu aberto", 10, n)
        time.sleep(0.6)
        m.mouse_para(MENU_X + 620 - 16 - 120 + 8 + 56, rodape + 20)   # sobre Desligar
        time.sleep(0.6)
        m.captura("H107-menu-rodape")
        n = len(m.serial())
        m.clique(MENU_X + 620 - 16 - 120 + 8 + 56, rodape + 20)
        esperar(m, "/bin/energia", 10, n)
        verificar(True, "Desligar, no rodapé, fica dentro da tela e abre a janela de energia")


if __name__ == "__main__":
    main()
