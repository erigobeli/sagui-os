#!/usr/bin/env python3
"""H56: Proxy, freeze, BigInt64Array, DataView e o layout do widget de checkbox.

JavaScript: Proxy com armadilhas (get/set/has/deleteProperty/ownKeys/apply/
construct, revocable), Object.freeze/seal/preventExtensions de verdade,
BigInt64Array/BigUint64Array, DataView com floats e BigInt, crypto com 32
bits, e mensagens de erro limpas (sem trecho do fonte grudado).
Layout (medido contra o Chrome com o mesmo HTML): display:table em <div> sem
espaço entre células, height:100% em cadeia, table-cell centralizando,
`* html .x` sem casar com o documento, e rádio redondo.
"""
import functools
import http.server
import os
import re
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

HTML = r"""<!DOCTYPE html><html><head><meta charset="utf-8"><title>Proxy</title>
<style>body{margin:0}.ib{display:inline-block;height:100%}.tb{display:table;height:100%}.tc{display:table-cell;vertical-align:middle}
.g{position:relative;display:-moz-inline-box;display:inline-block}* html .g{display:inline}:first-child+html .g{display:inline}
.cx{width:28px;height:28px;margin:4px;vertical-align:text-bottom}</style></head><body>
<div id="c" style="height:74px;width:206px"><div class="ib"><div class="tb"><div class="tc" style="padding-left:12px"><span id="k" class="g cx"></span></div></div></div><div class="ib"><div class="tb"><label id="l" class="tc" style="width:152px">I'm not a robot</label></div></div></div>
<script>
function R(n,ok,x){console.log('px: '+(ok?'ok ':'FALHA ')+n+(x!==undefined?' '+x:''))}
try{
 var alvo={a:1,b:2},p=new Proxy(alvo,{get:function(t,k){return k in t?t[k]:'padrao'},set:function(t,k,v){t[k]=v*10;return true},has:function(t,k){return k!=='b'},ownKeys:function(t){return Object.keys(t).concat(['z'])}});
 p.c=5;R('Proxy get/set/has/ownKeys',p.a===1&&p.x==='padrao'&&alvo.c===50&&!('b' in p)&&Object.keys(p).join()=='a,b,c'&&Reflect.ownKeys(p).join()=='a,b,c,z');
 var f=new Proxy(function(x){return x+1},{apply:function(t,th,a){return t(a[0])*2}});function K(v){this.v=v}var C=new Proxy(K,{construct:function(t,a){return new t(a[0]+1)}});
 R('Proxy apply/construct',f(3)===8&&typeof f=='function'&&new C(1).v===2&&Array.isArray(new Proxy([],{})));
 var rv=Proxy.revocable({},{});rv.revoke();var e=false;try{rv.proxy.x}catch(x){e=x instanceof TypeError}R('Proxy revocable',e);
 var o=Object.freeze({a:1});o.a=2;o.b=3;var s=Object.seal({a:1});s.a=2;s.b=3;delete s.a;
 R('freeze/seal',o.a===1&&o.b===undefined&&Object.isFrozen(o)&&s.a===2&&!('b' in s)&&Object.isSealed(s));
 var b=new BigInt64Array(2);b[0]=-5n;b[1]=2n**63n;R('BigInt64Array',b[0]===-5n&&b[1]===-(2n**63n)&&new BigUint64Array(b.buffer)[0]===18446744073709551611n);
 var d=new DataView(new ArrayBuffer(16));d.setFloat64(0,Math.PI);d.setFloat32(8,1.5,true);R('DataView floats',d.getFloat64(0)===Math.PI&&d.getFloat32(8,true)===1.5);
 var r=crypto.getRandomValues(new Uint32Array(64)),m=0;for(var i=0;i<64;i++)m=Math.max(m,r[i]);R('crypto 32 bits',m>65536);
 var msg='';try{null.x}catch(x){msg=x.message}var msg2='';try{({}).naoExiste()}catch(x){msg2=x.message}
 R('mensagens limpas',msg.indexOf('[')<0&&/naoExiste is not a function$/.test(msg2),msg2);
 var c=document.getElementById('c').getBoundingClientRect(),k=document.getElementById('k').getBoundingClientRect(),l=document.getElementById('l').getBoundingClientRect();
 R('layout caixa centralizada',k.x-c.x==16&&k.y-c.y==23&&k.width==28,[k.x-c.x,k.y-c.y,k.width].join());
 R('layout label ao lado',l.x-c.x==48&&l.y==c.y&&l.height==74,[l.x-c.x,l.y-c.y,l.height].join());
}catch(x){R('excecao',false,x.message)}
</script></body></html>"""

ESPERADOS = 10


def main():
    pasta = os.path.join(SAIDA, "h56-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w", encoding="utf-8") as f:
        f.write(HTML)
    tratador = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    tratador.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), tratador)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h56", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "Proxy"', 120)
        s = m.serial()
        res = re.findall(r"px: (ok|FALHA) ([^\n]*)", s)
        for r, n in res:
            print(r, n)
        verificar(len(res) == ESPERADOS, f"{ESPERADOS} verificações rodaram ({len(res)})")
        for r, n in res:
            verificar(r == "ok", f"px: {n}")


if __name__ == "__main__":
    main()
