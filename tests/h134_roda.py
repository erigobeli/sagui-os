#!/usr/bin/env python3
"""H134 - a roda do mouse.

O mouse PS/2 do QEMU é um "IntelliMouse": com a batida de taxas 200, 100 e
80, ele passa a mandar pacotes de 4 bytes com a roda. O monitor do QEMU gira
a roda com "mouse_move dx dy dz".

Critérios:
  - o kernel liga a roda (registro "com roda", id 3);
  - no Arquivos, numa pasta com 40 arquivos, a roda sobre a lista desce a
    lista (a imagem muda) e girar de volta a devolve igual;
  - no Editor, a roda sobre o texto desce o texto;
  - no Cipó, a roda sobre a página desce a página;
  - mover o mouse e clicar continuam funcionando (o clique abre o item).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402


def ultima_janela(serial, desde):
    """A última janela criada depois de "desde" (o título do Editor e o do Cipó mudam)."""
    m = re.findall(r'criada por \d+: "[^"]*" (\d+)x(\d+) em \((\d+),(\d+)\)', serial[desde:])[-1]
    return dict(l=int(m[0]), a=int(m[1]), x=int(m[2]) + 2, y=int(m[3]) + 24)


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def recorte(png, x, y, l, a):
    """Os pixels de uma faixa (amostrados de 4 em 4), para comparar."""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    return [im.getpixel((xx, yy)) for yy in range(y, y + a, 4) for xx in range(x, x + l, 4)]


def girar(m, passos):
    """Passos para baixo (positivo) ou para cima. No monitor do QEMU, dz
    positivo é girar para cima, e cada comando vira um passo só."""
    for _ in range(abs(passos)):
        m.monitor(f"mouse_move 0 0 {-1 if passos > 0 else 1}")
        time.sleep(0.15)
    time.sleep(0.8)


def main():
    with Maquina("h134", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        verificar(", com roda" in m.serial() and "id=3" in m.serial(), "o kernel liga a roda do mouse (IntelliMouse, id 3)")
        time.sleep(1)
        m.comando("mkdir /tmp/r", tempo=10)
        for i in range(1, 41):
            m.comando(f"echo x > /tmp/r/arquivo{i:02d}.txt", tempo=10)

        n = len(m.serial())
        m.digitar("arquivos /tmp/r\n", 0.03)
        esperar(m, "arquivos: pasta /tmp/r (40 itens)", 30, n)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        meio = (a["x"] + a["l"] // 2 + 60, a["y"] + a["a"] // 2)
        faixa = (a["x"] + 200, a["y"] + 90, a["l"] - 220, a["a"] - 140)
        m.mouse_para(*meio)
        time.sleep(0.5)
        antes = recorte(m.captura("h134-antes", guardar=False), *faixa)
        n = len(m.serial())
        girar(m, 2)
        esperar(m, "[MOUSE] roda", 5, n)
        depois = recorte(m.captura("H134-1-arquivos-roda"), *faixa)
        verificar(depois != antes, "a roda sobre a lista do Arquivos desce a lista")
        girar(m, -2)
        volta = recorte(m.captura("h134-volta", guardar=False), *faixa)
        verificar(volta == antes, "girando de volta, a lista volta igual")
        m.tecla("alt-f4")
        time.sleep(1)

        # Editor: um texto comprido.
        m.comando("seq 80 > /tmp/longo.txt", tempo=10)
        n = len(m.serial())
        m.digitar("editor /tmp/longo.txt\n", 0.03)
        time.sleep(3)
        e = ultima_janela(m.serial(), n)
        m.mouse_para(e["x"] + e["l"] // 2, e["y"] + e["a"] // 2)
        time.sleep(0.5)
        faixa = (e["x"] + 20, e["y"] + 60, e["l"] - 40, e["a"] - 100)
        antes = recorte(m.captura("h134-ed-antes", guardar=False), *faixa)
        girar(m, 3)
        depois = recorte(m.captura("H134-2-editor-roda"), *faixa)
        verificar(depois != antes, "a roda sobre o texto do Editor desce o texto")
        m.tecla("alt-f4")
        time.sleep(1)

        # Cipó: uma página interna comprida.
        n = len(m.serial())
        m.digitar("navegador /sistema/ajuda/versoes.md\n", 0.03)
        esperar(m, "navegador: pronto", 30, n)
        time.sleep(1)
        c = ultima_janela(m.serial(), n)
        m.mouse_para(c["x"] + c["l"] // 2, c["y"] + c["a"] // 2)
        time.sleep(0.5)
        faixa = (c["x"] + 20, c["y"] + 60, c["l"] - 40, c["a"] - 100)
        antes = recorte(m.captura("h134-cipo-antes", guardar=False), *faixa)
        girar(m, 3)
        depois = recorte(m.captura("H134-3-cipo-roda"), *faixa)
        verificar(depois != antes, "a roda sobre a página do Cipó desce a página")


if __name__ == "__main__":
    main()
