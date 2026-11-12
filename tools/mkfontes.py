#!/usr/bin/env python3
"""mkfontes.py - gera as fontes do sistema (rootfs/fontes) a partir da DejaVu.

Não roda no build (precisa do fontTools e das DejaVu instaladas): o resultado
vai versionado em rootfs/fontes. Para caber na meta de 1,44 MB da imagem:

- recorte: latim (com os estendidos), IPA, diacríticos, grego, cirílico,
  pontuação, sobrescritos, moedas, letras/números/setas/matemática, caixas,
  blocos, formas geométricas, símbolos e dingbats, ligaduras latinas;
- sem hinting (o rasterizador do Cipó não executa as instruções TrueType) e
  sem tabelas que ele não lê (GSUB/GPOS/kern, nomes de glifos...);
- sem as faces oblíquas da Sans e da Mono: elas são a regular inclinada, e o
  Cipó inclina na hora (fonte_obliqua). A Serif Italic é desenho próprio.

O ramdisk inteiro vai em LZMA, que aproveita o que as faces têm em comum.

Uso: tools/mkfontes.py [pasta_das_dejavu] [rootfs/fontes]
"""
import subprocess
import sys

FACES = ["DejaVuSans", "DejaVuSans-Bold", "DejaVuSerif", "DejaVuSerif-Bold", "DejaVuSerif-Italic",
         "DejaVuSansMono", "DejaVuSansMono-Bold"]
FAIXAS = ("U+0000-024F,U+0250-02FF,U+0300-036F,U+0370-03FF,U+0400-04FF,U+1E00-1EFF,U+2000-206F,U+2070-20CF,"
          "U+2100-21FF,U+2200-22FF,U+2300-23FF,U+2460-24FF,U+2500-27BF,U+FB00-FB06,U+FFFD")


def main():
    origem = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu"
    destino = sys.argv[2] if len(sys.argv) > 2 else "rootfs/fontes"
    for f in FACES:
        subprocess.run([sys.executable, "-m", "fontTools.subset", f"{origem}/{f}.ttf", f"--unicodes={FAIXAS}",
                        "--no-hinting", "--drop-tables+=GSUB,GPOS,GDEF,kern,BASE,JSTF,MATH,DSIG,LTSH,VDMX,hdmx,PCLT,gasp,FFTM",
                        "--name-IDs=0,1,2", "--no-glyph-names", f"--output-file={destino}/{f}.ttf"], check=True)


if __name__ == "__main__":
    main()
