#!/usr/bin/env python3
"""mkfonte_suave.py - desenha a fonte suave do SaguiOS (8x16, 16 tons).

Não roda no build (precisa do Pillow): o resultado vai versionado em
tools/fonte/suave8x16.txt, e o build (tools/mkfonte.py --suave) só o
converte em C, com a biblioteca padrão do Python.

As letras saem da DejaVu Sans Mono (rootfs/fontes, licença em
LICENCA-DejaVu.txt), regular e negrito, no tamanho em que o avanço de cada
letra dá 8 pixels; a linha de base fica no mesmo lugar da fonte bitmap (no
fim da linha 12 da célula de 16). Cada pixel guarda a cobertura em 16 tons
(0 = vazio, F = cheio): desenhar é só misturar a cor nesses tons, sem
calcular nada da letra na hora.

Uso: tools/mkfonte_suave.py [pasta_das_fontes] [saida.txt]
"""
import sys
from PIL import Image, ImageDraw, ImageFont

TAMANHO = 13.3     # 0,602 em por letra x 13,3 = 8 pixels
BASE = 13          # a linha de base fica entre as linhas 12 e 13 da célula


def face(caminho):
    f = ImageFont.truetype(caminho, TAMANHO)
    linhas = []
    for c in range(256):
        ch = chr(c)
        im = Image.new("L", (8, 16), 0)
        if 0x20 <= c < 0x7F or c >= 0xA0:
            d = ImageDraw.Draw(im)
            avanco = f.getlength(ch)
            d.text(((8 - avanco) / 2, BASE), ch, font=f, fill=255, anchor="ls")
        px = list(im.getdata())
        linhas.append("".join("%X" % min(15, (v * 15 + 127) // 255) for v in px))
    return linhas


def main():
    pasta = sys.argv[1] if len(sys.argv) > 1 else "rootfs/fontes"
    saida = sys.argv[2] if len(sys.argv) > 2 else "tools/fonte/suave8x16.txt"
    with open(saida, "w", encoding="utf-8") as f:
        f.write("# suave8x16.txt - gerado por tools/mkfonte_suave.py (DejaVu Sans Mono). Não edite.\n")
        f.write("# 2 faces (regular, negrito) x 256 letras (Latin-1); 128 tons por letra (8 x 16, F = cheio).\n")
        for nome in ("DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf"):
            f.write(f"@{nome}\n")
            f.write("\n".join(face(f"{pasta}/{nome}")) + "\n")


if __name__ == "__main__":
    main()
