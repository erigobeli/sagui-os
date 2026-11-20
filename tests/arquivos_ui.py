"""Geometria do Arquivos com o visual novo (H136), para os testes.

Tudo relativo à área de conteúdo da janela (o dict de qemu.janela).
"""
CMD_A, END_Y, CORPO_Y, LATERAL, CAB_A, ITEM = 48, 48, 92, 200, 30, 28
LISTA_X, LISTA_Y = LATERAL + 1, CORPO_Y + CAB_A
LETRA = 8


def item(a, i, x=120):
    """O meio da linha i da lista (sobre o nome)."""
    return a["x"] + LISTA_X + x, a["y"] + LISTA_Y + 2 + i * ITEM + ITEM // 2


def _cmds(lixeira):
    if lixeira:
        return [("Restaurar", False), ("Apagar de vez", False), None, ("Esvaziar lixeira", False), None, ("Ordenar", True)]
    return [("Novo", True), None, ("Renomear", False), ("Apagar", False), None, ("Ordenar", True)]


def comando(a, nome, lixeira=False):
    """O meio do botão "nome" da barra de comandos."""
    x = 10
    for c in _cmds(lixeira):
        l = 17 if c is None else 12 + 16 + 8 + len(c[0]) * LETRA + (18 if c[1] else 12)
        if c and c[0] == nome:
            return a["x"] + x + l // 2, a["y"] + CMD_A // 2
        x += l + 2
    raise ValueError(nome)


def menu_comando(a, nome, k, lixeira=False):
    """O item k do menu aberto pelo botão "nome" (Novo, Ordenar)."""
    x, _ = comando(a, nome, lixeira)
    return x, a["y"] + CMD_A - 6 + 4 + k * 26 + 13


def subir(a):
    return a["x"] + 8 + 2 * 36 + 16, a["y"] + END_Y + 22


def painel(serial):
    """O último "arquivos: painel:" do registro, com as continuações
    ("arquivos: painel+:"), como "nome@y, nome@y, ..."."""
    import re
    i = serial.rfind("arquivos: painel: ")
    if i < 0:
        raise ValueError("o painel ainda não foi desenhado")
    pedacos = []
    for linha_ in re.findall(r"arquivos: painel(\+?): ([^\r\n]*)", serial[i:]):
        if pedacos and not linha_[0]:
            break
        pedacos.append(linha_[1])
    return ", ".join(pedacos)


def no_painel(serial, a, nome, ejetar=False):
    """O item "nome" do painel (atalho, pasta da árvore, pendrive ou Lixeira),
    pela última linha "arquivos: painel:" do registro (o Arquivos diz onde
    desenhou cada item: nome@y)."""
    import re
    texto = painel(serial)
    for parte in texto.split(", "):
        n, _, y = parte.rpartition("@")
        if n == nome:
            return a["x"] + (LATERAL - 24 if ejetar else 110), a["y"] + int(y)
    raise ValueError(f"{nome!r} não está no painel: {texto!r}")


def lixeira(serial, a):
    return no_painel(serial, a, "Lixeira")
