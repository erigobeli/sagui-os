#!/usr/bin/env python3
"""H44: cada processo tem seu estado de x87 e SSE."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402


def main():
    esperado = int(sum(1.0 / i for i in range(1, 6000001)) * 1000)
    with Maquina("h44", "ide", imagem=imagem_de_teste("/testes/fpu")) as m:
        m.esperar_serial("[TESTE] programa terminou", 120)
        s = m.serial()
        verificar("fpu: x87 e SSE ligados" in s, "kernel ligou a FPU")
        verificar(f"fpu 1: {esperado}" in s, f"processo 1 calculou {esperado}")
        verificar(f"fpu 2: {esperado * 2}" in s or f"fpu 2: {esperado * 2 + 1}" in s, "processo 2 não foi contaminado")
    return 0


if __name__ == "__main__":
    sys.exit(main())
