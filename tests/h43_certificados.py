#!/usr/bin/env python3
"""
H43: verificação de certificados no TLS.

Critério: cadeias válidas com RSA, P-256 e P-384 abrem; nome errado, vencido,
autoridade desconhecida, assinatura falsa e intermediário sem CA são recusados.
"""
import datetime
import http.server
import os
import ssl
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cryptography import x509  # noqa: E402
from cryptography.hazmat.primitives import hashes, serialization  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec, rsa  # noqa: E402
from cryptography.x509.oid import NameOID  # noqa: E402
import ipaddress  # noqa: E402
from qemu import Maquina, verificar, imagem_com_raizes, SAIDA  # noqa: E402

AGORA = datetime.datetime.now(datetime.timezone.utc)
DIA = datetime.timedelta(days=1)


def chave(t):
    return rsa.generate_private_key(65537, 2048) if t == "rsa" else ec.generate_private_key(ec.SECP256R1() if t == "p256" else ec.SECP384R1())


def emitir(nome, k, emissor, k_emissor, ca, ip="10.0.2.2", vencido=False, h=hashes.SHA256):
    b = x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, nome)]))
    b = b.issuer_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, emissor)])).public_key(k.public_key())
    b = b.serial_number(x509.random_serial_number())
    b = b.not_valid_before(AGORA - 3 * DIA).not_valid_after(AGORA - DIA if vencido else AGORA + 3 * DIA)
    b = b.add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True)
    if ip:
        b = b.add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address(ip))]), critical=False)
    return b.sign(k_emissor, h())


def pem(c):
    return c.public_bytes(serialization.Encoding.PEM)


def servir(pasta, nome, cadeia, k):
    arq = os.path.join(pasta, nome + ".pem")
    with open(arq, "wb") as f:
        f.write(k.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        for c in cadeia:
            f.write(pem(c))
    trat = lambda *a: http.server.SimpleHTTPRequestHandler(*a, directory=pasta)  # noqa: E731
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), trat)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(arq)
    ctx.minimum_version = ctx.maximum_version = ssl.TLSVersion.TLSv1_3
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True, do_handshake_on_connect=False)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv.server_address[1]


def caso(nome, porta, raizes, espera):
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    url = f"/bin/cipo https://10.0.2.2:{porta}/"
    with Maquina("h43-" + nome, "ide", imagem=imagem_com_raizes(url, raizes, ["--vesa"]), extras=extras) as m:
        if espera is None:
            m.esperar_serial('cipo: pronto "Ok"', 60)
            verificar("certificado verificado" in m.serial(), f"{nome}: aceito")
        else:
            m.esperar_serial("cipo: erro:", 60)
            s = m.serial()
            verificar(espera in s and "certificado verificado" not in s, f"{nome}: recusado ({espera})")


def main():
    pasta = os.path.join(SAIDA, "h43-www")
    os.makedirs(pasta, exist_ok=True)
    open(os.path.join(pasta, "index.html"), "w").write("<html><head><title>Ok</title></head><body>oi</body></html>")

    for nome, tr, ti, tf, h in (("rsa", "rsa", "rsa", "rsa", hashes.SHA256), ("p256", "p256", "p256", "p256", hashes.SHA256),
                                ("p384", "p384", "p384", "p384", hashes.SHA384), ("misto", "rsa", "p384", "p256", hashes.SHA512)):
        kr, ki, kf = chave(tr), chave(ti), chave(tf)
        raiz = emitir("Raiz " + nome, kr, "Raiz " + nome, kr, True, ip=None, h=h)
        inter = emitir("Inter " + nome, ki, "Raiz " + nome, kr, True, ip=None, h=h)
        folha = emitir("site", kf, "Inter " + nome, ki, False, h=h)
        caso(nome, servir(pasta, nome, [folha, inter], kf), [pem(raiz)], None)

    kr, kf, kx = chave("p256"), chave("p256"), chave("p256")
    raiz = emitir("Raiz", kr, "Raiz", kr, True, ip=None)
    r = [pem(raiz)]
    caso("nome", servir(pasta, "nome", [emitir("site", kf, "Raiz", kr, False, ip="10.9.9.9")], kf), r, "outro site")
    caso("vencido", servir(pasta, "vencido", [emitir("site", kf, "Raiz", kr, False, vencido=True)], kf), r, "vencido")
    caso("desconhecida", servir(pasta, "desc", [emitir("site", kf, "Outra", kx, False)], kf), r, "autoridade desconhecida")
    caso("falsa", servir(pasta, "falsa", [emitir("site", kf, "Raiz", kx, False)], kf), r, "desconhecida")
    ki = chave("p256")
    inter = emitir("Inter", ki, "Raiz", kr, False, ip=None)
    caso("sem-ca", servir(pasta, "semca", [emitir("site", kf, "Inter", ki, False), inter], kf), r, "certificado")
    return 0


if __name__ == "__main__":
    sys.exit(main())
