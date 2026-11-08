#!/usr/bin/env python3
"""H48: fontes da web (WOFF2/WOFF), transform, z-index e position fixed no Cipó."""
import functools
import http.server
import os
import re
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402
from PIL import Image  # noqa: E402

HTML = """<html><head><meta charset="utf-8"><title>Fontes</title><style>
@font-face{font-family:"Liberdade";src:url(fo/serif.woff2) format("woff2")}
@font-face{font-family:Deja;src:url(fo/sans.woff) format("woff");unicode-range:U+0000-00FF}
h1{font:34px "Liberdade",serif;margin:4px}
p{font-family:Deja,sans-serif;font-size:20px;margin:4px}
.caixa{position:relative;height:160px}
.v{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);width:160px;height:80px;background:#e00000;z-index:3}
.a{position:absolute;left:380px;top:50px;width:160px;height:80px;background:#0000e0;z-index:1}
.rod{position:fixed;left:0;bottom:0;width:100%;height:24px;background:#00a000}
</style></head><body>
<h1>Cipó desenha fontes da web</h1>
<p>Parágrafo proporcional: iiiii WWWWW, ação.</p>
<div class="caixa"><div class="a"></div><div class="v"></div></div>
<div style="height:2000px"></div>
<div class="rod"></div>
</body></html>"""


def main():
    from fontTools.ttLib import TTFont
    pasta = os.path.join(SAIDA, "h48-www")
    os.makedirs(os.path.join(pasta, "fo"), exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w") as f:
        f.write(HTML)
    for orig, dest, fl in (("/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf", "serif.woff2", "woff2"),
                           ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "sans.woff", "woff")):
        ft = TTFont(orig)
        ft.flavor = fl
        ft.save(os.path.join(pasta, "fo", dest))
    trat = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    trat.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), trat)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h48", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "Fontes"', 90)
        s = m.serial()
        verificar("cipo: fonte Liberdade ok" in s, "WOFF2 baixada e aberta (Brotli + glyf transformado)")
        verificar("cipo: fonte Deja ok" in s, "WOFF baixada e aberta (zlib)")
        png = m.captura("H48-fontes")
        jan = re.search(r'criada por \d+: "Cip.*?" (\d+)x(\d+) em \((\d+),(\d+)\)', s)
        jl, ja, jx, jy = (int(x) for x in jan.groups())
        im = Image.open(png).convert("RGB")
        def conta(cond, y0, y1):
            return sum(1 for y in range(jy + y0, min(jy + y1, im.size[1]), 2) for x in range(jx + 4, jx + jl - 20, 2) if cond(im.getpixel((x, y))))
        cinza = conta(lambda c: 30 < c[0] < 220 and abs(c[0] - c[1]) < 12 and abs(c[1] - c[2]) < 12, 40, 140)
        verm = conta(lambda c: c[0] > 180 and c[1] < 60 and c[2] < 60, 40, ja)
        azul = conta(lambda c: c[2] > 180 and c[0] < 60 and c[1] < 60, 40, ja)
        verde = conta(lambda c: c[1] > 130 and c[0] < 60 and c[2] < 60, ja - 120, ja)
        print("cinza", cinza, "vermelho", verm, "azul", azul, "verde", verde)
        verificar(cinza > 150, "texto com antialias (tons de cinza)")
        verificar(verm > 400 and azul > 100, "caixa centralizada por transform e a de baixo aparecendo")
        verificar(verde > 100, "position:fixed no pé da janela")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
