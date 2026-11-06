#!/usr/bin/env python3
"""
lz.py - compressão do ramdisk: sequências de "copie N bytes" e "repita de D
bytes atrás". O kernel desfaz isso no boot (kernel/lib/lz.c).

Cada sequência:
    u8   token: 4 bits altos = literais, 4 bits baixos = tamanho da cópia - 4
    ...  mais bytes de tamanho, se o campo vale 15 (soma até um byte < 255)
    ...  os literais
    u16  distância da cópia (ausente na última sequência)
    ...  mais bytes do tamanho da cópia, se o campo vale 15
"""
PROFUNDIDADE = 48
JANELA = 65535


def _tam(saida, n):
    while n >= 255:
        saida.append(255)
        n -= 255
    saida.append(n)


def comprimir(d):
    n = len(d)
    saida = bytearray()
    cabeca, anterior = {}, [0] * n
    i = ini = 0
    while i < n:
        melhor = dist = 0
        if i + 4 <= n:
            chave = d[i:i + 4]
            p = cabeca.get(chave, -1)
            passos = PROFUNDIDADE
            while p >= 0 and i - p <= JANELA and passos:
                k = 4
                lim = n - i
                while k < lim and d[p + k] == d[i + k]:
                    k += 1
                if k > melhor:
                    melhor, dist = k, i - p
                p = anterior[p] - 1
                passos -= 1
        if melhor < 4:
            _inserir(d, i, n, cabeca, anterior)
            i += 1
            continue
        lit = i - ini
        saida.append((min(lit, 15) << 4) | min(melhor - 4, 15))
        if lit >= 15:
            _tam(saida, lit - 15)
        saida += d[ini:i]
        saida += dist.to_bytes(2, "little")
        if melhor - 4 >= 15:
            _tam(saida, melhor - 4 - 15)
        for k in range(i, min(i + melhor, n)):
            _inserir(d, k, n, cabeca, anterior)
        i += melhor
        ini = i
    lit = n - ini
    saida.append(min(lit, 15) << 4)
    if lit >= 15:
        _tam(saida, lit - 15)
    saida += d[ini:]
    return bytes(saida)


def _inserir(d, i, n, cabeca, anterior):
    if i + 4 <= n:
        chave = d[i:i + 4]
        anterior[i] = cabeca.get(chave, -1) + 1
        cabeca[chave] = i


def expandir(s, tam):
    d = bytearray()
    i = 0
    while i < len(s):
        t = s[i]
        i += 1
        lit = t >> 4
        if lit == 15:
            while True:
                b = s[i]; i += 1; lit += b
                if b != 255:
                    break
        d += s[i:i + lit]
        i += lit
        if i >= len(s):
            break
        dist = s[i] | s[i + 1] << 8
        i += 2
        m = (t & 15) + 4
        if t & 15 == 15:
            while True:
                b = s[i]; i += 1; m += b
                if b != 255:
                    break
        for _ in range(m):
            d.append(d[-dist])
    assert len(d) == tam
    return bytes(d)
