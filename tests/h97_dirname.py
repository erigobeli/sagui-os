#!/usr/bin/env python3
"""H92 - pasta-de (dirname): mostra o diretório de um caminho.

Critério: cada caminho de entrada dá o diretório esperado; erros saem em
português e com código de saída diferente de zero quando não há argumento. """
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
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
    out = m.comando(comando)
    # remove o eco da linha digitada e o prompt seguinte
    out = out.split("\n", 1)[1] if "\n" in out else ""
    return out[:out.rfind("sagui:")] if "sagui:" in out else out


def main():
    pen = criar_pendrive("h92-pen", rotulo="TEXTOS")
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h92", "ide", extras=extras) as m:
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
        achados = re.findall(r"\(pasta-de\) terminou com codigo (-?\d+)", m.serial())
        verificar(achados and int(achados[-1]) == 1,
                  "pasta-de sem argumento: código de saída 1 (veio %s)" % (achados[-1] if achados else "nenhum"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
