#!/usr/bin/env python3
"""H41 - som para os programas: mistura e volume.

Critério: dois programas tocando ao mesmo tempo (440 Hz e 880 Hz) aparecem
misturados no .wav gravado pelo QEMU; com o volume em 0, o tom de 660 Hz
não aparece; a aba Som das Configurações muda o volume.
"""
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, SAIDA, janela  # noqa: E402
from h40_som import som_qemu, ler_wav  # noqa: E402


def energia(trecho, taxa, f):
    """Força da frequência f no trecho (algoritmo de Goertzel)."""
    k = 2 * math.cos(2 * math.pi * f / taxa)
    s1 = s2 = 0.0
    for x in trecho:
        s0 = x + k * s1 - s2
        s2, s1 = s1, s0
    return math.sqrt(max(0.0, s1 * s1 + s2 * s2 - k * s1 * s2)) / len(trecho)


def main():
    wav = os.path.join(SAIDA, "h41.wav")
    with Maquina("h41", "ide", extras=som_qemu(wav)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        m.comando("som teste 440 2500 &")
        m.comando("som teste 880 1500", tempo=30)
        time.sleep(1.5)
        m.comando("som volume 0")
        m.comando("som teste 660 1200", tempo=30)
        out = m.comando("som volume 80")
        verificar("Volume: 80%" in out, "som volume muda o volume")
        m.comando("configuracoes som")
        m.esperar_serial("configuracoes: aberto na aba Som", 15)
        time.sleep(0.8)
        j = janela(m.serial(), "Configurações")
        m.clique(j["x"] + 190 + 260 + 18, j["y"] + 80 + 14)   # "+"
        m.esperar_serial("configuracoes: volume 90", 10)
        verificar(True, "o botão + da aba Som sobe o volume")
        time.sleep(0.5)
        m.captura("H41-aba-som")
    amostras, taxa = ler_wav(wav)
    jan = taxa // 10
    juntos = silencio_660 = 0
    fim_mistura = 0
    for k in range(0, len(amostras) - jan, jan):
        t = amostras[k:k + jan]
        if energia(t, taxa, 440) > 500 and energia(t, taxa, 880) > 500:
            juntos += 1
            fim_mistura = k
    # Depois da mistura (o som de início do sistema tem um mi de 659 Hz, então
    # só conta o que vem depois): com o volume em 0, nada de 660 Hz.
    for k in range(fim_mistura + jan, len(amostras) - jan, jan):
        if energia(amostras[k:k + jan], taxa, 660) > 300:
            silencio_660 += 1
    verificar(juntos >= 8, f"440 Hz e 880 Hz tocando juntos em {juntos} pedaços de 0,1 s (mistura de dois programas)")
    verificar(silencio_660 == 0, "com o volume em 0, o tom de 660 Hz não aparece no .wav")
    return 0


if __name__ == "__main__":
    sys.exit(main())
