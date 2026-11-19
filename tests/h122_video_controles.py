#!/usr/bin/env python3
"""H122 - os controles do tocador de vídeo (issue #29, D071).

Com o vídeo de 360p do Eli (testes_eli/sample-5s-360p.mp4, H.264 High com
AAC) num pendrive, "video /usb/v.mp4" abre a janela com a barra de controles
embaixo, e o teste confere:
  1. a barra mostra o tempo ("0:0x / 0:05", lido na captura pela fonte do
     sistema) e a parte verde da barra de progresso cresce;
  2. Espaço pausa: o diário diz "pausado", o tempo da barra para e o som
     some do .wav gravado pelo QEMU; Espaço de novo volta o som;
  3. um clique na barra de progresso pula (o diário diz para onde, perto do
     ponto clicado, e o tempo da barra vai para lá);
  4. as setas para cima e para baixo mudam o volume do sistema;
  5. F põe em tela cheia: a janela passa a ter o tamanho da tela (1024x768)
     e cobre a barra de cima e a doca; Esc volta ao tamanho de antes; o
     duplo clique na imagem também entra em tela cheia;
  6. o botão de maximizar da moldura maximiza (a janela é redimensionável)
     e a imagem continua com a proporção do vídeo.

Os vídeos não estão no repositório (são do Eli). Sem o de 360p em
testes_eli/ (ou em $SAGUI_VIDEO_ALTO), o teste avisa e não roda.
"""
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, criar_pendrive, pendrive_qemu, cor_do_pixel, _fonte, SAIDA  # noqa: E402
from h40_som import ler_wav, som_qemu  # noqa: E402
from h88_video_alto import achar_video  # noqa: E402

BARRA_A = 40
COR_TEXTO = (0xC8, 0xD2, 0xCC)
COR_FEITO = (0x2E, 0xB8, 0x72)


def texto_em(png, x, y, n):
    """Lê n letras da fonte 8x16 do SaguiOS a partir de (x, y) na captura:
    só os pontos exatamente da cor do texto contam (as quinas suavizadas
    têm cores misturadas)."""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    fonte = _fonte()
    saida = ""
    for k in range(n):
        bits = bytes(sum(0x80 >> c for c in range(8) if im.getpixel((x + 8 * k + c, y + lin)) == COR_TEXTO) for lin in range(16))
        saida += fonte.get(bits, "?") if any(bits) else " "
    return saida


def tempo_na_barra(m, j, nome):
    png = m.captura(nome, guardar=False)
    return texto_em(png, j["x"] + 90, j["y"] + j["a"] - BARRA_A + 12, 11), png


def feito_na_barra(png, j):
    """Quantos pontos verdes (a parte já tocada) há na linha da barra de progresso."""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    y = j["y"] + j["a"] - BARRA_A + 19
    return sum(1 for x in range(j["x"], j["x"] + j["l"]) if im.getpixel((x, y)) == COR_FEITO)


def energia(amostras, ini, fim):
    bloco = amostras[max(0, ini):max(0, fim):3]
    return math.sqrt(sum(v * v for v in bloco) / len(bloco)) if bloco else 0


def main():
    mp4 = achar_video("sample-5s-360p.mp4")
    if not mp4:
        print("  AVISO: sem testes_eli/sample-5s-360p.mp4, os controles do vídeo NÃO foram testados")
        return 0
    os.makedirs(SAIDA, exist_ok=True)
    wav = os.path.join(SAIDA, "h122-video.wav")
    pen = criar_pendrive("h122-pen", mb=64, rotulo="DADOS", arquivos={"v.mp4": open(mp4, "rb").read()})
    with Maquina("h122", "ide", memoria="768M", extras=["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen) + som_qemu(wav)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar")
        m.digitar("video /usb/v.mp4\n", 0.04)
        m.esperar_serial('"v.mp4"', 120)
        j = janela(m.serial(), "v.mp4")
        verificar((j["l"], j["a"]) == (640, 360 + BARRA_A), f"a janela tem a imagem de 640x360 e a barra de controles embaixo ({j['l']}x{j['a']})")
        wid = re.findall(r'janela (\d+) criada por \d+: "v.mp4"', m.serial())[-1]

        # 1. o tempo e a barra de progresso andam
        time.sleep(1.5)
        t1, png1 = tempo_na_barra(m, j, "h122-tempo1")
        time.sleep(1)
        t2, png2 = tempo_na_barra(m, j, "h122-tempo2")
        verificar(re.fullmatch(r"0:0\d / 0:05", t1) and re.fullmatch(r"0:0\d / 0:05", t2),
                  f"a barra mostra o tempo atual e o total ('{t1}', depois '{t2}')")
        verificar(t2 > t1 and feito_na_barra(png2, j) > feito_na_barra(png1, j) > 0,
                  f"o tempo anda e a parte verde da barra cresce ({feito_na_barra(png1, j)} -> {feito_na_barra(png2, j)} pontos)")
        m.captura("H122-1-controles")

        # 2. Espaço pausa e volta; o som para e volta
        m.tecla("spc")
        m.esperar_serial("video: pausado em", 20)
        n_pausa = os.path.getsize(wav)
        p1, _ = tempo_na_barra(m, j, "h122-pausa1")
        time.sleep(2.5)
        p2, _ = tempo_na_barra(m, j, "h122-pausa2")
        n_volta = os.path.getsize(wav)
        verificar(p1 == p2 and re.fullmatch(r"0:0\d / 0:05", p1), f"pausado, o tempo da barra fica parado ('{p1}')")
        m.tecla("spc")
        m.esperar_serial("video: tocando em", 20)
        time.sleep(2)

        # 3. clique na barra de progresso: pula para perto do ponto clicado
        antes = len(m.serial())
        prog_x = 8 + 72 + 12 + 13 * 8
        vol_x = j["l"] - 8 - 92 - 12 - 60
        prog_l = vol_x - 8 - 4 * 8 - prog_x
        m.clique(j["x"] + prog_x + prog_l // 5, j["y"] + j["a"] - BARRA_A + 20)
        fim = time.time() + 20
        while time.time() < fim and not re.search(r"video: pulo para (\d+) ms", m.serial()[antes:]):
            time.sleep(0.2)
        r = re.search(r"video: pulo para (\d+) ms", m.serial()[antes:])
        verificar(r and 700 <= int(r[1]) <= 1700, f"o clique a 1/5 da barra pulou para {r[1] if r else '?'} ms (o vídeo tem ~5,7 s)")
        time.sleep(0.6)
        t3, _ = tempo_na_barra(m, j, "h122-pulo")
        verificar(t3.startswith("0:01") or t3.startswith("0:02"), f"depois do pulo a barra mostra '{t3}'")

        # 4. volume pelas setas
        antes = len(m.serial())
        m.tecla("down")
        m.tecla("down")
        time.sleep(0.5)
        vols = [int(v) for v in re.findall(r"video: volume (\d+)", m.serial()[antes:])]
        verificar(len(vols) == 2 and vols[1] == vols[0] - 10, f"a seta para baixo baixa o volume do sistema ({vols})")
        m.tecla("up")
        m.tecla("up")
        time.sleep(0.5)

        # 5. tela cheia pela tecla F e pelo duplo clique; Esc volta
        antes = len(m.serial())
        m.tecla("f")
        fim = time.time() + 20
        while time.time() < fim and "video: janela 1024x768 (tela cheia)" not in m.serial()[antes:]:
            time.sleep(0.2)
        s = m.serial()[antes:]
        verificar(f"janela {wid} em tela cheia (1024x768)" in s and "video: janela 1024x768 (tela cheia)" in s,
                  "F põe o vídeo em tela cheia: a janela fica com 1024x768")
        time.sleep(4)        # a barra some 3 s depois do último movimento
        png = m.captura("H122-2-tela-cheia")
        topo = [cor_do_pixel(png, x, 6) for x in (200, 512, 900)]
        base = [cor_do_pixel(png, x, 740) for x in (300, 512, 700)]
        # 640x360 em 1024x768: a imagem fica com 1024x576 no meio; em cima e embaixo, preto
        verificar(all(c == (0, 0, 0) for c in topo + base),
                  f"em tela cheia a barra de cima e a doca ficam cobertas (preto em cima e embaixo da imagem: {topo + base})")
        meio = cor_do_pixel(png, 512, 384)
        verificar(meio != (0, 0, 0), f"e a imagem ocupa o meio da tela ({meio})")
        antes = len(m.serial())
        m.tecla("esc")
        fim = time.time() + 20
        while time.time() < fim and "video: janela 640x400" not in m.serial()[antes:]:
            time.sleep(0.2)
        verificar(f"janela {wid} saiu da tela cheia" in m.serial()[antes:] and "video: janela 640x400" in m.serial()[antes:],
                  "Esc sai da tela cheia e a janela volta a 640x400")
        antes = len(m.serial())
        m.clique(j["x"] + 320, j["y"] + 180, duplo=True)
        fim = time.time() + 20
        while time.time() < fim and "(tela cheia)" not in m.serial()[antes:]:
            time.sleep(0.2)
        verificar("video: janela 1024x768 (tela cheia)" in m.serial()[antes:], "o duplo clique na imagem também põe em tela cheia")
        m.tecla("esc")
        time.sleep(1)

        # 6. o botão de maximizar da moldura
        antes = len(m.serial())
        bx = j["l"] + 4 - 2 - 20
        m.clique(j["fx"] + bx - 40, j["fy"] + 12)
        fim = time.time() + 20
        while time.time() < fim and not re.search(r"maximizada[\s\S]*video: janela \d+x\d+", m.serial()[antes:]):
            time.sleep(0.2)
        r = re.search(r"video: janela (\d+)x(\d+)", m.serial()[antes:])
        verificar("maximizada" in m.serial()[antes:] and r and int(r[1]) > 640,
                  f"o botão de maximizar da moldura maximiza o tocador ({r[1] + 'x' + r[2] if r else '?'})")
        time.sleep(1.5)
        m.captura("H122-3-maximizado")
        m.tecla("esc")
        m.esperar_serial("video: fim", 60)

    amostras, taxa = ler_wav(wav)
    # posição no .wav: o QEMU grava em tempo real, 4 bytes por quadro estéreo de 16 bits
    i_pausa, i_volta = (n_pausa - 44) // 4, (n_volta - 44) // 4
    com_som = energia(amostras, i_pausa - taxa, i_pausa - taxa // 4)
    pausado = energia(amostras, i_pausa + taxa // 2, i_volta - taxa // 5)
    depois = energia(amostras, i_volta + taxa // 2, i_volta + taxa * 3 // 2)
    verificar(com_som > 200 and pausado < 20 and depois > 200,
              f"o som para durante a pausa e volta depois (energia antes {com_som:.0f}, pausado {pausado:.0f}, depois {depois:.0f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
