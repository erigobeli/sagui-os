#!/usr/bin/env python3
"""
mkarte.py - gera os ícones 64x64 da área de trabalho.

Uso: mkarte.py <saida.c>

Cada ícone é descrito aqui com formas simples (retângulo arredondado, círculo,
polígono, traço). O script calcula a cobertura de cada pixel com 4x4 amostras,
o que suaviza as bordas, acrescenta uma sombra e escreve vetores ARGB em C.
Nenhuma imagem pronta entra no projeto: os desenhos são estes números.
"""
import math
import sys

T = 64
AMOSTRAS = [(i + 0.5) / 4 for i in range(4)]


def rgb(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def vertical(c0, c1, y0, y1):
    a, b = rgb(c0), rgb(c1)

    def cor(_x, y):
        t = min(1, max(0, (y - y0) / (y1 - y0)))
        return tuple(a[k] + (b[k] - a[k]) * t for k in range(3))
    return cor


class Tela:
    def __init__(self):
        self.p = [[(0.0, 0.0, 0.0, 0.0)] * T for _ in range(T)]

    def forma(self, dentro, caixa, cor, opacidade=1.0):
        if isinstance(cor, int):
            fixa = rgb(cor)

            def cor(_x, _y, fixa=fixa):
                return fixa
        x0, y0, x1, y1 = caixa
        for y in range(max(0, int(y0) - 1), min(T, int(y1) + 2)):
            for x in range(max(0, int(x0) - 1), min(T, int(x1) + 2)):
                n = sum(dentro(x + i, y + j) for i in AMOSTRAS for j in AMOSTRAS)
                if not n:
                    continue
                a = n / 16 * opacidade
                c = cor(x + 0.5, y + 0.5)
                r, g, b, f = self.p[y][x]
                saida = a + f * (1 - a)
                self.p[y][x] = tuple((c[k] * a + (r, g, b)[k] * f * (1 - a)) / saida for k in range(3)) + (saida,)

    def ret(self, x0, y0, x1, y1, raio, cor, opacidade=1.0):
        def dentro(x, y):
            dx = max(x0 + raio - x, 0, x - (x1 - raio))
            dy = max(y0 + raio - y, 0, y - (y1 - raio))
            return x0 <= x <= x1 and y0 <= y <= y1 and dx * dx + dy * dy <= raio * raio
        self.forma(dentro, (x0, y0, x1, y1), cor, opacidade)

    def disco(self, cx, cy, r, cor, opacidade=1.0, ry=None):
        ry = ry or r
        self.forma(lambda x, y: ((x - cx) / r) ** 2 + ((y - cy) / ry) ** 2 <= 1,
                   (cx - r, cy - ry, cx + r, cy + ry), cor, opacidade)

    def poligono(self, pts, cor, opacidade=1.0):
        def dentro(x, y):
            d = False
            for (ax, ay), (bx, by) in zip(pts, pts[1:] + pts[:1]):
                if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
                    d = not d
            return d
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self.forma(dentro, (min(xs), min(ys), max(xs), max(ys)), cor, opacidade)

    def traco(self, pts, largura, cor, opacidade=1.0):
        r = largura / 2

        def dentro(x, y):
            for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                dx, dy = bx - ax, by - ay
                t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy or 1)))
                if (x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2 <= r * r:
                    return True
            return False
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self.forma(dentro, (min(xs) - r, min(ys) - r, max(xs) + r, max(ys) + r), cor, opacidade)

    def com_sombra(self):
        """Sombra macia: o alfa do desenho, deslocado para baixo e borrado."""
        alfa = [[self.p[y][x][3] for x in range(T)] for y in range(T)]
        saida = []
        for y in range(T):
            for x in range(T):
                soma = 0
                for j in range(-2, 3):
                    for i in range(-2, 3):
                        xx, yy = x + i, y + j - 2
                        if 0 <= xx < T and 0 <= yy < T:
                            soma += alfa[yy][xx]
                s = soma / 25 * 0.42
                r, g, b, f = self.p[y][x]
                a = f + s * (1 - f)
                if a <= 0:
                    saida.append(0)
                    continue
                k = f / a
                saida.append((round(a * 255) << 24) | (round(r * k) << 16) | (round(g * k) << 8) | round(b * k))
        return saida


def arquivos(t):
    t.ret(5, 12, 30, 28, 4, 0xC98A12)
    t.ret(5, 18, 59, 52, 5, 0xD9A11E)
    t.ret(11, 15, 53, 34, 2, 0xFFFDF2)
    t.ret(5, 24, 59, 54, 5, vertical(0xFFE066, 0xF2B21C, 24, 54))
    t.ret(5, 24, 59, 25.2, 1, 0xFFF2B0)


def terminal(t):
    t.ret(5, 9, 59, 47, 6, vertical(0x56616E, 0x323A44, 9, 47))
    t.ret(9, 13, 55, 43, 3, vertical(0x10261C, 0x0B1712, 13, 43))
    t.traco([(15, 20), (24, 28), (15, 36)], 3.6, 0x42E584)
    t.traco([(29, 37), (42, 37)], 3.6, 0xFFDF4A)
    t.ret(27, 47, 37, 53, 0, 0x3C4550)
    t.ret(17, 53, 47, 58, 2.5, vertical(0x6A7582, 0x444D58, 53, 58))


def editor(t):
    t.poligono([(13, 5), (41, 5), (52, 16), (52, 59), (13, 59)], vertical(0xFFFFFF, 0xE6EAEE, 5, 59))
    t.poligono([(41, 5), (52, 16), (41, 16)], 0xB9C2CB)
    for k, fim in enumerate((37, 44, 44, 34, 40)):
        t.traco([(20, 24 + k * 6.5), (fim, 24 + k * 6.5)], 2.2, 0x8795A3 if k else 0x0B7A3E)
    t.traco([(34, 51), (55, 28)], 8, 0xF2B21C)
    t.traco([(51, 32.5), (55, 28)], 8, 0xD8564B)
    t.poligono([(28.5, 57), (31, 48), (37.5, 54)], 0xF4D9B0)
    t.poligono([(28.5, 57), (29.5, 53.3), (32.5, 56)], 0x2A2F35)


def calculadora(t):
    t.ret(11, 4, 53, 60, 7, vertical(0x4B5764, 0x2B333C, 4, 60))
    t.ret(16, 10, 48, 23, 3, vertical(0xB7F5CC, 0x7FE2A4, 10, 23))
    t.ret(36, 15, 45, 18, 1, 0x0B5C30)
    cores = [0xDDE3E8, 0xDDE3E8, 0xDDE3E8, 0xF2B21C]
    for lin in range(4):
        for col in range(4):
            x, y = 16 + col * 8.3, 28 + lin * 7.7
            c = 0x2A6FD8 if lin == 3 and col == 3 else cores[col]
            t.ret(x, y, x + 6.6, y + 6, 1.6, c)


def banana(t):
    def curva(r, a0, a1, n=28):
        return [(46 - r * math.cos(a), 11 + r * math.sin(a)) for a in
                (a0 + (a1 - a0) * k / n for k in range(n + 1))]
    externa = curva(44, -0.12, 1.5)
    interna = curva(29, 1.42, 0.02)
    t.poligono(externa + interna, vertical(0xFFE35A, 0xF2B21C, 10, 56))
    t.traco(curva(35, 0.02, 1.42), 1.6, 0xD99A10, 0.75)
    t.traco(curva(30, 0.12, 1.36), 1.8, 0xFFF3A8, 0.8)
    t.traco([(9, 7), (14, 14)], 6, 0x7A5A1E)
    t.traco([(42, 54.5), (47, 53)], 4, 0x4C3A18)


def sagui3d(t):
    t.ret(5, 5, 59, 59, 7, vertical(0x8FD8F0, 0x56B3D8, 5, 34))
    t.poligono([(5, 59), (59, 59), (38, 34), (26, 34)], vertical(0x1B9A52, 0x0B6B38, 34, 59))
    t.poligono([(5, 8), (26, 22), (26, 34), (5, 59)], vertical(0x8D887A, 0x5D594F, 8, 59))
    t.poligono([(59, 8), (38, 22), (38, 34), (59, 59)], vertical(0x777267, 0x4F4B43, 8, 59))
    t.ret(26, 22, 38, 34, 0, 0x2C2A26)
    t.traco([(19, 51), (25, 54), (31, 52)], 3, 0xFFD52E)
    t.traco([(32, 43), (32, 31)], 2.2, 0x6B4A2A)
    t.traco([(26, 26), (32, 32), (38, 26)], 2.2, 0x6B4A2A)
    t.ret(5, 5, 59, 59, 7, 0xFFFFFF, 0.06)


def sobre(t):
    t.disco(13, 27, 11, 0xF5F5F0)
    t.disco(51, 27, 11, 0xF5F5F0)
    t.disco(32, 31, 23, vertical(0x4A382B, 0x2D211A, 8, 54))
    t.disco(32, 12, 6, 0xF5F5F0, ry=4.5)
    t.disco(32, 35, 16, vertical(0xC08E78, 0x9D6E5A, 19, 51), ry=14)
    t.disco(25, 32, 4.2, 0x17120F)
    t.disco(39, 32, 4.2, 0x17120F)
    t.disco(26.3, 30.6, 1.3, 0xFFFFFF)
    t.disco(40.3, 30.6, 1.3, 0xFFFFFF)
    t.disco(32, 39, 2.2, 0x3C2D23, ry=1.5)
    t.traco([(27, 44), (32, 46), (37, 44)], 1.8, 0x3C2D23)


def pendrive(t):
    t.ret(23, 3, 41, 18, 2, vertical(0xE6EAEE, 0xAEB7C0, 3, 18))
    t.ret(27, 7, 31, 11, 0, 0x4B5560)
    t.ret(33, 7, 37, 11, 0, 0x4B5560)
    t.ret(17, 16, 47, 60, 6, vertical(0x3E8BF0, 0x1A4FA8, 16, 60))
    t.ret(21, 20, 25, 56, 2, 0xFFFFFF, 0.18)
    t.disco(32, 42, 7, 0xFFDF4A)
    t.disco(32, 42, 3, 0x1A4FA8)


def cipo(t):
    t.disco(32, 32, 26, vertical(0x5CC4F0, 0x1A5FB8, 6, 58))
    t.poligono([(14, 20), (28, 13), (35, 22), (27, 30), (29, 42), (21, 46), (15, 33)], 0x2FB765)
    t.poligono([(39, 26), (52, 24), (55, 36), (46, 48), (38, 38)], 0x2FB765)
    t.traco([(8, 58), (20, 44), (28, 34), (42, 22), (58, 5)], 3.2, 0x7A5A1E)
    for x, y in ((18, 47), (31, 31), (45, 19)):
        t.disco(x + 3, y + 3, 4.5, 0xFFDF4A, ry=3)


def lixeira(t):
    t.ret(24, 5, 40, 12, 3, 0x8A949E)
    t.ret(10, 10, 54, 17, 3, vertical(0xC4CCD4, 0x98A2AC, 10, 17))
    t.poligono([(14, 18), (50, 18), (46, 60), (18, 60)], vertical(0xB7C0C9, 0x77828E, 18, 60))
    for x in (24, 32, 40):
        t.traco([(x, 24), (x + (32 - x) * 0.12, 54)], 3, 0x5A6470)


ICONES = [("arquivos", arquivos), ("terminal", terminal), ("editor", editor), ("calculadora", calculadora),
          ("banana", banana), ("sagui3d", sagui3d), ("sobre", sobre), ("pendrive", pendrive), ("cipo", cipo), ("lixeira", lixeira)]


def main():
    if len(sys.argv) != 2:
        sys.exit("uso: mkarte.py <saida.c>")
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        f.write("/* Gerado por tools/mkarte.py. Não edite. */\n#include \"arte.h\"\n\n")
        for nome, desenhar in ICONES:
            t = Tela()
            desenhar(t)
            px = t.com_sombra()
            f.write(f"const uint32_t arte_px_{nome}[{T * T}] = {{\n")
            for k in range(0, len(px), 8):
                f.write("    " + ", ".join(f"0x{v:08X}" for v in px[k:k + 8]) + ",\n")
            f.write("};\n\n")


if __name__ == "__main__":
    main()
