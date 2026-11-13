#!/usr/bin/env python3
"""H47 - rede na interface.

Critério: a aba Rede das Configurações mostra o estado; o modo manual com
outro endereço funciona (ping no roteador); o ícone de rede na barra de cima
muda quando o cabo é desligado (monitor do QEMU: set_link) e religado.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, cor_do_pixel  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]
X0 = 190
ICONE = (1024 - 21 * 8 - 10 - 30 - 28 + 2, 2 + 2 + 1)   # pixel da borda de cima do monitor


def main():
    with Maquina("h47", "ide", extras=REDE) as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 20)
        time.sleep(1.5)
        png = m.captura("H47-1-barra-conectado")
        verificar(cor_do_pixel(png, *ICONE) == (0x5F, 0xD3, 0x5F), "ícone de rede verde na barra (conectado)")
        m.monitor("set_link n0 off")
        m.esperar_serial("[REDE] cabo desligado", 10)
        time.sleep(1.5)
        png = m.captura("H47-2-barra-sem-cabo")
        verificar(cor_do_pixel(png, *ICONE) == (0x8A, 0x93, 0x9C), "ícone cinza com o cabo desligado")
        m.monitor("set_link n0 on")
        m.esperar_serial("[REDE] cabo ligado", 10)
        m.esperar_serial("[REDE] DHCP: endereco", 20)
        verificar(True, "religado o cabo, o DHCP pede o endereço de novo")
        m.comando("configuracoes rede")
        m.esperar_serial("configuracoes: aberto na aba Rede", 15)
        time.sleep(1)
        j = janela(m.serial(), "Configurações")
        m.captura("H47-3-aba-rede")
        m.clique(j["x"] + X0 + 135, j["y"] + 104 + 14)            # usar manual
        m.esperar_serial("configuracoes: rede manual", 10)
        time.sleep(0.6)
        m.clique(j["x"] + X0 + 110 + 10, j["y"] + 148 + 10)       # caixa do endereço
        m.tecla("end")
        for _ in range(16):
            m.tecla("backspace", 0.02)
        m.digitar("10.0.2.20")
        m.clique(j["x"] + X0 + 70, j["y"] + 284 + 14)              # Aplicar
        m.esperar_serial("configuracoes: rede manual 10.0.2.20", 10)
        m.esperar_serial("[REDE] configuracao manual aplicada", 10)
        verificar(True, "o modo manual aplica o endereço digitado")
        m.clique(j["fx"] + j["l"] + 4 - 2 - 20 + 8, j["fy"] + 12)   # fecha as Configurações
        time.sleep(0.8)
        out = m.comando("rede")
        verificar("Endereço:  10.0.2.20" in out and "manual" in out, "o comando rede mostra o endereço manual")
        out = m.comando("ping 10.0.2.2 2", tempo=30)
        verificar("2 respostas" in out, "com o endereço manual, o roteador responde ao ping")
    return 0


if __name__ == "__main__":
    sys.exit(main())
