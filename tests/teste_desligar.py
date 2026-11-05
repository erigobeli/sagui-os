#!/usr/bin/env python3
"""Teste do desligamento do sistema (desligar)."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_console


def main():
    print("Iniciando teste de desligamento no QEMU...")
    with Maquina("teste_desligar", "ide", imagem=imagem_console()) as m:
        m.esperar_serial("[SAGUI] shell iniciado", 60)
        m.esperar_serial("sagui:/$", 20)

        # Digita o comando desligar no terminal
        print("Enviando comando 'desligar'...")
        m.digitar("desligar\n", 0.04)

        # Espera mensagem no log do kernel
        m.esperar_serial("[SAGUI] desligando o computador", 20)
        print("Log serial confirmou início do desligamento.")

        # O QEMU deve encerrar por ACPI poweroff
        desligou = False
        try:
            m.proc.wait(timeout=15)
            desligou = True
        except Exception:
            desligou = False

        verificar(desligou, "QEMU encerrou com sucesso via ACPI poweroff ao executar desligar")
        print("Sucesso: A maquina virtual foi desligada com sucesso!")
    return 0


if __name__ == "__main__":
    sys.exit(main())

