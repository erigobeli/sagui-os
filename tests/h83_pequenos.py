#!/usr/bin/env python3
"""H83 (do pull request #7) - os programas pequenos: grep, uname, fetch, df,
top, triangulos e chuva.

Critério:
- o ajuda lista os comandos novos com os apelidos;
- grep acha texto num pipe (ls bin | grep ...) e num arquivo, com -i (também
  nas letras acentuadas), -n, -v e -c; o código de saída é 0 quando acha e 1
  quando não acha;
- uname mostra SaguiOS; uname -a, a versão e a arquitetura i686;
- fetch mostra a ficha do sistema (processador, memória, tela, shell);
- df mostra "/" e, com o pendrive montado, "/usb" com o tamanho certo;
- top atualiza a tela a cada segundo, com a %CPU de cada processo, e sai
  com q;
- triangulos e chuva abrem uma janela e fecham com Esc.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu  # noqa: E402


def limpo(texto):
    """Tira as sequências de cor e de cursor (ESC [ ... letra)."""
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", texto)


def codigo(m, programa):
    """O código de saída da última execução do programa (pelo log)."""
    achados = re.findall(rf"\({re.escape(programa)}\) terminou com codigo (-?\d+)", m.serial())
    return int(achados[-1]) if achados else None


def esperar_janela_e_fechar(m, titulo, linha_log, programa, captura):
    m.comando(programa, tempo=20)
    fim = time.time() + 20
    while not re.search(rf'criada por \d+: "{re.escape(titulo)}"', m.serial()) and time.time() < fim:
        time.sleep(0.2)
    verificar(re.search(rf'criada por \d+: "{re.escape(titulo)}"', m.serial()) is not None,
              f"{programa} abre a janela \"{titulo}\"")
    m.esperar_serial(linha_log, 20)
    time.sleep(1.5)
    m.captura(captura)
    # A janela nova fica com o foco: o Esc vai para ela.
    m.tecla("esc")
    m.esperar_serial(f"({programa}) terminou com codigo 0", 20)
    verificar(True, f"{programa} fecha com Esc")


def main():
    pen = criar_pendrive("h83p-pen", rotulo="MEUPEN", arquivos={"grande.bin": b"x" * 3000000})
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h83p", "ide", extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)

        out = m.comando("ajuda")
        for nome in ("top/monitor", "grep/procurar", "uname/sistema", "fetch/ficha", "df/espaco", "triangulos", "chuva"):
            verificar(nome in out, f"o ajuda lista {nome}")

        # ---- grep
        out = limpo(m.comando("ls bin | grep ri"))
        verificar("triangulos" in out and "calculadora" not in out, "ls bin | grep ri: só as linhas com ri")
        verificar(codigo(m, "grep") == 0, "grep que acha devolve 0")
        out = m.comando("grep -in SAGUI leia-me.txt")
        verificar("1:Bem-vindo ao SaguiOS!" in out, "grep -in: maiúsculas ignoradas e linhas numeradas")
        out = m.comando("grep -c é leia-me.txt")
        n_min = re.findall(r"^(\d+)$", out, re.M)
        out = m.comando("grep -ic É leia-me.txt")
        n_ign = re.findall(r"^(\d+)$", out, re.M)
        verificar(n_min and n_ign and int(n_min[-1]) > 0 and int(n_ign[-1]) >= int(n_min[-1]),
                  f"grep -i vale para letras acentuadas (é: {n_min}, -i É: {n_ign})")
        out = m.comando("grep -c zzzz leia-me.txt")
        verificar(re.search(r"^0$", out, re.M) is not None, "grep -c conta zero linhas")
        verificar(codigo(m, "grep") == 1, "grep que não acha devolve 1")
        out = m.comando("ls bin | grep -v a | grep -c s")
        verificar(re.search(r"^\d+$", out, re.M) is not None, "grep -v num pipe de três etapas")
        m.captura("H83-2-grep")

        # ---- uname e fetch
        out = m.comando("uname")
        verificar("SaguiOS" in out, "uname mostra SaguiOS")
        out = m.comando("uname -a")
        verificar(re.search(r"SaguiOS \S+ i686 \(ligado há \d+:\d\d:\d\d\)", out) is not None, "uname -a: versão, i686 e tempo ligado")
        out = limpo(m.comando("fetch"))
        for campo in ("Sistema:", "Processador:", "Memória:", "Tela:", "Shell: sagsh", "Processos:"):
            verificar(campo in out, f"fetch mostra {campo}")
        time.sleep(0.5)
        m.captura("H83-3-fetch")

        # ---- df
        out = m.comando("df")
        verificar(re.search(r"^/\s+ramdisk \+ RAM", out, re.M) is not None, "df mostra /")
        verificar("nenhum pendrive montado" in out, "df sem pendrive montado avisa")
        m.comando("montar")
        out = m.comando("espaco -k")
        linha = re.search(r"^/usb\s+FAT32\s+(\d+) KB\s+(\d+) KB\s+(\d+) KB\s+(\d+)%\s+\"MEUPEN\"", out, re.M)
        verificar(linha is not None, "df mostra /usb com o rótulo do pendrive")
        total, usado, livre, pct = (int(x) for x in linha.groups())
        verificar(60000 < total < 65536 and total == usado + livre, f"/usb: total {total} KB = usado + livre")
        verificar(2900 <= usado < 3300 and pct == 5, f"/usb: o arquivo de 3 MB aparece como usado ({usado} KB, {pct}%)")
        m.captura("H83-4-df")

        # ---- top: duas telas, depois q
        antes = m.serial().count("sagui:")
        n0 = m.serial().count("top\x1b[0m - ligado")
        m.digitar("top", 0.04)
        m.tecla("ret")
        fim = time.time() + 20
        while m.serial().count("top\x1b[0m - ligado") < n0 + 3 and time.time() < fim:
            time.sleep(0.2)
        telas = m.serial().count("top\x1b[0m - ligado") - n0
        verificar(telas >= 3, f"top atualiza a tela a cada segundo ({telas} telas)")
        ultima = limpo(m.serial().rsplit("top\x1b[0m - ligado", 1)[1])
        verificar("%CPU" in ultima and re.search(r"^\s+0\s+0\s+\S+\s+\d+ KB\s+\d+,\d\s+ocioso", ultima, re.M) is not None,
                  "top mostra a %CPU de cada processo")
        pcts = [int(a) * 10 + int(b) for a, b in re.findall(r" KB\s+(\d+),(\d)\s", ultima)]
        verificar(pcts and 900 <= sum(pcts) <= 1100, f"as %CPU do intervalo somam uns 100% ({sum(pcts) / 10}%)")
        m.captura("H83-1-top")
        m.tecla("q")
        fim = time.time() + 15
        while m.serial().count("sagui:") <= antes and time.time() < fim:
            time.sleep(0.2)
        verificar(codigo(m, "top") == 0, "top sai com q")

        # ---- as duas janelas
        esperar_janela_e_fechar(m, "Triângulos", "quadros por segundo com", "triangulos", "H83-5-triangulos")
        esperar_janela_e_fechar(m, "Chuva de letras", "chuva: 70 colunas", "chuva", "H83-6-chuva")
    return 0


if __name__ == "__main__":
    sys.exit(main())
