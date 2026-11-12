#!/usr/bin/env python3
"""H45: a placa AC'97 toca as amostras (o QEMU grava a saída num WAV)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402


def main():
    arq = os.path.join(SAIDA, "h45.wav")
    extras = ["-audiodev", f"wav,id=a0,path={arq}", "-device", "AC97,audiodev=a0"]
    with Maquina("h45", "ide", imagem=imagem_de_teste("/testes/tom"), extras=extras) as m:
        m.esperar_serial("tom: fim", 60)
        verificar("ac97: placa" in m.serial(), "kernel achou a placa")
    # O QEMU pode morrer sem fechar o cabeçalho: lê direto, 44,1 kHz estéreo.
    with open(arq, "rb") as f:
        q = f.read()[44:]
    taxa = 44100
    a = [int.from_bytes(q[i:i + 2], "little", signed=True) for i in range(0, len(q) - 1, 4)]
    ativos = [x for x in a if abs(x) > 500]
    troca = sum(1 for i in range(1, len(ativos)) if (ativos[i] > 0) != (ativos[i - 1] > 0))
    freq = troca / 2 / (len(ativos) / taxa) if ativos else 0
    print(f"  wav: {len(a)} quadros, {len(ativos)} com som, {freq:.0f} Hz")
    verificar(len(ativos) / taxa > 1.5, "gravou mais de 1,5 s de som")
    verificar(900 < freq < 1100, "frequência perto de 1 kHz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
