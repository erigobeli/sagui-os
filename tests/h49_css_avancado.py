#!/usr/bin/env python3
"""H49: contextos de empilhamento, mask-image, SVG externo, data: e fonte CFF (OpenType) no Cipó."""
import functools
import http.server
import os
import re
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402
from PIL import Image  # noqa: E402

TRI = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath d='M12 2 L22 22 L2 22 Z'/%3E%3C/svg%3E"
CIRC = "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMCAxMCI+PGNpcmNsZSBjeD0iNSIgY3k9IjUiIHI9IjQiIGZpbGw9IiMwMGEwMDAiLz48L3N2Zz4="
HTML = """<html><head><meta charset="utf-8"><title>Avancado</title><style>
@font-face{font-family:Livro;src:url(livro.woff2) format("woff2")}
body{margin:0}
h1{font:30px Livro,serif;margin:0 0 0 8px;height:40px}
.ctx{position:relative;z-index:1;width:300px;height:140px;background:#cccccc}
.alto{position:absolute;left:20px;top:20px;width:150px;height:100px;background:#ff0000;z-index:100}
.cobre{position:absolute;left:100px;top:60px;width:150px;height:80px;background:#0000ff;z-index:2}
.ic{position:absolute;left:340px;top:50px;width:64px;height:64px;background-color:#d00000;-webkit-mask:url("TRI") no-repeat center/contain}
.ic2{position:absolute;left:420px;top:50px;width:64px;height:64px;background-color:#ff8000;mask-image:url(bola.svg)}
img{position:absolute;top:50px}
</style></head><body>
<h1>Livro OpenType CFF</h1>
<div class="ctx"><div class="alto"></div></div>
<div class="cobre"></div>
<span class="ic"></span><span class="ic2"></span>
<img src="bola.svg" style="left:500px" width="64" height="64">
<img src="data:image/svg+xml;base64,CIRC" style="left:580px" width="64" height="64">
</body></html>""".replace("TRI", TRI).replace("CIRC", CIRC)
BOLA = '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><circle cx="12" cy="12" r="11" fill="#8000ff"/></svg>'


def main():
    from fontTools.ttLib import TTFont
    pasta = os.path.join(SAIDA, "h49-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w") as f:
        f.write(HTML)
    with open(os.path.join(pasta, "bola.svg"), "w") as f:
        f.write(BOLA)
    ft = TTFont("/usr/share/fonts/opentype/urw-base35/URWBookman-Light.otf")
    ft.flavor = "woff2"
    ft.save(os.path.join(pasta, "livro.woff2"))
    trat = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    trat.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), trat)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h49", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "Avancado"', 90)
        s = m.serial()
        verificar("cipo: fonte Livro ok" in s, "fonte OpenType CFF em WOFF2 aberta")
        png = m.captura("H49-css-avancado")
        jan = re.search(r'criada por \d+: "Cip.*?" (\d+)x(\d+) em \((\d+),(\d+)\)', s)
        jl, ja, jx, jy = (int(x) for x in jan.groups())
        topo = jy + 62   # área da página (barra do endereço acima)
        im = Image.open(png).convert("RGB")
        def px(x, y):
            return im.getpixel((jx + 2 + x, topo + y))
        def conta(cond, x0, y0, x1, y1):
            return sum(1 for y in range(y0, y1, 2) for x in range(x0, x1, 2) if cond(px(x, y)))
        # contexto z1 com filho z100 fica abaixo do irmão z2
        azul_sobre = conta(lambda c: c[2] > 200 and c[0] < 50 and c[1] < 50, 100, 100, 170, 140)
        verm_livre = conta(lambda c: c[0] > 200 and c[1] < 50 and c[2] < 50, 20, 60, 95, 95)
        print("azul sobre vermelho", azul_sobre, "vermelho livre", verm_livre)
        verificar(azul_sobre > 300 and verm_livre > 300, "contexto de empilhamento: filho z100 de pai z1 fica sob irmão z2")
        tinta = conta(lambda c: c[0] < 120 and c[1] < 120 and c[2] < 120, 0, 0, 400, 40)
        print("tinta do título", tinta)
        verificar(tinta > 150, "título desenhado com a fonte CFF")
        tri = [(x, y) for y in range(45, 120) for x in range(340, 404) if (lambda c: c[0] > 180 and c[1] < 60 and c[2] < 60)(px(x, y))]
        if tri:
            ys = [p[1] for p in tri]
            l_topo = len([p for p in tri if p[1] == min(ys) + 4]); l_base = len([p for p in tri if p[1] == max(ys) - 4])
        else:
            l_topo = l_base = 0
        print("triângulo: topo", l_topo, "base", l_base)
        verificar(l_base > 3 * max(l_topo, 1) and l_base > 30, "mask com SVG data: desenha o triângulo")
        lar = conta(lambda c: c[0] > 230 and 100 < c[1] < 160 and c[2] < 40, 420, 52, 484, 112)
        rox = conta(lambda c: c[0] > 100 and c[1] < 40 and c[2] > 220, 500, 52, 564, 112)
        ver = conta(lambda c: c[1] > 130 and c[0] < 40 and c[2] < 40, 580, 52, 644, 112)
        print("mask svg externo", lar, "img svg", rox, "img data base64", ver)
        verificar(lar > 300, "mask-image com SVG externo")
        verificar(rox > 300, "<img> com SVG externo")
        verificar(ver > 300, "<img> com SVG data: base64")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
