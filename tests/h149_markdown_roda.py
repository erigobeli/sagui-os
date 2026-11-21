#!/usr/bin/env python3
"""H149 - a roda do mouse no texto formatado (Markdown) e na conversa do
Sagui Code.

Critérios:
  - no Leitor: 3 passos para baixo rolam 3 linhas por passo (162 pixels),
    e 3 para cima voltam ao começo;
  - no Histórico da janela Sobre (o mesmo componente): a roda rola também;
  - na conversa do Sagui Code, a roda rola (para cima, numa conversa longa).
"""
import os
import re
import socketserver
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from qemu import Maquina, FalhaTeste, verificar, janela, criar_pendrive, pendrive_qemu  # noqa: E402
from h134_roda import girar  # noqa: E402
import h69_h70_saguicode as h69  # noqa: E402

LONGA = "\n\n".join(f"Parágrafo {k}: " + "texto " * 30 for k in range(40))


def roteiro(corpo):
    return h69.texto_em_pedacos(LONGA)


h69.roteiro = roteiro


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def topos(s):
    return [int(t) for t in re.findall(r"markdown: roda -?\d+, topo (\d+)", s)]


def main():
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), h69.Atendente)
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    pen = criar_pendrive("h149-pen", pastas=(".saguios", "projetos"), arquivos={
        ".saguios/chaves.txt": f"openrouter = {h69.CHAVE}\n",
        ".saguios/configuracoes.txt": f"ia.servidor = http://10.0.2.2:{porta}/api/v1\nia.modelo = anthropic/claude-teste\n",
    })
    try:
        with Maquina("h149", "ide", extras=h69.REDE + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(1)

            # Leitor
            n = len(m.serial())
            m.digitar("leitor /sistema/ajuda/c-do-sagui.md\n", 0.03)
            esperar(m, "leitor: titulo", 30, n)
            time.sleep(1)
            jl = janela(m.serial(), "c-do-sagui.md - Leitor")
            m.mouse_para(jl["x"] + 300, jl["y"] + 300)
            n = len(m.serial())
            girar(m, 3)
            t = topos(m.serial()[n:])
            verificar(t == [54, 108, 162], f"Leitor: 3 passos para baixo, 3 linhas (54 px) por passo ({t})")
            n = len(m.serial())
            girar(m, -3)
            t = topos(m.serial()[n:])
            verificar(t and t[-1] == 0, f"e 3 para cima voltam ao começo ({t})")
            m.tecla("esc")
            time.sleep(1)

            # Histórico, na janela Sobre
            n = len(m.serial())
            m.digitar("sobre-o-sistema historico\n", 0.03)
            esperar(m, "historico: pronto", 30, n)
            time.sleep(1)
            jh = janela(m.serial(), "Histórico de versões")
            m.mouse_para(jh["x"] + 300, jh["y"] + 300)
            n = len(m.serial())
            girar(m, 2)
            t = topos(m.serial()[n:])
            verificar(t == [54, 108], f"Histórico: a roda rola o texto formatado ({t})")
            m.tecla("esc")
            time.sleep(1)

            # Sagui Code: uma resposta longa; a conversa segue o fim, a roda sobe
            antes = len(m.serial())
            m.clique(40, 12)
            time.sleep(0.5)
            m.clique(44 + 1 * 108 + 54, 134 + 3 * 92 + 30)
            h69.esperar(m, "saguicode-janela: pronta", 30, antes)
            time.sleep(1)
            jan = janela(m.serial(), "Sagui Code")
            entrada = (jan["x"] + 222 + 60, jan["y"] + jan["a"] - 8 - 36)
            antes = len(m.serial())
            m.clique(*entrada)
            m.digitar("conte uma historia longa\n", 0.05)
            h69.esperar(m, "saguicode-janela: sessao 0001 salva", 60, antes)
            time.sleep(1)
            m.mouse_para(jan["x"] + 500, jan["y"] + 200)
            antes = len(m.serial())
            girar(m, -2)
            r = [int(x) for x in re.findall(r"saguicode-janela: roda -1, rolagem (\d+)", m.serial()[antes:])]
            verificar(len(r) == 2 and r[0] - r[1] == 51, f"Sagui Code: 2 passos para cima sobem a conversa 3 linhas cada ({r})")
            m.captura("H149-saguicode-roda")
    finally:
        servidor.shutdown()


if __name__ == "__main__":
    main()
