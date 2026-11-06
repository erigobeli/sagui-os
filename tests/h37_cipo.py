#!/usr/bin/env python3
"""H37 - Cipó, o navegador (D033).

Critério: o Cipó abre uma página HTML servida pela máquina dos testes (que,
de dentro do QEMU, fica em 10.0.2.2), mostra o título, segue um link
relativo com um clique e volta com o botão Voltar.
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

PAGINA_A = """<!DOCTYPE html><html><head><meta charset="utf-8"><title>Página A</title>
<style>body { color: red }</style><script>alert("não aparece")</script></head>
<body><h1>Olá, sagui!</h1><p><a href="pasta/../b.html">segunda página</a> e mais texto &amp; ação&eacute;.</p>
<ul><li>um</li><li>dois</li></ul></body></html>"""
PAGINA_B = "<html><head><title>Página B</title></head><body><p>Chegou &#233; aqui.</p></body></html>"


def main():
    pasta = os.path.join(SAIDA, "h37-www")
    os.makedirs(pasta, exist_ok=True)
    for nome, html in (("index.html", PAGINA_A), ("b.html", PAGINA_B)):
        with open(os.path.join(pasta, nome), "w", encoding="utf-8") as f:
            f.write(html)
    tratador = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    tratador.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), tratador)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    porta = srv.server_address[1]

    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    # O programa só abre depois que o DHCP terminou: o kernel começa o
    # programa de teste logo no boot, então ele espera um pouco antes.
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{porta}/", ["--vesa"])
    with Maquina("h37", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial("cipo: pronto", 60)
        s = m.serial()
        jan = re.search(r'janela (\d+) criada por \d+: "Cip.*?" (\d+)x(\d+) em \((\d+),(\d+)\)', s)
        jx, jy = int(jan.group(4)), int(jan.group(5))
        m.esperar_serial('pronto "P', 30)
        s = m.serial()
        verificar(re.search(r'cipo: pronto "P.gina A" \(\d+ linhas, 1 links\)', s), "a página A abriu, com o título e um link")
        m.captura("H37-cipo-pagina")
        # O link fica na terceira linha (título, linha em branco, parágrafo).
        m.clique(jx + 2 + 12 + 20, jy + 24 + 40 + 4 + 3 * 18 + 8)
        m.esperar_serial("gina B", 20)
        verificar("abrindo http://10.0.2.2:%d/b.html" % porta in m.serial(), "o clique seguiu o link relativo (pasta/../b.html -> /b.html)")
        m.clique(jx + 2 + 40, jy + 24 + 20)   # Voltar
        time.sleep(2)
        verificar(len(re.findall(r'cipo: pronto "P.gina A"', m.serial())) == 2, "Voltar abriu a página A de novo")
        m.clique(jx + 2 + 400, jy + 24 + 20)                      # clique no endereço e digita por cima
        time.sleep(0.4)
        m.digitar(f"10.0.2.2:{porta}/b.html\n", 0.08)
        m.esperar_serial(f"cipo: sem HTTPS, tentando http://10.0.2.2:{porta}/b.html", 30)
        time.sleep(3)
        verificar(len(re.findall(r'cipo: pronto "P.gina B"', m.serial())) == 2, "digitar troca o endereço, e sem HTTPS cai para HTTP")
        m.clique(jx + 2 + 40, jy + 24 + 20)                       # Voltar para a página A
        time.sleep(2)
        m.clique(jx + 864 - 2 - 40 + 8, jy + 12)                  # maximizar
        m.esperar_serial("maximizada: 1020x718", 10)
        time.sleep(3)
        verificar(len(re.findall(r'cipo: pronto "P.gina A"', m.serial())) == 4, "maximizado, diagrama a página de novo")
        m.captura("H37-cipo-maximizado")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
