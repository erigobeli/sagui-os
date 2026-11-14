#!/usr/bin/env python3
"""
mkimagem.py - monta a imagem de disco do SaguiOS (build/saguios.img).

Layout (PRD 4.1):
  LBA 0        MBR (estágio 1) + tabela de partições
  LBA 1        cabeçalho de boot (formato em boot/cabecalho.inc)
  LBA 2...     estágio 2
  a seguir     kernel (binário plano)
  a seguir     ramdisk (SGFS)

Todos os LBAs e endereços são calculados aqui e gravados no cabeçalho.
O endereço do ramdisk na memória vem do símbolo _kernel_fim do ELF do kernel
(fim da BSS, alinhado em 4 KB): assim a BSS zerada pelo kernel nunca apaga o
ramdisk.
"""
import argparse
import struct
import subprocess
import sys

SETOR = 512
KERNEL_ENDERECO = 0x100000
OPC_VESA = 1
TIPO_PARTICAO = 0xDA  # "dados sem sistema de arquivos": nenhum sistema tenta montar
GEOM_CABECAS, GEOM_SETORES = 255, 63
ALINHAMENTO_ARQUIVO = 64 * SETOR  # arquivo final múltiplo de 32 KB...
TAMANHO_MINIMO = 1024 * 1024      # ...e com pelo menos 1 MB (ver D004)


def setores(n):
    return (n + SETOR - 1) // SETOR


def preencher_setores(dados):
    return dados + b"\0" * ((-len(dados)) % SETOR)


def simbolos(elf):
    saida = subprocess.run(["nm", elf], check=True, capture_output=True, text=True).stdout
    tab = {}
    for linha in saida.splitlines():
        partes = linha.split()
        if len(partes) == 3:
            tab[partes[2]] = int(partes[0], 16)
    return tab


def chs(lba):
    """Converte LBA para o CHS de 3 bytes da tabela de partições (255 cabeças, 63 setores)."""
    c = lba // (GEOM_CABECAS * GEOM_SETORES)
    h = (lba // GEOM_SETORES) % GEOM_CABECAS
    s = lba % GEOM_SETORES + 1
    if c > 1023:
        c, h, s = 1023, 254, 63
    return bytes([h, s | ((c >> 2) & 0xC0), c & 0xFF])


def soma(dados):
    return sum(dados) & 0xFFFFFFFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mbr", required=True)
    ap.add_argument("--estagio2", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--kernel-elf", required=True)
    ap.add_argument("--ramdisk", required=True)
    ap.add_argument("--saida", required=True)
    ap.add_argument("--vesa", action="store_true", help="estágio 2 troca para o modo gráfico VESA")
    ap.add_argument("--console", action="store_true", help="sem área de trabalho: terminal de tela cheia")
    ap.add_argument("--vesa-largo", action="store_true", help="testes: prefere um modo largo de 768 linhas (1366x768)")
    ap.add_argument("--teste", default="", help="nome de um autoteste do kernel (só para os testes automáticos)")
    a = ap.parse_args()

    mbr = bytearray(open(a.mbr, "rb").read())
    e2 = open(a.estagio2, "rb").read()
    kern = open(a.kernel, "rb").read()
    rd = open(a.ramdisk, "rb").read()
    if len(mbr) != SETOR or mbr[510:512] != b"\x55\xAA":
        sys.exit("mkimagem: MBR inválido")
    if setores(len(e2)) > 64:
        sys.exit("mkimagem: estágio 2 maior que 32 KB")

    sim = simbolos(a.kernel_elf)
    if sim.get("_kernel_inicio") != KERNEL_ENDERECO:
        sys.exit("mkimagem: kernel não está ligado em 1 MB")
    kernel_fim = sim["_kernel_fim"]
    entrada = sim["_inicio"]
    rd_endereco = (kernel_fim + 0xFFF) & ~0xFFF

    e2_lba = 2
    k_lba = e2_lba + setores(len(e2))
    r_lba = k_lba + setores(len(kern))
    total = r_lba + setores(len(rd))

    opcoes = (OPC_VESA if a.vesa else 0) | (2 if a.console else 0) | (4 if a.vesa_largo else 0)
    cab = struct.pack("<IHHIIIIIIIIIIIIII",
                      0x44484753, 1, opcoes,
                      e2_lba, setores(len(e2)),
                      k_lba, setores(len(kern)), len(kern), KERNEL_ENDERECO,
                      r_lba, setores(len(rd)), len(rd), rd_endereco,
                      total, entrada, soma(kern), soma(rd))
    cab += a.teste.encode("latin-1")[:63].ljust(64, b"\0")

    # Tamanho do arquivo: arredondado para múltiplo de 32 KB. A partição cobre
    # do LBA 1 até o fim do arquivo e é a única, marcada como ativa (0x80),
    # porque algumas BIOS só tratam um pendrive como disco rígido se houver
    # uma partição ativa válida.
    tam_arquivo = ((total * SETOR) + ALINHAMENTO_ARQUIVO - 1) // ALINHAMENTO_ARQUIVO * ALINHAMENTO_ARQUIVO
    setores_arquivo = setores(max(tam_arquivo, TAMANHO_MINIMO))
    p_inicio, p_qtd = 1, setores_arquivo - 1
    entrada_part = (bytes([0x80]) + chs(p_inicio) + bytes([TIPO_PARTICAO]) + chs(p_inicio + p_qtd - 1)
                    + struct.pack("<II", p_inicio, p_qtd))
    mbr[0x1BE:0x1CE] = entrada_part

    img = bytes(mbr) + preencher_setores(cab) + preencher_setores(e2) + preencher_setores(kern) + preencher_setores(rd)
    assert len(img) == total * SETOR
    util = len(img)
    img += b"\0" * (setores_arquivo * SETOR - len(img))
    with open(a.saida, "wb") as f:
        f.write(img)
    with open(a.saida.rsplit("/", 1)[0] + "/tamanho.txt", "w") as f:
        f.write(str(util))

    print(f"mkimagem: estágio 2 LBA {e2_lba} ({setores(len(e2))} set), kernel LBA {k_lba} "
          f"({len(kern)} B em 0x{KERNEL_ENDERECO:X}, fim BSS 0x{kernel_fim:X}), "
          f"ramdisk LBA {r_lba} ({len(rd)} B em 0x{rd_endereco:X})")
    print(f"mkimagem: {util} bytes úteis ({100.0 * util / 1474560:.1f}% da meta de 1,44 MB), "
          f"arquivo {len(img)} bytes -> {a.saida}")


if __name__ == "__main__":
    main()
