#!/usr/bin/env python3
"""H139 - menus do botão direito no painel do Arquivos e Propriedades.

Critérios:
  - no SaguiOS (o computador): Abrir, Abrir em nova janela, Recolher,
    Nova pasta aqui, Sobre o SaguiOS e Propriedades; Propriedades mostra a
    versão do sistema, a arquitetura, a memória e os arquivos; "Sobre o
    SaguiOS" abre o Sobre;
  - na Lixeira: Abrir, Esvaziar lixeira e Propriedades; Esvaziar pergunta e
    apaga;
  - numa pasta da árvore: Nova pasta aqui cria a pasta dentro dela (e abre
    o nome para editar); Renomear pede o nome e renomeia;
  - na lista, Propriedades de um arquivo mostra tipo, local, tamanho e a
    data de modificação.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arquivos_ui as ui  # noqa: E402
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402


def esperar(m, texto, tempo=15, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def menu(m, ponto, desde):
    """Botão direito; devolve (x, y, n_itens) do menu aberto."""
    m.clique(*ponto, botao=2)
    s = esperar(m, "[GUI] menu aberto", 10, desde)
    r = re.findall(r"menu aberto \(tipo \d+, (\d+) itens\) em \((\d+),(\d+)\)", s)[-1]
    return int(r[1]), int(r[2]), int(r[0])


def item(x, y, k):
    return x + 40, y + 4 + k * 26 + 13


def item_chamado(m, x, y, nome, desde):
    """O item do menu do painel pelo nome (o Arquivos registra os itens)."""
    linha = re.findall(r"arquivos: menu do painel em [^:]*: ([^\r\n]*)", m.serial()[desde:])[-1]
    return item(x, y, linha.split("|").index(nome))


def main():
    with Maquina("h139", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("mkdir /tmp/k")
        m.comando("echo uma nota de teste > /tmp/k/nota.txt")
        m.comando("mkdir /.lixeira")
        m.comando("echo velho > /.lixeira/velho.txt")
        n = len(m.serial())
        m.digitar("arquivos /tmp/k\n", 0.03)
        esperar(m, "arquivos: ordem: nota.txt", 30, n)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")

        # SaguiOS: Propriedades (o último item).
        n = len(m.serial())
        x, y, q = menu(m, ui.no_painel(m.serial(), a, "SaguiOS"), n)
        s = esperar(m, "arquivos: menu do painel em /", 10, n)
        verificar(q == 6, f"o menu do SaguiOS tem 6 itens ({q})")
        time.sleep(0.4)
        m.captura("H139-1-menu-saguios")
        m.clique(*item(x, y, q - 1))
        s = esperar(m, '"Propriedades do SaguiOS"', 10, n)
        verificar("arquivos: propriedades do SaguiOS (versao 3." in s, "Propriedades do SaguiOS mostra a versão do sistema")
        time.sleep(0.6)
        m.captura("H139-2-propriedades-saguios")
        m.tecla("ret")
        time.sleep(0.6)
        n = len(m.serial())
        x, y, q = menu(m, ui.no_painel(m.serial(), a, "SaguiOS"), n)
        m.clique(*item(x, y, q - 2))                     # Sobre o SaguiOS
        esperar(m, "sobre: versao", 15, n)
        verificar(True, "Sobre o SaguiOS abre o Sobre")
        m.tecla("alt-f4")
        time.sleep(1)

        # Lixeira: Esvaziar.
        m.clique(a["x"] + a["l"] - 30, a["y"] + 400)
        time.sleep(0.5)
        n = len(m.serial())
        x, y, q = menu(m, ui.lixeira(m.serial(), a), n)
        verificar(q == 3, f"o menu da Lixeira tem 3 itens ({q})")
        m.clique(*item(x, y, 1))
        esperar(m, '"Esvaziar lixeira"', 10, n)
        time.sleep(0.5)
        m.digitar("\n", 0.05)
        esperar(m, "arquivos: lixeira esvaziada /.lixeira", 10, n)
        verificar(True, "Esvaziar lixeira no painel pergunta e apaga")

        # Pasta "k" da árvore: Nova pasta aqui; depois Renomear.
        n = len(m.serial())
        x, y, q = menu(m, ui.no_painel(m.serial(), a, "k"), n)
        nomes = ["Abrir", "Abrir em nova janela", "Nova pasta aqui", "Renomear...", "Apagar...", "Propriedades"]
        verificar(q == len(nomes), f"o menu de uma pasta da árvore tem {len(nomes)} itens ({q})")
        m.clique(*item(x, y, 2))
        esperar(m, "arquivos: criado /tmp/k/Nova pasta", 10, n)
        esperar(m, "arquivos: renomeando Nova pasta", 10, n)
        m.digitar("Fotos\n", 0.05)
        esperar(m, "renomeado /tmp/k/Nova pasta -> /tmp/k/Fotos", 10, n)
        verificar(True, "Nova pasta aqui cria a pasta dentro e abre o nome para editar")
        time.sleep(0.6)
        n = len(m.serial())
        x, y, q = menu(m, ui.no_painel(m.serial(), a, "k"), n)
        m.clique(*item_chamado(m, x, y, "Renomear...", n))
        esperar(m, '"Renomear"', 10, n)
        time.sleep(0.5)
        for _ in range(2):
            m.tecla("backspace", 0.05)
        m.digitar("cadernos\n", 0.05)
        esperar(m, "arquivos: renomeado /tmp/k -> /tmp/cadernos", 10, n)
        esperar(m, "arquivos: pasta /tmp/cadernos (", 10, n)
        verificar(True, "Renomear na árvore renomeia, e a pasta aberta segue o nome novo")

        # Propriedades de um arquivo, pelo menu da lista.
        time.sleep(0.6)
        n = len(m.serial())
        x, y, q = menu(m, ui.item(a, 1), n)               # nota.txt (depois da pasta Fotos)
        m.clique(*item(x, y, q - 1))
        s = esperar(m, '"Propriedades de nota.txt"', 10, n)
        verificar("arquivos: propriedades de /tmp/cadernos/nota.txt" in s, "Propriedades de um arquivo, pelo menu da lista")
        time.sleep(0.6)
        m.captura("H139-3-propriedades-arquivo")
        m.tecla("ret")


if __name__ == "__main__":
    main()
