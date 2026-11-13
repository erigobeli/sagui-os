#!/usr/bin/env python3
"""H80 - ponto flutuante nos programas.

Critério: dois programas fazem contas em double e float ao mesmo tempo,
cedendo a vez um ao outro a cada passo; cada um confere que os registradores
não foram estragados pelo outro e chega ao mesmo resultado de uma conta sem
trocas (conferido aqui com o Python). O diário mostra a FPU ligada.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402


def esperado(n):
    fator = 1.0 + n * 0.5
    s = 0.0
    for k in range(1, 2001):
        s += fator / (float(k) * float(k))
    f = 0.0
    for k in range(1, 2001):
        f = struct.unpack("f", struct.pack("f", f + struct.unpack("f", struct.pack("f", 1.0 / k))[0]))[0]
    inteiro = int(s)
    frac = int((s - inteiro) * 1000000.0 + 0.5)
    return f"flutuante {n}: ok {inteiro}.{frac:06d}", f


def main():
    with Maquina("h80", "ide", memoria="512M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        verificar("[FPU] ponto flutuante ligado" in m.serial(), "o kernel ligou o ponto flutuante (FXSAVE por processo)")
        m.comando("/testes/flutuante 1 &", tempo=10)
        m.comando("/testes/flutuante 3 &", tempo=10)
        out = m.comando("/testes/flutuante 2", tempo=60)
        time.sleep(3)
        s = m.serial()
        for n in (1, 2, 3):
            texto, _ = esperado(n)
            verificar(texto in s, f"processo {n}: {texto.split('ok ')[1]} (igual à conta sem trocas)")
        verificar("ESTRAGADO" not in s and "ERRO" not in s, "nenhum processo teve os registradores estragados pelos outros")
    return 0


if __name__ == "__main__":
    sys.exit(main())
