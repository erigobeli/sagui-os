#!/usr/bin/env python3
"""H123 - selecionar e copiar o texto da conversa do Sagui Code (janela).

Usa o servidor de IA falso do H69 (com uma chave FALSA no pendrive de teste).

Critérios:
  - arrastar o mouse sobre parte de uma resposta seleciona esse trecho (com
    destaque azul) e Ctrl+C o copia para a área de cópia do sistema;
  - colado no Terminal (Ctrl+Shift+V), sai exatamente o trecho arrastado;
  - na caixa do pedido, Ctrl+V cola o mesmo texto, e Ctrl+A e Ctrl+C copiam o
    que está na caixa (a seleção da conversa não rouba o Ctrl+C);
  - depois de clicar na conversa, Ctrl+A seleciona a conversa inteira e
    Ctrl+C copia (a caixa do pedido não rouba o Ctrl+C).
"""
import os
import sys
import socketserver
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, janela, cor_do_pixel, FalhaTeste  # noqa: E402
import h69_h70_saguicode as h69  # noqa: E402

RESPOSTA = "Resposta: copie-isto-456 e mais nada."
TRECHO = "copie-isto-456"                   # letras 10 a 23 da resposta


def roteiro(corpo):
    usuario = [m for m in corpo["messages"] if m["role"] == "user"][-1]["content"]
    if "frase" in usuario:
        return h69.texto_em_pedacos(RESPOSTA)
    return h69.texto_em_pedacos("Recebi.")


h69.roteiro = roteiro          # o Atendente do H69 procura o roteiro no módulo dele


def main():
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), h69.Atendente)
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    pen = criar_pendrive("h123-pen", pastas=(".saguios", "projetos"), arquivos={
        ".saguios/chaves.txt": f"openrouter = {h69.CHAVE}\n",
        ".saguios/configuracoes.txt": f"ia.servidor = http://10.0.2.2:{porta}/api/v1\nia.modelo = anthropic/claude-teste\n",
    })
    try:
        with Maquina("h123", "ide", extras=h69.REDE + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(2)
            term = janela(m.serial(), "Terminal")
            # Abre o Sagui Code pelo menu Iniciar (célula 16, como no H89).
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
            m.digitar("diga a frase\n", 0.05)
            h69.esperar(m, "saguicode-janela: sessao 0001 salva", 60, antes)
            time.sleep(1)

            # A conversa: o bloco do pedido (y 8, 48 de altura) e a resposta
            # (y 64), cujo texto começa uma linha (17) abaixo do rótulo. A
            # conversa começa em (222, 52) da janela, com 1 de moldura.
            tx = jan["x"] + 222 + 10 + 10
            ty = jan["y"] + 52 + 1 + 64 + 2 + 17 + 8
            png0 = m.captura("H123-antes", guardar=False)
            antes = len(m.serial())
            m.arrastar(tx + 10 * 8 + 1, ty, tx + 24 * 8 + 1, ty)
            h69.esperar(m, "saguicode-janela: selecao marcada", 10, antes)
            time.sleep(0.5)
            png = m.captura("H123-selecao")
            azul = sum(cor_do_pixel(png, tx + 10 * 8 + k, ty - 6) == (0x2A, 0x6F, 0xD8) for k in range(0, 14 * 8, 2))
            verificar(azul > 30, f"o trecho arrastado fica destacado em azul ({azul} pontos)")
            fora = sum(cor_do_pixel(png, tx + k, ty - 6) != cor_do_pixel(png0, tx + k, ty - 6) for k in range(0, 9 * 8))
            verificar(fora == 0, "o texto antes do trecho não mudou")
            m.tecla("ctrl-c")
            s = h69.esperar(m, "saguicode-janela: copiou a selecao", 10, antes)
            verificar(f"copiou a selecao ({len(TRECHO)} letras)" in s, f"Ctrl+C copiou as {len(TRECHO)} letras do trecho")
            verificar(f"area de copia: {len(TRECHO)} bytes" in s, "o kernel guardou o texto na área de cópia")

            # Cola no Terminal (a barra de título dele fica à vista à esquerda).
            antes = len(m.serial())
            m.clique(term["fx"] + 20, term["fy"] + 12)
            time.sleep(0.5)
            m.digitar("echo x", 0.04)
            m.tecla("ctrl-shift-v")
            m.tecla("ret")
            h69.esperar(m, "x" + TRECHO, 10, antes)
            verificar(True, "colado no Terminal, sai exatamente o trecho arrastado")

            # A caixa do pedido: Ctrl+V cola, Ctrl+A e Ctrl+C copiam o que
            # está nela (e não o que estava selecionado na conversa).
            m.clique(jan["fx"] + 400, jan["fy"] + 12)        # barra de título do Sagui Code
            time.sleep(0.5)
            m.clique(*entrada)
            m.tecla("ctrl-v")
            m.digitar(" fim", 0.05)
            antes = len(m.serial())
            m.tecla("ctrl-a")
            m.tecla("ctrl-c")
            s = h69.esperar(m, "area de copia:", 10, antes)
            verificar(f"area de copia: {len(TRECHO) + 4} bytes" in s and "copiou a selecao" not in s,
                      "na caixa do pedido, Ctrl+C copia o texto da caixa")
            antes = len(m.serial())
            m.tecla("ret")
            h69.esperar(m, "saguicode-janela: sessao 0001 salva", 60, antes)
            usuarios = [x["content"] for x in h69.pedidos[-1]["corpo"]["messages"] if x["role"] == "user"]
            verificar(usuarios[-1] == TRECHO + " fim", "Ctrl+V na caixa do pedido colou o trecho copiado da conversa")

            # Clique na conversa, Ctrl+A e Ctrl+C: a conversa inteira.
            time.sleep(1)
            m.clique(tx + 100, ty)
            antes = len(m.serial())
            m.tecla("ctrl-a")
            m.tecla("ctrl-c")
            s = h69.esperar(m, "saguicode-janela: copiou a selecao", 10, antes)
            total = len("diga a frase") + len(RESPOSTA) + len(TRECHO + " fim") + len("Recebi.") + 3 * 2
            verificar(f"copiou a selecao ({total} letras)" in s, f"Ctrl+A e Ctrl+C na conversa copiam a conversa inteira ({total} letras)")
            m.captura("H123-tudo-selecionado")
            verificar(h69.CHAVE not in m.serial(), "a chave não aparece no log")
    finally:
        servidor.shutdown()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
