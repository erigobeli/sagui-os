#!/usr/bin/env python3
"""H129 - o Cipó reaproveita a conexão e pede as imagens juntas.

As imagens de uma página vêm pela mesma conexão da página (keep-alive), e
várias de uma vez (pipelining: os pedidos saem juntos, as respostas voltam
em ordem). Um servidor local (HTTP/1.1) serve uma página com 8 imagens PNG,
conta as conexões TCP e marca quantos pedidos chegaram antes da primeira
resposta sair.

Critérios:
  - servidor normal: as 8 imagens aparecem; a página inteira (10 pedidos)
    usa no máximo 2 conexões (antes era uma por pedido: 10), e chegam
    pedidos juntos (vários lidos de uma vez na mesma conexão);
  - servidor que derruba a conexão depois de 2 pedidos, sem avisar (sem
    "Connection: close"): as 8 imagens aparecem do mesmo jeito (as que
    ficaram sem resposta vão de novo), sem erro de recurso no registro;
  - um CSS com endereço de quase 500 caracteres (o da Wikipédia passa de 400; antes o HTTP cortava em 383)
    chega inteiro ao servidor;
  - rolar a página (PgDn) enquanto as imagens chegam (servidor lento)
    interrompe a carga, mas ela continua depois: as 8 aparecem (antes
    ficavam faltando para sempre);
  - o baixar, que não liga a guarda, continua pedindo "Connection: close".
"""
import http.server
import io
import os
import socketserver
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar  # noqa: E402
from PIL import Image  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]
QTD = 8


def png(k):
    b = io.BytesIO()
    Image.new("RGB", (40, 30), ((k * 30) % 256, 120, 200)).save(b, "PNG")
    return b.getvalue()


IMAGENS = {f"/img{k}.png": png(k) for k in range(QTD)}
LONGO = "/estilo.css?m=" + "a" * 480
PAGINA = ("<html><head><meta charset='utf-8'><title>Guarda</title>"
          f"<link rel='stylesheet' href='{LONGO}'></head><body><h1>Imagens</h1>"
          + "".join(f"<img src='img{k}.png'>" for k in range(QTD)) + "</body></html>").encode()
estado = {"conexoes": 0, "juntos": 0, "cabecalhos": [], "caminhos": []}


class Atendente(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"   # keep-alive
    derrubar = 0                    # >0: fecha a conexão sem aviso depois de tantos pedidos
    atraso = 0.0                    # segundos antes de cada imagem (servidor lento)

    def setup(self):
        super().setup()
        estado["conexoes"] += 1
        self.feitos = 0

    def do_GET(self):
        estado["cabecalhos"].append(self.headers.get("Connection", ""))
        # Outro pedido já esperando no buffer antes de respondermos: veio junto (pipelining).
        self.connection.setblocking(False)   # olhar sem esperar
        try:
            if b"GET " in self.rfile.peek(4096):
                estado["juntos"] += 1
        except (BlockingIOError, OSError):
            pass
        finally:
            self.connection.setblocking(True)
        estado["caminhos"].append(self.path)
        if self.path == LONGO:
            corpo, tipo = b"h1 { color: #008000; }", "text/css"
        elif self.path == "/pagina.html":
            corpo, tipo = PAGINA, "text/html; charset=utf-8"
        elif self.path in IMAGENS:
            corpo, tipo = IMAGENS[self.path], "image/png"
            time.sleep(Atendente.atraso)
        else:
            corpo, tipo = b"nada", "text/plain"
        self.send_response(200 if corpo != b"nada" else 404)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)
        self.wfile.flush()
        self.feitos += 1
        if Atendente.derrubar and self.feitos >= Atendente.derrubar:
            self.close_connection = True   # some sem "Connection: close"

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
    with Maquina("h129", "ide", extras=REDE, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 30)
        time.sleep(1)

        n = len(m.serial())
        estado["conexoes"] = 0
        m.digitar(f"navegador {url}\n", 0.04)
        log = esperar(m, "navegador: imagens prontas", 60, n)
        verificar(f"imagens prontas ({QTD} de {QTD})" in log, f"as {QTD} imagens aparecem")
        verificar(LONGO in estado["caminhos"], "o CSS de quase 500 caracteres chegou inteiro")
        verificar(estado["conexoes"] <= 2, f"10 pedidos em {estado['conexoes']} conexão(ões) (antes: uma por pedido)")
        verificar(all(c == "keep-alive" for c in estado["cabecalhos"]), "o Cipó pede Connection: keep-alive")
        verificar(estado["juntos"] >= 3, f"as imagens vão juntas: {estado['juntos']} pedidos chegaram com outro já esperando")
        m.tecla("alt-f4")
        time.sleep(1)

        Atendente.derrubar = 2
        n = len(m.serial())
        estado["conexoes"] = 0
        m.digitar(f"navegador {url}\n", 0.04)
        log = esperar(m, "navegador: imagens prontas", 90, n)
        verificar(f"imagens prontas ({QTD} de {QTD})" in log, f"servidor derrubando a conexão a cada 2 pedidos: as {QTD} imagens aparecem")
        verificar("navegador: recurso" not in log, "nenhuma imagem falhou")
        verificar(estado["conexoes"] >= 4, f"e o Cipó abriu as conexões que precisou ({estado['conexoes']})")
        m.tecla("alt-f4")
        time.sleep(1)

        Atendente.derrubar = 0
        Atendente.atraso = 0.4
        n = len(m.serial())
        m.digitar(f"navegador {url}\n", 0.04)
        esperar(m, "navegador: carregada", 60, n)
        for _ in range(4):
            m.tecla("pgdn")
            time.sleep(0.3)
        log = esperar(m, f"imagens prontas ({QTD} de {QTD})", 90, n)
        verificar("imagens interrompidas" in log, "rolar a página interrompe a carga das imagens")
        verificar(True, f"e depois ela continua: as {QTD} imagens aparecem")
        m.tecla("alt-f4")
        time.sleep(1)

        Atendente.atraso = 0.0
        estado["cabecalhos"].clear()
        m.comando(f"baixar http://10.0.2.2:{porta}/img0.png /tmp/x.png", tempo=30)
        verificar(estado["cabecalhos"] == ["close"], f"o baixar continua com Connection: close ({estado['cabecalhos']})")


if __name__ == "__main__":
    main()
