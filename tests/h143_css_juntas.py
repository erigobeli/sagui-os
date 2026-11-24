#!/usr/bin/env python3
"""H143 - Cipó: as folhas de estilo da página vão juntas.

Uma página com 6 folhas de estilo no mesmo servidor (cada uma pinta uma
faixa de uma cor) e uma de outro endereço que não existe.

Critérios:
  - as 6 folhas chegam pedidas juntas (pipelining: pelo menos 3 pedidos
    chegam com outro já esperando) e numa conexão só;
  - cada folha é pedida uma vez (o motor usa a que já chegou, não pede de
    novo);
  - as 6 faixas aparecem com as cores das folhas;
  - a folha que não existe (404) não atrapalha.
"""
import http.server
import os
import socketserver
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402
from h62_navegador import BARRA  # noqa: E402
from PIL import Image  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]
CORES = ["#ff0000", "#00ff00", "#0000ff", "#ffff00", "#ff00ff", "#00ffff"]
FOLHAS = {f"/f{k}.css": f".f{k}{{height:30px;background:{c}}}".encode() for k, c in enumerate(CORES)}
PAGINA = ("<html><head><meta charset='utf-8'><title>Folhas</title>"
          + "".join(f"<link rel='stylesheet' href='f{k}.css'>" for k in range(len(CORES)))
          + "<link rel='stylesheet' href='nao-existe.css'>"
          + "<style>body{margin:0}</style></head><body>"
          + "".join(f"<div class='f{k}'></div>" for k in range(len(CORES))) + "</body></html>").encode()
estado = {"conexoes": 0, "juntos": 0, "caminhos": []}


class Atendente(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        estado["conexoes"] += 1

    def do_GET(self):
        self.connection.setblocking(False)
        try:
            if b"GET " in self.rfile.peek(4096):
                estado["juntos"] += 1
        except (BlockingIOError, OSError):
            pass
        finally:
            self.connection.setblocking(True)
        estado["caminhos"].append(self.path)
        if self.path == "/pagina.html":
            corpo, tipo, sit = PAGINA, "text/html; charset=utf-8", 200
        elif self.path in FOLHAS:
            corpo, tipo, sit = FOLHAS[self.path], "text/css", 200
        else:
            corpo, tipo, sit = b"nada", "text/plain", 404
        self.send_response(sit)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)
        self.wfile.flush()

    def log_message(self, *a):
        pass


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def main():
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Atendente)
    servidor.daemon_threads = True
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    url = f"http://10.0.2.2:{porta}/pagina.html"
    with Maquina("h143", "ide", extras=REDE, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 30)
        time.sleep(1)
        n = len(m.serial())
        estado["conexoes"] = 0
        m.digitar(f"navegador {url}\n", 0.04)
        log = esperar(m, "navegador: pronto", 60, n)
        verificar("folhas de estilo juntas: 6 de 7 em 1 lote(s)" in log,
                  "as 6 folhas que existem vieram num lote só (a sétima deu 404)")
        verificar(estado["juntos"] >= 3, f"pipelining: {estado['juntos']} pedidos chegaram com outro já esperando")
        verificar(estado["conexoes"] <= 2, f"tudo em {estado['conexoes']} conexão(ões)")
        repetidas = [c for c in FOLHAS if estado["caminhos"].count(c) != 1]
        verificar(not repetidas, f"cada folha pedida uma vez ({repetidas or 'nenhuma repetida'})")
        time.sleep(1)
        j = janela(m.serial(), "Cipó")
        m.mouse_para(j["x"] + 300, j["y"] + j["a"] + 10)
        time.sleep(0.5)
        png = Image.open(m.captura("H143-folhas")).convert("RGB")
        px = png.load()
        for k, c in enumerate(CORES):
            esperado = tuple(int(c[i:i + 2], 16) for i in (1, 3, 5))
            p = px[j["x"] + 100, j["y"] + BARRA + 15 + 30 * k]
            verificar(p == esperado, f"faixa {k + 1}: {p} (esperado {esperado})")
    servidor.shutdown()


if __name__ == "__main__":
    main()
