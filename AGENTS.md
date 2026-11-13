# Instruções para agentes de IA (AGENTS.md)

Este arquivo é para **assistentes de IA** (Claude Code, Codex, Cursor, Copilot,
OpenCode e outros) que forem ajudar alguém a mexer no SaguiOS. Leia tudo antes
de escrever código. As mesmas regras valem para pessoas: veja `CONTRIBUTING.md`.

## O que é o SaguiOS

Um sistema operacional x86 de 32 bits **escrito do zero**, começado pelo Claude
(Anthropic) para o Eli Rigobeli, que roda no QEMU, no VirtualBox e num Lenovo
G470 de 2011. O kernel, os drivers, a pilha de rede, o TLS, o SSH, o navegador,
o compilador C e o visual foram escritos aqui, a partir de **especificações
públicas**. O projeto é GPL 3 (`LICENSE`). O plano está em `prd.md` (versão 1)
e `prd2.md` (versão 2), e cada decisão de projeto em `docs/decisoes.md`.

## Regra principal: nada de código pronto de outros sistemas

**Não copie, não adapte e não reproduza de memória** código de outros sistemas
operacionais ou projetos, mesmo que você "lembre" dele:

- nada do **Linux**, dos BSDs, do Windows, do ReactOS, do Haiku, do MINIX, do
  SerenityOS, do ToaruOS ou de tutoriais de sistemas operacionais (OSDev wiki,
  "James Molloy", "Bran's kernel" e parecidos);
- nada de **drivers** de outros sistemas (por exemplo, o `r8152`, o `atl1c`, o
  `e1000` do Linux), nem de pilhas de rede, TLS (OpenSSL, mbed TLS, BearSSL...),
  SSH (OpenSSH, Dropbear), navegadores, motores de JavaScript, compiladores
  (GCC, Clang, TCC, chibicc) ou decodificadores de imagem e vídeo.

**Pode** (e é o jeito certo):

- escrever o código **do zero**, lendo **especificações e manuais públicos**: RFCs
  da IETF, especificações do USB-IF, manuais da Intel, datasheets dos
  fabricantes, o padrão C, a especificação ELF, a especificação do HTML (WHATWG),
  a do ECMAScript etc. Cite o documento no comentário do arquivo;
- usar **dados** de terceiros (como os certificados raiz em
  `rootfs/sistema/certificados`) quando forem indispensáveis, com licença
  compatível com a GPL 3 e uma linha em `CREDITOS.md`.

**Não pode nenhuma biblioteca pronta dentro do sistema**, mesmo com licença
livre (nada de stb_image, zlib, QuickJS, mbed TLS, lwIP, FreeType, minimp3,
etc.): tudo o que roda no SaguiOS é escrito aqui. Essa é a graça do projeto.
Fora do sistema (as ferramentas de build e de teste em `tools/` e `tests/`),
usar Python, GCC, QEMU, OpenSSL ou Pillow para conferir resultados é normal.

Se você não tem certeza se um trecho veio da sua memória de um projeto
existente, **reescreva-o** a partir da especificação e explique como pensou.

## Como o código é escrito aqui

- **Português do Brasil**: nomes de funções e variáveis, comentários, mensagens
  e documentação (`imprimir`, `abrir`, `janela`, não `print`, `open`, `window`).
- **Comentários explicam o porquê**, para um leigo curioso entender. Arquivos
  novos começam com um comentário dizendo o que fazem e de onde veio o
  conhecimento (qual especificação).
- **Sem libc** dentro do sistema: os programas usam a `libsagui`
  (`userland/inc/sagui.h`, `gui.h`); o kernel usa as próprias rotinas.
- **Sem ponto flutuante** no kernel e nos programas (`-mgeneral-regs-only`), e
  sem divisão de 64 bits que precise da libgcc.
- Siga o estilo do arquivo que você está mudando.
- Decisões de projeto importantes vão em `docs/decisoes.md` (uma seção nova,
  numerada); problemas encontrados e como foram resolvidos, em `docs/erros.md`.

## Testes

- `make` compila a imagem (`build/saguios.img`); `python3 tests/rodar_todos.py`
  roda todos os testes no QEMU. **Todo pull request precisa passar neles.**
- Funcionalidade nova vem com **teste novo** em `tests/` (veja os existentes:
  usam `tests/qemu.py` para ligar a máquina, digitar, clicar e tirar capturas).
- Não apague nem enfraqueça testes para fazer o seu código passar.

## Pull requests

- **Um assunto por pull request**, pequeno o bastante para ser revisado. Um
  pull request com dezenas de milhares de linhas não dá para revisar: quebre em
  partes.
- Cada commit com a linha **`Signed-off-by: Nome <email>`** (`git commit -s`):
  é a declaração de origem do código (DCO, a mesma do kernel do Linux), em que a
  pessoa afirma que tem direito de enviar aquele código sob a GPL 3.
- Se a IA ajudou, diga na descrição (e, se quiser, uma linha
  `Co-Authored-By:` da ferramenta).
- Preencha o modelo de pull request (`.github/pull_request_template.md`).

## Coisas que não devem ser feitas

- Não escrever no disco interno do G470 (lá estão os dados do Eli): a
  instalação em HD só acontece num disco que o usuário escolher e confirmar.
- Não colocar chaves de API, senhas ou dados pessoais em código, capturas ou
  commits.
- Não usar nomes ou marcas de jogos e produtos existentes nos jogos do sistema.
