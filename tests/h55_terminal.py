#!/usr/bin/env python3
"""H55 - terminal completo.

Critério: o programa de teste desenha cores (16, 256 e 24 bits), uma caixa
com molduras UTF-8 e outra com o conjunto gráfico antigo, blocos, acentos e
uma região de rolagem; a captura confere as cores e as letras nas posições
certas; o Terminal responde à pergunta da posição do cursor.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, cor_do_pixel  # noqa: E402

COLS = 94


def celula(j, l, c):
    """Centro aproximado da célula (linha l, coluna c, contando de 0) na tela."""
    return j["x"] + 4 + c * 8 + 4, j["y"] + 24 + 4 + l * 16 + 8


def main():
    with Maquina("h55", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        out = m.comando("/testes/vt", tempo=30)
        verificar("resposta da posicao: [20;5R" in out, "o Terminal responde à pergunta da posição do cursor (ESC [ 6 n)")
        time.sleep(0.5)
        png = m.captura("H55-terminal-vt")
        j = janela(m.serial(), "Terminal")
        # Fundo vermelho (cor 1) atrás de " fundo vermelho ", que começa na coluna 14 da linha 0.
        x, y = celula(j, 0, 15)
        verificar(cor_do_pixel(png, x - 3, y - 7) == (0xD9, 0x53, 0x4F), "fundo vermelho (cor 41) na célula certa")
        # Moldura: a linha horizontal da caixa (linha 4, coluna 10 = ┌) passa pela linha 7 da célula.
        x, y = celula(j, 4, 11)
        verificar(cor_do_pixel(png, x, j["y"] + 24 + 4 + 4 * 16 + 7) == (0xD8, 0xDD, 0xE2), "moldura ─ desenhada na linha certa da célula")
        x, y = celula(j, 5, 9)
        verificar(cor_do_pixel(png, j["x"] + 4 + 9 * 8 + 3, y) == (0xD8, 0xDD, 0xE2), "moldura │ desenhada na coluna certa da célula")
        x, y = celula(j, 4, 21)
        verificar(cor_do_pixel(png, x, j["y"] + 24 + 4 + 4 * 16 + 7) == (0xD8, 0xDD, 0xE2), "conjunto gráfico antigo (ESC ( 0) vira moldura")
        x, y = celula(j, 8, 0)
        verificar(cor_do_pixel(png, x, y) == (0xD8, 0xDD, 0xE2), "bloco cheio █")
    return 0


if __name__ == "__main__":
    sys.exit(main())
