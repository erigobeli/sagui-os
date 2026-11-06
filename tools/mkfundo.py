#!/usr/bin/env python3
"""
mkfundo.py - converte uma imagem no formato de papel de parede do SaguiOS.

Uso: mkfundo.py <entrada.png> <saida.sgi>   (precisa do Pillow; não roda no build)

Formato .sgi:
    0  char[4]  "SGI2"
    4  u16      largura
    6  u16      altura
    8  u8[768]  paleta: 256 cores em R, G, B
  776  u8[]     índices da paleta, linha a linha

Não há compressão aqui: o ramdisk inteiro já é comprimido (tools/lz.py). Menos
cores comprimem melhor, por isso a paleta usa só CORES entradas.
"""
import struct
import sys

from PIL import Image


CORES = 128


def main():
    if len(sys.argv) != 3:
        sys.exit("uso: mkfundo.py <entrada.png> <saida.sgi>")
    img = Image.open(sys.argv[1]).convert("RGB").resize((1024, 768), Image.LANCZOS)
    q = img.quantize(CORES, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = (q.getpalette() + [0] * 768)[:768]
    dados = q.tobytes()
    with open(sys.argv[2], "wb") as f:
        f.write(b"SGI2" + struct.pack("<HH", *q.size) + bytes(pal) + dados)
    print(f"mkfundo: {q.size[0]}x{q.size[1]}, {776 + len(dados)} bytes")


if __name__ == "__main__":
    main()
