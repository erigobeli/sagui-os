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
<button id="b">clique</button>
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
<div id="app"></div>
<script src="/ext.js"></script>
<script>
class A { constructor(x) { this.x = x; } dobro() { return this.x * 2; } static s() { return "st"; } }
class B extends A { constructor() { super(21); } dobro() { return super.dobro() + 0; } get g() { return "get"; } }
const fs = [1, 2, 3].map(n => () => n * n);
let [p, ...r] = [1, 2, 3], {q, w = 7} = {q: 5};
let m = new Map([["a", 1]]);
var e1 = "";
try { null.x; } catch (e) { e1 = e.name; } finally { e1 += "!"; }
var o = JSON.parse('{"a":[1,{"b":"c"}],"d":null}');
console.log("t1 " + new B().dobro() + " " + new B().g + " " + A.s() + " " + fs[2]() + " " + r.join("") + " " + q + w + " " + m.get("a") + " " + e1);
console.log("t2 " + JSON.stringify(o) + " " + `x${1 + 1}y` + " " + "a-b_c".replace(/[-_](.)/g, (t, c) => c.toUpperCase()) + " " + /(\\d+)-(\\d+)/.exec("ab 12-34")[2] + " " + "a,b;c".split(/[,;]/).length);
console.log("t3 " + (0.1 + 0.2).toFixed(2) + " " + 7 / 2 + " " + (5 >>> 1) + " " + Math.floor(-1.5) + " " + [3, 1, 2].sort().join("") + " " + typeof undefinedVar + " " + (o?.z?.y ?? "nn") + " " + [..."ab"].length);
async function af() { const v = await Promise.resolve(4); return v + 1; }
af().then(v => console.log("t4 " + v));
setTimeout(() => { document.getElementById("app").innerHTML += "<p class=t>timer ok</p>"; }, 50);
document.addEventListener("DOMContentLoaded", () => {
  const el = document.createElement("p"); el.textContent = "criado <ok>"; el.className = "c1"; el.classList.add("c2");
  document.querySelector("#app").appendChild(el);
  console.log("t5 " + document.querySelectorAll("p.c1.c2").length + " " + el.parentNode.id + " " + document.body.tagName);
  function* conta(n) { for (var i = 0; i < n; i++) { var x = yield i; if (x) i += x; } return "fim"; }
  var g = conta(5), r = [g.next().value, g.next().value, g.next(2).value, g.next().done, g.next().value];
  var o = { *pares() { yield 2; yield* [4, 6]; } }, sp = [...o.pares()];
  var gx = conta(3); gx.next(); var pego = 0; try { gx.throw(new Error("x")); } catch (e) { pego = 1; }
  var gr = conta(9); gr.next(); var rt = gr.return(7);
  console.log("t7 " + r.join(",") + " " + sp.join("+") + " " + pego + " " + rt.value + rt.done);
});
var cl = 0;
document.getElementById("b").addEventListener("click", () => { cl++; document.getElementById("b").textContent = "clicou " + cl; console.log("t6 " + cl); });
</script>
</body></html>"""
EXT = 'console.log("externo " + location.pathname); document.getElementById("app").innerHTML = "<p>do externo</p>";' 


def main():
    pasta = os.path.join(SAIDA, "h41-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w") as f:
        f.write(HTML)
    with open(os.path.join(pasta, "ext.js"), "w") as f:
        f.write(EXT)
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
        verificar("cipo: js 4 scripts, 1 parados" in s, "script desconhecido para sem derrubar a página")
        verificar("js: externo /" in s, "script externo")
        verificar("js: t1 42 get st 9 23 57 1 TypeError!" in s, "classes, closures, desestruturação e exceções")
        verificar('js: t2 {"a":[1,{"b":"c"}],"d":null} x2y aBC 34 3' in s, "JSON, modelos e regex")
        verificar("js: t3 0.30 3.5 2 -2 123 undefined nn 2" in s, "números e operadores novos")
        verificar("js: t4 5" in s, "async e promessas")
        verificar("js: t5 1 app BODY" in s, "DOM e eventos de carga")
        verificar("js: t7 0,1,4,true, 2+4+6 1 7true" in s, "geradores: next, yield*, throw e return")
        m.clique(197, 158)
        m.esperar_serial("cipo: clique js 0", 20)
        m.clique(197, 158)
        m.esperar_serial("js: t6 2", 20)
        time.sleep(1)
        s = m.serial()
        verificar("js: t6 1" in s, "clique roda o evento e a página é refeita")
        m.captura("H41-js")
        verificar("cipo: pronto" in s, "página pronta")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
