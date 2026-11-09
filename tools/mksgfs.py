#!/usr/bin/env python3
"""
mksgfs.py - monta o ramdisk SGFS (formato próprio do SaguiOS) a partir de uma pasta.

Uso: mksgfs.py <pasta_rootfs> <saida.sgfs>

Formato (todos os inteiros em little-endian):

  Cabeçalho, 32 bytes
    0  char[4]  "SGFS"
    4  u16      versão (1)
    6  u16      tamanho de cada entrada (72)
    8  u32      número de entradas
   12  u32      deslocamento da tabela de entradas (32)
   16  u32      deslocamento da área de dados
   20  u32      tamanho total do SGFS
   24  u32      soma de verificação: soma dos bytes de tudo depois do cabeçalho
   28  u32      reservado

  Entrada, 72 bytes
    0  char[56] caminho completo sem "/" inicial, terminado em zero (ex.: "docs/sobre.txt")
   56  u8       tipo: 1 = arquivo, 2 = pasta, 3 = programa
   57  u8[3]    reservado
   60  u32      tamanho em bytes (0 para pastas)
   64  u32      deslocamento dos dados a partir do início do SGFS
   68  u32      reservado

As entradas ficam ordenadas pelo caminho, então todo o conteúdo de uma pasta
aparece logo depois dela. Os textos (.txt) são convertidos de UTF-8 para
Latin-1, a codificação interna do sistema (PRD 4.4). Arquivos .gitkeep são
ignorados, mas a pasta que os contém entra na tabela.
"""
import os
import struct
import sys

import lz

TIPO_ARQUIVO, TIPO_PASTA, TIPO_PROGRAMA = 1, 2, 3
TAM_CAB, TAM_ENTRADA, MAX_CAMINHO = 32, 72, 56
EXT_TEXTO = {".txt", ".md", ".cfg"}


def coletar(raiz):
    itens = []
    for dirpath, dirs, arquivos in os.walk(raiz):
        dirs.sort()
        rel_dir = os.path.relpath(dirpath, raiz)
        if rel_dir != ".":
            itens.append((rel_dir.replace(os.sep, "/"), TIPO_PASTA, b""))
        for nome in sorted(arquivos):
            if nome == ".gitkeep":
                continue
            cam = os.path.join(dirpath, nome)
            rel = os.path.relpath(cam, raiz).replace(os.sep, "/")
            with open(cam, "rb") as f:
                dados = f.read()
            if dados[:4] == b"\x7fELF":
                tipo = TIPO_PROGRAMA
            else:
                tipo = TIPO_ARQUIVO
                if os.path.splitext(nome)[1].lower() in EXT_TEXTO:
                    dados = dados.decode("utf-8").encode("latin-1")
            itens.append((rel, tipo, dados))
    itens.sort(key=lambda i: i[0])
    return itens


def montar(itens):
    for caminho, _t, _d in itens:
        if len(caminho.encode("latin-1")) >= MAX_CAMINHO:
            sys.exit(f"mksgfs: caminho longo demais: {caminho}")
    tab_off = TAM_CAB
    dados_off = tab_off + TAM_ENTRADA * len(itens)
    dados_off = (dados_off + 15) & ~15
    tabela = b""
    area = b""
    for caminho, tipo, dados in itens:
        desloc = dados_off + len(area) if dados else 0
        tabela += struct.pack("<56sB3xIII", caminho.encode("latin-1"), tipo, len(dados), desloc, 0)
        area += dados
        area += b"\0" * ((-len(area)) % 16)  # cada arquivo começa alinhado em 16
    corpo = tabela + b"\0" * (dados_off - tab_off - len(tabela)) + area
    total = TAM_CAB + len(corpo)
    soma = sum(corpo) & 0xFFFFFFFF
    cab = struct.pack("<4sHHIIIIII", b"SGFS", 1, TAM_ENTRADA, len(itens), tab_off, dados_off, total, soma, 0)
    return cab + corpo


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    itens = coletar(sys.argv[1])
    img = montar(itens)
    tam_real = len(img)
    # LZMA (com o filtro x86 do xz, que deixa o código de máquina mais repetitivo);
    # o kernel desfaz no boot (kernel/lib/lzma.c). Antes era o LZ simples de lz.py.
    import lzma
    lc, lp, pb, dic = 3, 0, 2, 1 << 22
    filtros = [{"id": lzma.FILTER_X86}, {"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": dic, "lc": lc, "lp": lp, "pb": pb}]
    dados = lzma.compress(img, format=lzma.FORMAT_RAW, filters=filtros)
    img = b"SGXZ" + struct.pack("<IBI", tam_real, (pb * 5 + lp) * 9 + lc, dic) + dados
    os.makedirs(os.path.dirname(os.path.abspath(sys.argv[2])), exist_ok=True)
    with open(sys.argv[2], "wb") as f:
        f.write(img)
    print(f"mksgfs: {len(itens)} entradas, {tam_real} bytes, {len(img)} comprimidos -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
