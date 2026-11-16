#!/usr/bin/env python3
"""H96 - o leitor de ZIP do JClic recusa arquivos malformados sem ler fora deles.

Critério: o userland/jogos/jclic/zip.c, compilado no computador em 32 bits
(como no SaguiOS) e com verificação de memória, (1) lê um ZIP de verdade,
feito pelo zipfile do Python, e extrai todas as entradas; (2) recusa, sem
nenhum erro de memória, ZIPs com deslocamentos que dão a volta nos 32 bits:
o diretório central em 0xFFFFFFE0, o nome da entrada maior que o arquivo, o
cabeçalho local fora do arquivo e o tamanho comprimido enorme.

A especificação do formato é a APPNOTE.TXT da PKWARE (os registros de 46,
30 e 22 bytes abaixo).
"""
import io
import os
import struct
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import FalhaTeste, RAIZ, SAIDA  # noqa: E402

PASTA = os.path.join(SAIDA, "h96-zip")
PROG = os.path.join(PASTA, "teste_zip")


def verificar(cond, msg, detalhe=""):
    if not cond:
        raise FalhaTeste(f"{msg}: {detalhe}" if detalhe else msg)
    print("  ok:", msg)


def rodar(nome, dados):
    c = os.path.join(PASTA, nome + ".zip")
    with open(c, "wb") as f:
        f.write(dados)
    r = subprocess.run([PROG, c], capture_output=True, text=True, timeout=60,
                       env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1"))
    return r


def fim_do_diretorio(qtd, tam_dir, pos_dir):
    """O registro "fim do diretório central" (22 bytes)."""
    return struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, qtd, qtd, tam_dir, pos_dir, 0)


def entrada_central(nome, metodo, tam_c, tam, local):
    """Uma entrada do diretório central (46 bytes mais o nome)."""
    return struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, metodo, 0, 0, 0,
                       tam_c, tam, len(nome), 0, 0, 0, 0, 0, local) + nome


def main():
    os.makedirs(PASTA, exist_ok=True)
    r = subprocess.run(["gcc", "-m32", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover",
                        "-Wall", "-Wextra", "-Werror",
                        "-DERRO_INVALIDO=-1", "-DERRO_SEM_MEMORIA=-4",
                        "-I", os.path.join(RAIZ, "tests", "navegador", "simulado"),
                        "-I", os.path.join(RAIZ, "userland", "jogos", "jclic"),
                        "-I", os.path.join(RAIZ, "userland", "inc"), "-o", PROG,
                        os.path.join(RAIZ, "tests", "jclic", "teste_zip.c"),
                        os.path.join(RAIZ, "userland", "jogos", "jclic", "zip.c"),
                        os.path.join(RAIZ, "userland", "libsagui", "compactar.c")], capture_output=True, text=True)
    verificar(r.returncode == 0, "zip.c compila no computador (32 bits, com verificação de memória)", r.stderr[-1500:])

    # 1. Um ZIP de verdade, guardado e comprimido.
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("guardado.txt", b"sagui " * 50, compress_type=zipfile.ZIP_STORED)
        z.writestr("pasta/comprimido.xml", b"<banana/>" * 400, compress_type=zipfile.ZIP_DEFLATED)
    r = rodar("bom", buf.getvalue())
    verificar(r.returncode == 0 and "abriu 2" in r.stdout and "extraiu 2" in r.stdout,
              "um ZIP do Python abre e as 2 entradas saem", r.stdout + r.stderr[-800:])

    # 2. Malformados: cada um tem de ser recusado sem erro de memória.
    nome = b"a.txt"
    casos = {
        # O diretório central "começa" em 0xFFFFFFE0: pos + 46 dava a volta.
        "diretorio-longe": fim_do_diretorio(1, 46, 0xFFFFFFE0),
        # A entrada diz que o nome tem 65535 bytes, num arquivo pequeno.
        "nome-enorme": (lambda c: c + fim_do_diretorio(1, len(c), 0))(
            struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 0, 0, 0, 0, 1, 1, 0xFFFF, 0, 0, 0, 0, 0, 0)),
        # O cabeçalho local fica em 0xFFFFFFF0.
        "local-longe": (lambda c: c + fim_do_diretorio(1, len(c), 0))(entrada_central(nome, 0, 3, 3, 0xFFFFFFF0)),
        # O cabeçalho local existe, mas o tamanho comprimido passa do fim.
        "dados-longe": (lambda loc: (lambda c: loc + c + fim_do_diretorio(1, len(c), len(loc)))(
            entrada_central(nome, 8, 0xFFFFFF00, 10, 0)))(
            struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, 8, 0, 0, 0, 0xFFFFFF00, 10, len(nome), 0) + nome),
        # O nome e o campo extra do cabeçalho local passam do fim do arquivo.
        "local-extra-longe": (lambda loc: (lambda c: loc + c + fim_do_diretorio(1, len(c), len(loc)))(
            entrada_central(nome, 0, 3, 3, 0)))(
            struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0, 0, 3, 3, 0xFFFF, 0xFFFF)),
    }
    for caso, dados in casos.items():
        r = rodar(caso, dados)
        sem_erro = r.returncode == 0 and "AddressSanitizer" not in r.stderr and "runtime error" not in r.stderr
        recusou = "recusou" in r.stdout or "extraiu 0" in r.stdout
        verificar(sem_erro and recusou, f"ZIP malformado ({caso}) recusado sem ler fora do arquivo",
                  r.stdout + r.stderr[-1200:])
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
