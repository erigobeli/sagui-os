#!/usr/bin/env python3
"""mkfonte_suave.py - a fonte bitmap do SaguiOS com as bordas suaves (16 tons).

Não roda no build (precisa do Pillow): o resultado vai versionado em
tools/fonte/suave8x16.txt, e o build (tools/mkfonte.py) só o converte em C,
com a biblioteca padrão do Python.

As letras são as da própria fonte (tools/fonte/sagui8x16.txt, montada pelo
mkfonte.py): o mesmo desenho, o mesmo traço de 2 pixels, a mesma célula de
8 x 16. Cada letra é ampliada 8 vezes, o contorno é arredondado (um
desfoque leve seguido de corte no meio, que tira a escada das diagonais e
das curvas sem mudar a grossura) e a letra volta ao tamanho real tirando a
média de cada bloco de 8 x 8: a borda ganha tons, o miolo fica cheio.

O negrito é a mesma letra com o traço engrossado 3/8 de pixel para a direita
(e um quarto de pixel para baixo), ainda dentro da célula: as letras não
colam, como colavam com a letra desenhada duas vezes.

Uso: tools/mkfonte_suave.py [saida.txt]
"""
import os
import sys
import tempfile
from PIL import Image, ImageFilter

AQUI = os.path.dirname(os.path.abspath(__file__))
E = 8            # ampliação
RAIO = 2.6       # desfoque na grade ampliada (em subpixels)


def fonte_bitmap():
    """Roda o mkfonte.py e lê o vetor fonte_sagui do C gerado."""
    saida = os.path.join(tempfile.mkdtemp(), "f.c")
    os.system(f'{sys.executable} "{AQUI}/mkfonte.py" "{AQUI}/fonte/sagui8x16.txt" "{saida}" > /dev/null')
    fonte = []
    for linha in open(saida):
        if linha.strip().startswith("{0x") and len(fonte) < 256:
            fonte.append([int(v, 16) for v in linha.split("{")[1].split("}")[0].split(",")])
    return fonte


def suavizar(g, negrito):
    im = Image.new("L", (8 * E, 16 * E), 0)
    px = im.load()
    for y in range(16):
        for x in range(8):
            if g[y] & (0x80 >> x):
                for j in range(E):
                    for i in range(E):
                        px[x * E + i, y * E + j] = 255
    if negrito:   # engrossa: 3/8 de pixel para a direita, 1/4 para baixo
        base = im.copy()
        for dx in range(1, 3 * E // 8 + 1):
            im.paste(255, mask=base.transform(base.size, Image.AFFINE, (1, 0, -dx, 0, 1, 0)))
        base = im.copy()
        for dy in range(1, E // 4 + 1):
            im.paste(255, mask=base.transform(base.size, Image.AFFINE, (1, 0, 0, 0, 1, -dy)))
    im = im.filter(ImageFilter.GaussianBlur(RAIO)).point(lambda v: 255 if v >= 128 else 0)
    im = im.resize((8, 16), Image.BOX)
    return [min(15, (v * 15 + 127) // 255) for v in im.tobytes()]


def main():
    saida = sys.argv[1] if len(sys.argv) > 1 else os.path.join(AQUI, "fonte", "suave8x16.txt")
    fonte = fonte_bitmap()
    with open(saida, "w", encoding="utf-8") as f:
        f.write("# suave8x16.txt - gerado por tools/mkfonte_suave.py a partir de sagui8x16.txt. Não edite.\n")
        f.write("# 2 faces (regular, negrito) x 256 letras (Latin-1); 128 tons por letra (8 x 16, F = cheio).\n")
        for nome, negrito in (("regular", False), ("negrito", True)):
            f.write(f"@{nome}\n")
            for c in range(256):
                f.write("".join("%X" % v for v in suavizar(fonte[c], negrito)) + "\n")


if __name__ == "__main__":
    main()
