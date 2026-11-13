#!/usr/bin/env python3
"""H53/H54 - HTTPS de verdade dentro do SaguiOS (teste com internet).

Critério: "baixar https://..." conecta por TLS 1.3 com a criptografia e o
TLS próprios, confere o certificado com as raízes de /sistema/certificados e
grava a página (Wikipédia em português e example.com); certificados errados
(nome trocado e autoridade desconhecida, num
servidor openssl local) são recusados com a mensagem certa; há 24 raízes.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, SAIDA, criar_pendrive, pendrive_qemu  # noqa: E402
from h43_e1000 import REDE  # noqa: E402


PORTAS = {}


def certificados(pasta):
    """Autoridade de teste, um servidor com o nome certo (o IP 10.0.2.2), um com
    outro nome e um de uma autoridade desconhecida, gerados com o openssl."""
    os.makedirs(pasta, exist_ok=True)
    def ossl(*a):
        subprocess.run(["openssl", *a], cwd=pasta, check=True, capture_output=True)
    for ca in ("ca", "outra"):
        ossl("req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-keyout", f"{ca}.key",
             "-out", f"{ca}.pem", "-days", "30", "-subj", f"/CN=Autoridade {ca} do teste SaguiOS",
             "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")
    ossl("x509", "-in", "ca.pem", "-outform", "DER", "-out", "ca.der")
    for nome, ca, san in (("bom", "ca", "IP:10.0.2.2"), ("nome", "ca", "DNS:outro.exemplo"), ("desconhecida", "outra", "IP:10.0.2.2")):
        ossl("req", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-keyout", f"{nome}.key",
             "-out", f"{nome}.csr", "-subj", f"/CN={nome}")
        with open(os.path.join(pasta, f"{nome}.ext"), "w") as f:
            f.write(f"subjectAltName={san}\nbasicConstraints=CA:FALSE\n")
        ossl("x509", "-req", "-in", f"{nome}.csr", "-CA", f"{ca}.pem", "-CAkey", f"{ca}.key", "-CAcreateserial",
             "-out", f"{nome}.pem", "-days", "10", "-extfile", f"{nome}.ext")
    servidores = []
    for i, nome in enumerate(("bom", "nome", "desconhecida")):
        porta = 44300 + os.getpid() % 500 * 3 + i
        PORTAS[nome] = porta
        servidores.append(subprocess.Popen(["openssl", "s_server", "-quiet", "-tls1_3", "-www", "-accept", f"127.0.0.1:{porta}",
                                            "-cert", f"{nome}.pem", "-key", f"{nome}.key"], cwd=pasta,
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    time.sleep(1)
    return servidores


def main():
    pasta = os.path.join(SAIDA, "h53-certs")
    servidores = certificados(pasta)
    pen = criar_pendrive("h53-pen", rotulo="DADOS", arquivos={"ca-teste.der": open(os.path.join(pasta, "ca.der"), "rb").read()})
    try:
        return rodar(pen)
    finally:
        for s in servidores:
            s.terminate()


def rodar(pen):
    with Maquina("h53", "ide", extras=REDE + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 20)
        time.sleep(1)
        m.comando("cd /tmp")
        out = m.comando("baixar https://example.com/ exemplo.html", tempo=180)
        verificar("Gravado exemplo.html" in out, "HTTPS: example.com baixado com TLS 1.3 próprio")
        out = m.comando("cat exemplo.html", tempo=30)
        verificar("Example Domain" in out, "a página de example.com chegou certa")
        s = m.serial()
        verificar("tls: example.com conectado (TLS_" in s, "o log mostra a cifra e o certificado conferido")
        out = m.comando("baixar https://pt.wikipedia.org/wiki/Sagui sagui.html", tempo=300)
        verificar("Gravado sagui.html" in out, "HTTPS: artigo da Wikipédia em português baixado")
        out = m.comando("wc sagui.html", tempo=30)
        verificar("Sagui" in m.comando("cat sagui.html | conta", tempo=60) or "linhas" in out, "a página da Wikipédia tem conteúdo")
        m.captura("H53-https")
        # Certificados errados, num servidor openssl local (badssl.com não fala TLS 1.3).
        m.comando("montar")
        m.comando("cp /usb/ca-teste.der /sistema/certificados/ca-teste.der")
        out = m.comando(f"baixar https://10.0.2.2:{PORTAS['bom']}/ ok.html", tempo=120)
        verificar("Gravado ok.html" in out, "servidor local com a autoridade de teste copiada para /sistema/certificados: conecta")
        out = m.comando(f"baixar https://10.0.2.2:{PORTAS['nome']}/ x.html", tempo=120)
        verificar("nome não confere" in out, "certificado de outro nome é recusado (nome não confere)")
        out = m.comando(f"baixar https://10.0.2.2:{PORTAS['desconhecida']}/ x.html", tempo=120)
        verificar("autoridade desconhecida" in out or "cadeia quebrada" in out, "certificado de uma autoridade fora da lista é recusado")
        m.comando("rm /sistema/certificados/ca-teste.der", teclas_finais=("ret", "s", "ret"))
        out = m.comando("ls /sistema/certificados | conta")
        verificar("24 linhas" in out, "as 24 raízes de confiança estão em /sistema/certificados")
        m.comando("cd /")
    return 0


if __name__ == "__main__":
    sys.exit(main())
