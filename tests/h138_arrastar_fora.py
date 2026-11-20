#!/usr/bin/env python3
"""H138 - arrastar arquivos para fora do Arquivos: para outra janela e para
ícones da área de trabalho.

Critérios:
  - arrastar um arquivo do Arquivos até o ícone do Editor, na área de
    trabalho: enquanto passa sobre ele, o kernel mostra "Abrir com Editor";
    soltar abre o arquivo no Editor;
  - arrastar de uma janela do Arquivos para a outra: soltar em "Temporários"
    no painel da outra janela move o arquivo para /tmp;
  - soltar na área de trabalho vazia não faz nada;
  - soltar no ícone da Lixeira (com a 1ª janela minimizada, para ele
    aparecer) pergunta e manda o arquivo para a lixeira.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arquivos_ui as ui  # noqa: E402
from qemu import Maquina, FalhaTeste, verificar  # noqa: E402
import re


def esperar(m, texto, tempo=15, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def janela_desde(serial, titulo, desde):
    m = re.findall(rf'janela (\d+) criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial[desde:])[-1]
    return dict(id=int(m[0]), l=int(m[1]), a=int(m[2]), x=int(m[3]) + 2, y=int(m[4]) + 24, fx=int(m[3]), fy=int(m[4]))


def arrastar(m, de, para, meio=None, foto=None):
    m.mouse_para(*de)
    m.monitor("mouse_button 1")
    time.sleep(0.2)
    if meio:
        m.mouse_mover(*meio)
        time.sleep(0.3)
    m.mouse_mover(*para)
    time.sleep(0.8)
    if foto:
        m.captura(foto)
    m.monitor("mouse_button 0")
    time.sleep(1)


EDITOR = (20 + 48, 40 + 2 * 92 + 30)      # 3º ícone da coluna da esquerda


def main():
    with Maquina("h138", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        t = janela_desde(m.serial(), "Terminal", 0)
        m.comando("mkdir /tmp/k")
        m.comando("mkdir /tmp/k2")
        m.comando("echo vai mudar > /tmp/k/mover.txt")
        m.comando("echo uma nota > /tmp/k2/nota.txt")
        n = len(m.serial())
        m.digitar("arquivos /tmp/k2\n", 0.03)
        esperar(m, "arquivos: ordem: nota.txt", 30, n)
        time.sleep(1)
        w1 = janela_desde(m.serial(), "Arquivos", n)

        # nota.txt (já marcada) até o ícone do Editor, na área de trabalho.
        n = len(m.serial())
        arrastar(m, ui.item(w1, 0), EDITOR, meio=(w1["x"] + 40, w1["y"] + 300), foto="H138-1-abrir-com-editor")
        s = esperar(m, "editor: aberto /tmp/k2/nota.txt", 15, n)
        verificar("arrastando para fora da janela" in s and "[GUI] arrasto sobre: Abrir com Editor" in s,
                  "o kernel leva o arrastar para fora e mostra \"Abrir com Editor\" sobre o ícone")
        verificar("[GUI] soltos no icone Editor" in s, "soltar no ícone do Editor abre o arquivo nele")
        m.tecla("alt-f4")
        time.sleep(1)

        # A 2ª janela (pelo Terminal, cuja barra aparece acima do Arquivos), afastada.
        m.clique(t["fx"] + 120, t["fy"] + 12)
        time.sleep(0.5)
        n = len(m.serial())
        m.digitar("arquivos /tmp/k\n", 0.03)
        esperar(m, "arquivos: ordem: mover.txt", 30, n)
        time.sleep(1)
        w2 = janela_desde(m.serial(), "Arquivos", n)
        m.mouse_para(w2["fx"] + 400, w2["fy"] + 12)
        m.monitor("mouse_button 1")
        time.sleep(0.2)
        m.mouse_mover(w2["fx"] + 400 + 300, w2["fy"] + 12 + 220)
        m.monitor("mouse_button 0")
        s = esperar(m, f"janela {w2['id']} movida para", 10, n)
        mx, my = map(int, re.findall(rf"janela {w2['id']} movida para \((-?\d+),(-?\d+)\)", s)[-1])
        w2.update(x=mx + 2, y=my + 24, fx=mx, fy=my)
        time.sleep(0.8)

        # mover.txt (marcada) da 2ª para "Temporários" no painel da 1ª.
        n = len(m.serial())
        alvo = (w1["x"] + 60, w1["y"] + ui.CORPO_Y + 10 + 14)
        arrastar(m, ui.item(w2, 0), alvo, meio=(w2["x"] + 40, w2["y"] - 30), foto="H138-2-outra-janela")
        s = esperar(m, "arquivos: movido /tmp/k/mover.txt -> /tmp/mover.txt", 15, n)
        verificar(f"[GUI] arrasto solto na janela {w1['id']}" in s and "arquivos: recebidos 1 itens para /tmp" in s,
                  "soltar em \"Temporários\" na outra janela move o arquivo para /tmp")
        time.sleep(0.8)

        # Da 2ª janela para a área de trabalho vazia (abaixo da 1ª, à esquerda da doca): nada.
        n = len(m.serial())
        m.clique(*ui.comando(w2, "Novo"))                    # um arquivo novo na 2ª janela
        time.sleep(0.5)
        m.clique(*ui.menu_comando(w2, "Novo", 1))            # Documento de texto
        esperar(m, "arquivos: renomeando Novo arquivo.txt", 10, n)
        m.tecla("ret")
        esperar(m, "arquivos: ordem: Novo arquivo.txt", 10, n)
        time.sleep(0.6)
        n = len(m.serial())
        arrastar(m, ui.item(w2, 0), (300, 728), meio=(w2["x"] + 40, w2["y"] - 30))
        s = esperar(m, "[GUI] arrasto solto fora de um destino", 10, n)
        verificar("arrastando para fora" in s and "arquivos: movido" not in s and "arquivos: copiado" not in s,
                  "soltar na área de trabalho vazia não faz nada")

        # A Lixeira da área de trabalho: minimiza a 1ª janela e solta lá.
        n = len(m.serial())
        m.clique(w1["x"] + w1["l"] - 37, w1["y"] - 12)        # minimizar a 1ª (o botão do meio)
        time.sleep(1.2)
        m.clique(w2["x"] + w2["l"] // 2 - 100, w2["y"] + 300)
        time.sleep(0.5)
        n = len(m.serial())
        arrastar(m, ui.item(w2, 0), (20 + 110 + 48, 40 + 92 + 30), meio=(w2["x"] + 40, w2["y"] - 30), foto="H138-3-lixeira")
        s = esperar(m, '"Apagar"', 15, n)
        verificar("[GUI] arrasto sobre: Mandar para a Lixeira" in s and "soltos no icone Lixeira" in s,
                  "sobre o ícone da Lixeira aparece \"Mandar para a Lixeira\"")
        time.sleep(0.6)
        m.tecla("ret")
        esperar(m, "arquivos: apagado /tmp/k/Novo arquivo.txt (para a lixeira: /.lixeira/Novo arquivo.txt)", 10, n)
        verificar(True, "soltar no ícone da Lixeira pergunta e manda o arquivo para a lixeira")


if __name__ == "__main__":
    main()
