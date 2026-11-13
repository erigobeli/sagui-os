#!/usr/bin/env python3
"""H69 e H70 (sem o compilador) - Sagui Code contra um servidor falso da API.

O servidor falso (aqui mesmo, em Python) fala o formato do OpenRouter:
POST /api/v1/chat/completions com "stream": a resposta vai em eventos
"data: {...}", com comentários ": OPENROUTER PROCESSING", texto aos pedaços,
pedidos de ferramenta com os argumentos cortados em vários eventos e o uso
(tokens e custo) no fim. Ele segue um roteiro conforme a conversa que recebe.

Critérios:
  H69: a chave vem de /usb/.saguios/chaves.txt e chega no cabeçalho
       Authorization (e não aparece na tela nem no log); uma pergunta e uma
       resposta mostrada enquanto chega; o pedido leva as ferramentas e as
       instruções marcadas para o cache de prompt; o gasto aparece; com a chave
       errada, a mensagem de erro da API aparece.
  H70: o laço do agente: escrever_arquivo (arquivo novo, sem perguntar),
       ler_arquivo (o conteúdo volta em UTF-8 com os acentos certos),
       executar_comando (pede licença), escrever por cima de um arquivo que
       existe (pede licença; "n" impede e o modelo fica sabendo), e o limite
       de passos por pedido.
O laço com o compilador (escrever, compilar com erro, corrigir, compilar e
rodar) fica no teste da fase 7/8 com o cc.
"""
import http.server
import json
import os
import socketserver
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, criar_pendrive, pendrive_qemu, mtools  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]
CHAVE = "teste-chave-sagui-123"
pedidos = []


def evento(obj):
    return ("data: " + json.dumps(obj, ensure_ascii=False) + "\n\n").encode()


def texto_em_pedacos(texto, fim="stop", custo=0.00042):
    saida = [b": OPENROUTER PROCESSING\n\n"]
    for i in range(0, len(texto), 7):
        saida.append(evento({"choices": [{"index": 0, "delta": {"content": texto[i:i + 7]}}]}))
    saida.append(evento({"choices": [{"index": 0, "delta": {}, "finish_reason": fim}],
                         "usage": {"prompt_tokens": 1500, "completion_tokens": 40, "cost": custo,
                                   "prompt_tokens_details": {"cached_tokens": 1200}}}))
    saida.append(b"data: [DONE]\n\n")
    return saida


def chamada(nome, argumentos, texto="", idc="call_1"):
    args = json.dumps(argumentos, ensure_ascii=False)
    saida = [b": OPENROUTER PROCESSING\n\n"]
    if texto:
        saida.append(evento({"choices": [{"index": 0, "delta": {"content": texto}}]}))
    saida.append(evento({"choices": [{"index": 0, "delta": {"tool_calls": [
        {"index": 0, "id": idc, "type": "function", "function": {"name": nome, "arguments": ""}}]}}]}))
    for i in range(0, len(args), 5):
        saida.append(evento({"choices": [{"index": 0, "delta": {"tool_calls": [
            {"index": 0, "function": {"arguments": args[i:i + 5]}}]}}]}))
    saida.append(evento({"choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}],
                         "usage": {"prompt_tokens": 1600, "completion_tokens": 30, "cost": 0.0003}}))
    saida.append(b"data: [DONE]\n\n")
    return saida


def roteiro(corpo):
    """Decide a resposta pela conversa recebida."""
    msgs = corpo["messages"]
    usuario = [m for m in msgs if m["role"] == "user"][-1]["content"]
    depois = msgs[[i for i, m in enumerate(msgs) if m["role"] == "user"][-1] + 1:]
    resultados = [m["content"] for m in depois if m["role"] == "tool"]
    if "diga oi" in usuario:
        return texto_em_pedacos("Olá! Eu sou o Sagui Code, e falo com acentos: ação.")
    if "crie a nota" in usuario:
        if len(resultados) == 0:
            return chamada("escrever_arquivo", {"caminho": "nota.txt", "conteudo": "ação e coração\nlinha 2\n"}, "Vou criar o arquivo.")
        if len(resultados) == 1:
            return chamada("ler_arquivo", {"caminho": "nota.txt"}, idc="call_2")
        if len(resultados) == 2:
            return chamada("executar_comando", {"comando": "ls"}, idc="call_3")
        return texto_em_pedacos("Pronto: criei nota.txt.")
    if "sobrescreva" in usuario:
        if len(resultados) == 0:
            return chamada("escrever_arquivo", {"caminho": "nota.txt", "conteudo": "apagado\n"})
        return texto_em_pedacos("Tudo bem, não mudei o arquivo.")
    if "programa ola" in usuario:
        errado = '#include "sagui.h"\n\nint principal(int argc, char **argv)\n{\n    imprimir("oi do sagui code\\n")\n    return 0;\n}\n'
        if len(resultados) == 0:
            return chamada("escrever_arquivo", {"caminho": "ola.c", "conteudo": errado}, "Vou escrever o programa.")
        if len(resultados) == 1:
            return chamada("compilar", {"arquivo": "ola.c", "saida": "ola"}, idc="c2")
        if len(resultados) == 2:
            return chamada("editar_arquivo", {"caminho": "ola.c", "trecho_antigo": 'code\\n")\n', "trecho_novo": 'code\\n");\n'},
                           "O compilador pediu um ponto e vírgula.", idc="c3")
        if len(resultados) == 3:
            return chamada("compilar", {"arquivo": "ola.c", "saida": "ola"}, idc="c4")
        if len(resultados) == 4:
            return chamada("rodar", {"programa": "ola"}, idc="c5")
        return texto_em_pedacos("Pronto: o programa ola roda.")
    if "laço infinito" in usuario:
        return chamada("listar_pasta", {"caminho": "."}, idc=f"call_{len(resultados)}")
    return texto_em_pedacos("Não entendi.")


class Atendente(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        tam = int(self.headers.get("Content-Length", 0))
        corpo = json.loads(self.rfile.read(tam).decode())
        pedidos.append({"auth": self.headers.get("Authorization"), "corpo": corpo, "caminho": self.path})
        if self.headers.get("Authorization") != f"Bearer {CHAVE}":
            erro = json.dumps({"error": {"message": "No auth credentials found", "code": 401}}).encode()
            self.send_response(401)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(erro)))
            self.end_headers()
            self.wfile.write(erro)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        for pedaco in roteiro(corpo):
            self.wfile.write(f"{len(pedaco):x}\r\n".encode() + pedaco + b"\r\n")
            self.wfile.flush()
            time.sleep(0.03)
        self.wfile.write(b"0\r\n\r\n")

    def log_message(self, *a):
        pass


def esperar(m, texto, tempo, desde):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-500:]!r}")


def main():
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Atendente)
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    pen = criar_pendrive("h69-pen", pastas=(".saguios", "projetos"), arquivos={
        ".saguios/chaves.txt": f"# chaves de API\nopenrouter = {CHAVE}\n",
        ".saguios/configuracoes.txt": f"ia.servidor = http://10.0.2.2:{porta}/api/v1\nia.modelo = anthropic/claude-teste\nia.max_passos = 6\n",
    })
    try:
        with Maquina("h69", "ide", extras=REDE + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(2)
            antes = len(m.serial())
            m.digitar("saguicode\n", 0.04)
            esperar(m, "saguicode: iniciado, modelo anthropic/claude-teste", 30, antes)
            time.sleep(1)
            # H69: pergunta e resposta.
            m.digitar("diga oi\n", 0.05)
            s = esperar(m, "saguicode: pedido terminado", 60, antes)
            verificar("! Eu sou o Sagui Code, e falo com acentos" in s, "a resposta do modelo aparece no Terminal (em pedaços, com acentos)")
            p = pedidos[-1]
            verificar(p["auth"] == f"Bearer {CHAVE}" and p["caminho"] == "/api/v1/chat/completions",
                      "a chave de chaves.txt foi no cabeçalho Authorization")
            verificar(CHAVE not in m.serial(), "a chave não aparece na tela nem no log")
            c = p["corpo"]
            verificar(c["stream"] is True and c["model"] == "anthropic/claude-teste" and len(c["tools"]) == 7,
                      "o pedido leva stream, o modelo e as 7 ferramentas")
            sistema = c["messages"][0]
            verificar(sistema["role"] == "system" and isinstance(sistema["content"], list)
                      and sistema["content"][0].get("cache_control") == {"type": "ephemeral"},
                      "as instruções vão marcadas para o cache de prompt (modelo anthropic/)")
            verificar("custo 420 micro-dolares" in s and "cache 1200" in s, "tokens, cache e custo registrados")
            m.captura("H69-1-pergunta")
            # H70: o laço com ferramentas.
            antes = len(m.serial())
            m.digitar("crie a nota\n", 0.05)
            esperar(m, "saguicode: ferramenta executar_comando", 60, antes)
            esperar(m, "Rodar \"ls\"", 30, antes)
            m.digitar("s\n", 0.1)
            s = esperar(m, "saguicode: pedido terminado", 60, antes)
            verificar("saguicode: gravou nota.txt (23 bytes)" in s, "escrever_arquivo criou o arquivo novo sem perguntar")
            lido = [x for x in pedidos[-2]["corpo"]["messages"] if x["role"] == "tool"]
            verificar(lido and lido[-1]["content"] == "ação e coração\nlinha 2\n", "ler_arquivo devolveu o conteúdo em UTF-8 com acentos")
            ls = [x for x in pedidos[-1]["corpo"]["messages"] if x["role"] == "tool"][-1]["content"]
            verificar("nota.txt" in ls and "código de saída" in ls, "executar_comando (com licença) devolveu a saída do ls")
            verificar("Pronto: criei nota.txt" in s, "o laço terminou com a resposta final")
            m.captura("H70-1-laco")
            # Sobrescrever: pede licença; "n".
            antes = len(m.serial())
            m.digitar("sobrescreva a nota\n", 0.05)
            esperar(m, "Sobrescrever", 60, antes)
            m.digitar("n\n", 0.1)
            s = esperar(m, "saguicode: pedido terminado", 60, antes)
            negado = [x for x in pedidos[-1]["corpo"]["messages"] if x["role"] == "tool"][-1]["content"]
            verificar("não permitiu" in negado and "gravou" not in s, "sobrescrever pede licença; \"n\" impede e o modelo fica sabendo")
            m.captura("H70-2-licenca")
            # H70 com o compilador: escrever, compilar com erro, corrigir, compilar, rodar.
            m.digitar("/limpar\n", 0.05)
            time.sleep(1)
            antes = len(m.serial())
            n0 = len(pedidos)
            m.digitar("faça o programa ola\n", 0.05)
            esperar(m, "Editar", 90, antes)
            m.digitar("s\n", 0.1)
            esperar(m, "Rodar o programa", 90, antes)
            m.digitar("s\n", 0.1)
            s = esperar(m, "saguicode: pedido terminado", 90, antes)
            tools = [x["content"] for x in pedidos[-1]["corpo"]["messages"] if x["role"] == "tool"]
            verificar(len(tools) >= 5 and "faltou ';'" in tools[1] and "dica" in tools[1],
                      "a primeira compilação devolveu o erro com a dica para o modelo")
            verificar("Compilou sem erros" in tools[3], "depois de editar, compilou")
            verificar("oi do sagui code" in tools[4], "rodar devolveu a saída do programa")
            verificar("compilar ola.c -> ola (codigo 1)" in s and "compilar ola.c -> ola (codigo 0)" in s,
                      "o laço completo (escrever, erro, corrigir, compilar, rodar) passou pelo cc de verdade")
            m.captura("H70-3-compilar")
            # Limite de passos (max_passos = 6).
            antes = len(m.serial())
            n0 = len(pedidos)
            m.digitar("laço infinito\n", 0.05)
            s = esperar(m, "saguicode: pedido terminado", 90, antes)
            verificar(len(pedidos) - n0 == 6 and "Limite de 6 passos" in m.serial()[antes:].replace("\r", ""),
                      f"o limite de passos parou o laço ({len(pedidos) - n0} pedidos)")
            m.digitar("/sair\n", 0.05)
            time.sleep(1)
        # O arquivo no pendrive, em Latin-1.
        conteudo, rc = mtools(pen, "mtype", "p:/projetos/nota.txt")
        verificar(rc == 0, "nota.txt está no pendrive (pasta de trabalho /usb/projetos)")
        # Chave errada: o erro da API aparece.
        mtools(pen, "mdel", "p:/.saguios/chaves.txt")
        errado = os.path.join(os.path.dirname(pen), "chave-errada.txt")
        with open(errado, "w") as f:
            f.write("openrouter = errada\n")
        mtools(pen, "mcopy", "-o", errado, "p:/.saguios/chaves.txt")
        with Maquina("h69b", "ide", extras=REDE + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(2)
            antes = len(m.serial())
            m.digitar("saguicode diga oi\n", 0.04)
            s = esperar(m, "saguicode: erro da API", 60, antes)
            verificar("401" in s and "No auth credentials" in s, "chave errada: a mensagem de erro da API aparece")
    finally:
        servidor.shutdown()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
