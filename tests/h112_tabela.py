#!/usr/bin/env python3
"""H112 - a tabela de partições lida e conferida pelo kernel (D066; B3 a B5 da revisão do #53).

O sistema liga pelo pendrive e cada máquina tem 3 discos IDE, cada um com um
caso que o instalador precisa entender antes de pensar em gravar:
  - ext (0x83), partição em 63, estendida com duas lógicas;
  - partição que passa do fim do disco (fica como está, sem cortar);
  - partições sobrepostas, inclusive uma dentro da outra;
  - GPT com a entrada 0xEE, GPT sem ela (setor 0 zerado);
  - FAT32 e NTFS gravados no disco inteiro (o setor 0 é um VBR, não tabela);
  - setor 0 zerado, sem a assinatura, entrada com status inválido, entrada
    começando no setor 1;
  - lógica fora da estendida e cadeia de EBRs em laço.
O programa /testes/tabela mostra o que o kernel leu; o teste confere o
estado, se a tabela foi conferida, o problema e cada partição, com os
números exatamente como estão no disco. A caixa de confirmação do kernel
mostra a mesma leitura. No fim, cada disco está byte a byte igual.
"""
import hashlib
import os
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, SAIDA  # noqa: E402

TOTAL = 131072          # setores: 64 MB
S = 512


def entrada(st, tipo, ini, n):
    return bytes([st, 0, 0, 0, tipo, 0, 0, 0]) + struct.pack("<II", ini, n)


def tabela(entradas, base=None):
    """Um setor de MBR/EBR: código aleatório, as entradas e 0x55AA."""
    s = bytearray(base if base is not None else os.urandom(S))
    s[446:510] = bytes(64)
    for k, e in enumerate(entradas):
        s[446 + k * 16:462 + k * 16] = e
    s[510:512] = b"\x55\xaa"
    return bytes(s)


def ext_magico(img, ini):
    img[(ini + 2) * S + 56:(ini + 2) * S + 58] = b"\x53\xef"


def ntfs_vbr(img, lba):
    s = bytearray(img[lba * S:(lba + 1) * S])
    s[3:11] = b"NTFS    "
    s[510:512] = b"\x55\xaa"
    img[lba * S:(lba + 1) * S] = s


def criar(nome, monta):
    caminho = os.path.join(SAIDA, f"h112-{nome}.img")
    img = bytearray(hashlib.sha256(nome.encode()).digest() * (TOTAL * S // 32))
    monta(img)
    with open(caminho, "wb") as f:
        f.write(img)
    return caminho


def c_ext(img):
    img[0:S] = tabela([entrada(0x80, 0x83, 2048, 40960)])
    ext_magico(img, 2048)


def c_63(img):
    img[0:S] = tabela([entrada(0x00, 0x07, 63, 40000)])
    ntfs_vbr(img, 63)


def c_estendida(img):
    img[0:S] = tabela([entrada(0x80, 0x07, 2048, 20480), entrada(0, 0x05, 22528, 65536)])
    ntfs_vbr(img, 2048)
    img[22528 * S:22529 * S] = tabela([entrada(0, 0x83, 2048, 20480), entrada(0, 0x05, 24576, 12288)])
    ext_magico(img, 22528 + 2048)
    img[47104 * S:47105 * S] = tabela([entrada(0, 0x83, 2048, 10240)])


def c_alem(img):
    img[0:S] = tabela([entrada(0x80, 0x83, 2048, 200000)])


def c_sobrepostas(img):   # o exemplo do B2: uma pequena dentro de uma grande
    img[0:S] = tabela([entrada(0x80, 0x07, 2048, 100000), entrada(0, 0x83, 20480, 2048)])


def c_gpt(img):
    img[0:S] = tabela([entrada(0, 0xEE, 1, TOTAL - 1)])
    img[S:S + 8] = b"EFI PART"


def c_gpt_sem_ee(img):
    img[0:S] = bytes(S)
    img[S:S + 8] = b"EFI PART"


def c_fat32(img):
    caminho = os.path.join(SAIDA, "h112-fat-tmp.img")
    with open(caminho, "wb") as f:
        f.write(bytes(img))
    subprocess.run(["mkfs.vfat", "-F", "32", "-n", "VOLUME", caminho], check=True, capture_output=True)
    img[:] = open(caminho, "rb").read()
    os.remove(caminho)
    s = bytearray(img[0:S])               # o código de boot do VBR, onde seria a tabela
    s[446:510] = os.urandom(64)
    img[0:S] = s


def c_ntfs(img):
    ntfs_vbr(img, 0)


def c_vazio(img):
    img[0:S] = bytes(S)


def c_sem_assinatura(img):
    s = bytearray(img[0:S])
    s[510:512] = b"\x12\x34"
    img[0:S] = s


def c_status(img):
    img[0:S] = tabela([entrada(0x12, 0x83, 2048, 4096)])


def c_setor1(img):
    img[0:S] = tabela([entrada(0x80, 0x0C, 1, 4096)])


def c_logica_fora(img):
    img[0:S] = tabela([entrada(0, 0x0F, 2048, 8192)])
    img[2048 * S:2049 * S] = tabela([entrada(0, 0x83, 2048, 20000)])


def c_laco(img):
    img[0:S] = tabela([entrada(0, 0x05, 2048, 65536)])
    img[2048 * S:2049 * S] = tabela([entrada(0, 0x83, 2048, 4096), entrada(0, 0x05, 8192, 8192)])
    img[10240 * S:10241 * S] = tabela([entrada(0, 0x83, 2048, 4096), entrada(0, 0x05, 0, 8192)])   # volta para o primeiro


# (nome, montagem, estado, conferida, trecho do problema, maior_fim, partições esperadas)
CASOS = [
    ("ext", c_ext, "mbr", 1, "", 43008, [(1, 0x83, 2048, 40960, "ext")]),
    ("em63", c_63, "mbr", 1, "", 40063, [(1, 0x07, 63, 40000, "NTFS")]),
    ("estendida", c_estendida, "mbr", 1, "", 88064,
     [(1, 0x07, 2048, 20480, "NTFS"), (2, 0x05, 22528, 65536, "-"), (5, 0x83, 24576, 20480, "ext"), (6, 0x83, 49152, 10240, "-")]),
    ("alem", c_alem, "mbr", 0, "passa do fim", 202048, [(1, 0x83, 2048, 200000, "-")]),
    ("sobrepostas", c_sobrepostas, "mbr", 0, "se sobrep", 102048, [(1, 0x07, 2048, 100000, "-"), (2, 0x83, 20480, 2048, "-")]),
    ("gpt", c_gpt, "gpt", 0, "GPT", 0, []),
    ("gptsemee", c_gpt_sem_ee, "gpt", 0, "sem a entrada protetora", 0, []),
    ("fat32", c_fat32, "vbr", 0, "sem tabela", TOTAL, [(0, 0, 0, TOTAL, "FAT32")]),
    ("ntfs", c_ntfs, "vbr", 0, "sem tabela", TOTAL, [(0, 0, 0, TOTAL, "NTFS")]),
    ("vazio", c_vazio, "vazio", 1, "", 0, []),
    ("semassinatura", c_sem_assinatura, "desconhecida", 0, "0x55AA", 0, []),
    ("status", c_status, "mbr", 0, "status", 6144, [(1, 0x83, 2048, 4096, "-")]),
    ("setor1", c_setor1, "mbr", 0, "antes do 2", 4097, [(1, 0x0C, 1, 4096, "-")]),
    ("logicafora", c_logica_fora, "mbr", 0, "sai do seu lugar", 24096, [(1, 0x0F, 2048, 8192, "-"), (5, 0x83, 4096, 20000, "-")]),
    ("laco", c_laco, "mbr", 0, "volta para tr", 67584,
     [(1, 0x05, 2048, 65536, "-"), (5, 0x83, 4096, 4096, "-"), (6, 0x83, 12288, 4096, "-")]),
]


def main():
    os.makedirs(SAIDA, exist_ok=True)
    discos = [(c, criar(c[0], c[1])) for c in CASOS]
    somas = {caminho: hashlib.sha256(open(caminho, "rb").read()).hexdigest() for _, caminho in discos}
    for g in range(0, len(discos), 3):   # 3 IDE: o pendrive de boot ocupa uma das 4 vagas (MAX_DISCOS)
        grupo = discos[g:g + 3]
        extras = []
        for i, (_, caminho) in enumerate(grupo):
            extras += ["-drive", f"format=raw,file={caminho},if=ide,index={i}"]
        with Maquina(f"h112-{g // 3}", "usb", memoria="512M", extras=extras) as m:
            m.esperar_serial("sagui:/$", 120)
            time.sleep(1)
            for i, (caso, _) in enumerate(grupo):
                nome, _, estado, conf, prob, fim, parts = caso
                out = m.comando(f"/testes/tabela hd{i}")
                cab = re.search(rf'tabela: hd{i} (\S+) conferida (\d) fim (\d+) n (\d+) problema "([^"]*)"', out)
                if not cab:
                    raise FalhaTeste(f"{nome}: sem resposta da tabela ({out[-200:]!r})")
                achado = [(int(a), int(b, 16), int(c), int(d), e) for a, b, c, d, e in
                          re.findall(rf"tabela: hd{i} p (\d+) tipo ([0-9a-f]+) ini (\d+) n (\d+) (\S+)", out)]
                verificar(cab.group(1) == estado and int(cab.group(2)) == conf and prob in cab.group(5),
                          f"{nome}: {cab.group(1)}, conferida {cab.group(2)}, problema \"{cab.group(5)}\"")
                verificar(int(cab.group(3)) == fim, f"{nome}: o maior fim é {cab.group(3)} (esperado {fim})")
                verificar(achado == parts, f"{nome}: as partições como estão no disco {achado}")
            if g == 0:   # a caixa do kernel mostra a mesma leitura (o disco com a estendida)
                n = len(m.serial())
                m.digitar("/testes/setores hd2 0 ler liberar\n", 0.03)
                fim = time.time() + 20
                while time.time() < fim and "[GUI] confirmacao do disco hd2 aberta" not in m.serial()[n:]:
                    time.sleep(0.3)
                time.sleep(0.8)
                m.captura("H112-caixa-com-a-tabela")
                verificar(re.search(r"confirmacao: Tabela MBR: 4 parti.{1,4}es \(2 l.{1,2}gicas\)", m.serial()[n:]) is not None,
                          "a caixa do kernel mostra a tabela que ele leu")
                m.tecla("esc")
                time.sleep(1)
    for caso, caminho in discos:
        verificar(hashlib.sha256(open(caminho, "rb").read()).hexdigest() == somas[caminho],
                  f"{caso[0]}: o disco está byte a byte igual depois da leitura")


if __name__ == "__main__":
    main()
