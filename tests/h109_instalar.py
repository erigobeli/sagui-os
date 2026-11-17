#!/usr/bin/env python3
"""H109 - instalar (issue #45, parte 2): o SaguiOS num disco rígido, com a
escrita crua travada no kernel atrás de uma confirmação (D063).

Critérios:
  - o kernel acha o disco de onde o sistema ligou (hd0, pelo identificador do
    MBR) e o instalador oferece só o outro, escrito "disco interno";
  - sem a confirmação, gravar setores crus em qualquer disco e ler um disco
    interno devolve ERRO_ACESSO (-14); ler a partição do sistema da origem dá;
  - a caixa do kernel: nome errado não libera, Esc cancela (ERRO_ACESSO), o
    nome certo libera; a liberação acaba quando o processo termina;
  - "instalar hd1 --sim" não existe mais;
  - o menu Iniciar acha e abre o instalador; ligado pelo disco instalado, o
    instalador não aparece mais no menu (a busca não acha);
  - pelo assistente (Enter em cada passo), com "hd1" digitado na caixa do
    kernel, o instalador termina ("instalar: pronto em hd1");
  - a tabela de partições do disco instalado: 0xDA (sistema) e 0x0C (FAT32,
    alinhada em 1 MB), identificador do MBR novo, e o fsck.vfat passa;
  - ligando só pelo disco instalado: a partição dos arquivos monta sozinha, e
    um arquivo gravado nela continua lá depois de reiniciar;
  - capturas para a revisão a olho.
"""
import os
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, SAIDA, IMAGEM  # noqa: E402

ERRO_ACESSO = -14


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def setores(m, args):
    """Roda /testes/setores e devolve o código da última operação."""
    n = len(m.serial())
    m.digitar(f"/testes/setores {args}\n", 0.03)
    esperar(m, "setores: ", 20, n)
    esperar(m, "sagui:/$", 20, n)
    return int(re.findall(r"setores: (?:ler|gravar) \w+ \d+ -> (-?\d+)", m.serial()[n:])[-1])


def main():
    os.makedirs(SAIDA, exist_ok=True)
    hd = os.path.join(SAIDA, "h109-hd.img")
    with open(hd, "wb") as f:
        f.truncate(256 * 1024 * 1024)
    id_origem = open(IMAGEM, "rb").read(512)[440:444]

    with Maquina("h109a", "ide", memoria="768M", extras=["-drive", f"format=raw,file={hd},if=ide,index=1"]) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        s = m.serial()
        verificar("[DISCO] hd0:" in s and "[DISCO] hd1:" in s, "o driver ATA acha os dois discos")

        # A trava, sem confirmação.
        verificar(setores(m, "hd1 0 gravar") == ERRO_ACESSO, "sem confirmação, gravar no hd1 dá ERRO_ACESSO")
        verificar(setores(m, "hd1 0 ler") == ERRO_ACESSO, "sem confirmação, ler o disco interno hd1 dá ERRO_ACESSO")
        verificar(setores(m, "hd0 0 gravar") == ERRO_ACESSO, "sem confirmação, gravar no disco de origem também dá ERRO_ACESSO")
        verificar(setores(m, "hd0 1 ler") == 0, "ler a partição do sistema da origem (o que o instalador copia) dá")
        out = m.comando("instalar hd1 --sim")
        verificar("pronto" not in m.serial()[-2000:] and "não há opções" in out, "\"instalar hd1 --sim\" não existe mais")

        # A caixa do kernel: nome errado, depois Esc.
        n = len(m.serial())
        m.digitar("/testes/setores hd1 0 gravar liberar\n", 0.03)
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.8)
        m.digitar("hd0\n", 0.05)
        esperar(m, "confirmacao do disco hd1: nome diferente", 10, n)
        time.sleep(0.6)
        m.captura("H109-confirmacao-nome-errado")
        m.tecla("esc")
        esperar(m, "setores: gravar hd1 0 -> ", 10, n)
        verificar(f"setores: liberar hd1 -> {ERRO_ACESSO}" in m.serial()[n:] and f"gravar hd1 0 -> {ERRO_ACESSO}" in m.serial()[n:],
                  "nome errado não libera; Esc cancela e a gravação continua travada")
        esperar(m, "sagui:/$", 10, n)

        # O nome certo libera, só para esse processo.
        n = len(m.serial())
        m.digitar("/testes/setores hd1 0 gravar liberar\n", 0.03)
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.8)
        m.captura("H109-confirmacao")
        m.digitar("hd1\n", 0.05)
        esperar(m, "setores: gravar hd1 0 -> ", 10, n)
        verificar("setores: liberar hd1 -> 0" in m.serial()[n:] and "gravar hd1 0 -> 0" in m.serial()[n:],
                  "digitando hd1 na caixa do kernel, a gravação passa")
        esperar(m, "liberacao do processo", 10, n)
        esperar(m, "sagui:/$", 10, n)
        verificar(setores(m, "hd1 0 gravar") == ERRO_ACESSO, "a liberação acaba com o processo: o próximo volta a ser travado")

        # A instalação pela janela, aberta pelo menu Iniciar.
        n = len(m.serial())
        m.clique(40, 12)
        esperar(m, "[GUI] menu aberto", 10, n)
        time.sleep(0.6)
        m.digitar("instalar", 0.08)
        esperar(m, '[GUI] menu: busca "instalar" (1 resultados)', 10, n)
        m.digitar("\n", 0.05)
        esperar(m, "instalar: janela aberta (1 discos)", 20, n)
        verificar(True, "o menu Iniciar acha e abre o instalador")
        time.sleep(1)
        m.captura("H109-instalador")
        verificar(True, "o instalador oferece só o hd1 (o hd0 é a origem)")
        # O assistente: Boas-vindas > Disco > (caixa do kernel) > Partições > Resumo > Instalar.
        m.tecla("ret")
        esperar(m, "instalar: passo 1 (Disco)", 10, n)
        time.sleep(0.6)
        m.tecla("ret")
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.8)
        m.digitar("hd1\n", 0.05)
        esperar(m, "instalar: passo 2 (Partições)", 20, n)
        time.sleep(0.6)
        m.tecla("ret")
        esperar(m, "instalar: passo 3 (Resumo)", 10, n)
        time.sleep(0.6)
        m.tecla("ret")
        esperar(m, "instalar: pronto em hd1", 240, n)
        verificar(True, "o instalador terminou depois da confirmação")
        time.sleep(1)
        m.tecla("ret")   # Fechar
        time.sleep(1)

    mbr = open(hd, "rb").read(512)
    verificar(mbr[510:512] == b"\x55\xaa", "o disco instalado tem a assinatura de boot")
    verificar(mbr[440:444] not in (b"\0\0\0\0", id_origem), f"o identificador do MBR é novo ({mbr[440:444].hex()})")
    tipos = [mbr[446 + k * 16 + 4] for k in range(4)]
    ini_fat = [struct.unpack_from("<I", mbr, 446 + k * 16 + 8)[0] for k in range(4) if tipos[k] == 0x0C]
    verificar(0xDA in tipos and ini_fat and ini_fat[0] % 2048 == 0, f"partições: {[hex(t) for t in tipos]}, FAT32 em {ini_fat}")
    fat = os.path.join(SAIDA, "h109-fat.img")
    with open(hd, "rb") as f, open(fat, "wb") as g:
        f.seek(ini_fat[0] * 512)
        g.write(f.read())
    r = subprocess.run(["fsck.vfat", "-n", fat], capture_output=True, text=True)
    os.remove(fat)
    verificar(r.returncode == 0, f"fsck.vfat: a FAT32 do instalador está íntegra ({r.stdout.strip().splitlines()[-1]})")

    # Ligar só pelo disco instalado; gravar um arquivo; reiniciar; ler.
    with Maquina("h109b", "ide", imagem=hd, memoria="768M", reiniciar=True) as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("tem configuracoes do SaguiOS: montado sozinho" in m.serial(), "a partição dos arquivos monta sozinha no boot")
        # Já instalado: o instalador some do menu (a busca não acha).
        n = len(m.serial())
        m.clique(40, 12)
        esperar(m, "[GUI] menu aberto", 10, n)
        time.sleep(0.6)
        m.digitar("instalar", 0.08)
        esperar(m, '[GUI] menu: busca "instalar" (0 resultados)', 10, n)
        verificar(True, "ligado pelo disco instalado, o instalador não aparece no menu")
        m.tecla("esc")
        time.sleep(0.8)
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
        m.captura("H109-depois-de-reiniciar")
        verificar("guardado no disco" in m.serial()[n2:], "o arquivo continua no disco depois de reiniciar")


if __name__ == "__main__":
    main()
