#!/usr/bin/env python3
"""H98 - formas com a borda suavizada (kernel/inc/suave.h), testadas no PC.

O suave.h só tem funções static inline, sem dependência do kernel: aqui ele
é compilado com o gcc do computador (32 bits, com AddressSanitizer) junto de
um programa pequeno que confere as contas.

Critérios:
  - longe dos cantos, o pixel fica inteiro dentro (16 de 16 amostras);
  - a ponta do canto fica fora e a cobertura cresce na direção do centro;
  - os quatro cantos são iguais, espelhados;
  - o total de amostras do retângulo arredondado fica perto da área exata
    (l * a - (4 - pi) * r * r), com erro menor que 1%;
  - suave_ret pinta o miolo com a cor exata, mistura a borda e não passa do
    recorte;
  - suave_contorno só pinta perto da borda, nunca no miolo;
  - suave_traco cobre o meio do traço e deixa o lado de fora intacto.
"""
import math
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import FalhaTeste, RAIZ, SAIDA  # noqa: E402

PROGRAMA_C = r"""
#include <stdio.h>
#include "suave.h"

static uint32_t px[64 * 64];

int main(void)
{
    /* amostras de cada pixel de um retângulo 40 x 30 com raio 10 */
    long total = 0;
    for (int j = 0; j < 30; j++) {
        for (int i = 0; i < 40; i++) {
            int n = suave_amostras_ret(i, j, 40, 30, 10);
            total += n;
            printf("%d ", n);
        }
        printf("\n");
    }
    printf("total %ld\n", total);

    /* suave_ret em 20 x 20 dentro de um recorte que corta a metade de baixo */
    suave_t s = { px, 64, 0, 0, 64, 25 };
    for (int k = 0; k < 64 * 64; k++) px[k] = 0x000000;
    suave_ret(&s, 10, 10, 20, 20, 6, 0xFFFFFF, 0xFFFFFF, 256);
    printf("miolo %06x borda %06x ponta %06x recortado %06x\n",
           px[20 * 64 + 20], px[11 * 64 + 12], px[10 * 64 + 10], px[27 * 64 + 20]);

    /* contorno: nada no miolo */
    for (int k = 0; k < 64 * 64; k++) px[k] = 0;
    s.y1 = 64;
    suave_contorno(&s, 10, 10, 30, 30, 8, 0xFFFFFF, 256);
    printf("contorno miolo %06x lado %06x\n", px[25 * 64 + 25], px[25 * 64 + 10]);

    /* traço horizontal de (10, 40) a (40, 40), largura 4 px (em oitavos) */
    for (int k = 0; k < 64 * 64; k++) px[k] = 0;
    suave_traco(&s, 10 * 8, 40 * 8 + 4, 40 * 8, 40 * 8 + 4, 32, 0xFFFFFF);
    printf("traco meio %06x longe %06x\n", px[40 * 64 + 25], px[50 * 64 + 25]);
    return 0;
}
"""


def verificar(cond, msg):
    if not cond:
        raise FalhaTeste(msg)
    print(f"  ok: {msg}")


def main():
    os.makedirs(SAIDA, exist_ok=True)
    fonte = os.path.join(SAIDA, "h98_suave.c")
    programa = os.path.join(SAIDA, "h98_suave")
    with open(fonte, "w") as f:
        f.write(PROGRAMA_C)
    r = subprocess.run(["gcc", "-m32", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined",
                        "-Wall", "-Wextra", "-Werror", "-I", os.path.join(RAIZ, "kernel", "inc"),
                        "-o", programa, fonte], capture_output=True, text=True)
    if r.returncode:
        raise FalhaTeste("não compilou:\n" + r.stderr)
    saida = subprocess.run([programa], capture_output=True, text=True, check=True).stdout.splitlines()

    grade = [[int(v) for v in linha.split()] for linha in saida[:30]]
    verificar(grade[15][20] == 16 and grade[0][20] == 16 and grade[15][0] == 16,
              "longe dos cantos, o pixel fica inteiro dentro")
    verificar(grade[0][0] == 0, "a ponta do canto fica fora")
    diag = [grade[k][k] for k in range(10)]
    topo = grade[0][:11]
    meio = sorted({v for linha in grade[:10] for v in linha[:10]} - {0, 16})
    verificar(all(a <= b for a, b in zip(diag, diag[1:])) and all(a <= b for a, b in zip(topo, topo[1:])) and len(meio) >= 5,
              f"a cobertura cresce do canto para dentro, com valores no meio ({meio})")
    espelhos = all(grade[j][i] == grade[j][39 - i] == grade[29 - j][i] == grade[29 - j][39 - i]
                   for j in range(10) for i in range(10))
    verificar(espelhos, "os quatro cantos são iguais, espelhados")
    total = int(saida[30].split()[1]) / 16
    exato = 40 * 30 - (4 - math.pi) * 10 * 10
    verificar(abs(total - exato) / exato < 0.01, f"a área bate com a conta exata ({total:.1f} x {exato:.1f} pixels)")

    m = dict(zip(saida[31].split()[0::2], saida[31].split()[1::2]))
    verificar(m["miolo"] == "ffffff", "suave_ret pinta o miolo com a cor exata")
    verificar(m["ponta"] == "000000" and m["borda"] not in ("000000", "ffffff"),
              f"a ponta fica de fora e a borda do canto é mistura ({m['borda']})")
    verificar(m["recortado"] == "000000", "suave_ret respeita o recorte")

    c = saida[32].split()
    verificar(c[2] == "000000" and c[4] != "000000", "suave_contorno pinta a borda e não o miolo")
    t = saida[33].split()
    verificar(t[2] == "ffffff" and t[4] == "000000", "suave_traco cobre o meio do traço e não pinta longe dele")
    return 0


if __name__ == "__main__":
    sys.exit(main())
