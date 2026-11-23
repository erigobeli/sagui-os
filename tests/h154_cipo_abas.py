#!/usr/bin/env python3
"""H154 - abas do Cipó na mesma janela.

Critérios:
  - o "+" e o Ctrl+T abrem outra aba na mesma janela (nenhum processo novo);
  - um clique numa aba de trás traz a página dela de volta;
  - Ctrl+Tab passa para a próxima aba;
  - o botão do meio do mouse fecha a aba sob ele;
  - o "x" fecha a aba; Ctrl+W fecha a da frente;
  - o histórico é de cada aba: voltar não sai da aba.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela  # noqa: E402
from h140_cipo_barra import esperar  # noqa: E402

ABA_Y = 6 + 14


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
        esperar(m, "navegador: aba 1 de 3", 10, n)
        esperar(m, "navegador: pronto /tmp/a.html", 30, n)
        verificar(True, "o clique na aba 1 trouxe a página A")

        # Ctrl+Tab vai para a aba 2 (b.html); voltar leva à página inicial dela.
        n = len(m.serial())
        m.tecla("ctrl-tab")
        esperar(m, "navegador: aba 2 de 3", 10, n)
        esperar(m, "navegador: pronto /tmp/b.html", 30, n)
        verificar(True, "Ctrl+Tab passou para a aba 2")
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
        esperar(m, "navegador: pronto /tmp/a.html", 30, n)
        verificar(True, "o x fechou a aba da frente e a vizinha veio")

        # Ctrl+W na última aba fecha a janela.
        n = len(m.serial())
        m.tecla("ctrl-w")
        esperar(m, "navegador: aba fechada", 10, n)
        time.sleep(1)
        m.captura("H154-3-janela-fechada")
        verificar(True, "Ctrl+W na última aba fechou a janela")


if __name__ == "__main__":
    main()
