#!/usr/bin/env python3
"""H111 - a coluna da direita da área de trabalho: o instalador e as unidades.

Como no Linux, "Instalar o SaguiOS" e as unidades (pendrives e o disco do
SaguiOS) ficam no canto de cima à direita. Critérios:
  - ligado pela imagem (não instalado), com um pendrive: o instalador e o
    pendrive aparecem na coluna da direita;
  - o duplo clique no instalador abre o instalador;
  - o botão direito no instalador só tem "Abrir"; no pendrive, "Abrir" e
    "Remover com segurança";
  - o duplo clique no pendrive monta e abre o Arquivos em /usb;
  - captura da área de trabalho para a revisão a olho.
(O instalador some quando o sistema ligou pelo disco instalado: h110.)
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, criar_pendrive, pendrive_qemu  # noqa: E402

X = 1024 - 20 - 96 + 48        # o meio da coluna da direita
Y0 = 40 + 32                   # o meio do primeiro ícone


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def main():
    pen = criar_pendrive("h111-pen", 64, "FOTOS", arquivos={"oi.txt": b"oi\n"})
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h111", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        esperar(m, "coluna da direita: instalador sim, 1 unidades", 20)
        verificar(True, "o instalador e o pendrive aparecem na coluna da direita")
        time.sleep(1)
        m.tecla("meta_l-d")                      # Windows+D: mostra a área de trabalho
        time.sleep(6)                            # o aviso do pendrive some em 5 s
        m.captura("H111-area-de-trabalho")

        n = len(m.serial())
        m.clique(X, Y0, botao=2)
        esperar(m, "menu aberto (tipo", 10, n)
        verificar(re.search(r"menu aberto \(tipo \d+, 1 itens\)", m.serial()[n:]) is not None,
                  "no instalador, o botão direito só tem Abrir")
        m.tecla("esc")
        time.sleep(0.5)
        m.clique(560, 420)                       # um clique no vazio fecha o que sobrar
        time.sleep(0.5)
        n = len(m.serial())
        m.clique(X, Y0 + 108, botao=2)
        esperar(m, "menu aberto (tipo", 10, n)
        verificar(re.search(r"menu aberto \(tipo \d+, 2 itens\)", m.serial()[n:]) is not None,
                  "no pendrive, Abrir e Remover com segurança")
        time.sleep(0.5)
        m.captura("H111-menu-do-pendrive")
        m.tecla("esc")
        time.sleep(0.5)
        m.clique(560, 420)
        time.sleep(0.5)

        n = len(m.serial())
        m.clique(X, Y0 + 108, duplo=True)
        esperar(m, "arquivos: pasta /usb", 15, n)
        verificar("icone FOTOS (usb0) -> /bin/arquivos /usb" in m.serial()[n:], "o duplo clique no pendrive abre o Arquivos em /usb")
        time.sleep(1)
        m.tecla("alt-f4")
        time.sleep(1)
        m.tecla("meta_l-d")
        time.sleep(1)
        n = len(m.serial())
        m.clique(X, Y0, duplo=True)
        esperar(m, "instalar: janela aberta", 20, n)
        verificar(True, "o duplo clique no instalador abre o instalador")


if __name__ == "__main__":
    main()
