#!/usr/bin/env python3
"""H128 - a seleção de um ícone cobre o nome inteiro, inclusive a segunda linha.

"Instalar o SaguiOS", na coluna da direita, tem o nome em duas linhas. Antes,
o quadro da seleção parava na primeira linha e "SaguiOS" ficava para fora.

Critérios:
  - marcado com um clique, o pixel ao lado da segunda linha do nome muda de
    cor (o quadro da seleção chega lá);
  - um clique fora desmarca, e o pixel volta à cor de antes.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, cor_do_pixel  # noqa: E402

ICONE = (1024 - 20 - 96 + 48, 40 + 32)   # o meio do primeiro ícone da coluna da direita
LINHA2 = (1024 - 20 - 12, 40 + 94)       # à direita de "SaguiOS", a segunda linha do nome (o Terminal cobre a esquerda)
VAZIO = (60, 725)


def main():
    with Maquina("h128", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        antes = cor_do_pixel(m.captura("h128-antes", guardar=False), *LINHA2)
        m.clique(*ICONE)
        time.sleep(0.6)
        marcado = cor_do_pixel(m.captura("H128-selecao-nome-inteiro"), *LINHA2)
        verificar(marcado != antes, f"marcado, a seleção cobre a segunda linha do nome ({antes} -> {marcado})")
        m.clique(*VAZIO)
        time.sleep(0.6)
        depois = cor_do_pixel(m.captura("h128-depois", guardar=False), *LINHA2)
        verificar(depois == antes, f"desmarcado, a cor volta ({depois})")


if __name__ == "__main__":
    main()
