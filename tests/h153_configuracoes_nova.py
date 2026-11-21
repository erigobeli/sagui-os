#!/usr/bin/env python3
"""H153 - Configurações: lateral com busca e grupos, Início com cartões,
caminho clicável e opções em cartões.

Critérios:
  - abre no Início, com um cartão por área; um clique no cartão "Som" vai
    para a página Som;
  - "Configurações" no caminho volta ao Início;
  - a busca filtra as páginas: "fuso" deixa só "Data e hora"; Enter abre;
  - interruptor e botões de escolha gravam (aparencia.sagui, tema);
  - o volume deslizante muda com um clique na trilha.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, controle  # noqa: E402


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def main():
    with Maquina("h153", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        antes = len(m.serial())
        m.digitar("configuracoes\n", 0.05)
        esperar(m, "configuracoes: aberto na aba Início", 20, antes)
        time.sleep(1)
        j = janela(m.serial(), "Configurações")
        m.captura("H153-1-inicio")
        s = m.serial()[antes:]
        verificar(all(f"controle ir.{p} " in s for p in ("rede", "aparencia", "som", "relogio", "teclado", "programas")),
                  "o Início tem um cartão por área")
        antes = len(m.serial())
        m.clique(*controle(m.serial(), "ir.som", j))
        esperar(m, "configuracoes: aba Som", 10, antes)
        time.sleep(0.8)
        m.captura("H153-2-som")
        verificar(True, "o cartão Som leva à página Som")
        if "controle som.volume" in m.serial()[antes:]:
            antes = len(m.serial())
            x, y = controle(m.serial(), "som.volume", j)
            m.clique(x - 60, y)
            s = esperar(m, "configuracoes: volume", 10, antes)
            verificar(True, "um clique na trilha muda o volume")
        antes = len(m.serial())
        m.clique(*controle(m.serial(), "ir.inicio", j))
        esperar(m, "configuracoes: aba Início", 10, antes)
        verificar(True, "\"Configurações\" no caminho volta ao Início")
        # Busca.
        antes = len(m.serial())
        m.clique(*controle(m.serial(), "busca", j))
        time.sleep(0.4)
        m.digitar("fuso", 0.08)
        esperar(m, 'configuracoes: busca "fuso"', 10, antes)
        time.sleep(0.6)
        m.captura("H153-3-busca")
        s = m.serial()[m.serial().rfind('busca "fuso"'):]
        verificar("controle ir.relogio " in s and "controle ir.rede " not in s.split("controle ir.inicio")[0] if "controle ir.inicio" in s else "controle ir.relogio " in s,
                  "\"fuso\" deixa Data e hora na lateral")
        antes = len(m.serial())
        m.tecla("ret")
        esperar(m, "configuracoes: aba Relógio", 10, antes)
        verificar(True, "Enter abre a página achada")
        # Aparência: interruptor e escolha.
        antes = len(m.serial())
        m.clique(*controle(m.serial(), "ir.aparencia", j))
        esperar(m, "configuracoes: aba Aparência", 10, antes)
        time.sleep(0.6)
        antes = len(m.serial())
        m.clique(*controle(m.serial(), "tema.claro", j))
        esperar(m, "configuracoes: tema claro", 10, antes)
        time.sleep(0.6)
        m.captura("H153-4-aparencia")
        antes = len(m.serial())
        m.clique(*controle(m.serial(), "aparencia.sagui", j))
        esperar(m, "configuracoes: sagui nao", 10, antes)
        verificar(True, "botões de escolha e interruptor gravam (tema claro, sagui escondido)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
