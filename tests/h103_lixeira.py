#!/usr/bin/env python3
"""H103 - lixeira do Arquivos (issue #25, parte 5).

Critérios:
  - "Apagar" move o arquivo para /.lixeira em vez de apagar de vez;
  - o ícone da Lixeira na área de trabalho fica cheio, abre a lixeira com
    duplo clique e volta a ficar vazio quando ela esvazia;
  - o botão Lixeira, no pé da lateral, mostra a lixeira com o arquivo;
  - "Restaurar" devolve o arquivo para a pasta de onde ele saiu;
  - o .origens (os caminhos de volta) não aparece na lista da lixeira;
  - Shift+Delete apaga de vez, sem passar pela lixeira;
  - capturas para a revisão a olho.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arquivos_ui as ui  # noqa: E402
from qemu import Maquina, verificar  # noqa: E402


def janela(serial, titulo):
    m = re.findall(rf'criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial)[-1]
    return dict(l=int(m[0]), a=int(m[1]), x=int(m[2]) + 2, y=int(m[3]) + 24)


def main():
    with Maquina("h103", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("mkdir /tmp/lx")
        m.comando("echo lixo > /tmp/lx/a.txt")
        m.comando("echo outro > /tmp/lx/b.txt")
        m.digitar("arquivos /tmp/lx\n", 0.04)
        m.esperar_serial("arquivos: pasta /tmp/lx (2 itens)", 30)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")

        # a.txt é o 1º item: já vem marcado. Delete > Sim.
        # o 1º item já vem marcado (clicar nele de novo seria o renomear devagar)
        m.tecla("delete")
        m.esperar_serial('"Apagar"', 10)
        time.sleep(0.8)
        m.captura("H103-1-apagar")
        d = janela(m.serial(), "Apagar")
        m.clique(d["x"] + d["l"] - 16 - 45, d["y"] + d["a"] - 40 + 13)
        m.esperar_serial("arquivos: apagado /tmp/lx/a.txt (para a lixeira: /.lixeira/a.txt)", 10)
        verificar(True, "Apagar move para a lixeira")
        m.esperar_serial("arquivos: pasta /tmp/lx (1 itens)", 10)

        # O ícone da Lixeira na área de trabalho: fica cheio (o kernel confere
        # a cada 2 s) e o duplo clique abre o Arquivos na lixeira. Sem pendrive
        # nem programas instalados, ele é o 9º ícone: 2ª coluna, 2ª linha.
        m.esperar_serial("[GUI] lixeira cheia", 10)
        verificar(True, "o ícone da Lixeira fica cheio")
        m.tecla("alt-f4")                 # o Arquivos (900x560 desde a H136) cobre o ícone
        time.sleep(1)
        m.captura("H103-0-icone-cheio")
        n = len(m.serial())
        m.clique(20 + 110 + 30, 40 + 92 + 30, duplo=True)
        m.esperar_serial("[GUI] icone Lixeira -> /bin/arquivos", 10)
        s = m.serial()[n:]
        fim = time.time() + 15
        while "arquivos: pasta /.lixeira (1 itens)" not in s and time.time() < fim:
            time.sleep(0.3)
            s = m.serial()[n:]
        verificar("arquivos: pasta /.lixeira (1 itens)" in s, "o duplo clique no ícone abre a lixeira no Arquivos")
        time.sleep(1)
        m.captura("H103-1-icone-abre")
        m.tecla("alt-f4")
        time.sleep(1)
        n = len(m.serial())
        m.digitar("arquivos /tmp/lx\n", 0.04)
        m.esperar_serial("arquivos: pronto", 30)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")

        m.clique(*ui.lixeira(m.serial(), a))                       # Lixeira, no painel (H136)
        m.esperar_serial("arquivos: pasta /.lixeira (1 itens)", 10)
        verificar(True, "o botão Lixeira mostra o arquivo apagado (e esconde o .origens)")
        time.sleep(0.8)
        m.captura("H103-2-lixeira")
        m.clique(*ui.comando(a, "Restaurar", lixeira=True))   # Restaurar
        m.esperar_serial("arquivos: restaurado /.lixeira/a.txt -> /tmp/lx/a.txt", 10)
        verificar(True, "Restaurar devolve o arquivo para /tmp/lx")
        m.esperar_serial("arquivos: pasta /.lixeira (0 itens)", 10)
        m.esperar_serial("[GUI] lixeira vazia", 10)
        verificar(True, "depois de restaurar, o ícone volta a ficar vazio")

    with Maquina("h103b", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        # Captura da área de trabalho limpa, com a Lixeira cheia: minimiza o
        # Terminal e depois o traz de volta pela doca.
        m.comando("mkdir /.lixeira")
        m.comando("echo velho > /.lixeira/velho.txt")
        m.esperar_serial("[GUI] lixeira cheia", 10)
        t = janela(m.serial(), "Terminal")
        m.clique(t["x"] + t["l"] - 37, t["y"] - 12)       # botão de minimizar (o do meio, à esquerda do X)
        time.sleep(1.2)
        m.captura("H103-0-area-de-trabalho")
        m.clique(460, 730)                                 # o Terminal na doca
        time.sleep(1)
        m.comando("mkdir /tmp/lx")
        m.comando("echo some > /tmp/lx/c.txt")
        m.digitar("arquivos /tmp/lx\n", 0.04)
        m.esperar_serial("arquivos: pasta /tmp/lx (1 itens)", 30)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        # o 1º item já vem marcado (clicar nele de novo seria o renomear devagar)
        m.tecla("shift-delete")
        m.esperar_serial('"Apagar"', 10)
        time.sleep(0.8)
        d = janela(m.serial(), "Apagar")
        m.clique(d["x"] + d["l"] - 16 - 45, d["y"] + d["a"] - 40 + 13)
        m.esperar_serial("arquivos: apagado de vez /tmp/lx/c.txt", 10)
        verificar("para a lixeira" not in m.serial(), "Shift+Delete apaga de vez, sem passar pela lixeira")


if __name__ == "__main__":
    main()
