#!/usr/bin/env python3
"""H53: coletor de lixo do JavaScript do Cipó.

A página gera muito mais lixo do que cabe na memória do motor (160 MB) e
confere que o que continua vivo segue intacto: objetos, Map/Set, closures,
arrows (this), DOM e uma função async que suspende no meio das coletas.
Também confere cookies (document.cookie) e navegação pelo script.
"""
import functools
import http.server
import os
import re
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

HTML = """<html><head><title>GC</title></head><body><div id="r"></div><script>
var vivos=[], m=new Map(), s=new Set(), fns=[];
function lixo(n){var x;for(var i=0;i<n;i++){x={a:i,b:"str"+i+"-"+Math.random(),c:[i,i+1,i+2]};}return x}
for(let k=0;k<2000;k++){var o={id:k,nome:"obj"+k,filhos:[k*2,k*3],txt:("x"+k).repeat(20)};vivos.push(o);m.set("k"+k,o);s.add(o.nome);
  fns.push(function(){return k*7});fns.push(()=>this===window);lixo(15)}
var el=document.getElementById("r");for(var k=0;k<300;k++){var d=document.createElement("p");d.textContent="p"+k;el.appendChild(d);lixo(15)}
var bom=true;for(var k=0;k<2000;k++){var o=vivos[k];if(o.id!==k||o.nome!=="obj"+k||o.filhos[1]!==k*3||o.txt.length!==("x"+k).length*20||m.get("k"+k)!==o||!s.has("obj"+k)||fns[2*k]()!==k*7||fns[2*k+1]()!==true)bom=false}
var dom=el.childNodes.length===300&&el.childNodes[299].textContent==="p299";
async function f(n){var t=[];for(var i=0;i<n;i++){t.push({v:i,s:"a"+i});lixo(100);await null}return t}
document.cookie="sagui=1; path=/";
f(60).then(function(t){var a=t.length===60&&t[59].v===59&&t[59].s==="a59";
  console.log("GC "+bom+" "+dom+" "+a+" "+(document.cookie.indexOf("sagui=1")>=0));location.assign("/b.html")});
</script></body></html>"""

cookies = []


class Tratador(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        cookies.append(self.headers.get("Cookie") or "")
        super().do_GET()


def main():
    pasta = os.path.join(SAIDA, "h53-www")
    os.makedirs(pasta, exist_ok=True)
    open(os.path.join(pasta, "index.html"), "w").write(HTML)
    open(os.path.join(pasta, "b.html"), "w").write("<title>B</title><p>chegou</p>")
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Tratador, directory=pasta))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h53", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "B"', 240)
        s = m.serial()
        coletas = len(re.findall(r"js: coleta \d+:", s))
        print("coletas", coletas)
        verificar(coletas >= 3, "o coletor rodou várias vezes durante a página")
        verificar("abortado: memoria" not in s, "sem estourar a memória do motor")
        verificar("js: GC true true true true" in s, "objetos, Map/Set, closures, arrows, DOM, async e cookie intactos depois das coletas")
        verificar("o script navegou para" in s, "location.assign navega")
        verificar(any("sagui=1" in c for c in cookies), "document.cookie vai no pedido seguinte")


if __name__ == "__main__":
    main()
