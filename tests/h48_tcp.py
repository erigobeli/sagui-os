#!/usr/bin/env python3
"""H48 - TCP e sockets.

Critério: um programa do SaguiOS baixa 10 MB de um servidor na VPS sem perder
nem trocar nenhum byte (conferido pela soma Adler-32), também com perda de
pacotes simulada (um em cada 20 quadros que chegam é descartado).
"""
import os
import random
import socket
import sys
import threading
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402
from h43_e1000 import REDE  # noqa: E402

TAMANHO = 10 * 1024 * 1024


def servidor(porta, dados, vezes):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", porta))
    s.listen(4)
    for _ in range(vezes):
        c, _ = s.accept()
        c.recv(16)
        c.sendall(dados)
        c.close()
    s.close()


def main():
    random.seed(35)
    dados = random.randbytes(TAMANHO)
    adler = zlib.adler32(dados)
    porta = 40000 + os.getpid() % 10000
    threading.Thread(target=servidor, args=(porta, dados, 2), daemon=True).start()
    with Maquina("h48", "ide", extras=REDE) as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 20)
        time.sleep(1)
        out = m.comando(f"/testes/tcpreceber 10.0.2.2 {porta}", tempo=300)
        verificar(f"recebidos {TAMANHO} bytes, adler32 {adler:08x}" in out, f"10 MB chegaram inteiros e na ordem ({out.strip().splitlines()[-1]})")
        m.comando("config rede.teste_perda 20")
        out = m.comando(f"/testes/tcpreceber 10.0.2.2 {porta}", tempo=600)
        verificar(f"recebidos {TAMANHO} bytes, adler32 {adler:08x}" in out, f"com 5% dos quadros perdidos, os 10 MB também chegaram certos ({out.strip().splitlines()[-1]})")
        m.comando("config rede.teste_perda 0")
    return 0


if __name__ == "__main__":
    sys.exit(main())
