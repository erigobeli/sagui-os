#!/usr/bin/env python3
"""H57: bateria de 129 recursos de JavaScript e DOM no Cipó.

Cada recurso roda isolado em new Function (erro de sintaxe num não derruba
os outros). Cobre ES2015-2025 (Symbol, Proxy, classes com #privado e static
{}, iteradores, Map/Set, Intl, Promise.any...) e APIs do navegador
(DOMParser, TreeWalker, Range, FileReader, indexedDB...). Os que ainda faltam
estão listados em FALTAM; qualquer outro que falhar é regressão.
"""
import functools
import http.server
import os
import re
import shutil
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

FALTAM = {"RegExp v flag", "extends Array", "Array holes", "with"}


def main():
    pasta = os.path.join(SAIDA, "h57-www")
    os.makedirs(pasta, exist_ok=True)
    shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "recursos", "h57_recursos.html"), os.path.join(pasta, "index.html"))
    tratador = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    tratador.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), tratador)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h57", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "Recursos"', 180)
        s = m.serial()
        tot = re.search(r"FALTA (\d+)/(\d+)", s)
        verificar(tot is not None, "a bateria rodou")
        faltas = [re.sub(r" \[.*$", "", x.strip()) for x in re.findall(r"falta: ([^\n]*)", s)]
        print("faltam:", faltas)
        verificar(int(tot.group(2)) == 129, "129 recursos testados")
        novos = [f for f in faltas if f not in FALTAM]
        verificar(not novos, f"nenhum recurso novo faltando ({novos})")


if __name__ == "__main__":
    main()
