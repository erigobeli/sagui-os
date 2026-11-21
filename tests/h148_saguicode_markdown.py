#!/usr/bin/env python3
"""H148 - Sagui Code (janela): a resposta do modelo sai formatada (Markdown).

Usa o servidor de IA falso do H69 (com uma chave FALSA no pendrive de teste).
A resposta tem um título, negrito, código, uma lista e uma tabela.

Critérios:
  - a resposta sai formatada: 1 título, negrito, código, 2 itens de lista
    e as 4 células da tabela (o registro diz o que saiu);
  - os ** e # não aparecem: a primeira linha é o título grande;
  - arrastar sobre a palavra em negrito seleciona e Ctrl+C copia só a
    palavra ("negrito", sem os **): a seleção segue o texto original;
  - Ctrl+A e Ctrl+C na conversa copiam o Markdown original (com os **).
"""
import os
import sys
import socketserver
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, janela  # noqa: E402
import h69_h70_saguicode as h69  # noqa: E402

RESPOSTA = ("# Titulo\n\nUm texto com **negrito** e `codigo`.\n\n- item um\n- item dois\n\n"
            "| a | b |\n|---|---|\n| 1 | 2 |\n")


def roteiro(corpo):
    usuario = [m for m in corpo["messages"] if m["role"] == "user"][-1]["content"]
    if "frase" in usuario:
        return h69.texto_em_pedacos(RESPOSTA)
    return h69.texto_em_pedacos("Recebi.")


h69.roteiro = roteiro


def main():
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), h69.Atendente)
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    pen = criar_pendrive("h148-pen", pastas=(".saguios", "projetos"), arquivos={
        ".saguios/chaves.txt": f"openrouter = {h69.CHAVE}\n",
        ".saguios/configuracoes.txt": f"ia.servidor = http://10.0.2.2:{porta}/api/v1\nia.modelo = anthropic/claude-teste\n",
    })
    try:
        with Maquina("h148", "ide", extras=h69.REDE + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(2)
            antes = len(m.serial())
            m.clique(40, 12)
            time.sleep(0.5)
            m.clique(44 + 1 * 108 + 54, 134 + 3 * 92 + 30)   # o Sagui Code no menu Iniciar (como no H89)
            h69.esperar(m, "saguicode-janela: pronta", 30, antes)
            time.sleep(1)
            jan = janela(m.serial(), "Sagui Code")
            entrada = (jan["x"] + 222 + 60, jan["y"] + jan["a"] - 8 - 36)
            antes = len(m.serial())
            m.clique(*entrada)
            m.digitar("diga a frase\n", 0.05)
            s = h69.esperar(m, "saguicode-janela: resposta formatada", 60, antes)
            verificar("titulos 1, negrito 1, italico 0, codigo 1, links 0, itens 2, celulas 4" in s,
                      "a resposta saiu formatada: título, negrito, código, 2 itens e 4 células")
            h69.esperar(m, "saguicode-janela: sessao 0001 salva", 60, antes)
            time.sleep(1)
            m.captura("H148-1-resposta-formatada")

            # O parágrafo fica abaixo do título (36 de altura, 2x) e da linha
            # dele (+8): 44 pixels abaixo do começo do texto da resposta.
            tx = jan["x"] + 222 + 10 + 10
            ty = jan["y"] + 52 + 1 + 64 + 2 + 17 + 44 + 8
            antes = len(m.serial())
            m.arrastar(tx + 13 * 8 + 3, ty, tx + 20 * 8 - 3, ty)   # "negrito": da letra 13 ao meio da 19 (as bordas da palavra têm os ** escondidos)
            h69.esperar(m, "saguicode-janela: selecao marcada", 10, antes)
            time.sleep(0.5)
            m.captura("H148-2-selecao")
            m.tecla("ctrl-c")
            s = h69.esperar(m, "saguicode-janela: copiou a selecao", 10, antes)
            verificar("copiou a selecao (7 letras)" in s, "arrastar sobre a palavra em negrito copia \"negrito\" (7 letras, sem os **)")

            m.clique(tx + 100, ty)
            antes = len(m.serial())
            m.tecla("ctrl-a")
            m.tecla("ctrl-c")
            s = h69.esperar(m, "saguicode-janela: copiou a selecao", 10, antes)
            total = len("diga a frase") + len(RESPOSTA) + 2
            verificar(f"copiou a selecao ({total} letras)" in s,
                      f"Ctrl+A e Ctrl+C copiam a conversa com o Markdown original ({total} letras)")
            verificar(h69.CHAVE not in m.serial(), "a chave não aparece no log")
    finally:
        servidor.shutdown()


if __name__ == "__main__":
    main()
