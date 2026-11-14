#!/usr/bin/env python3
"""H79 (correções do G470) - Ctrl+C encerra o programa que está rodando.

No G470, o `tocar` ficou parado em "Tocando..." (a placa de som não andava) e
o Ctrl+C não fazia nada: só o shell lia a tecla, e só quando esperava uma
linha. Critério: com o `contador 100` rodando no Terminal, Ctrl+C mostra ^C,
volta ao prompt e o contador some da lista de processos; no prompt, Ctrl+C
continua só descartando a linha.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402


def main():
    with Maquina("h79c", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        antes = m.serial().count("sagui:")
        m.digitar("contador 100", 0.04)
        m.tecla("ret")
        m.esperar_serial("de 100 segundos", 30)
        m.tecla("ctrl-c")
        fim = time.time() + 15
        while m.serial().count("sagui:") <= antes and time.time() < fim:
            time.sleep(0.2)
        verificar(m.serial().count("sagui:") > antes, "Ctrl+C devolve o prompt com o programa rodando")
        verificar("Ctrl+C encerrou contador" in m.serial(), "o terminal encerrou o contador")
        out = m.comando("ps")
        verificar("contador" not in out, "o contador não está mais na lista de processos")
        # No prompt, Ctrl+C só descarta a linha (o shell continua vivo).
        m.digitar("abc", 0.04)
        m.tecla("ctrl-c")
        out = m.comando("ola")
        verificar("Ol" in out, "o shell segue funcionando depois do Ctrl+C")
        m.captura("H79-ctrl-c")
    return 0


if __name__ == "__main__":
    sys.exit(main())
