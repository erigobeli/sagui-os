#!/usr/bin/env python3
"""H105 - discos rígidos IDE/ATA (issue #45; D062 e D064).

Critérios:
  - o driver ATA acha o disco do sistema (hd0) e mais dois discos e lê o
    que há neles (formato e rótulo da FAT32, no log);
  - a partição que o instalador cria (FAT32 SAGUIOS com as configurações)
    monta sozinha, sem o aviso de "pendrive";
  - a FAT32 de outro sistema num HD não monta: "montar hd2" recusa
    (ERRO_ACESSO), e esse HD sai byte a byte igual ao que entrou;
  - um pendrive USB ao lado continua montando como antes;
  - captura para a revisão a olho.
"""
import hashlib
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu  # noqa: E402


def soma(caminho):
    h = hashlib.sha256()
    with open(caminho, "rb") as f:
        for bloco in iter(lambda: f.read(1 << 20), b""):
            h.update(bloco)
    return h.hexdigest()


def main():
    # Um HD com a FAT32 do SaguiOS (rótulo e .saguios/), o caso que mais
    # tentaria montar sozinho, e outro com a FAT32 de outro sistema.
    hd = criar_pendrive("h105-hd", 128, "SAGUIOS", pastas=(".saguios",), arquivos={
        ".saguios/configuracoes.txt": b"aparencia.efeitos = automatico\n",
        "nota.txt": "escrito aqui fora\n".encode(),
    })
    alheio = criar_pendrive("h105-alheio", 64, "DADOS", arquivos={"meu.txt": b"dados de quem testa\n"})
    pen = criar_pendrive("h105-pen", 64, "MEUPEN", arquivos={"oi.txt": b"pendrive\n"})
    antes = {p: soma(p) for p in (hd, alheio)}

    extras = ["-drive", f"format=raw,file={hd},if=ide,index=1",
              "-drive", f"format=raw,file={alheio},if=ide,index=2",
              "-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h105", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        verificar("[DISCO] hd0:" in s and "[DISCO] hd1:" in s and "[DISCO] hd2:" in s,
                  "o driver ATA acha os três discos (hd0, hd1 e hd2)")
        verificar('FAT32, rotulo "SAGUIOS"' in s and 'rotulo "DADOS"' in s,
                  "o driver lê os discos: formato e rótulo das duas FAT32")
        verificar("tem configuracoes do SaguiOS: montado sozinho" in s and "Pendrive com as suas configura" not in s,
                  "a partição SAGUIOS do hd1 monta sozinha, sem aviso de pendrive")
        out = m.comando("cat /usb/nota.txt")
        verificar("escrito aqui fora" in out, "o SaguiOS lê o arquivo do hd1")
        out = m.comando("desmontar")
        verificar("Disco hd1 desmontado: tudo gravado." in out and "pendrive" not in out, f"desmontar fala de disco ({out.strip()[-60:]!r})")
        out = m.comando("montar hd2")
        verificar("hd2: disco interno, nao monta" in m.serial() and "montado em /usb" not in out,
                  f"montar hd2 recusa a FAT32 de outro sistema ({out.strip()[-60:]!r})")
        out = m.comando("montar usb0")
        verificar("montado em /usb" in out, f"o pendrive USB ainda monta ({out.strip()[-60:]!r})")
        out = m.comando("cat /usb/oi.txt")
        verificar("pendrive" in out, "o SaguiOS lê o pendrive montado")
        m.comando("desmontar")
        m.digitar("arquivos\n", 0.04)
        m.esperar_serial("arquivos: pasta /", 30)
        time.sleep(1)
        m.captura("H105-disco")

    verificar(soma(alheio) == antes[alheio], "o HD de outro sistema saiu byte a byte igual")


if __name__ == "__main__":
    main()
