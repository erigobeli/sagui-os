#!/usr/bin/env python3
"""H110 - instalador visual: partições e instalação no espaço livre (D063).

O disco de teste já tem uma partição de outro sistema (tipo 0x07, como a do
Windows) com um conteúdo conhecido. No assistente:
  - o passo das partições mostra a partição e o espaço livre;
  - "Nova partição" cria uma FAT32 de 64 MB no começo do espaço livre;
  - o SaguiOS vai para o espaço livre que sobrou ("No espaço livre");
  - nada é gravado antes do resumo (o disco continua igual até o Instalar).

Aqui fora, no arquivo do disco:
  - a partição antiga continua na tabela e o conteúdo dela está intacto;
  - a FAT32 nova e a dos arquivos passam no fsck.vfat;
  - o sistema (0xDA) começa depois das outras, e o cabeçalho de boot aponta
    para lá.
Ligando só por esse disco, o SaguiOS sobe do lugar novo e monta a partição
dos arquivos sozinho. Capturas de cada passo, para a revisão a olho.
"""
import os
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, SAIDA  # noqa: E402

MB = 2048
ALHEIA_INI, ALHEIA_N = 1 * MB, 100 * MB          # a partição "do Windows"
MARCA = b"DADOS DE QUEM TESTA, NAO APAGAR " * 16    # 512 bytes


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def criar_disco(caminho):
    with open(caminho, "wb") as f:
        f.truncate(512 * 1024 * 1024)
        mbr = bytearray(512)
        e = bytes([0, 0, 0, 0, 0x07, 0, 0, 0]) + struct.pack("<II", ALHEIA_INI, ALHEIA_N)
        mbr[446:462] = e
        mbr[510:512] = b"\x55\xaa"
        f.seek(0)
        f.write(mbr)
        for k in (0, 1, ALHEIA_N // 2, ALHEIA_N - 1):     # a marca em vários setores da partição
            f.seek((ALHEIA_INI + k) * 512)
            f.write(MARCA)


def fsck(hd, ini, nome):
    img = os.path.join(SAIDA, f"h110-{nome}.img")
    with open(hd, "rb") as f, open(img, "wb") as g:
        f.seek(ini * 512)
        g.write(f.read(64 * 1024 * 1024) if nome == "dados" else f.read())
    r = subprocess.run(["fsck.vfat", "-n", img], capture_output=True, text=True)
    os.remove(img)
    return r


def main():
    os.makedirs(SAIDA, exist_ok=True)
    hd = os.path.join(SAIDA, "h110-hd.img")
    criar_disco(hd)
    with Maquina("h110a", "ide", memoria="768M", extras=["-drive", f"format=raw,file={hd},if=ide,index=1"]) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n = len(m.serial())
        m.digitar("instalar\n", 0.03)
        esperar(m, "instalar: passo 0 (Boas-vindas)", 20, n)
        time.sleep(1)
        w = janela(m.serial()[n:], "Instalar o SaguiOS")
        x0, y0 = w["x"], w["y"]
        m.captura("H110-1-boas-vindas")
        m.tecla("ret")
        esperar(m, "instalar: passo 1 (Disco)", 10, n)
        time.sleep(0.8)
        m.captura("H110-2-disco")
        m.tecla("ret")
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.8)
        m.captura("H110-3-confirmacao")
        m.digitar("hd1\n", 0.05)
        esperar(m, "instalar: passo 2 (Partições)", 20, n)
        verificar("instalar: particoes de hd1: 1 partições, 2 regiões" in m.serial()[n:],
                  "o passo das partições acha a partição de outro sistema e o espaço livre")
        time.sleep(0.8)
        m.captura("H110-4-particoes")

        X0, LISTA_Y, LINHA_A, CL = 240, 172, 28, 552
        def linha(r):
            return x0 + X0 + 100, y0 + LISTA_Y + r * LINHA_A + 14
        # A linha 1 é o espaço livre: Nova partição de 64 MB.
        m.clique(*linha(1))
        esperar(m, "instalar: regiao 1 (livre)", 10, n)
        time.sleep(0.5)
        m.clique(x0 + X0 + 112 + 72, y0 + LISTA_Y + 5 * LINHA_A + 14 + 17)   # Nova partição
        esperar(m, '"Nova partição"', 10, n)
        time.sleep(0.8)
        for _ in range(6):
            m.tecla("backspace")
        m.digitar("64\n", 0.08)
        esperar(m, "instalar: nova FAT32 em hd1", 10, n)
        time.sleep(0.8)
        # Agora: alheia, nova (64 MB), livre. Escolhe o livre e "No espaço livre".
        m.clique(*linha(2))
        esperar(m, "instalar: regiao 2 (livre)", 10, n)
        oy = LISTA_Y + 5 * LINHA_A + 84
        m.clique(x0 + X0 + CL // 2 + 6 + 100, y0 + oy + 28)
        esperar(m, "instalar: modo espaço livre (região 2)", 10, n)
        time.sleep(0.8)
        m.captura("H110-5-particoes-planejadas")
        verificar(open(hd, "rb").read(512)[446 + 16 + 4] == 0, "até aqui nada foi gravado no disco (a tabela continua igual)")
        m.tecla("ret")
        esperar(m, "instalar: passo 3 (Resumo)", 10, n)
        time.sleep(0.8)
        m.captura("H110-6-resumo")
        m.tecla("ret")
        esperar(m, "instalar: passo 4", 10, n)
        time.sleep(0.5)
        m.captura("H110-7-instalando")
        esperar(m, "instalar: pronto em hd1", 240, n)
        time.sleep(1)
        m.captura("H110-8-pronto")
        verificar(True, "o assistente instalou no espaço livre")
        m.tecla("ret")
        time.sleep(1)

    dados = open(hd, "rb").read(160 * 1024 * 1024)
    mbr = dados[:512]
    ents = [(mbr[446 + k * 16 + 4], *struct.unpack_from("<II", mbr, 446 + k * 16 + 8), mbr[446 + k * 16]) for k in range(4)]
    tipos = {t: (i, n, a) for t, i, n, a in ents if t}
    verificar(tipos.get(0x07, (0, 0))[:2] == (ALHEIA_INI, ALHEIA_N), f"a partição de outro sistema continua na tabela ({ents})")
    intacta = all(dados[(ALHEIA_INI + k) * 512:(ALHEIA_INI + k) * 512 + 512] == MARCA for k in (0, 1))
    with open(hd, "rb") as f:
        for k in (ALHEIA_N // 2, ALHEIA_N - 1):
            f.seek((ALHEIA_INI + k) * 512)
            intacta = intacta and f.read(512) == MARCA
    verificar(intacta, "o conteúdo da partição de outro sistema está intacto")
    fats = sorted(i for t, i, n, a in ents if t == 0x0C)
    verificar(len(fats) == 2 and fats[0] == ALHEIA_INI + ALHEIA_N, f"duas FAT32: a nova logo depois da antiga e a dos arquivos ({fats})")
    r = fsck(hd, fats[0], "dados")
    verificar(r.returncode == 0, f"fsck.vfat: a FAT32 nova está íntegra ({r.stdout.strip().splitlines()[-1]})")
    r = fsck(hd, fats[1], "arquivos")
    verificar(r.returncode == 0, f"fsck.vfat: a FAT32 dos arquivos está íntegra ({r.stdout.strip().splitlines()[-1]})")
    s_ini = tipos[0xDA][0]
    cab = dados[512:1024]
    e2 = struct.unpack_from("<I", cab, 8)[0]
    verificar(tipos[0xDA][2] == 0x80 and s_ini > fats[0] and e2 == s_ini,
              f"o sistema (0xDA, ativa) fica depois das outras, em {s_ini}, e o cabeçalho de boot aponta para lá ({e2})")

    with Maquina("h110b", "ide", imagem=hd, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("tem configuracoes do SaguiOS: montado sozinho" in m.serial(), "ligando pelo disco, o SaguiOS sobe do lugar novo e monta a partição dos arquivos")
        time.sleep(1)
        m.captura("H110-9-ligado-pelo-disco")


if __name__ == "__main__":
    main()
