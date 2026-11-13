#!/usr/bin/env python3
"""H38 - app Configurações.

Critério: o ícone Configurações abre o app; na aba Relógio, acertar a data e
a hora muda o relógio da placa-mãe (o comando data mostra a hora nova); na
aba Aparência, trocar o fundo redesenha a área de trabalho; a aba Sobre o
computador mostra processador, memória e dispositivos. Capturas de cada aba.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, cor_do_pixel  # noqa: E402

POR_COLUNA = (768 - 40) // 92


def icone(i):
    return 20 + (i // POR_COLUNA) * 110 + 48, 40 + (i % POR_COLUNA) * 92 + 32


def escrever_caixa(m, x, y, texto):
    m.clique(x + 10, y + 10)
    m.tecla("end")
    for _ in range(6):
        m.tecla("backspace", 0.03)
    m.digitar(texto)


def main():
    with Maquina("h38", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.clique(*icone(6), duplo=True)
        m.esperar_serial("configuracoes: aberto na aba Relógio", 15)
        verificar(True, "o ícone Configurações abre o app")
        time.sleep(0.6)
        j = janela(m.serial(), "Configurações")
        x0, y0 = j["x"], j["y"]
        m.captura("H38-1-relogio")
        X0 = 190
        for dx, valor in ((0, "25"), (56, "12"), (112, "2026"), (200, "10"), (256, "30")):
            escrever_caixa(m, x0 + X0 + dx, y0 + 112, valor)
        m.clique(x0 + X0 + 75, y0 + 150 + 14)
        m.esperar_serial("acertado para 25/12/2026 10:30", 10)
        verificar(True, "Acertar o relógio grava a data no relógio da placa-mãe")
        time.sleep(0.5)
        m.clique(x0 + 10 + 65, y0 + 16 + 40 + 15)              # aba Aparência
        m.esperar_serial("configuracoes: aba Aparência", 10)
        time.sleep(0.4)
        m.clique(x0 + X0 + 60, y0 + 174 + 60 + 12)            # Noite
        m.esperar_serial("configuracoes: fundo noite", 10)
        time.sleep(1.5)
        png = m.captura("H38-2-aparencia-noite")
        verificar(sum(cor_do_pixel(png, 700, 700)) < 120, "o fundo da área de trabalho ficou escuro (tema Noite)")
        m.clique(x0 + 10 + 65, y0 + 16 + 200 + 15)             # aba Sobre o computador
        m.esperar_serial("configuracoes: aba Sobre o computador", 10)
        time.sleep(0.8)
        m.captura("H38-3-sobre")
        verificar("configuracoes: processador QEMU" in m.serial(), "a aba Sobre mostra o processador e a memória")
        # Fecha pelo X; o Terminal volta a ter o foco.
        m.clique(j["fx"] + j["l"] + 4 - 2 - 20 + 8, j["fy"] + 12)
        m.esperar_serial("fechar janela", 10)
        time.sleep(0.6)
        out = m.comando("data")
        verificar("25/12/2026" in out and "10:3" in out, "o comando data mostra a data e a hora novas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
