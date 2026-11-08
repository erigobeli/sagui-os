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
.rod a{color:#fff;font-size:18px}
.gruda{position:sticky;top:0;height:30px;background:#ff8000;z-index:5}
</style></head><body>
<h1>Cipó desenha fontes da web</h1>
<p>Parágrafo proporcional: iiiii WWWWW, ação.</p>
<p style="font-size:40px;color:#0000c8;margin:0">Ωμέγα Жук → ✓ &#x416;&#x3A9;</p>
<p style="font-size:40px;color:#00c8c8;margin:0;font-family:monospace">ЖЖ ΩΩ</p>
<div class="caixa"><div class="a"></div><div class="v"></div><div style="position:absolute;left:60px;top:40px;width:80px;height:80px;background:#ff00ff;transform:rotate(45deg)"></div></div>
<div style="height:2000px"><div class="gruda">grudado</div>conteudo longo</div>
<div class="rod"><a href="/alvo.html">ir para o alvo</a></div>
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
        azul_texto = conta(lambda c: c[2] > 150 and c[0] < 40 and c[1] < 40, 100, 220)
        ciano = conta(lambda c: c[1] > 190 and c[2] > 190 and c[1] - c[0] > 40, 100, 260)
        print("utf8: azul", azul_texto, "ciano(caixas)", ciano)
        verificar(azul_texto > 300, "grego, cirílico e setas desenhados pela fonte da web (UTF-8 além do Latin-1)")
        verificar(ciano > 20, "sem fonte da web: caixa no lugar do caractere")
        verificar(verm > 400 and azul > 100, "caixa centralizada por transform e a de baixo aparecendo")
        mag = [(x, y) for y in range(jy + 40, jy + ja) for x in range(jx, jx + 300) if im.getpixel((x, y)) == (255, 0, 255)]
        if mag:
            ys = [p[1] for p in mag]; xs = [p[0] for p in mag]
            alt, larg = max(ys) - min(ys) + 1, max(xs) - min(xs) + 1
            meio = (min(ys) + max(ys)) // 2
            larg_meio = len([p for p in mag if p[1] == meio])
            topo = len([p for p in mag if p[1] == min(ys) + 3])
        else:
            alt = larg = larg_meio = topo = 0
        print("rotate: alt", alt, "larg", larg, "meio", larg_meio, "topo", topo, "area", len(mag))
        verificar(105 <= alt <= 118 and 105 <= larg <= 118 and larg_meio > 100 and topo < 12, "rotate(45deg) vira losango (diagonal ~113 px)")
        verificar(verde > 100, "position:fixed no pé da janela")
        # rola: o rodapé fixo fica no pé e o cabeçalho sticky prende no topo
        for _ in range(4):
            m.tecla("pgdn")
        import time
        time.sleep(1.5)
        png = m.captura("H48-rolado")
        im = Image.open(png).convert("RGB")
        verde2 = conta(lambda c: c[1] > 130 and c[0] < 60 and c[2] < 60, ja - 120, ja)
        laranja = conta(lambda c: c[0] > 220 and 100 < c[1] < 160 and c[2] < 40, 60, 140)
        verm2 = conta(lambda c: c[0] > 180 and c[1] < 60 and c[2] < 60, 40, ja)
        print("rolado: verde", verde2, "laranja", laranja, "vermelho", verm2)
        verificar(verm2 < 50, "a página rolou (a caixa vermelha saiu)")
        verificar(verde2 > 100, "fixed continua no pé depois de rolar")
        verificar(laranja > 300, "sticky preso no topo depois de rolar")
        # clique no link do rodapé fixo, já rolado
        ys = [y for y in range(jy + ja - 120, min(jy + ja, im.size[1])) if im.getpixel((jx + 100, y))[1] > 130 and im.getpixel((jx + 100, y))[0] < 60]
        m.clique(jx + 40, (ys[0] + ys[-1]) // 2)
        m.esperar_serial("alvo.html", 15)
        verificar(True, "clique no link do rodapé fixo depois de rolar")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
