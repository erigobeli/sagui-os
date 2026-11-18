#!/usr/bin/env python3
"""Roda todos os testes tests/hNN_*.py em ordem (ou os passados como argumento).

Uso:
  python3 tests/rodar_todos.py              todos os testes
  python3 tests/rodar_todos.py h16 h92      só os que começam com esses nomes
  python3 tests/rodar_todos.py --pedaco 2/6 só o 2.º de 6 pedaços (CI, D075)
  python3 tests/rodar_todos.py --pedaco 2/6 --listar
                                            mostra o pedaço sem rodar nada

O --pedaco serve para o CI rodar a suíte em várias máquinas ao mesmo tempo:
cada máquina roda um pedaço, e os pedaços juntos dão exatamente a suíte
inteira, sem repetir nenhum teste.
"""
import glob
import os
import subprocess
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))

# Quanto cada teste costuma levar, em segundos (QEMU sem aceleração, máquina
# de desenvolvimento; o maior tempo de três rodadas recentes da suíte). Só
# serve para equilibrar os pedaços: o número exato não importa, a proporção
# sim. Um teste que não está aqui conta como TEMPO_PADRAO; vale acrescentar a
# linha dele quando o tempo ficar conhecido.
TEMPOS = {
    "b3_bip": 34, "b4_sagui3d": 61, "h01_ambiente": 1, "h02_mbr": 14,
    "h03_modo_protegido": 74, "h04_imagens": 31, "h05_interrupcoes": 31,
    "h06_memoria": 29, "h07_teclado": 20, "h08_sgfs": 15, "h09_terminal": 24,
    "h100_arquivos": 40, "h101_visualizador": 34, "h102_captura": 24,
    "h103_lixeira": 77, "h104_cantos": 25, "h105_disco_ata": 24, "h106_botoes": 24,
    "h107_menu_640": 33, "h108_lixeira_pendrive": 24, "h109_trava_discos": 29,
    "h10_paginacao": 28, "h110_instalar": 122, "h111_area_unidades": 37,
    "h112_tabela": 74, "h114_menu_leve": 50, "h115_dirname": 18, "h116_hostname": 19,
    "h117_du": 18, "h118_renomear_devagar": 63, "h11_escalonador": 14, "h12_anel3": 12,
    "h13_elf": 22, "h14_arquivos": 12, "h15_pipes": 12, "h16_shell": 51,
    "h17_video": 38, "h18_fonte": 40, "h19_mouse": 38, "h20_area_trabalho": 24,
    "h21_janelas": 25, "h22_componentes": 33, "h23_arquivos": 48,
    "h24_terminal_janela": 24, "h25_editor": 31, "h26_calculadora": 37,
    "h27_banana": 31, "h28_acabamento": 31, "h29_dois_controladores": 16,
    "h29_ehci": 23, "h30_conexao": 16, "h31_setores": 18, "h32_fat32_leitura": 44,
    "h33_fat32_gravacao": 59, "h34_integracao": 72, "h35_pci": 16, "h36_energia": 43,
    "h37_configuracoes": 29, "h38_configuracoes_app": 39, "h39_programas": 31,
    "h40_som": 17, "h41_mistura": 28, "h42_tocar": 35, "h43_e1000": 31, "h44_ping": 27,
    "h45_dhcp_dns": 30, "h46_rede_usb": 1, "h47_rede_interface": 31, "h48_tcp": 107,
    "h49_http_ntp": 28, "h50_cripto": 14, "h50b_aleatorio": 30, "h52_h53_tls": 33,
    "h53_https": 41, "h55_terminal": 16, "h56_h58_ssh": 44, "h57_h58_ssh_saguios": 54,
    "h59_h60_imagens": 4, "h61_html": 14, "h62_navegador": 65, "h63_sites": 117,
    "h64_h66_compilador": 108, "h67_compilar_no_editor": 34, "h68_prova_de_fogo": 155,
    "h69_h70_saguicode": 220, "h69_json": 16, "h71_h72_saguicode": 38,
    "h74_h77_visual": 48, "h78_redimensionar": 41, "h79_copiar": 32, "h79_ctrl_c": 90,
    "h79_g470_correcoes": 52, "h80_flutuante": 22, "h81_visual": 63, "h82_cipo": 102,
    "h82_video": 15, "h83_desligar": 24, "h83_pequenos": 37, "h84_video_nativo": 541,
    "h86_mp3": 31, "h87_tocador": 84, "h88_video_alto": 87, "h89_menu_iniciar": 32,
    "h89_saguicode_janela": 55, "h90_teclado_es": 43, "h91_utilitarios": 124,
    "h92_cacabananas": 31, "h92_chip8": 45, "h92_jclic": 90, "h92_jogos": 57,
    "h92_paciencia": 39, "h93_compactar": 168, "h93_compactar_pc": 31,
    "h94_encaixe": 35, "h94_sobre": 24, "h95_atalhos": 23, "h96_zip_malformado": 2,
    "h97_colar_chave": 36, "h98_suave_pc": 1, "h99_laco": 23,
}
TEMPO_PADRAO = 40


def tempo_de(caminho):
    return TEMPOS.get(os.path.basename(caminho)[:-3], TEMPO_PADRAO)


def dividir(testes, total):
    """Reparte os testes em `total` pedaços de tempo parecido.

    O jeito é o de quem divide tarefas numa equipe: pega a tarefa mais
    demorada que sobrou e entrega para quem está com menos trabalho até
    agora. Começar pelas grandes evita que uma tarefa enorme caia no fim,
    num pedaço que já estava cheio. O empate é desfeito pelo nome e pelo
    número do pedaço, então todas as máquinas do CI calculam a mesma divisão
    sem conversar entre si. Dentro de cada pedaço, a ordem continua a
    alfabética de sempre.
    """
    pedacos = [[] for _ in range(total)]
    carga = [0] * total
    for t in sorted(testes, key=lambda t: (-tempo_de(t), os.path.basename(t))):
        i = min(range(total), key=lambda k: (carga[k], k))
        pedacos[i].append(t)
        carga[i] += tempo_de(t)
    return [sorted(p, key=testes.index) for p in pedacos], carga


def ler_pedaco(texto):
    """'2/6' -> (2, 6), conferindo que o número faz sentido."""
    try:
        n, total = (int(x) for x in texto.split("/"))
    except ValueError:
        n = total = 0
    if not (1 <= n <= total):
        sys.exit(f"ERRO: --pedaco espera N/TOTAL, com 1 <= N <= TOTAL (veio '{texto}')")
    return n, total


def main():
    args = sys.argv[1:]
    pedaco = None
    listar = False
    filtros = []
    i = 0
    while i < len(args):
        if args[i] == "--pedaco" and i + 1 < len(args):
            pedaco = ler_pedaco(args[i + 1])
            i += 2
            continue
        if args[i].startswith("--pedaco="):
            pedaco = ler_pedaco(args[i].split("=", 1)[1])
        elif args[i] == "--listar":
            listar = True
        elif args[i].startswith("--"):
            sys.exit(f"ERRO: opção desconhecida: {args[i]}")
        else:
            filtros.append(args[i])
        i += 1

    testes = sorted(glob.glob(os.path.join(AQUI, "h[0-9][0-9]*.py"))) + sorted(glob.glob(os.path.join(AQUI, "b[0-9]_*.py")))
    if filtros:
        testes = [t for t in testes if any(os.path.basename(t).startswith(f.lower()) for f in filtros)]
    if pedaco:
        n, total = pedaco
        pedacos, carga = dividir(testes, total)
        testes = pedacos[n - 1]
        print(f"Pedaço {n}/{total}: {len(testes)} testes, uns {carga[n - 1]}s pela tabela de tempos", flush=True)
    if listar:
        for t in testes:
            print(os.path.basename(t))
        return 0

    # Conta qual acelerador o QEMU vai usar (KVM ou TCG, D075): ajuda a
    # entender a diferença de tempo entre uma execução e outra.
    sys.path.insert(0, AQUI)
    import qemu
    print(f"QEMU com acelerador: {qemu.acelerador()}", flush=True)

    # Compila antes: testar um binário velho depois de um erro de compilação
    # daria um resultado enganoso.
    raiz = os.path.dirname(AQUI)
    if subprocess.run(["make", "-s", "-C", raiz]).returncode != 0:
        print("ERRO: o build falhou; nenhum teste rodou")
        return 1
    falhas = []
    for t in testes:
        nome = os.path.basename(t)
        print(f"== {nome}", flush=True)
        ini = time.time()
        try:
            r = subprocess.run([sys.executable, t], timeout=600)
            ok = r.returncode == 0
        except subprocess.TimeoutExpired:
            ok = False
            print("  TEMPO ESGOTADO")
        print(f"   {'PASSOU' if ok else 'FALHOU'} ({time.time() - ini:.1f}s)", flush=True)
        if not ok:
            falhas.append(nome)
    print(f"\n{len(testes) - len(falhas)}/{len(testes)} testes passaram")
    if falhas:
        print("Falharam:", ", ".join(falhas))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
