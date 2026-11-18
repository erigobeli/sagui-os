#!/usr/bin/env python3
"""H120 - o Tocador lê a música aos poucos e limpa o som ao trocar (D070, issue #27).

Critérios:
  - com uma música WAV grande (~7 MB) no pendrive, o Tocador começa a tocar
    antes de terminar de ler o arquivo, e a janela atende um clique (o botão
    de volume) enquanto ele ainda está lendo;
  - ao passar para a próxima música (>>), o som da anterior para logo: o
    tempo de 440 Hz gravado pelo QEMU não passa muito do tempo em que ela
    tocou de verdade (antes, o fim da fila e o anel da placa repetido durante
    a leitura da próxima estendiam a música velha), e depois que a nova
    começa a velha não volta;
  - o MP3 também toca em fluxo (lido em pedaços enquanto decodifica).
"""
import array
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, SAIDA, criar_pendrive, pendrive_qemu  # noqa: E402
from h40_som import som_qemu, ler_wav  # noqa: E402
from h41_mistura import energia  # noqa: E402

AQUI = os.path.dirname(os.path.abspath(__file__))


def wav_grande(freq, segundos, taxa=44100):
    """WAV estéreo de 16 bits com um tom puro (gerado por um período repetido
    de 1 s, para ser rápido: 440 e 660 Hz cabem inteiros em 1 s)."""
    um = array.array("h")
    for i in range(taxa):
        v = int(12000 * math.sin(2 * math.pi * freq * i / taxa))
        um.append(v)
        um.append(v)
    dados = um.tobytes() * segundos
    return (b"RIFF" + (36 + len(dados)).to_bytes(4, "little") + b"WAVE" +
            b"fmt " + (16).to_bytes(4, "little") + (1).to_bytes(2, "little") + (2).to_bytes(2, "little") +
            taxa.to_bytes(4, "little") + (taxa * 4).to_bytes(4, "little") + (4).to_bytes(2, "little") +
            (16).to_bytes(2, "little") + b"data" + len(dados).to_bytes(4, "little") + dados)


def esperar(m, texto, tempo=30, desde=0):
    """Espera o texto na serial, olhando a cada 20 ms (o horário importa)."""
    fim = time.time() + tempo
    while time.time() < fim:
        s = m.serial()
        if texto in s[desde:]:
            return s
        time.sleep(0.02)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def clicar_agora(m, x, y):
    """Clique com o horário de quando o botão foi solto (é quando o Tocador age)."""
    m.mouse_para(x, y)
    time.sleep(0.1)
    m.monitor("mouse_button 1")
    time.sleep(0.05)
    m.monitor("mouse_button 0")
    return time.time()


def main():
    wav = os.path.join(SAIDA, "h120.wav")
    mp3 = open(os.path.join(AQUI, "recursos", "h86_seno440.mp3"), "rb").read()
    # 300 MB: o FAT32 sai com clusters de 4 KB, como o de um pendrive
    # formatado no Windows (os de 64 MB dos outros testes têm 512 bytes).
    pen = criar_pendrive("h120-pen", mb=300, rotulo="DADOS", pastas=("musicas",),
                         arquivos={"musicas/a-la440.wav": wav_grande(440, 40),      # ~7 MB
                                   "musicas/b-mi660.wav": wav_grande(660, 30),      # ~5 MB
                                   "musicas/c-seno440.mp3": mp3 * 30})              # ~500 KB
    with Maquina("h120", "ide", memoria="768M",
                 extras=som_qemu(wav) + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar")
        antes = len(m.serial())
        m.digitar("tocador /usb/musicas/a-la440.wav\n", 0.04)
        s = esperar(m, "tocador: /usb/musicas/a-la440.wav (44100 Hz", 20, antes)
        t_a = time.time()
        verificar("tocador: arquivo lido" not in s[antes:],
                  "a música começa a tocar antes de o arquivo de 7 MB terminar de ser lido")
        s = esperar(m, "tocador: pronto", 10, antes)
        t = janela(s, "Tocador")
        # O botão "+" do volume, enquanto o arquivo ainda está chegando.
        m.clique(t["x"] + 560 - 20 - 32 + 16, t["y"] + 132 + 14)
        s = esperar(m, "tocador: volume", 10, antes)
        lido = s.find("tocador: arquivo lido", antes)
        verificar(lido < 0 or lido > s.find("tocador: volume", antes),
                  "a janela atende um clique enquanto o arquivo ainda está sendo lido")
        m.clique(t["x"] + 560 - 20 - 70 + 16, t["y"] + 132 + 14)   # "-": o volume volta ao que era
        esperar(m, "tocador: arquivo lido (7056044 bytes)", 30, antes)
        verificar(True, "o arquivo grande chega inteiro, em pedaços")
        # Deixa tocar um pouco e passa para a próxima (>>).
        while time.time() < t_a + 3.0:
            time.sleep(0.01)
        antes = len(m.serial())
        t_troca = clicar_agora(m, t["x"] + 264 + 35, t["y"] + 132 + 14)
        esperar(m, "tocador: /usb/musicas/b-mi660.wav (44100 Hz", 20, antes)
        t_b = time.time()
        while time.time() < t_b + 2.5:
            time.sleep(0.01)
        antes = len(m.serial())
        t_troca2 = clicar_agora(m, t["x"] + 264 + 35, t["y"] + 132 + 14)
        esperar(m, "tocador: /usb/musicas/c-seno440.mp3 (44100 Hz", 20, antes)
        esperar(m, "tocador: arquivo lido (504540 bytes)", 20, antes)
        verificar(True, "o MP3 abre e é lido em pedaços enquanto toca")
        time.sleep(1.5)
        m.captura("H120-tocador-fluxo")

    amostras, taxa = ler_wav(wav)
    jan = taxa // 20                                    # pedaços de 50 ms
    pedacos = [amostras[k:k + jan] for k in range(0, len(amostras) - jan, jan)]
    tons = []
    for x in pedacos:
        e440, e660 = energia(x, taxa, 440), energia(x, taxa, 660)
        tons.append(440 if e440 > 800 and e440 > e660 else 660 if e660 > 800 else 0)
    # O pedaço da primeira música: de onde o 440 começa até o primeiro 660
    # (procurado depois do 440: o som de início do sistema tem um 659 Hz).
    try:
        ini_440 = tons.index(440)
        ini_660 = tons.index(660, ini_440)
    except ValueError:
        raise FalhaTeste("a segunda música (660 Hz) não tocou")
    seg_440 = sum(1 for k in range(ini_440, ini_660) if tons[k] == 440) * 0.05
    tocou = t_troca - t_a
    print(f"  440 Hz gravado: {seg_440:.2f} s; tocou {tocou:.2f} s até o clique em >>")
    verificar(seg_440 >= tocou - 0.6, "a primeira música tocou sem buracos até a troca")
    # Antes da limpeza: a fila (~340 ms) e o anel repetido durante a leitura
    # do arquivo novo inteiro somavam bem mais que isso.
    verificar(seg_440 <= tocou + 0.35, f"o som velho para logo depois de >> ({seg_440 - tocou:+.2f} s)")
    # Depois que o 660 começa, o 440 só volta com o MP3 (a 3ª música).
    fim_660 = max(k for k, v in enumerate(tons) if v == 660)
    volta = [k for k in range(ini_660, fim_660) if tons[k] == 440]
    verificar(len(volta) <= 1, f"depois da troca, a música velha não volta ({len(volta)} pedaços de 440 Hz no meio)")
    seg_660 = sum(1 for k in range(ini_660, fim_660 + 1) if tons[k] == 660) * 0.05
    print(f"  660 Hz gravado: {seg_660:.2f} s; tocou {t_troca2 - t_b:.2f} s")
    verificar(seg_660 <= t_troca2 - t_b + 0.35, "a segunda também para logo no >> seguinte")
    depois = sum(1 for k in range(fim_660, len(tons)) if tons[k] == 440)
    verificar(depois >= 10, f"o MP3 (440 Hz) tocou depois ({depois} pedaços de 50 ms)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
