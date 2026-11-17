#!/usr/bin/env python3
"""H113 - o instalador no espaço livre (D067; B2 e B6 da revisão do #53).

Primeira máquina: um disco de 512 MB "de outra pessoa", com
  - bytes conhecidos nos setores 1 a 62 (onde o GRUB guarda o core.img);
  - uma FAT32 "WINDOWS" ativa no setor 63 (até 100 MB);
  - 50 MB livres;
  - uma estendida de 150 a 300 MB com duas lógicas, uma delas ext3 de
    verdade (mkfs.ext3);
  - 212 MB livres no fim.
O instalador tem de achar os 212 MB do fim (a maior região) e deixá-los
marcados. Esc na caixa do kernel não grava nada. Depois da instalação:
  - todos os bytes fora do setor 0 e da região do SaguiOS estão iguais: as
    partições, as lógicas e os EBRs, os 50 MB do meio e os setores 1 a 62;
  - no setor 0, o código de boot é o do SaguiOS, com o cabeçalho no início
    da partição do sistema; o identificador e as duas entradas de hoje
    ficam byte a byte; as do SaguiOS vão nas vazias, sem tirar a ativa;
  - a FAT32 nova passa no fsck.vfat, a ext3 continua passando no e2fsck, e
    o setor 0 antigo está em .saguios/mbr-antes.bin.
Segunda máquina, ligando só por esse disco: monta a FAT32 do SaguiOS (e não
a "WINDOWS", que vem antes na tabela) e lê o arquivo de configurações.
Terceira e quarta máquinas: discos em que o espaço livre não pode ser
oferecido (partição dentro de outra, como no exemplo do B2; 4 primárias;
GPT; NTFS no disco inteiro; entrada além do fim). A opção vem desligada,
clicar nela não muda nada, e cada disco fica byte a byte igual.
"""
import hashlib
import os
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, SAIDA, IMAGEM  # noqa: E402

S = 512
MB = 2048


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def entrada(st, tipo, ini, n):
    return bytes([st, 0, 0, 0, tipo, 0, 0, 0]) + struct.pack("<II", ini, n)


def setor_tabela(img, lba, entradas, ident=None):
    s = bytearray(img[lba * S:(lba + 1) * S])
    if ident is not None:
        s[440:444] = struct.pack("<I", ident)
    s[446:510] = bytes(64)
    for k, e in enumerate(entradas):
        s[446 + k * 16:462 + k * 16] = e
    s[510:512] = b"\x55\xaa"
    img[lba * S:(lba + 1) * S] = s


def formatar(cmd, n, nome):
    tmp = os.path.join(SAIDA, f"h113-{nome}.tmp")
    with open(tmp, "wb") as f:
        f.truncate(n * S)
    subprocess.run(cmd + [tmp], check=True, capture_output=True)
    dados = open(tmp, "rb").read()
    os.remove(tmp)
    return dados


def sha(caminho):
    return hashlib.sha256(open(caminho, "rb").read()).hexdigest()


def criar(caminho, setores, monta):
    img = bytearray(hashlib.sha256(os.path.basename(caminho).encode()).digest() * (setores * S // 32))
    monta(img)
    with open(caminho, "wb") as f:
        f.write(img)


# O disco da primeira máquina.
TOTAL_A = 512 * MB
P1 = (63, 100 * MB - 63)                       # FAT32 "WINDOWS", ativa
EXT = (150 * MB, 150 * MB)                     # estendida
L1 = (EXT[0] + 2048, 100000)                   # ext3
EBR2 = EXT[0] + 104448
L2 = (EBR2 + 2048, 50000)
ID = 0xCAFEBABE


def monta_a(img):
    img[P1[0] * S:(P1[0] + P1[1]) * S] = formatar(["mkfs.vfat", "-F", "32", "-n", "WINDOWS"], P1[1], "fat")
    img[L1[0] * S:(L1[0] + L1[1]) * S] = formatar(["mkfs.ext3", "-q", "-F", "-L", "linux"], L1[1], "ext3")
    setor_tabela(img, 0, [entrada(0x80, 0x0C, *P1), entrada(0, 0x05, *EXT)], ident=ID)
    setor_tabela(img, EXT[0], [entrada(0, 0x83, L1[0] - EXT[0], L1[1]), entrada(0, 0x05, EBR2 - EXT[0], 60000)])
    setor_tabela(img, EBR2, [entrada(0, 0x83, L2[0] - EBR2, L2[1])])


# Os discos em que o espaço livre não pode ser oferecido: (nome, setores, entradas do setor 0 ou função, trecho do motivo).
RECUSAS = [
    ("dentro", 1024 * MB, [entrada(0x80, 0x07, 1 * MB, 899 * MB), entrada(0, 0x83, 10 * MB, 1 * MB)], "se sobrep"),
    ("quatro", 512 * MB, [entrada(0, 0x07, MB, 10 * MB), entrada(0, 0x83, 20 * MB, 10 * MB),
                          entrada(0, 0x83, 40 * MB, 10 * MB), entrada(0, 0x83, 60 * MB, 10 * MB)], "2 entradas vazias"),
    ("gpt", 512 * MB, "gpt", "GPT"),
    ("ntfs", 512 * MB, "ntfs", "volume, sem tabela"),
    ("alem", 512 * MB, [entrada(0x80, 0x83, MB, 600 * MB)], "passa do fim"),
]


def monta_recusa(img, como):
    if como == "gpt":
        setor_tabela(img, 0, [entrada(0, 0xEE, 1, len(img) // S - 1)])
        img[S:S + 8] = b"EFI PART"
    elif como == "ntfs":
        s = bytearray(img[0:S])
        s[3:11] = b"NTFS    "
        s[510:512] = b"\x55\xaa"
        img[0:S] = s
    else:
        setor_tabela(img, 0, como)


class Instalador:
    """Os cliques, pela mesma conta do medir() e dos DISCO_Y/MODO_Y do instalar.c."""

    def __init__(self, m, w):
        self.m, self.w = m, w
        lado = 212 if w["l"] >= 800 else 0
        mg = 28 if w["l"] >= 560 else 16
        self.x0 = lado + mg
        self.cl = w["l"] - self.x0 - mg
        self.rodape = w["a"] - 58
        self.compacto = w["a"] < 460

    def clicar(self, x, y):
        self.m.clique(self.w["x"] + x, self.w["y"] + y)

    def continuar(self):
        self.clicar(self.x0 + self.cl - 70, self.rodape + 17)

    def cancelar(self):
        self.clicar(self.x0 + 55, self.rodape + 17)

    def disco(self, i):
        self.clicar(self.x0 + self.cl // 2, 86 + i * 86 + 37)

    def modo(self, i):
        self.clicar(self.x0 + self.cl // 2, 72 + i * (52 if self.compacto else 66) + 20)


def abrir(m):
    n = len(m.serial())
    m.digitar("instalar\n", 0.03)
    esperar(m, "instalar: passo 0 (Boas-vindas)", 20, n)
    time.sleep(1)
    return Instalador(m, janela(m.serial()[n:], "Instalar o SaguiOS")), n


def ate_particoes(m, inst, n, i):
    inst.continuar()
    esperar(m, "instalar: passo 1 (Disco)", 10, n)
    time.sleep(0.5)
    inst.disco(i)
    esperar(m, f"instalar: disco hd{i} escolhido", 10, n)
    inst.continuar()
    esperar(m, "instalar: passo 2 (Partições)", 10, n)
    time.sleep(0.6)


def main():
    os.makedirs(SAIDA, exist_ok=True)
    hd = os.path.join(SAIDA, "h113-hd.img")
    criar(hd, TOTAL_A, monta_a)
    antes = open(hd, "rb").read()
    original = hashlib.sha256(antes).hexdigest()

    # 1. Instalar no espaço livre.
    with Maquina("h113a", "ide", memoria="768M", extras=["-drive", f"format=raw,file={hd},if=ide,index=1"]) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        inst, n = abrir(m)
        ate_particoes(m, inst, n, 1)
        verificar("instalar: espaço livre: 212 MB em 614400" in m.serial()[n:],
                  "o instalador acha a maior região livre (212 MB no fim, não os 50 MB do meio)")
        m.captura("H113-1-particoes")
        inst.continuar()
        esperar(m, "instalar: passo 3 (Resumo)", 10, n)
        time.sleep(0.6)
        m.captura("H113-2-resumo")
        k = len(m.serial())
        inst.continuar()   # Instalar
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, k)
        caixa = m.serial()[k:]
        verificar("usar o espa" in caixa and "212 MB, a partir de 300 MB" in caixa and "Tabela MBR: 4 parti" in caixa,
                  "a caixa do kernel mostra a tabela e o resumo do espaço livre")
        time.sleep(0.6)
        m.captura("H113-3-confirmacao")
        m.tecla("esc")
        esperar(m, "instalar: liberar hd1 -> -14", 10, k)
        time.sleep(1)
        verificar(sha(hd) == original, "Esc na caixa do kernel: o disco inteiro está byte a byte igual")
        k = len(m.serial())
        inst.continuar()
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, k)
        time.sleep(0.5)
        m.digitar("hd1\n", 0.05)
        esperar(m, "instalar: pronto em hd1", 240, k)
        verificar("espaço livre" in m.serial()[k:] and "instalar: erro" not in m.serial()[k:], "o instalador terminou sem erro, no espaço livre")
        time.sleep(1)
        m.captura("H113-4-pronto")
        m.tecla("ret")
        esperar(m, "sagui:/$", 10, k)

    d = open(hd, "rb").read()
    o = open(IMAGEM, "rb").read()
    mbr = d[:S]
    e = [struct.unpack_from("<BBBBBBBBII", mbr, 446 + k * 16) for k in range(4)]
    s_ini = 300 * MB
    verificar(e[2][4] == 0xDA and e[2][8] == s_ini and e[2][0] == 0, f"entrada 3: o sistema em 300 MB, sem tirar a ativa da FAT32 ({e[2]})")
    verificar(e[3][4] == 0x0C and e[3][8] == e[2][8] + e[2][9] and e[3][8] + e[3][9] <= TOTAL_A, f"entrada 4: a FAT32 do SaguiOS logo depois ({e[3]})")
    fim_saguios = e[3][8] + e[3][9]
    verificar(mbr[446:478] == antes[446:478], "as entradas 1 e 2 (a FAT32 de hoje e a estendida) estão byte a byte iguais")
    verificar(mbr[440:446] == antes[440:446], "o identificador do disco ficou (o Windows e o Linux dependem dele)")
    verificar(mbr[:0x1B4] == o[:0x1B4] and struct.unpack_from("<I", mbr, 0x1B4)[0] == s_ini,
              "o código de boot é o do SaguiOS, com o cabeçalho no início da partição do sistema")
    verificar(d[S:63 * S] == antes[S:63 * S], "os setores 1 a 62 (o core.img do GRUB) estão iguais")
    for nome, (a, q) in (("a FAT32 WINDOWS", P1), ("a estendida inteira, com os EBRs e as lógicas", EXT), ("a lógica ext3", L1), ("a outra lógica", L2)):
        verificar(d[a * S:(a + q) * S] == antes[a * S:(a + q) * S], f"{nome} está byte a byte igual")
    verificar(d[S:s_ini * S] == antes[S:s_ini * S] and d[fim_saguios * S:] == antes[fim_saguios * S:],
              "todo o disco fora do setor 0 e da região do SaguiOS está byte a byte igual")
    cab = d[s_ini * S:(s_ini + 1) * S]
    verificar(cab[:4] == b"SGHD" and struct.unpack_from("<I", cab, 8)[0] == s_ini + 1, "o cabeçalho de boot está no início da partição do sistema")
    for nome, (a, q), cmd in (("FAT32 do SaguiOS", (e[3][8], e[3][9]), ["fsck.vfat", "-n"]), ("ext3", L1, ["e2fsck", "-fn"])):
        tmp = os.path.join(SAIDA, "h113-fsck.img")
        with open(tmp, "wb") as g:
            g.write(d[a * S:(a + q) * S])
        r = subprocess.run(cmd + [tmp], capture_output=True, text=True)
        verificar(r.returncode == 0, f"{nome}: {' '.join(cmd)} passa")
        if nome.startswith("FAT32"):
            r = subprocess.run(["mtype", "-i", tmp, "::/.saguios/mbr-antes.bin"], capture_output=True)
            verificar(r.stdout == antes[:S], "o setor 0 de antes está guardado em .saguios/mbr-antes.bin")
        os.remove(tmp)

    # 2. Ligar só pelo disco instalado.
    with Maquina("h113b", "ide", imagem=hd, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("tem configuracoes do SaguiOS: montado sozinho" in m.serial(), "ligado pelo disco, a FAT32 do SaguiOS monta sozinha")
        out = m.comando("cat /usb/.saguios/configuracoes.txt")
        verificar("sistema.instalado = sim" in out, "e é a do SaguiOS, não a WINDOWS que vem antes na tabela")

    # 3. Os discos em que o espaço livre não pode ser oferecido.
    discos = []
    for nome, setores, como, motivo in RECUSAS:
        caminho = os.path.join(SAIDA, f"h113-{nome}.img")
        criar(caminho, setores, lambda img, c=como: monta_recusa(img, c))
        discos.append((nome, caminho, motivo, sha(caminho)))
    for g in range(0, len(discos), 3):
        grupo = discos[g:g + 3]
        extras = []
        for i, (_, caminho, _, _) in enumerate(grupo):
            extras += ["-drive", f"format=raw,file={caminho},if=ide,index={i + 1}"]
        with Maquina(f"h113-recusa{g // 3}", "ide", memoria="768M", extras=extras) as m:
            m.esperar_serial("sagui:/$", 120)
            time.sleep(1)
            for i, (nome, _, motivo, _) in enumerate(grupo):
                inst, n = abrir(m)
                ate_particoes(m, inst, n, i + 1)
                txt = m.serial()[n:]
                r = re.search(r"instalar: espaço livre: não \(([^)]*)\)", txt)
                verificar(r is not None and motivo in r.group(1), f"{nome}: sem espaço livre ({r.group(1) if r else txt[-120:]!r})")
                if g == 0 and i == 0:
                    m.captura("H113-5-sem-espaco-livre")
                inst.modo(0)
                time.sleep(1)
                verificar("instalar: modo espa" not in m.serial()[n:], f"{nome}: clicar na opção desligada não muda nada")
                inst.cancelar()
                esperar(m, "sagui:/$", 10, n)
                time.sleep(0.5)
    for nome, caminho, _, soma in discos:
        verificar(sha(caminho) == soma, f"{nome}: o disco está byte a byte igual")


if __name__ == "__main__":
    main()
