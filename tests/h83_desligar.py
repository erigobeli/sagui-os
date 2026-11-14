#!/usr/bin/env python3
"""H83 (do pull request #4) - poweroff e a tela "pode desligar".

Critério: `poweroff` é aceito como o `desligar`; numa máquina sem ACPI
(QEMU com -machine acpi=off), o SaguiOS mostra em tela cheia que já pode
desligar o computador, em vez de deixar a área de trabalho parada.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, cor_do_pixel  # noqa: E402


def main():
    with Maquina("h83d", "ide", extras=["-machine", "pc,acpi=off"]) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        out = m.comando("ajuda poweroff", tempo=20)
        verificar("desligar" in out, "ajuda conhece o poweroff (apelido desligar)")
        m.digitar("poweroff", 0.04)
        m.tecla("ret")
        m.esperar_serial("[SAGUI] tela: pode desligar", 30)
        time.sleep(0.5)
        png = m.captura("H83-1-pode-desligar")
        verificar("desligar pelo ACPI nao funcionou" in m.serial(), "sem ACPI, o desligar caiu na tela de aviso")
        # O fundo é escuro e o aviso amarelo está no meio.
        verificar(sum(cor_do_pixel(png, 20, 20)) < 80, "tela inteira escura (a área de trabalho saiu)")
        meio = [cor_do_pixel(png, x, 768 // 2 + 18) for x in range(380, 650)]
        verificar(any(c[0] > 200 and c[1] > 180 and c[2] < 120 for c in meio), "a frase amarela \"terminou com segurança\" no meio")
    return 0


if __name__ == "__main__":
    sys.exit(main())
