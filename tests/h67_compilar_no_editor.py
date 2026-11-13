#!/usr/bin/env python3
"""H67 - compilar dentro do sistema, pelo Editor.

Critério (no QEMU, sem sair do SaguiOS): digitar um programa no Editor com
um erro, apertar "Compilar e rodar" e ver a mensagem do compilador (linha,
coluna e dica) com o cursor levado à linha do erro; corrigir, compilar e o
programa roda; "Instalar" grava o programa em /usb/programas e o ícone dele
aparece na área de trabalho.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, criar_pendrive, pendrive_qemu, mtools  # noqa: E402


def esperar(m, texto, tempo=30, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


COM_ERRO = '#include "sagui.h"\n\nint principal(int argc, char **argv)\n{\n    imprimir("ola do editor\\n")\n    return 0;\n}\n'
CERTO = '#include "sagui.h"\n\nint principal(int argc, char **argv)\n{\n    imprimir("ola do editor\\n");\n    return 0;\n}\n'


def main():
    pen = criar_pendrive("h67-pen", pastas=(".saguios",))
    with Maquina("h67", "ide", extras=["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        m.comando("echo > /tmp/ola.c", tempo=20)            # o arquivo existe (vazio) para o Editor abrir
        antes = len(m.serial())
        m.digitar("editor /tmp/ola.c\n", 0.04)
        esperar(m, "editor: pronto", 30, antes)
        time.sleep(1)
        e = janela(m.serial(), "Editor - sem nome")
        m.clique(e["x"] + 200, e["y"] + 200)
        m.digitar(COM_ERRO, 0.04)
        antes = len(m.serial())
        m.clique(e["x"] + 340 + 70, e["y"] + 7 + 13)           # Compilar e rodar
        s = esperar(m, "editor: erro de compilacao", 60, antes)
        verificar("editor: salvo /tmp/ola.c" in s, "o Editor salvou antes de compilar")
        time.sleep(1)
        m.captura("H67-1-erro")
        verificar(True, "a compilação com o ; faltando deu erro, mostrado na janela de mensagem")
        m.tecla("ret", 0.2)                                     # OK da mensagem
        time.sleep(0.5)
        # Corrige: o cursor foi para a linha 3 (a do erro); End e o ";".
        m.tecla("end", 0.1)
        m.digitar(";", 0.1)
        antes = len(m.serial())
        m.clique(e["x"] + 340 + 70, e["y"] + 7 + 13)
        s = esperar(m, "editor: compilou e abriu no Terminal /tmp/ola", 60, antes)
        s = esperar(m, "ola do editor", 30, antes)
        verificar(True, "corrigido, compilou e o programa rodou (escreveu \"ola do editor\")")
        m.captura("H67-2-rodou")
        # Instalar no pendrive.
        antes = len(m.serial())
        m.clique(e["x"] + 488 + 40, e["y"] + 7 + 13)
        esperar(m, "editor: instalado /usb/programas/ola", 60, antes)
        m.tecla("ret", 0.2)
        esperar(m, "programas instalados no pendrive: 1", 30, antes)
        time.sleep(1)
        m.captura("H67-3-instalado")
        verificar(True, "Instalar gravou /usb/programas/ola e o ícone apareceu na área de trabalho")
    _, rc = mtools(pen, "mdir", "p:/programas/ola")
    verificar(rc == 0, "o programa está no pendrive")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
