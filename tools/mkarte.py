#!/usr/bin/env python3
"""
mkarte.py - gera os ícones do SaguiOS (64x64 na área de trabalho, 40x40 na doca).

Uso: mkarte.py <saida.c>

Escrito pelo Robson de Lima Barbosa (pull request #1) e trazido para a 2.0
(H81), com o segundo tamanho e os ícones novos da 2.0 (Configurações, Cipó,
programa instalado).

Cada ícone é descrito aqui com formas simples (retângulo arredondado, círculo,
polígono, traço), sempre numa grade de 64x64. O script calcula a cobertura de
cada pixel com 4x4 amostras, o que suaviza as bordas, acrescenta uma sombra e
escreve vetores ARGB em C. Para o tamanho menor, as mesmas formas são
amostradas numa grade mais grossa (cada pixel cobre 64/40 da grade).
Os ícones dos programas vêm agora de uma folha de ícones (arte/icones-folha.png,
feita como imagem num estilo só), convertida pelo tools/png_icones.py em
tools/icones_imagem.py; os desenhos abaixo ficam para os ícones que a folha
não tem.

O padrão para um ícone novo: uma função que desenha na grade de 64, com uma
margem de uns 4 pixels (a sombra precisa dela), cores com um degradê de cima
para baixo e um contorno claro só onde ajuda a ler; depois, uma linha em
ICONES. O C gerado tem arte_px_<nome> (64) e arte_px_<nome>_40.
"""
import base64
import math
import os
import sys
import zlib

GRADE = 64     # os desenhos são feitos nesta grade
T = 64         # tamanho da saída (muda para 40 na segunda passada)
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
        e = GRADE / T                      # quanto da grade cada pixel da saída cobre
        x0, y0, x1, y1 = (v / e for v in caixa)
        for y in range(max(0, int(y0) - 1), min(T, int(y1) + 2)):
            for x in range(max(0, int(x0) - 1), min(T, int(x1) + 2)):
                n = sum(dentro((x + i) * e, (y + j) * e) for i in AMOSTRAS for j in AMOSTRAS)
                if not n:
                    continue
                a = n / 16 * opacidade
                c = cor((x + 0.5) * e, (y + 0.5) * e)
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
                d = 2 if T >= 64 else 1        # sombra proporcional ao tamanho
                for j in range(-2, 3):
                    for i in range(-2, 3):
                        xx, yy = x + i, y + j - d
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


def configuracoes(t):
    """Engrenagem: um disco com oito dentes e um furo, em cinza azulado."""
    cor = vertical(0x9AA6B4, 0x5A6674, 6, 58)
    for k in range(8):
        a = k * math.pi / 4
        c, s_ = math.cos(a), math.sin(a)
        # cada dente é um quadrilátero que sai do centro
        pts = [(32 + c * 18 - s_ * 5, 32 + s_ * 18 + c * 5), (32 + c * 27 - s_ * 4, 32 + s_ * 27 + c * 4),
               (32 + c * 27 + s_ * 4, 32 + s_ * 27 - c * 4), (32 + c * 18 + s_ * 5, 32 + s_ * 18 - c * 5)]
        t.poligono(pts, cor)
    t.disco(32, 32, 21, cor)
    t.disco(32, 32, 15, vertical(0xC4CDD6, 0x8794A2, 17, 47))
    t.disco(32, 32, 7, 0x2B333C)
    t.disco(32, 32, 7, 0xFFFFFF, 0.08)


def cipo(t):
    """O navegador Cipó: um globo azul abraçado por um cipó verde com folhas."""
    t.disco(32, 32, 25, vertical(0x5FB6F2, 0x1C5FB8, 7, 57))
    for dy in (-12, 0, 12):                              # paralelos
        r = math.sqrt(max(0, 25 * 25 - dy * dy))
        t.traco([(32 - r + 2, 32 + dy), (32 + r - 2, 32 + dy)], 1.4, 0xFFFFFF, 0.35)
    t.traco([(32 + 11 * math.sin(a / 10), 7 + a * 50 / 31) for a in range(32)], 1.4, 0xFFFFFF, 0.35)
    t.traco([(32 - 11 * math.sin(a / 10), 7 + a * 50 / 31) for a in range(32)], 1.4, 0xFFFFFF, 0.35)
    # o cipó: uma curva que dá a volta e três folhas
    pts = [(6 + k * 52 / 30, 48 - 30 * math.sin(k * math.pi / 30) + 6 * math.sin(k / 3)) for k in range(31)]
    t.traco(pts, 3.4, 0x2E8B3A)
    for (fx, fy, ang) in ((16, 30, -0.9), (34, 20, 0.2), (50, 30, 0.9)):
        c, s_ = math.cos(ang), math.sin(ang)
        folha = [(fx + c * u * 9 - s_ * v * 4, fy + s_ * u * 9 + c * v * 4) for (u, v) in
                 ((0, 0), (0.3, -0.8), (0.7, -0.7), (1, 0), (0.7, 0.7), (0.3, 0.8))]
        t.poligono(folha, vertical(0x6BD66B, 0x2E8B3A, fy - 8, fy + 8))


def instalado(t):
    """Programa instalado: uma janela clara com uma estrela amarela."""
    t.ret(6, 9, 58, 55, 6, vertical(0xF4F6F8, 0xD8DEE4, 9, 55))
    t.ret(6, 9, 58, 20, 6, vertical(0x18A65A, 0x0A6E38, 9, 20))
    t.ret(6, 15, 58, 20, 0, 0x0A6E38)
    t.disco(50, 14.5, 2.6, 0xFFFFFF, 0.85)
    estrela = []
    for k in range(10):
        r = 13 if k % 2 == 0 else 5.5
        a = -math.pi / 2 + k * math.pi / 5
        estrela.append((32 + r * math.cos(a), 38 + r * math.sin(a)))
    t.poligono(estrela, vertical(0xFFE35A, 0xF2B21C, 25, 51))


def tocador(t):
    """O Tocador: um disco verde com uma nota musical amarela."""
    t.disco(32, 32, 26, vertical(0x2EB872, 0x0E6E3C, 6, 58))
    t.disco(32, 32, 26, 0xFFFFFF, 0.08)
    t.disco(25, 43, 7, 0xFFDF4A, ry=5.5)                 # cabeça da nota
    t.ret(30, 16, 33.5, 43, 0, 0xFFDF4A)                # haste
    t.poligono([(33.5, 16), (44, 20), (44, 27), (33.5, 23)], 0xFFDF4A)   # bandeirola


def saguicode(t):
    """O Sagui Code: a cara do sagui (tufos brancos) sobre uma tela escura,
    com os sinais de código "</>" embaixo."""
    t.ret(5, 6, 59, 58, 8, vertical(0x34404C, 0x1C232B, 6, 58))
    t.disco(19, 21, 7.5, 0xF5F5F0)                       # tufos das orelhas
    t.disco(45, 21, 7.5, 0xF5F5F0)
    t.disco(32, 24, 13, vertical(0x5A4433, 0x3A2B20, 11, 37))
    t.disco(32, 12.5, 4, 0xF5F5F0, ry=3)                 # a mancha branca da testa
    t.disco(32, 27, 8.5, vertical(0xC99A82, 0xA67660, 19, 35), ry=8)
    t.disco(28.5, 25.5, 2.4, 0x17120F)
    t.disco(35.5, 25.5, 2.4, 0x17120F)
    t.disco(29.2, 24.8, 0.8, 0xFFFFFF)
    t.disco(36.2, 24.8, 0.8, 0xFFFFFF)
    t.disco(32, 30.5, 1.4, 0x3C2D23, ry=1)
    t.traco([(22, 43), (15, 48.5), (22, 54)], 3.4, 0x42E584)   # <
    t.traco([(35.5, 41.5), (28.5, 55.5)], 3.4, 0xFFDF4A)       # /
    t.traco([(42, 43), (49, 48.5), (42, 54)], 3.4, 0x42E584)   # >


def banana_curva(t, cx, cy, r_ext, r_int, a0, a1, cor, n=24):
    """Uma banana como faixa de anel (H92): o arco externo de a0 a a1 e o
    interno de volta. Usada nos ícones dos jogos com tema de banana."""
    ext = [(cx - r_ext * math.cos(a), cy + r_ext * math.sin(a)) for a in
           (a0 + (a1 - a0) * k / n for k in range(n + 1))]
    inte = [(cx - r_int * math.cos(a), cy + r_int * math.sin(a)) for a in
            (a1 + (a0 - a1) * k / n for k in range(n + 1))]
    t.poligono(ext + inte, cor)


def jogos(t):
    """Os Jogos (H92): um controle verde com cruz e botões, um deles amarelo-banana."""
    t.ret(5, 18, 59, 48, 14, vertical(0x2EB872, 0x0E6E3C, 18, 48))
    t.disco(16, 46, 9, 0x13804A)
    t.disco(48, 46, 9, 0x13804A)
    t.ret(5, 18, 59, 30, 12, 0xFFFFFF, 0.10)
    t.ret(13, 30, 25, 35, 1.2, 0x18202A)                 # a cruz
    t.ret(16.5, 26.5, 21.5, 38.5, 1.2, 0x18202A)
    t.disco(44, 29, 3.6, 0xFFDF4A)                       # botões
    t.disco(51, 34, 3.6, 0xE05A4A)
    t.disco(37, 34, 3.6, 0x42A5F5)
    t.disco(44, 39, 3.6, 0xF5F5F0)


def cacabananas(t):
    """O Caça-Bananas (H92): uma banana-bomba, com faixas escuras e pavio
    aceso, sobre um canteiro verde do bananal."""
    t.ret(5, 5, 59, 59, 8, vertical(0x5CC46E, 0x2E8B3A, 5, 59))
    for k in (1, 2):                                      # as casas do canteiro
        t.traco([(5 + k * 18, 8), (5 + k * 18, 56)], 1, 0x1F6B2A, 0.35)
        t.traco([(8, 5 + k * 18), (56, 5 + k * 18)], 1, 0x1F6B2A, 0.35)
    banana_curva(t, 42, 12, 36, 18, 0.12, 1.42, vertical(0xFFE35A, 0xE8A812, 16, 50))
    for a in (0.55, 0.85, 1.15):                          # as faixas de "bomba"
        t.traco([(42 - 19 * math.cos(a), 12 + 19 * math.sin(a)), (42 - 35 * math.cos(a), 12 + 35 * math.sin(a))],
                2.6, 0x6B4A1A, 0.85)
    t.traco([(8, 19), (11, 13)], 5, 0x5A3E1A)             # o cabinho
    t.traco([(11, 13), (15, 8), (20, 8)], 1.8, 0x3A2A16)   # o pavio
    for a in range(8):                                     # a faísca
        ang = a * math.pi / 4
        t.traco([(21, 8), (21 + 5 * math.cos(ang), 8 + 5 * math.sin(ang))], 1.6, 0xFF8A1E)
    t.disco(21, 8, 2.2, 0xFFF3A8)


def paciencia(t):
    """A Paciência Banana (H92): uma carta virada (verso com banana) e uma
    carta aberta com um ás de copas."""
    t.ret(6, 10, 34, 50, 4, 0xFFFFFF)
    t.ret(7.5, 11.5, 32.5, 48.5, 3, vertical(0x2E9E52, 0x16683A, 11, 48))
    banana_curva(t, 26, 18, 14, 8, 0.2, 1.4, 0xFFD52E, n=14)
    t.poligono([(26, 6), (54, 12), (46, 58), (18, 52)], 0x9AA3AC)       # sombra da carta da frente
    t.poligono([(27, 5), (55, 11), (47, 57), (19, 51)], 0xFFFFFF)
    t.disco(33, 33.5, 5.2, 0xD8323A)                      # copas: dois discos e um triângulo
    t.disco(41, 35, 5.2, 0xD8323A)
    t.poligono([(28.2, 35.5), (45.8, 38.8), (34.5, 47)], 0xD8323A)
    t.traco([(29, 20), (32.5, 11.5), (35, 21)], 2, 0xD8323A)   # um "A" no canto
    t.traco([(30.4, 17.3), (34.2, 18)], 1.6, 0xD8323A)


def chip8(t):
    """O emulador de CHIP-8 (H92): um monitor antigo com uma banana de pixels grandes."""
    t.ret(5, 7, 59, 49, 6, vertical(0xC9C2B0, 0x938B78, 7, 49))
    t.ret(10, 12, 54, 42, 2, 0x0B140E)
    desenho = [".........#", "........#.", "#......##.", "##....###.", ".#######..", "..####...."]
    for lin, fila in enumerate(desenho):
        for col, c in enumerate(fila):
            if c == "#":
                t.ret(12.5 + col * 4, 15 + lin * 4, 15.8 + col * 4, 18.3 + lin * 4, 0, 0x42E584)
    t.ret(24, 49, 40, 54, 0, 0x7A7262)
    t.ret(16, 54, 48, 59, 2.5, vertical(0xA8A090, 0x7A7262, 54, 59))


def jclic(t):
    """As atividades JClic (H92): três blocos de montar com cores fortes."""
    t.ret(6, 30, 30, 54, 3, vertical(0xFF8A4A, 0xD8562A, 30, 54))
    t.ret(34, 30, 58, 54, 3, vertical(0x5AA8F5, 0x2A6FD8, 30, 54))
    t.ret(20, 6, 44, 30, 3, vertical(0xFFE35A, 0xE8A812, 6, 30))
    t.traco([(12, 48), (18, 36), (24, 48)], 2.6, 0xFFFFFF)      # A
    t.traco([(14.5, 43), (21.5, 43)], 2.4, 0xFFFFFF)
    t.traco([(46 + 6.5 * math.cos(a), 42 + 6.5 * math.sin(a)) for a in
             (0.8 + k * 4.7 / 16 for k in range(17))], 2.6, 0xFFFFFF)   # C
    t.disco(32, 18, 6.5, 0x0B7A3E)                             # um ponto verde no bloco de cima


ICONES = [("arquivos", arquivos), ("terminal", terminal), ("editor", editor), ("calculadora", calculadora),
          ("banana", banana), ("sagui3d", sagui3d), ("sobre", sobre), ("pendrive", pendrive),
          ("configuracoes", configuracoes), ("cipo", cipo), ("instalado", instalado), ("tocador", tocador),
          ("saguicode", saguicode), ("jogos", jogos), ("cacabananas", cacabananas), ("paciencia", paciencia),
          ("chip8", chip8), ("jclic", jclic)]
TAMANHOS = [(64, ""), (40, "_40")]


def da_imagem(pixels):
    """Ícone a partir da folha de ícones (tools/icones_imagem.py, gerado pelo
    png_icones.py): copia os pixels RGBA do tamanho T para a tela; a sombra
    entra depois, como nos desenhos."""
    def desenhar(t):
        d = zlib.decompress(base64.b64decode(pixels[T]))
        for y in range(T):
            for x in range(T):
                r, g, b, a = d[(y * T + x) * 4:(y * T + x) * 4 + 4]
                t.p[y][x] = (r, g, b, a / 255)
    return desenhar


def com_imagens(lista):
    """Troca os desenhos pelos da folha, quando ela tem o ícone."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from icones_imagem import ICONES as IMG
    except ImportError:
        return lista
    return [(n, da_imagem(IMG[n]) if n in IMG else d) for n, d in lista]


def main():
    if len(sys.argv) != 2:
        sys.exit("uso: mkarte.py <saida.c>")
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        f.write("/* Gerado por tools/mkarte.py. Não edite. */\n#include \"arte.h\"\n\n")
        global T
        for tam, sufixo in TAMANHOS:
            T = tam
            for nome, desenhar in com_imagens(ICONES):
                t = Tela()
                desenhar(t)
                px = t.com_sombra()
                f.write(f"const uint32_t arte_px_{nome}{sufixo}[{T * T}] = {{\n")
                for k in range(0, len(px), 8):
                    f.write("    " + ", ".join(f"0x{v:08X}" for v in px[k:k + 8]) + ",\n")
                f.write("};\n\n")


if __name__ == "__main__":
    main()
