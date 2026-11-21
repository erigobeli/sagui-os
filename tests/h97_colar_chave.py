#!/usr/bin/env python3
"""H97 - colar a chave da IA, Editor de texto simples e o menu do Arquivos.

Critérios:
  - num arquivo de texto (.txt), o Editor não mostra "Compilar e rodar" nem
    "Instalar" (são só para programas em C);
  - no Arquivos, o botão direito num .txt mostra "Abrir no Editor" uma vez
    só (7 itens: Abrir no Editor, Renomear, Apagar..., Nova pasta, Novo
    arquivo de texto, Nova página HTML e Voltar), e a escolha "Apagar..."
    continua chegando certo;
  - em Configurações > IA, Ctrl+V cola na caixa da chave o texto copiado no
    Editor (a chave é longa demais para digitar), "Guardar" grava a chave em
    /usb/.saguios/chaves.txt, e a chave não aparece no registro do sistema.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import controle, Maquina, FalhaTeste, verificar, janela, cor_do_pixel, criar_pendrive, pendrive_qemu  # noqa: E402

# Uma chave falsa, do tamanho das de verdade (sk-or-v1- e 64 letras).
CHAVE = "sk-or-v1-" + "a1b2c3d4e5f60718" * 4


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def main():
    pen = criar_pendrive("h97-pen", rotulo="DADOS", pastas=(".saguios", "dados"),
                         arquivos={"dados/chave.txt": CHAVE + "\n"})
    with Maquina("h97", "ide", memoria="768M", extras=["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar")

        # 1. Arquivos: o menu de um .txt não repete "Abrir no Editor".
        antes = len(m.serial())
        m.digitar("arquivos /usb/dados\n", 0.04)
        esperar(m, "arquivos: pronto", 20, antes)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        xi, yi = a["x"] + 170 + 6 + 60, a["y"] + 70 + 2 + 10
        antes = len(m.serial())
        m.clique(xi, yi, botao=2)
        esperar(m, "menu aberto (tipo 3, 7 itens)", 10, antes)
        m.captura("H97-1-menu-texto")
        verificar(True, "o menu de um .txt tem 7 itens, com \"Abrir no Editor\" uma vez só")
        # O 3º item é "Apagar...": a escolha tem de chegar como apagar (o
        # Arquivos pergunta antes; a resposta é Não).
        antes = len(m.serial())
        m.clique(xi + 20, yi + 4 + 2 * 26 + 10)
        esperar(m, "arquivos: menu 2", 10, antes)
        esperar(m, ': "Apagar"', 10, antes)
        verificar(True, "a 3ª escolha do menu abre a pergunta de apagar")
        time.sleep(1)
        m.tecla("esc")
        time.sleep(0.8)
        verificar("arquivos: apagado" not in m.serial()[antes:], "Esc na pergunta não apaga nada")

        # 2. Editor com o .txt: sem os botões de compilar; copia a chave.
        antes = len(m.serial())
        m.clique(xi, yi, duplo=True)
        esperar(m, "editor: pronto", 20, antes)
        time.sleep(1)
        e = janela(m.serial(), "Editor - sem nome")
        png = m.captura("H97-2-editor-texto")
        barra = cor_do_pixel(png, e["x"] + 400, e["y"] + 20)
        verificar(barra == (0xE6, 0xE9, 0xEC), f"o Editor de um .txt não mostra \"Compilar e rodar\" ({barra})")
        m.clique(e["x"] + 60, e["y"] + 80)
        m.tecla("ctrl-a")
        m.tecla("ctrl-c")
        time.sleep(0.5)

        # 3. Configurações > IA: Ctrl+V na caixa da chave e Guardar.
        t = janela(m.serial(), "Terminal")
        m.clique(t["fx"] + 20, t["fy"] + 12)                 # foco de volta no Terminal
        time.sleep(0.5)
        antes = len(m.serial())
        m.comando("configuracoes IA")
        esperar(m, "configuracoes: aberto na aba IA", 15, antes)
        time.sleep(1)
        c = janela(m.serial(), "Configurações")
        m.clique(*controle(m.serial(), "ia.chave", c))
        m.tecla("ctrl-v")
        time.sleep(0.5)
        m.captura("H97-3-chave-colada")
        antes = len(m.serial())
        m.clique(*controle(m.serial(), "ia.guardar", c))
        esperar(m, "configuracoes: chave do OpenRouter guardada", 15, antes)
        verificar(CHAVE not in m.serial(), "a chave não apareceu no registro do sistema")
        verificar(True, "Ctrl+V colou a chave e Guardar a gravou")

        # A chave gravada é a mesma, inteira (só agora ela vai para a tela).
        m.clique(t["fx"] + 20, t["fy"] + 12)
        time.sleep(0.5)
        antes = len(m.serial())
        m.comando("cat /usb/.saguios/chaves.txt")
        esperar(m, "openrouter = " + CHAVE, 10, antes)
        verificar(True, f"chaves.txt tem a chave inteira ({len(CHAVE)} letras)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
