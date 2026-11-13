#!/usr/bin/env python3
"""H57 e H58 - SSH de verdade, de dentro do SaguiOS.

Critérios:
  H57: o `ssh-chave` do SaguiOS faz uma chave Ed25519; a parte pública, posta
       no authorized_keys de um sshd de teste (o OpenSSH desta máquina, numa
       porta alta), deixa o `ssh` entrar sem senha.
  H58: `ssh usuario@servidor` dentro do Terminal: na primeira vez mostra a
       impressão digital certa da chave do servidor e pergunta; o shell remoto
       responde; o tamanho do terminal combinado é 28 x 94; o vim edita um
       arquivo usando as setas (que precisam ir no "modo de aplicação" que o
       vim pede) e grava; o htop desenha a tela dele; na segunda vez o servidor
       já é conhecido; se a chave do servidor muda, a conexão é recusada com
       aviso. As capturas mostram o vim e o htop.

O sshd roda como o usuário atual, ouvindo em 127.0.0.1 (o 10.0.2.2 da rede do
QEMU). Só o SaguiOS fala SSH com ele: nenhum cliente da máquina é usado.
"""
import getpass
import os
import re
import shutil
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, SAIDA, janela  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]
PASTA = os.path.join(SAIDA, "h58-ssh")
USUARIO = getpass.getuser()
SSHD = shutil.which("sshd") or "/usr/sbin/sshd"


def porta_livre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Sshd:
    def __init__(self, chave_servidor, autorizadas, porta):
        self.porta = porta
        conf = os.path.join(PASTA, f"sshd-{porta}.conf")
        self.log = os.path.join(PASTA, f"sshd-{porta}.log")
        with open(conf, "w") as f:
            f.write("\n".join([
                "ListenAddress 127.0.0.1", f"HostKey {chave_servidor}", f"PidFile {PASTA}/sshd-{porta}.pid",
                f"AuthorizedKeysFile {autorizadas}", "StrictModes no", "UsePAM no", "PasswordAuthentication no",
                "KbdInteractiveAuthentication no", "PrintMotd no", "PrintLastLog no", "LogLevel VERBOSE",
            ]) + "\n")
        self.saida = open(self.log, "wb")
        self.p = subprocess.Popen([SSHD, "-D", "-e", "-f", conf, "-p", str(porta)], stdout=self.saida, stderr=subprocess.STDOUT)
        for _ in range(100):
            if b"Server listening" in open(self.log, "rb").read():
                return
            time.sleep(0.05)
        raise FalhaTeste("o sshd de teste não subiu: " + open(self.log, errors="replace").read()[-500:])

    def parar(self):
        self.p.terminate()
        try:
            self.p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.p.kill()
        self.saida.close()


def chave_servidor(nome):
    caminho = os.path.join(PASTA, nome)
    if not os.path.exists(caminho):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", caminho], check=True)
    digital = subprocess.run(["ssh-keygen", "-lf", caminho + ".pub", "-E", "sha256"], capture_output=True, text=True,
                             check=True).stdout.split()[1]
    return caminho, digital


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim da serial: {m.serial()[-600:]!r}")


def main():
    if os.path.exists(PASTA):
        shutil.rmtree(PASTA)
    os.makedirs(PASTA)
    chave1, digital1 = chave_servidor("servidor1")
    chave2, digital2 = chave_servidor("servidor2")
    autorizadas = os.path.join(PASTA, "authorized_keys")
    open(autorizadas, "w").close()
    arquivo_vim = os.path.join(PASTA, "nota.txt")
    porta = porta_livre()
    sshd = Sshd(chave1, autorizadas, porta)
    try:
        with Maquina("h58", "ide", extras=REDE, memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(1)
            # H57: a chave.
            out = m.comando("ssh-chave", tempo=60)
            achou = re.search(r"ssh-chave: (ssh-ed25519 [A-Za-z0-9+/=]+ saguios)", m.serial())
            verificar(achou is not None, "ssh-chave fez uma chave Ed25519 e mostrou a parte pública")
            with open(autorizadas, "w") as f:
                f.write(achou.group(1) + "\n")
            r = subprocess.run(["ssh-keygen", "-lf", autorizadas], capture_output=True, text=True)
            verificar(r.returncode == 0 and "ED25519" in r.stdout, "o ssh-keygen da máquina aceita a linha pública do SaguiOS")
            out = m.comando("ssh-chave", tempo=30)
            verificar("Chave já existe" in out, "ssh-chave de novo não troca a chave sem pedir")

            # H58: primeira conexão.
            inicio = len(m.serial())
            m.digitar(f"ssh -p {porta} {USUARIO}@10.0.2.2\n", 0.04)
            esperar(m, "Confiar neste servidor", 90, inicio)
            verificar(digital1 in m.serial()[inicio:], f"a impressão digital mostrada é a do servidor ({digital1[:20]}...)")
            m.digitar("s\n", 0.05)
            esperar(m, "ssh: sessão aberta", 60, inicio)
            verificar("login pela chave" in m.serial() and "aceito" in m.serial(), "entrou pela chave do ssh-chave, sem senha")
            time.sleep(2)
            antes = len(m.serial())
            # Prompt e título neutros (as capturas vão para o repositório).
            m.digitar("export PS1='servidor$ '; printf '\\033]0;servidor de teste\\007'; clear\n", 0.04)
            time.sleep(1)
            m.digitar("stty size; expr 40 + 2\n", 0.04)
            esperar(m, "28 94", 30, antes)
            esperar(m, "42", 30, antes)
            verificar(True, "shell remoto responde; terminal combinado de 28 linhas x 94 colunas")
            # H78: maximizar o Terminal muda o tamanho no servidor (window-change).
            t = janela(m.serial(), "Terminal")
            bmax = (t["fx"] + t["l"] + 4 - 2 - 20 - 48 + 8, t["fy"] + 12)
            antes = len(m.serial())
            m.clique(*bmax)
            esperar(m, "ssh: tamanho 126 x 38", 30, antes)
            time.sleep(1)
            m.digitar("stty size\n", 0.04)
            esperar(m, "38 126", 30, antes)
            m.clique(t["fx"] + t["l"] + 4 - 2 - 20 - 48 + 8, 24 + 12)       # botão no lugar da janela maximizada (x igual, y no topo)
            esperar(m, "ssh: tamanho 94 x 28", 30, antes)
            verificar(True, "maximizar o Terminal muda o tamanho no servidor (38 x 126) e voltar também")

            # vim: insere, usa as setas e grava.
            m.digitar(f"vim -u NONE -N {arquivo_vim}\n", 0.04)
            time.sleep(3)
            m.digitar("iabc", 0.08)
            m.tecla("left", 0.15)
            m.tecla("left", 0.15)
            m.digitar("X", 0.08)
            m.tecla("end", 0.15)
            m.digitar(" Sagui", 0.08)
            time.sleep(1)
            m.captura("H58-vim")
            m.tecla("esc", 0.2)
            m.digitar(":wq\n", 0.08)
            fim = time.time() + 20
            while time.time() < fim and not os.path.exists(arquivo_vim):
                time.sleep(0.3)
            conteudo = open(arquivo_vim).read() if os.path.exists(arquivo_vim) else "(não gravou)"
            verificar(conteudo == "aXbc Sagui\n", f"vim no servidor: setas no modo de aplicação e gravação ({conteudo!r})")

            # htop.
            time.sleep(1)
            antes = len(m.serial())
            m.digitar("htop -u nobody\n", 0.04)        # só os processos de "nobody": a captura não mostra os da máquina
            time.sleep(4)
            m.captura("H58-htop")
            s = m.serial()[antes:]
            verificar("Load average" in s and "Tasks" in s, "htop desenhou a tela dele no Terminal")
            m.digitar("q", 0.1)
            time.sleep(1)
            antes = len(m.serial())
            m.digitar("exit\n", 0.05)
            esperar(m, "encerrada (código 0)", 30, antes)
            esperar(m, "sagui:", 30, antes)
            verificar(True, "exit encerra a sessão e volta ao prompt do SaguiOS")

            # Segunda vez: servidor conhecido, comando direto.
            out = m.comando(f"ssh -p {porta} {USUARIO}@10.0.2.2 echo SEGUNDA_VEZ", tempo=90)
            verificar("SEGUNDA_VEZ" in out and "Confiar" not in out, "na segunda vez o servidor já é conhecido (sem pergunta)")

            # Chave do servidor trocada: recusa.
            sshd.parar()
            sshd = Sshd(chave2, autorizadas, porta)
            out = m.comando(f"ssh -p {porta} {USUARIO}@10.0.2.2 echo NAO_PODE", tempo=90)
            verificar("MUDOU" in out and "NAO_PODE" not in out.replace("echo NAO_PODE", ""),
                      "chave do servidor diferente: conexão recusada com aviso")
            m.captura("H58-chave-mudou")
    finally:
        sshd.parar()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
