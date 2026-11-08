#!/usr/bin/env python3
"""H47: custom elements, shadow DOM com slots e POST no fetch/XHR."""
import functools
import http.server
import os
import re
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402
from PIL import Image  # noqa: E402

HTML = """<html><head><title>Componentes</title></head><body>
<template id="t"><style>:host{display:block;background:#00c000;padding:10px;margin:6px} .x{color:#ffffff}</style><span class="x">OI <slot name="nome">ninguem</slot></span> <slot></slot></template>
<cartao-sagui cor="a"><b slot="nome">Robson</b> sobra</cartao-sagui>
<div id="lugar"></div>
<script>
class Cartao extends HTMLElement {
  static get observedAttributes() { return ["cor"]; }
  campo = 7;
  constructor() { super(); this.attachShadow({mode: "open"}).appendChild(document.getElementById("t").content.cloneNode(true)); }
  connectedCallback() { console.log("ce con " + this.campo + " " + (this instanceof Cartao) + " " + (this instanceof HTMLElement)); }
  attributeChangedCallback(n, a, v) { console.log("ce attr " + n + " " + a + " " + v); }
}
customElements.whenDefined("cartao-sagui").then(function () { console.log("ce definido"); });
customElements.define("cartao-sagui", Cartao);
var c = document.querySelector("cartao-sagui");
c.setAttribute("cor", "b");
var n = document.createElement("cartao-sagui");
n.textContent = "novo";
document.getElementById("lugar").appendChild(n);
console.log("ce sr " + (c.shadowRoot.querySelectorAll("slot").length) + " " + (c.shadowRoot.host === c) + " " + (document.querySelectorAll("slot").length));
fetch("/eco", {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify({a: 1})})
  .then(function (r) { return r.json(); }).then(function (j) { console.log("post " + j.metodo + " " + j.corpo + " " + j.tipo); });
var x = new XMLHttpRequest(); x.open("PUT", "/eco"); x.setRequestHeader("X-Sagui", "sim");
setTimeout(function () { document.getElementById("lugar").insertAdjacentHTML("beforeend", "<p id=viva>pagina viva</p>"); console.log("viva tarde"); }, 7000);
x.onload = function () { console.log("xhr " + x.status + " " + JSON.parse(x.responseText).metodo + " " + JSON.parse(x.responseText).sagui); }; x.send("oi");
</script>
</body></html>"""


class Eco(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _eco(self):
        n = int(self.headers.get("Content-Length", 0))
        corpo = self.rfile.read(n).decode()
        import json
        r = json.dumps({"metodo": self.command, "corpo": corpo, "tipo": self.headers.get("Content-Type", ""),
                        "sagui": self.headers.get("X-Sagui", "")}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(r)))
        self.end_headers()
        self.wfile.write(r)

    do_POST = _eco
    do_PUT = _eco


def main():
    pasta = os.path.join(SAIDA, "h47-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w") as f:
        f.write(HTML)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Eco, directory=pasta))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h47", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "Componentes"', 60)
        s = m.serial()
        print("\n".join(l for l in s.splitlines() if re.search(r"js: (ce|post|xhr)|js: .*rro", l)))
        verificar("js: ce con 7 true true" in s, "classe sobe sobre o nó, campos e connectedCallback")
        verificar("js: ce attr cor null a" in s and "js: ce attr cor a b" in s, "atributos observados")
        verificar("js: ce definido" in s, "whenDefined")
        verificar("js: ce sr 2 true 0" in s, "shadowRoot separado do documento")
        verificar("js: post POST {\"a\":1} application/json" in s, "fetch POST com corpo e cabeçalho")
        verificar("js: xhr 200 PUT sim" in s, "XHR PUT com setRequestHeader")
        m.esperar_serial("cipo: viva redesenhou", 40)
        s = m.serial()
        verificar("js: viva tarde" in s, "timer roda depois do load")
        verificar("cipo: viva redesenhou" in s, "DOM mudou e a página foi redesenhada")
        png = m.captura("H47-componentes")
        jan = re.search(r'criada por \d+: "Cip.*?" \d+x\d+ em \((\d+),(\d+)\)', s)
        jx, jy = int(jan.group(1)), int(jan.group(2))
        im = Image.open(png).convert("RGB")
        verdes = sum(1 for y in range(jy + 40, jy + 400, 3) for x in range(jx + 4, jx + 840, 3)
                     if (lambda c: c[1] > 150 and c[0] < 60 and c[2] < 60)(im.getpixel((x, y))))
        print("verdes:", verdes)
        verificar(verdes > 500, ":host da sombra pinta o hospedeiro")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
