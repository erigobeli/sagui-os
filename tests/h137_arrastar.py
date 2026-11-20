#!/usr/bin/env python3
"""H137 - arrastar arquivos no Arquivos.

Critérios:
  - arrastar um arquivo para uma pasta da lista move o arquivo para ela;
  - durante o arrastar, o nome vai junto do cursor com "Mover para <pasta>";
  - arrastar dois marcados para uma pasta da árvore do painel move os dois;
  - arrastar para um pedaço do caminho move para lá;
  - arrastar para a Lixeira do painel pergunta e manda para a lixeira;
  - arrastar uma pasta para dentro dela mesma não faz nada;
  - soltar fora de um destino não muda nada.
"""
import os
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


def arrastar(m, de, para, foto=None):
    m.mouse_para(*de)
    m.monitor("mouse_button 1")
    time.sleep(0.2)
    m.mouse_mover(para[0], para[1])
    time.sleep(0.6)
    if foto:
        m.captura(foto)
    m.monitor("mouse_button 0")
    time.sleep(0.8)


def main():
    with Maquina("h137", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("mkdir /tmp/d")
        m.comando("mkdir /tmp/d/Destino")
        m.comando("mkdir /tmp/d/Outra")
        m.comando("echo a > /tmp/d/a.txt")
        m.comando("echo b > /tmp/d/b.txt")
        m.comando("echo c > /tmp/d/c.txt")
        n = len(m.serial())
        m.digitar("arquivos /tmp/d\n", 0.03)
        esperar(m, "arquivos: ordem: Destino, Outra, a.txt, b.txt, c.txt", 30, n)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")

        # a.txt (linha 2) para Destino (linha 0).
        n = len(m.serial())
        m.clique(*ui.item(a, 2))
        time.sleep(0.6)
        arrastar(m, ui.item(a, 2), ui.item(a, 0), foto="H137-1-arrastando")
        s = esperar(m, "arquivos: movido /tmp/d/a.txt -> /tmp/d/Destino/a.txt", 10, n)
        verificar("arquivos: arrastando 1 itens" in s, "arrastar a.txt para Destino move o arquivo")
        esperar(m, "arquivos: ordem: Destino, Outra, b.txt, c.txt", 10, n)

        # b.txt e c.txt (marcados com o laço) para "Outra" na árvore do painel.
        lx = a["x"] + ui.LISTA_X + 520                      # fora da coluna do nome (na data)
        m.mouse_para(lx, ui.item(a, 2)[1] - 6)
        m.monitor("mouse_button 1")
        time.sleep(0.2)
        m.mouse_mover(a["x"] + ui.LISTA_X + 40, ui.item(a, 3)[1] + 4)
        m.monitor("mouse_button 0")
        esperar(m, "arquivos: 2 marcados", 10)
        # abre "d" na árvore pela setinha (nível 2) e rola o painel até as filhas
        _, yd = ui.no_painel(m.serial(), a, "d")
        n = len(m.serial())
        m.clique(a["x"] + 8 + 2 * 14 + 5, yd)
        esperar(m, "arquivos: arvore abre /tmp/d", 10, n)
        m.mouse_para(a["x"] + 100, a["y"] + 400)
        for _ in range(3):
            m.monitor("mouse_move 0 0 -1")
            time.sleep(0.15)
        time.sleep(0.6)
        x, y = ui.no_painel(m.serial(), a, "Outra")
        n = len(m.serial())
        arrastar(m, ui.item(a, 2), (x, y), foto="H137-2-para-a-arvore")
        s = esperar(m, "arquivos: movido /tmp/d/c.txt -> /tmp/d/Outra/c.txt", 10, n)
        verificar("arquivos: movido /tmp/d/b.txt -> /tmp/d/Outra/b.txt" in s and "arrastando 2 itens" in s,
                  "arrastar os 2 marcados para uma pasta da árvore move os dois")
        esperar(m, "arquivos: ordem: Destino, Outra", 10, n)
        time.sleep(0.6)

        # Destino (já marcado: é o primeiro) para o pedaço "tmp" do caminho.
        n = len(m.serial())
        x_tmp = 158 + 34 + 56 + 8 + 14 + 12
        arrastar(m, ui.item(a, 0), (a["x"] + x_tmp, a["y"] + ui.END_Y + 22))
        esperar(m, "arquivos: movido /tmp/d/Destino -> /tmp/Destino", 10, n)
        verificar(True, "arrastar para um pedaço do caminho move para lá")
        esperar(m, "arquivos: ordem: Outra", 10, n)
        time.sleep(0.6)

        # Outra (já marcada) para a Lixeira do painel: pergunta (Sim) e vai para a lixeira.
        n = len(m.serial())
        arrastar(m, ui.item(a, 0), ui.lixeira(m.serial(), a))
        esperar(m, '"Apagar"', 10, n)
        time.sleep(0.6)
        m.digitar("\n", 0.05)
        esperar(m, "arquivos: apagado /tmp/d/Outra (para a lixeira: /.lixeira/Outra)", 10, n)
        verificar(True, "arrastar para a Lixeira pergunta e manda para a lixeira")

        # Uma pasta para dentro dela mesma: nada.
        t = janela(m.serial(), "Terminal")
        m.clique(t["fx"] + 100, t["fy"] + 12)               # o Terminal na frente (a barra dele aparece acima do Arquivos)
        time.sleep(0.5)
        m.comando("mkdir /tmp/d/Funda")
        m.clique(a["x"] + a["l"] - 30, a["y"] + 400)         # o Arquivos de volta na frente (a ponta direita não fica sob o Terminal)
        time.sleep(0.5)
        n = len(m.serial())
        m.tecla("f5")                                       # atualiza: a Funda aparece
        esperar(m, "arquivos: ordem: Funda", 10, n)
        time.sleep(0.6)
        n = len(m.serial())                                 # Funda já vem marcada
        arrastar(m, ui.item(a, 0), ui.no_painel(m.serial(), a, "Funda"))
        time.sleep(0.5)
        verificar("arquivos: movido" not in m.serial()[n:], "uma pasta não vai para dentro dela mesma")

        # Soltar fora de um destino: nada.
        n = len(m.serial())
        arrastar(m, ui.item(a, 0), (a["x"] + a["l"] - 80, a["y"] + ui.LISTA_Y + 300))
        s = esperar(m, "arquivos: arrastar: solto fora de um destino", 10, n)
        verificar("arquivos: movido" not in s, "soltar fora de um destino não muda nada")


if __name__ == "__main__":
    main()
