#!/usr/bin/env python3
"""H104 - cantos arredondados com a borda suavizada nas janelas e nos menus.

Antes, o canto de raio 8 só pulava pixels inteiros (5, 3, 2, 1, 1 por
linha) e a borda ficava em escada. Agora o raio é 12 e cada pixel da
ponta entra na proporção do quanto fica dentro do círculo (4 x 4 amostras).

Critérios (medidos nos pixels da captura):
  - canto de cima da janela: a ponta é o fundo, o miolo é a barra de título
    e o pixel do meio da curva é uma mistura das duas cores;
  - canto de baixo da janela: também redondo (a ponta é o fundo, não a
    borda), e o pixel do meio da curva é mistura;
  - a sombra segue a curva: na diagonal do canto ela é mais fraca que
    embaixo da janela, à mesma distância;
  - menu do botão direito: o mesmo nos cantos de cima;
  - capturas para a revisão a olho.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, cor_do_pixel  # noqa: E402


def distancia(a, b):
    return sum(abs(x - y) for x, y in zip(a, b))


def mistura(meio, fora, dentro):
    """meio fica entre as duas pontas: diferente de cada uma e perto da reta."""
    return distancia(meio, fora) > 12 and distancia(meio, dentro) > 12 and \
        distancia(fora, meio) + distancia(meio, dentro) <= distancia(fora, dentro) + 40


def main():
    with Maquina("h104", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        # Os cantos e a sombra são dos efeitos completos: se o quadro ficar
        # lento demais (mais de 40 ms), o sistema liga os leves sozinho.
        q = re.findall(r"quadro inteiro com vidro: (\d+) ms; efeitos (\w+)", m.serial())
        verificar(q and q[-1][1] == "completos", f"os efeitos completos continuam ligados (quadro de {q[-1][0] if q else '?'} ms)")
        n = len(m.serial())
        m.digitar("calculadora\n", 0.03)
        m.esperar_serial('"Calculadora"', 30)
        time.sleep(1)
        t = janela(m.serial(), "Terminal")
        m.clique(t["x"] + t["l"] - 37, t["y"] - 12)       # minimiza o Terminal: a Calculadora fica sobre o fundo claro
        time.sleep(1.5)
        c = janela(m.serial()[n:], "Calculadora")
        png = m.captura("H104-janela")
        fx, fy = c["fx"], c["fy"]
        fa = c["a"] + 24 + 2

        fora, dentro, meio = cor_do_pixel(png, fx, fy), cor_do_pixel(png, fx + 14, fy + 3), cor_do_pixel(png, fx + 3, fy + 3)   # (3, 3): na curva de raio 12
        verificar(distancia(fora, dentro) > 25, f"o canto de cima é redondo: ponta {fora}, barra {dentro}")
        verificar(mistura(meio, fora, dentro), f"a curva de cima é suavizada: {meio} entre {fora} e {dentro}")

        ponta, borda = cor_do_pixel(png, fx, fy + fa - 1), cor_do_pixel(png, fx + 12, fy + fa - 1)
        verificar(distancia(ponta, borda) > 40, f"o canto de baixo é redondo: ponta {ponta}, borda {borda}")
        meio_b = cor_do_pixel(png, fx + 3, fy + fa - 4)    # (3, 3) contado da ponta de baixo: na curva
        borda_b = cor_do_pixel(png, fx + 9, fy + fa - 9)   # o conteúdo da janela, já dentro da curva
        verificar(mistura(meio_b, ponta, borda_b), f"a curva de baixo é suavizada: {meio_b} entre {ponta} e {borda_b}")

        # A sombra segue a curva: na diagonal do canto de baixo ela é mais
        # clara que embaixo do meio da janela, à mesma distância. (A sombra
        # antiga era de cantos retos e deixava uma ponta escura ali.)
        def brilho(c):
            return sum(c)
        diag = cor_do_pixel(png, fx - 2, fy + fa + 4)
        baixo = cor_do_pixel(png, fx + 60, fy + fa + 4)
        longe = cor_do_pixel(png, fx + 60, fy + fa + 40)
        verificar(brilho(diag) - brilho(baixo) > 0.3 * (brilho(longe) - brilho(baixo)),
                  f"a sombra arredonda no canto: diagonal {diag}, embaixo {baixo}, fundo {longe}")

        # O menu do botão direito na área vazia.
        n = len(m.serial())
        m.clique(260, 596, botao=2)   # abaixo do Terminal, sobre o fundo claro
        m.esperar_serial("[GUI] menu aberto (tipo", 10)
        x, y = map(int, re.findall(r"menu aberto \(tipo \d+, \d+ itens\) em \((\d+),(\d+)\)", m.serial()[n:])[-1])
        m.mouse_para(760, 300)                             # o cursor sai de cima do canto
        time.sleep(0.8)
        png = m.captura("H104-menu")
        fora, dentro, meio = cor_do_pixel(png, x, y), cor_do_pixel(png, x + 20, y + 3), cor_do_pixel(png, x + 3, y + 3)
        verificar(distancia(fora, dentro) > 30, f"o canto do menu é redondo: ponta {fora}, menu {dentro}")
        verificar(mistura(meio, fora, dentro), f"a curva do menu é suavizada: {meio} entre {fora} e {dentro}")
        m.tecla("esc")


if __name__ == "__main__":
    main()
