#!/usr/bin/env python3
"""H124 - o SaguiOS em português, espanhol e inglês (primeira parte, D073).

Critério:
  1. (no computador de build) as tabelas /sistema/idiomas/es.txt e en.txt têm
     a descrição curta e a explicação de todos os comandos da ajuda do sagsh,
     com as mesmas marcas %s/%d do português e as descrições curtas cabendo
     na coluna de 17 letras;
  2. no shell, `config sistema.idioma es` faz a `ajuda` sair em espanhol (só o
     nome Unix na lista, descrições traduzidas, "ajuda ls" com a explicação e o
     apelido em português), e o mesmo com `en`; um shell novo (`sagsh`)
     cumprimenta na língua escolhida; `config sistema.idioma pt` volta tudo;
  3. nas Configurações, a aba "Idioma e teclado" troca a língua pelos botões
     (grava sistema.idioma) e a janela reaberta tem o título traduzido.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import RAIZ, Maquina, verificar, imagem_console, janela  # noqa: E402


def marcas(s):
    return re.findall(r"%[-0*.1-9]*[a-zA-Z ]", s.replace("%%", ""))


def ler_tabela(lingua):
    tab, problemas = {}, []
    with open(os.path.join(RAIZ, "rootfs", "sistema", "idiomas", lingua + ".txt"), encoding="utf-8") as f:
        for n, linha in enumerate(f, 1):
            linha = linha.strip()
            if not linha or linha.startswith("#"):
                continue
            if " = " not in linha:
                problemas.append(f"linha {n}: sem o separador ' = '")
                continue
            pt, outro = (x.replace("\\=", "=") for x in linha.split(" = ", 1))
            if pt in tab:
                problemas.append(f"linha {n}: texto repetido")
            if marcas(pt) != marcas(outro):
                problemas.append(f"linha {n}: marcas de formato diferentes")
            tab[pt] = outro
    verificar(not problemas, f"{lingua}.txt bem formado ({len(tab)} traduções; problemas: {problemas[:3]})")
    return tab


def conferir_tabelas():
    src = open(os.path.join(RAIZ, "userland", "programas", "sagsh.c"), encoding="utf-8").read()
    cmds = re.findall(r'^\s*\{ "[^"]+", (?:"[^"]*"|NULL), (?:true|false), (?:true|false), '
                      r'"((?:[^"\\]|\\.)*)", "(?:[^"\\]|\\.)*", "((?:[^"\\]|\\.)*)" \},', src, re.M)
    verificar(len(cmds) > 80, f"a tabela de comandos do sagsh foi lida ({len(cmds)} comandos)")
    for lingua in ("es", "en"):
        tab = ler_tabela(lingua)
        faltam, longas = [], []
        for curta, expl in cmds:
            curta, expl = curta.replace('\\"', '"'), expl.replace('\\"', '"')
            faltam += [t for t in (curta, expl) if t not in tab]
            if curta in tab and len(tab[curta]) > 17:
                longas.append(tab[curta])
        verificar(not faltam, f"{lingua}: todos os comandos da ajuda traduzidos (faltam {faltam[:3]})")
        verificar(not longas, f"{lingua}: as descrições curtas cabem na coluna de 17 letras (passam: {longas})")


def main():
    conferir_tabelas()

    with Maquina("h124", "ide", imagem=imagem_console()) as m:
        m.esperar_serial("[SAGUI] shell iniciado: processo", 120)
        m.esperar_serial("sagui:/$", 30)

        out = m.comando("ajuda")
        verificar("Comandos do SaguiOS (nome/apelido):" in out and "ls/listar" in out and "lista arquivos" in out,
                  "em português (padrão), a ajuda mostra nome/apelido")

        out = m.comando("config sistema.idioma es")
        verificar("[CONFIG] sistema.idioma = es" in m.serial(), "config grava sistema.idioma = es")
        out = m.comando("ajuda")
        verificar("Comandos de SaguiOS:" in out, "ajuda em espanhol: título")
        verificar(re.search(r"^  ls +lista archivos", out, re.M), "ajuda em espanhol: só o nome Unix e a descrição traduzida")
        verificar("ls/listar" not in out, "ajuda em espanhol: sem o apelido em português na lista")
        verificar("ajuda <comando> explica un comando." in out, "ajuda em espanhol: as dicas do fim")
        out = m.comando("ajuda ls")
        verificar("Uso: ls [-l] [pasta]" in out and "Muestra los archivos y carpetas" in out, "ajuda ls em espanhol")
        verificar("Alias en portugués: listar" in out, "ajuda ls mostra o apelido em português")
        out = m.comando("naoexiste")
        verificar("sagsh: comando no encontrado: naoexiste (escribe ajuda)" in out, "mensagem de comando desconhecido em espanhol")
        out = m.comando("cd /naoexiste")
        verificar("cd: /naoexiste: no existe" in out, "os nomes dos erros também saem em espanhol")
        out = m.comando("sagsh")   # um shell novo, como um Terminal aberto depois da troca
        verificar("sagsh: el shell de SaguiOS" in out, "um shell novo cumprimenta em espanhol")
        out = m.comando("sair")
        verificar("sagsh: ¡hasta luego!" in out, "o shell se despede em espanhol (Latin-1 lido do UTF-8)")
        m.captura("H124-ajuda-es")

        m.comando("config sistema.idioma en")
        out = m.comando("ajuda")
        verificar("SaguiOS commands:" in out and re.search(r"^  ls +list files", out, re.M), "ajuda em inglês")
        out = m.comando("ajuda cd")
        verificar("Usage: cd <pasta>" in out and "Changes the current folder." in out and "Portuguese alias: entrar" in out,
                  "ajuda cd em inglês")
        verificar("Type: shell built-in command" in out, "o tipo do comando em inglês")
        m.captura("H124-ajuda-en")

        m.comando("config sistema.idioma pt")
        out = m.comando("ajuda ls")
        verificar("Mostra os arquivos e pastas" in out and "Apelido em português: listar" in out, "de volta ao português")
        verificar("idioma: es, " in m.serial() and "idioma: en, " in m.serial(), "as tabelas es e en foram carregadas")
        verificar("marcas de formato diferentes" not in m.serial(), "nenhuma tradução recusada por marcas diferentes")

    # As Configurações: a aba "Idioma e teclado".
    with Maquina("h124-janela", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("configuracoes idioma")
        m.esperar_serial("configuracoes: aberto na aba Teclado", 15)
        time.sleep(0.6)
        j = janela(m.serial(), "Configurações")
        x0, y0 = j["x"], j["y"]
        m.clique(x0 + 190 + 128 + 60, y0 + 280 + 14)        # botão "Español"
        m.esperar_serial("configuracoes: idioma es", 10)
        verificar("[CONFIG] sistema.idioma = es" in m.serial(), "o botão Español grava sistema.idioma = es")
        time.sleep(0.8)
        m.captura("H124-configuracoes-es")
        # Fecha pelo X; o Terminal volta a ter o foco e abre as Configurações de novo.
        n = m.serial().count("fechar janela")
        m.clique(j["fx"] + j["l"] + 4 - 2 - 20 + 8, j["fy"] + 12)
        fim = time.time() + 10
        while m.serial().count("fechar janela") <= n and time.time() < fim:
            time.sleep(0.2)
        time.sleep(0.6)
        out = m.comando("ajuda")
        verificar("Comandos de SaguiOS:" in out, "o Terminal aberto antes também segue a escolha (no próximo comando)")
        m.comando("configuracoes idioma")
        m.esperar_serial('"Configuración"', 15)
        time.sleep(0.6)
        j = janela(m.serial(), "Configuración")
        m.clique(j["x"] + 190 + 256 + 60, j["y"] + 280 + 14)  # botão "English"
        m.esperar_serial("configuracoes: idioma en", 10)
        time.sleep(0.8)
        m.captura("H124-configuracoes-en")
        m.clique(j["x"] + 190 + 60, j["y"] + 280 + 14)        # botão "Português"
        m.esperar_serial("configuracoes: idioma pt", 10)
        verificar("[CONFIG] sistema.idioma = pt" in m.serial(), "o botão Português volta para sistema.idioma = pt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
