#!/usr/bin/env python3
"""H146 - Arquivos: extrair e compactar pelo menu do botão direito.

Pasta /tmp/z com a.txt, a pasta sub (com b.txt) e c.txt.gz.

Critérios:
  - num item: "Compactar em .zip" cria sub.zip (com sub/b.txt dentro);
  - com tudo marcado: "Compactar em .tar.gz" cria z.tar.gz com tudo;
  - num .zip: "Extrair aqui" não substitui o que já existe (avisa), e
    "Extrair para "sub/"" cria "sub (2)" (o nome sub já existe) com o
    conteúdo;
  - num .tar.gz: "Extrair para "z/"" cria a pasta z com tudo;
  - num .gz: "Extrair aqui" devolve c.txt ao lado (o .gz fica);
  - o menu de um arquivo comum não tem "Extrair"; o de um pacote começa
    com "Abrir" e não tem "Abrir no Editor";
  - abrir um pacote (duplo clique) extrai numa pasta nova e entra nela.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arquivos_ui as ui  # noqa: E402
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def linha_de(m, nome):
    """A linha do item pela última "arquivos: ordem:" do registro."""
    ordem = re.findall(r"arquivos: ordem: ([^\r\n]*)", m.serial())[-1].split(", ")
    return ordem.index(nome)


def menu_item(m, a, nome, escolha, desde):
    """Botão direito no item "nome" e clique no item "escolha" do menu."""
    k = linha_de(m, nome)
    m.clique(*ui.item(a, k))   # marca só ele
    time.sleep(0.6)
    n = len(m.serial())
    m.clique(*ui.item(a, k), botao=2)
    esperar(m, "[GUI] menu aberto", 10, n)
    s = esperar(m, "arquivos: menu: ", 10, n)
    itens = re.findall(r"arquivos: menu: ([^\r\n]*)", s)[-1].split("|")
    x, y = map(int, re.findall(r"menu aberto \(tipo \d+, \d+ itens\) em \((\d+),(\d+)\)", s)[-1])
    if escolha is None:
        m.tecla("esc")
        return itens
    m.clique(x + 40, y + 4 + itens.index(escolha) * 26 + 13)
    return itens


def main():
    with Maquina("h146", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        for c in ("mkdir /tmp/z", "echo um > /tmp/z/a.txt", "mkdir /tmp/z/sub", "echo dois > /tmp/z/sub/b.txt",
                  "echo tres > /tmp/z/c.txt", "gzip /tmp/z/c.txt"):
            m.comando(c, tempo=10)
        n = len(m.serial())
        m.digitar("arquivos /tmp/z\n", 0.03)
        esperar(m, "arquivos: pronto", 30, n)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")

        itens = menu_item(m, a, "a.txt", None, 0)
        verificar(not any("Extrair" in i for i in itens) and "Compactar em .zip" in itens,
                  "num arquivo comum: Compactar, sem Extrair")
        time.sleep(0.5)

        n = len(m.serial())
        menu_item(m, a, "sub", "Compactar em .zip", n)
        esperar(m, "arquivos: compactar 1 item(ns) em /tmp/z/sub.zip: codigo 0", 30, n)
        verificar(True, "Compactar em .zip criou sub.zip")
        time.sleep(0.8)
        m.captura("H146-1-compactado")

        n = len(m.serial())
        m.clique(*ui.item(a, 0))
        m.tecla("ctrl-a")
        time.sleep(0.5)
        m.clique(*ui.item(a, 0), botao=2)
        esperar(m, "[GUI] menu aberto", 10, n)
        s = esperar(m, "arquivos: menu: ", 10, n)
        itens = re.findall(r"arquivos: menu: ([^\r\n]*)", s)[-1].split("|")
        x, y = map(int, re.findall(r"menu aberto \(tipo \d+, \d+ itens\) em \((\d+),(\d+)\)", s)[-1])
        m.clique(x + 40, y + 4 + itens.index("Compactar em .tar.gz") * 26 + 13)
        esperar(m, "em /tmp/z/z.tar.gz: codigo 0", 30, n)
        verificar(re.search(r"compactar (\d+) item\(ns\) em /tmp/z/z.tar.gz", m.serial()[n:]).group(1) == "4",
                  "com os 4 itens marcados, Compactar em .tar.gz criou z.tar.gz (o nome da pasta)")
        time.sleep(0.8)

        n = len(m.serial())
        itens = menu_item(m, a, "sub.zip", "Extrair aqui", n)
        verificar('Extrair para "sub/"' in itens, f"no .zip: Extrair aqui e Extrair para \"sub/\" ({itens})")
        verificar(itens[0] == "Abrir" and "Abrir no Editor" not in itens, "no pacote, o primeiro item é Abrir, sem Abrir no Editor")
        s = esperar(m, "arquivos: extrair /tmp/z/sub.zip em /tmp/z: codigo", 30, n)
        verificar("codigo 0" not in s.split("arquivos: extrair /tmp/z/sub.zip")[-1].split("\n")[0],
                  "Extrair aqui com sub/b.txt já existindo não substitui (o unzip recusa)")
        time.sleep(0.8)
        m.captura("H146-2-aviso")

        n = len(m.serial())
        menu_item(m, a, "sub.zip", 'Extrair para "sub/"', n)
        esperar(m, "arquivos: extrair /tmp/z/sub.zip em /tmp/z/sub (2): codigo 0", 30, n)
        verificar(True, "Extrair para criou \"sub (2)\" (sub já existia)")
        time.sleep(0.8)

        n = len(m.serial())
        menu_item(m, a, "z.tar.gz", 'Extrair para "z/"', n)
        esperar(m, "arquivos: extrair /tmp/z/z.tar.gz em /tmp/z/z: codigo 0", 30, n)
        time.sleep(0.8)

        n = len(m.serial())
        menu_item(m, a, "c.txt.gz", "Extrair aqui", n)
        esperar(m, "arquivos: extrair /tmp/z/c.txt.gz em /tmp/z: codigo 0", 30, n)
        time.sleep(1)
        m.captura("H146-3-extraidos")

        n = len(m.serial())
        m.clique(*ui.item(a, linha_de(m, "sub.zip")), duplo=True)
        esperar(m, "arquivos: extrair /tmp/z/sub.zip em /tmp/z/sub (3): codigo 0", 30, n)
        esperar(m, "arquivos: pasta /tmp/z/sub (3)", 10, n)
        verificar(True, "duplo clique no sub.zip: extraiu em \"sub (3)\" e entrou nela")
        time.sleep(0.8)
        m.captura("H146-4-aberto")

        m.tecla("alt-f4")
        time.sleep(1.5)
        out = m.comando("cat '/tmp/z/sub (2)/sub/b.txt'", tempo=10)
        verificar("dois" in out, "sub (2)/sub/b.txt tem o conteúdo de antes")
        out = m.comando("cat /tmp/z/z/a.txt /tmp/z/z/sub/b.txt", tempo=10)
        verificar("um" in out and "dois" in out, "z/ tem a.txt e sub/b.txt")
        out = m.comando("cat /tmp/z/c.txt", tempo=10) + m.comando("ls /tmp/z", tempo=10)
        verificar("tres" in out and "c.txt.gz" in out, "c.txt voltou e o c.txt.gz ficou")


if __name__ == "__main__":
    main()
