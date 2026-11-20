#!/usr/bin/env python3
"""H133 - "Esvaziar lixeira..." no menu do ícone da Lixeira, na área de trabalho.

Critérios:
  - com a lixeira cheia, o botão direito no ícone mostra "Esvaziar lixeira..."
    (4 itens: Abrir, Esvaziar lixeira..., Renomear, Esconder o ícone);
  - a escolha abre a pergunta "Esvaziar lixeira", sem abrir a janela do
    Arquivos; Esc desiste e nada é apagado;
  - Sim apaga tudo (arquivos e pastas) de /.lixeira, que continua existindo
    vazia, e o ícone volta a ficar vazio;
  - com a lixeira vazia, o item não aparece (3 itens).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402

LIXEIRA = (20 + 110 + 30, 40 + 92 + 30)   # 2ª coluna, 2ª linha (sem pendrive nem programas instalados)


def esperar(m, texto, tempo=15, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def menu(m):
    """Botão direito na Lixeira; devolve (x, y, itens) do menu."""
    n = len(m.serial())
    m.clique(*LIXEIRA, botao=2)
    s = esperar(m, "[GUI] menu do icone", 10, n)
    r = re.search(r"menu aberto \(tipo \d+, (\d+) itens\) em \((\d+),(\d+)\)", s)
    return int(r.group(2)), int(r.group(3)), int(r.group(1))


def main():
    with Maquina("h133", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("mkdir /.lixeira")
        m.comando("echo velho > /.lixeira/velho.txt")
        m.comando("mkdir /.lixeira/pasta")
        m.comando("echo dentro > /.lixeira/pasta/x.txt")
        m.esperar_serial("[GUI] lixeira cheia", 10)

        x, y, itens = menu(m)
        verificar(itens == 4, f"com a lixeira cheia, o menu tem {itens} itens, um deles Esvaziar lixeira")
        time.sleep(0.5)
        m.captura("H133-1-menu")
        n = len(m.serial())
        m.clique(x + 60, y + 4 + 1 * 26 + 13)        # o 2º item
        esperar(m, "[GUI] lixeira: esvaziar", 10, n)
        esperar(m, '"Esvaziar lixeira"', 10, n)
        time.sleep(0.8)
        verificar("criada por" in m.serial()[n:] and '"Arquivos"' not in m.serial()[n:], "a pergunta abre, sem a janela do Arquivos")
        m.captura("H133-2-pergunta")
        m.tecla("esc")
        esperar(m, "arquivos: esvaziar lixeira: desistiu", 10, n)
        out = m.comando("ls /.lixeira")
        verificar("velho.txt" in out and "pasta" in out, "Esc desiste: nada foi apagado")

        x, y, _ = menu(m)
        n = len(m.serial())
        m.clique(x + 60, y + 4 + 1 * 26 + 13)
        esperar(m, '"Esvaziar lixeira"', 10, n)
        time.sleep(0.8)
        d = janela(m.serial(), "Esvaziar lixeira")
        m.clique(d["x"] + d["l"] - 16 - 45, d["y"] + d["a"] - 40 + 13)   # Sim
        esperar(m, "arquivos: lixeira esvaziada /.lixeira (0)", 10, n)
        esperar(m, "[GUI] lixeira vazia", 10, n)
        verificar(True, "Sim esvazia, e o ícone volta a ficar vazio")
        out = m.comando("ls /.lixeira")
        verificar("velho.txt" not in out and "pasta" not in out and "terminou com codigo 0" in out,
                  f"/.lixeira continua existindo, vazia (ls sai com código 0 e não lista nada)")
        time.sleep(0.5)
        m.captura("H133-3-vazia")

        _, _, itens = menu(m)
        verificar(itens == 3, f"com a lixeira vazia, o item some ({itens} itens)")
        m.tecla("esc")


if __name__ == "__main__":
    main()
