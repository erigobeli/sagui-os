#!/usr/bin/env python3
"""H63 - sites de aceite do Sagui Navegador (teste "internet").

Critério: abrir, com texto legível, links e imagens, pela internet de
verdade e com o HTTPS próprio do SaguiOS:
  - um artigo da Wikipédia em português (com as imagens) e uma busca feita
    pelo formulário da própria Wikipédia;
  - example.com;
  - uma página de documentação simples (a man page do ls, no man7.org);
  - o README.md do SaguiOS no GitHub, em texto.
Cada site deixa uma captura em docs/v2/capturas (H63-*). Precisa de internet.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402
from h62_navegador import achar_campo, digitar_endereco, BARRA, VISTA_A  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]


def esperar(m, texto, tempo, desde):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim da serial: {m.serial()[-400:]!r}")


def carregada(s, url):
    """Os números da linha 'navegador: carregada <url> ...' do log."""
    for linha in s.splitlines():
        if "navegador: carregada " + url in linha:
            achou = re.search(r"situacao (\d+)\) em (\d+) ms: (\d+) nos, (\d+) pecas, (\d+) links, (\d+) imagens", linha)
            if achou:
                return [int(x) for x in achou.groups()]
    return None


def abrir(m, j, url, tempo=300):
    antes = len(m.serial())
    digitar_endereco(m, j, url)
    s = esperar(m, "navegador: pronto ", tempo, antes)
    time.sleep(0.5)
    m.mouse_para(j["x"] + 300, j["y"] + BARRA + VISTA_A + 10)
    time.sleep(0.5)
    return s


def main():
    with Maquina("h63", "ide", extras=REDE, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 30)
        time.sleep(1)
        m.comando("relogio internet", tempo=60)       # o HTTPS confere a data dos certificados
        antes = len(m.serial())
        m.digitar("navegador https://pt.wikipedia.org/wiki/Sagui\n", 0.04)
        s = esperar(m, "navegador: pronto https://pt.wikipedia.org/wiki/Sagui", 400, antes)
        j = janela(m.serial(), "Cipó")   # o nome desde a H82
        n = carregada(s, "https://pt.wikipedia.org/wiki/Sagui")
        verificar(n and n[0] == 200 and n[2] > 1000 and n[4] > 100,
                  f"Wikipédia: artigo com {n[2] if n else 0} nós e {n[4] if n else 0} links")
        prontas = re.search(r"imagens prontas \((\d+) de (\d+)\)", s)
        verificar(prontas and int(prontas.group(1)) >= 3, f"Wikipédia: {prontas.group(0) if prontas else 'sem imagens'}")
        m.mouse_para(j["x"] + 300, j["y"] + BARRA + VISTA_A + 10)
        time.sleep(0.5)
        m.captura("H63-1-wikipedia")
        m.tecla("pgdn", 0.3)
        m.tecla("pgdn", 0.3)
        time.sleep(1)
        m.captura("H63-2-wikipedia-rolada")
        # Busca pelo formulário da Wikipédia. Desde a H82 o Cipó mostra o artigo
        # como os outros navegadores: no topo a busca é só uma lupa, e o campo
        # aparece por JavaScript. A página de busca da Wikipédia tem o campo à vista.
        antes = len(m.serial())
        digitar_endereco(m, j, "https://pt.wikipedia.org/wiki/Especial:Pesquisar")
        esperar(m, "navegador: pronto https://pt.wikipedia.org/wiki/Especial:Pesquisar", 400, antes)
        time.sleep(1)
        from PIL import Image
        bordas = [(0x76, 0x76, 0x76), (0xA2, 0xA9, 0xB1), (0x72, 0x77, 0x7D), (0x7C, 0x80, 0x85), (0x36, 0x66, 0xCC)]
        campo = achar_campo(Image.open(m.captura("H63-tmp", guardar=False)).convert("RGB"), j, bordas)
        verificar(campo is not None, "Wikipédia: o campo de busca aparece")
        m.clique(*campo)
        m.digitar("Mata Atlântica", 0.06)
        antes = len(m.serial())
        m.tecla("ret", 0.1)
        s = esperar(m, "navegador: pronto ", 400, antes)
        verificar("formulario -> https://pt.wikipedia.org/w/index.php?" in s and "search=Mata+Atl%C3%A2ntica" in s,
                  "Wikipédia: a busca saiu pelo formulário, com acento em UTF-8")
        n = carregada(s, "https://pt.wikipedia.org/w/index.php?")
        verificar(n and n[0] == 200 and n[2] > 1000 and "navegador: titulo Mata Atl" in s,
                  "Wikipédia: a busca chegou ao artigo da Mata Atlântica")
        m.mouse_para(j["x"] + 300, j["y"] + BARRA + VISTA_A + 10)
        time.sleep(0.5)
        m.captura("H63-3-wikipedia-busca")
        # example.com
        s = abrir(m, j, "example.com")
        n = carregada(s, "https://example.com")
        verificar(n and n[0] == 200, "example.com aberto (sem https:// no endereço: o navegador completa)")
        m.captura("H63-4-example")
        # Documentação.
        url = "https://man7.org/linux/man-pages/man1/ls.1.html"
        s = abrir(m, j, url)
        n = carregada(s, url)
        verificar(n and n[0] == 200 and n[4] >= 5, f"documentação (man page do ls) aberta com {n[4] if n else 0} links")
        m.captura("H63-5-documentacao")
        # README do SaguiOS em texto.
        url = "https://raw.githubusercontent.com/erigobeli/sagui-os/main/README.md"
        s = abrir(m, j, url)
        n = carregada(s, url)
        verificar(n and n[0] == 200 and "text/plain" in s, "README.md do SaguiOS no GitHub, em texto puro")
        m.captura("H63-6-readme")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
