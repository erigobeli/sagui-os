#!/usr/bin/env python3
"""H84 - resolução nativa do painel (placa Intel HD Graphics 3000 do G470).

O QEMU não tem a placa Intel, então o driver em si só é provado no G470.
Aqui se prova o que dá para provar:
  1. sem "video.nativo" no pendrive, nada muda (1024x768) e o diário diz que
     o driver está desligado;
  2. com "video.nativo = sim", na placa VGA do QEMU, nada muda e o diário diz
     que a placa não é Intel; a área de trabalho fica igual à do caso 1;
  3. com "video.nativo = teste-qemu", a troca de resolução com o sistema já
     ligado (o mesmo caminho que o driver Intel usa no fim) leva a tela a
     1366x768, e a interface funciona: ícones, doca no meio e o Terminal
     maximizado ocupando a largura toda;
  4. com o estágio 2 escolhendo um modo VESA largo (1280x768), a interface
     também funciona.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import (Maquina, FalhaTeste, verificar, criar_pendrive, pendrive_qemu, tamanho_png,  # noqa: E402
                  cor_do_pixel, imagem_de_teste, janela)


def com_config(nome, texto):
    pen = criar_pendrive(nome, pastas=(".saguios",), arquivos={".saguios/configuracoes.txt": texto})
    return ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def icones(png):
    """Os pixels da coluna de ícones (sem a barra de cima, que tem o relógio)."""
    return [cor_do_pixel(png, x, y) for x in range(10, 130, 3) for y in range(40, 680, 5)]


def main():
    # 1. Sem a configuração: desligado.
    with Maquina("h84-desligado", "ide", extras=com_config("h84-pen1", "som.volume = 80\n")) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        s = m.serial()
        verificar("montado sozinho" in s, "[desligado] o pendrive com configurações foi montado no boot")
        verificar("[VIDEO] nativo: nao agi: desligado" in s, "[desligado] o diário diz que o driver está desligado")
        verificar("escrevi" not in s, "[desligado] nenhum registrador escrito")
        out = m.comando("registro VIDEO", tempo=20)
        verificar("desligado" in out and "placa de video 1234:1111" in out, "[desligado] 'registro VIDEO' mostra o motivo e a placa")
        png1 = m.captura("H84-1-desligado")
        verificar(tamanho_png(png1) == (1024, 768), "[desligado] a tela continua 1024x768")

    # 2. Ligado, mas a placa não é Intel: nada muda.
    with Maquina("h84-sim", "ide", extras=com_config("h84-pen2", "video.nativo = sim\n")) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        s = m.serial()
        verificar("[VIDEO] nativo: nao agi: a placa de video nao e Intel" in s,
                  "[sim] o diário diz que a placa (VGA do QEMU) não é Intel")
        verificar("escrevi" not in s and "agora " not in s, "[sim] nada foi escrito na placa e a resolução não mudou")
        png2 = m.captura("H84-2-sim-sem-intel")
        verificar(tamanho_png(png2) == (1024, 768), "[sim] a tela continua 1024x768")
        verificar(icones(png1) == icones(png2), "[sim] a área de trabalho é igual à do sistema sem a configuração")

    # 3. Troca de resolução com o sistema ligado, para 1366x768.
    with Maquina("h84-1366", "ide", memoria="768M", extras=com_config("h84-pen3", "video.nativo = teste-qemu\n")) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        s = m.serial()
        verificar("[VIDEO] agora 1366x768" in s and "teste-qemu: tela trocada para 1366x768" in s,
                  "[1366] a tela foi trocada para 1366x768 com o sistema ligado")
        png = m.captura("H84-3-1366x768")
        verificar(tamanho_png(png)[1] == 768 and tamanho_png(png)[0] >= 1366, f"[1366] a captura tem {tamanho_png(png)}")
        # A doca fica no meio da tela de 1366: o ícone do Terminal da doca
        # fica perto de x = 683; o fundo da área de trabalho perto de x = 1300 não é preto.
        fundo = cor_do_pixel(png, 1300, 400)
        verificar(sum(fundo) > 60, f"[1366] o fundo cobre a tela até a direita ({fundo})")
        verificar(cor_do_pixel(png, 1365, 10) != (0, 0, 0), "[1366] a barra de cima vai até a borda direita")
        t = janela(m.serial(), "Terminal")
        wid = re.findall(r'janela (\d+) criada por \d+: "Terminal"', m.serial())[-1]
        antes = len(m.serial())
        m.clique(t["fx"] + t["l"] // 2, t["fy"] + 12, duplo=True)   # duplo clique na barra de título maximiza
        s = esperar(m, "maximizada", 20, antes)
        r = re.search(rf"janela {wid} redimensionada para (\d+)x(\d+)", s)
        verificar(r and int(r[1]) == 1366 - 4, f"[1366] o Terminal maximizado tem {r[1] if r else '?'} de largura (1362 esperado)")
        col = re.search(r"terminal: tamanho \d+x\d+ \((\d+) colunas", esperar(m, "terminal: tamanho", 20, antes))
        verificar(col and int(col[1]) >= 160, f"[1366] o Terminal maximizado tem {col[1] if col else '?'} colunas")
        time.sleep(1)
        m.captura("H84-4-1366-maximizado")
        out = m.comando("echo tela larga", tempo=20)
        verificar("tela larga" in out, "[1366] o shell funciona na tela larga")

    # 4. Modo VESA largo escolhido pelo estágio 2 (o QEMU oferece 1280x768).
    with Maquina("h84-vesa-largo", "ide", imagem=imagem_de_teste("x", ["--vesa", "--vesa-largo"])) as m:
        m.esperar_serial("area de trabalho no ar", 90)
        time.sleep(3)
        verificar(re.search(r"\[VIDEO\] 1(280|360|366)x768, 32 bits", m.serial()), "[vesa largo] o estágio 2 escolheu um modo largo de 768 linhas")
        png = m.captura("H84-5-vesa-largo")
        verificar(tamanho_png(png)[0] > 1024, f"[vesa largo] a área de trabalho tem {tamanho_png(png)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
