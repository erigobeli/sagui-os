#!/usr/bin/env python3
"""H36 - Rede: placa e1000, DHCP, ping, TCP e HTTP (D032).

Critério: no QEMU com a placa e1000 e a rede "user" (que faz o papel do
roteador 10.0.2.2 e do servidor DHCP/DNS 10.0.2.3), o SaguiOS recebe um
endereço por DHCP, responde ao "ping" do comando, e baixa por HTTP um
arquivo servido pela própria máquina dos testes (que, de dentro do QEMU,
fica em 10.0.2.2). DNS só é testado se a máquina dos testes tem internet.
"""
import functools
import http.server
import os
import socket
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_console, SAIDA  # noqa: E402


def servidor_http(pasta):
    tratador = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    tratador.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), tratador)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def main():
    pasta = os.path.join(SAIDA, "h36-www")
    os.makedirs(pasta, exist_ok=True)
    # ~38 KB: mais que o anel de recepção de 32 KB, conferido inteiro na tela
    texto = "".join(f"linha {i:04d}: Olá do servidor de testes!\n" for i in range(1000))
    with open(os.path.join(pasta, "teste.txt"), "w", encoding="utf-8") as f:
        f.write(texto + "FIM\n")
    grande = bytes((i * 7) & 255 for i in range(300_000))   # passa do anel de 32 KB várias vezes
    with open(os.path.join(pasta, "grande.bin"), "wb") as f:
        f.write(grande)
    srv = servidor_http(pasta)
    porta = srv.server_address[1]

    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    with Maquina("h36", "ide", imagem=imagem_console(), extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("[E1000]" in m.serial(), "a placa e1000 foi achada no PCI")
        m.esperar_serial("[REDE] DHCP: ip 10.0.2.15", 30)
        out = m.comando("rede")
        verificar("IP: 10.0.2.15" in out and "Roteador: 10.0.2.2" in out, "o comando rede mostra o IP do DHCP e o roteador")
        out = m.comando("ping 10.0.2.2 2")
        verificar("2 enviados, 2 respondidos" in out, "ping no roteador: as duas respostas voltaram")
        out = m.comando(f"baixar http://10.0.2.2:{porta}/teste.txt", tempo=120)
        verificar("200 OK" in out, "baixar: o servidor respondeu 200 OK")
        verificar(texto + "FIM\n" in out, "baixar: os 38 KB chegaram inteiros e em ordem, com acento")
        out = m.comando(f"baixar http://10.0.2.2:{porta}/grande.bin /g.bin", tempo=120)
        verificar("300000 bytes gravados em /g.bin" in out, "baixar: 300 KB gravados em arquivo")
        out = m.comando("wc /g.bin")
        verificar("300000" in out, "o arquivo gravado tem o tamanho certo")
        out = m.comando("baixar http://10.0.2.2:1/")
        verificar("recusada" in out, "porta fechada: conexão recusada")
        try:
            socket.gethostbyname("example.com")
            tem_internet = True
        except OSError:
            tem_internet = False
        if tem_internet:
            out = m.comando("ping example.com 1")
            verificar("PING example.com (" in out, "DNS: example.com virou um endereço IP")
        else:
            print("  (sem internet na máquina dos testes: DNS não testado)")
        m.captura("H36-rede")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
