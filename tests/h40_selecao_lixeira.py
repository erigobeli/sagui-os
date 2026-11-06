#!/usr/bin/env python3
"""
H40: laço de seleção, lixeira, seleção múltipla, copiar e colar, Atualizar e CSS.
"""
import functools
import http.server
import os
import re
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402


def janela(s, titulo):
    m = re.findall(rf'criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', s)[-1]
    return dict(x=int(m[2]) + 2, y=int(m[3]) + 24)


def item(a, i):
    return a["x"] + 146 + 120, a["y"] + 72 + i * 20 + 8


def main():
    with Maquina("h40", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        for nome in ("um", "dois"):
            m.comando(f"echo {nome} > /tmp/{nome}.txt")
            time.sleep(0.5)
        m.comando("arquivos /tmp")
        m.esperar_serial("arquivos: pronto", 20)
        time.sleep(0.5)
        a = janela(m.serial(), "Arquivos")
        m.clique(*item(a, 1))                                    # um.txt (dois.txt vem antes)
        m.tecla("ctrl-c")
        m.esperar_serial("arquivos: copiado 1 itens", 10)
        m.tecla("ctrl-v")
        m.esperar_serial("arquivos: colado /tmp/um (2).txt", 10)
        verificar(True, "Ctrl+C e Ctrl+V duplicam um arquivo")
        m.tecla("ctrl-a")
        time.sleep(0.5)
        m.captura("H40-1-selecao-multipla")
        m.tecla("delete")
        m.esperar_serial('"Apagar"', 10)
        time.sleep(0.3)
        m.tecla("ret")
        m.esperar_serial("arquivos: pasta /tmp (0 itens)", 15)
        verificar("arquivos: lixeira /tmp/um.txt" in m.serial(), "apagar manda vários para a lixeira")
        m.clique(130, 10)                                         # foca o Terminal pela barra
        m.comando("ls /.lixeira")
        verificar("um.txt" in m.serial(), "os arquivos estão na lixeira")
        m.comando("arquivos /.lixeira")
        m.esperar_serial("arquivos: pasta /.lixeira (3 itens)", 20)
        time.sleep(0.5)
        b = janela(m.serial(), "Arquivos")
        m.clique(*item(b, 0), botao=2)
        m.esperar_serial("[GUI] menu aberto com 2 itens", 10)
        x, y = re.findall(r"menu aberto com 2 itens em \((\d+),(\d+)\)", m.serial())[-1]
        m.clique(int(x) + 20, int(y) + 4 + 12)                    # Restaurar
        m.esperar_serial("arquivos: restaurado /tmp/dois.txt", 10)
        verificar(True, "Restaurar devolve o arquivo ao lugar de origem")

        m.arrastar(500, 640, 10, 120)                             # laço da área de trabalho
        m.esperar_serial("[GUI] icones selecionados:", 10)
        verificar(True, "laço seleciona ícones")
        m.clique(700, 700, botao=2)
        m.esperar_serial("[GUI] menu aberto com 8 itens", 10)
        x, y = re.findall(r"menu aberto com 8 itens em \((\d+),(\d+)\)", m.serial())[-1]
        m.clique(int(x) + 20, int(y) + 4 + 7 * 24 + 18 + 12)     # Atualizar
        m.esperar_serial("[GUI] area de trabalho atualizada", 10)
        verificar(True, "Atualizar no menu da área de trabalho")

    pasta = os.path.join(SAIDA, "h40-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w") as f:
        f.write("""<html><head><title>CSS</title><style>.some{display:none} #meio{text-align:center}
        p.verde{color:#00aa00} @media print{.x{color:red}}</style></head><body>
        <p class="some">ESCONDIDO</p><p id="meio">centro</p><p class="verde">XXXXXXXXXX</p>
        <p style="color:#cc0000">YYYYYYYYYY</p></body></html>""")
    trat = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    trat.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), trat)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    porta = srv.server_address[1]
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    with Maquina("h40-c", "ide", imagem=imagem_de_teste(f"/bin/cipo http://10.0.2.2:{porta}/", ["--vesa"]), extras=extras) as m:
        m.esperar_serial('cipo: pronto "CSS"', 60)
        time.sleep(1)
        s = m.serial()
        verificar("cipo: css 3 regras" in s, "regras de <style> lidas")
        m.captura("H40-2-css")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
