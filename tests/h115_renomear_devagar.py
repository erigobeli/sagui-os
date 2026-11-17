#!/usr/bin/env python3
"""H115 - renomear devagar, no lugar: um segundo clique, sem pressa, no nome
de um ícone ou arquivo que já estava marcado abre a edição do nome ali mesmo,
sem janela de diálogo.

Critérios:
  - área de trabalho: clique no Terminal, espera, outro clique: a caixa de
    texto aparece embaixo do ícone com o nome todo marcado; digitar substitui
    e Enter grava (icone.terminal); nenhuma janela abre;
  - Esc desiste e o nome fica;
  - um clique fora da caixa também grava;
  - o duplo clique num ícone já marcado continua abrindo o programa, sem
    abrir a edição;
  - Arquivos: o mesmo no nome de um arquivo (notas.txt -> diario.txt, só o
    nome marcado, a extensão fica); o duplo clique no item marcado abre o
    arquivo, sem editar o nome.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402

LATERAL = 140
LISTA_X, LISTA_Y = LATERAL + 6, 70
TERMINAL = (20 + 48, 24 + 16 + 1 * 92 + 30)   # o segundo ícone da coluna
VAZIO = (60, 725)                              # área de trabalho sem ícone nem janela (embaixo da coluna)


def esperar(m, texto, tempo=15, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def devagar(m, x, y, limpar=VAZIO):
    """Desmarca tudo e dá dois cliques separados (mais de meio segundo entre eles)."""
    m.clique(*limpar)
    time.sleep(0.6)
    m.clique(x, y)
    time.sleep(0.6)
    m.clique(x, y)


def main():
    with Maquina("h115", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)

        # Área de trabalho: renomear o Terminal devagar.
        n = len(m.serial())
        devagar(m, *TERMINAL)
        esperar(m, "renomear no lugar: icone 1 (Terminal)", 10, n)
        time.sleep(0.5)
        m.captura("H115-1-caixa-no-icone")
        verificar("criada por" not in m.serial()[n:], "nenhuma janela abriu para pedir o nome")
        m.digitar("Console\n", 0.05)
        esperar(m, "icone renomeado: terminal agora se chama Console", 10, n)
        verificar(True, "clique devagar no Terminal marcado, digitar e Enter: virou Console")
        time.sleep(0.8)
        m.captura("H115-2-renomeado")

        # Esc desiste.
        n = len(m.serial())
        devagar(m, *TERMINAL)
        esperar(m, "renomear no lugar: icone 1 (Console)", 10, n)
        m.digitar("xyz", 0.05)
        m.tecla("esc")
        esperar(m, "renomear no lugar: nome mantido", 10, n)
        verificar("icone renomeado" not in m.serial()[n:], "Esc desiste e o nome fica")

        # Um clique fora grava.
        n = len(m.serial())
        devagar(m, *TERMINAL)
        esperar(m, "renomear no lugar: icone 1 (Console)", 10, n)
        m.digitar("Shell", 0.05)
        m.clique(*VAZIO)
        esperar(m, "icone renomeado: terminal agora se chama Shell", 10, n)
        verificar(True, "um clique fora da caixa grava o nome")

        # Duplo clique num ícone já marcado: abre, sem editar.
        m.clique(*VAZIO)
        time.sleep(0.6)
        m.clique(*TERMINAL)
        time.sleep(0.8)
        n = len(m.serial())
        m.clique(*TERMINAL, duplo=True)
        esperar(m, "criada por", 15, n)
        time.sleep(1.2)
        verificar("renomear no lugar" not in m.serial()[n:], "o duplo clique no ícone marcado abre o programa, sem editar o nome")

        # Arquivos: o mesmo num arquivo.
        m.comando("mkdir /tmp/r", tempo=10)
        m.comando("echo oi > /tmp/r/notas.txt", tempo=10)
        n = len(m.serial())
        m.digitar("arquivos /tmp/r\n", 0.03)
        esperar(m, "arquivos: pronto", 30, n)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        linha = (a["x"] + LISTA_X + 40, a["y"] + LISTA_Y + 2 + 10)   # o nome do primeiro item
        n = len(m.serial())
        vazio = (a["x"] + LISTA_X + 300, a["y"] + LISTA_Y + 250)   # o vazio da lista
        devagar(m, *linha, limpar=vazio)
        esperar(m, "arquivos: renomeando notas.txt", 10, n)
        time.sleep(0.4)
        m.captura("H115-3-arquivos-na-linha")
        m.digitar("diario\n", 0.05)
        esperar(m, "renomeado /tmp/r/notas.txt -> /tmp/r/diario.txt", 10, n)
        verificar(True, "Arquivos: clique devagar no nome e Enter: notas.txt virou diario.txt")

        m.clique(*vazio)
        time.sleep(0.6)
        m.clique(*linha)
        time.sleep(0.8)
        n = len(m.serial())
        m.clique(*linha, duplo=True)
        esperar(m, "arquivos: abrindo /tmp/r/diario.txt", 10, n)
        time.sleep(1.2)
        verificar("renomeando" not in m.serial()[n:], "Arquivos: o duplo clique no item marcado abre, sem editar o nome")


if __name__ == "__main__":
    main()
