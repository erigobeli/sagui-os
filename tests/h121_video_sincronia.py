#!/usr/bin/env python3
"""H121 - o tocador de vídeo segue o relógio do som (issue #28, D071).

No G470, o MP4 de 1080p (testes_eli/sample-5s.mp4, H.264 High com quadros B,
AAC) ficava em câmera lenta e o som "parava": o tocador decodificava e
mostrava todos os quadros, um atrás do outro, mesmo atrasado; o som tocava
os seus 5 segundos e acabava, e a imagem continuava devagar, muda. No QEMU
(sem KVM), o mesmo arquivo levava 109 s para os 5,7 s do vídeo, e o som
tinha buracos (cada quadro de 1080p demorava mais que os ~340 ms da fila de
som do misturador).

Critério, com o vídeo de 1080p num pendrive e o som gravado pelo QEMU num .wav:
  1. o som do vídeo sai inteiro e contínuo: do primeiro ao último trecho de
     0,1 s com som não há nenhum trecho mudo, e a duração é a do áudio do
     arquivo (5,8 s), não esticada;
  2. o relógio quase nunca teve de esperar pelo som (o tocador enche a fila
     no meio dos quadros grandes): menos de 300 ms no total;
  3. com a decodificação atrasada, os quadros B que não são referência foram
     descartados (o diário conta) e todos os de referência foram
     decodificados, sem erro;
  4. o tocador chega ao fim bem antes do que levaria decodificando todos os
     quadros: só os de referência, que não podem ser pulados, atrasam a
     imagem quando a máquina não dá conta do tempo real.

Os vídeos não estão no repositório (são do Eli). Sem o de 1080p em
testes_eli/ (ou em $SAGUI_VIDEO_ALTO), o teste avisa e não roda.
"""
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, SAIDA  # noqa: E402
from h40_som import ler_wav, som_qemu  # noqa: E402
from h88_video_alto import achar_video  # noqa: E402

DURACAO_AUDIO = 249 * 1024 / 44100     # 249 quadros AAC de 1024 amostras a 44,1 kHz
QUADROS = 171


def trechos_com_som(amostras, taxa, desde):
    """Para cada 0,1 s depois de "desde": True se tem som."""
    jan = taxa // 10
    saida = []
    for k in range(desde, len(amostras) - jan, jan):
        bloco = amostras[k:k + jan:3]
        saida.append(math.sqrt(sum(v * v for v in bloco) / len(bloco)) > 60)
    return saida


def main():
    mp4 = achar_video("sample-5s.mp4")
    if not mp4:
        print("  AVISO: sem testes_eli/sample-5s.mp4 (1080p), a sincronia do vídeo NÃO foi testada")
        return 0
    os.makedirs(SAIDA, exist_ok=True)
    wav = os.path.join(SAIDA, "h121-video.wav")
    pen = criar_pendrive("h121-pen", mb=64, rotulo="DADOS", arquivos={"v.mp4": open(mp4, "rb").read()})
    with Maquina("h121", "ide", memoria="768M", extras=["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen) + som_qemu(wav)) as m:
        m.esperar_serial("sagui:/$", 120)
        m.comando("montar")
        time.sleep(2)                         # o som de "pendrive montado" acaba
        inicio_wav = os.path.getsize(wav)     # o som do boot e do pendrive fica antes daqui
        m.digitar("video /usb/v.mp4\n", 0.04)
        m.esperar_serial("video: 1920x1080", 120)
        m.esperar_serial("video: media", 300)
        m.captura("H121-1-video-1080p")
        s = m.serial()
    f = re.search(r"video: fim; (\d+) quadros mostrados, (\d+) pulados, (\d+) ms.*decodificando (\d+) ms.*"
                  r"descartados (\d+) \(sem referencia\).*cor (\d+) ms em (\d+) quadros, relogio segurado pelo som (\d+) ms", s)
    d = re.search(r"video: H\.264: (\d+) de (\d+) quadros decodificados, (\d+) erros", s)
    verificar(f and d, "o tocador chegou ao fim do vídeo de 1080p")
    mostrados, pulados, total_ms, decod_ms, descartados, cor_ms, cor_q, segurado = (int(v) for v in f.groups())
    decodificados, n, erros = (int(v) for v in d.groups())
    por_quadro = decod_ms / max(decodificados, 1)
    print(f"  no QEMU (sem KVM): {por_quadro:.0f} ms por quadro decodificado, {cor_ms / max(cor_q, 1):.0f} ms por quadro "
          f"convertido e escalado; {mostrados} mostrados, {pulados} decodificados e não mostrados (atrasados), "
          f"{descartados} descartados; fim em {total_ms} ms")

    # 1. o som
    amostras, taxa = ler_wav(wav)
    trechos = trechos_com_som(amostras, taxa, max(0, (inicio_wav - 44) // 4))
    com = [i for i, t in enumerate(trechos) if t]
    verificar(com, "o som do vídeo foi gravado no .wav")
    primeiro, ultimo = com[0], com[-1]
    mudos = [i for i in range(primeiro, ultimo + 1) if not trechos[i]]
    dur = (ultimo - primeiro + 1) / 10
    # o fim do áudio do arquivo é quase mudo: com som acima do limiar são ~5 s
    verificar(not mudos and 4.6 <= dur <= DURACAO_AUDIO + 0.3,
              f"o som saiu contínuo, em {dur:.1f} s (o áudio do arquivo tem {DURACAO_AUDIO:.1f} s; trechos mudos no meio: {len(mudos)})")

    # 2. o relógio não esperou pelo som
    verificar(segurado < 300, f"a fila de som nunca secou: o relógio esperou o som só {segurado} ms")

    # 3. descarte
    verificar(n == QUADROS and erros == 0 and decodificados + descartados == QUADROS,
              f"{decodificados} quadros decodificados + {descartados} descartados = {QUADROS}, sem erro")
    if por_quadro > 1000 / 30:
        verificar(descartados > 0, f"atrasado ({por_quadro:.0f} ms por quadro, o vídeo tem 30 por segundo), "
                                   f"o tocador descartou {descartados} quadros B sem referência")
    else:
        print("  AVISO: esta máquina decodifica 1080p em tempo real; o descarte não foi exercitado")

    # 4. sem câmera lenta dos quadros que podiam ser pulados
    # Decodificar e mostrar todos levaria uns 171 x (ms por quadro); o descarte
    # tira quase a metade (os B sem referência são ~85 dos 171).
    sem_descarte = QUADROS * por_quadro
    verificar(total_ms < 0.8 * sem_descarte or por_quadro <= 1000 / 30,
              f"o vídeo chegou ao fim em {total_ms / 1000:.1f} s; sem descartar levaria uns {sem_descarte / 1000:.0f} s "
              f"(antes da D071, 109,3 s no QEMU desta máquina)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
