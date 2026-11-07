"""Mede o motor JS do Cipó rodando o base.js do player do YouTube.
Serve uma página local que carrega o base.js direto do YouTube e registra
no serial até onde chegou. Uso (no X79): python3 tools/video/yt_base.py URL_DO_BASE_JS"""
import functools, http.server, os, re, sys, threading, time
sys.path.insert(0, 'tests')
from qemu import Maquina, imagem_de_teste, SAIDA

base = sys.argv[1]
pasta = os.path.join(SAIDA, 'ytbase-www'); os.makedirs(pasta, exist_ok=True)
open(os.path.join(pasta, 'index.html'), 'w').write(f'''<html><head><title>yt base</title>
<script>console.log("yt: antes "+Date.now())</script>
<script src="{base}"></script>
<script>var p=window._yt_player; console.log("yt: depois "+Date.now()+" tipo "+typeof p+" chaves "+(p?Object.keys(p).length:0));</script>
</head><body>x</body></html>''')
tr = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta); tr.log_message = lambda *a: None
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), tr); threading.Thread(target=srv.serve_forever, daemon=True).start()
ex = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
with Maquina("ytbase", "ide", imagem=imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"]), extras=ex) as m:
    try: m.esperar_serial('cipo: pronto', 240)
    except Exception: print('SEM PRONTO')
    s = m.serial()
for l in s.splitlines():
    if re.search(r'js:|yt:|cipo: js|erro|Erro', l): print(l[:300])
