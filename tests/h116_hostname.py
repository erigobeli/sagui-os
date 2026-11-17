#!/usr/bin/env python3
"""H116 - nome-maquina (hostname): mostra e muda o nome do computador.

Critério: sem argumento mostra o nome atual (padrão "sagui"); com argumento
muda o nome, guarda no pendrive (se houver) e aceita apenas letras, números
e hífen. Nomes inválidos saem em português com código diferente de zero. """
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from h91_utilitarios import rodar as rodar_util  # noqa: E402
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, RAIZ  # noqa: E402


def rodar(m, comando):
    """O que o comando escreveu, sem o eco, sem o prompt e sem as linhas de
    registro do kernel ("[REDE] ...", "[GUI] ...") que saem na mesma serial."""
    return rodar_util(m, comando)


def codigo(m, programa):
    achados = re.findall(rf"\({re.escape(programa)}\) terminou com codigo (-?\d+)", m.serial())
    return int(achados[-1]) if achados else None


def main():
    pen = criar_pendrive("h116-pen", rotulo="TEXTOS")
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h116", "ide", extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)

        # Sem argumento: mostra o padrão.
        out = rodar(m, "nome-maquina")
        verificar(out == "sagui\n", f"nome-maquina sem argumento -> sagui (veio {out!r})")

        # Muda para um nome válido.
        out = rodar(m, "nome-maquina meupc")
        verificar("nome-maquina: meupc" in out, f"nome-maquina meupc -> aceite (veio {out!r})")
        out = rodar(m, "nome-maquina")
        verificar(out == "meupc\n", f"nome-maquina agora mostra meupc (veio {out!r})")

        # Nome inválido: espaço não é permitido.
        out = rodar(m, "nome-maquina \"meu pc\"")
        verificar("use só letras, números e hífen" in out and codigo(m, "hostname") == 1,
                  "nome-maquina com espaço: erro em português e código 1")

        # Nome muito vazio.
        out = rodar(m, "nome-maquina \"\"")
        verificar("de 1 a 63 caracteres" in out and codigo(m, "hostname") == 1,
                  "nome-maquina vazio: erro e código 1")

        # Volta ao padrão para não poluir outros testes.
        rodar(m, "nome-maquina sagui")
    return 0


if __name__ == "__main__":
    sys.exit(main())
