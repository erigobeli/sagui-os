#!/usr/bin/env python3
"""H46: o tocador de vídeo baixa um MP4 (H.264 Baseline + AAC-LC) por HTTP,
mostra os quadros e toca o som na AC'97. O MP4 é gerado pelo ffmpeg da
máquina dos testes; sem ffmpeg, o teste é pulado."""
import functools
import http.server
import os
import re
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

SEGUNDOS = 8


def main():
    if not shutil.which("ffmpeg"):
        print("  sem ffmpeg: pulado")
        return 0
    pasta = os.path.join(SAIDA, "h46-www")
    os.makedirs(pasta, exist_ok=True)
    mp4 = os.path.join(pasta, "teste.mp4")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i", f"testsrc2=size=320x240:rate=25:d={SEGUNDOS}",
                    "-f", "lavfi", "-i", f"sine=f=440:d={SEGUNDOS}",
                    "-c:v", "libx264", "-profile:v", "baseline", "-pix_fmt", "yuv420p",
                    "-c:a", "aac", "-ar", "44100", "-ac", "2", "-movflags", "+faststart", mp4], check=True)
    tratador = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    tratador.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), tratador)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    porta = srv.server_address[1]

    wav = os.path.join(SAIDA, "h46.wav")
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0",
              "-audiodev", f"wav,id=a0,path={wav}", "-device", "AC97,audiodev=a0"]
    img = imagem_de_teste(f"/bin/video http://10.0.2.2:{porta}/teste.mp4", ["--vesa"])
    with Maquina("h46", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial("video: 320x240", 90)
        t0 = time.time()
        time.sleep(SEGUNDOS / 2)
        m.captura("H46-video")
        m.esperar_serial("video: fim", 120)
        dur = time.time() - t0
        s = m.serial()
    f = re.search(r"video: fim; (\d+) quadros mostrados, (\d+) pulados, (\d+) ms", s)
    verificar(f, "o tocador chegou ao fim")
    mostrados, pulados, ms = int(f.group(1)), int(f.group(2)), int(f.group(3))
    total = SEGUNDOS * 25
    print(f"  {mostrados} quadros mostrados, {pulados} pulados de {total}; {ms} ms no relógio do tocador, {dur:.1f} s de parede")
    verificar(mostrados + pulados == total, "decodificou todos os quadros")
    verificar(mostrados >= total // 2, "mostrou pelo menos metade dos quadros")
    with open(wav, "rb") as fw:
        q = fw.read()[44:]
    a = [int.from_bytes(q[i:i + 2], "little", signed=True) for i in range(0, len(q) - 1, 4)]
    idx = [i for i, x in enumerate(a) if abs(x) > 500]
    trecho = a[idx[0]:idx[-1]] if idx else []
    troca = sum(1 for i in range(1, len(trecho)) if (trecho[i] >= 0) != (trecho[i - 1] >= 0))
    seg = len(trecho) / 44100
    freq = troca / 2 / seg if seg else 0
    print(f"  wav: {seg:.1f} s com som, {freq:.0f} Hz")
    verificar(seg > SEGUNDOS * 0.9, "tocou quase todo o áudio")
    verificar(400 < freq < 480, "o som é o tom de 440 Hz do arquivo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
