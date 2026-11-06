#!/usr/bin/env python3
"""H41: o interpretador de JavaScript do Cipó."""
import functools
import http.server
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

HTML = """<html><head><title>Antes</title></head><body>
<p id="alvo">velho</p>
<script>
function fat(n) { if (n <= 1) { return 1; } return n * fat(n - 1); }
var total = 0;
for (var i = 1; i <= 10; i++) { if (i == 5) { continue; } total += i; }
var s = "fat=" + fat(5) + " soma=" + total;
document.write("<p>" + s + "</p>");
document.getElementById("alvo").textContent = "novo " + s.length;
document.title = "Depois";
console.log("resultado " + s);
</script>
<script>var y = desconhecido.coisa(1); document.write("NAO");</script>
<script>console.log("global " + total);</script>
</body></html>"""


def main():
    pasta = os.path.join(SAIDA, "h41-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w") as f:
        f.write(HTML)
    trat = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    trat.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), trat)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h41", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "Depois"', 60)
        time.sleep(1)
        s = m.serial()
        verificar("js: resultado fat=120 soma=50" in s, "funções, recursão, for, continue e textos")
        verificar("js: global 50" in s, "variáveis valem entre scripts")
        verificar("cipo: js 2 scripts, 1 parados" in s, "script desconhecido para sem derrubar a página")
        m.captura("H41-js")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
