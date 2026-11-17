#!/usr/bin/env python3
"""H115 - pasta-de (dirname): mostra o diretório de um caminho.

Critério: cada caminho de entrada dá o diretório esperado; erros saem em
português e com código de saída diferente de zero quando não há argumento. """
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from h91_utilitarios import rodar as rodar_util  # noqa: E402
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, RAIZ  # noqa: E402

# Casos: comando -> saída esperada. O último é um erro (sem argumento).
CASOS = (
    ("pasta-de /a/b/c.txt", "/a/b\n"),
    ("pasta-de /a/b/", "/a\n"),
    ("pasta-de c.txt", ".\n"),
    ("pasta-de /", "/\n"),
    ("pasta-de a/b", "a\n"),
    ("pasta-de /usr/bin/gcc", "/usr/bin\n"),
    ("pasta-de somentearquivo", ".\n"),
)


def rodar(m, comando):
    """O que o comando escreveu, sem o eco, sem o prompt e sem as linhas de
    registro do kernel ("[REDE] ...", "[GUI] ...") que saem na mesma serial."""
    return rodar_util(m, comando)


def main():
    pen = criar_pendrive("h115-pen", rotulo="TEXTOS")
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h115", "ide", extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        for comando, esperado in CASOS:
            out = rodar(m, comando)
            verificar(out == esperado,
                      f"{comando!r} -> {esperado!r}" + ("" if out == esperado else f" (veio {out!r})"))
        # Sem argumento: uso e código diferente de zero.
        out = rodar(m, "pasta-de")
        verificar("uso: pasta-de CAMINHO" in out, "pasta-de sem argumento: uso em português")
        # código de saída (pelo log do kernel)
        import re
        achados = re.findall(r"\(dirname\) terminou com codigo (-?\d+)", m.serial())
        verificar(achados and int(achados[-1]) == 1,
                  "pasta-de sem argumento: código de saída 1 (veio %s)" % (achados[-1] if achados else "nenhum"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
