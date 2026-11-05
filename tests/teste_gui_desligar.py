#!/usr/bin/env python3
"""Teste do clique no botao Desligar da barra superior na GUI."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, IMAGEM


def main():
    print("Iniciando teste da GUI com clique no botão Desligar...")
    with Maquina("teste_gui_desligar", "ide", imagem=IMAGEM) as m:
        m.esperar_serial("[GUI] area de trabalho pronta", 60)
        time.sleep(2)

        # O botão Desligar na barra superior fica centrado em x=785, y=10
        print("Clicando no botão 'Desligar' na barra superior (x=785, y=10)...")
        m.clique(785, 10)

        # Verifica se o gerenciador abriu o diálogo /bin/desligar
        m.esperar_serial("botao Desligar clicado -> /bin/desligar", 10)
        print("Diálogo /bin/desligar aberto com sucesso!")
        time.sleep(1)

        # Descobre a posição da janela do /bin/desligar no log serial
        # O kernel registra: [GUI] janela N criada por ...: "Desligar o SaguiOS" 340x150 em (X,Y)
        s = m.serial()
        import re
        match = re.search(r'"Desligar o SaguiOS" \d+x\d+ em \((\d+),(\d+)\)', s)
        if match:
            jx, jy = int(match.group(1)), int(match.group(2))
            print(f"Janela localizada em ({jx}, {jy})")
        else:
            jx, jy = 50, 54
            print(f"Usando coordenadas padrão da janela ({jx}, {jy})")

        # BORDA = 4, BARRA_TITULO = 24
        # Dentro do conteúdo: bt_desligar fica em x=16, y=96 (largura 92, altura 28)
        # Centro do botão bt_desligar:
        clique_x = jx + 4 + 16 + 46
        clique_y = jy + 24 + 96 + 14
        print(f"Confirmando desligamento no diálogo (clique em {clique_x}, {clique_y})...")
        m.clique(clique_x, clique_y)

        # Espera mensagem de encerramento
        m.esperar_serial("[SAGUI] desligando o computador", 20)
        print("Log serial confirmou encerramento pelo diálogo!")

        desligou = False
        try:
            m.proc.wait(timeout=15)
            desligou = True
        except Exception:
            desligou = False

        verificar(desligou, "QEMU encerrou após confirmar no diálogo gráfico")
        print("Sucesso: A GUI acionou o desligamento perfeitamente!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
