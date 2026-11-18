#!/usr/bin/env python3
"""H117 - disco-usado (du): mostra o espaço usado por arquivos e pastas.

Critério: du soma o tamanho dos arquivos de uma pasta (e de subpastas
recursivamente) e imprime "TAMANHO\tCAMINHO" por linha. Arquivo inexistente
dá erro em português com código diferente de zero. """
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from h91_utilitarios import rodar as rodar_util  # noqa: E402
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, RAIZ  # noqa: E402

# Arquivos com tamanhos conhecidos (bytes).
ARQUIVOS = {
    "pequeno.txt": "oi\n",                              # 3 bytes
    "medio.txt": "A" * 100,                             # 100 bytes
    "sub/nested.txt": "B" * 250,                        # 250 bytes
    "sub/outro.txt": "C" * 50,                          # 50 bytes
    "vazio.txt": "",                                    # 0 bytes
}
PASTAS = ("sub",)


def rodar(m, comando):
    """O que o comando escreveu, sem o eco, sem o prompt e sem as linhas de
    registro do kernel ("[REDE] ...", "[GUI] ...") que saem na mesma serial."""
    return rodar_util(m, comando)


def codigo(m, programa):
    achados = re.findall(rf"\({re.escape(programa)}\) terminou com codigo (-?\d+)", m.serial())
    return int(achados[-1]) if achados else None


def main():
    pen = criar_pendrive("h117-pen", rotulo="TEXTOS", arquivos=ARQUIVOS, pastas=PASTAS)
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h117", "ide", extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        m.comando("montar")
        m.comando("cd /usb")

        # Arquivo simples: tamanho exato.
        out = rodar(m, "du pequeno.txt")
        verificar(out == "3\tpequeno.txt\n",
                  f"du pequeno.txt -> 3 (veio {out!r})")

        # Vários arquivos.
        out = rodar(m, "du medio.txt vazio.txt")
        esperado = "100\tmedio.txt\n0\tvazio.txt\n"
        verificar(out == esperado,
                  f"du medio.txt vazio.txt -> {esperado!r} (veio {out!r})")

        # Pasta com subpastas: soma recursiva.
        out = rodar(m, "du sub")
        verificar(out == "300\tsub\n",
                  f"du sub -> 300 (veio {out!r})")

        # Pasta atual (somente o que tem em /usb, sem o próprio /usb).
        out = rodar(m, "du")
        verificar("403\t.\n" in out,
                  f"du (sem argumento) inclui 403 em . (veio {out!r})")

        # Arquivo que não existe: erro em português e código diferente de zero.
        out = rodar(m, "du naoexiste.txt")
        verificar("não consigo ler" in out and codigo(m, "du") == 1,
                  f"du inexistente: erro em português e código 1 (veio {out!r})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
