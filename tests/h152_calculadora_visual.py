#!/usr/bin/env python3
"""H152 - Calculadora: visual novo e copiar/colar.

Critérios:
  - o "=" é verde, os algarismos brancos e as operações cinza;
  - o visor mostra a conta em andamento ("12 ×") em cima;
  - botão direito no visor: menu com Copiar, Colar, Recortar e Limpar;
    Copiar leva o número do visor;
  - Ctrl+V cola o número e a conta segue com ele; Ctrl+X recorta (copia e
    zera o visor).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, cor_do_pixel  # noqa: E402


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def main():
    with Maquina("h152", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("calculadora")
        m.esperar_serial("calculadora: pronta", 20)
        time.sleep(0.5)
        c = janela(m.serial(), "Calculadora")
        x, y = c["x"], c["y"]
        m.digitar("12*", 0.08)
        time.sleep(0.6)
        png = m.captura("H152-1-calculadora")
        tecla = lambda col, lin: (x + 10 + col * 70 + 6, y + 92 + lin * 58 + 25)
        verificar(cor_do_pixel(png, *tecla(3, 3)) == (0x0B, 0x7A, 0x3E), "o = é verde")
        verificar(cor_do_pixel(png, *tecla(0, 1)) == (0xFF, 0xFF, 0xFF), "os algarismos são brancos")
        verificar(cor_do_pixel(png, *tecla(3, 1)) == (0xE9, 0xEC, 0xEF), "as operações são cinza")
        conta = sum(cor_do_pixel(png, x + 200 + k, y + 22) != (0xF3, 0xF5, 0xF7) for k in range(80))
        verificar(conta > 5, f"a conta em andamento aparece em cima ({conta} pixels)")
        antes = len(m.serial())
        m.digitar("4\n", 0.08)
        esperar(m, "calculadora: visor 48", 10, antes)
        # Botão direito no visor > Copiar.
        antes = len(m.serial())
        m.clique(x + 100, y + 40, botao=2)
        s = esperar(m, "menu aberto", 10, antes)
        mx, my = [int(v) for v in re.findall(r"menu aberto \(tipo \d+, 4 itens\) em \((\d+),(\d+)\)", s)[-1]]
        time.sleep(0.5)
        m.captura("H152-2-menu")
        m.clique(mx + 30, my + 4 + 12)
        esperar(m, "calculadora: copiado 48", 10, antes)
        verificar(True, "botão direito no visor > Copiar leva 48")
        # Ctrl+V cola e a conta segue.
        antes = len(m.serial())
        m.digitar("c", 0.08)
        m.tecla("ctrl-v")
        esperar(m, "calculadora: colado 48", 10, antes)
        m.digitar("+2\n", 0.08)
        esperar(m, "calculadora: visor 50", 10, antes)
        verificar(True, "Ctrl+V cola 48 e 48 + 2 = 50")
        # Ctrl+X recorta: copia 50 e zera.
        antes = len(m.serial())
        m.tecla("ctrl-x")
        esperar(m, "calculadora: copiado 50", 10, antes)
        esperar(m, "calculadora: visor 0", 10, antes)
        verificar(True, "Ctrl+X copia 50 e zera o visor")
        # Seleção: 123456, arrastar sobre "23".
        antes = len(m.serial())
        m.digitar("c123456", 0.08)
        time.sleep(0.5)
        x0 = x + 296 - 18 - 6 * 16
        m.arrastar(x0 + 16 + 2, y + 56, x0 + 48 + 2, y + 56)
        esperar(m, "calculadora: selecao 1 a 3", 10, antes)
        time.sleep(0.5)
        m.captura("H152-3-selecao")
        m.tecla("ctrl-c")
        esperar(m, "calculadora: copiado 23", 10, antes)
        verificar(True, "arrastar marca 23 e Ctrl+C copia só ele")
        m.tecla("backspace")
        esperar(m, "calculadora: visor 1456", 10, antes)
        verificar(True, "Backspace apaga o pedaço marcado: 1456")
        antes = len(m.serial())
        x0 = x + 296 - 18 - 4 * 16
        m.clique(x0 + 20, y + 56, duplo=True)
        time.sleep(0.4)
        m.digitar("9", 0.08)
        esperar(m, "calculadora: visor 9", 10, antes)
        verificar(True, "dois cliques marcam tudo e o 9 entra no lugar")
        # Menu: Colar e Recortar.
        def menu_item(i):
            antes = len(m.serial())
            m.clique(x + 100, y + 40, botao=2)
            s = esperar(m, "menu aberto", 10, antes)
            mx, my = [int(v) for v in re.findall(r"menu aberto \(tipo \d+, 4 itens\) em \((\d+),(\d+)\)", s)[-1]]
            time.sleep(0.4)
            m.clique(mx + 30, my + 4 + i * 26 + 12)
        antes = len(m.serial())
        m.tecla("ctrl-c")
        esperar(m, "calculadora: copiado 9", 10, antes)
        m.digitar("c", 0.08)
        menu_item(1)
        esperar(m, "calculadora: colado 9", 10, antes)
        verificar(True, "menu > Colar cola 9")
        antes = len(m.serial())
        menu_item(2)
        esperar(m, "calculadora: copiado 9", 10, antes)
        esperar(m, "calculadora: visor 0", 10, antes)
        verificar(True, "menu > Recortar copia 9 e zera")
        # Colar um texto: "abc" copiado no Editor.
        t_ = janela(m.serial(), "Terminal")
        m.clique(t_["x"] + 600, t_["y"] + 300)
        time.sleep(0.4)
        antes = len(m.serial())
        m.digitar("editor\n", 0.05)
        esperar(m, "editor: pronto", 30, antes)
        time.sleep(0.8)
        m.digitar("abc", 0.05)
        m.tecla("ctrl-a")
        m.tecla("ctrl-c")
        time.sleep(0.5)
        ed = janela(m.serial(), "Editor - sem nome")
        m.clique(ed["x"] + ed["l"] - 20 + 8 - 2, ed["y"] - 12)   # X da janela
        time.sleep(1)
        d = janela(m.serial(), "Editor")
        m.clique(d["x"] + d["l"] - 16 - 190 + 45, d["y"] + d["a"] - 40 + 13)   # Não salvar
        esperar(m, "editor: fechado", 10, antes)
        time.sleep(0.5)
        m.clique(t_["x"] + t_["l"] - 37, t_["y"] - 12)          # minimiza o Terminal: a Calculadora aparece
        time.sleep(1.5)
        antes = len(m.serial())
        m.clique(x + 100, y + 40)
        m.digitar("7", 0.08)
        m.tecla("ctrl-v")
        esperar(m, "calculadora: colar recusado", 10, antes)
        time.sleep(0.5)
        m.captura("H152-4-colar-texto")
        m.digitar("c7+1\n", 0.08)
        esperar(m, "calculadora: visor 8", 10, antes)
        verificar(True, "colar \"abc\" avisa e a calculadora segue funcionando (7 + 1 = 8)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
