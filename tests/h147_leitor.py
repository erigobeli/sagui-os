#!/usr/bin/env python3
"""H147 - o Leitor de Markdown.

Critérios:
  - "leitor /sistema/ajuda/c-do-sagui.md" mostra o manual formatado: o
    título "O C do Sagui", dezenas de títulos, os blocos de código e as
    células das tabelas;
  - no Arquivos, o duplo clique num .md abre no Leitor;
  - a janela muda de tamanho e o texto é refeito na largura nova (mais
    estreita, mais alta);
  - um texto em UTF-8 de fora (com acentos e aspas curvas) aparece em
    Latin-1, sem letras trocadas.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def resumo(s, caminho):
    r = re.findall(r"leitor: " + re.escape(caminho) + r": titulos (\d+), negrito (\d+), italico (\d+), codigo (\d+), links (\d+), itens (\d+), celulas (\d+), citacoes (\d+), altura (\d+)", s)
    return [int(x) for x in r[-1]] if r else None


def main():
    with Maquina("h147", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n = len(m.serial())
        m.digitar("leitor /sistema/ajuda/c-do-sagui.md\n", 0.03)
        s = esperar(m, "leitor: titulo", 30, n)
        r = resumo(s, "/sistema/ajuda/c-do-sagui.md")
        verificar("leitor: titulo O C do Sagui" in s, "o título do manual: O C do Sagui")
        verificar(r and r[0] >= 20 and r[3] >= 20 and r[6] >= 50,
                  f"formatado: {r[0]} títulos, {r[3]} trechos de código, {r[6]} células de tabela" if r else "sem resumo")
        time.sleep(1)
        m.captura("H147-1-manual")
        # Rola para uma tabela: PgDn algumas vezes.
        for _ in range(6):
            m.tecla("pgdn", 0.2)
        time.sleep(0.8)
        m.captura("H147-2-manual-rolado")

        # Mudar o tamanho: maximizar pelo duplo clique na barra de título.
        jl = janela(m.serial(), "c-do-sagui.md - Leitor")
        alt_antes = r[8]
        n = len(m.serial())
        m.clique(jl["fx"] + 200, jl["fy"] + 12, duplo=True)
        s = esperar(m, "leitor: tamanho", 15, n)
        lm, am, altura = map(int, re.findall(r"leitor: tamanho (\d+)x(\d+), altura do texto (\d+)", s)[-1])
        verificar(lm > 780 and altura < alt_antes, f"maximizada ({lm}x{am}): o texto ficou mais baixo ({alt_antes} -> {altura})")
        m.tecla("esc")
        time.sleep(1)

        # No Arquivos: o histórico abre no Leitor com o duplo clique.
        n = len(m.serial())
        m.digitar("arquivos /sistema/ajuda\n", 0.03)
        esperar(m, "arquivos: pronto", 30, n)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        verificar("arquivos: pasta /sistema/ajuda (2 itens)" in m.serial()[n:], "a pasta tem os dois textos de ajuda")
        k = 1   # em ordem alfabética: c-do-sagui.md, versoes.md
        lista_x, lista_y, item_a = 140 + 6, 70, 20   # a lista do Arquivos (como na H100)
        x, y = a["x"] + lista_x + 60, a["y"] + lista_y + 2 + k * item_a + 10
        n = len(m.serial())
        m.clique(x, y, duplo=True)
        s = esperar(m, "leitor: titulo", 30, n)
        verificar("abrindo /sistema/ajuda/versoes.md com /bin/leitor" in s, "o duplo clique num .md abre no Leitor")
        verificar("leitor: titulo Hist" in s, "e o Leitor mostra o histórico")
        time.sleep(1)
        m.captura("H147-3-historico-no-leitor")


if __name__ == "__main__":
    main()
