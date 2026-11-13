#!/usr/bin/env python3
"""H25 - Editor de texto.

Critério: teste cria tmp/nota.txt com o texto `Olá, SaguiOS! Ação e coração.`,
salva, fecha, reabre e confere o conteúdo. (A reabertura pelo Arquivos é
conferida no teste da H23.) Também: título com nome e "*", aviso ao fechar
sem salvar, Ctrl+S.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402

TEXTO = "Olá, SaguiOS! Ação e coração."


def janela(serial, titulo):
    m = re.findall(rf'criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial)[-1]
    return dict(l=int(m[0]), a=int(m[1]), x=int(m[2]), y=int(m[3]))


def fechar_pelo_x(m, j):
    m.clique(j["x"] + j["l"] + 4 - 2 - 20 + 8, j["y"] + 12)


def main():
    with Maquina("h25", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("editor")
        m.esperar_serial("editor: pronto", 20)
        time.sleep(0.5)
        m.digitar(TEXTO, 0.05)
        m.esperar_serial("editor: titulo Editor - sem nome *", 10)
        verificar(True, "o título ganha * ao digitar")
        m.captura("H25-editor-digitando")
        m.tecla("ctrl-s")
        m.esperar_serial('"Salvar como"', 10)
        time.sleep(0.4)
        m.digitar("nota.txt\n", 0.05)                 # a caixa já vem com "/tmp/"
        m.esperar_serial("editor: salvo /tmp/nota.txt", 10)
        n = int(re.findall(r"editor: salvo /tmp/nota.txt \((\d+) bytes\)", m.serial())[-1])
        verificar(n == len(TEXTO.encode("latin-1")), f"Ctrl+S sem nome pede o caminho e salva /tmp/nota.txt ({n} bytes)")
        m.esperar_serial("editor: titulo Editor - nota.txt\r", 10)
        verificar(True, "o título mostra o nome do arquivo, sem *")
        fechar_pelo_x(m, janela(m.serial(), "Editor - sem nome"))
        m.esperar_serial("editor: fechado", 10)
        verificar(True, "sem alterações, o Editor fecha direto")

        time.sleep(0.5)
        out = m.comando("cat /tmp/nota.txt")
        verificar(TEXTO in out, "cat /tmp/nota.txt mostra o texto com acentos")

        m.comando("editor /tmp/nota.txt")
        m.esperar_serial("editor: aberto /tmp/nota.txt", 10)
        verificar(True, "editor /tmp/nota.txt reabre o arquivo")
        time.sleep(0.5)
        m.captura("H25-editor-reaberto")
        m.tecla("end")
        m.digitar(" Mais.", 0.05)
        m.esperar_serial("editor: titulo Editor - nota.txt *", 10)
        fechar_pelo_x(m, janela(m.serial(), "Editor - sem nome"))   # criada com esse título, que depois muda
        m.esperar_serial('"Editor" 380x', 10)
        time.sleep(0.4)
        m.captura("H25-aviso-ao-fechar")
        d = janela(m.serial(), "Editor")
        m.clique(d["x"] + 2 + 380 - 16 - 190 + 45, d["y"] + 24 + d["a"] - 40 + 13)   # "Não"
        m.esperar_serial("editor: fechado", 10)
        verificar(True, "fechar com alteração pergunta se quer salvar; 'Não' fecha sem salvar")
        time.sleep(0.5)
        out = m.comando("cat /tmp/nota.txt")
        verificar(TEXTO in out and "Mais." not in out, "o arquivo continua como foi salvo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
