#!/usr/bin/env python3
"""H151 - janela de abrir e salvar arquivo (libsagui), pelo Editor.

Critérios:
  - Ctrl+S num texto sem nome abre "Salvar como" em /tmp, com os locais
    à esquerda e a pasta no meio;
  - um clique em "SaguiOS" vai para /; escrever "/tmp" no nome e Enter
    entra na pasta;
  - escolher o tipo "Texto (.txt)" põe a extensão no nome sem extensão:
    "dialogo" vira /tmp/dialogo.txt;
  - salvar por cima de um arquivo que existe pede confirmação;
  - Ctrl+O abre "Abrir"; dois cliques no arquivo abrem numa aba nova.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402

L, A, PE_A = 660, 440, 88
PE_Y = A - PE_A


def esperar(m, texto, tempo=30, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def main():
    with Maquina("h151", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        antes = len(m.serial())
        m.digitar("editor\n", 0.04)
        esperar(m, "editor: pronto", 30, antes)
        time.sleep(1)
        e = janela(m.serial(), "Editor - sem nome")
        m.clique(e["x"] + 300, e["y"] + 200)
        m.digitar("texto do dialogo", 0.03)
        antes = len(m.serial())
        m.tecla("ctrl-s")
        esperar(m, "arquivo-dialogo: aberto (salvar) em /tmp", 10, antes)
        time.sleep(0.8)
        d = janela(m.serial(), "Salvar como")
        m.captura("H151-1-salvar-como")
        verificar(True, "Ctrl+S sem nome abre Salvar como em /tmp")
        antes = len(m.serial())
        m.clique(d["x"] + 40, d["y"] + 48 + 34 + 13)          # local "SaguiOS"
        esperar(m, "arquivo-dialogo: pasta / (", 10, antes)
        verificar(True, "o local SaguiOS vai para /")
        antes = len(m.serial())
        m.digitar("/tmp\n", 0.05)
        esperar(m, "arquivo-dialogo: pasta /tmp (", 10, antes)
        verificar(True, "escrever o caminho de uma pasta e Enter entra nela")
        # Tipo: Texto (.txt).
        tx = 70 + 330 + 12
        antes = len(m.serial())
        m.clique(d["x"] + tx + 30, d["y"] + PE_Y + 23)
        esperar(m, "menu aberto", 10, antes)
        time.sleep(0.5)
        m.captura("H151-2-tipos")
        mx, my = [int(v) for v in __import__("re").findall(r"menu aberto \(tipo \d+, 4 itens\) em \((\d+),(\d+)\)", m.serial()[antes:])[-1]]
        m.clique(mx + 30, my + 4 + 26 + 12)                    # o segundo item
        esperar(m, "arquivo-dialogo: tipo Texto (.txt)", 10, antes)
        m.digitar("dialogo\n", 0.05)
        esperar(m, "editor: salvo /tmp/dialogo.txt", 10, antes)
        verificar(True, "o tipo Texto põe .txt no nome: salvo /tmp/dialogo.txt")
        # Salvar como por cima: pede confirmação.
        m.tecla("ctrl-n")
        time.sleep(0.5)
        m.digitar("outro texto", 0.03)
        antes = len(m.serial())
        m.tecla("ctrl-s")
        esperar(m, "arquivo-dialogo: aberto (salvar)", 10, antes)
        time.sleep(0.5)
        m.digitar("dialogo.txt\n", 0.05)
        time.sleep(1)
        m.captura("H151-3-substituir")
        m.tecla("ret")                                          # Sim
        esperar(m, "editor: salvo /tmp/dialogo.txt", 10, antes)
        verificar(True, "salvar por cima pergunta e substitui com Sim")
        # Abrir: dois cliques no arquivo.
        antes = len(m.serial())
        m.tecla("ctrl-o")
        esperar(m, "arquivo-dialogo: aberto (abrir) em /tmp", 10, antes)
        time.sleep(0.8)
        d = janela(m.serial(), "Abrir")
        m.captura("H151-4-abrir")
        s = m.serial()[antes:]
        verificar("arquivo-dialogo: pasta /tmp (" in s, "Abrir começa na pasta do arquivo atual")
    return 0


if __name__ == "__main__":
    sys.exit(main())
