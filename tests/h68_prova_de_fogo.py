#!/usr/bin/env python3
"""H68 - prova de fogo: programas da 1.0 compilados pelo compilador do SaguiOS.

Critério: wc, cat, calculadora e banana, compilados pelo cc do SaguiOS (e não
pelo GCC), passam nos mesmos testes da versão 1.0.

Como: o núcleo do cc é compilado para o Linux (o mesmo código; o teste da
H66 confere que ele gera, byte a byte, o mesmo executável que o cc rodando
dentro do SaguiOS). Com ele os quatro programas são compilados e postos no
lugar dos do GCC numa cópia da imagem (build/h68/). Depois os testes da 1.0
que usam esses programas rodam com essa imagem (SAGUI_IMAGEM e
SAGUI_RAMDISK), guardando as capturas fora de docs/.
"""
import hashlib
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from qemu import FalhaTeste, verificar, RAIZ  # noqa: E402
import h64_h66_compilador as h66  # noqa: E402

PASTA = os.path.join(RAIZ, "build", "h68")
PROGRAMAS = ["wc", "cat", "calculadora", "banana"]
# Os testes da 1.0 que usam esses programas: wc (o "conta" do ls bin | conta)
# e cat no shell e nos pipes, a calculadora e a banana nas janelas.
TESTES = ["h15_pipes.py", "h16_shell.py", "h24_terminal_janela.py", "h26_calculadora.py", "h27_banana.py"]


def main():
    if os.path.exists(PASTA):
        shutil.rmtree(PASTA)
    os.makedirs(PASTA)
    h66.preparar()
    raiz = os.path.join(PASTA, "rootfs")
    shutil.copytree(os.path.join(RAIZ, "build", "rootfs"), raiz)
    for nome in PROGRAMAS:
        fonte = os.path.join(RAIZ, "userland", "programas", nome + ".c")
        saida = os.path.join(PASTA, nome)
        r = subprocess.run([h66.CC, fonte] + h66.INC + ["-L", h66.LIB, "-o", saida], capture_output=True)
        verificar(r.returncode == 0, f"{nome}.c compilado pelo cc do SaguiOS" + (": " + r.stderr.decode()[:600] if r.returncode else ""))
        gcc = open(os.path.join(raiz, "bin", nome), "rb").read()
        nosso = open(saida, "rb").read()
        verificar(nosso != gcc and nosso[:4] == b"\x7fELF",
                  f"  {nome}: {len(nosso)} bytes pelo cc (o do GCC tem {len(gcc)}): é outro executável")
        shutil.copy(saida, os.path.join(raiz, "bin", nome))
    ramdisk = os.path.join(PASTA, "ramdisk.sgfs")
    imagem = os.path.join(PASTA, "saguios.img")
    b = os.path.join(RAIZ, "build")
    subprocess.run(["python3", os.path.join(RAIZ, "tools", "mksgfs.py"), raiz, ramdisk], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["python3", os.path.join(RAIZ, "tools", "mkimagem.py"), "--mbr", os.path.join(b, "boot", "mbr.bin"),
                    "--estagio2", os.path.join(b, "boot", "estagio2.bin"), "--kernel", os.path.join(b, "kernel.bin"),
                    "--kernel-elf", os.path.join(b, "kernel.elf"), "--ramdisk", ramdisk, "--saida", imagem, "--vesa"],
                   check=True, stdout=subprocess.DEVNULL)
    verificar(True, f"imagem com os quatro programas do cc: {os.path.relpath(imagem, RAIZ)}")
    os.makedirs(os.path.join(PASTA, "capturas"), exist_ok=True)
    amb = dict(os.environ, SAGUI_IMAGEM=imagem, SAGUI_RAMDISK=ramdisk, SAGUI_CAPTURAS=os.path.join(PASTA, "capturas"))
    falhas = []
    for t in TESTES:
        r = subprocess.run([sys.executable, os.path.join(RAIZ, "tests", t)], env=amb, capture_output=True, text=True, timeout=900)
        oks = r.stdout.count("ok:")
        if r.returncode != 0:
            falhas.append(t)
            print(r.stdout[-1500:], r.stderr[-1500:])
        print(f"  {'ok' if r.returncode == 0 else 'FALHOU'}: {t} com os programas do cc ({oks} conferências)")
    verificar(not falhas, "os testes da 1.0 passam com wc, cat, calculadora e banana feitos pelo cc" +
              (": falharam " + ", ".join(falhas) if falhas else ""))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
