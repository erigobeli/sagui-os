"""Mede o motor JS do Cipó rodando o base.js do player do YouTube e procura,
entre as funções do escopo do player, as que transformam o parâmetro n.
Uso (no X79): python3 tools/video/yt_base.py URL_DO_BASE_JS [valor_de_n]"""
import functools, http.server, os, re, sys, threading
sys.path.insert(0, 'tests')
from qemu import Maquina, imagem_de_teste, SAIDA

base = sys.argv[1]
nval = sys.argv[2] if len(sys.argv) > 2 else 'j7xSd1VOPgQtI6MbPr'
pular = os.environ.get('PULAR', 'op')
PAGINA = r'''<html><head><title>yt base</title>
<script>console.log("yt: antes "+Date.now())</script>
<script src="BASE"></script>
<script>var p=window._yt_player; console.log("yt: depois "+Date.now()+" tipo "+typeof p+" chaves "+(p?Object.keys(p).length:0));
var f=null,ks=Object.keys(p);for(var i=0;i<ks.length&&!f;i++)if(typeof p[ks[i]]=="function"&&__escopo(p[ks[i]]).length>100)f=p[ks[i]];
var fs=f?__escopo(f):[];for(var q=0;q<ks.length;q++)if(typeof p[ks[q]]=="function")fs.push(["exp."+ks[q],p[ks[q]],1,0]);console.log("yt: escopo "+fs.length);
var PULAR=",PULARV,";var n0="NVALOR",c=[],t0=Date.now(),tent=0,hist={};for(var i=0;i<fs.length;i++){var k=fs[i][2]+":"+(fs[i][3]>2000?"2000+":fs[i][3]>300?"300+":"p");hist[k]=(hist[k]||0)+1}console.log("yt: hist "+JSON.stringify(hist));
for(var i=0;i<fs.length;i++){if(fs[i][2]>2||PULAR.indexOf(","+fs[i][0]+",")>=0)continue;tent++;if(tent%50==0||tent>0)__log("c "+fs[i][0]+" "+fs[i][3]);var t=__tentar(fs[i][1],n0,150);var r=t[1];if(t[0]&&typeof r=="string"&&r!==n0&&/^[\w-]{8,90}$/.test(r)){var t2=__tentar(fs[i][1],n0,150);if(t2[0]&&t2[1]===r)c.push(fs[i][0]+"="+r)}}
console.log("yt: tentou "+tent+" em "+(Date.now()-t0)+" ms; candidatas "+c.length+" "+c.slice(0,60).join(" "));</script>
</head><body>x</body></html>'''
pasta = os.path.join(SAIDA, 'ytbase-www'); os.makedirs(pasta, exist_ok=True)
open(os.path.join(pasta, 'index.html'), 'w').write(PAGINA.replace('BASE', base).replace('NVALOR', nval).replace('PULARV', pular))
tr = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta); tr.log_message = lambda *a: None
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), tr); threading.Thread(target=srv.serve_forever, daemon=True).start()
ex = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
with Maquina("ytbase", "ide", imagem=imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"]), extras=ex) as m:
    try: m.esperar_serial('cipo: pronto', 900)
    except Exception: print('SEM PRONTO')
    s = m.serial()
for l in s.splitlines():
    if re.search(r'js:|yt:|cipo: js|Falta', l): print(l[:600])
