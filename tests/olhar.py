#!/usr/bin/env python3
"""Ferramenta (não é teste da suíte): abre o Cipó num site de verdade, com a
janela maximizada, e guarda a captura e o log serial para comparar com outro
navegador. Uso: python3 tests/olhar.py URL [nome] [segundos]"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, imagem_de_teste, SAIDA  # noqa: E402


def main():
    url = sys.argv[1]
    nome = sys.argv[2] if len(sys.argv) > 2 else "olhar"
    tempo = int(sys.argv[3]) if len(sys.argv) > 3 else 240
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo {url}", ["--vesa"])
    with Maquina(nome, "ide", imagem=img, extras=extras) as m:
        t0 = time.time()
        m.esperar_serial("cipo: pronto", tempo)
        print(f"pronto em {time.time() - t0:.1f} s (desde o boot)")
        s = m.serial()
        jan = re.search(r'criada por \d+: "Cip.*?" (\d+)x(\d+) em \((\d+),(\d+)\)', s)
        if jan:
            jl, ja, jx, jy = (int(x) for x in jan.groups())
            m.clique(jx + 200, jy + 8, duplo=True)   # duplo clique no título: maximiza
            fim = time.time() + tempo
            while m.serial().count("cipo: pronto") < 2 and time.time() < fim:
                time.sleep(0.5)
            print(f"maximizado e pronto em {time.time() - t0:.1f} s")
        time.sleep(3)
        png = m.captura(nome, guardar=False)
        with open(os.path.join(SAIDA, nome + ".log"), "w", encoding="latin-1") as f:
            f.write(m.serial())
        print("captura", png)


if __name__ == "__main__":
    main()
