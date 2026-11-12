#!/usr/bin/env python3
"""H52: interação com a página no Cipó, como em qualquer navegador.

- a roda do mouse rola a página (IntelliMouse, pacote de 4 bytes);
- o polegar da barra de rolagem é arrastado;
- clicar numa caixa de texto põe o foco nela e a digitação aparece dentro
  da própria caixa (sem janela à parte); Enter envia o formulário com todos
  os campos; o evento input chega ao JavaScript;
- elemento com clique do JavaScript continua sendo o próprio elemento
  (nada de "[ok]" no lugar de um input).
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

HTML = """<!DOCTYPE html><html><head><meta charset="utf-8"><title>Interação</title><style>
body{margin:0;font:16px sans-serif}
#q{position:absolute;left:40px;top:40px;width:300px;height:28px;font-size:18px;border:1px solid #888}
#b{position:absolute;left:360px;top:40px;width:120px;height:30px}
#eco{position:absolute;left:40px;top:90px;width:400px;height:24px}
.faixa{height:400px}
</style></head><body>
<form action="/busca"><input type="hidden" name="x" value="1"><input id="q" name="q" placeholder="Pesquisar">
<input id="b" type="button" value="Sorte"></form>
<div id="eco">nada</div>
<div style="height:120px"></div>
<div class="faixa" style="background:#ff0000"></div><div class="faixa" style="background:#00ff00"></div>
<div class="faixa" style="background:#0000ff"></div><div class="faixa" style="background:#ffff00"></div>
<div class="faixa" style="background:#ff00ff"></div>
<script>
document.getElementById('q').addEventListener('input',function(e){document.getElementById('eco').textContent='eco:'+this.value});
document.getElementById('b').onclick=function(){document.getElementById('eco').textContent='sorte'};
</script></body></html>"""

pedidos = []


class Tratador(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        pedidos.append(self.path)
        if self.path.startswith("/busca"):
            corpo = b"<title>Resultado</title><p>ok</p>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(corpo)))
            self.end_headers()
            self.wfile.write(corpo)
            return
        super().do_GET()


def main():
    pasta = os.path.join(SAIDA, "h52-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w", encoding="utf-8") as f:
        f.write(HTML)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Tratador, directory=pasta))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h52", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "Interação"', 120)
        s = m.serial()
        verificar(", com roda" in s, "mouse PS/2 com roda (IntelliMouse, ID 3)")
        jan = re.search(r'criada por \d+: "Cip.*?" (\d+)x(\d+) em \((\d+),(\d+)\)', s)
        jl, ja, jx, jy = (int(x) for x in jan.groups())
        # área de conteúdo: moldura de 2 px e barra de título de 24 px; barra do Cipó com 40 px
        ox, oy = jx + 2, jy + 24 + 40
        verificar("[ok]" not in s and "[Sorte]" not in s, "input com clique do JS segue como input")
        png = m.captura("h52-inicio", guardar=False)
        im = Image.open(png).convert("RGB")
        cinza = sum(1 for x in range(ox + 300, ox + 480, 2) for y in range(oy + 40, oy + 70, 2) if im.getpixel((x, y))[0] < 230)
        verificar(cinza > 30, "botão Sorte desenhado como botão (borda e texto)")

        # roda: três degraus para baixo (no monitor do QEMU, dz negativo = para baixo)
        m.mouse_para(ox + 500, oy + 300)
        for _ in range(3):
            m.monitor("mouse_move 0 0 -1")
            time.sleep(0.15)
        time.sleep(0.8)
        rol = [int(x) for x in re.findall(r"cipo: rolagem (\d+)", m.serial())]
        print("rolagens", rol)
        verificar(rol and rol[-1] == 300, "roda: 3 degraus = 300 px")
        m.monitor("mouse_move 0 0 1")
        time.sleep(0.8)
        rol = [int(x) for x in re.findall(r"cipo: rolagem (\d+)", m.serial())]
        verificar(rol[-1] == 200, "roda para cima volta 100 px")

        # arrastar o polegar da barra (à direita) para baixo
        bx = jx + 2 + jl - 7
        im = Image.open(m.captura("h52-barra", guardar=False)).convert("RGB")
        pol = [y for y in range(oy, jy + ja - 30) if im.getpixel((bx, y)) == (0x8A, 0x93, 0x9C)]
        verificar(len(pol) > 10, "polegar da barra visível")
        n0 = len(re.findall(r"cipo: rolagem", m.serial()))
        m.arrastar(bx, pol[0] + 5, bx, pol[0] + 105)
        time.sleep(0.8)
        rol = [int(x) for x in re.findall(r"cipo: rolagem (\d+)", m.serial())]
        print("depois de arrastar", rol[-5:])
        verificar(len(rol) > n0 + 2 and rol[-1] > 400, "arrastar o polegar rola a página (vários passos)")

        m.tecla("home")
        time.sleep(0.8)
        # clique no input do JS: o eco muda sem "[Sorte]"
        m.clique(ox + 400, oy + 55)
        m.esperar_serial("cipo: clique js", 20)
        # foco na caixa e digitação dentro dela
        m.clique(ox + 100, oy + 54)
        m.esperar_serial("cipo: foco no campo", 20)
        verificar("Digite o texto" not in m.tela_texto() if hasattr(m, "tela_texto") else True, "sem janela de diálogo")
        m.digitar("sagui ok", 0.15)
        time.sleep(1.5)
        im = Image.open(m.captura("h52-digitado", guardar=False)).convert("RGB")
        tinta = sum(1 for x in range(ox + 42, ox + 330) for y in range(oy + 44, oy + 66) if sum(im.getpixel((x, y))) < 200)
        print("tinta na caixa", tinta)
        verificar(tinta > 60, "texto digitado aparece dentro da caixa")
        eco = sum(1 for x in range(ox + 40, ox + 300) for y in range(oy + 90, oy + 112) if sum(im.getpixel((x, y))) < 200)
        print("tinta no eco", eco)
        verificar(eco > 80, "evento input chegou ao JavaScript e o DOM mudou")
        m.tecla("ret")
        m.esperar_serial('cipo: pronto "Resultado"', 30)
        busca = [p for p in pedidos if p.startswith("/busca")]
        print("pedidos", busca)
        verificar(busca and busca[-1] == "/busca?x=1&q=sagui+ok", "Enter envia o formulário com todos os campos")


if __name__ == "__main__":
    main()
