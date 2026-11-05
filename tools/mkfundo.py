#!/usr/bin/env python3
"""
mkfundo.py - converte uma imagem no formato de papel de parede do SaguiOS.

Uso: mkfundo.py <entrada.png> <saida.sgi>   (precisa do Pillow; não roda no build)

Formato .sgi:
    0  char[4]  "SGI1"
    4  u16      largura
    6  u16      altura
    8  u8[768]  paleta: 256 cores em R, G, B
  776  dados    índices da paleta, linha a linha, em RLE:
                n < 128: copie os n + 1 bytes seguintes
                n >= 128: repita o byte seguinte n - 125 vezes
"""
import struct
import sys

from PIL import Image


def rle(d):
    saida, i = bytearray(), 0
    literal = bytearray()

    def soltar():
        for k in range(0, len(literal), 128):
            parte = literal[k:k + 128]
            saida.append(len(parte) - 1)
            saida.extend(parte)
        literal.clear()
    while i < len(d):
        n = 1
        while i + n < len(d) and d[i + n] == d[i] and n < 130:
            n += 1
        if n >= 3:
            soltar()
            saida += bytes((n + 125, d[i]))
        else:
            literal.extend(d[i:i + n])
        i += n
    soltar()
    return bytes(saida)


def main():
    if len(sys.argv) != 3:
        sys.exit("uso: mkfundo.py <entrada.png> <saida.sgi>")
    img = Image.open(sys.argv[1]).convert("RGB").resize((1024, 768), Image.LANCZOS)
    q = img.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = (q.getpalette() + [0] * 768)[:768]
    dados = rle(q.tobytes())
    with open(sys.argv[2], "wb") as f:
        f.write(b"SGI1" + struct.pack("<HH", *q.size) + bytes(pal) + dados)
    print(f"mkfundo: {q.size[0]}x{q.size[1]}, {776 + len(dados)} bytes")


if __name__ == "__main__":
    main()
