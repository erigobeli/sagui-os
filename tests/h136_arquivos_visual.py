#!/usr/bin/env python3
"""H136 - o Arquivos com o visual novo.

Critérios:
  - a lista vem em colunas, com as pastas primeiro e o resto por nome;
  - clicar em "Tamanho" no cabeçalho ordena pelo tamanho, e de novo inverte
    (as pastas continuam primeiro);
  - a pesquisa filtra enquanto se digita, e Esc limpa;
  - duplo clique numa pasta entra; voltar e avançar andam pelo histórico;
    clicar num pedaço do caminho vai para lá;
  - "Novo > Pasta" cria a pasta e já abre o nome para editar;
  - o painel é uma árvore: em /tmp/v/Pasta1 ela vem aberta até lá (SaguiOS >
    tmp > v > Pasta1), a setinha fecha e abre um ramo sem sair da pasta, e
    clicar numa pasta da árvore vai para ela; a árvore rola até a pasta
    atual quando ela fica abaixo;
  - "Lixeira" no painel abre a lixeira, com os comandos dela;
  - pasta vazia mostra o aviso.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela  # noqa: E402

CMD_A, END_Y, CORPO_Y, LATERAL, CAB_A, ITEM_A = 48, 48, 92, 200, 30, 28
LISTA_Y = CORPO_Y + CAB_A


def esperar(m, texto, tempo=15, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def linha(a, k):
    return (a["x"] + LATERAL + 80, a["y"] + LISTA_Y + 2 + k * ITEM_A + ITEM_A // 2)


def main():
    with Maquina("h136", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("mkdir /tmp/v")
        m.comando("mkdir /tmp/v/Pasta1")
        m.comando("cp /sobre.txt /tmp/v/b.txt")
        m.comando("echo oi > /tmp/v/a.txt")
        m.comando("echo '<p>uma pagina</p>' > /tmp/v/c.html")
        n = len(m.serial())
        m.digitar("arquivos /tmp/v\n", 0.03)
        esperar(m, "arquivos: pronto", 30, n)
        verificar("arquivos: ordem: Pasta1, a.txt, b.txt, c.html" in m.serial()[n:], "pastas primeiro, depois por nome")
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        m.mouse_para(*linha(a, 2))
        time.sleep(0.6)
        m.captura("H136-1-pasta")

        # Cabeçalho: Tamanho, e de novo (inverte).
        tam = (a["x"] + a["l"] - 60, a["y"] + CORPO_Y + 15)
        n = len(m.serial())
        m.clique(*tam)
        esperar(m, "arquivos: ordenar por tamanho", 10, n)
        esperar(m, "arquivos: ordem: Pasta1, a.txt, c.html, b.txt", 10, n)
        verificar(True, "Tamanho ordena do menor para o maior (a pasta continua primeiro)")
        n = len(m.serial())
        m.clique(*tam)
        esperar(m, "arquivos: ordem: Pasta1, b.txt, c.html, a.txt", 10, n)
        verificar(True, "de novo, do maior para o menor")

        # Pesquisa.
        busca = (a["x"] + a["l"] - 200, a["y"] + END_Y + 22)
        n = len(m.serial())
        m.clique(*busca)
        esperar(m, "arquivos: pesquisa", 10, n)
        m.digitar("b.", 0.08)
        esperar(m, 'arquivos: busca "b." em /tmp/v (1 itens)', 10, n)
        time.sleep(0.6)
        m.captura("H136-2-pesquisa")
        verificar(True, "a pesquisa filtra enquanto se digita")
        n = len(m.serial())
        m.tecla("esc")
        esperar(m, "arquivos: pasta /tmp/v (4 itens)", 10, n)
        verificar(True, "Esc limpa a pesquisa")

        # Entrar, voltar, avançar, caminho.
        n = len(m.serial())
        m.clique(*linha(a, 0), duplo=True)
        esperar(m, "arquivos: pasta /tmp/v/Pasta1 (0 itens)", 10, n)
        time.sleep(0.6)
        m.captura("H136-3-vazia")
        verificar(True, "duplo clique na pasta entra (e a pasta vazia mostra o aviso)")
        n = len(m.serial())
        m.clique(a["x"] + 24, a["y"] + END_Y + 22)          # voltar
        esperar(m, "arquivos: voltar -> /tmp/v", 10, n)
        m.clique(a["x"] + 24 + 36, a["y"] + END_Y + 22)     # avançar
        esperar(m, "arquivos: avancar -> /tmp/v/Pasta1", 10, n)
        verificar(True, "voltar e avançar andam pelo histórico")
        # A árvore do painel veio aberta até Pasta1.
        import re
        import arquivos_ui as ui
        painel = ui.painel(m.serial())
        nomes = [p.rpartition("@")[0] for p in painel.split(", ")]
        verificar(all(x in nomes for x in ("tmp", "v", "Pasta1")) and nomes.index("tmp") < nomes.index("v") < nomes.index("Pasta1"),
                  f"a árvore vem aberta até a pasta atual e rolada até ela ({', '.join(nomes)})")
        time.sleep(0.4)
        m.captura("H136-6-arvore")
        n = len(m.serial())
        _, y_v = ui.no_painel(m.serial(), a, "v")
        recuo_v = 8 + 2 * 14                                 # nível 2: SaguiOS (0) > tmp (1) > v (2)
        m.clique(a["x"] + recuo_v + 5, y_v)                  # a setinha de "v": fecha
        esperar(m, "arquivos: arvore fecha /tmp/v", 10, n)
        painel = ui.painel(m.serial())
        verificar("Pasta1@" not in painel and "arquivos: pasta " not in m.serial()[n:], "a setinha fecha o ramo, sem sair da pasta")
        m.clique(a["x"] + recuo_v + 5, y_v)                  # abre de novo
        esperar(m, "arquivos: arvore abre /tmp/v", 10, n)
        n = len(m.serial())
        m.clique(*ui.no_painel(m.serial(), a, "v"))
        esperar(m, "arquivos: pasta /tmp/v (4 itens)", 10, n)
        verificar(True, "clicar numa pasta da árvore vai para ela")
        m.clique(*ui.no_painel(m.serial(), a, "Pasta1"))
        esperar(m, "arquivos: pasta /tmp/v/Pasta1 (0 itens)", 10, n)

        n = len(m.serial())
        x_tmp = 158 + 34 + 56 + 8 + 14 + 12                 # "SaguiOS" (56 px) + ">" e o começo de "tmp"
        m.clique(a["x"] + x_tmp, a["y"] + END_Y + 22)
        esperar(m, "arquivos: pasta /tmp (", 10, n)
        verificar(True, "clicar em \"tmp\" no caminho vai para /tmp")

        # Novo > Pasta.
        n = len(m.serial())
        m.clique(a["x"] + 40, a["y"] + 24)
        esperar(m, "arquivos: comando Novo", 10, n)
        time.sleep(0.5)
        m.captura("H136-4-novo")
        m.clique(a["x"] + 50, a["y"] + CMD_A - 6 + 4 + 13)   # o 1º item: Pasta
        esperar(m, "arquivos: criado /tmp/Nova pasta", 10, n)
        esperar(m, "arquivos: renomeando Nova pasta", 10, n)
        verificar(True, "Novo > Pasta cria a pasta e abre o nome para editar")
        m.tecla("ret")
        time.sleep(0.5)

        # Lixeira no painel.
        n = len(m.serial())
        import arquivos_ui as ui
        m.clique(*ui.lixeira(m.serial(), a))
        esperar(m, "arquivos: pasta /.lixeira", 10, n)
        time.sleep(0.6)
        m.captura("H136-5-lixeira")
        verificar(True, "Lixeira no painel abre a lixeira")


if __name__ == "__main__":
    main()
