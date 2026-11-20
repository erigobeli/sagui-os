#!/usr/bin/env python3
"""H140 - a barra nova do Cipó: aba, botões com ícones, endereço, estrela,
barra de favoritos e menu.

Critérios:
  - a estrela no fim do endereço guarda a página nos favoritos, e ela
    aparece na barra de baixo; outro clique (ou Ctrl+D) tira;
  - nas páginas internas (sagui:) a estrela fica apagada e não faz nada;
  - um clique no favorito da barra abre a página;
  - o menu dos três pontos abre os Favoritos;
  - a casa volta à página inicial; F5 recarrega;
  - Ctrl+N e o "+" abrem outra janela do Cipó; o "x" da aba fecha a janela.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402
from h62_navegador import botao  # noqa: E402

LARG = 960
Y_BOTOES = 36 + 4 + 16


def esperar(m, texto, tempo=30, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def main():
    with Maquina("h140", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("echo '<html><head><title>Pagina A</title></head><body><h1>A</h1></body></html>' > /tmp/a.html", tempo=10)
        m.comando("echo '<html><head><title>Pagina B</title></head><body><h1>B</h1></body></html>' > /tmp/b.html", tempo=10)
        n = len(m.serial())
        m.digitar("navegador /tmp/a.html\n", 0.04)
        esperar(m, "navegador: pronto /tmp/a.html", 60, n)
        time.sleep(1)
        j = janela(m.serial(), "Cipó")

        # A estrela guarda, e o favorito aparece na barra.
        n = len(m.serial())
        m.clique(*botao(j, "fav"))
        esperar(m, "navegador: favorito /tmp/a.html", 10, n)
        time.sleep(0.8)
        m.captura("H140-1-favorito-na-barra")
        verificar(True, "a estrela guardou /tmp/a.html nos favoritos")

        # Ctrl+D tira (a estrela já estava cheia).
        m.clique(j["x"] + 400, j["y"] + 300)   # foco de volta no Cipó, na página
        n = len(m.serial())
        m.tecla("ctrl-d")
        esperar(m, "navegador: favorito tirado /tmp/a.html", 10, n)
        verificar(True, "Ctrl+D com a estrela cheia tira a página dos favoritos")
        m.clique(j["x"] + 400, j["y"] + 300)
        n = len(m.serial())
        m.clique(*botao(j, "fav"))
        esperar(m, "navegador: favorito /tmp/a.html", 10, n)

        # Outra página; o favorito da barra traz a primeira de volta.
        n = len(m.serial())
        m.clique(j["x"] + 300, j["y"] + Y_BOTOES)
        m.digitar("/tmp/b.html\n", 0.04)
        esperar(m, "navegador: pronto /tmp/b.html", 30, n)
        time.sleep(0.5)
        n = len(m.serial())
        m.clique(*botao(j, "barra_favoritos"))
        esperar(m, "navegador: favorito aberto /tmp/a.html", 10, n)
        esperar(m, "navegador: pronto /tmp/a.html", 30, n)
        verificar(True, "um clique no favorito da barra abre a página")

        # O menu: o terceiro item abre os Favoritos.
        n = len(m.serial())
        m.clique(*botao(j, "menu"))
        esperar(m, "navegador: botao 5", 10, n)
        time.sleep(0.8)
        m.captura("H140-2-menu")
        m.clique(j["x"] + LARG - 228 + 60, j["y"] + 91 + 26 * 2)
        esperar(m, "navegador: menu 2", 10, n)
        esperar(m, "navegador: pronto sagui:favoritos", 30, n)
        verificar(True, "o menu dos três pontos abre os Favoritos")

        # Página interna: a estrela fica apagada.
        n = len(m.serial())
        m.clique(*botao(j, "fav"))
        time.sleep(1)
        verificar("navegador: favorito" not in m.serial()[n:], "em sagui:favoritos a estrela não faz nada")

        # A casa e o F5.
        n = len(m.serial())
        m.clique(j["x"] + 8 + 3 * 34 + 16, j["y"] + Y_BOTOES)
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        verificar(True, "a casa abre a página inicial")
        n = len(m.serial())
        m.tecla("f5")
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        verificar(True, "F5 recarrega")
        time.sleep(0.5)
        m.mouse_para(j["x"] + 120, j["y"] + Y_BOTOES)
        time.sleep(0.6)
        m.captura("H140-3-inicio")

        # Ctrl+N abre outra janela; o x da aba dela fecha.
        n = len(m.serial())
        m.tecla("ctrl-n")
        esperar(m, "navegador: nova janela pid", 10, n)
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        time.sleep(1)
        j2 = janela(m.serial(), "Cipó")
        verificar(m.serial()[n:].count('criada por') >= 1, "Ctrl+N abriu outra janela do Cipó")
        n = len(m.serial())
        m.clique(j2["x"] + 8 + 240 - 28 + 10, j2["y"] + 12 + 10)
        esperar(m, "navegador: aba fechada", 10, n)
        time.sleep(1.5)
        n = len(m.serial())
        m.clique(j["x"] + 8 + 240 + 6 + 14, j["y"] + 6 + 14)   # o "+" da primeira janela
        esperar(m, "navegador: nova janela pid", 10, n)
        esperar(m, "navegador: pronto sagui:inicio", 30, n)
        verificar(True, "o x da aba fechou a segunda janela, e o + abriu outra")


if __name__ == "__main__":
    main()
