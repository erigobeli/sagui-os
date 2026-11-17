#!/usr/bin/env python3
"""H111 - instalador: formatar uma partição para o SaguiOS e instalar nela.

O disco de teste é todo ocupado por uma partição de outro sistema (0x07),
sem espaço livre. No passo das partições:
  - sem espaço livre, "Continuar" não libera (não há onde instalar);
  - escolher a partição e "Formatar" (para o SaguiOS) libera o Continuar;
  - o resumo diz que a partição é formatada para o SaguiOS.
Aqui fora: a partição 0x07 sumiu, e no lugar dela estão o sistema (0xDA) e
a FAT32 dos arquivos (fsck.vfat). Ligando pelo disco, o SaguiOS sobe e
monta a partição dos arquivos sozinho. Capturas para a revisão a olho.
"""
import os
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, SAIDA  # noqa: E402

MB = 2048
TOTAL = 400 * MB


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def main():
    os.makedirs(SAIDA, exist_ok=True)
    hd = os.path.join(SAIDA, "h111-hd.img")
    with open(hd, "wb") as f:
        f.truncate(TOTAL * 512)
        mbr = bytearray(512)
        mbr[446:462] = bytes([0, 0, 0, 0, 0x07, 0, 0, 0]) + struct.pack("<II", MB, TOTAL - MB)
        mbr[510:512] = b"\x55\xaa"
        f.write(mbr)
    with Maquina("h111a", "ide", memoria="768M", extras=["-drive", f"format=raw,file={hd},if=ide,index=1"]) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n = len(m.serial())
        m.digitar("instalar\n", 0.03)
        esperar(m, "instalar: passo 0 (Boas-vindas)", 20, n)
        time.sleep(1)
        w = janela(m.serial()[n:], "Instalar o SaguiOS")
        x0, y0 = w["x"], w["y"]
        m.tecla("ret")
        esperar(m, "instalar: passo 1 (Disco)", 10, n)
        time.sleep(0.6)
        m.tecla("ret")
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.8)
        m.digitar("hd1\n", 0.05)
        esperar(m, "instalar: passo 2 (Partições)", 20, n)
        time.sleep(0.8)
        m.tecla("ret")
        time.sleep(1)
        verificar("instalar: passo 3" not in m.serial()[n:], "sem espaço livre, o Continuar não libera")
        X0, LISTA_Y, LINHA_A = 240, 172, 28
        m.clique(x0 + X0 + 100, y0 + LISTA_Y + 14)                         # a partição 1
        esperar(m, "instalar: regiao 0 (NTFS ou exFAT)", 10, n)
        time.sleep(0.5)
        m.clique(x0 + X0 + 264 + 52, y0 + LISTA_Y + 5 * LINHA_A + 14 + 17)  # Formatar
        esperar(m, '"Formatar para o SaguiOS"', 10, n)
        time.sleep(0.8)
        m.captura("H111-1-formatar")
        m.tecla("ret")                                                      # Sim
        esperar(m, "instalar: formatar hd1", 10, n)
        verificar("para o SaguiOS (instala nela)" in m.serial()[n:], "Formatar marca a partição para o SaguiOS")
        time.sleep(0.8)
        m.captura("H111-2-particao-formatada")
        m.tecla("ret")
        esperar(m, "instalar: passo 3 (Resumo)", 10, n)
        verificar(True, "o Continuar libera depois de formatar a partição")
        time.sleep(0.8)
        m.captura("H111-3-resumo")
        m.tecla("ret")
        esperar(m, "instalar: pronto em hd1", 240, n)
        verificar(True, "o SaguiOS foi instalado na partição formatada")
        m.tecla("ret")
        time.sleep(1)

    mbr = open(hd, "rb").read(512)
    ents = [(mbr[446 + k * 16 + 4], *struct.unpack_from("<II", mbr, 446 + k * 16 + 8)) for k in range(4)]
    tipos = {t: (i, c) for t, i, c in ents if t}
    verificar(0x07 not in tipos and 0xDA in tipos and 0x0C in tipos, f"a partição 0x07 deu lugar ao sistema e aos arquivos ({ents})")
    verificar(tipos[0xDA][0] == MB and tipos[0x0C][0] + tipos[0x0C][1] <= TOTAL, "tudo dentro do espaço da partição antiga")
    fat = os.path.join(SAIDA, "h111-fat.img")
    with open(hd, "rb") as f, open(fat, "wb") as g:
        f.seek(tipos[0x0C][0] * 512)
        g.write(f.read(tipos[0x0C][1] * 512))
    r = subprocess.run(["fsck.vfat", "-n", fat], capture_output=True, text=True)
    os.remove(fat)
    verificar(r.returncode == 0, f"fsck.vfat: a FAT32 dos arquivos está íntegra ({r.stdout.strip().splitlines()[-1]})")
    with Maquina("h111b", "ide", imagem=hd, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("tem configuracoes do SaguiOS: montado sozinho" in m.serial(), "ligando pelo disco, o SaguiOS sobe e monta os arquivos")


if __name__ == "__main__":
    main()
