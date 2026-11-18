#!/usr/bin/env python3
"""H109 - a trava dos setores crus no kernel (issue #45, parte 2; D063).

Critérios, conferindo o disco inteiro byte a byte:
  - sem a confirmação, gravar setores crus dá ERRO_ACESSO (-14) em qualquer
    disco, e ler um disco interno também; ler o que o instalador copia da
    origem (setores 0 e 1 e a partição do sistema) dá, e o resto da origem
    não;
  - a caixa do kernel mostra o disco (modelo, tamanho, o que tem nele) e,
    separado, o resumo que o programa mandou;
  - nome errado não libera; Esc cancela (ERRO_ACESSO);
  - o nome certo libera ler e gravar, só para aquele processo: a leitura
    devolve os bytes da imagem, e a liberação acaba quando ele termina;
  - no fim, o disco tem exatamente um setor diferente (o gravado depois da
    confirmação), com o conteúdo esperado; nenhum outro byte mudou;
  - capturas da caixa para a revisão a olho.
"""
import hashlib
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, SAIDA, IMAGEM  # noqa: E402

ERRO_ACESSO = -14
SETOR_NEGADO = 7     # as tentativas sem confirmação apontam para cá
SETOR_GRAVADO = 9    # e a gravação confirmada, para cá


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def setores(m, args):
    """Roda /testes/setores e devolve (código, soma) da última operação."""
    n = len(m.serial())
    m.digitar(f"/testes/setores {args}\n", 0.03)
    esperar(m, "setores: ", 20, n)
    esperar(m, "sagui:/$", 20, n)
    r = re.findall(r"setores: (?:ler|gravar) \w+ \d+ -> (-?\d+) soma (\d+)", m.serial()[n:])[-1]
    return int(r[0]), int(r[1])


def soma(setor):
    s = 0
    for b in setor:
        s = (s * 31 + b) & 0xFFFFFFFF
    return s


def main():
    os.makedirs(SAIDA, exist_ok=True)
    # Um HD "de quem testa": bytes pseudoaleatórios e uma tabela MBR com uma
    # partição Linux, para a caixa ter o que mostrar.
    hd = os.path.join(SAIDA, "h109-hd.img")
    tam = 64 * 1024 * 1024
    with open(hd, "wb") as f:
        bloco = hashlib.sha256(b"h109").digest() * (1 << 15)   # 1 MB
        for _ in range(tam // len(bloco)):
            f.write(bloco)
    with open(hd, "r+b") as f:
        mbr = bytearray(f.read(512))
        mbr[446:510] = bytes(64)
        mbr[446 + 4] = 0x83
        struct.pack_into("<II", mbr, 446 + 8, 2048, tam // 512 - 2048)
        mbr[510:512] = b"\x55\xaa"
        f.seek(0)
        f.write(mbr)
    original = open(hd, "rb").read()

    # Onde fica a partição do sistema na imagem de origem.
    orig_mbr = open(IMAGEM, "rb").read(512)
    sis = [struct.unpack_from("<II", orig_mbr, 446 + k * 16 + 8) for k in range(4) if orig_mbr[446 + k * 16 + 4] == 0xDA]
    verificar(len(sis) == 1, f"a imagem de origem tem a partição do sistema ({sis})")
    sis_ini, sis_n = sis[0]
    fora = sis_ini + sis_n + 8   # um setor da origem fora do que o instalador copia
    # A imagem acaba na partição do sistema; uma cópia maior dá à origem
    # setores fora dela, como num pendrive de verdade.
    origem = os.path.join(SAIDA, "h109-origem.img")
    with open(IMAGEM, "rb") as f, open(origem, "wb") as g:
        g.write(f.read())
        g.truncate(32 * 1024 * 1024)

    with Maquina("h109", "ide", imagem=origem, memoria="768M", extras=["-drive", f"format=raw,file={hd},if=ide,index=1"]) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        s = m.serial()
        verificar("[DISCO] hd0:" in s and "[DISCO] hd1:" in s, "o driver ATA acha os dois discos")

        # Sem confirmação.
        verificar(setores(m, f"hd1 {SETOR_NEGADO} gravar")[0] == ERRO_ACESSO, "sem confirmação, gravar no hd1 dá ERRO_ACESSO")
        verificar(setores(m, f"hd1 {SETOR_NEGADO} ler")[0] == ERRO_ACESSO, "sem confirmação, ler o disco interno hd1 dá ERRO_ACESSO")
        verificar(setores(m, f"hd0 {SETOR_NEGADO} gravar")[0] == ERRO_ACESSO, "sem confirmação, gravar no disco de origem também dá ERRO_ACESSO")
        verificar(setores(m, "hd0 1 ler")[0] == 0, "ler o setor 1 da origem (o cabeçalho de boot) dá")
        verificar(setores(m, f"hd0 {sis_ini + 3} ler")[0] == 0, "ler a partição do sistema da origem dá")
        verificar(setores(m, f"hd0 {fora} ler")[0] == ERRO_ACESSO, f"ler a origem fora do sistema (setor {fora}) dá ERRO_ACESSO")

        # A caixa: nome errado, depois Esc.
        n = len(m.serial())
        m.digitar(f"/testes/setores hd1 {SETOR_NEGADO} gravar liberar\n", 0.03)
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        caixa = m.serial()[n:]
        verificar("disco interno" in caixa and "ext (Linux)" in caixa, "a caixa mostra o disco e o que tem nele hoje (do kernel)")
        verificar("O programa diz que vai:" in caixa and "gravar 1 setor de teste" in caixa,
                  "a caixa mostra, separado, o resumo que o programa mandou")
        time.sleep(0.8)
        m.digitar("hd0\n", 0.05)
        esperar(m, "confirmacao do disco hd1: nome diferente", 10, n)
        time.sleep(0.6)
        m.captura("H109-confirmacao-nome-errado")
        m.tecla("esc")
        esperar(m, f"setores: gravar hd1 {SETOR_NEGADO} -> ", 10, n)
        verificar(f"setores: liberar hd1 -> {ERRO_ACESSO}" in m.serial()[n:] and f"gravar hd1 {SETOR_NEGADO} -> {ERRO_ACESSO}" in m.serial()[n:],
                  "nome errado não libera; Esc cancela e a gravação continua travada")
        esperar(m, "sagui:/$", 10, n)

        # O nome certo libera ler...
        n = len(m.serial())
        m.digitar(f"/testes/setores hd1 {SETOR_NEGADO} ler liberar\n", 0.03)
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.5)
        m.digitar("hd1\n", 0.05)
        esperar(m, f"setores: ler hd1 {SETOR_NEGADO} -> ", 10, n)
        esperado = soma(original[SETOR_NEGADO * 512:(SETOR_NEGADO + 1) * 512])
        verificar(f"ler hd1 {SETOR_NEGADO} -> 0 soma {esperado}" in m.serial()[n:], "liberado, ler devolve os bytes da imagem")
        esperar(m, "sagui:/$", 10, n)

        # ...e gravar, só para esse processo.
        n = len(m.serial())
        m.digitar(f"/testes/setores hd1 {SETOR_GRAVADO} gravar liberar\n", 0.03)
        esperar(m, "[GUI] confirmacao do disco hd1 aberta", 20, n)
        time.sleep(0.8)
        m.captura("H109-confirmacao")
        m.digitar("hd1\n", 0.05)
        esperar(m, f"setores: gravar hd1 {SETOR_GRAVADO} -> ", 10, n)
        verificar("setores: liberar hd1 -> 0" in m.serial()[n:] and f"gravar hd1 {SETOR_GRAVADO} -> 0" in m.serial()[n:],
                  "digitando hd1 na caixa do kernel, a gravação passa")
        esperar(m, "liberacao do processo", 10, n)
        esperar(m, "sagui:/$", 10, n)
        verificar(setores(m, f"hd1 {SETOR_NEGADO} gravar")[0] == ERRO_ACESSO, "a liberação acaba com o processo: o próximo volta a ser travado")
        verificar(setores(m, f"hd1 {SETOR_NEGADO} ler")[0] == ERRO_ACESSO, "e ler também volta a ser travado")

    depois = open(hd, "rb").read()
    diferentes = sorted({i // 512 for i in range(0, len(depois), 512) if depois[i:i + 512] != original[i:i + 512]})
    verificar(len(depois) == len(original) and diferentes == [SETOR_GRAVADO],
              f"byte a byte: só o setor {SETOR_GRAVADO} mudou (mudaram: {diferentes[:8]})")
    verificar(depois[SETOR_GRAVADO * 512:(SETOR_GRAVADO + 1) * 512] == bytes(0xA5 ^ (i & 0xFF) for i in range(512)),
              "o setor gravado tem o conteúdo que o programa mandou")


if __name__ == "__main__":
    main()
