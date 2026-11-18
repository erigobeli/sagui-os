#!/usr/bin/env python3
"""H110 - o instalador, apagando o disco inteiro (issue #45, parte 3; D064).

O disco de teste é "de quem testa": bytes conhecidos em todo ele e uma
tabela com uma partição NTFS e uma Linux.

Primeira máquina, só os caminhos que não podem gravar nada:
  - Cancelar no passo do disco fecha o instalador;
  - no resumo, Enter não instala (nem abre a caixa do kernel);
  - Instalar abre a caixa do kernel, que mostra o disco e o resumo; nome
    errado e Esc voltam ao resumo sem liberar nada;
  - depois disso, o disco inteiro continua byte a byte igual.

Segunda máquina, a instalação: Instalar, o nome certo na caixa, e pronto.
Aqui fora, no arquivo do disco:
  - o MBR: código de boot do SaguiOS, identificador novo, 0xDA (ativa) em
    1 MB e 0x0C (FAT32) alinhada em 1 MB, as outras duas entradas vazias;
  - o resto do primeiro MB e os 33 setores do fim (a cópia da GPT) zerados;
  - a partição do sistema igual, byte a byte, à da origem, e o cabeçalho de
    boot apontando para ela;
  - a FAT32 passa no fsck.vfat.
A instalação começa pelo menu Iniciar (a busca acha o instalador).
Terceira máquina, ligando só pelo disco instalado: a partição dos arquivos
monta sozinha, o instalador some do menu e da área de trabalho (o Disco fica
na coluna da direita), e um arquivo gravado nela continua lá depois de
reiniciar.
Capturas de cada passo, para a revisão a olho.
"""
import hashlib
import os
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, SAIDA, IMAGEM  # noqa: E402

MB = 2048
TAM = 256 * 1024 * 1024


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def criar_disco(caminho):
    bloco = hashlib.sha256(b"h110").digest() * (1 << 15)   # 1 MB
    with open(caminho, "wb") as f:
        for _ in range(TAM // len(bloco)):
            f.write(bloco)
        mbr = bytearray(bloco[:512])
        mbr[446:510] = bytes(64)
        mbr[446:462] = bytes([0x80, 0, 0, 0, 0x07, 0, 0, 0]) + struct.pack("<II", 1 * MB, 100 * MB)
        mbr[462:478] = bytes([0, 0, 0, 0, 0x83, 0, 0, 0]) + struct.pack("<II", 101 * MB, 100 * MB)
        mbr[510:512] = b"\x55\xaa"
        f.seek(0)
        f.write(mbr)


class Instalador:
    """Os botões do rodapé, pela mesma conta do medir() do instalar.c."""

    def __init__(self, m, w):
        self.m, self.w = m, w
        lado = 212 if w["l"] >= 800 else 0
        mg = 28 if w["l"] >= 560 else 16
        self.x0 = lado + mg
        self.cl = w["l"] - self.x0 - mg
        self.rodape = w["a"] - 58

    def clicar(self, x, y):
        self.m.clique(self.w["x"] + x, self.w["y"] + y)

    def continuar(self):          # também é o "Instalar" no resumo
        self.clicar(self.x0 + self.cl - 70, self.rodape + 17)

    def cancelar(self):
        self.clicar(self.x0 + 55, self.rodape + 17)


def abrir(m):
    n = len(m.serial())
    m.digitar("instalar\n", 0.03)
    esperar(m, "instalar: passo 0 (Boas-vindas)", 20, n)
    time.sleep(1)
    return Instalador(m, janela(m.serial()[n:], "Instalar o SaguiOS")), n


def ate_o_resumo(m, inst, n, capturar=False):
    inst.continuar()
    esperar(m, "instalar: passo 1 (Disco)", 10, n)
    time.sleep(0.6)
    if capturar:
        m.captura("H110-2-disco")
    inst.continuar()
    esperar(m, "instalar: passo 2 (Partições)", 10, n)
    time.sleep(0.6)
    verificar("instalar: espaço livre: não (o maior espaço livre tem 55 MB" in m.serial()[n:],
              "no disco com NTFS e Linux, o espaço livre (55 MB) não basta: fica marcado o disco inteiro")
    if capturar:
        m.captura("H110-2b-particoes")
    inst.continuar()
    esperar(m, "instalar: passo 3 (Resumo)", 10, n)
    time.sleep(0.6)


def main():
    os.makedirs(SAIDA, exist_ok=True)
    hd = os.path.join(SAIDA, "h110-hd.img")
    criar_disco(hd)
    original = hashlib.sha256(open(hd, "rb").read()).hexdigest()
    extras = ["-drive", f"format=raw,file={hd},if=ide,index=1"]

    # 1. Os caminhos que não gravam.
    with Maquina("h110a", "ide", memoria="768M", extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        inst, n = abrir(m)
        m.captura("H110-1-boas-vindas")
        inst.continuar()
        esperar(m, "instalar: passo 1 (Disco)", 10, n)
        time.sleep(0.5)
        inst.cancelar()
        esperar(m, "sagui:/$", 10, n)
        verificar("[GUI] confirmacao do disco" not in m.serial()[n:], "Cancelar no passo do disco fecha sem pedir o disco")

        inst, n = abrir(m)
        ate_o_resumo(m, inst, n)
        m.tecla("ret")
        time.sleep(2)
        verificar("[GUI] confirmacao do disco" not in m.serial()[n:] and "instalar: liberar" not in m.serial()[n:],
                  "no resumo, Enter não instala nem abre a caixa do kernel")

        inst.continuar()   # Instalar
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        caixa = m.serial()[n:]
        verificar("disco interno" in caixa and "NTFS" in caixa, "a caixa mostra o disco e o que tem nele hoje (do kernel)")
        verificar("O programa diz que vai:" in caixa and "apagar todas as parti" in caixa and "copiar o SaguiOS" in caixa,
                  "a caixa mostra o resumo do instalador")
        verificar("hd1 (2) e tudo o que" in caixa and "Tabela MBR: 2 parti" in caixa,
                  "o resumo conta as partições que o kernel leu na tabela (D066)")
        time.sleep(0.8)
        m.captura("H110-4-confirmacao")
        m.digitar("hd0\n", 0.05)
        esperar(m, "confirmacao do disco hd1: nome diferente", 10, n)
        m.tecla("esc")
        esperar(m, "instalar: liberar hd1 -> -14", 10, n)
        time.sleep(0.5)
        verificar("instalar: passo 4" not in m.serial()[n:], "nome errado e Esc: volta ao resumo, sem instalar")
        k = len(m.serial())
        inst.continuar()
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, k)
        time.sleep(0.5)
        m.tecla("esc")
        esperar(m, "instalar: liberar hd1 -> -14", 10, k)
        inst.cancelar()
        esperar(m, "sagui:/$", 10, n)
    verificar(hashlib.sha256(open(hd, "rb").read()).hexdigest() == original,
              "depois de Cancelar, Enter, nome errado e Esc, o disco inteiro está byte a byte igual")

    # 2. A instalação, aberta pelo menu Iniciar.
    with Maquina("h110b", "ide", memoria="768M", extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n = len(m.serial())
        m.clique(40, 12)
        esperar(m, "[GUI] menu aberto", 10, n)
        time.sleep(0.6)
        m.digitar("instalar", 0.08)
        esperar(m, '[GUI] menu: busca "instalar" (1 resultados)', 10, n)
        m.digitar("\n", 0.05)
        esperar(m, "instalar: passo 0 (Boas-vindas)", 20, n)
        verificar(True, "o menu Iniciar acha e abre o instalador")
        time.sleep(1)
        inst = Instalador(m, janela(m.serial()[n:], "Instalar o SaguiOS"))
        ate_o_resumo(m, inst, n, capturar=True)
        m.captura("H110-3-resumo")
        inst.continuar()
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.5)
        m.digitar("hd1\n", 0.05)
        esperar(m, "instalar: passo 4 (Instalação)", 20, n)
        time.sleep(2)
        m.captura("H110-5-instalando")
        esperar(m, "instalar: pronto em hd1", 240, n)
        time.sleep(1)
        m.captura("H110-6-pronto")
        verificar("instalar: erro" not in m.serial()[n:], "o instalador terminou sem erro")
        m.tecla("ret")   # Enter na tela final fecha
        esperar(m, "(instalar) terminou com codigo 0", 10, n)

    d = open(hd, "rb").read()
    o = open(IMAGEM, "rb").read()
    mbr = d[:512]
    verificar(mbr[:0x1B4] == o[:0x1B4] and mbr[510:512] == b"\x55\xaa", "o MBR tem o código de boot do SaguiOS e a assinatura")
    verificar(struct.unpack_from("<I", mbr, 0x1B4)[0] == MB, "o MBR aponta o cabeçalho de boot no início da partição do sistema (D067)")
    verificar(mbr[440:444] not in (b"\0\0\0\0", o[440:444]), f"o identificador do disco é novo ({mbr[440:444].hex()})")
    e = [struct.unpack_from("<BBBBBBBBII", mbr, 446 + k * 16) for k in range(4)]
    verificar(e[0][0] == 0x80 and e[0][4] == 0xDA and e[0][8] == MB, f"entrada 1: o sistema (0xDA, ativa) em 1 MB ({e[0]})")
    verificar(e[1][4] == 0x0C and e[1][8] % MB == 0 and e[1][8] == e[0][8] + e[0][9], f"entrada 2: a FAT32 logo depois, alinhada em 1 MB ({e[1]})")
    verificar(mbr[478:510] == bytes(32), "as entradas 3 e 4 estão vazias (as partições antigas sumiram)")
    verificar(d[512:MB * 512] == bytes(MB * 512 - 512), "o resto do primeiro MB, o setor 1 inclusive, está zerado (sem restos de GPT ou carregador)")
    verificar(d[-33 * 512:] == bytes(33 * 512), "os 33 setores do fim (a cópia da GPT) estão zerados")
    cab_o, cab_d = o[512:1024], d[MB * 512:(MB + 1) * 512]
    sis_ini, fim = struct.unpack_from("<I", cab_o, 8)[0], struct.unpack_from("<I", cab_o, 48)[0]
    sis_n = fim - sis_ini
    verificar(d[(MB + 1) * 512:(MB + 1 + sis_n) * 512] == o[sis_ini * 512:fim * 512], f"o sistema ({sis_n} setores) é a cópia exata da origem, depois do cabeçalho")
    for campo in (8, 16, 32):
        verificar(struct.unpack_from("<I", cab_d, campo)[0] == struct.unpack_from("<I", cab_o, campo)[0] - sis_ini + MB + 1,
                  f"o cabeçalho de boot aponta para o lugar novo (campo {campo})")
    fat = os.path.join(SAIDA, "h110-fat.img")
    with open(fat, "wb") as g:
        g.write(d[e[1][8] * 512:(e[1][8] + e[1][9]) * 512])
    r = subprocess.run(["fsck.vfat", "-n", fat], capture_output=True, text=True)
    os.remove(fat)
    verificar(r.returncode == 0, f"fsck.vfat: a FAT32 do instalador está íntegra ({r.stdout.strip().splitlines()[-1]})")

    # 3. Ligar só pelo disco instalado; gravar um arquivo; reiniciar; ler.
    with Maquina("h110c", "ide", imagem=hd, memoria="768M", reiniciar=True) as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("tem configuracoes do SaguiOS: montado sozinho" in m.serial(), "a partição dos arquivos monta sozinha no boot")
        verificar("coluna da direita: instalador nao, 1 unidades" in m.serial(),
                  "sem o instalador na área de trabalho, e o Disco na coluna da direita")
        n = len(m.serial())
        m.clique(40, 12)
        esperar(m, "[GUI] menu aberto", 10, n)
        time.sleep(0.6)
        m.digitar("instalar", 0.08)
        esperar(m, '[GUI] menu: busca "instalar" (0 resultados)', 10, n)
        verificar(True, "ligado pelo disco instalado, o instalador não aparece no menu")
        m.tecla("esc")
        time.sleep(0.8)
        out = m.comando("cat /usb/.saguios/configuracoes.txt")
        verificar("sistema.instalado = sim" in out, "as configurações dizem que o sistema está instalado")
        n = len(m.serial())
        m.digitar("echo guardado no disco > /usb/nota.txt\n", 0.03)
        time.sleep(1.5)
        m.digitar("reiniciar\n", 0.03)
        fim = time.time() + 180
        while time.time() < fim and m.serial()[n:].count("sagui:/$") < 2:
            time.sleep(0.3)
        n2 = len(m.serial())
        verificar("[SAGUI] reiniciando" in m.serial()[n:], "o sistema reiniciou")
        m.digitar("cat /usb/nota.txt\n", 0.03)
        time.sleep(2)
        m.captura("H110-7-ligado-pelo-disco")
        verificar("guardado no disco" in m.serial()[n2:], "o arquivo continua no disco depois de reiniciar")


if __name__ == "__main__":
    main()
