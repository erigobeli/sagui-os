#!/usr/bin/env python3
"""H112 - instalador em telas pequenas, com um disco de nome longo.

O instalador nasce do tamanho da tela e arranja tudo a partir da janela:
em 640x480 a coluna dos passos some e os textos longos são cortados com
"..." em vez de vazar dos cartões e botões. Critérios:
  - em 640x480 e em 800x600 a janela cabe na tela (pelo tamanho no log);
  - o assistente vai até o fim nos dois tamanhos;
  - na tela final, "Fechar" fecha o instalador (640x480) e "Reiniciar
    agora" reinicia o sistema (800x600);
  - capturas de cada passo, para a revisão a olho (nada vazando).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, SAIDA  # noqa: E402

MODELO = "SAMSUNG SSD 870 EVO 1TB ULTRA RAPIDO PRO"


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def rodar(nome, vram, tela, acao):
    hd = os.path.join(SAIDA, f"{nome}-hd.img")
    with open(hd, "wb") as f:
        f.truncate(300 * 1024 * 1024)
    extras = ["-vga", "none", "-device", f"VGA,vgamem_mb={vram}",
              "-drive", f"if=none,id=d1,format=raw,file={hd}", "-device", f"ide-hd,drive=d1,bus=ide.0,unit=1,model={MODELO}"]
    with Maquina(nome, "ide", extras=extras, memoria="768M", reiniciar=acao == "reiniciar") as m:
        m.esperar_serial("sagui:/$", 120)
        verificar(f"[VIDEO] {tela[0]}x{tela[1]}" in m.serial(), f"o sistema liga em {tela[0]}x{tela[1]}")
        time.sleep(1)
        n = len(m.serial())
        m.digitar("instalar\n", 0.03)
        esperar(m, "instalar: passo 0 (Boas-vindas)", 20, n)
        c = re.findall(r'criada por \d+: "Instalar o SaguiOS" (\d+)x(\d+) em \((-?\d+),(-?\d+)\)', m.serial()[n:])[-1]
        l, a, x, y = map(int, c)
        verificar(x >= 0 and x + l + 4 <= tela[0] and y + a + 26 <= tela[1] - 62, f"a janela ({l}x{a} em {x},{y}) cabe na tela, acima da doca")
        time.sleep(1)
        m.captura(f"H112-{tela[0]}-1-boas-vindas")
        m.tecla("ret")
        esperar(m, "instalar: passo 1 (Disco)", 10, n)
        time.sleep(0.8)
        m.captura(f"H112-{tela[0]}-2-disco")
        m.tecla("ret")
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.8)
        m.captura(f"H112-{tela[0]}-3-confirmacao")
        m.digitar("hd1\n", 0.05)
        esperar(m, "instalar: passo 2 (Partições)", 20, n)
        time.sleep(0.8)
        m.captura(f"H112-{tela[0]}-4-particoes")
        m.tecla("ret")
        esperar(m, "instalar: passo 3 (Resumo)", 10, n)
        time.sleep(0.8)
        m.captura(f"H112-{tela[0]}-5-resumo")
        m.tecla("ret")
        esperar(m, "instalar: pronto em hd1", 240, n)
        time.sleep(1)
        m.captura(f"H112-{tela[0]}-6-pronto")
        verificar(True, f"o assistente vai até o fim em {tela[0]}x{tela[1]}")
        # Os botões do rodapé: a borda direita é X0 + CL (margem 16 ou 28).
        mg = 16 if l < 560 else 28
        lado = 212 if l >= 720 else 0
        dir_x = x + 2 + l - mg
        by = y + 24 + a - 58 + 17
        if lado == 0 and l >= 560:
            dir_x = x + 2 + l - 28
        k = len(m.serial())
        if acao == "fechar":
            m.clique(dir_x - 170 - 10 - 55, by)
            esperar(m, "(instalar) terminou", 10, k)
            verificar(True, "Fechar fecha o instalador")
        else:
            m.clique(dir_x - 85, by)
            esperar(m, "instalar: reiniciar", 10, k)
            esperar(m, "[SAGUI] reiniciando", 20, k)
            verificar(True, "Reiniciar agora reinicia o sistema")


def main():
    os.makedirs(SAIDA, exist_ok=True)
    rodar("h112a", 1, (640, 480), "fechar")
    rodar("h112b", 2, (800, 600), "reiniciar")


if __name__ == "__main__":
    main()
