#!/usr/bin/env python3
"""
H39: criptografia, HTTPS e imagens no Cipó.

Critério: as primitivas passam nos vetores das especificações; o Cipó abre por
TLS 1.3 uma página de um servidor local e mostra um PNG e um JPEG; um
servidor só com TLS 1.2 dá erro claro.
"""
import functools
import http.server
import os
import re
import ssl
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image  # noqa: E402
from qemu import Maquina, verificar, imagem_de_teste, imagem_com_raizes, cor_do_pixel, SAIDA  # noqa: E402

HTML = """<html><head><title>Fotos</title></head><body><h1>Seguro</h1>
<img src="a.png" alt="png"><p>meio</p><img src="b.jpg" alt="jpeg"></body></html>"""


def servidor(pasta, versao):
    chave, cert = os.path.join(pasta, "k.pem"), os.path.join(pasta, "c.pem")
    if not os.path.exists(cert):
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", chave, "-out", cert,
                        "-days", "1", "-subj", "/CN=teste", "-addext", "subjectAltName=IP:10.0.2.2"], check=True, capture_output=True)
    trat = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    trat.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), trat)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, chave)
    ctx.minimum_version = ctx.maximum_version = versao
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def main():
    with Maquina("h39-v", "ide", imagem=imagem_de_teste("/testes/cripto", ["--vesa"])) as m:
        m.esperar_serial("cripto: ", 60)
        m.esperar_serial("cripto: tudo certo", 60)
        verificar(True, "vetores de SHA-256, HMAC, AES, GCM e X25519 conferem")

    pasta = os.path.join(SAIDA, "h39-www")
    os.makedirs(pasta, exist_ok=True)
    for velho in ("k.pem", "c.pem"):   # certificado de uma rodada anterior pode ter vencido
        if os.path.exists(os.path.join(pasta, velho)):
            os.remove(os.path.join(pasta, velho))
    open(os.path.join(pasta, "index.html"), "w").write(HTML)
    Image.new("RGB", (120, 60), (200, 30, 40)).save(os.path.join(pasta, "a.png"))
    Image.new("RGB", (120, 60), (20, 60, 200)).save(os.path.join(pasta, "b.jpg"), quality=95)
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]

    srv = servidor(pasta, ssl.TLSVersion.TLSv1_3)
    porta = srv.server_address[1]
    raiz = open(os.path.join(pasta, "c.pem"), "rb").read()
    with Maquina("h39", "ide", imagem=imagem_com_raizes(f"/bin/cipo https://10.0.2.2:{porta}/", [raiz], ["--vesa"]), extras=extras) as m:
        m.esperar_serial('cipo: pronto "Fotos"', 120)
        s = m.serial()
        time.sleep(1)   # "pronto" sai antes de a página ser pintada
        verificar("TLS 1.3, certificado verificado" in s, "conectou por TLS 1.3 com o certificado verificado")
        verificar(len(re.findall(r"cipo: imagem 120x60", s)) == 2, "PNG e JPEG decodificados")
        png = m.captura("H39-cipo-https-imagens")
        jan = re.search(r'criada por \d+: "Cip.*?" \d+x\d+ em \((\d+),(\d+)\)', s)
        jx, jy = int(jan.group(1)), int(jan.group(2))
        x = jx + 2 + 12 + 60
        cores = [cor_do_pixel(png, x, y) for y in range(jy + 70, jy + 400, 6)]
        verificar(any(c[0] > 180 and c[1] < 60 and c[2] < 70 for c in cores), "vermelho do PNG na tela")
        verificar(any(c[2] > 170 and c[0] < 50 and c[1] < 90 for c in cores), "azul do JPEG na tela")
    # Digitado à mão e sem esquema: tenta HTTPS. O endereço da página inicial não atrapalha.
    with Maquina("h39-d", "ide", imagem=imagem_com_raizes("/bin/cipo", [raiz], ["--vesa"]), extras=extras) as m:
        m.esperar_serial('cipo: pronto "In', 60)
        time.sleep(1)
        m.digitar(f"10.0.2.2:{porta}\n", 0.08)
        m.esperar_serial(f"cipo: abrindo https://10.0.2.2:{porta}", 10)
        m.esperar_serial('cipo: pronto "Fotos"', 60)
        verificar(True, "endereço digitado sem esquema abre por HTTPS")
    srv.shutdown()

    srv = servidor(pasta, ssl.TLSVersion.TLSv1_2)
    porta = srv.server_address[1]
    with Maquina("h39-12", "ide", imagem=imagem_de_teste(f"/bin/cipo https://10.0.2.2:{porta}/", ["--vesa"]), extras=extras) as m:
        m.esperar_serial("cipo: erro:", 60)
        verificar(True, "servidor só com TLS 1.2 mostra erro, sem travar")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
