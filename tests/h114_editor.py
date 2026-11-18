#!/usr/bin/env python3
"""H114 - o editor de partições do instalador (D068; B4 e B6 da revisão do #53).

Disco de 400 MB: bytes conhecidos nos setores 1 a 62, uma FAT32 "WINDOWS"
ativa em 63 (até 100 MB) e uma estendida de 100 a 390 MB com duas lógicas
(uma ext3 de verdade). Só 10 MB livres: o espaço livre é recusado.

  - no editor, sem nada marcado, o Continuar não avança;
  - um clique na FAT32 a marca, outro desmarca;
  - um clique numa lógica marca a estendida inteira (B4: a lógica não sai
    sozinha, e a estendida não sai sem as lógicas aparecerem);
  - o resumo e a caixa do kernel dizem "excluir a partição 2 (e as lógicas
    5 a 6)": nenhuma exclusão fica de fora;
  - Esc na caixa: o disco inteiro byte a byte igual;
  - instalado: a entrada 1 e o identificador byte a byte, o SaguiOS na
    entrada 2 (a da estendida) e na 3, a 4 vazia, a FAT32 WINDOWS e os
    setores 1 a 62 iguais, todo o disco fora do setor 0 e da região do
    SaguiOS igual, fsck das duas FAT32, o setor 0 antigo guardado;
  - ligado só pelo disco: monta a FAT32 do SaguiOS.
"""
import hashlib
import os
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, SAIDA, IMAGEM  # noqa: E402
from h113_espaco_livre import (esperar, entrada, setor_tabela, formatar, sha, criar, abrir,  # noqa: E402
                               ate_particoes, S, MB)

TOTAL = 400 * MB
P1 = (63, 100 * MB - 63)
EXT = (100 * MB, 290 * MB)
L1 = (EXT[0] + 2048, 100000)
EBR2 = EXT[0] + 104448
L2 = (EBR2 + 2048, 50000)
ID = 0x5A61D00D


def monta(img):
    img[P1[0] * S:(P1[0] + P1[1]) * S] = formatar(["mkfs.vfat", "-F", "32", "-n", "WINDOWS"], P1[1], "fat")
    img[L1[0] * S:(L1[0] + L1[1]) * S] = formatar(["mkfs.ext3", "-q", "-F", "-L", "linux"], L1[1], "ext3")
    setor_tabela(img, 0, [entrada(0x80, 0x0C, *P1), entrada(0, 0x05, *EXT)], ident=ID)
    setor_tabela(img, EXT[0], [entrada(0, 0x83, L1[0] - EXT[0], L1[1]), entrada(0, 0x05, EBR2 - EXT[0], 60000)])
    setor_tabela(img, EBR2, [entrada(0, 0x83, L2[0] - EBR2, L2[1])])


def linha(inst, k):
    """A k-ésima partição da lista do editor (MODO_Y(3) + 4 + 12, de 20 em 20)."""
    y0 = 72 + 3 * (52 if inst.compacto else 66) + 4 + 12
    inst.clicar(inst.x0 + 120, y0 + k * 20 + 8)


def main():
    os.makedirs(SAIDA, exist_ok=True)
    hd = os.path.join(SAIDA, "h114-hd.img")
    criar(hd, TOTAL, monta)
    antes = open(hd, "rb").read()
    original = hashlib.sha256(antes).hexdigest()

    with Maquina("h114a", "ide", memoria="768M", extras=["-drive", f"format=raw,file={hd},if=ide,index=1"]) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        inst, n = abrir(m)
        ate_particoes(m, inst, n, 1)
        verificar("instalar: espaço livre: não (o maior espaço livre tem 10 MB" in m.serial()[n:], "só 10 MB livres: o espaço livre é recusado")
        inst.modo(2)
        esperar(m, "instalar: modo editor", 10, n)
        time.sleep(0.5)
        inst.continuar()
        time.sleep(1)
        verificar("instalar: passo 3" not in m.serial()[n:], "no editor, sem nada marcado, o Continuar não avança")
        k = len(m.serial())
        linha(inst, 0)
        esperar(m, "instalar: partição 1 sai", 10, k)
        linha(inst, 0)
        esperar(m, "instalar: partição 1 fica", 10, k)
        t = m.serial()[k:]
        verificar(t.index("partição 1 sai") < t.index("partição 1 fica"), "um clique marca a FAT32 para sair, outro desmarca")
        k = len(m.serial())
        linha(inst, 2)   # a lógica 5
        esperar(m, "instalar: partição 2 sai", 10, k)
        verificar("instalar: editor: SaguiOS em 100 MB, 300 MB" in m.serial()[k:],
                  "um clique na lógica marca a estendida inteira, e o SaguiOS vai no lugar dela (100 MB, 300 MB)")
        time.sleep(0.6)
        m.captura("H114-1-editor")
        inst.continuar()
        esperar(m, "instalar: passo 3 (Resumo)", 10, n)
        time.sleep(0.6)
        m.captura("H114-2-resumo")
        k = len(m.serial())
        inst.continuar()   # Instalar
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, k)
        caixa = m.serial()[k:]
        verificar("excluir a parti" in caixa and "2 de hd1 (e as l" in caixa and "gicas 5 a 6)" in caixa,
                  "a caixa do kernel diz que a partição 2 sai com as lógicas 5 e 6")
        time.sleep(0.6)
        m.captura("H114-3-confirmacao")
        m.tecla("esc")
        esperar(m, "instalar: liberar hd1 -> -14", 10, k)
        time.sleep(1)
        verificar(sha(hd) == original, "Esc na caixa do kernel: o disco inteiro está byte a byte igual")
        k = len(m.serial())
        inst.continuar()
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, k)
        time.sleep(0.5)
        m.digitar("hd1\n", 0.05)
        esperar(m, "instalar: pronto em hd1", 240, k)
        verificar("editor" in m.serial()[k:] and "instalar: erro" not in m.serial()[k:], "o instalador terminou sem erro, no editor")
        time.sleep(1)
        m.tecla("ret")
        esperar(m, "sagui:/$", 10, k)

    d = open(hd, "rb").read()
    o = open(IMAGEM, "rb").read()
    mbr = d[:S]
    e = [struct.unpack_from("<BBBBBBBBII", mbr, 446 + k * 16) for k in range(4)]
    s_ini = 100 * MB
    verificar(mbr[446:462] == antes[446:462], "a entrada 1 (a FAT32 WINDOWS, ativa) está byte a byte igual")
    verificar(e[1][4] == 0xDA and e[1][8] == s_ini and e[1][0] == 0, f"entrada 2, a da estendida: o sistema em 100 MB ({e[1]})")
    verificar(e[2][4] == 0x0C and e[2][8] == e[1][8] + e[1][9] and e[2][8] + e[2][9] <= TOTAL, f"entrada 3: a FAT32 do SaguiOS ({e[2]})")
    verificar(mbr[494:510] == bytes(16), "a entrada 4 continua vazia")
    verificar(mbr[440:446] == antes[440:446], "o identificador do disco ficou")
    verificar(mbr[:0x1B4] == o[:0x1B4] and struct.unpack_from("<I", mbr, 0x1B4)[0] == s_ini, "o código de boot é o do SaguiOS")
    verificar(d[S:63 * S] == antes[S:63 * S], "os setores 1 a 62 estão iguais")
    verificar(d[P1[0] * S:(P1[0] + P1[1]) * S] == antes[P1[0] * S:(P1[0] + P1[1]) * S], "a FAT32 WINDOWS está byte a byte igual")
    fim = e[2][8] + e[2][9]
    verificar(d[S:s_ini * S] == antes[S:s_ini * S] and d[fim * S:] == antes[fim * S:],
              "todo o disco fora do setor 0 e da região do SaguiOS está byte a byte igual")
    for nome, (a, q) in (("FAT32 WINDOWS", P1), ("FAT32 do SaguiOS", (e[2][8], e[2][9]))):
        tmp = os.path.join(SAIDA, "h114-fsck.img")
        with open(tmp, "wb") as g:
            g.write(d[a * S:(a + q) * S])
        r = subprocess.run(["fsck.vfat", "-n", tmp], capture_output=True, text=True)
        verificar(r.returncode == 0, f"{nome}: fsck.vfat -n passa")
        if nome.endswith("SaguiOS"):
            r = subprocess.run(["mtype", "-i", tmp, "::/.saguios/mbr-antes.bin"], capture_output=True)
            verificar(r.stdout == antes[:S], "o setor 0 de antes está guardado em .saguios/mbr-antes.bin")
        os.remove(tmp)

    with Maquina("h114b", "ide", imagem=hd, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("tem configuracoes do SaguiOS: montado sozinho" in m.serial(), "ligado pelo disco, a FAT32 do SaguiOS monta sozinha")
        out = m.comando("cat /usb/.saguios/configuracoes.txt")
        verificar("sistema.instalado = sim" in out, "e as configurações dizem que o sistema está instalado")


if __name__ == "__main__":
    main()
