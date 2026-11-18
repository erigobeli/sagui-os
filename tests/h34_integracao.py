#!/usr/bin/env python3
"""H34 - integração: o pendrive aparece no sistema.

Critério: roteiro completo no QEMU, pelos dois caminhos (interface e
terminal): conectar o pendrive com o sistema rodando, montar, criar uma nota
no Editor, salvar em /usb, copiar um arquivo pelo terminal, desmontar,
remover; a VPS confere o conteúdo com mtools. Capturas de cada etapa.
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_console, criar_pendrive, pendrive_qemu, RAIZ  # noqa: E402

NOTA = "Nota salva no pendrive pelo SaguiOS. Ação!"


def janela(serial, titulo):
    m = re.findall(rf'criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial)[-1]
    return dict(l=int(m[0]), a=int(m[1]), x=int(m[2]) + 2, y=int(m[3]) + 24, fx=int(m[2]), fy=int(m[3]))


# As unidades ficam na coluna da direita, no canto de cima, como no Linux
# (D065): primeiro "Instalar o SaguiOS" (o sistema ligou pela imagem, não
# está instalado), depois o pendrive.
PEN_X, PEN_Y = 1024 - 20 - 96 + 16, 40 + 1 * 108      # canto do desenho (64x64)


POR_COLUNA = (768 - 40) // 92   # os ícones dos programas continuam numa segunda coluna (H38)


def icone(i):
    return 20 + (i // POR_COLUNA) * 110 + 48, 40 + (i % POR_COLUNA) * 92 + 32


def icone_pendrive():
    return PEN_X + 48, PEN_Y + 32


def azuis(png):
    """Pixels do azul do corpo do pendrive na parte do ícone que o Terminal
    não cobre (a janela vai até x = 946, com a sombra)."""
    x0 = 956
    saida = subprocess.run(["convert", png, "-crop", f"{PEN_X + 64 - x0}x64+{x0}+{PEN_Y}", "-depth", "8", "txt:-"],
                           capture_output=True, text=True, check=True).stdout
    n = 0
    for r, g, b in re.findall(r"\((\d+),(\d+),(\d+)", saida):
        if int(b) > 150 and int(r) < 110 and int(b) > int(g) + 30:
            n += 1
    return n


def mtype(img, caminho):
    return subprocess.run(["mtype", "-i", img + "@@1M", f"::{caminho}"], capture_output=True).stdout


def espetar(m, img, ident):
    m.monitor(f"drive_add 0 if=none,id={ident},format=raw,file={img}")
    m.monitor(f"device_add usb-storage,bus=ehci.0,drive={ident},id=dev-{ident},removable=on")


def main():
    pen = criar_pendrive("h34-pen", rotulo="MEUPEN", arquivos={"leia.txt": "já estava no pendrive\n"})
    # ---------------- caminho pela interface gráfica ----------------
    with Maquina("h34", "ide", extras=["-device", "usb-ehci,id=ehci"]) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        espetar(m, pen, "p1")
        m.esperar_serial("notificacao: Pendrive conectado: MEUPEN (64 MB)", 20)
        time.sleep(0.6)
        png = m.captura("H34-1-pendrive-conectado")
        verificar(True, "ao espetar: notificação 'Pendrive conectado: MEUPEN (64 MB)'")
        # Ícone 64x64 suavizado (H81): o corpo azul do pendrive, na coluna da direita.
        azul = azuis(png)
        verificar(azul >= 40, f"ícone do pendrive apareceu na coluna da direita ({azul} pixels do azul dele)")

        m.clique(*icone_pendrive(), duplo=True)
        m.esperar_serial("arquivos: pasta /usb", 15)
        verificar("Pendrive montado em /usb" in m.serial(), "duplo clique no ícone monta o pendrive e abre o Arquivos em /usb")
        time.sleep(0.5)
        m.captura("H34-2-arquivos-no-pendrive")
        arq = janela(m.serial(), "Arquivos")

        m.clique(*icone(2), duplo=True)                               # Editor
        m.esperar_serial("editor: pronto", 15)
        time.sleep(0.5)
        m.digitar(NOTA, 0.04)
        m.tecla("ctrl-s")
        m.esperar_serial('"Salvar como"', 10)
        time.sleep(0.3)
        for _ in range(5):
            m.tecla("backspace", 0.03)
        m.digitar("/usb/nota do sagui.txt\n", 0.04)
        m.esperar_serial("editor: salvo /usb/nota do sagui.txt", 15)
        verificar(True, "o Editor salva a nota em /usb")
        time.sleep(0.3)
        m.captura("H34-3-editor-salvou-no-pendrive")
        ed = janela(m.serial(), "Editor - sem nome")
        m.clique(ed["fx"] + ed["l"] + 4 - 2 - 20 + 8, ed["fy"] + 12)  # fecha o Editor
        m.esperar_serial("editor: fechado", 10)
        time.sleep(0.5)

        m.clique(arq["x"] + 8 + 62, arq["y"] + 148 + 22)             # "Remover com segurança"
        m.esperar_serial("notificacao: Pode remover o pendrive.", 15)
        time.sleep(0.5)
        m.captura("H34-4-pode-remover")
        m.tecla("ret")                                                # fecha a mensagem
        verificar("[FAT32] desmontado (tudo gravado)" in m.serial(), "'Remover com segurança' no Arquivos grava tudo e desmonta")
        m.monitor("device_del dev-p1")
        m.esperar_serial("notificacao: Pendrive removido.", 15)
        time.sleep(0.6)
        png = m.captura("H34-5-pendrive-removido")
        verificar(azuis(png) == 0, f"removido: o ícone some ({azuis(png)} pixels do azul dele)")

        # Espeta de novo, monta pelo ícone e arranca sem desmontar.
        espetar(m, pen, "p2")
        m.esperar_serial("usb1: QEMU", 20)
        time.sleep(1)
        m.clique(*icone_pendrive(), duplo=True)
        m.esperar_serial("[FAT32] montado", 15)
        time.sleep(0.5)
        m.monitor("device_del dev-p2")
        m.esperar_serial("removido sem desmontar: dados podem ter se perdido", 15)
        time.sleep(0.5)
        m.captura("H34-6-removido-sem-desmontar")
        verificar(True, "arrancado sem desmontar: o sistema avisa que dados podem ter se perdido")

    verificar(mtype(pen, "/nota do sagui.txt") == NOTA.encode("latin-1"), "mtools: a nota do Editor está no pendrive, com acento")
    verificar(mtype(pen, "/leia.txt") == "já estava no pendrive\n".encode(), "mtools: o que já estava no pendrive continua lá")

    # ---------------- caminho pelo terminal ----------------
    with Maquina("h34-terminal", "ide", imagem=imagem_console(), extras=["-device", "usb-ehci,id=ehci"]) as m:
        m.esperar_serial("sagui:/$", 120)
        espetar(m, pen, "p3")
        m.esperar_serial("[DISCO] usb0", 20)
        out = m.comando("discos")
        verificar(re.search(r'usb0\s+QEMU QEMU USB HARDDRIVE\s+64 MB\s+FAT32\s+"MEUPEN"\s+\(não montado\)', out), "discos: usb0  QEMU QEMU USB HARDDRIVE  64 MB  FAT32  \"MEUPEN\"  (não montado)")
        out = m.comando("montar usb0")
        verificar("Pendrive montado em /usb" in out, "montar usb0 -> Pendrive montado em /usb")
        out = m.comando("ls /usb")
        verificar("nota do sagui.txt" in out and "leia.txt" in out, "ls /usb mostra a nota gravada pela interface")
        m.comando("cp /leia-me.txt /usb/")
        out = m.comando("desmontar usb0")
        verificar("Pode remover o pendrive." in out, "desmontar usb0 -> Pode remover o pendrive.")
        m.captura("H34-7-terminal")
        m.monitor("device_del dev-p3")
        m.esperar_serial("Pendrive removido.", 15)
    original = open(os.path.join(RAIZ, "rootfs", "leia-me.txt"), encoding="utf-8").read()
    verificar(mtype(pen, "/leia-me.txt") == original.encode("latin-1"), "mtools: leia-me.txt copiado pelo terminal está no pendrive")

    # ---------------- um pendrive só: o do boot, com uma FAT32 depois ----------------
    # Como ficou o pendrive do Eli no G470 (erro 18): o saguios.img gravado no
    # começo (partição 0xDA) e uma partição FAT32 criada no espaço livre.
    unico = os.path.join(os.path.dirname(pen), "h34-unico.img")
    subprocess.run(["cp", os.path.join(RAIZ, "build", "saguios.img"), unico], check=True)
    mb = os.path.getsize(unico) // (1024 * 1024) + 1
    with open(unico, "r+b") as f:
        f.truncate(64 * 1024 * 1024)
    subprocess.run(["sfdisk", "-q", "--append", unico], input=f"start={mb * 2048}, type=c\n", text=True, check=True)
    subprocess.run(["mkfs.vfat", "-F", "32", "-n", "DADOS", "--offset", str(mb * 2048), unico], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["mcopy", "-i", unico + f"@@{mb}M", os.path.join(RAIZ, "rootfs", "sobre.txt"), "::/do-pc.txt"], check=True)
    with Maquina("h34-unico", "usb", imagem=unico) as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("notificacao: Pendrive conectado: DADOS", 20)
        verificar(True, "boot e dados no mesmo pendrive: a FAT32 na segunda partição é achada e notificada")
        time.sleep(1)
        m.clique(*icone_pendrive(), duplo=True)
        m.esperar_serial("arquivos: pasta /usb (1 itens)", 15)
        verificar(True, "o ícone abre o Arquivos com o arquivo que veio do PC")
        m.captura("H34-8-um-pendrive-so")
    particao = pen + ".part"
    subprocess.run(["dd", f"if={pen}", f"of={particao}", "bs=512", "skip=2048", "status=none"], check=True)
    r = subprocess.run(["fsck.vfat", "-n", particao], capture_output=True, text=True)
    verificar(r.returncode == 0, "fsck.vfat: íntegro depois de tudo (o arrancar sem desmontar aconteceu sem gravação pendente)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
