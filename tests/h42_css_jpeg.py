#!/usr/bin/env python3
"""
H42: folhas de estilo externas e JPEG progressivo no Cipó.

Critério: o Cipó baixa um .css por <link>, aplica seletor composto, descendente,
peso e @media; o JPEG progressivo (com subamostragem) sai com as cores certas;
uma página que o CSS esconde inteira continua legível.
"""
import functools
import http.server
import os
import re
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image  # noqa: E402
from qemu import Maquina, verificar, imagem_de_teste, cor_do_pixel, SAIDA  # noqa: E402

HTML = """<html><head><title>Estilos</title><link rel="stylesheet" href="s.css">
<link rel="stylesheet" media="print" href="nao.css"></head><body>
<div class="caixa"><p class="alvo">some-descendente</p></div>
<p class="alvo">fica-fora</p>
<p class="a b">some-composto</p>
<p class="a">fica-composto</p>
<p class="tela">some-media</p>
<p class="impresso">fica-impresso</p>
<p id="volta" class="fora">fica-peso</p>
<p class="dobra">fica-ordem</p>
<a class="botao:hover">fica-pseudo</a>
<img src="p.jpg" alt="progressivo"><img src="w.webp" alt="webp"><img src="g.gif" alt="gif"></body></html>"""

CSS = """/* comentário */
.caixa .alvo { display: none }
p.a.b { display: none }
@media screen and (min-width: 600px) { .tela { display: none } }
@media print { .impresso { display: none } }
@media (min-width: 2000px) { .impresso { display: none } }
#volta { display: block }
.fora { display: none }
.dobra { display: none }
.dobra { display: block }
a:hover { display: none }
"""

ESCONDIDA = """<html><head><title>Oculta</title><link rel="stylesheet" href="t.css"></head>
<body><div class="app"><p>texto-revelado</p><p>outra linha</p><p>mais uma</p></div></body></html>"""


CAIXAS = """<html><head><title>Caixas</title><style>
:root { --vermelho: #c81e28 }
body { background: #f0e6c8 }
.linha { display: flex; gap: 0 }
.a { background: var(--vermelho); color: white; padding: 18px; width: 50% }
.b { background-color: rgb(20, 60, 200); color: white; padding: 18px; width: 50% }
</style></head><body><div class="linha"><div class="a">esquerda</div><div class="b">direita</div></div>
<table><tr><td>um</td><td>dois</td></tr></table></body></html>"""


def main():
    pasta = os.path.join(SAIDA, "h42-www")
    os.makedirs(pasta, exist_ok=True)
    open(os.path.join(pasta, "index.html"), "w").write(HTML)
    open(os.path.join(pasta, "sem.html"), "w").write(HTML.replace('href="s.css"', 'href="vazia.css"'))
    open(os.path.join(pasta, "vazia.css"), "w").write("")
    open(os.path.join(pasta, "caixas.html"), "w").write(CAIXAS)
    open(os.path.join(pasta, "app.html"), "w").write("<html><head><title>App</title></head><body><div>Carregando</div><script>var a='" + "x" * 80000 + "';</script></body></html>")
    open(os.path.join(pasta, "oculta.html"), "w").write(ESCONDIDA)
    open(os.path.join(pasta, "s.css"), "w").write(CSS)
    open(os.path.join(pasta, "t.css"), "w").write(".app { display: none }")
    open(os.path.join(pasta, "nao.css"), "w").write("p { display: none }")
    foto = Image.new("RGB", (161, 83), (20, 60, 200))
    foto.paste((210, 30, 30), (0, 0, 80, 83))
    foto.save(os.path.join(pasta, "p.jpg"), quality=90, progressive=True, subsampling=2)
    Image.new("RGB", (150, 70), (30, 160, 60)).save(os.path.join(pasta, "w.webp"), quality=80)
    Image.new("RGB", (140, 66), (230, 200, 20)).save(os.path.join(pasta, "g.gif"))
    pedidos = []

    class Trat(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pedidos.append(self.path)

    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Trat, directory=pasta))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    porta = srv.server_address[1]
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]

    with Maquina("h42", "ide", imagem=imagem_de_teste(f"/bin/cipo http://10.0.2.2:{porta}/", ["--vesa"]), extras=extras) as m:
        m.esperar_serial('cipo: pronto "Estilos"', 120)
        time.sleep(1)
        s = m.serial()
        verificar("/s.css" in pedidos, "baixou a folha externa")
        verificar("/nao.css" not in pedidos, "pulou a folha de impressão")
        verificar("cipo: imagem 161x83" in s, "JPEG progressivo decodificado")
        verificar("cipo: imagem 150x70" in s, "WebP decodificado")
        verificar("cipo: imagem 140x66" in s, "GIF decodificado")
        linhas = int(re.search(r'pronto "Estilos" \((\d+) linhas', s).group(1))
        png = m.captura("H42-cipo-css-jpeg")
        jan = re.search(r'criada por \d+: "Cip.*?" \d+x\d+ em \((\d+),(\d+)\)', s)
        jx, jy = int(jan.group(1)), int(jan.group(2))
        cores_e = [cor_do_pixel(png, jx + 2 + 12 + 30, y) for y in range(jy + 70, jy + 500, 4)]
        cores_d = [cor_do_pixel(png, jx + 2 + 12 + 130, y) for y in range(jy + 70, jy + 500, 4)]
        verificar(any(c[0] > 170 and c[1] < 70 and c[2] < 70 for c in cores_e), "vermelho da foto na tela")
        verificar(any(c[2] > 160 and c[0] < 60 and c[1] < 100 for c in cores_d), "azul da foto na tela")
        print("linhas:", linhas)

    # A mesma página sem a folha: a diferença de linhas são os 3 parágrafos escondidos.
    with Maquina("h42-c", "ide", imagem=imagem_de_teste(f"/bin/cipo http://10.0.2.2:{porta}/sem.html", ["--vesa"]), extras=extras) as m:
        m.esperar_serial('cipo: pronto "Estilos"', 120)
        sem = int(re.search(r'pronto "Estilos" \((\d+) linhas', m.serial()).group(1))
    print("sem folha:", sem, "com folha:", linhas)
    verificar(sem - linhas == 6, f"exatamente 3 parágrafos somem ({sem} para {linhas} linhas)")

    with Maquina("h42-l", "ide", imagem=imagem_de_teste(f"/bin/cipo http://10.0.2.2:{porta}/caixas.html", ["--vesa"]), extras=extras) as m:
        m.esperar_serial('cipo: pronto "Caixas"', 120)
        time.sleep(1)
        s = m.serial()
        png = m.captura("H42-cipo-caixas")
        jan = re.search(r'criada por \d+: "Cip.*?" \d+x\d+ em \((\d+),(\d+)\)', s)
        jx, jy = int(jan.group(1)), int(jan.group(2))
        y = jy + 90
        linha = [cor_do_pixel(png, x, y)[:3] for x in range(jx + 20, jx + 800, 10)]
        verificar(any(c == (200, 30, 40) for c in linha), "coluna vermelha do flex")
        verificar(any(c == (20, 60, 200) for c in linha), "coluna azul ao lado, na mesma altura")
        fundo = cor_do_pixel(png, jx + 8, jy + 300)[:3]
        verificar(fundo == (240, 230, 200), f"fundo do body pintado ({fundo})")
    with Maquina("h42-a", "ide", imagem=imagem_de_teste(f"/bin/cipo http://10.0.2.2:{porta}/app.html", ["--vesa"]), extras=extras) as m:
        m.esperar_serial('cipo: pronto "App"', 120)
        verificar("pagina so com JavaScript" in m.serial(), "casca de aplicativo mostra a explicação, não tela branca")

    with Maquina("h42-o", "ide", imagem=imagem_de_teste(f"/bin/cipo http://10.0.2.2:{porta}/oculta.html", ["--vesa"]), extras=extras) as m:
        m.esperar_serial('cipo: pronto "Oculta"', 120)
        s = m.serial()
        verificar("css escondeu a pagina" in s, "percebeu a página escondida pelo CSS")
        n = int(re.search(r'pronto "Oculta" \((\d+) linhas', s).group(1))
        verificar(n >= 3, f"página escondida fica legível ({n} linhas)")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
