#!/usr/bin/env python3
"""H71 e H72 - contexto e interface do Sagui Code (com o servidor falso).

Critérios:
  H71: a primeira mensagem leva as instruções, o manual do C do SaguiOS e os
       cabeçalhos da libsagui (marcada para o cache de prompt); numa conversa
       longa (vários pedidos que leem o manual inteiro), nenhum pedido passa
       do limite configurado (ia.max_contexto): os resultados antigos de
       ferramentas são resumidos e o log registra o corte; o gasto aparece ao
       fim de cada pedido.
  H72: /gasto mostra tokens e dólares; /salvar grava a conversa no pendrive
       (sem a chave) e /abrir a traz de volta; /modelo troca o modelo.
       Capturas de uma sessão.
"""
import json
import os
import socketserver
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, mtools, FalhaTeste  # noqa: E402
import h69_h70_saguicode as h69  # noqa: E402

LIMITE = 20000


def main():
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), h69.Atendente)
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    pen = criar_pendrive("h71-pen", pastas=(".saguios", "projetos"), arquivos={
        ".saguios/chaves.txt": f"openrouter = {h69.CHAVE}\n",
        ".saguios/configuracoes.txt": f"ia.servidor = http://10.0.2.2:{porta}/api/v1\nia.modelo = anthropic/claude-teste\n"
                                      f"ia.max_contexto = {LIMITE}\n",
    })
    try:
        with Maquina("h71", "ide", extras=h69.REDE + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(2)
            antes = len(m.serial())
            m.digitar("saguicode\n", 0.04)
            h69.esperar(m, "saguicode: iniciado", 30, antes)
            time.sleep(1)
            # H71: quatro pedidos que leem o manual inteiro.
            n0 = len(h69.pedidos)
            for k in range(4):
                antes = len(m.serial())
                m.digitar(f"leia o manual {k}\n", 0.05)
                s = h69.esperar(m, "saguicode: pedido terminado", 90, antes)
                verificar("custo" in s and "US$" in m.serial()[antes:], f"pedido {k + 1}: terminou e mostrou o gasto")
            sistema = h69.pedidos[n0]["corpo"]["messages"][0]["content"][0]["text"]
            verificar("Sagui Code" in sistema and "c-do-sagui.md" in sistema and "sagui.h" in sistema and "gui.h" in sistema,
                      f"a primeira mensagem leva instruções, manual e cabeçalhos ({len(sistema)} letras)")
            tamanhos = []
            for p in h69.pedidos[n0:]:
                letras = sum(len(json.dumps(x.get("content") or "", ensure_ascii=False)) + len(json.dumps(x.get("tool_calls") or ""))
                             for x in p["corpo"]["messages"]) + len(json.dumps(p["corpo"]["tools"], ensure_ascii=False))
                tamanhos.append(letras // 4)
            verificar(max(tamanhos) <= LIMITE * 11 // 10,
                      f"nenhum pedido passou do limite de {LIMITE} tokens (estimados: {tamanhos})")
            verificar("saguicode: contexto cortado" in m.serial(), "a conversa longa foi resumida (log do corte)")
            m.captura("H71-1-conversa-longa")
            # H72: /gasto, /modelo, /salvar e /abrir.
            antes = len(m.serial())
            m.digitar("/gasto\n", 0.05)
            time.sleep(1)
            verificar("tokens" in m.serial()[antes:] and "US$" in m.serial()[antes:], "/gasto mostra tokens e dólares")
            m.digitar("/modelo google/gemini-teste\n", 0.05)
            time.sleep(1)
            verificar("Modelo: google/gemini-teste" in m.serial()[antes:], "/modelo troca o modelo")
            m.digitar("/salvar sessao\n", 0.05)
            h69.esperar(m, "Conversa salva", 20, antes)
            m.digitar("/limpar\n", 0.05)
            time.sleep(0.5)
            m.digitar("/abrir sessao\n", 0.05)
            s = h69.esperar(m, "Conversa sessao aberta", 20, antes)
            verificar(True, "/salvar e /abrir: a conversa volta do pendrive")
            m.captura("H72-1-sessao")
            m.digitar("/sair\n", 0.05)
            time.sleep(1)
        verificar(not h69.erros_servidor, "todos os pedidos foram JSON em UTF-8 válido" + (": " + "; ".join(h69.erros_servidor) if h69.erros_servidor else ""))
        conteudo, rc = mtools(pen, "mtype", "p:/.saguios/conversas/sessao.json")
        verificar(rc == 0 and h69.CHAVE not in conteudo and "leia o manual" in conteudo,
                  "a conversa salva está no pendrive e não tem a chave")
    finally:
        servidor.shutdown()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
