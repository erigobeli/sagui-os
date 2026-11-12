#!/usr/bin/env python3
"""
status.py - mantém o estado do projeto SaguiOS.

Fonte única da verdade: dashboard/status.json.
A partir dele este script gera:
  - dashboard/status.js   (o mesmo JSON embrulhado em JS, para o painel abrir
                           direto do disco, sem servidor: navegadores bloqueiam
                           fetch() em file://)
  - status.md             (resumo legível na raiz do repositório)
  - docs/historias/HXX.md (uma página por história, se ainda não existir)

Também recalcula dados medidos: linhas de código, tamanho da imagem e a
lista de capturas de tela.

Uso:
  status.py iniciar-historia H02
  status.py concluir-historia H02 "resultado em uma linha"
  status.py acao "texto"
  status.py erro "sintoma" "causa" "correcao"
  status.py tanenbaum "Processos" feito|parcial|a fazer
  status.py aguardar-fase 1 "mensagem de fim de fase"
  status.py confirmar-fase 1 "fase 1 ok"
  status.py bloqueio "texto"   (texto vazio limpa o bloqueio)
  status.py atualizar          (só recalcula e regenera)
"""
import json
import os
import sys
import datetime

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Qual versão do projeto: v1 (prd.md, painel em dashboard/) ou v2 (prd2.md,
# painel em dashboard/v2/). Escolhida pela variável SAGUI_PROJETO.
PROJETO = os.environ.get("SAGUI_PROJETO", "v1")
if PROJETO == "v2":
    ARQ_JSON = os.path.join(RAIZ, "dashboard", "v2", "status.json")
    ARQ_JS = os.path.join(RAIZ, "dashboard", "v2", "status.js")
    ARQ_MD = os.path.join(RAIZ, "status-v2.md")
    PASTA_HIST = os.path.join(RAIZ, "docs", "v2", "historias")
    PASTA_CAPT = os.path.join(RAIZ, "docs", "v2", "capturas")
    META_BYTES = None  # a 2.0 não tem teto de tamanho: só mede
else:
    ARQ_JSON = os.path.join(RAIZ, "dashboard", "status.json")
    ARQ_JS = os.path.join(RAIZ, "dashboard", "status.js")
    ARQ_MD = os.path.join(RAIZ, "status.md")
    PASTA_HIST = os.path.join(RAIZ, "docs", "historias")
    PASTA_CAPT = os.path.join(RAIZ, "docs", "capturas")
    META_BYTES = 1474560  # 1,44 MB, o tamanho de um disquete


def agora():
    return datetime.datetime.now().replace(microsecond=0).isoformat()


def carregar():
    with open(ARQ_JSON, encoding="utf-8") as f:
        return json.load(f)


def contar_linhas(pastas, extensoes):
    total = 0
    for pasta in pastas:
        base = os.path.join(RAIZ, pasta)
        for dirpath, _dirs, arquivos in os.walk(base):
            for nome in arquivos:
                if os.path.splitext(nome)[1] in extensoes:
                    with open(os.path.join(dirpath, nome), encoding="utf-8", errors="replace") as f:
                        total += sum(1 for _ in f)
    return total


def medir(estado):
    ext = {".c", ".h", ".asm", ".inc", ".ld", ".S"}
    estado["linhas"] = {
        "boot": contar_linhas(["boot"], ext),
        "kernel": contar_linhas(["kernel"], ext),
        "userland": contar_linhas(["userland"], ext),
        "ferramentas": contar_linhas(["tools", "tests"], {".py", ".sh"}),
    }
    # Tamanho útil = bytes realmente ocupados (MBR + cabeçalho + estágio 2 +
    # kernel + ramdisk). O build grava esse número em build/tamanho.txt.
    util = 0
    arq_tam = os.path.join(RAIZ, "build", "tamanho.txt")
    if os.path.exists(arq_tam):
        with open(arq_tam) as f:
            util = int(f.read().strip() or 0)
    img = os.path.join(RAIZ, "build", "saguios.img")
    estado["imagem"] = {
        "bytes_util": util,
        "bytes_arquivo": os.path.getsize(img) if os.path.exists(img) else 0,
        "meta_bytes": META_BYTES,
    }
    capturas = []
    if os.path.isdir(PASTA_CAPT):
        for nome in sorted(os.listdir(PASTA_CAPT)):
            if nome.lower().endswith(".png"):
                caminho = os.path.join(PASTA_CAPT, nome)
                capturas.append({"arquivo": os.path.relpath(caminho, RAIZ).replace(os.sep, "/"),
                                 "nome": nome,
                                 "data": datetime.datetime.fromtimestamp(os.path.getmtime(caminho)).replace(microsecond=0).isoformat()})
    capturas.sort(key=lambda c: c["data"], reverse=True)
    estado["capturas"] = capturas


def progresso(estado):
    hs = [h for h in estado["historias"] if not h.get("bonus")]
    feitas = sum(1 for h in hs if h["estado"] in ("feito", "aguardando teste"))
    estado["progresso_geral"] = round(100.0 * feitas / len(hs), 1) if hs else 0
    for fase in estado["fases"]:
        dela = [h for h in hs if h["fase"] == fase["numero"]]
        if not dela:   # fase bônus: conta as histórias bônus que não foram canceladas
            dela = [h for h in estado["historias"] if h["fase"] == fase["numero"] and h["estado"] != "cancelado"]
        f = sum(1 for h in dela if h["estado"] in ("feito", "aguardando teste"))
        fase["progresso"] = round(100.0 * f / len(dela), 1) if dela else 0


def gerar_md(estado):
    linhas = []
    if estado.get("mensagem_fase"):
        linhas += ["```", estado["mensagem_fase"].rstrip(), "```", ""]
    linhas += ["# Status do " + ("SaguiOS 2.0" if PROJETO == "v2" else "SaguiOS"), "",
               f"_Atualizado em {estado['atualizado']}_", "",
               f"- **Situação:** {estado['situacao']}",
               f"- **Fase atual:** {estado['fase_atual']}",
               f"- **História atual:** {estado.get('historia_atual') or '-'}",
               f"- **Progresso geral:** {estado['progresso_geral']}%",
               f"- **Bloqueio:** {estado.get('bloqueio') or 'nenhum'}",
               f"- **Imagem:** {estado['imagem']['bytes_util']} bytes úteis "
               + (f"(meta {META_BYTES} bytes = 1,44 MB)" if META_BYTES else "(sem teto: só medida)"),
               f"- **Linhas de código:** boot {estado['linhas']['boot']}, kernel {estado['linhas']['kernel']}, "
               f"userland {estado['linhas']['userland']}", "",
               "## Fases", "", "| Fase | Nome | Progresso | Estado |", "| --- | --- | --- | --- |"]
    for f in estado["fases"]:
        linhas.append(f"| {f['numero']} | {f['nome']} | {f['progresso']}% | {f['estado']} |")
    linhas += ["", "## Histórias", "", "| ID | Título | Estado | Minutos |", "| --- | --- | --- | --- |"]
    for h in estado["historias"]:
        estado_h = h['estado']
        if estado_h == "cancelado":
            estado_h = "Cancelado pelo Eli: " + h.get("resultado", "").replace("Cancelado pelo Eli: ", "")
        linhas.append(f"| {h['id']} | {h['titulo']} | {estado_h} | {h.get('minutos') or ''} |")
    linhas += ["", "## Últimas ações", ""]
    for a in estado["acoes"][:15]:
        linhas.append(f"- {a['data']}: {a['texto']}")
    linhas.append("")
    with open(ARQ_MD, "w", encoding="utf-8") as f:
        f.write("\n".join(linhas))


def gerar_paginas(estado):
    os.makedirs(PASTA_HIST, exist_ok=True)
    for h in estado["historias"]:
        arq = os.path.join(PASTA_HIST, h["id"] + ".md")
        if os.path.exists(arq):
            continue
        with open(arq, "w", encoding="utf-8") as f:
            f.write(f"# {h['id']}. {h['titulo']}\n\n**Fase:** {h['fase']}" + (" (versão 2.0)" if PROJETO == "v2" else "") + "\n\n"
                    f"## Objetivo\n\n{h['objetivo']}\n\n## Critério de aceite\n\n{h['aceite']}\n\n"
                    "## Resultado\n\n_A fazer._\n")


def salvar(estado):
    estado["atualizado"] = agora()
    medir(estado)
    progresso(estado)
    texto = json.dumps(estado, ensure_ascii=False, indent=2)
    with open(ARQ_JSON, "w", encoding="utf-8") as f:
        f.write(texto + "\n")
    with open(ARQ_JS, "w", encoding="utf-8") as f:
        f.write("// Gerado por tools/status.py a partir de status.json. Não editar.\n")
        f.write("window.SAGUI_STATUS = " + texto + ";\n")
    gerar_md(estado)
    gerar_paginas(estado)


def historia(estado, hid):
    for h in estado["historias"]:
        if h["id"].lower() == hid.lower():
            return h
    sys.exit(f"história {hid} não existe")


def registrar_acao(estado, texto):
    estado["acoes"].insert(0, {"data": agora(), "texto": texto})
    del estado["acoes"][60:]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd, args = sys.argv[1], sys.argv[2:]
    estado = carregar()
    if cmd == "iniciar-historia":
        h = historia(estado, args[0])
        h["estado"] = "em andamento"
        h["inicio"] = agora()
        estado["historia_atual"] = h["id"]
        estado["fase_atual"] = h["fase"]
        estado["situacao"] = f"Trabalhando na {h['id']}: {h['titulo']}"
        for f in estado["fases"]:
            if f["numero"] == h["fase"] and f["estado"] == "a fazer":
                f["estado"] = "em andamento"
        registrar_acao(estado, f"Início da {h['id']}: {h['titulo']}")
    elif cmd == "concluir-historia":
        h = historia(estado, args[0])
        h["estado"] = "feito"
        h["fim"] = agora()
        if h.get("inicio"):
            ini = datetime.datetime.fromisoformat(h["inicio"])
            fim = datetime.datetime.fromisoformat(h["fim"])
            h["minutos"] = max(1, round((fim - ini).total_seconds() / 60))
        if len(args) > 1:
            h["resultado"] = args[1]
        registrar_acao(estado, f"{h['id']} concluída" + (f": {args[1]}" if len(args) > 1 else ""))
    elif cmd == "acao":
        registrar_acao(estado, args[0])
    elif cmd == "erro":
        estado["erros"].insert(0, {"data": agora(), "sintoma": args[0], "causa": args[1], "correcao": args[2]})
        registrar_acao(estado, "Erro corrigido: " + args[0])
    elif cmd == "tanenbaum":
        for t in estado["tanenbaum"]:
            if t["conceito"].lower() == args[0].lower():
                t["estado"] = args[1]
                break
        else:
            sys.exit("conceito não encontrado: " + args[0])
    elif cmd == "aguardar-fase":
        n = int(args[0])
        for f in estado["fases"]:
            if f["numero"] == n:
                f["estado"] = "aguardando teste"
        for h in estado["historias"]:
            if h["fase"] == n and h["estado"] == "feito":
                h["estado"] = "aguardando teste"
        estado["situacao"] = f"Aguardando teste do Eli: fase {n}"
        estado["historia_atual"] = None
        estado["mensagem_fase"] = args[1] if len(args) > 1 else ""
        registrar_acao(estado, f"Fase {n} entregue. Aguardando teste do Eli.")
    elif cmd == "confirmar-fase":
        n = int(args[0])
        for f in estado["fases"]:
            if f["numero"] == n:
                f["estado"] = "feito"
                f["confirmado"] = agora()
        for h in estado["historias"]:
            if h["fase"] == n and h["estado"] == "aguardando teste":
                h["estado"] = "feito"
        estado["mensagem_fase"] = ""
        estado["situacao"] = f"Fase {n} confirmada no hardware. Seguindo para a fase {n + 1}."
        registrar_acao(estado, f"Eli confirmou: {args[1] if len(args) > 1 else 'fase ok'}")
    elif cmd == "bloqueio":
        estado["bloqueio"] = args[0] if args else ""
        if estado["bloqueio"]:
            registrar_acao(estado, "Bloqueio: " + estado["bloqueio"])
    elif cmd != "atualizar":
        sys.exit("comando desconhecido: " + cmd)
    salvar(estado)


if __name__ == "__main__":
    main()
