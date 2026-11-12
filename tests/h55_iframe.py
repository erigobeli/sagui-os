#!/usr/bin/env python3
"""H55: iframes de verdade e BigInt no Cipó.

Cada <iframe> é uma janela própria (window, document, protótipos e timers),
pintada dentro da caixa dele. A página confere: parent/top/frameElement/name
no filho, contentWindow/contentDocument e chamada de função entre janelas,
window.length/window[0], postMessage nos dois sentidos com origem e source,
MessageChannel transferido, e BigInt (aritmética, bits, asIntN). Depois um
clique dentro do quadro roda o onclick do filho, que avisa o pai; o pai
redesenha com o texto novo.
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
from PIL import Image  # noqa: E402

PAI = r"""<!DOCTYPE html><html><head><meta charset="utf-8"><title>Quadros</title>
<style>body{margin:0;font-family:sans-serif}iframe{display:block;margin:20px;border:0}</style></head><body>
<iframe id="f" name="filhinho" src="filho.html" width="320" height="140"></iframe><p id="r">sem resposta</p>
<script>
function R(n,ok,x){console.log('quadro: '+(ok?'ok ':'FALHA ')+n+(x!==undefined?' '+x:''))}
var f=document.getElementById('f'),msgs=0;
window.addEventListener('message',function(e){
  if(e.data&&e.data.oi){R('postMessage do filho',e.origin==location.origin&&e.source===f.contentWindow,e.origin);e.source.postMessage({resp:[1,2,{x:'y'}]},'*')}
  if(e.ports&&e.ports.length)e.ports[0].postMessage('pela porta');
  if(e.data==='clique no filho'){document.getElementById('r').textContent='pai recebeu o clique';console.log('quadro: clique chegou ao pai')}
});
f.addEventListener('load',function(){
  R('contentDocument',f.contentDocument.title=='Filho');
  R('contentWindow chama funcao',f.contentWindow.dobro(21)===42);
  R('window.length e window[0]',window.length==1&&window[0]===f.contentWindow);
  R('janela propria',f.contentWindow!==window&&f.contentWindow.Array!==Array);
});
try{
  R('BigInt aritmetica',String(2n**64n+1n)=='18446744073709551617'&&String(-7n/2n)=='-3'&&typeof 1n=='bigint');
  R('BigInt bits',String(-1n&0xffn)=='255'&&String(1n<<100n)=='1267650600228229401496703205376'&&String(-9n>>1n)=='-5');
  R('BigInt asIntN',String(BigInt.asIntN(64,2n**63n))=='-9223372036854775808'&&String(BigInt.asUintN(64,-1n))=='18446744073709551615');
  var erro=false;try{1n+1}catch(e){erro=e instanceof TypeError}R('BigInt misturado da TypeError',erro);
  R('toString de String e Function',"x".toString()=='x'&&(function a(b){return b}).toString()=='function a(b){return b}');
}catch(e){R('excecao',false,e.message)}
</script></body></html>"""

FILHO = r"""<!DOCTYPE html><html><head><title>Filho</title></head>
<body style="margin:0;background:#cfe8ff;font-family:sans-serif">
<div id="b" style="margin:10px;padding:10px;background:#22aa77;color:#fff;width:200px" onclick="this.textContent='clicado';parent.postMessage('clique no filho','*')">Clique aqui</div>
<script>
function dobro(x){return x*2}
function R(n,ok){console.log('quadro: '+(ok?'ok ':'FALHA ')+n)}
R('parent/top/frameElement/name',parent!==window&&top===parent&&frameElement&&frameElement.id=='f'&&window.name=='filhinho');
window.addEventListener('message',function(e){if(e.data&&e.data.resp)R('resposta do pai clonada',Array.isArray(e.data.resp)&&e.data.resp instanceof Array&&e.data.resp[2].x=='y')});
var ch=new MessageChannel();ch.port1.onmessage=function(e){R('MessageChannel entre janelas',e.data=='pela porta')};
parent.postMessage({oi:1},'*');parent.postMessage('porta','*',[ch.port2]);
</script></body></html>"""

ESPERADOS = 13


def main():
    pasta = os.path.join(SAIDA, "h55-www")
    os.makedirs(pasta, exist_ok=True)
    for nome, txt in (("index.html", PAI), ("filho.html", FILHO)):
        with open(os.path.join(pasta, nome), "w", encoding="utf-8") as f:
            f.write(txt)
    tratador = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    tratador.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), tratador)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h55", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "Quadros"', 120)
        time.sleep(4)   # mensagens e porta: timers depois do load
        s = m.serial()
        res = re.findall(r"quadro: (ok|FALHA) ([^\n]*)", s)
        for r, n in res:
            print(r, n)
        verificar(len(res) == ESPERADOS, f"{ESPERADOS} verificações rodaram ({len(res)})")
        for r, n in res:
            verificar(r == "ok", f"quadro: {n}")
        jan = re.search(r'criada por \d+: "Cip.*?" (\d+)x(\d+) em \((\d+),(\d+)\)', s)
        jl, ja, jx, jy = (int(x) for x in jan.groups())
        ox, oy = jx + 2, jy + 24 + 40   # canto da área da página
        im = Image.open(m.captura("h55", guardar=False)).convert("RGB")
        azul = sum(1 for x in range(ox + 20, ox + 340, 4) for y in range(oy + 20, oy + 160, 4) if im.getpixel((x, y)) == (207, 232, 255))
        print("pixels do fundo do quadro", azul)
        verificar(azul > 300, "o conteúdo do iframe aparece pintado na caixa dele")
        # clique no botão verde do filho (margem 20 do iframe + 10 do div)
        m.clique(ox + 20 + 60, oy + 20 + 22)
        m.esperar_serial("quadro: clique chegou ao pai", 30)
        time.sleep(3)
        verificar("clique no quadro" in m.serial(), "o clique foi para dentro do quadro")
        im = Image.open(m.captura("h55-clique", guardar=False)).convert("RGB")
        verde = sum(1 for x in range(ox + 30, ox + 250, 3) for y in range(oy + 30, oy + 60, 3) if im.getpixel((x, y)) == (0x22, 0xaa, 0x77))
        verificar(verde > 50, "o quadro foi repintado depois do clique")


if __name__ == "__main__":
    main()
