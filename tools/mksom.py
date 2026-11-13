#!/usr/bin/env python3
"""Gera rootfs/sons/sagui.wav: o "canto do sagui" do SaguiOS (H42).

Os saguis chamam uns aos outros com assobios agudos e curtos. Este som é
feito do zero por síntese: três assobios que descem de tom, cada um com
volume entrando e saindo em curva, e um trinado no fim. 22050 Hz, 8 bits,
mono (o arquivo fica pequeno; o tocador converte para a placa)."""
import math
import os
import struct
import sys

TAXA = 22050


def assobio(f0, f1, dur, vol=0.8, trinado=0.0):
    n = int(TAXA * dur)
    fase = 0.0
    saida = []
    for i in range(n):
        t = i / n
        f = f0 + (f1 - f0) * t + trinado * math.sin(2 * math.pi * 30 * i / TAXA) * f0
        fase += 2 * math.pi * f / TAXA
        envelope = math.sin(math.pi * t) ** 0.7
        saida.append(vol * envelope * math.sin(fase))
    return saida


def silencio(dur):
    return [0.0] * int(TAXA * dur)


def main():
    destino = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "rootfs", "sons", "sagui.wav")
    s = assobio(3400, 2600, 0.12) + silencio(0.05) + assobio(3200, 2400, 0.12) + silencio(0.05) \
        + assobio(3000, 2200, 0.14) + silencio(0.08) + assobio(2600, 2600, 0.30, 0.6, trinado=0.05)
    dados = bytes(max(0, min(255, int(128 + 120 * v))) for v in s)
    with open(destino, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(dados)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, TAXA, TAXA, 1, 8))
        f.write(b"data" + struct.pack("<I", len(dados)) + dados)


if __name__ == "__main__":
    main()
