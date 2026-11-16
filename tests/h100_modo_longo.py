#!/usr/bin/env python3
"""H100 - boot em modo longo (SaguiOS 3.0, fase 1).

Critério (prd3.md): o estágio 2 confere o CPUID (bit LM), monta tabelas de
página de 4 níveis, liga PAE, EFER.LME e a paginação e entra no kernel ELF64;
o kernel de 64 bits escreve na tela e na serial; numa CPU sem 64 bits, uma
mensagem clara. E os programas continuam ELF32, rodando sem mudança.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, RAIZ, SAIDA, imagem_console, imagem_de_teste  # noqa: E402

B = os.path.join(RAIZ, "build")


def classe_elf(caminho):
    """1 = ELF32, 2 = ELF64 (byte 4 do cabeçalho, especificação ELF)."""
    with open(caminho, "rb") as f:
        cab = f.read(20)
    return cab[4], int.from_bytes(cab[18:20], "little")   # classe e máquina (3 = i386, 62 = x86-64)


def imagem_parado64():
    """Imagem com o estágio 2 real e um kernel ELF64 que só para."""
    os.makedirs(SAIDA, exist_ok=True)
    obj = os.path.join(SAIDA, "kernel_parado64.o")
    elf = os.path.join(SAIDA, "kernel_parado64.elf")
    binario = os.path.join(SAIDA, "kernel_parado64.bin")
    img = os.path.join(SAIDA, "h100-parado64.img")
    subprocess.run(["nasm", "-f", "elf64", "-o", obj, os.path.join(RAIZ, "tests", "recursos", "kernel_parado64.asm")], check=True)
    subprocess.run(["ld", "-m", "elf_x86_64", "-T", os.path.join(RAIZ, "kernel", "ligacao.ld"), "-o", elf, obj], check=True)
    subprocess.run(["objcopy", "-O", "binary", elf, binario], check=True)
    subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "mkimagem.py"), "--mbr", os.path.join(B, "boot", "mbr.bin"),
                    "--estagio2", os.path.join(B, "boot", "estagio2.bin"), "--kernel", binario, "--kernel-elf", elf,
                    "--ramdisk", os.path.join(B, "ramdisk.sgfs"), "--saida", img], check=True, stdout=subprocess.DEVNULL)
    return img


def main():
    # O build: kernel ELF64 (x86-64), programas ELF32 (i386) como na 2.x.
    verificar(classe_elf(os.path.join(B, "kernel.elf")) == (2, 62), "kernel.elf é ELF64 x86-64")
    verificar(classe_elf(os.path.join(B, "userland", "bin", "ola.elf")) == (1, 3), "/bin/ola continua ELF32 i386")

    # Estágio 2: mensagem do modo longo e código de 64 bits rodando.
    with Maquina("h100-e2", "ide", imagem=imagem_parado64()) as m:
        m.esperar_tela("[6/6] entrando no modo longo (64 bits)", 60)
        tela = m.tela_texto()
        verificar(tela[0].endswith("64"), "o kernel de mentira de 64 bits escreveu na tela VGA")
        m.captura("H100-estagio2-modo-longo", guardar=False)

    # CPU sem 64 bits (qemu32 não tem o bit LM): mensagem clara, sem travar no escuro.
    with Maquina("h100-cpu32", "ide", imagem=imagem_console(), extras=["-cpu", "qemu32"]) as m:
        m.esperar_tela("este processador nao tem 64 bits", 60)
        texto = "\n".join(m.tela_texto())
        verificar("ERRO: este processador nao tem 64 bits (x86-64)." in texto, "CPU sem 64 bits: mensagem de erro clara")
        verificar("make ARQ=32" in texto, "a mensagem diz como obter a imagem de 32 bits")
        verificar("[SAGUI] kernel iniciado" not in m.serial(), "o kernel de 64 bits não foi chamado")
        m.captura("H100-cpu-sem-64-bits", guardar=False)

    # O kernel de 64 bits completo, nos dois modos de boot, e um programa de
    # 32 bits rodando nele (modo de compatibilidade).
    for modo in ("ide", "usb"):
        with Maquina("h100", modo, imagem=imagem_console()) as m:
            m.esperar_serial("[SAGUI] boot ok", 120)
            s = m.serial()
            verificar("[SAGUI] modo longo (64 bits)" in s, f"[{modo}] o kernel registrou o modo longo na serial")
            verificar("paginacao de 4 niveis" in s, f"[{modo}] paginação de 4 níveis")
            m.esperar_tela("Modo longo (64 bits)", 30)
            verificar(True, f"[{modo}] a tela mostra o modo longo")
    with Maquina("h100-ola", "ide", imagem=imagem_de_teste("/bin/ola 64 bits")) as m:
        m.esperar_serial("[TESTE] programa terminou", 60)
        s = m.serial()
        verificar("rodando no anel 3" in s and 'argv[2] = "bits"' in s, "programa ELF32 /bin/ola rodou no kernel de 64 bits")
        verificar("(ola) terminou com codigo 0" in s, "e terminou com código 0")
    return 0


if __name__ == "__main__":
    sys.exit(main())
