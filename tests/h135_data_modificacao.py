#!/usr/bin/env python3
"""H135 - a data de modificação dos arquivos.

Critérios:
  - um arquivo criado agora na memória tem a data de hoje (a do relógio do
    sistema), com hora e minuto ("ls -l");
  - mexer num arquivo antigo muda a data dele; a pasta muda quando um
    arquivo é criado dentro dela;
  - os arquivos que vêm na imagem do sistema não têm data ("-");
  - no pendrive (FAT32), um arquivo gravado pelo computador antes com data
    conhecida mostra essa data, e um gravado agora pelo SaguiOS, a de agora;
  - o "ls" sem -l continua igual.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, mtools, SAIDA  # noqa: E402


def data_de(out, nome):
    m = re.search(rf"^{re.escape(nome)}\s.*?(\d\d/\d\d/\d{{4}} \d\d:\d\d|-)\s*$", out, re.M)
    return m.group(1) if m else None


def main():
    pen = criar_pendrive("h135-pen", 64, "DATAS", arquivos={"velho.txt": b"de 2019\n"})
    # A data do velho.txt no pendrive: 15/03/2019 10:20 (mtools usa a hora do arquivo de origem).
    tmp = os.path.join(SAIDA, "velho.txt")
    with open(tmp, "wb") as f:
        f.write(b"de 2019\n")
    os.utime(tmp, (1552645200, 1552645200))   # 2019-03-15 10:20:00 UTC
    os.environ["TZ"] = "UTC"   # o FAT guarda a hora local; em UTC, a do arquivo é 10:20
    time.tzset()
    mtools(pen, "mcopy", "-o", "-m", tmp, "p:/velho.txt")
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h135", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        out = m.comando("data")
        hoje = re.search(r"(\d\d)/(\d\d)/(\d{4})", out)
        verificar(hoje is not None, f"o relógio do sistema: {out.strip()[-40:]!r}")
        hoje = f"{hoje.group(1)}/{hoje.group(2)}/{hoje.group(3)}"

        m.comando("mkdir /tmp/d")
        m.comando("echo novo > /tmp/d/novo.txt")
        out = m.comando("ls -l /tmp/d")
        d = data_de(out, "novo.txt")
        verificar(d is not None and d.startswith(hoje), f"arquivo criado agora: {d} (hoje {hoje})")
        out = m.comando("ls -l /tmp")
        verificar((data_de(out, "d") or "").startswith(hoje), "a pasta /tmp/d mudou quando o arquivo foi criado nela")

        out = m.comando("ls -l /sistema")
        datas = re.findall(r"(\d\d/\d\d/\d{4} \d\d:\d\d|-)\s*$", out, re.M)
        verificar(datas and all(x == "-" for x in datas), f"os arquivos da imagem do sistema não têm data ({len(datas)} linhas)")
        out = m.comando("ls -l /")
        verificar(data_de(out, "sobre.txt") == "-", "o /sobre.txt, da imagem, começa sem data")
        m.comando("echo mudou >> /sobre.txt")
        out = m.comando("ls -l /")
        verificar((data_de(out, "sobre.txt") or "").startswith(hoje), "mexer num arquivo da imagem (/sobre.txt) dá a ele a data de agora")

        out = m.comando("ls /tmp/d")
        verificar("novo.txt" in out and "/" not in out.split("novo.txt")[1][:30] and not re.search(r"\d\d/\d\d/\d{4}", out),
                  "o ls sem -l continua sem data")

        m.esperar_serial("[DISCO] usb0", 30)
        m.comando("montar usb0", tempo=20)
        out = m.comando("ls -l /usb")
        verificar(data_de(out, "velho.txt") == "15/03/2019 10:20", f"no pendrive, o arquivo gravado antes: {data_de(out, 'velho.txt')}")
        m.comando("echo agora > /usb/agora.txt")
        out = m.comando("ls -l /usb")
        verificar((data_de(out, "agora.txt") or "").startswith(hoje), f"no pendrive, o arquivo gravado agora: {data_de(out, 'agora.txt')}")
        m.comando("clear")
        m.comando("ls -l /tmp/d")
        m.comando("ls -l /usb")
        time.sleep(0.5)
        m.captura("H135-ls-l")


if __name__ == "__main__":
    main()
