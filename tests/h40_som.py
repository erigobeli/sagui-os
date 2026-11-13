#!/usr/bin/env python3
"""H40 - driver Intel HD Audio.

Critério: com o controlador intel-hda e o codec hda-duplex do QEMU, o kernel
acha o codec e uma saída; o comando "som teste 440" toca uma senoide que o
QEMU grava num .wav, e a frequência medida no arquivo é 440 Hz.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, SAIDA  # noqa: E402


def som_qemu(wav):
    return ["-audiodev", f"wav,id=som,path={wav}", "-device", "intel-hda", "-device", "hda-duplex,audiodev=som"]


def ler_wav(wav):
    """Amostras do canal esquerdo e a taxa. O QEMU só completa os tamanhos do
    cabeçalho ao fechar o arquivo; lê os campos fixos e as amostras direto."""
    bruto = open(wav, "rb").read()
    canais = struct.unpack("<H", bruto[22:24])[0]
    taxa = struct.unpack("<I", bruto[24:28])[0]
    dados = bruto[44:]
    dados = dados[:len(dados) // (2 * canais) * 2 * canais]
    return struct.unpack("<" + "h" * (len(dados) // 2), dados)[::canais], taxa


def frequencias(wav):
    """Frequência (pelos cruzamentos do zero) de cada pedaço de 0,1 s com som."""
    amostras, taxa = ler_wav(wav)
    jan = taxa // 10
    saida = []
    for k in range(0, len(amostras) - jan, jan):
        trecho = amostras[k:k + jan]
        if max(abs(a) for a in trecho) < 2000:
            continue
        cruz = sum(1 for a, b in zip(trecho, trecho[1:]) if a < 0 <= b)
        saida.append(cruz * taxa / jan)
    return saida


def main():
    wav = os.path.join(SAIDA, "h40.wav")
    with Maquina("h40", "ide", extras=som_qemu(wav)) as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        verificar("[HDA] codec" in s and "saida(s)" in s and "tocando" in s, "codec encontrado e saída configurada")
        time.sleep(1)
        out = m.comando("som")
        verificar("Placa de som HD Audio ligada" in out, "o comando som vê a placa")
        out = m.comando("som teste 440 1500", tempo=30)
        verificar("Tocando 440 Hz" in out, "som teste 440 tocou")
        m.captura("H40-som-teste")
        time.sleep(0.5)
    fs = frequencias(wav)
    verificar(len(fs) >= 10, f"o .wav gravado pelo QEMU tem som ({len(fs)} pedaços de 0,1 s)")
    no_tom = [f for f in fs if abs(f - 440) < 15]
    verificar(len(no_tom) >= 10, f"a frequência gravada é 440 Hz em {len(no_tom)} pedaços de 0,1 s (o tom tem 1,5 s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
