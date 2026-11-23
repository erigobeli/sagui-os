#!/usr/bin/env python3
"""H154 - abas do Cipó na mesma janela.

Critérios:
  - o "+" e o Ctrl+T abrem outra aba na mesma janela (nenhum processo novo);
  - um clique numa aba de trás traz a página dela de volta como estava,
    sem abrir de novo;
  - Ctrl+Tab passa para a próxima aba;
  - o botão do meio do mouse fecha a aba sob ele;
  - o "x" fecha a aba; Ctrl+W fecha a da frente;
  - o histórico é de cada aba: voltar não sai da aba;
  - durante uma carga demorada, a janela continua atendendo: um clique em
    outra aba troca na hora, e a página continua carregando atrás; ao
    voltar, ela já está pronta, sem abrir de novo;
  - Esc no meio da carga desiste dela.
"""
import http.server
import os
import socketserver
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela  # noqa: E402
from h140_cipo_barra import esperar  # noqa: E402

ABA_Y = 6 + 14


class Lenta(http.server.BaseHTTPRequestHandler):
    """Uma página que demora 6 s para chegar."""
    def do_GET(self):
        time.sleep(6)
        corpo = b"<html><head><title>Lenta</title></head><body><h1>Lenta</h1></body></html>"
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)

    def log_message(self, *a):
        pass


def sem_recarga(m, desde, espera=1.5):
    time.sleep(espera)
    return "navegador: pronto" not in m.serial()[desde:] and "Carregando" not in m.serial()[desde:]


def aba(j, i, qtd):
    """O meio do título da aba i, com qtd abas na janela de 960."""
    l = min(240, (960 - 8 - 48) // qtd)
    return (j["x"] + 8 + i * l + 60, j["y"] + ABA_Y)


def mais(j, qtd):
    l = min(240, (960 - 8 - 48) // qtd)
    return (j["x"] + 8 + qtd * l + 6 + 14, j["y"] + 6 + 14)


def main():
    with Maquina("h154", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("echo '<html><head><title>Pagina A</title></head><body><h1>A</h1></body></html>' > /tmp/a.html", tempo=10)
        m.comando("echo '<html><head><title>Pagina B</title></head><body><h1>B</h1></body></html>' > /tmp/b.html", tempo=10)
        m.comando("echo '<html><head><title>Pagina C</title></head><body><h1>C</h1></body></html>' > /tmp/c.html", tempo=10)
        for t in ("X", "Y"):
            js = ("<html><head><title>" + t + "</title></head><body>" + t + "<script>var n=0;setInterval(function(){n++;"
                  "document.body.innerHTML=n;document.title=n},300)</script></body></html>")
            m.comando("echo '" + js + "' > /tmp/" + t.lower() + ".html", tempo=10)
        n = len(m.serial())
        m.digitar("navegador /tmp/a.html\n", 0.04)
        esperar(m, "navegador: pronto /tmp/a.html", 60, n)
        time.sleep(1)
        j = janela(m.serial(), "Cipó")

        # O "+" abre a segunda aba, na mesma janela.
        n = len(m.serial())
        m.clique(*mais(j, 1))
        esperar(m, "navegador: nova aba, agora 2", 10, n)
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        verificar("nova janela" not in m.serial()[n:] and "criada por" not in m.serial()[n:],
                  "o + abriu a aba na mesma janela, sem outro processo")

        # Na aba 2, vai para b.html pelo endereço.
        n = len(m.serial())
        m.tecla("ctrl-l")
        m.digitar("/tmp/b.html\n", 0.04)
        esperar(m, "navegador: pronto /tmp/b.html", 30, n)

        # Ctrl+T abre a terceira, com c.html.
        n = len(m.serial())
        m.tecla("ctrl-t")
        esperar(m, "navegador: nova aba, agora 3", 10, n)
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        m.tecla("ctrl-l")
        m.digitar("/tmp/c.html\n", 0.04)
        esperar(m, "navegador: pronto /tmp/c.html", 30, n)
        time.sleep(1)
        m.mouse_para(*aba(j, 1, 3))
        time.sleep(0.8)
        m.captura("H154-1-tres-abas")
        verificar(True, "Ctrl+T abriu a terceira aba")

        # Clique na primeira aba: a página A volta.
        n = len(m.serial())
        m.clique(*aba(j, 0, 3))
        esperar(m, "navegador: aba 1 de 3", 5, n)
        verificar(sem_recarga(m, n), "o clique na aba 1 trouxe a página A, sem abrir de novo")

        # Ctrl+Tab vai para a aba 2 (b.html); voltar leva à página inicial dela.
        n = len(m.serial())
        m.tecla("ctrl-tab")
        esperar(m, "navegador: aba 2 de 3", 5, n)
        verificar(sem_recarga(m, n), "Ctrl+Tab passou para a aba 2, sem abrir de novo")
        n = len(m.serial())
        m.clique(j["x"] + 400, j["y"] + 400)
        m.tecla("backspace")
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        verificar("/tmp/a.html" not in m.serial()[n:], "voltar usa o histórico da própria aba")

        # O botão do meio fecha a aba 3 (de trás).
        n = len(m.serial())
        m.clique(*aba(j, 2, 3), botao=4)
        esperar(m, "navegador: aba fechada, ficam 2", 10, n)
        time.sleep(0.8)
        verificar("navegador: pronto" not in m.serial()[n:], "fechar uma aba de trás não recarrega a da frente")
        m.captura("H154-2-meio-fechou")

        # O "x" da aba da frente (2): a aba 1 vem para a frente.
        n = len(m.serial())
        l = min(240, (960 - 8 - 48) // 2)
        m.clique(j["x"] + 8 + l + l - 28 + 10, j["y"] + 6 + 6 + 10)
        esperar(m, "navegador: aba fechada, ficam 1", 10, n)
        esperar(m, "navegador: aba 1 de 1", 5, n)
        verificar(sem_recarga(m, n), "o x fechou a aba da frente e a vizinha veio como estava")

        # Carga demorada: a janela atende, e outra aba vem na hora.
        servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Lenta)
        servidor.daemon_threads = True
        porta = servidor.server_address[1]
        threading.Thread(target=servidor.serve_forever, daemon=True).start()
        lenta = f"http://10.0.2.2:{porta}/lenta"
        n = len(m.serial())
        m.tecla("ctrl-t")
        esperar(m, "navegador: nova aba, agora 2", 10, n)
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        m.tecla("ctrl-l")
        m.digitar(lenta + "\n", 0.03)
        time.sleep(2)
        n = len(m.serial())
        t0 = time.time()
        m.clique(*aba(j, 0, 2))
        esperar(m, "navegador: aba 1 de 2", 3, n)
        dt = time.time() - t0
        verificar("navegador: carga interrompida" not in m.serial()[n:] and dt < 3,
                  f"durante a carga, o clique na aba 1 trocou em {dt:.1f} s, sem interromper")
        m.captura("H154-3-carga-atras")
        esperar(m, "navegador: pronto " + lenta, 30, n)
        verificar(True, "a página lenta terminou de carregar atrás")
        n = len(m.serial())
        m.clique(*aba(j, 1, 2))
        esperar(m, "navegador: aba 2 de 2", 5, n)
        verificar(sem_recarga(m, n), "de volta à aba 2, a página lenta já estava pronta")
        # Esc no meio da carga desiste.
        n = len(m.serial())
        m.tecla("f5")
        time.sleep(1.5)
        m.tecla("esc")
        esperar(m, "navegador: carga interrompida", 5, n)
        verificar(True, "Esc desistiu da carga")
        servidor.shutdown()

        # JavaScript vivo nas duas abas: cada uma conta no seu ritmo, e a de
        # trás continua de onde parou quando volta para a frente.
        n = len(m.serial())
        m.tecla("ctrl-l")
        m.digitar("/tmp/x.html\n", 0.03)
        esperar(m, "navegador: pronto /tmp/x.html", 30, n)
        m.tecla("ctrl-t")
        esperar(m, "navegador: nova aba, agora 3", 10, n)
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        m.tecla("ctrl-l")
        m.digitar("/tmp/y.html\n", 0.03)
        esperar(m, "navegador: pronto /tmp/y.html", 30, n)
        time.sleep(2)
        for volta in range(3):
            n = len(m.serial())
            m.clique(*aba(j, 1, 3))
            esperar(m, "navegador: aba 2 de 3", 5, n)
            time.sleep(1.5)
            n2 = len(m.serial())
            m.clique(*aba(j, 2, 3))
            esperar(m, "navegador: aba 3 de 3", 5, n2)
            time.sleep(1.5)
        n = len(m.serial())
        time.sleep(1.5)
        verificar("pagina viva redesenhada" in m.serial()[n:], "o JavaScript da aba da frente continua vivo depois das trocas")
        m.captura("H154-4-js-y")
        n = len(m.serial())
        m.clique(*aba(j, 1, 3))
        esperar(m, "navegador: aba 2 de 3", 5, n)
        time.sleep(2)
        verificar("pagina viva redesenhada" in m.serial()[n:] and "navegador: pronto" not in m.serial()[n:],
                  "a aba X voltou viva, sem abrir de novo")
        m.captura("H154-5-js-x")
        verificar("encerrado" not in m.serial() and "falha" not in m.serial().lower().split("navegador: aba")[-1],
                  "nenhuma falha nas trocas com JavaScript")
        n = len(m.serial())
        m.tecla("ctrl-w")
        esperar(m, "navegador: aba fechada, ficam 2", 10, n)
        n = len(m.serial())
        m.tecla("ctrl-w")
        esperar(m, "navegador: aba fechada, ficam 1", 10, n)

        # Ctrl+W na última aba fecha a janela.
        n = len(m.serial())
        m.tecla("ctrl-w")
        esperar(m, "navegador: aba fechada", 10, n)
        time.sleep(1)
        m.captura("H154-6-janela-fechada")
        verificar(True, "Ctrl+W na última aba fechou a janela")


if __name__ == "__main__":
    main()
