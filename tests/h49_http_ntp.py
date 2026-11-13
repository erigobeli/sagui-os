#!/usr/bin/env python3
"""H49 - HTTP e relógio pela internet.

Critério: "baixar http://..." de um servidor da VPS grava o arquivo certo
(também com resposta em pedaços e com redirecionamento); "relogio internet"
acerta o relógio por um servidor NTP de teste na VPS, com o fuso aplicado.
"""
import http.server
import os
import random
import socket
import socketserver
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402
from h43_e1000 import REDE  # noqa: E402

random.seed(49)
ARQUIVO = random.randbytes(300 * 1024)
NTP_SEGUNDOS = 1924992000 + 3 * 3600 + 25 * 60   # 01/01/2031 03:25 UTC


class Pagina(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/dados.bin":
            self.send_response(200)
            self.send_header("Content-Length", str(len(ARQUIVO)))
            self.end_headers()
            self.wfile.write(ARQUIVO)
        elif self.path == "/pedacos.txt":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for parte in (b"Ola, ", b"SaguiOS ", b"em pedacos!\n"):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(parte), parte))
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/velho":
            self.send_response(301)
            self.send_header("Location", "/pedacos.txt")
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()


def ntp(porta):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", porta))
    while True:
        dados, quem = s.recvfrom(64)
        r = bytearray(48)
        r[0] = 0x24
        struct.pack_into(">I", r, 40, NTP_SEGUNDOS + 2208988800)
        s.sendto(bytes(r), quem)


def main():
    porta = 42000 + os.getpid() % 5000
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", porta), Pagina)
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    porta_ntp = porta + 1
    threading.Thread(target=ntp, args=(porta_ntp,), daemon=True).start()
    with Maquina("h49", "ide", extras=REDE) as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 20)
        time.sleep(1)
        m.comando("cd /tmp")
        out = m.comando(f"baixar http://10.0.2.2:{porta}/dados.bin", tempo=120)
        verificar(f"Gravado dados.bin: {len(ARQUIVO)} bytes" in out, "baixar grava 300 KB com Content-Length")
        out = m.comando("wc dados.bin")
        verificar(str(len(ARQUIVO)) in out, "o arquivo gravado tem o tamanho certo")
        out = m.comando(f"baixar http://10.0.2.2:{porta}/velho pedacos.txt", tempo=60)
        verificar("Gravado pedacos.txt: 25 bytes" in out, "segue o redirecionamento 301 e junta a resposta em pedaços")
        out = m.comando("cat pedacos.txt")
        verificar("Ola, SaguiOS em pedacos!" in out, "o conteúdo em pedaços chegou inteiro")
        out = m.comando(f"baixar http://10.0.2.2:{porta}/nao-existe", tempo=60)
        verificar("respondeu 404" in out, "um 404 é avisado")
        m.captura("H49-baixar")
        m.comando("config relogio.fuso -3")
        out = m.comando(f"relogio internet 10.0.2.2:{porta_ntp}", tempo=60)
        verificar("acertado pela internet: 01/01/2031 00:25" in out, "relogio internet acerta pelo NTP de teste (03:25 UTC = 00:25 com fuso -3)")
        out = m.comando("data")
        verificar("01/01/2031" in out, "o relógio da placa ficou com a data nova")
        m.comando("cd /")
    return 0


if __name__ == "__main__":
    sys.exit(main())
