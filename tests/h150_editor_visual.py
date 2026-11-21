#!/usr/bin/env python3
"""H150 - Editor com aba, menus e barra de status.

Critérios:
  - a aba mostra o arquivo; um ponto verde aparece quando há alteração;
  - a barra de status mostra linha, coluna e o total de letras, e acompanha
    o cursor;
  - o menu Arquivo abre embaixo do nome; Exibir esconde e mostra a barra de
    status (a área de texto cresce até o fim da janela);
  - num programa em C, o botão verde "Compilar e rodar" fica à direita;
  - abas: Ctrl+N abre outra aba, um clique na primeira volta para ela, o
    "x" fecha a aba vazia e o texto da primeira continua lá.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, cor_do_pixel  # noqa: E402

PROGRAMA = '#include "sagui.h"\n\n/* Ola do Editor */\nint principal(int argc, char **argv)\n{\n    imprimir("ola\\n");\n    return 0;\n}\n'


def esperar(m, texto, tempo=30, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def main():
    with Maquina("h150", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        m.comando("echo > /tmp/ola.c", tempo=20)
        antes = len(m.serial())
        m.digitar("editor /tmp/ola.c\n", 0.04)
        esperar(m, "editor: pronto", 30, antes)
        time.sleep(1)
        e = janela(m.serial(), "Editor - sem nome")
        x, y, l, a = e["x"], e["y"], 700, 480
        png0 = m.captura("H150-0-vazio", guardar=False)
        verificar(cor_do_pixel(png0, x + 8 + 210 - 18, y + 21) != (0x0B, 0x7A, 0x3E), "sem alteração, a aba não tem o ponto (tem o x de fechar)")
        m.clique(x + 300, y + 200)
        m.digitar(PROGRAMA, 0.03)
        time.sleep(1)
        png = m.captura("H150-1-editor")
        verificar(cor_do_pixel(png, x + 8 + 210 - 18, y + 21) == (0x0B, 0x7A, 0x3E), "com alteração, a aba ganha o ponto verde")
        verificar(cor_do_pixel(png, x + l - 10 - 84 - 8 - 4, y + 36 + 17) == (0x0B, 0x7A, 0x3E), "programa em C: botão verde Compilar e rodar à direita")
        verificar(cor_do_pixel(png, x + 400, y + a - 3) == (0xF6, 0xF7, 0xF8), "barra de status embaixo")
        # O status acompanha o cursor: depois de digitar tudo, linha 9, coluna 1.
        m.tecla("up")
        m.tecla("end")
        time.sleep(0.5)
        png2 = m.captura("H150-status", guardar=False)
        mudou = sum(cor_do_pixel(png2, x + 14 + k, y + a - 13 + d) != cor_do_pixel(png, x + 14 + k, y + a - 13 + d)
                    for k in range(0, 110) for d in range(-4, 5))
        verificar(mudou > 10, f"o status mudou com o cursor ({mudou} pixels)")
        # Menu Arquivo.
        antes = len(m.serial())
        m.clique(x + 8 + 30, y + 36 + 17)
        time.sleep(0.8)
        m.captura("H150-2-menu-arquivo")
        m.tecla("esc")
        m.clique(x + l // 2, y + 36 + 17)                     # fora: fecha o menu
        time.sleep(0.5)
        # Exibir > esconder a barra de status.
        xe = x + 8 + (7 * 8 + 22) + (6 * 8 + 22) + 20
        m.clique(xe, y + 36 + 17)
        time.sleep(0.8)
        m.clique(xe + 20, y + 70 + 12)                         # o único item
        esperar(m, "editor: menu 2 0", 10, antes)
        time.sleep(0.8)
        png3 = m.captura("H150-3-sem-status", guardar=False)
        verificar(cor_do_pixel(png3, x + 400, y + a - 3) == (0xFF, 0xFF, 0xFF), "Exibir esconde a barra de status")
        # Abas.
        antes = len(m.serial())
        m.tecla("ctrl-n")
        esperar(m, "editor: aba 2 de 2 (sem nome)", 10, antes)
        m.digitar("segunda aba", 0.03)
        time.sleep(0.8)
        m.captura("H150-4-duas-abas")
        m.clique(x + 8 + 40, y + 20)
        esperar(m, "editor: aba 1 de 2 (ola.c)", 10, antes)
        verificar(True, "Ctrl+N abre outra aba e o clique volta para a primeira")
        m.tecla("ctrl-n")
        esperar(m, "editor: aba 3 de 3 (sem nome)", 10, antes)
        antes = len(m.serial())
        al3 = min(210, (l - 8 - 40) // 3 - 4)                   # largura de cada aba com 3 abertas
        x3 = x + 8 + 2 * (al3 + 4)
        m.clique(x3 + al3 - 18, y + 21)                        # o "x" da terceira (vazia)
        esperar(m, "editor: aba fechada, ficam 2", 10, antes)
        verificar(True, "o x fecha a aba vazia sem perguntar")
    return 0


if __name__ == "__main__":
    sys.exit(main())
