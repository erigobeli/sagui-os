#!/usr/bin/env python3
"""Análise estática do SaguiOS com o analisador do GCC (-fanalyzer).

O que faz: pede ao próprio Makefile a lista de comandos de compilação
(`make -Bn`, que só imprime, sem compilar), pega os que compilam um .c de
kernel/ ou de userland/ e roda cada um de novo com -fanalyzer, jogando o
objeto fora. Assim cada arquivo é analisado com exatamente as mesmas opções
do build de verdade (32 bits, freestanding, os -I de cada parte).

O -fanalyzer do GCC (manual do GCC, seção "Options That Control Static
Analysis") segue os caminhos possíveis de cada função e avisa de defeitos
como ponteiro nulo usado, memória usada depois de liberada, variável lida
antes de receber valor, escrita ou leitura fora de um vetor e vazamento de
descritor.

O código de terceiros (terceiros/) fica de fora: não é do SaguiOS e não é
mexido aqui (AGENTS.md). Avisos que caem num cabeçalho de terceiros/ só
são contados.

Uso:
    python3 tools/analise_estatica.py            # informativo: sai com 0
    python3 tools/analise_estatica.py --estrito  # sai com 1 se houver achado
    python3 tools/analise_estatica.py -j 4       # quantos gcc ao mesmo tempo

Saídas: build/analise/relatorio.txt (tudo o que o GCC disse, arquivo por
arquivo) e build/analise/resumo.md (contagem por tipo e lista de achados),
que o CI também mostra no resumo da execução.
"""
import argparse
import collections
import concurrent.futures
import os
import re
import shlex
import subprocess
import sys
import time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAIDA = os.path.join(RAIZ, 'build', 'analise')

# arquivo:linha:coluna: warning: mensagem [-Wopção]
AVISO = re.compile(r'^([^:\s]+):(\d+):(\d+): warning: (.*?)(?: \[(-W[^\]]+)\])?$')


def comandos_de_compilacao():
    """Os comandos gcc que o Makefile usaria para compilar tudo do zero."""
    r = subprocess.run(['make', '-Bn'], cwd=RAIZ, capture_output=True, text=True, check=True)
    cmds = []
    for linha in r.stdout.splitlines():
        if not linha.startswith('gcc ') or ' -c ' not in linha:
            continue
        partes = shlex.split(linha)
        fonte = partes[-1]
        # Só o que é do SaguiOS; o C gerado no build (fonte, ícones) é tabela.
        if not fonte.endswith('.c') or not fonte.startswith(('kernel/', 'userland/')):
            continue
        # Troca "-o objeto" por "-o /dev/null": a análise não grava nada no build.
        i = partes.index('-o')
        partes[i + 1] = os.devnull
        partes[1:1] = ['-fanalyzer', '-fdiagnostics-plain-output', '-fdiagnostics-color=never']
        cmds.append((fonte, partes))
    # Um arquivo pode aparecer em dois programas com as mesmas opções.
    vistos, unicos = set(), []
    for fonte, partes in cmds:
        chave = (fonte, tuple(partes))
        if chave not in vistos:
            vistos.add(chave)
            unicos.append((fonte, partes))
    return unicos


def analisar(item, limite):
    """Analisa um arquivo. Devolve (fonte, código, texto, segundos); código
    None quer dizer que o analisador passou do limite de tempo."""
    fonte, partes = item
    inicio = time.monotonic()
    try:
        r = subprocess.run(partes, cwd=RAIZ, capture_output=True, text=True, timeout=limite)
        return fonte, r.returncode, r.stderr, time.monotonic() - inicio
    except subprocess.TimeoutExpired as e:
        texto = e.stderr.decode('utf-8', 'replace') if isinstance(e.stderr, bytes) else (e.stderr or '')
        return fonte, None, texto, time.monotonic() - inicio


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--estrito', action='store_true', help='sai com erro se houver achado')
    ap.add_argument('-j', type=int, default=os.cpu_count() or 2, help='análises ao mesmo tempo')
    # O analisador segue caminhos dentro de cada função; num arquivo enorme
    # (o motor de JavaScript) isso leva muitos minutos. O limite impede que
    # um arquivo só segure o CI inteiro: ele aparece no resumo como não
    # analisado até termos uma solução melhor.
    ap.add_argument('--limite', type=int, default=1800, help='segundos por arquivo (padrão 1800)')
    args = ap.parse_args()

    # O navegador inclui um cabeçalho gerado pelo build (o JavaScript embutido).
    subprocess.run(['make', 'build/gerado/js_base.h'], cwd=RAIZ, check=True,
                   stdout=subprocess.DEVNULL)
    cmds = comandos_de_compilacao()
    print(f'Analisando {len(cmds)} arquivos com gcc -fanalyzer ({args.j} ao mesmo tempo)...', flush=True)

    os.makedirs(SAIDA, exist_ok=True)
    achados = []           # (arquivo, linha, mensagem, opção)
    de_terceiros = 0
    falhas = []
    sem_tempo = []
    with open(os.path.join(SAIDA, 'relatorio.txt'), 'w', encoding='utf-8') as rel, \
            concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as ex:
        tarefas = [ex.submit(analisar, c, args.limite) for c in cmds]
        for feitos, t in enumerate(concurrent.futures.as_completed(tarefas), 1):
            fonte, codigo, texto, seg = t.result()
            print(f'[{feitos}/{len(cmds)}] {fonte} ({seg:.0f} s)', flush=True)
            if texto.strip():
                rel.write(f'===== {fonte}\n{texto}\n')
                rel.flush()
            if codigo is None:
                sem_tempo.append(fonte)
            elif codigo != 0:
                falhas.append(fonte)
            for linha in texto.splitlines():
                m = AVISO.match(linha)
                if not m:
                    continue
                arq, num, _col, msg, opcao = m.groups()
                if arq.startswith('terceiros/'):
                    de_terceiros += 1
                    continue
                achados.append((arq, int(num), msg, opcao or '-W?'))

    # O mesmo aviso num cabeçalho aparece uma vez por arquivo que o inclui.
    achados = sorted(set(achados))
    por_tipo = collections.Counter(a[3] for a in achados)
    resumo = ['## Análise estática (gcc -fanalyzer)', '',
              f'- Arquivos analisados: {len(cmds)}',
              f'- Achados no código do SaguiOS: {len(achados)}',
              f'- Avisos em cabeçalhos de terceiros/ (ignorados): {de_terceiros}',
              f'- Arquivos em que o gcc falhou: {len(falhas)}',
              f'- Arquivos que passaram de {args.limite} s (não analisados até o fim): {len(sem_tempo)}', '']
    if por_tipo:
        resumo += ['| Tipo | Quantos |', '|---|---|']
        resumo += [f'| `{t}` | {n} |' for t, n in por_tipo.most_common()]
        resumo += ['', '### Achados', '']
        resumo += [f'- `{a}:{n}`: {m} (`{t}`)' for a, n, m, t in achados]
    if falhas:
        resumo += ['', '### O gcc falhou em', ''] + [f'- `{f}`' for f in sorted(falhas)]
    if sem_tempo:
        resumo += ['', '### Passaram do limite de tempo', ''] + [f'- `{f}`' for f in sorted(sem_tempo)]
    resumo += ['', 'Relatório completo (com o caminho de cada achado): `build/analise/relatorio.txt`.']
    texto = '\n'.join(resumo) + '\n'
    with open(os.path.join(SAIDA, 'resumo.md'), 'w', encoding='utf-8') as f:
        f.write(texto)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as f:
            f.write(texto)
    print(texto)

    if falhas:
        return 2
    if args.estrito and achados:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
